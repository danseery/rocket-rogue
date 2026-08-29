#include "core/Content.h"
#include "core/ContentIds.h"
#include "core/CrewPresentation.h"
#include "core/FlightProgress.h"
#include "core/FlightInstrumentPresentation.h"
#include "core/GameFormat.h"
#include "core/GameMath.h"
#include "core/GameState.h"
#include "core/HangarPresentation.h"
#include "core/InventoryPresentation.h"
#include "core/LaunchPresentation.h"
#include "core/LaunchReadinessPresentation.h"
#include "core/LaunchSimulation.h"
#include "core/MiniDroneCoordination.h"
#include "core/MiningSystem.h"
#include "core/MiningPresentation.h"
#include "core/PostSolarSystem.h"
#include "core/OutcomePresentation.h"
#include "core/PanelChromePresentation.h"
#include "core/ProgramPresentation.h"
#include "core/RefitPresentation.h"
#include "core/ResearchPresentation.h"
#include "core/ResearchSystem.h"
#include "core/ScenarioSystem.h"
#include "core/SaveData.h"
#include "core/SaveSchema.h"
#include "core/ShipPresentation.h"
#include "core/SurfaceScanPresentation.h"
#include "core/Tuning.h"
#include "core/GameUi.h"
#include "game/GamePanel.h"
#include "render/RenderSnapshot.h"

#include <cassert>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace rocket;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        // Exit normally through the test harness instead of opening the
        // platform crash dialog. This keeps failures observable in automated
        // native and WebAssembly runs.
        std::exit(3);
    }
}

void require(bool condition, const std::string& message)
{
    require(condition, message.c_str());
}

const DetailPresentationRow* findDetailPresentationRow(const std::vector<DetailPresentationRow>& rows, std::string_view label);
bool hasDetailPresentationHeader(const std::vector<DetailPresentationRow>& rows, std::string_view label);

std::size_t countOccurrences(std::string_view text, std::string_view needle)
{
    std::size_t count = 0;
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string_view::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

std::string panelTestEscape(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '&': escaped += "&amp;"; break;
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        case '"': escaped += "&quot;"; break;
        default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

// Existing content assertions intentionally inspect one flat test string.
// Production consumes the structured presentation and never serializes typed
// modals back into the retired template[data-modal] transport.
std::string buildGamePanelHtml(const PanelRenderContext& context)
{
    const PanelDocumentPresentation presentation = buildGamePanelPresentation(context);
    std::string markup = presentation.contentMarkup;
    for (const ModalPresentation& modal : presentation.modals) {
        markup += "<template data-modal=\"" + panelTestEscape(modal.id) + "\"";
        if (modal.autoOpen) {
            markup += " data-auto-modal=\"1\" data-modal-dismissible=\"";
            markup += modal.dismissible ? "1" : "0";
            markup += "\" data-modal-close-action=\"" + panelTestEscape(modal.closeAction)
                + "\" data-title=\"" + panelTestEscape(modal.title) + "\"";
        } else {
            markup += " data-title=\"" + panelTestEscape(modal.title) + "\"";
            if (!modal.dismissible) {
                markup += " data-modal-dismissible=\"0\"";
            }
        }
        if (!modal.showClose) {
            markup += " data-modal-hide-close=\"1\"";
        }
        markup += ">" + modal.bodyMarkup + "</template>";
    }
    return markup;
}

void activateOnlyCrew(GameState& state, std::string_view id)
{
    for (Astronaut& astronaut : state.run.crew) {
        astronaut.status = astronaut.id == id ? CrewStatus::Active : CrewStatus::Dead;
    }
}

void prepareMiningSiteForTest(GameState& state)
{
    state.run.surfaceExpedition.miningSitePrepared = true;
}

void clearMiningTerrainForEvaTest(MiningRunState& mining)
{
    for (MiningCell& cell : mining.terrain.cells) {
        cell = {};
        cell.revealed = true;
    }
    std::fill(
        mining.terrain.dirtyChunks.begin(),
        mining.terrain.dirtyChunks.end(),
        0);
    mining.enemies.clear();
    mining.gate = {};
    mining.gravityStrength = 0.0;
    mining.oxygenSeconds = 1000.0;
}

GameState activeMiningStateForEvaTest(
    const ContentCatalog& catalog,
    std::uint64_t seed,
    int destinationIndex = 2,
    int difficulty = 4)
{
    GameState state = createNewGame(catalog, seed);
    state.run.destinationIndex = destinationIndex;
    startSurfaceExpedition(state, catalog);
    require(
        state.run.surfaceExpedition.active,
        "EVA test setup should start a surface expedition");
    prepareMiningSiteForTest(state);
    require(
        startMiningRun(
            state,
            catalog,
            {MiningAct::ActOne, difficulty, seed},
            false)
            .applied,
        "EVA test setup should start a mining run");
    clearMiningTerrainForEvaTest(state.run.mining);
    return state;
}

GameState configuredState(const ContentCatalog& catalog, int destinationIndex, double targetMultiplier)
{
    GameState state = createNewGame(catalog, 12345);
    state.run.destinationIndex = destinationIndex;
    syncLaunchConfig(state, catalog);
    state.launchConfig.burnGoalMultiplier = targetMultiplier;
    return state;
}

bool nearlyEqual(double lhs, double rhs, double tolerance = 0.000001)
{
    return std::abs(lhs - rhs) <= tolerance;
}

std::string offerKeyAt(const GameState& state, std::size_t index)
{
    if (!state.run.offerModuleIds[index].empty()) {
        return "module:" + state.run.offerModuleIds[index];
    }
    if (!state.run.offerCrewUpgradeIds[index].empty()) {
        return "crew:" + state.run.offerCrewUpgradeIds[index];
    }
    return "";
}

const Destination& launchDestination(
    const ContentCatalog& catalog,
    std::string_view destinationId)
{
    const Destination* destination = catalog.findDestination(destinationId);
    require(destination != nullptr, "launch curriculum test destination must exist");
    return *destination;
}

PreparedLaunch preparedCurriculumLaunch(
    const ContentCatalog& catalog,
    std::string_view destinationId,
    LaunchMissionKind missionKind,
    bool frontierTransfer,
    int fuelRank,
    int controlRank,
    int coolingRank,
    int hullRank,
    std::uint64_t seed)
{
    GameState state = createNewGame(catalog, seed);
    state.launchConfig.destinationId = std::string(destinationId);
    state.launchConfig.missionKind = missionKind;
    state.launchConfig.frontierTransfer = frontierTransfer;
    const Destination& destination = launchDestination(catalog, destinationId);
    state.launchConfig.burnGoalMultiplier = frontierTransfer || missionKind == LaunchMissionKind::Standard
        ? destination.targetMultiplier
        : 1.0 + (destination.targetMultiplier - 1.0) *
            tuning::launchProgression::calibrationTargetShare;
    state.meta.launchUpgrades = {fuelRank, controlRank, coolingRank, hullRank};
    Random rng(seed);
    return prepareLaunch(state, catalog, rng);
}

int openAsteroidLane(const PreparedLaunch& launch, int row)
{
    std::array<bool, tuning::launch::asteroidLaneCount> blocked {};
    const int firstIndex = row * (tuning::launch::asteroidLaneCount - 1);
    const int endIndex = std::min(
        launch.asteroidCount,
        firstIndex + tuning::launch::asteroidLaneCount - 1);
    for (int index = firstIndex; index < endIndex; ++index) {
        const LaunchAsteroid& asteroid = launch.asteroids[static_cast<std::size_t>(index)];
        int closestLane = 0;
        double closestDistance = std::abs(
            asteroid.courseOffset - launchAsteroidLaneOffset(0));
        for (int lane = 0; lane < tuning::launch::asteroidLaneCount; ++lane) {
            const double distance = std::abs(
                asteroid.courseOffset - launchAsteroidLaneOffset(lane));
            if (distance < closestDistance) {
                closestDistance = distance;
                closestLane = lane;
            }
        }
        blocked[static_cast<std::size_t>(closestLane)] = true;
    }
    for (int lane = 0; lane < tuning::launch::asteroidLaneCount; ++lane) {
        if (!blocked[static_cast<std::size_t>(lane)]) {
            return lane;
        }
    }
    return -1;
}

LaunchFlightStep flyCompetentPolicy(
    LaunchFlightState& flight,
    const PreparedLaunch& launch,
    const Destination& destination,
    bool avoidAsteroids,
    int maximumSteps = 12000)
{
    bool cooling = false;
    LaunchFlightStep step;
    for (int index = 0; index < maximumSteps && flight.active; ++index) {
        if (launch.heatEnabled) {
            if (flight.heat >= 0.68) {
                cooling = true;
            } else if (flight.heat <= 0.44) {
                cooling = false;
            }
        }

        double targetCourse = 0.0;
        if (avoidAsteroids && launch.asteroidsEnabled && !flight.returningHome) {
            for (int row = 0; row < tuning::launch::asteroidRowCount; ++row) {
                const int firstIndex = row * (tuning::launch::asteroidLaneCount - 1);
                const int endIndex = std::min(
                    launch.asteroidCount,
                    firstIndex + tuning::launch::asteroidLaneCount - 1);
                double rowClearProgress = 0.0;
                for (int asteroidIndex = firstIndex; asteroidIndex < endIndex; ++asteroidIndex) {
                    const LaunchAsteroid& asteroid =
                        launch.asteroids[static_cast<std::size_t>(asteroidIndex)];
                    rowClearProgress = std::max(
                        rowClearProgress,
                        asteroid.routeProgress +
                            (asteroid.radius + tuning::launch::asteroidShipRadius) /
                                tuning::launch::asteroidRouteAxisScale);
                }
                if (rowClearProgress < flight.travelProgress) {
                    continue;
                }
                const int lane = openAsteroidLane(launch, row);
                require(lane >= 0, "every asteroid row must expose an open lane");
                targetCourse = launchAsteroidLaneOffset(lane);
                break;
            }
        }

        LaunchControlInput input;
        input.steer = std::clamp(
            (targetCourse - flight.courseOffset) * 5.5 -
                flight.courseVelocity * 2.4,
            -1.0,
            1.0);
        input.enginesCut = cooling;
        step = updateLaunchFlight(
            flight,
            launch,
            destination,
            input,
            0.04);
        if (step.failed || step.reachedDestination || step.reachedHome) {
            return step;
        }
    }
    return step;
}

void launchCurriculumFuelMathAndRange()
{
    const ContentCatalog catalog = createDefaultContent();
    const Destination& moon = launchDestination(catalog, content::destination::moon);

    GameState fresh = createNewGame(catalog, 1101);
    require(fresh.run.credits == 0.0, "fresh launch curriculum should start at zero credits");
    require(fresh.run.destinationIndex == 0, "Earth remains only the hidden compatibility origin");
    require(catalog.destinations[0].hiddenFromProgression,
        "the internal Earth origin must never be presented as a mission");
    require(fresh.launchConfig.destinationId == content::destination::moon,
        "a new campaign must immediately target the Moon");
    require(fresh.launchConfig.missionKind == LaunchMissionKind::FuelCalibration,
        "the first launch should be the fuel survey");
    require(launchMissionReady(fresh) && !canCommitToNextFrontier(fresh, catalog),
        "the fuel lesson should be ready while the Moon transfer remains locked");

    Random rng(1101);
    const PreparedLaunch survey = prepareLaunch(fresh, catalog, rng);
    require(nearlyEqual(survey.fuelCapacity, 10.0), "base launch tank should hold 10 fuel");
    require(nearlyEqual(survey.cruiseFuelCost, 10.0), "Moon transit should cost 10 fuel");
    require(nearlyEqual(
                0.5 * survey.cruiseFuelCost *
                    launchFuelUseMultiplier(tuning::launch::calibratedThrottle),
                5.0),
        "the survey marker must consume exactly five fuel at calibrated throttle");
    require(!survey.manualControlsEnabled && !survey.heatEnabled && !survey.asteroidsEnabled,
        "the first lesson must hide steering, temperature, and hull mechanics");

    LaunchFlightState surveyFlight = beginLaunchFlight(survey, moon);
    LaunchFlightStep surveyStep;
    double surveyOutboundSeconds = 0.0;
    for (int index = 0; index < 30000 && surveyFlight.travelProgress < 0.5; ++index) {
        surveyStep = updateLaunchFlight(
            surveyFlight,
            survey,
            moon,
            {1.0, 1.0, true},
            0.001);
        surveyOutboundSeconds += 0.001;
        require(!surveyStep.failed, "the fixed first-flight policy must reach its survey marker");
        require(nearlyEqual(surveyFlight.courseOffset, 0.0),
            "first-flight movement input must be ignored");
    }
    require(nearlyEqual(surveyFlight.fuelRemaining, 5.0, 0.002),
        "the halfway warning should leave exactly the five fuel needed to return");
    require(calibrationFuelWarning(survey, surveyFlight) == CalibrationFuelWarning::TurnAround,
        "the fuel survey should enter the shared turnaround warning at its halfway marker");
    require(surveyOutboundSeconds >= 8.0,
        "the first Fuel Survey pass should move slowly enough to read and react to each warning");
    beginLaunchReturn(surveyFlight);
    require(surveyFlight.fuelSurveyReturnTiming == FuelSurveyReturnTiming::Timely,
        "turning at the marker should latch a timely return");
    for (int index = 0; index < 30000 && surveyFlight.active; ++index) {
        surveyStep = updateLaunchFlight(surveyFlight, survey, moon, {}, 0.001);
    }
    require(surveyStep.reachedHome && !surveyStep.failed,
        "the exact five-out/five-back survey must resolve home before fuel failure");
    require(nearlyEqual(surveyFlight.fuelRemaining, 0.0, 0.002),
        "the calibrated survey should arrive home on the displayed reserve");

    LaunchFlightState lateTurnFlight = beginLaunchFlight(survey, moon);
    LaunchFlightStep lateTurnStep;
    for (int index = 0; index < 30000 && lateTurnFlight.travelProgress < 0.90; ++index) {
        lateTurnStep = updateLaunchFlight(lateTurnFlight, survey, moon, {}, 0.001);
        require(!lateTurnStep.failed,
            "the fuel lesson should allow the player to continue experimenting before turning around");
    }
    require(lateTurnFlight.fuelRemaining > 0.0 && lateTurnFlight.fuelRemaining < 2.0,
        "the late-turn tolerance test must begin with only a small amount of fuel left");
    require(lateTurnFlight.fuelSurveyLateLatched,
        "crossing 30 percent fuel outbound should latch the late-return state");
    beginLaunchReturn(lateTurnFlight);
    require(lateTurnFlight.fuelSurveyReturnTiming == FuelSurveyReturnTiming::Late,
        "a red-zone turnaround should preserve its late classification");
    for (int index = 0; index < 30000 && lateTurnFlight.active; ++index) {
        lateTurnStep = updateLaunchFlight(lateTurnFlight, survey, moon, {}, 0.001);
    }
    require(lateTurnStep.reachedHome && !lateTurnStep.failed,
        "turning around anywhere before the first tank is empty must safely complete the fuel lesson");
    require(nearlyEqual(lateTurnFlight.fuelRemaining, 0.0, 0.002),
        "the protected tutorial return should consume the remaining fuel across the trip home");

    const PreparedLaunch controlsWarningLaunch = preparedCurriculumLaunch(
        catalog,
        content::destination::moon,
        LaunchMissionKind::FlightControlsCalibration,
        false,
        1,
        0,
        0,
        0,
        1105);
    LaunchFlightState controlsWarningFlight = beginLaunchFlight(controlsWarningLaunch, moon);
    controlsWarningFlight.fuelRemaining = controlsWarningFlight.fuelCapacity *
        tuning::launchProgression::fuelSurveyTargetFuelShare;
    require(calibrationFuelWarning(controlsWarningLaunch, controlsWarningFlight) ==
            CalibrationFuelWarning::TurnAround,
        "the controls calibration must use the same turnaround fuel warning as the survey");
    controlsWarningFlight.fuelRemaining = controlsWarningFlight.fuelCapacity *
        tuning::launchProgression::fuelSurveyLateFuelShare;
    require(calibrationFuelWarning(controlsWarningLaunch, controlsWarningFlight) ==
            CalibrationFuelWarning::Critical,
        "the controls calibration must use the survey's critical fuel warning threshold");

    GameState partialState = createNewGame(catalog, 1103);
    Random partialPrepareRng(1103);
    const PreparedLaunch partialSurvey = prepareLaunch(
        partialState,
        catalog,
        partialPrepareRng);
    const double partialMarker = 1.0 +
        (partialSurvey.config.burnGoalMultiplier - 1.0) * 0.40;
    Random partialResolveRng(1104);
    const LaunchOutcome partialReturn = resolveLaunch(
        partialSurvey,
        catalog,
        partialState,
        partialMarker,
        RecoveryMethod::ReturnHome,
        partialResolveRng,
        {true, LaunchFailureCause::None, 1.0, 0});
    const double partialNet = partialReturn.payout - partialReturn.recoveryCost;
    require(partialNet > 0.0 &&
            partialNet < tuning::launchProgression::lessonReward,
        "a positive pre-marker Fuel Survey return must pay partial data credits, never the full 22-credit lesson reward");
    require(partialReturn.blueprintGain == 0,
        "an incomplete Fuel Survey must not leak blueprint progress");
    applyLaunchOutcome(partialState, catalog, partialReturn);
    require(nearlyEqual(partialState.run.credits, partialNet) &&
            partialState.meta.launchLessons.stage == LaunchTrainingStage::FuelCalibration &&
            !partialState.run.refitEntitled,
        "a paid pre-marker return must leave the Fuel lesson incomplete with no upgrade entitlement");

    const PreparedLaunch moonTransfer = preparedCurriculumLaunch(
        catalog,
        content::destination::moon,
        LaunchMissionKind::Standard,
        true,
        1,
        1,
        0,
        0,
        1102);
    require(nearlyEqual(moonTransfer.fuelCapacity, 15.0) &&
            nearlyEqual(moonTransfer.arrivalReserveFuel, 0.0),
        "Fuel Tanks I must expose a 15-fuel Moon route with no hidden landing reserve");
    LaunchFlightState arrivalFlight = beginLaunchFlight(moonTransfer, moon);
    const LaunchFlightStep arrival = flyCompetentPolicy(
        arrivalFlight,
        moonTransfer,
        moon,
        false);
    if (!arrival.reachedDestination || arrival.failed) {
        std::cerr << "Moon transfer diagnostic: cause=" << static_cast<int>(arrival.failureCause)
                  << " progress=" << arrivalFlight.travelProgress
                  << " fuel=" << arrivalFlight.fuelRemaining
                  << " course=" << arrivalFlight.courseOffset << "\n";
    }
    require(arrival.reachedDestination && !arrival.failed,
        "15 fuel must complete the calibrated Moon transit");
    require(arrivalFlight.fuelRemaining > 0.0,
        "landing must retain the fuel left after transit instead of consuming a hidden insertion reserve");

    LaunchFlightState tolerantArrival = beginLaunchFlight(moonTransfer, moon);
    tolerantArrival.travelProgress = 1.0;
    tolerantArrival.previousTravelProgress = 1.0;
    tolerantArrival.fuelRemaining = 3.0;
    const LaunchFlightStep tolerantArrivalStep = updateLaunchFlight(
        tolerantArrival,
        moonTransfer,
        moon,
        {},
        0.04);
    require(tolerantArrivalStep.reachedDestination && !tolerantArrivalStep.failed &&
            nearlyEqual(tolerantArrival.fuelRemaining, 3.0),
        "a Moon transfer arriving with fuel remaining must land without a minimum-fuel gate");

    LaunchFlightState insufficientArrival = beginLaunchFlight(moonTransfer, moon);
    insufficientArrival.travelProgress = 1.0;
    insufficientArrival.previousTravelProgress = 1.0;
    insufficientArrival.fuelRemaining = 0.1;
    const LaunchFlightStep insufficientArrivalStep = updateLaunchFlight(
        insufficientArrival,
        moonTransfer,
        moon,
        {},
        0.04);
    require(insufficientArrivalStep.reachedDestination && !insufficientArrivalStep.failed &&
            nearlyEqual(insufficientArrival.fuelRemaining, 0.1),
        "any positive fuel remaining at the destination must land without consuming an insertion reserve");

    LaunchFlightState dryAtTouchdown = beginLaunchFlight(moonTransfer, moon);
    dryAtTouchdown.travelProgress = 1.0;
    dryAtTouchdown.previousTravelProgress = 1.0;
    dryAtTouchdown.fuelRemaining = 0.0;
    const LaunchFlightStep dryAtTouchdownStep = updateLaunchFlight(
        dryAtTouchdown,
        moonTransfer,
        moon,
        {},
        0.04);
    require(dryAtTouchdownStep.reachedDestination && !dryAtTouchdownStep.failed,
        "transfer fuel reaching zero exactly at touchdown must still complete the arrival");

    LaunchFlightState dryBeforeArrival = beginLaunchFlight(moonTransfer, moon);
    dryBeforeArrival.travelProgress = 0.75;
    dryBeforeArrival.previousTravelProgress = 0.75;
    dryBeforeArrival.fuelRemaining = 0.0;
    const LaunchFlightStep dryBeforeArrivalStep = updateLaunchFlight(
        dryBeforeArrival,
        moonTransfer,
        moon,
        {},
        0.04);
    require(dryBeforeArrivalStep.failed &&
            dryBeforeArrivalStep.failureCause == LaunchFailureCause::FuelExhausted,
        "transfer fuel exhausted before the destination must still fail the flight");

    FlightActionState outboundActions;
    tolerantArrival.fuelRemaining = 3.0;
    tolerantArrival.fuelFailureSeconds = 0.0;
    tolerantArrival.projectedFuelReserve = 0.0;
    require(
        launchStatusMessage(moonTransfer, tolerantArrival, outboundActions).find(
            "FUEL LOW") == std::string::npos,
        "a destination arrival with fuel remaining should not expose an internal reserve target");
    insufficientArrival.fuelRemaining = 0.1;
    insufficientArrival.fuelFailureSeconds = 0.0;
    insufficientArrival.projectedFuelReserve = -1.1;
    require(
        launchStatusMessage(moonTransfer, insufficientArrival, outboundActions).find(
            "FUEL LOW") != std::string::npos,
        "the HUD must plainly tell the player to reduce throttle when the route projection is short on fuel");

    require(nearlyEqual(launchFuelUseMultiplier(0.60), 1.0),
        "60 percent throttle must be the calibrated fuel baseline");
    require(launchFuelUseMultiplier(0.18) < 1.0 &&
            launchFuelUseMultiplier(1.0) > 1.5,
        "low throttle must extend range while full throttle spends fuel nonlinearly");

    PreparedLaunch strandedLaunch = moonTransfer;
    LaunchFlightState stranded = beginLaunchFlight(strandedLaunch, moon);
    stranded.selectedThrottle = 1.0;
    LaunchFlightStep strandedStep;
    for (int index = 0; index < 12000 && stranded.active; ++index) {
        strandedStep = updateLaunchFlight(stranded, strandedLaunch, moon, {}, 0.04);
        if (strandedStep.reachedDestination) {
            break;
        }
    }
    require(strandedStep.failed &&
            strandedStep.failureCause == LaunchFailureCause::FuelExhausted,
        "prolonged full throttle must strand an exactly provisioned early ship");
}

void launchControlsAreSeededCorrectableAndImproveByRank()
{
    const ContentCatalog catalog = createDefaultContent();
    const Destination& moon = launchDestination(catalog, content::destination::moon);
    const PreparedLaunch first = preparedCurriculumLaunch(
        catalog,
        content::destination::moon,
        LaunchMissionKind::FlightControlsCalibration,
        false,
        1,
        0,
        0,
        0,
        2201);
    const PreparedLaunch repeated = preparedCurriculumLaunch(
        catalog,
        content::destination::moon,
        LaunchMissionKind::FlightControlsCalibration,
        false,
        1,
        0,
        0,
        0,
        2201);
    require(nearlyEqual(
                first.controlSteeringResponseVariation,
                repeated.controlSteeringResponseVariation),
        "control response variation must repeat for the same launch seed");
    require(std::abs(first.controlSteeringResponseVariation) <=
            tuning::launch::controlSteeringResponseVariance * first.controlChaos + 0.000001,
        "seeded steering-response variance must remain inside the documented +/-20 percent chaos band");
    for (int index = 0; index < first.controlKickCount; ++index) {
        require(nearlyEqual(
                    first.controlKickDirections[static_cast<std::size_t>(index)],
                    repeated.controlKickDirections[static_cast<std::size_t>(index)]),
            "seeded throttle kicks must repeat exactly");
    }

    LaunchFlightState unmanaged = beginLaunchFlight(first, moon);
    LaunchFlightStep unmanagedStep;
    for (int index = 0; index < 4000 && unmanaged.active &&
         unmanaged.travelProgress < tuning::launchProgression::calibrationTargetShare; ++index) {
        unmanagedStep = updateLaunchFlight(unmanaged, first, moon, {}, 0.04);
    }
    require(unmanagedStep.failed &&
            unmanagedStep.failureCause == LaunchFailureCause::TrainingRescue &&
            unmanaged.travelProgress < tuning::launchProgression::calibrationTargetShare,
        "ignoring the seeded startup drift at base controls must lose course before the calibration marker");

    LaunchFlightState corrected = beginLaunchFlight(first, moon);
    LaunchFlightStep correctedStep;
    for (int index = 0; index < 4000 && corrected.active &&
         corrected.travelProgress < tuning::launchProgression::calibrationTargetShare; ++index) {
        const double steer = std::clamp(
            -corrected.courseOffset * 5.5 - corrected.courseVelocity * 2.4,
            -1.0,
            1.0);
        correctedStep = updateLaunchFlight(corrected, first, moon, {steer, 0.0, false}, 0.04);
    }
    require(corrected.active && !correctedStep.failed,
        "active correction must recover the same seeded Rank-0 flight to its calibration marker");
    beginLaunchReturn(corrected);
    for (int index = 0; index < 4000 && corrected.active; ++index) {
        const double steer = std::clamp(
            -corrected.courseOffset * 5.5 - corrected.courseVelocity * 2.4,
            -1.0,
            1.0);
        correctedStep = updateLaunchFlight(corrected, first, moon, {steer, 0.0, false}, 0.04);
    }
    require(correctedStep.reachedHome && !correctedStep.failed,
        "a pilot who corrects the startup drift must be able to bank the controls calibration safely");

    PreparedLaunch neutral = first;
    neutral.controlSteeringResponseVariation = 0.0;
    neutral.controlKickCount = 0;
    LaunchFlightState left = beginLaunchFlight(neutral, moon);
    LaunchFlightState right = beginLaunchFlight(neutral, moon);
    updateLaunchFlight(left, neutral, moon, {-1.0, 0.0, false}, 0.08);
    updateLaunchFlight(right, neutral, moon, {1.0, 0.0, false}, 0.08);
    require(left.courseVelocity < 0.0 && right.courseVelocity > 0.0,
        "left and right steering must always honor the player's requested direction");
    require(right.courseVelocity > std::abs(left.courseVelocity),
        "base controls should teach visible right-side overshoot without reversing input");
    require(nearlyEqual(
                right.courseVelocity / std::abs(left.courseVelocity),
                1.0 + tuning::launch::controlRightOvershoot,
                0.000001),
        "base right steering must use the documented 1 + 0.45c oversteer gain");

    const std::array<double, 4> expectedChaos {1.00, 0.55, 0.20, 0.00};
    double previousKick = 100.0;
    for (int rank = 0; rank <= 3; ++rank) {
        require(nearlyEqual(launchControlChaosForRank(rank), expectedChaos[static_cast<std::size_t>(rank)]),
            "control chaos ranks must use the documented monotonic values");
        PreparedLaunch ranked = first;
        ranked.flightControlRank = rank;
        ranked.controlChaos = launchControlChaosForRank(rank);
        ranked.controlSteeringResponseVariation = 0.0;
        ranked.controlKickDirections[0] = 1.0;
        ranked.controlKickCount = 1;
        LaunchFlightState flight = beginLaunchFlight(ranked, moon);
        require(nearlyEqual(
                    std::abs(flight.courseVelocity),
                    tuning::launch::controlStartupDrift * ranked.controlChaos),
            "startup drift must scale exactly with the current control-chaos rank");
        updateLaunchFlight(flight, ranked, moon, {0.0, 1.0, false}, 0.08);
        const double kick = std::abs(flight.courseVelocity);
        require(kick <= previousKick + 0.000001,
            "each Flight Controls rank must reduce acceleration kick");
        previousKick = kick;
        if (rank == 3) {
            require(nearlyEqual(kick, 0.0), "Rank III controls should be calm and stable");
        }
    }

    PreparedLaunch exactKickLaunch = first;
    exactKickLaunch.controlSteeringResponseVariation = 0.0;
    exactKickLaunch.controlKickDirections[0] = 1.0;
    LaunchFlightState exactKick = beginLaunchFlight(exactKickLaunch, moon);
    exactKick.courseOffset = 0.0;
    exactKick.courseVelocity = 0.0;
    exactKick.nextControlKickIndex = 0;
    exactKick.throttleKickCooldownSeconds = 0.0;
    exactKick.throttleAtLastKick = exactKick.selectedThrottle -
        tuning::launch::controlThrottleKickThreshold;
    updateLaunchFlight(exactKick, exactKickLaunch, moon, {0.0, 1.0, false}, 0.08);
    const double expectedKick = tuning::launch::controlThrottleKick *
        (1.0 - tuning::launch::controlDampingMinimum * 0.08);
    require(nearlyEqual(exactKick.courseVelocity, expectedKick, 0.000001),
        "a seeded acceleration pulse must apply the documented +/-0.35c kick before damping");

    PreparedLaunch cooldownLaunch = first;
    cooldownLaunch.controlSteeringResponseVariation = 0.0;
    LaunchFlightState cooldown = beginLaunchFlight(cooldownLaunch, moon);
    updateLaunchFlight(cooldown, cooldownLaunch, moon, {0.0, 1.0, false}, 0.04);
    require(cooldown.nextControlKickIndex == 1, "a throttle rise should consume one seeded kick");
    updateLaunchFlight(cooldown, cooldownLaunch, moon, {0.0, 0.0, false}, 0.04);
    updateLaunchFlight(cooldown, cooldownLaunch, moon, {0.0, 1.0, false}, 0.04);
    require(cooldown.nextControlKickIndex == 1,
        "re-pressing throttle inside 0.35 seconds must not create frame-noise kicks");
    for (int index = 0; index < 9; ++index) {
        updateLaunchFlight(cooldown, cooldownLaunch, moon, {0.0, 0.0, false}, 0.04);
    }
    updateLaunchFlight(cooldown, cooldownLaunch, moon, {0.0, 1.0, false}, 0.04);
    updateLaunchFlight(cooldown, cooldownLaunch, moon, {0.0, 1.0, false}, 0.04);
    require(cooldown.nextControlKickIndex == 2,
        "a new throttle rise after the cooldown should consume the next seeded kick");

    PreparedLaunch recoveryLaunch = first;
    recoveryLaunch.controlSteeringResponseVariation = 0.0;
    recoveryLaunch.controlKickCount = 0;
    LaunchFlightState recovery = beginLaunchFlight(recoveryLaunch, moon);
    recovery.courseOffset = tuning::launch::pilotingCourseCaution;
    recovery.courseVelocity = 0.25;
    LaunchFlightStep recoveryStep;
    for (int index = 0; index < 100 && std::abs(recovery.courseOffset) >=
            tuning::launch::pilotingCourseSafe; ++index) {
        const double steer = recovery.courseOffset > 0.0 ? -1.0 : 1.0;
        recoveryStep = updateLaunchFlight(
            recovery,
            recoveryLaunch,
            moon,
            {steer, 0.0, false},
            0.04);
    }
    require(!recoveryStep.failed &&
            std::abs(recovery.courseOffset) < tuning::launch::pilotingCourseSafe,
        "a caution-level overshoot must be correctable before the course-loss timer expires");
}

void launchThermalManagementIsPlayerDriven()
{
    const ContentCatalog catalog = createDefaultContent();
    const Destination& mars = launchDestination(catalog, content::destination::mars);
    const std::array<double, 4> heatMultipliers {1.00, 0.88, 0.76, 0.64};
    const std::array<double, 4> coolingRates {0.10, 0.14, 0.18, 0.22};
    for (int rank = 0; rank <= 3; ++rank) {
        require(nearlyEqual(
                    launchPoweredHeatMultiplierForRank(rank),
                    heatMultipliers[static_cast<std::size_t>(rank)]) &&
                nearlyEqual(
                    launchEngineOffCoolingForRank(rank),
                    coolingRates[static_cast<std::size_t>(rank)]),
            "Cooling ranks must match the published powered-heat and engine-off values");
    }

    PreparedLaunch launch = preparedCurriculumLaunch(
        catalog,
        content::destination::mars,
        LaunchMissionKind::ThermalManagement,
        true,
        2,
        2,
        0,
        0,
        3301);
    LaunchFlightState cooling = beginLaunchFlight(launch, mars);
    cooling.heat = 0.90;
    const double before = cooling.heat;
    updateLaunchFlight(cooling, launch, mars, {0.0, 0.0, true}, 0.08);
    require(cooling.heat < before &&
            nearlyEqual(before - cooling.heat, 0.10 * 0.08, 0.000001),
        "base engines-off cooling must be deterministic and never overpowered");

    LaunchFlightState coast = beginLaunchFlight(launch, mars);
    for (int index = 0; index < 60; ++index) {
        updateLaunchFlight(coast, launch, mars, {}, 0.04);
    }
    const double coastStart = coast.travelProgress;
    for (int index = 0; index < 50; ++index) {
        updateLaunchFlight(coast, launch, mars, {0.0, 0.0, true}, 0.04);
    }
    require(coast.travelVelocity == 0.0,
        "cut engines should coast to a full stop in roughly 1.5 seconds");
    const double stoppedProgress = coast.travelProgress;
    for (int index = 0; index < 50; ++index) {
        updateLaunchFlight(coast, launch, mars, {0.0, 0.0, true}, 0.04);
    }
    require(nearlyEqual(coast.travelProgress, stoppedProgress),
        "engine-off coasting must not complete the route for free");
    require(stoppedProgress > coastStart, "engine cuts should preserve a brief, visible coast");

    LaunchFlightState managed = beginLaunchFlight(launch, mars);
    const LaunchFlightStep managedResult = flyCompetentPolicy(managed, launch, mars, false);
    require(managedResult.reachedDestination && !managedResult.failed,
        "a skilled pilot must be able to reach Mars with base cooling");

    LaunchFlightState reckless = beginLaunchFlight(launch, mars);
    reckless.selectedThrottle = 1.0;
    LaunchFlightStep recklessResult;
    for (int index = 0; index < 12000 && reckless.active; ++index) {
        recklessResult = updateLaunchFlight(reckless, launch, mars, {}, 0.04);
        if (recklessResult.reachedDestination) {
            break;
        }
    }
    require(recklessResult.failed &&
            recklessResult.failureCause == LaunchFailureCause::ThermalRunaway,
        "sustained full throttle through temperature warnings must fail before Mars");

    PreparedLaunch qualification = launch;
    qualification.config.frontierTransfer = false;
    qualification.trainingMission = true;
    qualification.manualControlsEnabled = false;
    LaunchFlightState ignoredQualification = beginLaunchFlight(qualification, mars);
    ignoredQualification.heat = 1.0;
    LaunchFlightStep qualificationFailure;
    for (int index = 0; index < 100 && ignoredQualification.active; ++index) {
        qualificationFailure = updateLaunchFlight(
            ignoredQualification,
            qualification,
            mars,
            {},
            0.04);
    }
    require(qualificationFailure.failed &&
            qualificationFailure.failureCause == LaunchFailureCause::ThermalRunaway,
        "ignoring heat during the Mars qualification must retain the explicit Thermal Runaway cause");
}

void launchCurriculumResolutionHasNoHiddenDamageOrBlueprintLeaks()
{
    const ContentCatalog catalog = createDefaultContent();

    const auto requireDamageFreeSuccess = [&](PreparedLaunch launch,
                                              RecoveryMethod method,
                                              std::uint64_t seed,
                                              std::string_view label) {
        GameState state = createNewGame(catalog, seed);
        state.launchConfig = launch.config;
        Random resolveRng(seed + 10000);
        const LaunchOutcome outcome = resolveLaunch(
            launch,
            catalog,
            state,
            launch.config.burnGoalMultiplier,
            method,
            resolveRng,
            {true, LaunchFailureCause::None, 1.0, 0});
        require(outcome.type != LaunchResultType::Destroyed &&
                outcome.shipDamage == 0,
            std::string(label) + " must resolve with zero hidden ship damage when no asteroid was hit");
        applyLaunchOutcome(state, catalog, outcome);
        require(state.run.shipDamage == 0,
            std::string(label) + " must apply without adding hidden ship damage");
    };

    requireDamageFreeSuccess(
        preparedCurriculumLaunch(
            catalog,
            content::destination::moon,
            LaunchMissionKind::FuelCalibration,
            false,
            0,
            0,
            0,
            0,
            3310),
        RecoveryMethod::ReturnHome,
        3310,
        "Fuel Survey");
    requireDamageFreeSuccess(
        preparedCurriculumLaunch(
            catalog,
            content::destination::moon,
            LaunchMissionKind::FlightControlsCalibration,
            false,
            1,
            0,
            0,
            0,
            3311),
        RecoveryMethod::ReturnHome,
        3311,
        "Flight Controls calibration");
    requireDamageFreeSuccess(
        preparedCurriculumLaunch(
            catalog,
            content::destination::moon,
            LaunchMissionKind::Standard,
            true,
            1,
            1,
            0,
            0,
            3312),
        RecoveryMethod::TransferArrival,
        3312,
        "Moon transfer");
    requireDamageFreeSuccess(
        preparedCurriculumLaunch(
            catalog,
            content::destination::mars,
            LaunchMissionKind::ThermalManagement,
            false,
            2,
            2,
            0,
            0,
            3313),
        RecoveryMethod::ReturnHome,
        3313,
        "thermal qualification");
    requireDamageFreeSuccess(
        preparedCurriculumLaunch(
            catalog,
            content::destination::mars,
            LaunchMissionKind::ThermalManagement,
            true,
            2,
            2,
            0,
            0,
            3314),
        RecoveryMethod::TransferArrival,
        3314,
        "Mars transfer");

    const std::array<std::pair<PreparedLaunch, LaunchFailureCause>, 4> failedQualifications {{
        {preparedCurriculumLaunch(
             catalog,
             content::destination::moon,
             LaunchMissionKind::FuelCalibration,
             false,
             0,
             0,
             0,
             0,
             3320),
         LaunchFailureCause::TrainingRescue},
        {preparedCurriculumLaunch(
             catalog,
             content::destination::moon,
             LaunchMissionKind::FlightControlsCalibration,
             false,
             1,
             0,
             0,
             0,
             3321),
         LaunchFailureCause::TrainingRescue},
        {preparedCurriculumLaunch(
             catalog,
             content::destination::mars,
             LaunchMissionKind::ThermalManagement,
             false,
             2,
             2,
             0,
             0,
             3322),
         LaunchFailureCause::ThermalRunaway},
        {preparedCurriculumLaunch(
             catalog,
             content::destination::jupiter,
             LaunchMissionKind::AsteroidBelt,
             false,
             3,
             3,
             0,
             0,
             3323),
         LaunchFailureCause::HullBreach},
    }};
    for (std::size_t index = 0; index < failedQualifications.size(); ++index) {
        const PreparedLaunch& launch = failedQualifications[index].first;
        const LaunchFailureCause cause = failedQualifications[index].second;
        GameState state = createNewGame(catalog, 3330 + index);
        Random resolveRng(3340 + index);
        const LaunchOutcome failure = resolveLaunch(
            launch,
            catalog,
            state,
            1.0,
            RecoveryMethod::ReturnHome,
            resolveRng,
            {true, cause, 0.0, 0});
        require(failure.blueprintGain == 0,
            "failed launch qualifications must never grant blueprint progress");
    }
}

void launchAsteroidsAreDeterministicFairAndHullScaled()
{
    const ContentCatalog catalog = createDefaultContent();
    const Destination& jupiter = launchDestination(catalog, content::destination::jupiter);
    const PreparedLaunch first = preparedCurriculumLaunch(
        catalog,
        content::destination::jupiter,
        LaunchMissionKind::AsteroidBelt,
        true,
        3,
        3,
        0,
        0,
        4401);
    const PreparedLaunch repeated = preparedCurriculumLaunch(
        catalog,
        content::destination::jupiter,
        LaunchMissionKind::AsteroidBelt,
        true,
        3,
        3,
        0,
        0,
        4401);
    const PreparedLaunch varied = preparedCurriculumLaunch(
        catalog,
        content::destination::jupiter,
        LaunchMissionKind::AsteroidBelt,
        true,
        3,
        3,
        0,
        0,
        4402);
    require(first.asteroidCount == 10 && repeated.asteroidCount == 10,
        "the Jupiter belt should contain ten asteroids in five rows");

    bool anyVariation = false;
    for (int index = 0; index < first.asteroidCount; ++index) {
        const LaunchAsteroid& lhs = first.asteroids[static_cast<std::size_t>(index)];
        const LaunchAsteroid& rhs = repeated.asteroids[static_cast<std::size_t>(index)];
        require(nearlyEqual(lhs.routeProgress, rhs.routeProgress) &&
                nearlyEqual(lhs.courseOffset, rhs.courseOffset) &&
                nearlyEqual(lhs.scale, rhs.scale),
            "asteroid layouts must repeat exactly for the same launch seed");
        const LaunchAsteroid& other = varied.asteroids[static_cast<std::size_t>(index)];
        anyVariation = anyVariation ||
            !nearlyEqual(lhs.courseOffset, other.courseOffset) ||
            !nearlyEqual(lhs.scale, other.scale);
        require(lhs.scale >= 0.75 && lhs.scale <= 1.25,
            "asteroid scale must stay within the collision-tested range");
    }
    require(anyVariation, "different launch seeds should vary the belt");

    int previousOpenLane = -1;
    for (int row = 0; row < tuning::launch::asteroidRowCount; ++row) {
        const int lane = openAsteroidLane(first, row);
        require(lane >= 0, "each row must leave exactly one open lane");
        if (previousOpenLane >= 0) {
            require(std::abs(lane - previousOpenLane) <= 1,
                "adjacent asteroid openings must form a steerable path");
        }
        previousOpenLane = lane;
    }

    require(nearlyEqual(launchAsteroidImpactDamage(0, 1.0), 40.0) &&
            nearlyEqual(launchAsteroidImpactDamage(1, 1.0), 32.0) &&
            nearlyEqual(launchAsteroidImpactDamage(2, 1.0), 26.0) &&
            nearlyEqual(launchAsteroidImpactDamage(3, 1.0), 20.0),
        "Hull Plating must reduce the same standard impact to 100, 80, 65, and 50 percent");

    PreparedLaunch collision = first;
    collision.trainingMission = false;
    collision.manualControlsEnabled = false;
    collision.heatEnabled = false;
    collision.asteroidCount = 2;
    collision.asteroids[0] = {0.50, 0.0, 0.10, 1.25};
    collision.asteroids[1] = {0.54, 0.0, 0.10, 1.00};
    LaunchFlightState struck = beginLaunchFlight(collision, jupiter);
    struck.travelProgress = 0.45;
    struck.travelVelocity = 1.50;
    const LaunchFlightStep firstHit = updateLaunchFlight(
        struck,
        collision,
        jupiter,
        {},
        0.08);
    require(firstHit.asteroidHit && nearlyEqual(struck.hullRemaining, 50.0),
        "swept collision must catch a large asteroid crossed between frames and base hull must survive");
    require(struck.active && !firstHit.failed,
        "a clean base Hull must survive one maximum-scale legal asteroid impact");
    GameState collisionState = createNewGame(catalog, 4403);
    Random collisionResolveRng(4403);
    const LaunchOutcome collisionOutcome = resolveLaunch(
        collision,
        catalog,
        collisionState,
        struck.currentMultiplier,
        RecoveryMethod::ReturnHome,
        collisionResolveRng,
        {true, LaunchFailureCause::None, struck.minimumSafetyMargin, struck.hullDamageTaken});
    require(collisionOutcome.shipDamage == struck.hullDamageTaken,
        "a piloted asteroid return must apply only the explicit collision damage, with no hidden stress damage");
    require(nearlyEqual(
                struck.asteroidInvulnerabilitySeconds,
                tuning::launch::asteroidInvulnerabilitySeconds),
        "an impact must grant exactly 0.75 seconds of invulnerability");
    const int damageAfterFirst = struck.hullDamageTaken;
    updateLaunchFlight(struck, collision, jupiter, {}, 0.08);
    require(struck.hullDamageTaken == damageAfterFirst,
        "nearby asteroids must not multi-hit during post-impact invulnerability");

    beginLaunchReturn(struck);
    require(!struck.asteroidHit[0] && !struck.asteroidHit[1],
        "returning home should preserve the field while resetting per-leg impact markers");

    PreparedLaunch beltQualification = collision;
    beltQualification.config.frontierTransfer = false;
    beltQualification.trainingMission = true;
    beltQualification.asteroidCount = 1;
    beltQualification.asteroids[0] = {0.05, 0.0, 0.10, 1.25};
    LaunchFlightState breached = beginLaunchFlight(beltQualification, jupiter);
    breached.hullRemaining = 1.0;
    breached.travelVelocity = 1.50;
    LaunchFlightStep breach;
    for (int index = 0; index < 10 && breached.active; ++index) {
        breach = updateLaunchFlight(breached, beltQualification, jupiter, {}, 0.08);
    }
    require(breach.failed && breach.failureCause == LaunchFailureCause::HullBreach,
        "accumulated asteroid damage during the belt survey must remain an explicit Hull Breach");

    for (int rank = 0; rank <= 3; ++rank) {
        PreparedLaunch ranked = first;
        ranked.hullRank = rank;
        LaunchFlightState integrity = beginLaunchFlight(ranked, jupiter);
        require(nearlyEqual(
                    integrity.hullMaximum,
                    100.0 + static_cast<double>(rank) * 25.0),
            "Hull ranks must expose 100, 125, 150, and 175 HP");
    }

    LaunchFlightState noHit = beginLaunchFlight(first, jupiter);
    noHit.selectedThrottle = tuning::launch::pilotingMinimumPoweredThrottle;
    const LaunchFlightStep completion = flyCompetentPolicy(
        noHit,
        first,
        jupiter,
        true);
    require(completion.reachedDestination && !completion.failed &&
            noHit.hullRemaining == noHit.hullMaximum,
        "a no-hit pilot must complete Jupiter without Hull Plating");
}

void launchCurriculumEconomyGatesAndRoundTrips()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 5501);
    const ShipModule* fuelTanksTwo = catalog.findModule(content::module::fuelTanks2);
    const ShipModule* fuelTanksThree = catalog.findModule(content::module::fuelTanks3);
    require(fuelTanksTwo != nullptr && fuelTanksThree != nullptr &&
            moduleOfferCost(*fuelTanksTwo) == 22 &&
            moduleOfferCost(*fuelTanksThree) == 92 &&
            tuning::launchProgression::lessonReward == 22.0,
        "launch refits must use their rarity price while a qualified lesson still pays 22 credits");

    GameState flightModes = createNewGame(catalog, 5500);
    require(!flightModes.launchConfig.frontierTransfer,
        "the Fuel Survey must remain a return flight");
    flightModes.meta.launchLessons.stage = LaunchTrainingStage::FlightControlsCalibration;
    flightModes.meta.launchUpgrades.fuelTanks = 1;
    syncLaunchConfig(flightModes, catalog);
    require(!flightModes.launchConfig.frontierTransfer,
        "the Flight Controls calibration must remain a return flight");
    flightModes.meta.launchLessons.stage = LaunchTrainingStage::MoonTransfer;
    flightModes.meta.launchUpgrades.flightControls = 1;
    syncLaunchConfig(flightModes, catalog);
    require(flightModes.launchConfig.frontierTransfer,
        "the Moon transfer and every later curriculum flight must use arrival mode");

    GameState moonArrival = createNewGame(catalog, 5505);
    moonArrival.meta.launchLessons.stage = LaunchTrainingStage::MoonTransfer;
    moonArrival.meta.launchUpgrades.fuelTanks = 1;
    moonArrival.meta.launchUpgrades.flightControls = 1;
    syncLaunchConfig(moonArrival, catalog);
    LaunchOutcome moonLanding;
    moonLanding.type = LaunchResultType::MissionComplete;
    moonLanding.recoveryMethod = RecoveryMethod::TransferArrival;
    moonLanding.destinationId = content::destination::moon;
    moonLanding.frontierTransfer = true;
    moonLanding.pilotedFlight = true;
    moonLanding.minimumSafetyMargin = 0.5;
    moonLanding.payout = 20.0;
    applyLaunchOutcome(moonArrival, catalog, moonLanding);
    require(moonArrival.run.destinationIndex == 1 &&
            moonArrival.meta.launchLessons.stage == LaunchTrainingStage::ThermalManagement &&
            moonArrival.run.refitEntitled &&
            nearlyEqual(moonArrival.run.credits, static_cast<double>(moduleOfferCost(*fuelTanksTwo))),
        "the successful Moon transfer must fund the mandatory Fuel Tanks II refit");

    moonArrival.run.credits = 20.0;
    syncLaunchConfig(moonArrival, catalog);
    require(nearlyEqual(moonArrival.run.credits, 20.0),
        "synchronization must not grant missing refit credits to an older save");
    require(currentDestinationLaunchReady(moonArrival, catalog) &&
            !launchMissionReady(moonArrival, catalog),
        "the reached Moon must remain launchable while the next Mars route still needs Fuel Tanks II");

    GameState thermalArrival = createNewGame(catalog, 5506);
    thermalArrival.run.destinationIndex = 1;
    thermalArrival.meta.furthestTier = 1;
    thermalArrival.meta.launchLessons.stage = LaunchTrainingStage::ThermalManagement;
    thermalArrival.meta.launchUpgrades.fuelTanks = 2;
    thermalArrival.meta.launchUpgrades.flightControls = 1;
    thermalArrival.meta.unlockKeys.push_back(content::unlock::routeMars);
    syncLaunchConfig(thermalArrival, catalog);
    require(thermalArrival.launchConfig.frontierTransfer &&
            thermalArrival.launchConfig.destinationId == content::destination::mars &&
            thermalArrival.launchConfig.missionKind == LaunchMissionKind::ThermalManagement,
        "the first thermal lesson must be a real Mars arrival, not a proving return");
    LaunchOutcome thermalLanding;
    thermalLanding.type = LaunchResultType::MissionComplete;
    thermalLanding.recoveryMethod = RecoveryMethod::TransferArrival;
    thermalLanding.destinationId = content::destination::mars;
    thermalLanding.frontierTransfer = true;
    thermalLanding.pilotedFlight = true;
    thermalLanding.minimumSafetyMargin = 0.5;
    applyLaunchOutcome(thermalArrival, catalog, thermalLanding);
    require(thermalArrival.run.destinationIndex == 2 &&
            thermalArrival.meta.furthestTier == 2 &&
            thermalArrival.meta.launchLessons.stage == LaunchTrainingStage::HullIntegrity,
        "landing on Mars during the thermal lesson must advance the frontier and curriculum together");
    require(thermalArrival.run.credits == tuning::launchProgression::lessonReward,
        "the successful Mars lesson arrival must retain the direct-upgrade reward");

    thermalArrival.meta.launchUpgrades.fuelTanks = 3;
    thermalArrival.meta.unlockKeys.push_back(content::unlock::routeJupiter);
    syncLaunchConfig(thermalArrival, catalog);
    require(thermalArrival.launchConfig.frontierTransfer &&
            thermalArrival.launchConfig.destinationId == content::destination::jupiter &&
            thermalArrival.launchConfig.missionKind == LaunchMissionKind::AsteroidBelt,
        "the first asteroid lesson must be a real Jupiter arrival, not a proving return");
    LaunchOutcome beltLanding;
    beltLanding.type = LaunchResultType::MissionComplete;
    beltLanding.recoveryMethod = RecoveryMethod::TransferArrival;
    beltLanding.destinationId = content::destination::jupiter;
    beltLanding.frontierTransfer = true;
    beltLanding.pilotedFlight = true;
    beltLanding.minimumSafetyMargin = 0.5;
    applyLaunchOutcome(thermalArrival, catalog, beltLanding);
    require(thermalArrival.run.destinationIndex == 3 &&
            thermalArrival.meta.furthestTier == 3 &&
            thermalArrival.meta.launchLessons.stage == LaunchTrainingStage::Complete,
        "landing on Jupiter during the asteroid lesson must advance the frontier and finish the curriculum");

    GameState repeatedFailures = state;
    repeatedFailures.meta.destinationAttempts.assign(catalog.destinations.size(), 99);
    Random baselineRng(55010);
    Random retryRng(55010);
    const PreparedLaunch baselineLaunch = prepareLaunch(state, catalog, baselineRng);
    const PreparedLaunch retriedLaunch = prepareLaunch(repeatedFailures, catalog, retryRng);
    require(nearlyEqual(baselineLaunch.fuelCapacity, retriedLaunch.fuelCapacity) &&
            nearlyEqual(baselineLaunch.controlChaos, retriedLaunch.controlChaos) &&
            baselineLaunch.coolingRank == retriedLaunch.coolingRank &&
            baselineLaunch.hullRank == retriedLaunch.hullRank &&
            nearlyEqual(
                baselineLaunch.controlSteeringResponseVariation,
                retriedLaunch.controlSteeringResponseVariation),
        "repeated failures alone must never add hidden pity to live launch survival");

    LaunchOutcome lesson;
    lesson.type = LaunchResultType::MissionComplete;
    lesson.recoveryMethod = RecoveryMethod::ReturnHome;
    lesson.destinationId = content::destination::moon;
    lesson.ejectMultiplier = state.launchConfig.burnGoalMultiplier;
    lesson.pilotedFlight = true;
    lesson.minimumSafetyMargin = 0.5;
    applyLaunchOutcome(state, catalog, lesson);
    require(state.run.credits == 22.0 &&
            state.meta.launchLessons.stage == LaunchTrainingStage::FlightControlsCalibration,
        "a qualified fuel-survey return should net exactly 22 and unlock only Fuel Tanks I");
    require(launchUpgradeUnlocked(state, LaunchUpgradeKind::FuelTanks, 1) &&
            !launchUpgradeUnlocked(state, LaunchUpgradeKind::FlightControls, 1),
        "lesson offers must appear one at a time");
    require(installLaunchUpgrade(state, catalog, LaunchUpgradeKind::FuelTanks) &&
            state.run.credits == 0.0 &&
            state.meta.launchUpgrades.fuelTanks == 1,
        "installing Fuel Tanks I should spend the full lesson reward");
    require(state.meta.launchLessons.stage == LaunchTrainingStage::FlightControlsCalibration &&
            !canCommitToNextFrontier(state, catalog),
        "Flight Controls calibration should remain incomplete until its return");

    GameState timelyState = createNewGame(catalog, 5508);
    LaunchOutcome timelyLesson = lesson;
    timelyLesson.fuelSurveyReturnTiming = FuelSurveyReturnTiming::Timely;
    applyLaunchOutcome(timelyState, catalog, timelyLesson);
    require(nearlyEqual(timelyState.run.credits, 25.0) &&
            timelyState.meta.launchLessons.stage == LaunchTrainingStage::FlightControlsCalibration,
        "a timely qualified Fuel Survey should pay 22 funding plus the 3-credit safety bonus");
    const Astronaut* timelyPilot = activeAstronaut(timelyState);
    require(timelyPilot != nullptr && timelyPilot->stress == tuning::stress::survivedLaunchStress,
        "a timely Fuel Survey should apply only normal successful-launch stress");

    GameState lateState = createNewGame(catalog, 5509);
    LaunchOutcome lateLesson = lesson;
    lateLesson.fuelSurveyReturnTiming = FuelSurveyReturnTiming::Late;
    applyLaunchOutcome(lateState, catalog, lateLesson);
    require(nearlyEqual(lateState.run.credits, 22.0) &&
            lateState.meta.launchLessons.stage == LaunchTrainingStage::FlightControlsCalibration,
        "a late qualified Fuel Survey should keep the mandatory 22-credit upgrade budget");
    const Astronaut* latePilot = activeAstronaut(lateState);
    require(latePilot != nullptr && latePilot->stress ==
            tuning::stress::survivedLaunchStress + tuning::launchProgression::fuelSurveyLateStress,
        "a late Fuel Survey should add five stress to the normal successful-launch stress");

    GameState rescued = createNewGame(catalog, 5507);
    LaunchOutcome rescue;
    rescue.type = LaunchResultType::SafeEject;
    rescue.recoveryMethod = RecoveryMethod::ReturnHome;
    rescue.destinationId = content::destination::moon;
    rescue.pilotedFlight = true;
    rescue.failureCause = LaunchFailureCause::TrainingRescue;
    applyLaunchOutcome(rescued, catalog, rescue);
    require(rescued.run.credits == 0.0 &&
            rescued.meta.launchLessons.stage == LaunchTrainingStage::FuelCalibration &&
            rescued.meta.shipsLost == 0 &&
            !rescued.run.refitEntitled,
        "Training Rescue must grant no credits, no upgrade progress, and no recovery-floor exploit");

    const auto verifyIncompleteCurriculumTransfer = [&](LaunchTrainingStage stage,
                                                        std::string_view destinationId,
                                                        int currentTier,
                                                        int fuelRank) {
        GameState returned = createNewGame(catalog, 5510 + currentTier);
        returned.run.destinationIndex = currentTier;
        returned.meta.furthestTier = currentTier;
        returned.meta.launchLessons.stage = stage;
        returned.meta.launchUpgrades.fuelTanks = fuelRank;
        returned.meta.launchUpgrades.flightControls = 1;
        if (currentTier >= 1) {
            returned.meta.unlockKeys.push_back(content::unlock::routeMars);
        }
        if (currentTier >= 2) {
            returned.meta.unlockKeys.push_back(content::unlock::routeJupiter);
        }
        syncLaunchConfig(returned, catalog);
        const double creditsBefore = returned.run.credits;
        const int blueprintsBefore = returned.meta.blueprintProgress;

        LaunchOutcome earlyReturn;
        earlyReturn.type = LaunchResultType::SafeEject;
        earlyReturn.recoveryMethod = RecoveryMethod::ReturnHome;
        earlyReturn.destinationId = std::string(destinationId);
        earlyReturn.frontierTransfer = true;
        earlyReturn.pilotedFlight = true;
        earlyReturn.ejectMultiplier = 1.8;
        earlyReturn.payout = 19.0;
        earlyReturn.recoveryCost = 4.0;
        earlyReturn.blueprintGain = 2;
        applyLaunchOutcome(returned, catalog, earlyReturn);

        require(returned.meta.launchLessons.stage == stage &&
                returned.run.destinationIndex == currentTier &&
                returned.meta.furthestTier == currentTier,
            "an incomplete curriculum transfer must preserve its destination and lesson stage");
        require(returned.run.frontierReadiness == 0 &&
                returned.meta.blueprintProgress == blueprintsBefore &&
                !returned.run.refitEntitled,
            "an incomplete curriculum transfer must grant no readiness, blueprint, or refit progress");
        require(nearlyEqual(returned.run.credits, creditsBefore + 15.0),
            "an incomplete curriculum transfer must preserve its existing safe-return funding");
    };
    verifyIncompleteCurriculumTransfer(
        LaunchTrainingStage::MoonTransfer,
        content::destination::moon,
        0,
        1);
    verifyIncompleteCurriculumTransfer(
        LaunchTrainingStage::MarsTransfer,
        content::destination::mars,
        1,
        2);
    verifyIncompleteCurriculumTransfer(
        LaunchTrainingStage::JupiterTransfer,
        content::destination::jupiter,
        2,
        3);

    state.meta.launchLessons.stage = LaunchTrainingStage::MoonTransfer;
    state.meta.launchUpgrades.flightControls = 1;
    require(launchMissionReady(state),
        "Fuel I and Controls I should make the Moon transfer ready");

    state.meta.furthestTier = 1;
    state.meta.launchLessons.stage = LaunchTrainingStage::MarsTransfer;
    state.meta.launchUpgrades.fuelTanks = 1;
    state.meta.launchUpgrades.cooling = 0;
    require(!launchUpgradeUnlocked(state, LaunchUpgradeKind::FuelTanks, 2) &&
            !launchMissionReady(state),
        "Fuel II and the Mars transfer must remain hidden until the Prospector contract reveals the route");
    state.meta.launchUpgrades.fuelTanks = 2;
    require(launchUpgradeUnlocked(state, LaunchUpgradeKind::FuelTanks, 2) &&
            !launchMissionReady(state),
        "an installed or migrated Fuel II rank must remain recognized without bypassing the Mars story gate");
    state.meta.unlockKeys.push_back(content::unlock::routeMars);
    require(launchMissionReady(state),
        "the Mars transfer must allow skilled play without optional Cooling I");
    require(launchUpgradeUnlocked(state, LaunchUpgradeKind::FuelTanks, 2) &&
            launchUpgradeUnlocked(state, LaunchUpgradeKind::FlightControls, 2),
        "Moon arrival must unlock Fuel II and Flight Controls II");
    require(launchUpgradeUnlocked(state, LaunchUpgradeKind::Cooling, 1),
        "completing the thermal qualification must unlock optional Cooling I");
    state.meta.launchUpgrades.flightControls = 2;
    state.meta.launchUpgrades.cooling = 1;

    state.meta.furthestTier = 2;
    state.meta.launchLessons.stage = LaunchTrainingStage::JupiterTransfer;
    state.meta.launchUpgrades.fuelTanks = 2;
    state.meta.launchUpgrades.hull = 0;
    require(!launchUpgradeUnlocked(state, LaunchUpgradeKind::FuelTanks, 3) &&
            !launchMissionReady(state),
        "Fuel III and the Jupiter transfer must remain hidden until the Mars bay contract reveals the route");
    state.meta.launchUpgrades.fuelTanks = 3;
    require(launchUpgradeUnlocked(state, LaunchUpgradeKind::FuelTanks, 3) &&
            !launchMissionReady(state),
        "an installed or migrated Fuel III rank must remain recognized without bypassing the Jupiter story gate");
    state.meta.unlockKeys.push_back(content::unlock::routeJupiter);
    require(launchMissionReady(state),
        "the Jupiter transfer must allow a no-hit pilot without optional Hull I");
    require(launchUpgradeUnlocked(state, LaunchUpgradeKind::FlightControls, 3) &&
            launchUpgradeUnlocked(state, LaunchUpgradeKind::Cooling, 2),
        "Mars arrival must unlock Controls III and Cooling II");

    state.meta.furthestTier = 3;
    state.meta.launchUpgrades.cooling = 2;
    state.meta.launchUpgrades.hull = 1;
    require(launchUpgradeUnlocked(state, LaunchUpgradeKind::Cooling, 3) &&
            launchUpgradeUnlocked(state, LaunchUpgradeKind::Hull, 2),
        "Jupiter arrival must unlock Cooling III and Hull II");
    state.meta.furthestTier = 4;
    state.meta.launchUpgrades.hull = 2;
    require(launchUpgradeUnlocked(state, LaunchUpgradeKind::Hull, 3),
        "Saturn arrival must unlock Hull III");

    const SaveData captured = captureSaveData(state);
    require(captured.version == save_schema::currentVersion, "new saves must use the current schema version");
    const std::optional<SaveData> decoded = deserializeSaveData(serializeSaveData(captured));
    require(decoded && decoded->version == save_schema::currentVersion &&
            decoded->launchUpgrades.fuelTanks == state.meta.launchUpgrades.fuelTanks &&
            decoded->launchLessons.stage == state.meta.launchLessons.stage,
        "version-10 launch ranks and lesson state must round-trip");

}

void launchCompetentPoliciesSurviveFiveThousandSeeds()
{
    const ContentCatalog catalog = createDefaultContent();
    const Destination& moon = launchDestination(catalog, content::destination::moon);
    const Destination& mars = launchDestination(catalog, content::destination::mars);
    const Destination& jupiter = launchDestination(catalog, content::destination::jupiter);

    for (std::uint64_t seed = 1; seed <= 5000; ++seed) {
        const PreparedLaunch moonLaunch = preparedCurriculumLaunch(
            catalog,
            content::destination::moon,
            LaunchMissionKind::Standard,
            true,
            1,
            1,
            0,
            0,
            seed);
        LaunchFlightState moonFlight = beginLaunchFlight(moonLaunch, moon);
        const LaunchFlightStep moonResult = flyCompetentPolicy(
            moonFlight,
            moonLaunch,
            moon,
            false);
        require(moonResult.reachedDestination && !moonResult.failed,
            "competent Moon policy must have zero incident-only deaths across 5,000 seeds");

        const PreparedLaunch marsLaunch = preparedCurriculumLaunch(
            catalog,
            content::destination::mars,
            LaunchMissionKind::ThermalManagement,
            true,
            2,
            1,
            0,
            0,
            seed);
        LaunchFlightState marsFlight = beginLaunchFlight(marsLaunch, mars);
        const LaunchFlightStep marsResult = flyCompetentPolicy(
            marsFlight,
            marsLaunch,
            mars,
            false);
        require(marsResult.reachedDestination && !marsResult.failed,
            "competent cooling policy must have zero random Mars deaths across 5,000 seeds");

        const PreparedLaunch jupiterLaunch = preparedCurriculumLaunch(
            catalog,
            content::destination::jupiter,
            LaunchMissionKind::AsteroidBelt,
            true,
            3,
            3,
            1,
            0,
            seed);
        for (int row = 0; row < tuning::launch::asteroidRowCount; ++row) {
            const int lane = openAsteroidLane(jupiterLaunch, row);
            require(lane >= 0, "every seeded belt must retain an open lane");
            if (row > 0) {
                require(std::abs(lane - openAsteroidLane(jupiterLaunch, row - 1)) <= 1,
                    "all 5,000 belts must retain a steerable connected path");
            }
        }
        LaunchFlightState jupiterFlight = beginLaunchFlight(jupiterLaunch, jupiter);
        jupiterFlight.selectedThrottle = tuning::launch::pilotingMinimumPoweredThrottle;
        const LaunchFlightStep jupiterResult = flyCompetentPolicy(
            jupiterFlight,
            jupiterLaunch,
            jupiter,
            true);
        if (!jupiterResult.reachedDestination || jupiterResult.failed ||
            jupiterFlight.hullRemaining != jupiterFlight.hullMaximum) {
            std::cerr << "Jupiter policy diagnostic: seed=" << seed
                      << " cause=" << static_cast<int>(jupiterResult.failureCause)
                      << " progress=" << jupiterFlight.travelProgress
                      << " course=" << jupiterFlight.courseOffset
                      << " velocity=" << jupiterFlight.courseVelocity
                      << " hull=" << jupiterFlight.hullRemaining
                      << "/" << jupiterFlight.hullMaximum << " lanes=";
            for (int row = 0; row < tuning::launch::asteroidRowCount; ++row) {
                std::cerr << openAsteroidLane(jupiterLaunch, row);
            }
            std::cerr << " hits=";
            for (int asteroid = 0; asteroid < jupiterLaunch.asteroidCount; ++asteroid) {
                if (jupiterFlight.asteroidHit[static_cast<std::size_t>(asteroid)]) {
                    const LaunchAsteroid& hit =
                        jupiterLaunch.asteroids[static_cast<std::size_t>(asteroid)];
                    std::cerr << asteroid << "@" << hit.routeProgress
                              << "/" << hit.courseOffset << "/" << hit.scale << ",";
                }
            }
            std::cerr << "\n";
        }
        require(jupiterResult.reachedDestination && !jupiterResult.failed &&
                jupiterFlight.hullRemaining == jupiterFlight.hullMaximum,
            "competent open-lane policy must have zero impossible Jupiter deaths across 5,000 seeds");
    }
}

void launchSkillFailuresRemainVisibleAndNonRandom()
{
    const ContentCatalog catalog = createDefaultContent();
    const Destination& moon = launchDestination(catalog, content::destination::moon);

    PreparedLaunch calibration = preparedCurriculumLaunch(
        catalog,
        content::destination::moon,
        LaunchMissionKind::FlightControlsCalibration,
        false,
        1,
        0,
        0,
        0,
        6601);
    calibration.controlSteeringResponseVariation = 0.20;
    calibration.controlKickCount = static_cast<int>(calibration.controlKickDirections.size());
    std::fill(
        calibration.controlKickDirections.begin(),
        calibration.controlKickDirections.end(),
        1.0);
    LaunchFlightState ignored = beginLaunchFlight(calibration, moon);
    LaunchFlightStep ignoredResult;
    for (int index = 0; index < 12000 && ignored.active; ++index) {
        const int pulse = index % 24;
        const double throttle = pulse < 5 ? 1.0 : (pulse < 12 ? -1.0 : 0.0);
        ignoredResult = updateLaunchFlight(
            ignored,
            calibration,
            moon,
            {0.0, throttle, false},
            0.04);
    }
    require(ignoredResult.failed &&
            ignoredResult.failureCause == LaunchFailureCause::TrainingRescue,
        "ignoring unstable base controls must eventually trigger a non-destructive training rescue");

    PreparedLaunch fuelLesson = preparedCurriculumLaunch(
        catalog,
        content::destination::moon,
        LaunchMissionKind::FuelCalibration,
        false,
        0,
        0,
        0,
        0,
        6602);
    LaunchFlightState missedTurn = beginLaunchFlight(fuelLesson, moon);
    LaunchFlightStep fuelResult;
    for (int index = 0; index < 12000 && missedTurn.active; ++index) {
        fuelResult = updateLaunchFlight(missedTurn, fuelLesson, moon, {}, 0.04);
    }
    require(fuelResult.failed &&
            fuelResult.failureCause == LaunchFailureCause::TrainingRescue,
        "ignoring the fuel light must rescue the tutorial pilot without destroying campaign assets");

    PreparedLaunch controlsLateReturn = preparedCurriculumLaunch(
        catalog,
        content::destination::moon,
        LaunchMissionKind::FlightControlsCalibration,
        false,
        1,
        0,
        0,
        0,
        6604);
    controlsLateReturn.controlChaos = 0.0;
    controlsLateReturn.controlSteeringResponseVariation = 0.0;
    LaunchFlightState controlsReturn = beginLaunchFlight(controlsLateReturn, moon);
    LaunchFlightStep controlsReturnStep;
    while (controlsReturn.travelProgress < 0.80) {
        controlsReturnStep = updateLaunchFlight(
            controlsReturn, controlsLateReturn, moon, {}, 0.04);
        require(!controlsReturnStep.failed,
            "the controls lesson should remain safe while the player experiments outbound");
    }
    require(controlsReturn.fuelRemaining > 0.0,
        "the controls lesson should have fuel remaining when the player turns back");
    beginLaunchReturn(controlsReturn);
    while (controlsReturn.active) {
        controlsReturnStep = updateLaunchFlight(
            controlsReturn, controlsLateReturn, moon, {}, 0.04);
    }
    require(controlsReturnStep.reachedHome && !controlsReturnStep.failed,
        "a late controls-lesson turnaround should stretch remaining fuel across the return instead of rescuing at Earth");

    // The controls sortie has enough fuel to touch the Moon, but it is a
    // navigation test rather than a cleared lunar transfer. Reaching the Moon
    // must visibly collide instead of quietly completing the test mission.
    PreparedLaunch landingTest = preparedCurriculumLaunch(
        catalog,
        content::destination::moon,
        LaunchMissionKind::FlightControlsCalibration,
        false,
        1,
        3,
        0,
        0,
        6603);
    landingTest.controlChaos = 0.0;
    landingTest.controlSteeringResponseVariation = 0.0;
    LaunchFlightState lunarImpact = beginLaunchFlight(landingTest, moon);
    LaunchFlightStep lunarImpactStep;
    for (int index = 0; index < 12000 && lunarImpact.active; ++index) {
        lunarImpactStep = updateLaunchFlight(lunarImpact, landingTest, moon, {}, 0.04);
    }
    require(lunarImpactStep.failed &&
            lunarImpactStep.failureCause == LaunchFailureCause::LunarImpact &&
            !lunarImpactStep.reachedDestination,
        "continuing a controls-calibration sortie into the Moon must cause a visible Lunar Impact, not a landing or generic rescue");
    GameState impactState = createNewGame(catalog, 6603);
    Random impactRng(6603);
    const LaunchOutcome impactOutcome = resolveLaunch(
        landingTest,
        catalog,
        impactState,
        lunarImpact.currentMultiplier,
        RecoveryMethod::None,
        impactRng,
        {true, lunarImpactStep.failureCause, lunarImpact.minimumSafetyMargin, lunarImpact.hullDamageTaken});
    require(impactOutcome.type == LaunchResultType::Destroyed &&
            impactOutcome.failureCause == LaunchFailureCause::LunarImpact,
        "the uncalibrated lunar collision must retain its explicit destructive terminal cause");
}

void emergencyRecruitmentPreventsDeadRosterSoftLock()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 101);
    for (Astronaut& astronaut : state.run.crew) {
        astronaut.status = CrewStatus::Dead;
    }
    state.run.credits = 5.0;
    syncLaunchConfig(state, catalog);

    require(activeAstronaut(state) == nullptr, "test setup should have no living astronaut");
    require(recruitCrew(state, catalog), "emergency recruitment should work even with low credits");
    require(activeAstronaut(state) != nullptr, "recruitment should restore a launchable astronaut");
    require(state.run.credits == 5.0, "emergency recruitment should be free when the roster is dead");
    require(!state.launchConfig.astronautId.empty(), "recruitment should select the new astronaut");
}

void emergencyRecruitmentOffersAnimalCandidateChoice()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 102);
    for (Astronaut& astronaut : state.run.crew) {
        astronaut.status = CrewStatus::Dead;
    }
    state.run.credits = 0.0;
    syncLaunchConfig(state, catalog);

    const std::vector<const Astronaut*> candidates = recruitCandidateTemplates(state, catalog);
    require(candidates.size() == 3, "pilot intake should offer three candidate cards");
    const std::string pickedName = candidates[1]->name;
    const std::string pickedClass = candidates[1]->background;

    require(recruitCrew(state, catalog, 1), "indexed pilot intake should recruit the chosen card");
    const Astronaut* recruited = activeAstronaut(state);
    require(recruited != nullptr && recruited->name == pickedName, "indexed recruitment should preserve the selected animal pilot");
    require(recruited->background == pickedClass, "indexed recruitment should preserve the animal class text");
    require(state.run.credits == 0.0, "emergency pilot intake should remain free");
}

void moduleOffersAreOneChoiceRefits()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 202);
    state.run.credits = 200.0;
    state.meta.unlockKeys.push_back(content::unlock::thermal);
    state.meta.unlockKeys.push_back(content::unlock::surfaceDrills);

    Random rng(909);
    generateModuleOffers(state, catalog, rng);

    for (const std::string& moduleId : state.run.offerModuleIds) {
        require(moduleId.empty() || std::find(state.meta.ownedModuleIds.begin(), state.meta.ownedModuleIds.end(), moduleId) == state.meta.ownedModuleIds.end(),
            "refit offers should never sell an already-owned starter module");
        if (const ShipModule* module = catalog.findModule(moduleId)) {
            require(!module->compatibilityOnly,
                "new Refit rolls must never offer a legacy compatibility module");
        }
    }

    require(!offerKeyAt(state, 0).empty(), "reward screen should receive offer one");
    require(!offerKeyAt(state, 1).empty(), "reward screen should receive offer two");
    require(!offerKeyAt(state, 2).empty(), "reward screen should receive offer three");
    require(offerKeyAt(state, 0) != offerKeyAt(state, 1), "offer one and two should be distinct when possible");
    require(offerKeyAt(state, 0) != offerKeyAt(state, 2), "offer one and three should be distinct when possible");
    require(offerKeyAt(state, 1) != offerKeyAt(state, 2), "offer two and three should be distinct when possible");

    int pickedIndex = -1;
    for (int index = 0; index < 3; ++index) {
        const ShipModule* module = catalog.findModule(state.run.offerModuleIds[static_cast<std::size_t>(index)]);
        if (module == nullptr || (module->surfaceDepthUpgradeKind == SurfaceDepthUpgradeKind::None && module->rigFuelLoopRank <= 0)) {
            pickedIndex = index;
            break;
        }
    }
    require(pickedIndex >= 0, "the mixed Refit fixture should retain one ordinary one-choice offer");
    const std::string picked = offerKeyAt(state, static_cast<std::size_t>(pickedIndex));
    require(buyOffer(state, catalog, static_cast<std::size_t>(pickedIndex)), "player should be able to install one affordable reward module");
    require(state.run.offerModuleIds[0].empty() && state.run.offerModuleIds[1].empty() && state.run.offerModuleIds[2].empty(), "buying one reward should consume the refit window");
    require(state.run.offerCrewUpgradeIds[0].empty() && state.run.offerCrewUpgradeIds[1].empty() && state.run.offerCrewUpgradeIds[2].empty(), "buying one reward should consume crew refit offers");
    if (picked.find("module:") == 0) {
        const std::string moduleId = picked.substr(7);
        require(std::find(state.run.inventoryModuleIds.begin(), state.run.inventoryModuleIds.end(), moduleId) != state.run.inventoryModuleIds.end(), "installed module should enter inventory");
        require(std::find(state.meta.ownedModuleIds.begin(), state.meta.ownedModuleIds.end(), moduleId) != state.meta.ownedModuleIds.end(), "installed module should enter permanent shipyard inventory");
    } else {
        const std::string upgradeId = picked.substr(5);
        require(std::find(state.run.crewUpgradeIds.begin(), state.run.crewUpgradeIds.end(), upgradeId) != state.run.crewUpgradeIds.end(), "installed crew upgrade should enter facilities");
    }
}

void openingRefitTracksAreCuratedAndEntitled()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 212);
    Random rng(2120);

    LaunchOutcome fuelLesson;
    fuelLesson.type = LaunchResultType::MissionComplete;
    fuelLesson.recoveryMethod = RecoveryMethod::ReturnHome;
    fuelLesson.destinationId = content::destination::moon;
    fuelLesson.pilotedFlight = true;
    fuelLesson.ejectMultiplier = state.launchConfig.burnGoalMultiplier;
    fuelLesson.minimumSafetyMargin = 0.5;
    fuelLesson.payout = tuning::launchProgression::lessonReward;
    applyLaunchOutcome(state, catalog, fuelLesson);
    require(state.run.refitEntitled &&
            state.meta.launchLessons.stage == LaunchTrainingStage::FlightControlsCalibration &&
            state.run.credits == 22.0,
        "the qualified fuel return should grant one direct upgrade entitlement and exactly its 22-credit price");

    generateModuleOffers(state, catalog, rng);
    require(state.run.offerModuleIds[0] == content::module::fuelTanks1 &&
            state.run.offerModuleIds[1].empty() &&
            state.run.offerModuleIds[2].empty() &&
            std::all_of(
                state.run.offerCrewUpgradeIds.begin(),
                state.run.offerCrewUpgradeIds.end(),
                [](const std::string& id) { return id.empty(); }),
        "the first lesson result should show only Fuel Tanks I, never a randomized three-card board");
    require(!rerollOffers(state, catalog, rng),
        "direct launch lessons must not expose rerolls");
    require(buyOffer(state, catalog, 0) &&
            state.meta.launchUpgrades.fuelTanks == 1 &&
            state.run.credits == 0.0 &&
            !state.run.refitEntitled,
        "installing the one fuel offer should consume the lesson reward and entitlement");

    LaunchOutcome controlsLesson = fuelLesson;
    controlsLesson.ejectMultiplier = state.launchConfig.burnGoalMultiplier;
    controlsLesson.payout = tuning::launchProgression::lessonReward;
    applyLaunchOutcome(state, catalog, controlsLesson);
    require(state.run.refitEntitled &&
            state.meta.launchLessons.stage == LaunchTrainingStage::MoonTransfer &&
            state.run.credits == 22.0,
        "the controls calibration should unlock its one plainly named rank");
    generateModuleOffers(state, catalog, rng);
    require(state.run.offerModuleIds[0] == content::module::flightControls1 &&
            state.run.offerModuleIds[1].empty() &&
            state.run.offerModuleIds[2].empty(),
        "the second lesson result should show only Flight Controls I");
    require(buyOffer(state, catalog, 0) &&
            state.meta.launchUpgrades.flightControls == 1 &&
            state.run.credits == 0.0,
        "Flight Controls I should cost the same exact 22-credit lesson reward");

    for (const std::string_view retired : {
             std::string_view(content::module::sparrowInjectorTune),
             std::string_view(content::module::reserveFeedManifold),
             std::string_view(content::module::sustainedBurnPackage),
             std::string_view(content::module::radiatorVaneExtension),
             std::string_view(content::module::telemetryNoiseFilter),
             std::string_view(content::module::pressureBalanceBaffles),
             std::string_view(content::module::patchworkCrossBracing),
             std::string_view(content::module::springCapsuleRetropack),
             std::string_view(content::module::recoveryCradle)}) {
        require(std::find(
                    state.meta.ownedModuleIds.begin(),
                    state.meta.ownedModuleIds.end(),
                    retired) == state.meta.ownedModuleIds.end(),
            "retired proving-card IDs must never be awarded by the live curriculum");
    }
}

void fuelRefitsTeachMarsAndFundJupiter()
{
    const ContentCatalog catalog = createDefaultContent();
    const ShipModule* fuelTanksTwo = catalog.findModule(content::module::fuelTanks2);
    const ShipModule* fuelTanksThree = catalog.findModule(content::module::fuelTanks3);
    require(fuelTanksTwo != nullptr && fuelTanksThree != nullptr,
        "fuel progression requires both permanent tank refits");
    require(moduleOfferCost(*fuelTanksTwo) == 22 &&
            fuelTanksThree->rarity == Rarity::Prototype &&
            moduleOfferCost(*fuelTanksThree) == 92,
        "Fuel Tanks II must remain the taught Common purchase and Fuel Tanks III must use Prototype pricing");

    GameState marsRefit = createNewGame(catalog, 0xF002);
    marsRefit.run.destinationIndex = 1;
    marsRefit.meta.furthestTier = 1;
    marsRefit.meta.launchLessons.stage = LaunchTrainingStage::ThermalManagement;
    marsRefit.meta.launchUpgrades.fuelTanks = 1;
    marsRefit.meta.launchUpgrades.flightControls = 1;
    marsRefit.meta.unlockKeys.push_back(content::unlock::routeMars);
    marsRefit.run.refitEntitled = true;
    marsRefit.run.credits = 22.0;
    syncLaunchConfig(marsRefit, catalog);

    Random taughtRng(0xF002);
    generateModuleOffers(marsRefit, catalog, taughtRng);
    require(marsRefit.run.offerModuleIds[0] == content::module::fuelTanks2 &&
            marsRefit.run.offerModuleIds[1].empty() &&
            marsRefit.run.offerModuleIds[2].empty(),
        "the post-Prospector refit must teach Fuel Tanks II as a single offer");
    const RefitWindowPresentation taughtWindow = refitWindowPresentation(marsRefit, catalog);
    require(taughtWindow.offers.size() == 1 && taughtWindow.offers.front().cost == 22 &&
            taughtWindow.offers.front().affordable && !taughtWindow.showSkip,
        "an affordable taught tank refit must require the purchase before continuing");
    require(buyOffer(marsRefit, catalog, 0) &&
            marsRefit.meta.launchUpgrades.fuelTanks == 2 &&
            nearlyEqual(marsRefit.run.credits, 0.0) &&
            nearlyEqual(launchFuelCapacity(marsRefit), 20.0) &&
            launchMissionReady(marsRefit, catalog),
        "buying Fuel Tanks II must spend 22 credits and make the 20-fuel Mars transfer available");

    GameState underfundedLegacy = marsRefit;
    underfundedLegacy.meta.launchUpgrades.fuelTanks = 1;
    underfundedLegacy.meta.ownedModuleIds.erase(
        std::remove(
            underfundedLegacy.meta.ownedModuleIds.begin(),
            underfundedLegacy.meta.ownedModuleIds.end(),
            std::string(content::module::fuelTanks2)),
        underfundedLegacy.meta.ownedModuleIds.end());
    underfundedLegacy.run.refitEntitled = true;
    underfundedLegacy.run.credits = 20.0;
    syncLaunchConfig(underfundedLegacy, catalog);
    Random legacyRng(0xF003);
    generateModuleOffers(underfundedLegacy, catalog, legacyRng);
    const RefitWindowPresentation legacyWindow = refitWindowPresentation(underfundedLegacy, catalog);
    require(nearlyEqual(underfundedLegacy.run.credits, 20.0) && legacyWindow.showSkip,
        "an underfunded legacy save must keep its earned credits and be able to replay Moon instead of receiving a grant or soft lock");

    GameState jupiterRefit = createNewGame(catalog, 0xF004);
    jupiterRefit.run.destinationIndex = 2;
    jupiterRefit.meta.furthestTier = 2;
    jupiterRefit.meta.launchLessons.stage = LaunchTrainingStage::HullIntegrity;
    jupiterRefit.meta.launchUpgrades.fuelTanks = 2;
    jupiterRefit.meta.launchUpgrades.flightControls = 1;
    jupiterRefit.meta.unlockKeys.push_back(content::unlock::routeMars);
    jupiterRefit.meta.unlockKeys.push_back(content::unlock::routeJupiter);
    jupiterRefit.run.refitEntitled = true;
    jupiterRefit.run.credits = 83.0;
    if (ScenarioInstance* marsBay = findScenarioInstance(
            jupiterRefit.meta, content::scenario::marsBayExpansion)) {
        if (ScenarioStepProgress* delivery = findScenarioStepProgress(*marsBay, "delivery")) {
            delivery->briefingAcknowledged = true;
            delivery->completed = true;
            delivery->claimed = true;
        }
    }
    require(performScenarioAction(
                jupiterRefit,
                catalog,
                content::scenario::marsBayExpansion,
                "funding",
                ScenarioActionKind::AcknowledgeBriefing)
                .applied,
        "the Mars transfer-assist fixture must review the authored funding beat");
    syncLaunchConfig(jupiterRefit, catalog);

    const FrontierGateStatus blockedJupiter = frontierGateStatus(jupiterRefit, catalog);
    require(!blockedJupiter.satisfied && blockedJupiter.kind == FrontierGateKind::FlightData &&
            currentDestinationLaunchReady(jupiterRefit, catalog),
        "insufficient Jupiter fuel must block only the next route while leaving Mars replay available");
    const GameState neither = jupiterRefit;

    GameState slingshotOnly = jupiterRefit;
    const double slingshotCreditsBefore = slingshotOnly.run.credits;
    const int slingshotResearchBefore = slingshotOnly.meta.blueprintProgress;
    require(startJupiterSlingshotRun(slingshotOnly, catalog) &&
            slingshotOnly.screen == Screen::Flyby &&
            slingshotOnly.run.flyby.purpose == FlybyPurpose::JupiterSlingshot,
        "the Mars departure slingshot must remain available without Fuel Tanks III");
    {
        const std::optional<SaveData> inProgressSave = deserializeSaveData(
            serializeSaveData(captureSaveData(slingshotOnly)));
        require(inProgressSave.has_value(),
            "an in-progress Mars departure Flyby must save safely");
        GameState restoredInProgress = createNewGame(catalog, 0xF0041);
        restoreSaveData(restoredInProgress, catalog, *inProgressSave);
        require(restoredInProgress.screen == Screen::Hangar &&
                !restoredInProgress.run.flyby.active &&
                canStartJupiterSlingshot(restoredInProgress, catalog),
            "loading during the realtime Flyby must return to a retryable Hangar without granting momentum");
    }
    slingshotOnly.run.flyby.completed = true;
    slingshotOnly.run.flyby.result = FlybyGrade::Perfect;
    slingshotOnly.run.flyby.elapsedSeconds = tuning::flyby::minimumFinishSeconds;
    slingshotOnly.run.flyby.pathProgress = 1.0;
    const double exitTangentXRaw =
        tuning::flyby::endX - tuning::flyby::control2X;
    const double exitTangentYRaw =
        tuning::flyby::endY - tuning::flyby::control2Y;
    const double exitTangentLength = std::hypot(exitTangentXRaw, exitTangentYRaw);
    const double exitTangentX = exitTangentXRaw / exitTangentLength;
    const double exitTangentY = exitTangentYRaw / exitTangentLength;
    const double exitRightX = exitTangentY;
    const double exitRightY = -exitTangentX;
    const double flybyExitOffset =
        -slingshotOnly.run.flyby.perfectBand * 0.50;
    slingshotOnly.run.flyby.shipX =
        tuning::flyby::endX + exitRightX * flybyExitOffset;
    slingshotOnly.run.flyby.shipY =
        tuning::flyby::endY + exitRightY * flybyExitOffset;
    slingshotOnly.run.flyby.velocityX =
        exitTangentX * tuning::flyby::maxSpeed;
    slingshotOnly.run.flyby.velocityY =
        exitTangentY * tuning::flyby::maxSpeed;
    require(armJupiterSlingshot(slingshotOnly),
        "a Perfect Mars pass must physically arm the one-attempt Jupiter slingshot");
    const double achievedSpeedBoost = slingshotOnly.run.pendingTransferAssist.speedBoost;
    const double expectedLaunchCourseOffset =
        -tuning::launch::pilotingCourseSafe * 0.50;
    require(nearlyEqual(
                slingshotOnly.run.pendingTransferAssist.exitCourseOffset,
                expectedLaunchCourseOffset),
        "the transfer assist must preserve the Flyby exit side and its exact position within the gold band");
    completeFlybyRun(slingshotOnly, catalog);
    require(slingshotOnly.run.pendingTransferAssist.active() &&
            nearlyEqual(launchFuelCapacity(slingshotOnly), 20.0) &&
            nearlyEqual(pendingLaunchFuelSavingsForDestination(slingshotOnly, content::destination::jupiter), 5.0) &&
            nearlyEqual(pendingLaunchInstabilityPenaltyForDestination(slingshotOnly, content::destination::jupiter), 0.0) &&
            nearlyEqual(calibratedTransferFuelMargin(
                slingshotOnly,
                *catalog.findDestination(content::destination::jupiter)), 5.0) &&
            jupiterTransferMarginReady(slingshotOnly),
        "a Perfect Mars slingshot alone must open Jupiter with 20 tank, 15 burn, 5 margin, and normal stability");
    require(nearlyEqual(slingshotOnly.run.credits, slingshotCreditsBefore) &&
            slingshotOnly.meta.blueprintProgress == slingshotResearchBefore,
        "the dedicated departure slingshot must award no credits or Research Data");
    require(nearlyEqual(
                achievedSpeedBoost,
                tuning::flyby::slingshotSpeedBoost *
                    tuning::flyby::slingshotMaxSpeedScale),
        "a maximum-speed slingshot must retain the full 40 percent travel-rate bonus");

    GameState slowGoldSlingshot = jupiterRefit;
    require(startJupiterSlingshotRun(slowGoldSlingshot, catalog),
        "the slow-Gold anti-cheese fixture must start a Mars slingshot");
    slowGoldSlingshot.run.flyby.completed = true;
    slowGoldSlingshot.run.flyby.result = FlybyGrade::Perfect;
    slowGoldSlingshot.run.flyby.pathProgress = 1.0;
    slowGoldSlingshot.run.flyby.shipX = tuning::flyby::endX;
    slowGoldSlingshot.run.flyby.shipY = tuning::flyby::endY;
    slowGoldSlingshot.run.flyby.velocityX =
        exitTangentX * tuning::flyby::minSpeed;
    slowGoldSlingshot.run.flyby.velocityY =
        exitTangentY * tuning::flyby::minSpeed;
    require(armJupiterSlingshot(slowGoldSlingshot) &&
            nearlyEqual(
                slowGoldSlingshot.run.pendingTransferAssist.speedBoost,
                0.0),
        "slowing to minimum speed in Gold must grant zero bonus velocity");

    GameState fastGreenSlingshot = jupiterRefit;
    require(startJupiterSlingshotRun(fastGreenSlingshot, catalog),
        "the fast-Green fixture must start a Mars slingshot");
    fastGreenSlingshot.run.flyby.completed = true;
    fastGreenSlingshot.run.flyby.result = FlybyGrade::Good;
    fastGreenSlingshot.run.flyby.pathProgress = 1.0;
    const double greenBandShare = 0.75;
    const double fastGreenExitOffset =
        fastGreenSlingshot.run.flyby.perfectBand +
        (fastGreenSlingshot.run.flyby.goodBand -
            fastGreenSlingshot.run.flyby.perfectBand) * greenBandShare;
    fastGreenSlingshot.run.flyby.shipX =
        tuning::flyby::endX + exitRightX * fastGreenExitOffset;
    fastGreenSlingshot.run.flyby.shipY =
        tuning::flyby::endY + exitRightY * fastGreenExitOffset;
    fastGreenSlingshot.run.flyby.velocityX =
        exitTangentX * tuning::flyby::maxSpeed;
    fastGreenSlingshot.run.flyby.velocityY =
        exitTangentY * tuning::flyby::maxSpeed;
    require(armJupiterSlingshot(fastGreenSlingshot) &&
            fastGreenSlingshot.run.pendingTransferAssist.grade == FlybyGrade::Good &&
            nearlyEqual(
                fastGreenSlingshot.run.pendingTransferAssist.speedBoost,
                tuning::flyby::slingshotSpeedBoost *
                    tuning::flyby::slingshotMaxSpeedScale) &&
            nearlyEqual(
                fastGreenSlingshot.run.pendingTransferAssist.exitCourseOffset,
                tuning::launch::pilotingCourseSafe +
                    (tuning::launch::pilotingCourseCaution -
                        tuning::launch::pilotingCourseSafe) * greenBandShare),
        "a breakneck Green exit must keep its full speed while preserving its farther-off-center launch position");

    const double calibratedBurn = launchPoweredFuelCost(
        20.0,
        tuning::launch::calibratedThrottle,
        tuning::flyby::jupiterSlingshotFuelSavings);
    require(nearlyEqual(calibratedBurn, 15.0) &&
            launchPoweredFuelCost(20.0, tuning::launch::pilotingMinimumPoweredThrottle, 5.0) < calibratedBurn &&
            launchPoweredFuelCost(20.0, 1.0, 5.0) > 20.0,
        "slingshot savings must apply after throttle scaling so cautious, calibrated, and reckless burns retain distinct risk");

    GameState goodSlingshot = jupiterRefit;
    require(startJupiterSlingshotRun(goodSlingshot, catalog),
        "the dedicated Mars slingshot should start from the reviewed Jupiter window");
    goodSlingshot.run.flyby.completed = true;
    goodSlingshot.run.flyby.result = FlybyGrade::Good;
    goodSlingshot.run.flyby.elapsedSeconds = tuning::flyby::durationSeconds - 1.0;
    Random goodResultRng(0xF0044);
    const PreparedLaunch goodResultLaunch = prepareLaunch(goodSlingshot, catalog, goodResultRng);
    const std::string goodResultHtml = buildGamePanelHtml(
        {goodSlingshot, catalog, goodResultLaunch, goodResultLaunch});
    require(goodResultHtml.find("SLINGSHOT ACTIVE — WILD RIDE") != std::string::npos &&
            goodResultHtml.find("+35% flight instability") != std::string::npos &&
            goodResultHtml.find("finish // +") != std::string::npos &&
            goodResultHtml.find("launch velocity") != std::string::npos &&
            goodResultHtml.find("Continue to Jupiter") != std::string::npos,
        "a Good Mars result must show its actual finish-derived velocity and the wilder flight-control cost");
    completeFlybyRun(goodSlingshot, catalog);
    require(goodSlingshot.run.pendingTransferAssist.active() &&
            nearlyEqual(pendingLaunchFuelSavingsForDestination(goodSlingshot, content::destination::jupiter), 5.0) &&
            nearlyEqual(
                pendingLaunchInstabilityPenaltyForDestination(goodSlingshot, content::destination::jupiter),
                tuning::flyby::jupiterSlingshotGoodInstabilityPenalty) &&
            jupiterTransferMarginReady(goodSlingshot) &&
            nearlyEqual(goodSlingshot.run.credits, slingshotCreditsBefore) &&
            goodSlingshot.meta.blueprintProgress == slingshotResearchBefore,
        "a Good Mars pass must open Jupiter with full momentum, +35% flight instability, and no economy reward");
    Random goodLaunchRng(0xF0042);
    const PreparedLaunch goodLaunch = prepareLaunch(goodSlingshot, catalog, goodLaunchRng);
    require(nearlyEqual(
                goodLaunch.controlChaos,
                launchControlChaosForRank(goodSlingshot.meta.launchUpgrades.flightControls) +
                    tuning::flyby::jupiterSlingshotGoodInstabilityPenalty) &&
            nearlyEqual(
                goodLaunch.slingshotInstabilityPenalty,
                tuning::flyby::jupiterSlingshotGoodInstabilityPenalty),
        "the Good slingshot must add its one-attempt penalty to the existing Flight Controls instability");

    GameState cappedGood = goodSlingshot;
    cappedGood.meta.launchUpgrades.flightControls = 0;
    Random cappedGoodRng(0xF0043);
    require(nearlyEqual(prepareLaunch(cappedGood, catalog, cappedGoodRng).controlChaos, 1.0),
        "Good slingshot instability must cap at the existing 100% maximum");

    GameState impactRetry = jupiterRefit;
    require(startJupiterSlingshotRun(impactRetry, catalog),
        "the dedicated impact fixture must start the Mars slingshot");
    impactRetry.run.flyby.shipX = tuning::flyby::destinationX;
    impactRetry.run.flyby.shipY = tuning::flyby::destinationY;
    const int expectedImpactDamage = impactRetry.run.flyby.impactHullDamage;
    updateFlybyRun(impactRetry, 0.001);
    require(impactRetry.run.flyby.completed &&
            impactRetry.run.flyby.collidedWithBody &&
            impactRetry.run.shipDamage == expectedImpactDamage &&
            expectedImpactDamage == tuning::flyby::impactHullDamage,
        "a Mars slingshot impact must retain the existing 18 hull damage");
    completeFlybyRun(impactRetry, catalog);
    require(canStartJupiterSlingshot(impactRetry, catalog),
        "an impact must spend hull integrity but leave the departure pass retryable");

    SaveData slingshotSave = captureSaveData(slingshotOnly);
    const std::optional<SaveData> restoredSlingshotSave =
        deserializeSaveData(serializeSaveData(slingshotSave));
    require(restoredSlingshotSave.has_value(),
        "an active Mars slingshot must serialize");
    GameState restoredSlingshot = createNewGame(catalog, 0xF005);
    restoreSaveData(restoredSlingshot, catalog, *restoredSlingshotSave);
    require(restoredSlingshot.run.pendingTransferAssist.active() &&
            restoredSlingshot.run.pendingTransferAssist.definitionId == content::transferAssist::marsJupiter &&
            nearlyEqual(restoredSlingshot.run.pendingTransferAssist.fuelSavings, 5.0) &&
            nearlyEqual(restoredSlingshot.run.pendingTransferAssist.instabilityPenalty, 0.0) &&
            nearlyEqual(
                restoredSlingshot.run.pendingTransferAssist.exitCourseOffset,
                expectedLaunchCourseOffset),
        "an active Mars slingshot must survive save/load until the Jupiter attempt begins");
    SaveData legacySlingshot = slingshotSave;
    legacySlingshot.pendingTransferAssist = {};
    legacySlingshot.jupiterSlingshotActive = true;
    legacySlingshot.nextLaunchFuelBoost = tuning::flyby::jupiterSlingshotFuelSavings;
    legacySlingshot.nextLaunchSpeedBoost = tuning::flyby::slingshotSpeedBoost;
    legacySlingshot.nextLaunchInstabilityPenalty = tuning::flyby::jupiterSlingshotGoodInstabilityPenalty;
    const std::optional<SaveData> restoredLegacySlingshotSave =
        deserializeSaveData(serializeSaveData(legacySlingshot));
    require(restoredLegacySlingshotSave.has_value(), "a legacy Jupiter slingshot projection must deserialize");
    GameState restoredLegacySlingshot = createNewGame(catalog, 0xF0051);
    restoreSaveData(restoredLegacySlingshot, catalog, *restoredLegacySlingshotSave);
    require(restoredLegacySlingshot.run.pendingTransferAssist.definitionId == content::transferAssist::marsJupiter &&
            restoredLegacySlingshot.run.pendingTransferAssist.targetDestinationId == content::destination::jupiter &&
            nearlyEqual(restoredLegacySlingshot.run.nextLaunchInstabilityPenalty, 0.0) &&
            nearlyEqual(
                pendingLaunchInstabilityPenaltyForDestination(
                    restoredLegacySlingshot, content::destination::jupiter),
                tuning::flyby::jupiterSlingshotGoodInstabilityPenalty),
        "a legacy active Jupiter slingshot must migrate into the canonical target-bound assist record");

    const std::optional<SaveData> restoredGoodSave = deserializeSaveData(
        serializeSaveData(captureSaveData(goodSlingshot)));
    require(restoredGoodSave.has_value(), "a Good Mars slingshot must serialize");
    GameState restoredGood = createNewGame(catalog, 0xF006);
    restoreSaveData(restoredGood, catalog, *restoredGoodSave);
    require(restoredGood.run.pendingTransferAssist.active() &&
            nearlyEqual(
                pendingLaunchInstabilityPenaltyForDestination(restoredGood, content::destination::jupiter),
                tuning::flyby::jupiterSlingshotGoodInstabilityPenalty),
        "a Good Mars slingshot must preserve its visible instability penalty across save/load");

    // A second content-only transfer assist proves the runtime is not coupled
    // to Mars or Jupiter: it starts, awards, presents, gates its target, and
    // never leaks to a different prepared launch.
    ContentCatalog reusableCatalog = catalog;
    TransferAssistDefinition& reusableAssist = reusableCatalog.transferAssists.emplace_back(
        TransferAssistDefinition {
            "test_jupiter_saturn_assist",
            content::destination::jupiter,
            content::destination::saturn,
            content::scenario::marsBayExpansion,
            "funding",
            {},
            FlybyGrade::Good,
            9.0,
            0.25,
            0.30,
            tuning::flyby::impactHullDamage,
            "Jupiter Test Assist"});
    Destination* reusableTarget = const_cast<Destination*>(
        reusableCatalog.findDestination(content::destination::saturn));
    require(reusableTarget != nullptr, "the reusable transfer-assist fixture requires Saturn content");
    reusableTarget->calibratedTransferMarginRequired = 4.0;
    GameState reusableAssistState = jupiterRefit;
    reusableAssistState.run.destinationIndex = static_cast<int>(std::distance(
        reusableCatalog.destinations.begin(),
        std::find_if(
            reusableCatalog.destinations.begin(), reusableCatalog.destinations.end(),
            [](const Destination& destination) {
                return destination.id == content::destination::jupiter;
            })));
    reusableAssistState.meta.unlockKeys.push_back(content::unlock::routeSaturn);
    reusableAssistState.meta.launchLessons.stage = LaunchTrainingStage::Complete;
    reusableAssistState.launchConfig.destinationId = content::destination::saturn;
    reusableAssistState.launchConfig.frontierTransfer = true;
    require(startTransferAssistRun(reusableAssistState, reusableCatalog, reusableAssist.id) &&
            reusableAssistState.run.flyby.transferAssistId == reusableAssist.id,
        "a content-defined Jupiter-to-Saturn assist must start without a planet-specific runtime branch");
    reusableAssistState.run.flyby.completed = true;
    reusableAssistState.run.flyby.result = FlybyGrade::Good;
    reusableAssistState.run.flyby.elapsedSeconds = tuning::flyby::minimumFinishSeconds;
    reusableAssistState.run.flyby.velocityX = tuning::flyby::maxSpeed;
    require(armTransferAssist(reusableAssistState, reusableCatalog),
        "a content-defined transfer assist must award through the generic API");
    completeFlybyRun(reusableAssistState, reusableCatalog);
    Random reusableRng(0xF0046);
    const PreparedLaunch reusableLaunch = prepareLaunch(reusableAssistState, reusableCatalog, reusableRng);
    const std::string reusableHtml = buildGamePanelHtml(
        {reusableAssistState, reusableCatalog, reusableLaunch, reusableLaunch});
    require(reusableLaunch.transferAssistId == reusableAssist.id &&
            nearlyEqual(reusableLaunch.slingshotFuelSavings, 9.0) &&
            destinationTransferMarginReady(reusableAssistState, reusableCatalog, *reusableTarget) &&
            reusableHtml.find("Continue to Saturn") != std::string::npos,
        "a second assist must display and satisfy only its own target's calibrated margin");
    reusableAssistState.launchConfig.destinationId = content::destination::mars;
    const PreparedLaunch unrelatedLaunch = prepareLaunch(reusableAssistState, reusableCatalog, reusableRng);
    require(unrelatedLaunch.transferAssistId.empty() &&
            nearlyEqual(unrelatedLaunch.slingshotFuelSavings, 0.0),
        "a pending transfer assist must not spill into a non-target launch");

    Random jupiterRng(0xF004);
    generateModuleOffers(jupiterRefit, catalog, jupiterRng);
    require(jupiterRefit.run.offerModuleIds[0] == content::module::fuelTanks3,
        "Fuel Tanks III must be pinned into the first eligible post-Mars refit slot");
    const RefitWindowPresentation jupiterWindow = refitWindowPresentation(jupiterRefit, catalog);
    require(jupiterWindow.offers.size() >= 2 &&
            jupiterWindow.offers.front().cost == 92 &&
            !jupiterWindow.offers.front().affordable &&
            jupiterWindow.showSkip && jupiterWindow.showReroll,
        "the Jupiter refit must remain a normal choice board when the Prototype tank is not yet affordable");
    require(rerollOffers(jupiterRefit, catalog, jupiterRng) &&
            jupiterRefit.run.offerModuleIds[0] == content::module::fuelTanks3,
        "rerolling the post-Mars board must preserve the Fuel Tanks III opportunity");

    const Destination* mars = catalog.findDestination(content::destination::mars);
    require(mars != nullptr, "the fuel economy fixture requires Mars content");
    const double conservativeFirstMarsCredits = 30.0 + tuning::launchProgression::lessonReward;
    const double repeatMarsTransferCredits = mars->baseReward * mars->targetMultiplier *
        tuning::rewards::transferArrivalPayoutFactor;
    require(conservativeFirstMarsCredits < static_cast<double>(moduleOfferCost(*fuelTanksThree)) &&
            conservativeFirstMarsCredits + repeatMarsTransferCredits >=
                static_cast<double>(moduleOfferCost(*fuelTanksThree)),
        "the conservative economy must put Fuel Tanks III out of reach after first Mars but fund it with one successful repeat transfer");

    jupiterRefit.run.credits = 92.0;
    require(buyOffer(jupiterRefit, catalog, 0) &&
            jupiterRefit.meta.launchUpgrades.fuelTanks == 3 &&
            nearlyEqual(jupiterRefit.run.credits, 0.0) &&
            nearlyEqual(launchFuelCapacity(jupiterRefit), 25.0) &&
            launchMissionReady(jupiterRefit, catalog),
        "buying the pinned Prototype must spend 92 credits and make the 25-fuel Jupiter transfer available");
    require(canStartJupiterSlingshot(jupiterRefit, catalog),
        "installing Fuel Tanks III must never hide or disable the Mars slingshot");

    GameState both = jupiterRefit;
    both.run.pendingTransferAssist = PendingTransferAssist {
        content::transferAssist::marsJupiter,
        content::destination::mars,
        content::destination::jupiter,
        FlybyGrade::Perfect,
        tuning::flyby::jupiterSlingshotFuelSavings,
        achievedSpeedBoost,
        0.0};
    const Destination* jupiter = catalog.findDestination(content::destination::jupiter);
    require(jupiter != nullptr &&
            nearlyEqual(launchFuelCapacity(both), 25.0) &&
            nearlyEqual(launchPoweredFuelCost(
                launchCruiseFuelCostForTier(jupiter->tier),
                tuning::launch::calibratedThrottle,
                pendingLaunchFuelSavingsForDestination(both, jupiter->id)), 15.0) &&
            nearlyEqual(calibratedTransferFuelMargin(both, *jupiter), 10.0) &&
            jupiterTransferMarginReady(both),
        "Fuel Tanks III and the Mars slingshot must stack to 25 tank, 15 burn, and 10 margin");

    Random inheritedLaunchRng(0xF0047);
    const PreparedLaunch inheritedLaunch = prepareLaunch(
        slingshotOnly,
        catalog,
        inheritedLaunchRng);
    const LaunchFlightState inheritedFlight = beginLaunchFlight(
        inheritedLaunch,
        *jupiter);
    const double expectedInheritedVelocity =
        tuning::launch::pilotingBaseProgressRate *
        (tuning::launch::pilotingPoweredSteeringBase +
            tuning::launch::pilotingInitialThrottle) /
        (1.0 + static_cast<double>(jupiter->tier) *
            tuning::launch::pilotingTierDurationScale) *
        (1.0 + achievedSpeedBoost);
    require(nearlyEqual(
                inheritedLaunch.slingshotCourseOffset,
                expectedLaunchCourseOffset) &&
            nearlyEqual(inheritedFlight.courseOffset, expectedLaunchCourseOffset),
        "the post-slingshot launch must begin at the saved left/right corridor position");
    require(nearlyEqual(
                inheritedFlight.travelVelocity,
                expectedInheritedVelocity) &&
            inheritedFlight.travelVelocity > 0.0,
        "the post-slingshot launch must start with the earned physical velocity instead of accelerating from rest");

    PreparedLaunch centeredAssistLaunch = inheritedLaunch;
    centeredAssistLaunch.controlChaos = 0.0;
    centeredAssistLaunch.slingshotCourseOffset = 0.0;
    const LaunchFlightState centeredAssistFlight = beginLaunchFlight(
        centeredAssistLaunch,
        *jupiter);
    PreparedLaunch greenAssistLaunch = centeredAssistLaunch;
    greenAssistLaunch.slingshotCourseOffset =
        tuning::launch::pilotingCourseCaution * 0.90;
    const LaunchFlightState greenAssistFlight = beginLaunchFlight(
        greenAssistLaunch,
        *jupiter);
    require(nearlyEqual(centeredAssistFlight.courseVelocity, 0.0) &&
            greenAssistFlight.courseVelocity > 0.0 &&
            nearlyEqual(
                greenAssistFlight.courseVelocity,
                greenAssistLaunch.slingshotCourseOffset /
                    launchCourseLimit(greenAssistLaunch) *
                    tuning::launch::slingshotExitCourseDrift),
        "farther off-center transfer exits must seed proportionally wilder outward launch navigation");

    PreparedLaunch heatBaseline;
    heatBaseline.config.destinationId = jupiter->id;
    heatBaseline.config.frontierTransfer = true;
    heatBaseline.fuelCapacity = 25.0;
    heatBaseline.cruiseFuelCost = 20.0;
    heatBaseline.heatEnabled = true;
    PreparedLaunch heatSlingshot = heatBaseline;
    heatSlingshot.slingshotFuelSavings = 5.0;
    heatSlingshot.slingshotSpeedBoost = achievedSpeedBoost;
    LaunchFlightState baselineFlight = beginLaunchFlight(heatBaseline, *jupiter);
    LaunchFlightState slingshotFlight = beginLaunchFlight(heatSlingshot, *jupiter);
    baselineFlight.selectedThrottle = tuning::launch::calibratedThrottle;
    slingshotFlight.selectedThrottle = tuning::launch::calibratedThrottle;
    (void)updateLaunchFlight(baselineFlight, heatBaseline, *jupiter, {}, 0.04);
    (void)updateLaunchFlight(slingshotFlight, heatSlingshot, *jupiter, {}, 0.04);
    require(nearlyEqual(baselineFlight.heat, slingshotFlight.heat),
        "gravity-provided slingshot velocity must add no powered heat input");

    const auto hangarHtml = [&](GameState rendered, std::uint64_t seed) {
        rendered.screen = Screen::Hangar;
        syncLaunchConfig(rendered, catalog);
        Random panelRng(seed);
        const PreparedLaunch panelLaunch = prepareLaunch(rendered, catalog, panelRng);
        return buildGamePanelHtml({rendered, catalog, panelLaunch, panelLaunch});
    };
    const std::string neitherHtml = hangarHtml(neither, 0xF010);
    const std::string tanksHtml = hangarHtml(jupiterRefit, 0xF011);
    const std::string slingshotHtml = hangarHtml(slingshotOnly, 0xF012);
    const std::string bothHtml = hangarHtml(both, 0xF013);
    require(neitherHtml.find("THE JUPITER WINDOW") != std::string::npos &&
            neitherHtml.find("Create five fuel of transfer margin") != std::string::npos &&
            neitherHtml.find("25 tank // 15 burn // 10 margin // +slingshot velocity") != std::string::npos,
        "the Jupiter story beat must expose both independent choices and the combined preview");
    require(neitherHtml.find("20 tank") != std::string::npos &&
            neitherHtml.find("0 MARGIN") != std::string::npos &&
            neitherHtml.find("hangar-frontier-meter\" aria-hidden=\"true\"><i") != std::string::npos,
        "the Hangar readiness strip must show the locked neither-path state without text inside its segmented bar");
    require(neitherHtml.find("Good-or-better Flyby") != std::string::npos &&
            neitherHtml.find("Good: +35% instability") != std::string::npos,
        "the locked Hangar must explain that Good departs with a visible instability cost");
    require(tanksHtml.find("25 tank") != std::string::npos &&
            tanksHtml.find("+5 MARGIN") != std::string::npos &&
            tanksHtml.find("Transfer: Jupiter") != std::string::npos &&
            tanksHtml.find("Begin Mars Slingshot") != std::string::npos,
        "Fuel Tanks III must open Jupiter while preserving the independent slingshot action");
    require(slingshotHtml.find("20 tank") != std::string::npos &&
            slingshotHtml.find("15 powered burn") != std::string::npos &&
            slingshotHtml.find("+5 MARGIN") != std::string::npos &&
            slingshotHtml.find("SLINGSHOT ACTIVE") != std::string::npos &&
            slingshotHtml.find("stable flight") != std::string::npos,
        "the slingshot-only Hangar must show its exact physical transfer math");
    const std::string goodHtml = hangarHtml(goodSlingshot, 0xF014);
    require(goodHtml.find("SLINGSHOT ACTIVE // WILD RIDE") != std::string::npos &&
            goodHtml.find("+35% flight instability") != std::string::npos,
        "the Good slingshot Hangar must make the wilder Jupiter flight explicit");
    require(bothHtml.find("25 tank") != std::string::npos &&
            bothHtml.find("15 powered burn") != std::string::npos &&
            bothHtml.find("+10 MARGIN") != std::string::npos,
        "the both-path Hangar must visibly stack permanent tank capacity and flyby savings");
    require(tanksHtml.find("Launch: Mars") != std::string::npos &&
            tanksHtml.find("Begin Mars Slingshot") != std::string::npos &&
            tanksHtml.find("Transfer: Jupiter") != std::string::npos,
        "Mars replay, slingshot, and Jupiter transfer must remain separate peer actions");

    const auto jupiterArrivalHtml = [&](double tank, double savings, double instability, std::uint64_t seed) {
        GameState result = both;
        result.screen = Screen::Results;
        result.lastOutcome = {};
        result.lastOutcome.type = LaunchResultType::MissionComplete;
        result.lastOutcome.failureCause = LaunchFailureCause::None;
        result.lastOutcome.recoveryMethod = RecoveryMethod::TransferArrival;
        result.lastOutcome.destinationId = content::destination::jupiter;
        result.lastOutcome.transferFuelCapacity = tank;
        result.lastOutcome.slingshotFuelSavings = savings;
        result.lastOutcome.slingshotInstabilityPenalty = instability;
        Random panelRng(seed);
        const PreparedLaunch panelLaunch = prepareLaunch(result, catalog, panelRng);
        return buildGamePanelHtml({result, catalog, panelLaunch, panelLaunch});
    };
    require(jupiterArrivalHtml(25.0, 0.0, 0.0, 0xF020).find("PERMANENT ENGINEERING MARGIN") != std::string::npos &&
            jupiterArrivalHtml(20.0, 5.0, 0.0, 0xF021).find("BORROWED MOMENTUM") != std::string::npos &&
            jupiterArrivalHtml(25.0, 5.0, 0.0, 0xF022).find("MAXIMUM PREPARATION") != std::string::npos &&
            jupiterArrivalHtml(20.0, 5.0, tuning::flyby::jupiterSlingshotGoodInstabilityPenalty, 0xF023)
                    .find("BORROWED MOMENTUM — WILD RIDE") != std::string::npos,
        "Jupiter arrival must acknowledge tanks-only, slingshot-only, and stacked methods distinctly");
}

void refitRerollsSpendAndEscalate()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 303);
    state.run.credits = 200.0;

    Random rng(3030);
    generateModuleOffers(state, catalog, rng);

    require(offerRerollCost(state) == tuning::hangar::rerollBaseCost, "first refit reroll should cost the tuned base credits");
    require(rerollOffers(state, catalog, rng), "affordable refit reroll should succeed");
    require(state.run.credits == 190.0, "first refit reroll should spend 10 credits");
    require(state.run.offerRerollsThisExpedition == 1, "first refit reroll should increment run reroll count");
    require(offerRerollCost(state) == tuning::hangar::rerollBaseCost * 2.0, "second refit reroll should cost twice the base credits");

    require(rerollOffers(state, catalog, rng), "second affordable refit reroll should succeed");
    require(state.run.credits == 170.0, "second refit reroll should spend 20 credits");
    require(state.run.offerRerollsThisExpedition == 2, "second refit reroll should increment run reroll count");
    require(offerRerollCost(state) == tuning::hangar::rerollBaseCost * 3.0, "third refit reroll should cost three times the base credits");

    state.run.credits = 29.0;
    require(!rerollOffers(state, catalog, rng), "reroll should be blocked when credits are short");
    require(state.run.offerRerollsThisExpedition == 2, "failed reroll should not increment run reroll count");

    startNewExpedition(state, catalog);
    require(state.run.offerRerollsThisExpedition == 0, "new expedition should reset reroll escalation");
}

void specialShipComponentsRequireRecoveredMaterials()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 434);
    state.meta.unlockKeys.push_back(content::unlock::deepSpace);
    state.run.credits = 200.0;
    state.meta.materials = {.common = 2};
    state.run.offerModuleIds = {content::module::deepBoreFrame, "", ""};
    state.run.offerCrewUpgradeIds = {};

    const ShipModule* module = catalog.findModule(content::module::deepBoreFrame);
    require(module != nullptr, "special component test needs a material-gated Surface module");
    require(module->materialCost.common == 2 && module->materialCost.rare == 1, "deep-bore frame should require recovered materials");
    require(!canAffordModuleOffer(state, *module), "special ship components should check material affordability");
    require(!buyOffer(state, catalog, 0), "buying without required materials should fail");
    require(state.run.credits == 200.0, "failed material-gated refit should not spend credits");

    const RefitWindowPresentation blocked = refitWindowPresentation(state, catalog);
    require(!blocked.offers.empty(), "material-gated refit should still present the offer");
    require(!blocked.offers.front().affordable, "material-gated offer should expose unaffordable state");

    state.meta.materials.rare = 1;
    require(canAffordModuleOffer(state, *module), "adding recovered materials should satisfy special component cost");
    require(buyOffer(state, catalog, 0), "buying with credits and materials should succeed");
    require(state.meta.materials.common == 0 && state.meta.materials.rare == 0, "buying special component should spend recovered materials");
    require(std::find(state.run.inventoryModuleIds.begin(), state.run.inventoryModuleIds.end(), content::module::deepBoreFrame) != state.run.inventoryModuleIds.end(), "bought special component should enter inventory");
    require(std::find(state.meta.ownedModuleIds.begin(), state.meta.ownedModuleIds.end(), content::module::deepBoreFrame) != state.meta.ownedModuleIds.end(), "bought special component should enter permanent shipyard inventory");
}

void preMiningRefitOffersAvoidMaterialCosts()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 441);
    state.run.credits = 400.0;
    state.meta.furthestTier = 1;
    state.meta.unlockKeys = {
        content::unlock::starter,
        content::unlock::thermal,
        content::unlock::recovery,
        content::unlock::deepSpace,
        content::unlock::ai,
        content::unlock::exotic
    };
    for (const ShipModule& module : catalog.modules) {
        if (module.materialCost.common == 0 && module.materialCost.rare == 0 && module.materialCost.exotic == 0) {
            state.meta.ownedModuleIds.push_back(module.id);
        }
    }

    Random rng(4410);
    generateModuleOffers(state, catalog, rng);

    bool sawOffer = false;
    for (const std::string& moduleId : state.run.offerModuleIds) {
        if (moduleId.empty()) {
            continue;
        }
        const ShipModule* module = catalog.findModule(moduleId);
        require(module != nullptr, "generated module offer should resolve");
        sawOffer = true;
        require(module->materialCost.common == 0 && module->materialCost.rare == 0 && module->materialCost.exotic == 0, "pre-mining refit offers should not require materials");
    }
    for (const std::string& upgradeId : state.run.offerCrewUpgradeIds) {
        if (!upgradeId.empty()) {
            sawOffer = true;
        }
    }
    require(sawOffer, "pre-mining refits should still offer an installable path");
}


void shipModuleProgressSurvivesDestroyedVehicles()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 435);
    state.meta.unlockKeys.push_back(content::unlock::thermal);
    state.run.credits = 200.0;
    state.run.offerModuleIds = {content::module::cryoLoop, "", ""};
    state.run.offerCrewUpgradeIds = {};

    require(buyOffer(state, catalog, 0), "buying a thermal ship module should succeed");
    require(std::find(state.meta.ownedModuleIds.begin(), state.meta.ownedModuleIds.end(), content::module::cryoLoop) != state.meta.ownedModuleIds.end(), "ship upgrades should become permanent shipyard tech");
    require(std::find(state.meta.defaultEquippedModuleIds.begin(), state.meta.defaultEquippedModuleIds.end(), content::module::cryoLoop) != state.meta.defaultEquippedModuleIds.end(), "installed ship upgrades should become the default new-build loadout");

    LaunchOutcome damaged;
    damaged.type = LaunchResultType::SafeEject;
    damaged.recoveryMethod = RecoveryMethod::ReturnHome;
    damaged.destinationId = currentDestination(state, catalog).id;
    damaged.moduleDestroyedId = content::module::cryoLoop;
    damaged.ejectMultiplier = 1.05;
    applyLaunchOutcome(state, catalog, damaged);
    require(std::find(state.run.equippedModuleIds.begin(), state.run.equippedModuleIds.end(), content::module::cryoLoop) == state.run.equippedModuleIds.end(), "damaged permanent tech should go offline for the current expedition");
    require(std::find(state.run.inventoryModuleIds.begin(), state.run.inventoryModuleIds.end(), content::module::cryoLoop) != state.run.inventoryModuleIds.end(), "offline tech should remain in permanent inventory");
    startNewExpedition(state, catalog);

    require(std::find(state.meta.ownedModuleIds.begin(), state.meta.ownedModuleIds.end(), content::module::cryoLoop) != state.meta.ownedModuleIds.end(), "ship destruction should not erase permanent shipyard tech");
    require(std::find(state.run.inventoryModuleIds.begin(), state.run.inventoryModuleIds.end(), content::module::cryoLoop) != state.run.inventoryModuleIds.end(), "replacement ships should inherit permanent shipyard inventory");
    require(std::find(state.run.equippedModuleIds.begin(), state.run.equippedModuleIds.end(), content::module::cryoLoop) != state.run.equippedModuleIds.end(), "replacement ships should keep the improved default loadout");
}

void deadCrewLosesTraining()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 457);
    Astronaut* pilot = activeAstronaut(state);
    require(pilot != nullptr, "starter pilot should exist");
    pilot->training = 7;

    LaunchOutcome destroyed;
    destroyed.type = LaunchResultType::Destroyed;
    destroyed.destinationId = currentDestination(state, catalog).id;
    destroyed.shipDamage = tuning::damage::destroyedShipDamage;
    destroyed.crewKilled = true;
    applyLaunchOutcome(state, catalog, destroyed);

    require(!state.run.crew.empty(), "dead pilot should remain in the memorial roster");
    require(state.run.crew.front().status == CrewStatus::Dead, "pilot should be marked dead");
    require(state.run.crew.front().training == 0, "dead pilot training should reset");
}

void crewUpgradeOffersInstallAndModifyCrewOps()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 606);
    state.run.credits = 200.0;
    state.meta.unlockKeys = {
        content::unlock::starter,
        content::unlock::thermal,
        content::unlock::recovery,
        content::unlock::deepSpace,
        content::unlock::ai
    };
    state.run.inventoryModuleIds.clear();
    for (const ShipModule& module : catalog.modules) {
        state.run.inventoryModuleIds.push_back(module.id);
    }
    state.meta.ownedModuleIds = state.run.inventoryModuleIds;

    Random rng(6060);
    generateModuleOffers(state, catalog, rng);

    int crewOfferIndex = -1;
    for (std::size_t i = 0; i < state.run.offerCrewUpgradeIds.size(); ++i) {
        if (!state.run.offerCrewUpgradeIds[i].empty()) {
            crewOfferIndex = static_cast<int>(i);
            break;
        }
    }
    require(crewOfferIndex >= 0, "refit offers should include crew upgrades when ship module pool is owned");
    const std::string upgradeId = state.run.offerCrewUpgradeIds[static_cast<std::size_t>(crewOfferIndex)];
    require(buyOffer(state, catalog, crewOfferIndex), "buying a crew upgrade refit should succeed");
    require(std::find(state.run.crewUpgradeIds.begin(), state.run.crewUpgradeIds.end(), upgradeId) != state.run.crewUpgradeIds.end(), "crew upgrade should be installed");

    state.run.crewUpgradeIds = {
        content::crewUpgrade::analogSimBay,
        content::crewUpgrade::highGSimulator,
        content::crewUpgrade::medicalRecoveryWard,
        content::crewUpgrade::missionPsychOffice,
        content::crewUpgrade::traitCoachingLab
    };
    CrewUpgradeStats stats = aggregateCrewUpgradeStats(state, catalog);
    require(stats.trainingGain == 1, "crew simulator upgrades should aggregate training gain");
    require(stats.trainingStressRelief == 13, "crew simulator upgrades should aggregate stress relief");
    require(stats.restStressBonus == 12, "medical upgrades should aggregate rest bonus");
    require(stats.launchStressRelief == 5, "psych upgrades should aggregate launch stress relief");
    require(stats.traitModifier > 0.34, "trait upgrades should aggregate trait modifiers");

    Astronaut* pilot = activeAstronaut(state);
    require(pilot != nullptr, "crew upgrade test needs a pilot");
    pilot->training = 1;
    pilot->stress = 20;
    const double creditsBeforeTraining = state.run.credits;
    require(trainCrew(state, catalog), "crew training should use facility upgrades");
    require(pilot->training == 3, "upgraded simulator should grant extra training");
    require(pilot->stress == 41, "upgraded simulator should reduce training stress gain without eliminating stress");
    require(state.run.credits < creditsBeforeTraining, "training should still cost credits");
    require(crewTrainingStressGain(state, catalog) >= tuning::crew::stressPerStep, "crew training should always carry at least one stress step");
    pilot->training = tuning::crew::maxTraining;
    pilot->stress = 0;
    require(!trainCrew(state, catalog), "crew training should be blocked when no training benefit remains");
    pilot->training = 3;
    pilot->stress = tuning::crew::maxStress;
    require(!trainCrew(state, catalog), "crew training should be blocked at max stress");
    pilot->stress = tuning::crew::maxStress - crewTrainingStressGain(state, catalog) + 1;
    require(!trainCrew(state, catalog), "crew training should be blocked if it would overflow stress");

    pilot->stress = 60;
    pilot->status = CrewStatus::Injured;
    require(crewRestStressRecovery(state, catalog) == 18, "unproven frontier difficulty should reduce medical rest recovery");
    const double restCostAt60 = crewRestCost(state, catalog);
    pilot->stress = 90;
    require(crewRestCost(state, catalog) > restCostAt60, "medical rest should cost more for more-stressed crews");
    pilot->stress = 60;
    require(restCrew(state, catalog), "medical rest should use facility upgrades");
    require(pilot->status == CrewStatus::Active, "medical rest should clear injury");
    require(pilot->stress == 33, "medical rest should not fully reset stress on an unproven frontier");

    pilot->stress = 10;
    LaunchOutcome outcome;
    outcome.type = LaunchResultType::SafeEject;
    outcome.recoveryMethod = RecoveryMethod::ReturnHome;
    outcome.destinationId = catalog.destinations[0].id;
    outcome.ejectMultiplier = 1.2;
    applyLaunchOutcome(state, catalog, outcome);
    require(pilot->stress == 17, "psych facility should reduce post-launch stress gain");

    GameState baseline = createNewGame(catalog, 707);
    GameState upgraded = baseline;
    upgraded.run.crewUpgradeIds = {
        content::crewUpgrade::missionPsychOffice,
        content::crewUpgrade::traitCoachingLab
    };
    Random baselineRng(7070);
    Random upgradedRng(7070);
    const PreparedLaunch baselineLaunch = prepareLaunch(baseline, catalog, baselineRng);
    const PreparedLaunch upgradedLaunch = prepareLaunch(upgraded, catalog, upgradedRng);
    require(nearlyEqual(upgradedLaunch.fuelCapacity, baselineLaunch.fuelCapacity) &&
            nearlyEqual(upgradedLaunch.controlChaos, baselineLaunch.controlChaos) &&
            upgradedLaunch.coolingRank == baselineLaunch.coolingRank &&
            upgradedLaunch.hullRank == baselineLaunch.hullRank,
        "unrelated crew facilities must not secretly improve the four direct launch systems");
}

void hangarOpsStartCheapAndEscalate()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 808);
    state.run.credits = 300.0;
    state.run.shipDamage = 80;

    const double firstRepairCost = repairShipCost(state);
    require(firstRepairCost < 30.0, "first repair assignment should be affordable after a rough run");
    require(repairShipAmount(state) == 35, "repair bay should still cap each assignment");
    require(repairShip(state), "first repair should succeed");
    const double secondRepairCost = repairShipCost(state);
    require(secondRepairCost > firstRepairCost, "second repair assignment should cost more this expedition");
    require(repairShip(state), "second repair should still be possible with enough credits");
    require(repairShipCost(state) > secondRepairCost, "third repair assignment should continue escalating");

    Astronaut* pilot = activeAstronaut(state);
    require(pilot != nullptr, "hangar op test needs a pilot");
    pilot->stress = 20;
    const double firstTrainingCost = crewTrainingCost(state, catalog);
    require(firstTrainingCost < 18.0, "first simulator burn should be cheaper than the old flat cost");
    require(trainCrew(state, catalog), "first simulator burn should succeed");
    require(crewTrainingCost(state, catalog) > firstTrainingCost, "training should escalate after use");

    pilot->stress = 60;
    const double firstRestCost = crewRestCost(state, catalog);
    require(firstRestCost < 20.0, "first medical rest should be cheaper for stressed crews");
    require(restCrew(state, catalog), "first medical rest should succeed");
    require(crewRestCost(state, catalog) > firstRestCost, "medical rest should escalate after use");

    startNewExpedition(state, catalog);
    require(state.run.repairOpsThisExpedition == 0, "new expedition should reset repair escalation");
    require(state.run.trainingOpsThisExpedition == 0, "new expedition should reset training escalation");
    require(state.run.restOpsThisExpedition == 0, "new expedition should reset rest escalation");
}

void medicalRestEscalationResetsAfterSurvivedMission()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 809);
    state.run.credits = 300.0;
    Astronaut* pilot = activeAstronaut(state);
    require(pilot != nullptr, "medical rest reset test needs a pilot");
    pilot->stress = 80;

    const double baseRestCost = crewRestCost(state, catalog);
    require(restCrew(state, catalog), "first medical rest should succeed");
    pilot->stress = 80;
    require(crewRestCost(state, catalog) > baseRestCost, "medical rest should escalate before a flight");

    LaunchOutcome outcome;
    outcome.type = LaunchResultType::SafeEject;
    outcome.recoveryMethod = RecoveryMethod::ReturnHome;
    outcome.destinationId = catalog.destinations.front().id;
    outcome.ejectMultiplier = 1.3;
    applyLaunchOutcome(state, catalog, outcome);

    pilot = activeAstronaut(state);
    require(pilot != nullptr, "pilot should survive the recovered mission");
    pilot->stress = 80;
    require(state.run.restOpsThisExpedition == 0, "survived missions should reset medical rest escalation");
    require(std::abs(crewRestCost(state, catalog) - baseRestCost) < 0.001, "medical rest should return to base cost after a survived mission");
}

void hangarOperationPreviewMatchesCoreMath()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 818);
    state.run.credits = 120.0;
    state.run.shipDamage = 48;
    Astronaut* pilot = activeAstronaut(state);
    require(pilot != nullptr, "hangar preview test needs a pilot");
    pilot->stress = 42;

    HangarOperationPreview preview = hangarOperationPreview(state, catalog);
    require(preview.repairAmount == repairShipAmount(state), "hangar preview should share repair amount");
    require(std::abs(preview.repairCost - repairShipCost(state)) < 0.001, "hangar preview should share repair cost");
    require(preview.trainingGain == crewTrainingGain(state, catalog), "hangar preview should share training gain");
    require(preview.trainingStressGain == crewTrainingStressGain(state, catalog), "hangar preview should share simulator stress");
    require(std::abs(preview.trainingCost - crewTrainingCost(state, catalog)) < 0.001, "hangar preview should share training cost");
    require(preview.restStressRecovery == crewRestStressRecovery(state, catalog), "hangar preview should share rest recovery");
    require(std::abs(preview.restCost - crewRestCost(state, catalog)) < 0.001, "hangar preview should share rest cost");
    require(preview.repairAvailable && preview.trainingAvailable && preview.restAvailable, "funded hangar preview should mark available ops");

    pilot->training = tuning::crew::maxTraining;
    preview = hangarOperationPreview(state, catalog);
    require(!preview.trainingAvailable, "hangar preview should block training when the pilot is capped");
    pilot->training = 0;

    pilot->stress = tuning::crew::maxStress;
    preview = hangarOperationPreview(state, catalog);
    require(!preview.trainingAvailable, "hangar preview should block training at max stress");

    pilot->stress = 0;
    pilot->status = CrewStatus::Active;
    preview = hangarOperationPreview(state, catalog);
    require(!preview.restNeeded, "hangar preview should not need medical rest for a healthy calm crew");
    require(!preview.restAvailable, "hangar preview should block medical rest when there is no benefit");
    require(!restCrew(state, catalog), "medical rest should not spend credits on a healthy calm crew");

    pilot->status = CrewStatus::Injured;
    preview = hangarOperationPreview(state, catalog);
    require(preview.restNeeded && preview.restAvailable, "injured crew should still be eligible for medical rest at zero stress");

    for (Astronaut& astronaut : state.run.crew) {
        astronaut.status = CrewStatus::Dead;
    }
    state.run.credits = 0.0;
    preview = hangarOperationPreview(state, catalog);
    require(preview.emergencyRecruitment, "hangar preview should flag emergency recruitment when no pilot is alive");
    require(preview.recruitCost == tuning::hangar::emergencyRecruitCost, "hangar preview should use emergency recruit cost");
    require(preview.recruitAvailable, "free emergency recruitment should be available when broke");
}


void totaledShipCanAlwaysReachSalvageRepair()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 820);
    state.meta.launchLessons.stage = LaunchTrainingStage::MoonTransfer;
    state.run.shipDamage = tuning::damage::destroyedShipDamage;
    state.run.credits = 5.0;

    const int repaired = repairShipAmount(state);
    const HangarOperationPreview preview = hangarOperationPreview(state, catalog);
    require(repaired == tuning::hangar::repairAmountCap, "salvage rebuild should still use the repair bay cap");
    require(preview.repairAvailable, "totaled ships should always have a repair path even when broke");
    require(preview.repairCost == 5.0, "salvage rebuild should consume remaining credits instead of blocking");

    const std::vector<HangarOperationCardPresentation> cards = hangarOperationCards(state, catalog);
    const bool repairActionAvailable = std::any_of(cards.begin(), cards.end(), [](const HangarOperationCardPresentation& card) {
        return card.actionId == ui::actions::repairShip && card.available;
    });
    require(repairActionAvailable, "salvage rebuild should expose an available repair action");

    require(repairShip(state), "salvage rebuild should repair a totaled ship");
    require(state.run.credits == 0.0, "salvage rebuild should consume remaining credits");
    require(state.run.shipDamage == tuning::damage::destroyedShipDamage - repaired, "salvage rebuild should make the ship launchable but damaged");
}

void lowCreditRefitWindowIncludesAffordableOffer()
{
    const ContentCatalog catalog = createDefaultContent();

    for (int i = 0; i < 40; ++i) {
        GameState state = createNewGame(catalog, 220 + static_cast<std::uint64_t>(i));
        state.run.credits = 35.0;
        state.meta.unlockKeys = {
            content::unlock::starter,
            content::unlock::thermal,
            content::unlock::recovery
        };

        Random rng(77000 + static_cast<std::uint64_t>(i));
        generateModuleOffers(state, catalog, rng);

        bool affordable = false;
        for (std::size_t offerIndex = 0; offerIndex < state.run.offerModuleIds.size(); ++offerIndex) {
            if (const ShipModule* module = catalog.findModule(state.run.offerModuleIds[offerIndex])) {
                affordable = affordable || state.run.credits >= static_cast<double>(moduleOfferCost(*module));
            }
            if (const CrewUpgrade* upgrade = catalog.findCrewUpgrade(state.run.offerCrewUpgradeIds[offerIndex])) {
                affordable = affordable || state.run.credits >= static_cast<double>(crewUpgradeCost(*upgrade));
            }
        }
        require(affordable, "a clean starter return should see at least one affordable early refit");
    }
}

void researchPhasesUnlockOnlyAfterMarsArrival()
{
    const ContentCatalog catalog = createDefaultContent();

    LaunchOutcome moonArrival;
    moonArrival.type = LaunchResultType::MissionComplete;
    moonArrival.frontierTransfer = true;
    moonArrival.destinationId = content::destination::moon;
    require(!shouldOpenPostArrivalPhases(moonArrival, catalog), "Moon arrival should not open the Mars research loop yet");

    LaunchOutcome marsArrival = moonArrival;
    marsArrival.destinationId = content::destination::mars;
    require(shouldOpenPostArrivalPhases(marsArrival, catalog), "Mars arrival should support the later post-arrival surface systems");
    const std::vector<PhaseStepPresentation> arrivalSteps = postArrivalPhaseSteps(Screen::Results);
    require(arrivalSteps.size() == 4, "arrival result should expose the full post-arrival phase track");
    require(arrivalSteps[0].stateClass == "active" && arrivalSteps[1].stateClass == "pending",
        "arrival phase track should mark the current phase and pending follow-up");
}

void introduceArrivalFlybyForTest(GameState& state)
{
    ScenarioInstance* scenario = findScenarioInstance(state.meta, content::scenario::marsBayExpansion);
    ScenarioStepProgress* funding = scenario == nullptr ? nullptr : findScenarioStepProgress(*scenario, "funding");
    require(funding != nullptr, "the Mars transfer-assist briefing should exist in test state");
    funding->briefingAcknowledged = true;
}

void arrivalOperationsUseMutuallyExclusiveCommitments()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 607);

    LaunchOutcome moonArrival;
    moonArrival.type = LaunchResultType::MissionComplete;
    moonArrival.frontierTransfer = true;
    moonArrival.destinationId = content::destination::moon;
    require(shouldOpenArrivalOps(moonArrival, catalog), "Moon transfer arrival should open arrival operations");

    LaunchOutcome jupiterArrival;
    jupiterArrival.type = LaunchResultType::MissionComplete;
    jupiterArrival.frontierTransfer = true;
    jupiterArrival.destinationId = content::destination::jupiter;
    require(shouldOpenArrivalOps(jupiterArrival, catalog), "Jupiter transfer arrival should open arrival operations");

    startArrivalOps(state, moonArrival);
    require(!canRunArrivalFlyby(state, catalog), "Moon Flyby should stay hidden until the Jupiter transfer window introduces it");
    require(canEnterArrivalOrbit(state, catalog), "Moon orbit should be available as an initial approach path");
    require(!canAttemptArrivalLanding(state, catalog), "the first Moon landing should require an orbit capture");
    require(captureArrivalOrbit(state), "a successful orbit should capture the approach");
    require(!canRunArrivalFlyby(state, catalog), "captured orbit should close Pass Through");
    require(!canEnterArrivalOrbit(state, catalog), "captured orbit should close repeat capture");
    require(canAttemptArrivalLanding(state, catalog), "captured orbit should expose mapped landing");
    require(!canDepartCapturedArrivalOrbit(state, catalog), "the first Moon orbit must continue to landing instead of departing with science");
    require(!hasUnlock(state.meta, content::unlock::routeMars),
        "Moon landing alone must not silently chart Mars; the explicit Prospector contract owns that route unlock");
    state.run.destinationIndex = 1;
    require(!flybyClearsGenericNextRoute(state, catalog), "Moon Pass Through must not bypass the authored Mars route");
    require(!bankFlybyRouteClearance(state, catalog), "authored story routes must reject generic Flyby clearance");

    GameState returnMoon = createNewGame(catalog, 6071);
    returnMoon.meta.destinationLandings[1] = 1;
    startArrivalOps(returnMoon, moonArrival);
    require(canAttemptArrivalLanding(returnMoon, catalog), "later Moon visits should restore direct descent");
    require(captureArrivalOrbit(returnMoon) && canDepartCapturedArrivalOrbit(returnMoon, catalog),
        "later Moon visits should restore departure after a captured orbit");

    GameState generic = createNewGame(catalog, 611);
    generic.run.destinationIndex = 4;
    introduceArrivalFlybyForTest(generic);
    LaunchOutcome saturnArrival = moonArrival;
    saturnArrival.destinationId = content::destination::saturn;
    startArrivalOps(generic, saturnArrival);
    require(flybyClearsGenericNextRoute(generic, catalog), "Saturn Pass Through should expose the generic Uranus fast route");
    require(bankFlybyRouteClearance(generic, catalog), "successful generic Flyby should fill onward Flight Data readiness");
    require(generic.run.frontierReadiness == frontierReadinessCap(generic, catalog), "generic Flyby should fill the exact route-readiness cap");

    LaunchOutcome marsArrival = moonArrival;
    marsArrival.destinationId = content::destination::mars;
    GameState mars = createNewGame(catalog, 608);
    startArrivalOps(mars, marsArrival);
    require(canAttemptArrivalLanding(mars, catalog), "Mars landing should allow an unmapped direct descent without prior recon");
    const Destination* marsDestination = catalog.findDestination(content::destination::mars);
    require(marsDestination != nullptr, "Mars destination should resolve");
    const double expectedNoReconPenalty = 0.20;
    startSurfaceExpedition(mars, catalog);
    require(mars.run.surfaceExpedition.active, "Mars YOLO landing should start surface operations");
    require(mars.run.surfaceExpedition.hazard >= tuning::research::baseHazard + marsDestination->tier * tuning::research::hazardPerTier + expectedNoReconPenalty - 0.001, "direct descent should carry the visible +0.20 surface hazard");

    GameState mapped = createNewGame(catalog, 609);
    startArrivalOps(mapped, marsArrival);
    require(captureArrivalOrbit(mapped), "mapped descent setup should capture orbit");
    startSurfaceExpedition(mapped, catalog);
    require(mapped.run.surfaceExpedition.hazard <= mars.run.surfaceExpedition.hazard - expectedNoReconPenalty + 0.08,
        "captured orbit should remove the visit's unmapped descent penalty");

    GameState historical = createNewGame(catalog, 610);
    historical.meta.destinationFlybys.resize(catalog.destinations.size(), 4);
    historical.meta.destinationOrbits.resize(catalog.destinations.size(), 4);
    startArrivalOps(historical, marsArrival);
    startSurfaceExpedition(historical, catalog);
    require(historical.run.surfaceExpedition.hazard >= tuning::research::baseHazard + marsDestination->tier * tuning::research::hazardPerTier + expectedNoReconPenalty - 0.001,
        "archive histories must not make a later direct descent safer");
}

void arrivalPresentationExplainsCommitmentAndResearchProgress()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 612);
    state.run.destinationIndex = 1;
    state.screen = Screen::ArrivalOps;
    state.meta.blueprintProgress = 7;
    LaunchOutcome arrival;
    arrival.type = LaunchResultType::MissionComplete;
    arrival.frontierTransfer = true;
    arrival.destinationId = content::destination::moon;
    arrival.transferFuelCapacity = 15.0;
    startArrivalOps(state, arrival);
    Random rng(612);
    const PreparedLaunch launch = prepareLaunch(state, catalog, rng);
    PanelRenderContext context {state, catalog, launch, launch};
    context.firstTimeIntroductionsEnabled = false;

    const std::string uncommitted = buildGamePanelHtml(context);
    require(uncommitted.find("APPROACH UNCOMMITTED") != std::string::npos
            && uncommitted.find("ORBIT — CAPTURE") != std::string::npos
            && uncommitted.find("FLYBY — PASS THROUGH") == std::string::npos
            && uncommitted.find("DIRECT DESCENT") == std::string::npos,
        "the first Moon arrival should present only its required Orbit capture path");
    require(uncommitted.find("UNMAPPED +20") != std::string::npos
            && uncommitted.find("7/8 • RECOVERY") != std::string::npos
            && uncommitted.find("future Refit offers") != std::string::npos,
        "Arrival board should expose exact unmapped hazard and current/next Research Data milestone meaning");
    require(uncommitted.find("First landing protocol: Capture Orbit, then Land") != std::string::npos,
        "Moon approach copy should explain the required Orbit-to-Land sequence");
    require(uncommitted.find("data-rr-action=\"arrival_flyby\" data-ui-focus-id=\"action:arrival_flyby\" data-ui-default-focus=\"1\"") == std::string::npos
            && uncommitted.find("data-rr-action=\"arrival_orbit\" data-ui-focus-id=\"action:arrival_orbit\" data-ui-default-focus=\"1\"") == std::string::npos
            && uncommitted.find("data-rr-action=\"arrival_landing\" data-ui-focus-id=\"action:arrival_landing\" data-ui-default-focus=\"1\"") == std::string::npos,
        "uncommitted arrival choices must not let confirm make an irreversible approach decision");
    require(uncommitted.find("rr-button-label\">ORBIT</span>") != std::string::npos
            && uncommitted.find("rr-button-label\">LAND</span>") == std::string::npos
            && uncommitted.find("rr-button-label\">CAPTURE ORBIT</span>") == std::string::npos
            && uncommitted.find("rr-button-label\">DESCEND DIRECT</span>") == std::string::npos,
        "Arrival approach buttons should use the concise Orbit and Land labels");

    require(captureArrivalOrbit(state), "presentation setup should capture Orbit");
    PanelRenderContext capturedContext {state, catalog, launch, launch};
    capturedContext.firstTimeIntroductionsEnabled = false;
    const std::string captured = buildGamePanelHtml(capturedContext);
    require(captured.find("ORBIT CAPTURED") != std::string::npos
            && captured.find("LAND") != std::string::npos
            && captured.find("DEPART WITH SCIENCE") == std::string::npos
            && captured.find("data-rr-action=\"arrival_orbit_depart\"") == std::string::npos,
        "the first captured Moon orbit should expose only its required mapped landing");
    require(captured.find("data-rr-action=\"arrival_flyby\"") == std::string::npos
            && captured.find("data-rr-action=\"arrival_orbit\"") == std::string::npos,
        "captured Orbit presentation should close Pass Through and repeat capture actions");
    require(captured.find("data-rr-action=\"arrival_landing\" data-ui-focus-id=\"action:arrival_landing\" data-ui-default-focus=\"1\"") != std::string::npos,
        "captured Orbit should make the safe mapped Land action the explicit confirm default");

    state.meta.blueprintProgress = tuning::unlocks::blueprintUnlocks[0].threshold;
    state.meta.unlockKeys.push_back(std::string(tuning::unlocks::blueprintUnlocks[0].key));
    PanelRenderContext breakthroughContext {state, catalog, launch, launch};
    breakthroughContext.firstTimeIntroductionsEnabled = false;
    const PanelDocumentPresentation breakthrough = buildGamePanelPresentation(breakthroughContext);
    const auto pending = std::find_if(breakthrough.modals.begin(), breakthrough.modals.end(), [](const ModalPresentation& modal) {
        return modal.id == "research_breakthrough_thermal";
    });
    require(pending != breakthrough.modals.end() && pending->autoOpen && !pending->dismissible
            && pending->bodyMarkup.find("future Refit offers") != std::string::npos
            && pending->bodyMarkup.find("no module was granted") != std::string::npos,
        "crossed Research Data milestones should create an explicit saved breakthrough review");
    ui::briefings::acknowledge(state.meta.acknowledgedActivityBriefingIds, pending->closeAction);
    PanelRenderContext reviewedContext {state, catalog, launch, launch};
    reviewedContext.firstTimeIntroductionsEnabled = false;
    const PanelDocumentPresentation reviewed = buildGamePanelPresentation(reviewedContext);
    require(std::none_of(reviewed.modals.begin(), reviewed.modals.end(), [](const ModalPresentation& modal) {
        return modal.id == "research_breakthrough_thermal";
    }), "reviewed Research breakthrough should remain acknowledged in saved briefing state");
}

void solarRouteLegsKeepCampaignProgressSeparateFromShipPosition()
{
    const ContentCatalog catalog = createDefaultContent();
    std::string routeAuditError;
    require(validateRouteCatalog(catalog, &routeAuditError),
        "the authored solar route catalog should pass its source, target, fuel, and recovery-policy audit");
    const std::array<std::string_view, 6> routeIds {{
        content::routeLink::earthMoon,
        content::routeLink::moonMars,
        content::routeLink::marsJupiter,
        content::routeLink::jupiterSaturn,
        content::routeLink::saturnUranus,
        content::routeLink::uranusNeptune,
    }};
    for (const std::string_view id : routeIds) {
        const RouteLinkDefinition* route = catalog.findRouteLink(id);
        require(route != nullptr && catalog.findDestination(route->sourceDestinationId) != nullptr &&
                catalog.findDestination(route->targetDestinationId) != nullptr,
            "every solar route must name valid authored source and target destinations");
        require(route->cruiseFuelCost > 0.0,
            "every solar route must provide a calibrated flight profile");
    }
    const RouteLinkDefinition* marsJupiter = catalog.findRouteLink(content::routeLink::marsJupiter);
    const RouteLinkDefinition* jupiterSaturn = catalog.findRouteLink(content::routeLink::jupiterSaturn);
    require(marsJupiter != nullptr && marsJupiter->recoveryAvailable,
        "Mars to Jupiter must permit its authored return recovery flight");
    require(jupiterSaturn != nullptr && jupiterSaturn->oneWayExpedition && !jupiterSaturn->recoveryAvailable,
        "the Saturn expedition must retain its authored one-way policy");

    GameState state = createNewGame(catalog, 0x71A9);
    state.run.destinationIndex = 3;
    state.meta.launchLessons.stage = LaunchTrainingStage::Complete;
    LaunchOutcome jupiterArrival;
    jupiterArrival.type = LaunchResultType::MissionComplete;
    jupiterArrival.recoveryMethod = RecoveryMethod::TransferArrival;
    jupiterArrival.frontierTransfer = true;
    jupiterArrival.destinationId = content::destination::jupiter;
    jupiterArrival.routeTransit = makeRouteTransit(
        catalog,
        content::destination::mars,
        content::destination::jupiter,
        RouteTransitIntent::Outbound);
    startArrivalOps(state, jupiterArrival);
    require(state.run.arrivalOps.incomingRoute.active() &&
            state.run.arrivalOps.incomingRoute.originDestinationId == content::destination::mars,
        "arrival operations must retain the physical origin of their incoming leg");

    introduceArrivalFlybyForTest(state);
    startArrivalFlybyRun(state, catalog);
    state.run.flyby.completed = true;
    state.run.flyby.result = FlybyGrade::Good;
    state.run.flyby.elapsedSeconds = tuning::flyby::minimumFinishSeconds;
    Random panelRng(0x71A9);
    const PreparedLaunch panelLaunch = prepareLaunch(state, catalog, panelRng);
    const std::string flybyStamp = buildGamePanelHtml({state, catalog, panelLaunch, panelLaunch});
    require(flybyStamp.find("RECOVERY ROUTE REQUIRED") != std::string::npos &&
            flybyStamp.find("Begin Recovery: Jupiter \xE2\x86\x92 Mars") != std::string::npos,
        "a blocked Jupiter Pass Through must offer a visible Jupiter-to-Mars recovery instead of an implicit relaunch");

    completeFlybyRun(state, catalog);
    require(state.screen == Screen::ArrivalOps,
        "a normal Jupiter flyby must return to the arrival result stamp");
    require(!flybyClearsGenericNextRoute(state, catalog),
        "a normal Jupiter flyby must preserve Io's authored onward gate");

    require(queueBlockedArrivalFlybyRecovery(state, catalog),
        "blocked authored flybys must queue their route-defined recovery leg");
    require(state.screen == Screen::Hangar && state.run.routeTransit.intent == RouteTransitIntent::Recovery &&
            state.run.routeTransit.originDestinationId == content::destination::jupiter &&
            state.run.routeTransit.targetDestinationId == content::destination::mars,
        "the recovery leg must travel from Jupiter back to Mars");
    syncLaunchConfig(state, catalog);
    Random recoveryRng(0x71AA);
    const PreparedLaunch recoveryLaunch = prepareLaunch(state, catalog, recoveryRng);
    require(recoveryLaunch.config.destinationId == content::destination::mars &&
            recoveryLaunch.routeProfileDestinationId == content::destination::jupiter &&
            std::abs(recoveryLaunch.cruiseFuelCost - marsJupiter->cruiseFuelCost) < 0.001,
        "recovery must arrive at Mars while reusing the authored Mars-Jupiter leg profile");

    const std::optional<SaveData> saved = deserializeSaveData(serializeSaveData(captureSaveData(state)));
    require(saved.has_value(), "pending recovery route should serialize in the version-14 save");
    GameState restored = createNewGame(catalog, 0x71AB);
    restoreSaveData(restored, catalog, *saved);
    require(restored.run.routeTransit.intent == RouteTransitIntent::Recovery &&
            restored.run.routeTransit.originDestinationId == content::destination::jupiter &&
            restored.run.routeTransit.targetDestinationId == content::destination::mars,
        "saved recovery routes must restore their exact origin and target");

    LaunchOutcome recovered;
    recovered.type = LaunchResultType::MissionComplete;
    recovered.recoveryMethod = RecoveryMethod::TransferArrival;
    recovered.frontierTransfer = true;
    recovered.destinationId = content::destination::mars;
    recovered.routeTransit = restored.run.routeTransit;
    applyLaunchOutcome(restored, catalog, recovered);
    require(restored.run.destinationIndex == 3 &&
            restored.run.routeTransit.intent == RouteTransitIntent::Reapproach &&
            restored.run.routeTransit.originDestinationId == content::destination::mars &&
            restored.run.routeTransit.targetDestinationId == content::destination::jupiter,
        "successful recovery must retain Jupiter campaign frontier and queue an explicit Mars-to-Jupiter reapproach");
}


void arrivalFlybyMinigameRewardsProgressionAndSlingshot()
{
    const ContentCatalog catalog = createDefaultContent();
    LaunchOutcome moonArrival;
    moonArrival.type = LaunchResultType::MissionComplete;
    moonArrival.frontierTransfer = true;
    moonArrival.destinationId = content::destination::moon;

    const auto startArrivalFlybyForTest = [&](GameState& flybyState, const LaunchOutcome& arrival) {
        introduceArrivalFlybyForTest(flybyState);
        startArrivalOps(flybyState, arrival);
        startArrivalFlybyRun(flybyState, catalog);
    };

    GameState miss = createNewGame(catalog, 701);
    startArrivalFlybyForTest(miss, moonArrival);
    require(miss.screen == Screen::Flyby && miss.run.flyby.active, "starting arrival flyby should open the flyby minigame");
    require(miss.run.flyby.currentZone >= 1, "flyby should begin inside the single-pass corridor");
    require(miss.run.flyby.gravityStrength <= tuning::flyby::gravityEasy + 0.001, "Moon flyby should use easy gravity");
    Random flybyPanelRng(701);
    const PreparedLaunch flybyPanelLaunch = prepareLaunch(miss, catalog, flybyPanelRng);
    const std::string flybyPanelHtml = buildGamePanelHtml({miss, catalog, flybyPanelLaunch, flybyPanelLaunch});
    require(flybyPanelHtml.find("rr-hud-flyby-good") == std::string::npos
            && flybyPanelHtml.find("rr-hud-flyby-perfect") == std::string::npos
            && flybyPanelHtml.find("rr-hud-flyby-slingshot") == std::string::npos,
        "active flyby should keep only timer, speed, zone, and reward visible");
    const int missFlybysBefore = destinationHistoryValue(miss.meta.destinationFlybys, catalog, content::destination::moon);
    const int missBlueprintsBefore = miss.meta.blueprintProgress;
    const double missCreditsBefore = miss.run.credits;
    miss.run.flyby.completed = true;
    miss.run.flyby.result = FlybyGrade::Miss;
    completeFlybyRun(miss, catalog);
    require(miss.screen == Screen::ArrivalOps, "missed flyby should return to approach options");
    require(destinationHistoryValue(miss.meta.destinationFlybys, catalog, content::destination::moon) == missFlybysBefore, "missed flyby should not unlock Moon flyby history");
    require(miss.meta.blueprintProgress == missBlueprintsBefore, "missed flyby should not grant blueprint progress");
    require(std::abs(miss.run.credits - missCreditsBefore) < 0.001, "missed flyby should not grant credits");
    require(miss.meta.totalFlybyMisses == 1, "missed flyby should be counted for future achievement stats");

    GameState completedPose = createNewGame(catalog, 7011);
    startArrivalFlybyForTest(completedPose, moonArrival);
    completedPose.run.flyby.durationSeconds = 0.001;
    completedPose.run.flyby.gravityStrength = 0.0;
    completedPose.run.flyby.velocityX = 0.24;
    completedPose.run.flyby.velocityY = 0.18;
    updateFlybyRun(completedPose, 0.01);
    require(completedPose.run.flyby.completed &&
            std::hypot(completedPose.run.flyby.velocityX, completedPose.run.flyby.velocityY) > 0.001 &&
            nearlyEqual(
                completedPose.run.flyby.velocityY / completedPose.run.flyby.velocityX,
                0.18 / 0.24,
                0.001),
        "a completed Flyby must retain its final heading for the result stamp and achieved-speed reward");

    GameState good = createNewGame(catalog, 702);
    startArrivalFlybyForTest(good, moonArrival);
    const int goodFlybysBefore = destinationHistoryValue(good.meta.destinationFlybys, catalog, content::destination::moon);
    const int goodBlueprintsBefore = good.meta.blueprintProgress;
    const double goodCreditsBefore = good.run.credits;
    good.run.flyby.completed = true;
    good.run.flyby.result = FlybyGrade::Good;
    good.run.flyby.elapsedSeconds = tuning::flyby::durationSeconds - 1.0;
    completeFlybyRun(good, catalog);
    require(destinationHistoryValue(good.meta.destinationFlybys, catalog, content::destination::moon) == goodFlybysBefore + 1, "good flyby should bank destination flyby history");
    require(good.meta.blueprintProgress == goodBlueprintsBefore + tuning::flyby::goodBlueprintGain, "good flyby should grant blueprint progress");
    require(good.run.credits > goodCreditsBefore, "good flyby should grant credits");
    require(good.run.nextLaunchFuelBoost == 0.0 && good.run.nextLaunchSpeedBoost == 0.0, "good flyby should not grant slingshot boosts");
    require(good.meta.totalFlybyGoods == 1, "good flyby should be counted for future achievement stats");

    GameState perfect = createNewGame(catalog, 703);
    startArrivalFlybyForTest(perfect, moonArrival);
    perfect.run.flyby.completed = true;
    perfect.run.flyby.result = FlybyGrade::Perfect;
    perfect.run.flyby.elapsedSeconds = tuning::flyby::durationSeconds - 1.0;
    Random perfectFlybyPanelRng(703);
    const PreparedLaunch perfectFlybyPanelLaunch = prepareLaunch(perfect, catalog, perfectFlybyPanelRng);
    const std::string perfectFlybyHtml = buildGamePanelHtml({perfect, catalog, perfectFlybyPanelLaunch, perfectFlybyPanelLaunch});
    require(perfectFlybyHtml.find("data-panel-mode=\"mission-stamp\"") != std::string::npos,
        "completed flyby should use the centered RmlUi stamp mode");
    require(perfectFlybyHtml.find("class=\"arrival-fanfare-panel\"") != std::string::npos,
        "completed flyby should render its mission stamp inside RmlUi");
    require(perfectFlybyHtml.find("class=\"arrival-stamp-content\"") != std::string::npos,
        "completed flyby should keep stamp copy in a scrollable lane above the continue action");
    require(perfectFlybyHtml.find("data-flyby-stamp") == std::string::npos,
        "completed flyby should not emit the legacy browser-shell stamp marker");
    require(perfectFlybyHtml.find("data-rr-action=\"flyby_continue\"") != std::string::npos,
        "completed flyby RmlUi stamp should expose its continue action");
    completeFlybyRun(perfect, catalog);
    require(perfect.run.nextLaunchFuelBoost >= tuning::flyby::slingshotFuelBoost - 0.001, "perfect flyby should grant next-launch fuel boost");
    require(perfect.run.nextLaunchSpeedBoost > 0.0,
        "a moving perfect flyby should grant a next-launch speed boost derived from its actual finish velocity");
    require(perfect.meta.totalFlybyPerfects == 1, "perfect flyby should be counted for future achievement stats");
    const double baselineFuelBoost = perfect.run.nextLaunchFuelBoost;
    const double baselineSpeedBoost = perfect.run.nextLaunchSpeedBoost;
    const double baselinePerfectReward = perfect.run.credits;
    Random rng(704);
    const PreparedLaunch launch = prepareLaunch(perfect, catalog, rng);
    require(launch.slingshotFuelSavings >= tuning::flyby::slingshotFuelBoost - 0.001, "prepared launch should include pending slingshot fuel savings");
    require(nearlyEqual(launch.fuelCapacity, launchFuelCapacity(perfect)),
        "a flyby benefit must save powered fuel without changing tank capacity");
    require(nearlyEqual(launch.slingshotSpeedBoost, baselineSpeedBoost),
        "prepared launch should include the exact pending finish-speed bonus");

    GameState fastPerfect = createNewGame(catalog, 711);
    startArrivalFlybyForTest(fastPerfect, moonArrival);
    fastPerfect.run.flyby.completed = true;
    fastPerfect.run.flyby.result = FlybyGrade::Perfect;
    fastPerfect.run.flyby.elapsedSeconds = tuning::flyby::minimumFinishSeconds;
    fastPerfect.run.flyby.velocityX = tuning::flyby::maxSpeed;
    fastPerfect.run.flyby.velocityY = 0.0;
    completeFlybyRun(fastPerfect, catalog);
    require(fastPerfect.run.nextLaunchFuelBoost > baselineFuelBoost, "faster perfect flyby should grant a larger fuel slingshot bonus");
    require(fastPerfect.run.nextLaunchSpeedBoost > baselineSpeedBoost, "faster perfect flyby should grant a larger speed slingshot bonus");
    require(fastPerfect.run.credits > baselinePerfectReward, "faster perfect flyby should also amplify the credit reward");

    SaveData save = captureSaveData(fastPerfect);
    const std::optional<SaveData> restoredSave = deserializeSaveData(serializeSaveData(save));
    require(restoredSave.has_value(), "flyby stat counters should serialize");
    GameState restored = createNewGame(catalog, 717);
    restoreSaveData(restored, catalog, *restoredSave);
    require(restored.meta.totalFlybyPerfects == fastPerfect.meta.totalFlybyPerfects, "perfect flyby totals should survive save roundtrip");

    GameState fastGate = createNewGame(catalog, 712);
    startArrivalFlybyForTest(fastGate, moonArrival);
    const auto cubicPoint = [](double a, double b, double c, double d, double t) {
        const double u = 1.0 - t;
        return u * u * u * a + 3.0 * u * u * t * b + 3.0 * u * t * t * c + t * t * t * d;
    };
    fastGate.run.flyby.gravityStrength = 0.0;
    fastGate.run.flyby.elapsedSeconds = tuning::flyby::minimumFinishSeconds;
    fastGate.run.flyby.shipX = cubicPoint(tuning::flyby::startX, tuning::flyby::control1X, tuning::flyby::control2X, tuning::flyby::endX, 0.96);
    fastGate.run.flyby.shipY = cubicPoint(tuning::flyby::startY, tuning::flyby::control1Y, tuning::flyby::control2Y, tuning::flyby::endY, 0.96);
    const double gateDx = tuning::flyby::endX - fastGate.run.flyby.shipX;
    const double gateDy = tuning::flyby::endY - fastGate.run.flyby.shipY;
    const double gateDistance = std::hypot(gateDx, gateDy);
    fastGate.run.flyby.velocityX = gateDx / gateDistance * tuning::flyby::maxSpeed;
    fastGate.run.flyby.velocityY = gateDy / gateDistance * tuning::flyby::maxSpeed;
    updateFlybyRun(fastGate, tuning::launch::maxFrameStepSeconds);
    require(fastGate.run.flyby.completed, "fast flyby crossing the exit gate should complete in the crossing frame");
    require(fastGate.run.flyby.result == FlybyGrade::Perfect, "fast flyby should score the swept exit gate instead of missing after overshooting the sample");
    const double finalShipX = fastGate.run.flyby.shipX;
    const double finalShipY = fastGate.run.flyby.shipY;
    require(std::hypot(fastGate.run.flyby.velocityX, fastGate.run.flyby.velocityY) > 0.001,
        "flyby should retain the shuttle's final heading when the exit gate is reached");
    updateFlybyRun(fastGate, tuning::launch::maxFrameStepSeconds);
    require(nearlyEqual(fastGate.run.flyby.shipX, finalShipX) && nearlyEqual(fastGate.run.flyby.shipY, finalShipY),
        "completed flyby should freeze the shuttle in place after preserving its final heading");

    GameState fastOvershoot = createNewGame(catalog, 713);
    startArrivalFlybyForTest(fastOvershoot, moonArrival);
    fastOvershoot.run.flyby.gravityStrength = 0.0;
    fastOvershoot.run.flyby.elapsedSeconds = tuning::flyby::minimumFinishSeconds;
    fastOvershoot.run.flyby.shipX = cubicPoint(tuning::flyby::startX, tuning::flyby::control1X, tuning::flyby::control2X, tuning::flyby::endX, 0.98);
    fastOvershoot.run.flyby.shipY = cubicPoint(tuning::flyby::startY, tuning::flyby::control1Y, tuning::flyby::control2Y, tuning::flyby::endY, 0.98);
    const double overshootDx = tuning::flyby::endX - fastOvershoot.run.flyby.shipX;
    const double overshootDy = tuning::flyby::endY - fastOvershoot.run.flyby.shipY;
    const double overshootDistance = std::hypot(overshootDx, overshootDy);
    fastOvershoot.run.flyby.velocityX = overshootDx / overshootDistance * tuning::flyby::maxSpeed;
    fastOvershoot.run.flyby.velocityY = overshootDy / overshootDistance * tuning::flyby::maxSpeed;
    updateFlybyRun(fastOvershoot, tuning::launch::maxFrameStepSeconds);
    require(fastOvershoot.run.flyby.completed, "max-speed flyby should complete even when it overshoots beyond the exit gate in one frame");
    require(fastOvershoot.run.flyby.result == FlybyGrade::Perfect, "max-speed flyby should grade the gate crossing, not post-finish overshoot");
    require(fastOvershoot.run.flyby.worstZone >= 2, "post-finish overshoot should not degrade a perfect gate crossing");

    GameState visibleGate = createNewGame(catalog, 715);
    startArrivalFlybyForTest(visibleGate, moonArrival);
    visibleGate.run.flyby.gravityStrength = 0.0;
    visibleGate.run.flyby.elapsedSeconds = tuning::flyby::minimumFinishSeconds;
    const double finishDx = 3.0 * (tuning::flyby::endX - tuning::flyby::control2X);
    const double finishDy = 3.0 * (tuning::flyby::endY - tuning::flyby::control2Y);
    const double finishLength = std::hypot(finishDx, finishDy);
    const double finishTangentX = finishDx / finishLength;
    const double finishTangentY = finishDy / finishLength;
    visibleGate.run.flyby.shipX = tuning::flyby::endX - finishTangentX * (tuning::flyby::shipColliderHalfLength + 0.02);
    visibleGate.run.flyby.shipY = tuning::flyby::endY - finishTangentY * (tuning::flyby::shipColliderHalfLength + 0.02);
    visibleGate.run.flyby.velocityX = finishTangentX * tuning::flyby::maxSpeed;
    visibleGate.run.flyby.velocityY = finishTangentY * tuning::flyby::maxSpeed;
    visibleGate.run.flyby.pathProgress = tuning::flyby::finishProgress - 0.02;
    visibleGate.run.flyby.currentZone = 2;
    visibleGate.run.flyby.worstZone = 2;
    updateFlybyRun(visibleGate, tuning::launch::maxFrameStepSeconds);
    require(visibleGate.run.flyby.completed, "crossing the visible finish line at max speed should finish the flyby");
    require(visibleGate.run.flyby.result == FlybyGrade::Perfect, "crossing the finish line while still perfect should award perfect");

    FlybyRunState strictMiss = fastGate.run.flyby;
    strictMiss.result = FlybyGrade::Active;
    strictMiss.worstZone = 0;
    require(flybyGrade(strictMiss) == FlybyGrade::Miss, "touching the miss zone should make the whole flyby a miss");

    GameState instantMiss = createNewGame(catalog, 716);
    startArrivalFlybyForTest(instantMiss, moonArrival);
    instantMiss.run.flyby.gravityStrength = 0.0;
    instantMiss.run.flyby.shipX = tuning::flyby::startX - tuning::flyby::goodBand * 2.8;
    instantMiss.run.flyby.shipY = tuning::flyby::startY + tuning::flyby::goodBand * 3.0;
    instantMiss.run.flyby.velocityX = tuning::flyby::startVelocityX;
    instantMiss.run.flyby.velocityY = tuning::flyby::startVelocityY;
    updateFlybyRun(instantMiss, 0.05);
    require(instantMiss.run.flyby.completed, "leaving the good corridor should end the flyby immediately");
    require(instantMiss.run.flyby.result == FlybyGrade::Miss, "leaving the good corridor should immediately grade as a miss");

    FlybyRunState strictGood = fastGate.run.flyby;
    strictGood.result = FlybyGrade::Active;
    strictGood.worstZone = 1;
    require(flybyGrade(strictGood) == FlybyGrade::Good, "leaving perfect but staying inside the good corridor should grade as good");

    FlybyRunState unfinished = fastGate.run.flyby;
    unfinished.result = FlybyGrade::Active;
    unfinished.pathProgress = tuning::flyby::finishProgress - 0.01;
    unfinished.worstZone = 2;
    require(flybyGrade(unfinished) == FlybyGrade::Miss, "a flyby should not score until the finish line is reached");

    LaunchOutcome outerArrival;
    outerArrival.type = LaunchResultType::MissionComplete;
    outerArrival.frontierTransfer = true;
    outerArrival.destinationId = content::destination::jupiter;
    GameState outer = createNewGame(catalog, 707);
    startArrivalFlybyForTest(outer, outerArrival);
    require(outer.run.flyby.gravityStrength > miss.run.flyby.gravityStrength, "large-planet flyby should apply stronger gravity than Moon/Mars");
    const double initialProgress = outer.run.flyby.pathProgress;
    updateFlybyRun(outer, 0.5);
    require(outer.run.flyby.pathProgress >= initialProgress, "single-pass flyby should advance along the corridor over time");

    GameState longTrail = createNewGame(catalog, 718);
    startArrivalFlybyForTest(longTrail, moonArrival);
    longTrail.run.flyby.gravityStrength = 0.0;
    longTrail.run.flyby.trailPoints.clear();
    for (int i = 0; i < 120; ++i) {
        longTrail.run.flyby.trailPoints.push_back({-1.2 + static_cast<double>(i) * 0.001, -0.9});
    }
    const FlybyTrailPoint oldestTrailPoint = longTrail.run.flyby.trailPoints.front();
    const std::size_t previousTrailSize = longTrail.run.flyby.trailPoints.size();
    updateFlybyRun(longTrail, 0.05);
    require(longTrail.run.flyby.trailPoints.size() > previousTrailSize, "flyby trail should keep growing instead of trimming older path points");
    require(longTrail.run.flyby.trailPoints.front().x == oldestTrailPoint.x && longTrail.run.flyby.trailPoints.front().y == oldestTrailPoint.y,
        "flyby trail should preserve the oldest path point through a long run");

    GameState impact = createNewGame(catalog, 708);
    startArrivalFlybyForTest(impact, moonArrival);
    impact.run.shipDamage = 7;
    impact.run.flyby.shipX = tuning::flyby::destinationX;
    impact.run.flyby.shipY = tuning::flyby::destinationY;
    updateFlybyRun(impact, 0.1);
    require(impact.run.flyby.completed && impact.run.flyby.collidedWithBody, "flyby should end immediately when the ship intersects the destination body");
    require(impact.run.flyby.result == FlybyGrade::Miss, "planet impact should grade as a missed flyby");
    require(impact.run.shipDamage == 7 + impact.run.flyby.impactHullDamage, "planet impact should add assisted hull damage to the ship");

    GameState outOfBounds = createNewGame(catalog, 714);
    startArrivalFlybyForTest(outOfBounds, moonArrival);
    outOfBounds.run.flyby.shipX = 1.05;
    outOfBounds.run.flyby.velocityX = tuning::flyby::maxSpeed;
    updateFlybyRun(outOfBounds, 0.2);
    require(outOfBounds.run.flyby.completed, "leaving the flyby playfield should end the run");
    require(outOfBounds.run.flyby.result == FlybyGrade::Miss, "leaving the flyby playfield should be a missed flyby");

    GameState controls = createNewGame(catalog, 709);
    startArrivalFlybyForTest(controls, moonArrival);
    Random controlsPanelRng(709);
    const PreparedLaunch controlsPanelLaunch = prepareLaunch(controls, catalog, controlsPanelRng);
    const std::string controlsPanelHtml = buildGamePanelHtml({controls, catalog, controlsPanelLaunch, controlsPanelLaunch});
    require(controlsPanelHtml.find("data-flyby-completed=\"0\"") != std::string::npos
            && controlsPanelHtml.find("cockpit-hud flight-hud") == std::string::npos,
        "active flyby should reserve the left panel for telemetry and expose controls through the bottom input helper");
    controls.run.flyby.gravityStrength = 0.0;
    setFlybyMove(controls, 0.0, 1.0);
    updateFlybyRun(controls, tuning::launch::maxFrameStepSeconds);
    require(controls.run.flyby.selectedThrottle > 0.0,
        "holding Flyby throttle should progressively raise the retained burn level");
    setFlybyMove(controls, 0.0, 0.0);
    const double heldThrottle = controls.run.flyby.selectedThrottle;
    updateFlybyRun(controls, tuning::launch::maxFrameStepSeconds);
    require(nearlyEqual(controls.run.flyby.selectedThrottle, heldThrottle),
        "releasing Flyby throttle input should retain the selected burn level");
    setFlybyMove(controls, 0.0, -1.0);
    for (int step = 0; step < 20; ++step) {
        updateFlybyRun(controls, tuning::launch::maxFrameStepSeconds);
    }
    require(controls.run.flyby.selectedThrottle < heldThrottle,
        "holding Flyby decrease throttle should progressively lower the retained burn level");

    GameState turn = createNewGame(catalog, 710);
    startArrivalFlybyForTest(turn, moonArrival);
    turn.run.flyby.gravityStrength = 0.0;
    const double headingBefore = std::atan2(turn.run.flyby.velocityY, turn.run.flyby.velocityX);
    setFlybyMove(turn, 1.0, 0.0);
    updateFlybyRun(turn, 0.2);
    const double headingAfter = std::atan2(turn.run.flyby.velocityY, turn.run.flyby.velocityX);
    require(headingAfter > headingBefore, "positive flyby turn input should rotate counter-clockwise");
}

void shipUpgradesAssistFlybyAndOrbitMinigames()
{
    const ContentCatalog catalog = createDefaultContent();
    LaunchOutcome marsArrival;
    marsArrival.type = LaunchResultType::MissionComplete;
    marsArrival.destinationId = content::destination::mars;
    marsArrival.frontierTransfer = true;

    GameState baseline = createNewGame(catalog, 718);
    baseline.run.equippedModuleIds = {
        content::module::sparrowEngine,
        content::module::stableTank,
        content::module::patchworkHull,
        content::module::radiatorVanes,
        content::module::analogTelemetry,
        content::module::springCapsule
    };
    introduceArrivalFlybyForTest(baseline);
    startArrivalOps(baseline, marsArrival);
    startArrivalFlybyRun(baseline, catalog);
    startArrivalOrbitRun(baseline, catalog);
    const FlybyRunState baselineFlyby = baseline.run.flyby;
    const OrbitRunState baselineOrbit = baseline.run.orbit;

    GameState assisted = createNewGame(catalog, 719);
    assisted.run.equippedModuleIds = {
        content::module::sparrowEngine,
        content::module::stableTank,
        content::module::patchworkHull,
        content::module::radiatorVanes,
        content::module::analogTelemetry,
        content::module::springCapsule,
        content::module::kestrelEngine,
        content::module::deepReservoir,
        content::module::titaniumRib,
        content::module::ablativeSkin,
        content::module::predictiveGuidance,
        content::module::abortTower
    };
    introduceArrivalFlybyForTest(assisted);
    startArrivalOps(assisted, marsArrival);
    startArrivalFlybyRun(assisted, catalog);
    startArrivalOrbitRun(assisted, catalog);

    require(assisted.run.flyby.goodBand > baselineFlyby.goodBand, "sensor upgrades should widen the flyby good corridor");
    require(assisted.run.flyby.perfectBand > baselineFlyby.perfectBand, "sensor upgrades should widen the flyby perfect corridor");
    require(assisted.run.flyby.turnRateRadians > baselineFlyby.turnRateRadians, "thrust and escape upgrades should improve flyby steering response");
    require(assisted.run.flyby.impactHullDamage < baselineFlyby.impactHullDamage, "hull, cooling, and escape upgrades should reduce flyby impact damage");
    require(nearlyEqual(assisted.run.orbit.goodBand, baselineOrbit.goodBand), "legacy module loadouts should not secretly change the orbital research band");
    require(nearlyEqual(assisted.run.orbit.perfectBand, baselineOrbit.perfectBand), "legacy module loadouts should not secretly change the perfect orbit band");
    require(nearlyEqual(assisted.run.orbit.thrustAcceleration, baselineOrbit.thrustAcceleration), "legacy module loadouts should not secretly change orbit trim authority");
    require(nearlyEqual(assisted.run.orbit.durationSeconds, baselineOrbit.durationSeconds), "legacy module loadouts should not secretly change the orbit timer");
}

GameState startOrbitForTest(const ContentCatalog& catalog, std::string_view destinationId, std::uint64_t seed)
{
    GameState state = createNewGame(catalog, seed);
    LaunchOutcome arrival;
    arrival.type = LaunchResultType::MissionComplete;
    arrival.frontierTransfer = true;
    arrival.destinationId = std::string(destinationId);
    startArrivalOps(state, arrival);
    completeArrivalFlyby(state, catalog);
    startArrivalOps(state, arrival);
    startArrivalOrbitRun(state, catalog);
    require(state.screen == Screen::Orbit && state.run.orbit.active, "orbit fixture should start an active run");
    return state;
}

void orbitControlsFollowClockwiseProgradeDirection()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = startOrbitForTest(catalog, content::destination::moon, 7191);
    OrbitRunState& orbit = state.run.orbit;
    orbit.gravityStrength = 0.0;
    const double distance = std::hypot(orbit.shipX, orbit.shipY);
    const double radialX = orbit.shipX / distance;
    const double radialY = orbit.shipY / distance;
    setOrbitMove(state, 0.0, 1.0);
    updateOrbitRun(state, 0.08);
    require(orbit.selectedThrottle > 0.0,
        "positive tangential Orbit input should progressively raise the retained throttle");
    const double heldThrottle = orbit.selectedThrottle;
    setOrbitMove(state, 0.0, 0.0);
    updateOrbitRun(state, 0.08);
    require(nearlyEqual(orbit.selectedThrottle, heldThrottle),
        "releasing Orbit throttle input should retain the selected burn level");

    setOrbitMove(state, 0.0, -1.0);
    updateOrbitRun(state, 0.08);
    require(orbit.selectedThrottle < heldThrottle,
        "negative tangential Orbit input should progressively lower the retained throttle");
}

void orbitStartsCircularAndIsSolvableAcrossDestinationTiers()
{
    const ContentCatalog catalog = createDefaultContent();
    const std::array<std::string_view, 5> destinations {{
        content::destination::moon,
        content::destination::mars,
        content::destination::jupiter,
        content::destination::saturn,
        content::destination::neptune
    }};

    for (std::size_t index = 0; index < destinations.size(); ++index) {
        GameState state = startOrbitForTest(catalog, destinations[index], 7200 + index);
        const OrbitRunState& started = state.run.orbit;
        const double insertionRadius = std::hypot(started.shipX, started.shipY);
        const double expectedSpeed = std::sqrt(
            started.gravityStrength * insertionRadius /
            (insertionRadius * insertionRadius + tuning::orbit::gravitySoftening));
        require(
            std::abs(std::hypot(started.velocityX, started.velocityY) - expectedSpeed) < 0.000001,
            "each destination should begin at its circular-orbit speed");

        for (int frame = 0; frame < 1200 && !state.run.orbit.completed; ++frame) {
            updateOrbitRun(state, 1.0 / 60.0);
        }
        require(state.run.orbit.completed, "a baseline orbit should complete within its visible timer");
        require(
            state.run.orbit.result == OrbitGrade::Good,
            "a baseline no-input orbit should earn Good outside the Perfect band at "
                + std::string(destinations[index]) + " (grade " + std::to_string(static_cast<int>(state.run.orbit.result))
                + ", perfect seconds " + display::fixed(state.run.orbit.perfectSeconds, 2)
                + ", good seconds " + display::fixed(state.run.orbit.goodSeconds, 2)
                + ", miss seconds " + display::fixed(state.run.orbit.missSeconds, 2) + ")");

        GameState controlled = startOrbitForTest(catalog, destinations[index], 7300 + index);
        for (int frame = 0; frame < 1200 && !controlled.run.orbit.completed; ++frame) {
            const OrbitRunState& liveOrbit = controlled.run.orbit;
            const double radialError = std::hypot(liveOrbit.shipX, liveOrbit.shipY) - liveOrbit.targetRadius;
            const double trimThreshold = liveOrbit.perfectBand * 0.18;
            setOrbitMove(
                controlled,
                radialError > trimThreshold ? -1.0 : (radialError < -trimThreshold ? 1.0 : 0.0),
                0.0);
            updateOrbitRun(controlled, 1.0 / 60.0);
        }
        require(controlled.run.orbit.completed && controlled.run.orbit.result == OrbitGrade::Perfect,
            "controlled orbit inputs should retain or return to Perfect from the authored insertion at "
                + std::string(destinations[index]));

        OrbitRunState trimmed = state.run.orbit;
        trimmed.orbitProgress = 1.0;
        trimmed.currentZone = 2;
        trimmed.perfectSeconds = 0.0;
        trimmed.missSeconds = 0.0;
        trimmed.goodSeconds = std::max(trimmed.goodSeconds, 1.0);
        require(orbitGrade(trimmed) == OrbitGrade::Perfect,
            "a clean loop that finishes in Perfect should promote a controlled trim above baseline Good");
        trimmed.missSeconds = 0.02;
        require(orbitGrade(trimmed) != OrbitGrade::Perfect,
            "entering red must prevent a final Perfect orbit even when the ship recovers the inner band");
    }
}

void launchUpgradeRanksProvideExplicitOrbitAssists()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState baseline = startOrbitForTest(catalog, content::destination::moon, 7211);
    GameState upgraded = createNewGame(catalog, 7212);
    upgraded.meta.launchUpgrades.fuelTanks = 2;
    upgraded.meta.launchUpgrades.flightControls = 2;
    upgraded.meta.launchUpgrades.cooling = 1;
    upgraded.meta.launchUpgrades.hull = 3;
    LaunchOutcome arrival;
    arrival.type = LaunchResultType::MissionComplete;
    arrival.frontierTransfer = true;
    arrival.destinationId = content::destination::moon;
    startArrivalOps(upgraded, arrival);
    completeArrivalFlyby(upgraded, catalog);
    startArrivalOps(upgraded, arrival);
    startArrivalOrbitRun(upgraded, catalog);

    require(
        nearlyEqual(
            upgraded.run.orbit.durationSeconds - baseline.run.orbit.durationSeconds,
            2.0 * tuning::orbit::fuelDurationAssistPerRank),
        "Fuel Tanks should add the documented orbit insertion time");
    require(
        nearlyEqual(
            upgraded.run.orbit.thrustAcceleration / baseline.run.orbit.thrustAcceleration,
            1.0 + 2.0 * tuning::orbit::flightControlsThrustAssistPerRank +
                tuning::orbit::coolingThrustAssistPerRank),
        "Flight Controls and Engine Cooling should add the documented orbit trim authority");
    require(
        upgraded.run.orbit.collisionPadding < baseline.run.orbit.collisionPadding,
        "Hull Plating should reduce the low-orbit collision clearance");
}

void activeFlybySaveResumesAtApproach()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 705);
    LaunchOutcome moonArrival;
    moonArrival.type = LaunchResultType::MissionComplete;
    moonArrival.frontierTransfer = true;
    moonArrival.destinationId = content::destination::moon;
    introduceArrivalFlybyForTest(state);
    startArrivalOps(state, moonArrival);
    startArrivalFlybyRun(state, catalog);

    const SaveData save = captureSaveData(state);
    require(save.screen == Screen::ArrivalOps, "saving during flyby should persist the safe approach screen");
    GameState restored = createNewGame(catalog, 706);
    restoreSaveData(restored, catalog, save);
    require(restored.screen == Screen::ArrivalOps, "loading during flyby should resume at approach options");
    require(!restored.run.flyby.active, "loading during flyby should not restore transient flyby state");
    require(restored.run.arrivalOps.active && restored.run.arrivalOps.destinationId == content::destination::moon, "approach destination should survive flyby save/load");
}

void arrivalOrbitMinigameRewardsProgressionOnlyResearch()
{
    const ContentCatalog catalog = createDefaultContent();
    LaunchOutcome moonArrival;
    moonArrival.type = LaunchResultType::MissionComplete;
    moonArrival.frontierTransfer = true;
    moonArrival.destinationId = content::destination::moon;

    GameState miss = createNewGame(catalog, 721);
    startArrivalOps(miss, moonArrival);
    completeArrivalFlyby(miss, catalog);
    startArrivalOps(miss, moonArrival);
    startArrivalOrbitRun(miss, catalog);
    require(miss.screen == Screen::Orbit && miss.run.orbit.active, "starting arrival orbit should open the orbit minigame");
    Random missPanelRng(721);
    const PreparedLaunch missPanelLaunch = prepareLaunch(miss, catalog, missPanelRng);
    const std::string missPanelHtml = buildGamePanelHtml({miss, catalog, missPanelLaunch, missPanelLaunch});
    require(missPanelHtml.find("data-orbit-completed=\"0\"") != std::string::npos
            && missPanelHtml.find("orbit-control-panel") == std::string::npos,
        "active orbit should reserve the left panel for telemetry and expose controls through the bottom input helper");
    require(missPanelHtml.find("rr-hud-orbit-good") == std::string::npos
            && missPanelHtml.find("rr-hud-orbit-perfect") == std::string::npos,
        "active orbit should keep only timer, zone, loop progress, and reward visible");
    require(miss.run.orbit.currentZone >= 1, "orbit should begin inside the scalable orbital band");
    const double expectedOrbitEntryAngle = tuning::orbit::flybyExitAngleRadians();
    require(nearlyEqual(miss.run.orbit.angleRadians, expectedOrbitEntryAngle) &&
            nearlyEqual(std::atan2(miss.run.orbit.shipY, miss.run.orbit.shipX), expectedOrbitEntryAngle) &&
            std::hypot(miss.run.orbit.shipX, miss.run.orbit.shipY) > miss.run.orbit.targetRadius + miss.run.orbit.perfectBand,
        "orbit insertion should begin at Flyby's endpoint angle inside Good and outside Perfect");
    const double initialAngularMomentum = miss.run.orbit.shipX * miss.run.orbit.velocityY
        - miss.run.orbit.shipY * miss.run.orbit.velocityX;
    require(initialAngularMomentum < 0.0,
        "orbit insertion should continue Flyby's clockwise screen-space approach");
    require(nearlyEqual(miss.run.orbit.durationSeconds, tuning::orbit::durationSeconds), "a baseline orbit should start from the tuned insertion timer");
    const int missOrbitsBefore = destinationHistoryValue(miss.meta.destinationOrbits, catalog, content::destination::moon);
    const int missBlueprintsBefore = miss.meta.blueprintProgress;
    const double missCreditsBefore = miss.run.credits;
    miss.run.orbit.completed = true;
    miss.run.orbit.result = OrbitGrade::Miss;
    completeOrbitRun(miss, catalog);
    require(miss.screen == Screen::ArrivalOps, "missed orbit should return to approach options");
    require(destinationHistoryValue(miss.meta.destinationOrbits, catalog, content::destination::moon) == missOrbitsBefore, "missed orbit should not bank orbit history");
    require(miss.meta.blueprintProgress == missBlueprintsBefore, "missed orbit should not grant science");
    require(std::abs(miss.run.credits - missCreditsBefore) < 0.001, "missed orbit should not grant credits");

    GameState good = createNewGame(catalog, 722);
    startArrivalOps(good, moonArrival);
    completeArrivalFlyby(good, catalog);
    startArrivalOps(good, moonArrival);
    startArrivalOrbitRun(good, catalog);
    const int goodOrbitsBefore = destinationHistoryValue(good.meta.destinationOrbits, catalog, content::destination::moon);
    const int goodBlueprintsBefore = good.meta.blueprintProgress;
    const double goodCreditsBefore = good.run.credits;
    good.run.orbit.completed = true;
    good.run.orbit.result = OrbitGrade::Good;
    completeOrbitRun(good, catalog);
    require(destinationHistoryValue(good.meta.destinationOrbits, catalog, content::destination::moon) == goodOrbitsBefore + 1, "good orbit should bank destination orbit history");
    require(good.meta.blueprintProgress == goodBlueprintsBefore + tuning::orbit::goodBlueprintGain, "good orbit should grant science");
    require(good.run.credits > goodCreditsBefore, "good orbit should grant credits");
    require(good.run.nextLaunchFuelBoost == 0.0 && good.run.nextLaunchSpeedBoost == 0.0, "good orbit should not grant launch boosts");

    GameState perfect = createNewGame(catalog, 723);
    startArrivalOps(perfect, moonArrival);
    completeArrivalFlyby(perfect, catalog);
    startArrivalOps(perfect, moonArrival);
    startArrivalOrbitRun(perfect, catalog);
    const int perfectBlueprintsBefore = perfect.meta.blueprintProgress;
    const double perfectCreditsBefore = perfect.run.credits;
    perfect.run.orbit.completed = true;
    perfect.run.orbit.result = OrbitGrade::Perfect;
    Random perfectRng(723);
    const PreparedLaunch perfectLaunch = prepareLaunch(perfect, catalog, perfectRng);
    const std::string perfectOrbitHtml = buildGamePanelHtml({perfect, catalog, perfectLaunch, perfectLaunch});
    require(perfectOrbitHtml.find("data-panel-mode=\"mission-stamp\"") != std::string::npos,
        "completed orbit should use the centered RmlUi stamp mode");
    require(perfectOrbitHtml.find("class=\"arrival-fanfare-panel\"") != std::string::npos,
        "completed orbit should render its mission stamp inside RmlUi");
    require(perfectOrbitHtml.find("data-orbit-stamp") == std::string::npos,
        "completed orbit should not emit the legacy browser-shell stamp marker");
    require(perfectOrbitHtml.find("data-rr-action=\"orbit_continue\"") != std::string::npos,
        "completed orbit RmlUi stamp should expose its continue action");
    completeOrbitRun(perfect, catalog);
    require(perfect.meta.blueprintProgress == perfectBlueprintsBefore + tuning::orbit::perfectBlueprintGain, "perfect orbit should grant stronger science");
    require(perfect.run.credits > perfectCreditsBefore, "perfect orbit should grant credits");
    require(perfect.run.nextLaunchFuelBoost == 0.0 && perfect.run.nextLaunchSpeedBoost == 0.0, "perfect orbit should still avoid launch boosts");
}

void activeOrbitSaveResumesAtApproach()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 724);
    LaunchOutcome moonArrival;
    moonArrival.type = LaunchResultType::MissionComplete;
    moonArrival.frontierTransfer = true;
    moonArrival.destinationId = content::destination::moon;
    startArrivalOps(state, moonArrival);
    completeArrivalFlyby(state, catalog);
    startArrivalOps(state, moonArrival);
    startArrivalOrbitRun(state, catalog);

    const SaveData save = captureSaveData(state);
    require(save.screen == Screen::ArrivalOps, "saving during orbit should persist the safe approach screen");
    GameState restored = createNewGame(catalog, 725);
    restoreSaveData(restored, catalog, save);
    require(restored.screen == Screen::ArrivalOps, "loading during orbit should resume at approach options");
    require(!restored.run.orbit.active, "loading during orbit should not restore transient orbit state");
    require(restored.run.arrivalOps.active && restored.run.arrivalOps.destinationId == content::destination::moon, "approach destination should survive orbit save/load");
    require(restored.run.arrivalOps.commitment == ApproachCommitment::Uncommitted,
        "mid-orbit saves should normalize to an uncommitted approach");

    require(captureArrivalOrbit(restored), "completed Orbit setup should persist a captured commitment");
    const auto capturedSave = deserializeSaveData(serializeSaveData(captureSaveData(restored)));
    require(capturedSave.has_value(), "captured Orbit save should parse");
    GameState capturedRestored = createNewGame(catalog, 726);
    restoreSaveData(capturedRestored, catalog, *capturedSave);
    require(capturedRestored.run.arrivalOps.commitment == ApproachCommitment::OrbitCaptured,
        "captured Orbit should survive a v14 save round trip");
    require(!canRunArrivalFlyby(capturedRestored, catalog) && canAttemptArrivalLanding(capturedRestored, catalog),
        "restored captured Orbit should keep mutually exclusive availability");

    std::string legacyV13 = serializeSaveData(captureSaveData(restored));
    const std::string commitmentField = std::string(save_schema::field::arrivalApproachCommitment) + "=";
    const std::size_t commitmentStart = legacyV13.find(commitmentField);
    require(commitmentStart != std::string::npos, "v14 save should write the optional approach commitment field");
    const std::size_t commitmentEnd = legacyV13.find('\n', commitmentStart);
    legacyV13.erase(commitmentStart, commitmentEnd == std::string::npos
        ? std::string::npos
        : commitmentEnd - commitmentStart + 1);
    const auto olderV13 = deserializeSaveData(legacyV13);
    require(olderV13.has_value() && olderV13->arrivalOps.commitment == ApproachCommitment::Uncommitted,
        "current saves without the optional field should default safely to Uncommitted");
}

void researchProjectsGenerateAndCompleteFromSharedRules()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 606);
    state.run.destinationIndex = 2;
    state.meta.materials = {.common = 4, .rare = 2};
    Random rng(606);

    generateResearchProjects(state, catalog, rng);
    const auto firstProject = std::find_if(state.run.researchProjectIds.begin(), state.run.researchProjectIds.end(), [](const std::string& id) {
        return !id.empty();
    });
    require(firstProject != state.run.researchProjectIds.end(), "Mars research should generate at least one available project");

    const auto index = static_cast<int>(std::distance(state.run.researchProjectIds.begin(), firstProject));
    const ResearchProject* project = catalog.findResearchProject(*firstProject);
    require(project != nullptr, "generated research project id should resolve");
    const int blueprintsBefore = state.meta.blueprintProgress;
    const int expectedBlueprintGain = researchBlueprintGain(state.meta, *project);
    const MaterialInventory materialsBefore = state.meta.materials;

    const ResearchOutcome outcome = completeResearchProject(state, catalog, index);
    require(outcome.completed, "affordable research project should complete");
    require(outcome.projectId == project->id, "research outcome should identify the project");
    require(outcome.blueprintGain == expectedBlueprintGain, "research outcome should report effective blueprint progress");
    require(state.meta.blueprintProgress == blueprintsBefore + expectedBlueprintGain, "research should grant effective blueprint progress");
    require(state.meta.materials.common == materialsBefore.common - project->materialCost.common, "research should spend common material cost");
    require(state.meta.materials.rare == materialsBefore.rare - project->materialCost.rare, "research should spend rare material cost");
    require(state.run.researchProjectIds[static_cast<std::size_t>(index)].empty(), "completed research slot should be consumed");
}

void materialResearchUnlocksModuleFamilies()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 616);
    state.run.destinationIndex = 2;
    state.meta.materials = {.common = 1, .rare = 1};
    state.run.researchProjectIds = {content::research::prototypeSchematic, "", ""};

    require(!hasUnlock(state.meta, content::unlock::thermal), "test starts before thermal research unlock");
    const ResearchOutcome outcome = completeResearchProject(state, catalog, 0);
    require(outcome.completed, "material-funded prototype research should complete");
    require(outcome.rewardUnlockKey == content::unlock::thermal, "prototype research should report its reward unlock");
    require(outcome.unlockedReward, "first material research completion should report a new unlock");
    require(hasUnlock(state.meta, content::unlock::thermal), "material-funded research should unlock the module family");
    require(catalog.findModule(content::module::slushTank) != nullptr, "test needs thermal module content");
    require(isModuleUnlocked(state.meta, *catalog.findModule(content::module::slushTank)), "new research unlock should affect module availability");

    state.meta.materials = {.common = 1, .rare = 1};
    state.run.researchProjectIds = {content::research::prototypeSchematic, "", ""};
    const ResearchOutcome repeated = completeResearchProject(state, catalog, 0);
    require(repeated.completed, "repeating an already-unlocked project should still complete if affordable");
    require(!repeated.unlockedReward, "repeating an already-unlocked project should not report a fresh unlock");
}

void artifactInsightImprovesFutureResearch()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 627);
    state.run.destinationIndex = 2;
    state.meta.materials = {.common = 4};
    state.meta.artifacts = {
        {"mars_artifact_1", content::destination::mars, true},
        {"mars_artifact_2", content::destination::mars, true},
        {"mars_artifact_3", content::destination::mars, false}
    };
    state.run.researchProjectIds = {content::research::appliedMaterialsLab, "", ""};

    const ResearchProject* project = catalog.findResearchProject(content::research::appliedMaterialsLab);
    require(project != nullptr, "artifact insight test needs materials research content");
    require(identifiedArtifactCount(state.meta) == 2, "artifact insight should count only decoded artifacts");
    require(artifactInsightBlueprintBonus(state.meta) == 2, "decoded artifacts should add blueprint insight");

    const ResearchOutcome outcome = completeResearchProject(state, catalog, 0);
    require(outcome.completed, "research should complete with artifact insight active");
    require(outcome.blueprintGain == project->blueprintGain + 2, "artifact insight should improve future research output");
    require(state.meta.blueprintProgress == project->blueprintGain + 2, "artifact insight should be added to meta blueprint progress");

    state.meta.artifacts = {
        {"a", content::destination::mars, true},
        {"b", content::destination::mars, true},
        {"c", content::destination::mars, true},
        {"d", content::destination::mars, true}
    };
    require(artifactInsightBlueprintBonus(state.meta) == tuning::research::artifactInsightBlueprintMaximum, "artifact insight should be capped");
}

void researchFacilitiesImproveFutureResearch()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 628);
    state.run.destinationIndex = 2;
    state.meta.materials = {.common = 3, .rare = 1};
    state.run.researchProjectIds = {content::research::missionAnalysisLab, "", ""};

    const ResearchOutcome labOutcome = completeResearchProject(state, catalog, 0);
    require(labOutcome.completed, "mission analysis lab should complete when funded");
    require(hasUnlock(state.meta, content::unlock::analysisLab), "mission analysis lab research should unlock the research facility");
    require(researchFacilityBlueprintBonus(state.meta) == tuning::research::analysisLabBlueprintBonus, "analysis lab should add future blueprint output");

    const ResearchProject* project = catalog.findResearchProject(content::research::blueprintSurvey);
    require(project != nullptr, "research facility test needs blueprint survey content");
    state.meta.materials = {};
    state.run.researchProjectIds = {content::research::blueprintSurvey, "", ""};
    const ResearchOutcome surveyOutcome = completeResearchProject(state, catalog, 0);
    require(surveyOutcome.completed, "no-cost blueprint survey should complete after lab research");
    require(surveyOutcome.blueprintGain == project->blueprintGain + tuning::research::analysisLabBlueprintBonus, "analysis lab should improve future research blueprint gain");
}

void artifactResearchIdentifiesRecoveredArtifacts()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 626);
    state.run.destinationIndex = 4;
    state.meta.unlockKeys.push_back(content::unlock::ai);
    state.meta.materials = {.rare = 2, .exotic = 1};
    state.meta.artifacts.push_back({"mars_artifact_3", content::destination::mars, false});
    state.run.researchProjectIds = {content::research::artifactDecoding, "", ""};
    const ResearchProject* project = catalog.findResearchProject(content::research::artifactDecoding);
    require(project != nullptr, "artifact research test needs artifact decoding content");

    const ResearchOutcome outcome = completeResearchProject(state, catalog, 0);
    require(outcome.completed, "artifact research should complete when affordable and unlocked");
    require(outcome.blueprintGain == project->blueprintGain, "newly decoded artifacts should improve future research, not the current decoding pass");
    require(outcome.identifiedArtifact, "artifact research should identify a recovered artifact");
    require(outcome.artifactId == "mars_artifact_3", "artifact research should report the identified artifact");
    require(state.meta.artifacts.front().identified, "identified artifact should persist in meta progress");

    state.meta.materials = {.rare = 2, .exotic = 1};
    state.run.researchProjectIds = {content::research::artifactDecoding, "", ""};
    const ResearchOutcome repeated = completeResearchProject(state, catalog, 0);
    require(repeated.completed, "artifact research should still complete when every artifact is already identified");
    require(!repeated.identifiedArtifact, "artifact research should not report a new artifact when none are unidentified");
}


void surfaceToolResearchImprovesExpeditions()
{
    const ContentCatalog catalog = createDefaultContent();

    GameState baseline = createNewGame(catalog, 636);
    baseline.run.destinationIndex = 2;
    startSurfaceExpedition(baseline, catalog);
    const int baselineSupply = baseline.run.surfaceExpedition.supply;
    Random baselineRng(636);
    const SurfaceActionOutcome baselineSurvey = surveySurfaceSite(baseline, baselineRng);
    const SurfaceActionOutcome baselineMine = mineSurfaceDeposit(baseline, baselineRng);

    GameState upgraded = createNewGame(catalog, 637);
    upgraded.run.destinationIndex = 2;
    upgraded.meta.unlockKeys.push_back(content::unlock::surfaceProbes);
    upgraded.meta.unlockKeys.push_back(content::unlock::surfaceDrills);
    upgraded.meta.unlockKeys.push_back(content::unlock::cargoRigs);
    startSurfaceExpedition(upgraded, catalog);
    require(upgraded.run.surfaceExpedition.supply > baselineSupply, "field probes should add surface expedition supply");

    Random upgradedRng(636);
    const SurfaceActionOutcome upgradedSurvey = surveySurfaceSite(upgraded, upgradedRng);
    const SurfaceActionOutcome upgradedMine = mineSurfaceDeposit(upgraded, upgradedRng);
    require(upgradedSurvey.materialDelta.common > baselineSurvey.materialDelta.common, "field probes should improve survey returns");
    require(upgradedMine.materialDelta.common > baselineMine.materialDelta.common, "surface drills should improve mine returns");


}

void animalCrewClassesModifySurfaceExpeditions()
{
    const ContentCatalog catalog = createDefaultContent();

    GameState prairieDog = createNewGame(catalog, 638);
    activateOnlyCrew(prairieDog, content::astronaut::eli);
    const SurfaceCrewEffects prairieDogEffects = surfaceCrewEffects(prairieDog);
    require(prairieDogEffects.surveyCommonBonus > 0, "prairie dog scouts should improve surface surveying");
    require(prairieDogEffects.artifactChanceBonus > 0.0, "prairie dog scouts should improve anomaly reads");

    GameState squirrel = createNewGame(catalog, 639);
    activateOnlyCrew(squirrel, content::astronaut::jo);
    const SurfaceCrewEffects squirrelEffects = surfaceCrewEffects(squirrel);
    require(squirrelEffects.mineRareChanceBonus > 0.0, "squirrel hoarders should improve rare material odds");

    GameState fox = createNewGame(catalog, 640);
    activateOnlyCrew(fox, content::astronaut::nia);
    const SurfaceCrewEffects foxEffects = surfaceCrewEffects(fox);
    require(foxEffects.hazardRelief > 0.0, "fox aces should improve field-action hazard routing");

    GameState capybara = createNewGame(catalog, 641);
    capybara.run.destinationIndex = 2;
    startSurfaceExpedition(capybara, catalog);
    const SurfaceExpeditionPresentation presentation = surfaceExpeditionPresentation(capybara);
    require(!presentation.details.empty(), "surface details should expose active expedition modifiers");
}

void expeditionExperienceQueuesDistinctSelectableOffers()
{
    ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 642);
    state.screen = Screen::SurfaceExpedition;
    const SurfaceUpgradeCardPresentation firstRankCard = runUpgradeOfferCardPresentation(
        state,
        catalog,
        {RunUpgradeKind::Rig, catalog.surfaceUpgrades.front().id, 1, -1},
        0);
    const auto progressionChip = std::find_if(
        firstRankCard.effectChips.begin(),
        firstRankCard.effectChips.end(),
        [](const PanelMetricPresentation& chip) { return chip.value == "None -> Rank I"; });
    require(
        progressionChip != firstRankCard.effectChips.end()
            && progressionChip->label.empty(),
        "the first rig rank should use only the compact rank transition copy");
    const SurfaceUpgradeCardPresentation secondRankCard = runUpgradeOfferCardPresentation(
        state,
        catalog,
        {RunUpgradeKind::Rig, catalog.surfaceUpgrades.front().id, 2, -1},
        0);
    require(
        std::any_of(
            secondRankCard.effectChips.begin(),
            secondRankCard.effectChips.end(),
            [](const PanelMetricPresentation& chip) {
                return chip.label.empty() && chip.value == "Rank I -> Rank II";
            }),
        "later rig ranks should also omit the redundant Progression label");
    const ExpeditionExperienceAward award = awardExpeditionExperience(state, 75.0, state.screen);
    require(award.levelsGained == 3 && state.run.surfaceExpedition.expeditionLevel == 4,
        "75 expedition XP should cross the 10, 16, and 25 thresholds");
    require(std::abs(state.run.surfaceExpedition.expeditionExperience - 24.0) < 0.001 &&
            state.run.surfaceExpedition.pendingRunUpgradeChoices == 3,
        "75 expedition XP should leave 24 XP and queue three mandatory choices");
    Random rng(643);
    require(generateRunUpgradeOffers(state, catalog, rng), "a queued level should generate a persisted offer");
    const SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    require(expedition.runUpgradeOfferPending && expedition.runUpgradeOfferCount == 3,
        "a populated run-upgrade pool should expose three cards");
    for (int left = 0; left < expedition.runUpgradeOfferCount; ++left) {
        for (int right = left + 1; right < expedition.runUpgradeOfferCount; ++right) {
            const RunUpgradeOffer& a = expedition.runUpgradeOffers[static_cast<std::size_t>(left)];
            const RunUpgradeOffer& b = expedition.runUpgradeOffers[static_cast<std::size_t>(right)];
            require(a.kind != b.kind || a.definitionId != b.definitionId || a.slotIndex != b.slotIndex,
                "one level-up board must not repeat an identical offer target");
        }
    }
    const Screen originalScreen = state.screen;
    require(chooseRunUpgrade(state, catalog, 0), "selecting a valid persisted offer should apply it");
    require(state.run.surfaceExpedition.pendingRunUpgradeChoices == 2 &&
            !state.run.surfaceExpedition.runUpgradeOfferPending,
        "selection should consume exactly one choice and clear only the current board");
    require(state.screen == originalScreen,
        "core offer selection must not mutate screens or auto-open the next board");
}

void launchFailureSummariesMatchTheActualLesson()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 6610);
    state.lastOutcome.failureCause = LaunchFailureCause::ThermalRunaway;
    const LaunchOutcomeSummaryPresentation thermal = launchOutcomeSummaryPresentation(state, catalog);
    require(thermal.title == "ENGINES TOASTED" &&
            thermal.consequence.find("cooked themselves") != std::string::npos &&
            thermal.consequence.find("fuel") == std::string::npos,
        "Mars thermal failure should explain overheated engines without blaming fuel");

    state.launchConfig.missionKind = LaunchMissionKind::FlightControlsCalibration;
    state.lastOutcome.failureCause = LaunchFailureCause::TrainingRescue;
    const LaunchOutcomeSummaryPresentation controls = launchOutcomeSummaryPresentation(state, catalog);
    require(controls.title == "OFF COURSE" &&
            controls.consequence.find("careening off into oblivion") != std::string::npos &&
            controls.consequence.find("fuel") == std::string::npos,
        "controls rescue should explain the lost course with the calibration lesson's tone");
}

void sharedFlightInstrumentPresentationMatchesEachMode()
{
    PreparedLaunch launch;
    launch.config.burnGoalMultiplier = 2.0;
    launch.manualControlsEnabled = true;
    launch.heatEnabled = true;
    LaunchFlightState flight;
    flight.active = true;
    flight.currentMultiplier = 1.5;
    flight.heat = 0.72;
    flight.fuelCapacity = 20.0;
    flight.fuelRemaining = 5.0;
    flight.selectedThrottle = 0.65;
    flight.courseOffset = tuning::launch::pilotingCourseCaution;
    const FlightInstrumentPresentation launchInstruments = launchFlightInstruments(launch, flight);
    require(launchInstruments.visible && nearlyEqual(launchInstruments.speed, 0.5)
            && nearlyEqual(launchInstruments.temperature, 0.72)
            && nearlyEqual(launchInstruments.fuel, 0.25)
            && nearlyEqual(launchInstruments.throttle, 0.65)
            && !launchInstruments.temperatureCritical
            && launchInstruments.offCourse && !launchInstruments.courseCritical,
        "Launch instruments should use authoritative speed, heat, fuel, and course state");

    FlybyRunState flyby;
    flyby.active = true;
    flyby.durationSeconds = 20.0;
    flyby.elapsedSeconds = 5.0;
    flyby.velocityX = tuning::flyby::maxSpeed;
    flyby.velocityY = 0.0;
    flyby.selectedThrottle = 1.0;
    flyby.currentZone = 0;
    const FlightInstrumentPresentation flybyInstruments = flybyFlightInstruments(flyby);
    require(flybyInstruments.visible && nearlyEqual(flybyInstruments.speed, 1.0)
            && nearlyEqual(flybyInstruments.fuel, 0.75)
            && nearlyEqual(flybyInstruments.throttle, 1.0)
            && flybyInstruments.temperature > 0.8 && flybyInstruments.temperatureCritical
            && flybyInstruments.offCourse,
        "Flyby instruments should derive display-only heat and fuel from thrust and endurance");

    flyby.inputY = 0.0;
    flyby.selectedThrottle = 0.46;
    const FlightInstrumentPresentation flybyHeldInstruments = flybyFlightInstruments(flyby);
    require(nearlyEqual(flybyHeldInstruments.throttle, 0.46)
            && flybyHeldInstruments.throttleValue == "Throttle 46%",
        "Flyby instruments should display the retained throttle level when no adjustment key is held");

    OrbitRunState orbit;
    orbit.active = true;
    orbit.durationSeconds = 16.0;
    orbit.elapsedSeconds = 8.0;
    orbit.velocityX = tuning::orbit::minSpeed;
    orbit.velocityY = 0.0;
    orbit.inputX = 1.0;
    orbit.selectedThrottle = 0.50;
    orbit.currentZone = 0;
    const FlightInstrumentPresentation orbitInstruments = orbitFlightInstruments(orbit);
    require(orbitInstruments.visible && nearlyEqual(orbitInstruments.speed, 0.0)
            && nearlyEqual(orbitInstruments.fuel, 0.5)
            && nearlyEqual(orbitInstruments.throttle, 0.5)
            && orbitInstruments.temperature > 0.5 && orbitInstruments.offCourse,
        "Orbit instruments should derive display-only heat and fuel without changing mechanics");

    flyby.completed = true;
    orbit.completed = true;
    require(!flybyFlightInstruments(flyby).visible && !orbitFlightInstruments(orbit).visible,
        "completed flight minigames should unmount the instrument cluster");
}

void activeFlightPanelsUseTheClusterAndCompactStatusRows()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState launchState = createNewGame(catalog, 6611);
    launchState.screen = Screen::Launch;
    Random rng(6611);
    PreparedLaunch launch = prepareLaunch(launchState, catalog, rng);
    launch.asteroidsEnabled = true;
    LaunchFlightState flight = beginLaunchFlight(launch, currentDestination(launchState, catalog));
    PanelRenderContext launchContext {launchState, catalog, launch, launch};
    launchContext.currentMultiplier = flight.currentMultiplier;
    launchContext.flightArmed = true;
    launchContext.launchFlight = &flight;
    const PanelDocumentPresentation launchPanel = buildGamePanelPresentation(launchContext);
    require(launchPanel.metadata.overlay == PanelOverlayKind::FlightInstruments
            && launchPanel.contentMarkup.find("rr-hud-launch-metric-") == std::string::npos
            && launchPanel.contentMarkup.find("rr-hud-launch-hull") != std::string::npos,
        "active Launch should move instrumentation into the scene and retain compact hull status");
    require(!launchPanel.runtime.instrumentSpeedValue.empty()
            && !launchPanel.runtime.instrumentTemperatureValue.empty()
            && !launchPanel.runtime.instrumentFuelValue.empty()
            && !launchPanel.runtime.instrumentThrottleValue.empty(),
        "the scene overlay should receive initial deterministic Launch readouts including throttle");
    RealtimeHudState launchHud;
    buildRealtimeHudState(launchContext, launchHud);
    const auto hasLaunchPatch = [&](std::string_view id) {
        return std::any_of(launchHud.patches.begin(), launchHud.patches.end(), [id](const RealtimeHudPatch& patch) {
            return patch.elementId == id;
        });
    };
    require(hasLaunchPatch("rr-flight-speed-value")
            && hasLaunchPatch("rr-flight-temperature-value")
            && hasLaunchPatch("rr-flight-fuel-value")
            && !hasLaunchPatch("rr-flight-throttle-value")
            && hasLaunchPatch("rr-flight-throttle-accessibility")
            && hasLaunchPatch("rr-flight-nav-indicator")
            && hasLaunchPatch("rr-flight-temperature-label")
            && hasLaunchPatch("rr-flight-temperature-readout")
            && hasLaunchPatch("rr-hud-launch-hull"),
        "realtime Launch patches should update every visible readout, the hidden throttle value, navigation lamp, and hull row");

    GameState flybyState = createNewGame(catalog, 6612);
    flybyState.screen = Screen::Flyby;
    flybyState.run.flyby.active = true;
    flybyState.run.flyby.durationSeconds = 18.0;
    const PanelDocumentPresentation flybyPanel = buildGamePanelPresentation(
        {flybyState, catalog, launch, launch});
    require(flybyPanel.metadata.overlay == PanelOverlayKind::FlightInstruments
            && flybyPanel.contentMarkup.find("rr-hud-flyby-speed") == std::string::npos
            && flybyPanel.contentMarkup.find("rr-hud-flyby-timer") != std::string::npos
            && flybyPanel.contentMarkup.find("rr-hud-flyby-grade") != std::string::npos,
        "active Flyby should use the cluster plus compact timer and grade rows");

    GameState orbitState = createNewGame(catalog, 6613);
    orbitState.screen = Screen::Orbit;
    orbitState.run.orbit.active = true;
    orbitState.run.orbit.durationSeconds = 15.0;
    const PanelDocumentPresentation orbitPanel = buildGamePanelPresentation(
        {orbitState, catalog, launch, launch});
    require(orbitPanel.metadata.overlay == PanelOverlayKind::FlightInstruments
            && orbitPanel.contentMarkup.find("flight-readout") == std::string::npos
            && orbitPanel.contentMarkup.find("rr-hud-orbit-timer") != std::string::npos
            && orbitPanel.contentMarkup.find("rr-hud-orbit-zone") != std::string::npos
            && orbitPanel.contentMarkup.find("rr-hud-orbit-loop") != std::string::npos,
        "active Orbit should use the cluster plus compact mission rows");
}

void exhaustedRunUpgradePoolConsumesQueuedChoices()
{
    ContentCatalog catalog = createDefaultContent();
    catalog.surfaceUpgrades.clear();
    catalog.miniDrones.clear();
    catalog.droneModules.clear();
    catalog.droneSynergies.clear();

    GameState state = createNewGame(catalog, 6421);
    state.run.surfaceExpedition.pendingRunUpgradeChoices = 2;
    Random rng(6422);
    require(!generateRunUpgradeOffers(state, catalog, rng) &&
            state.run.surfaceExpedition.pendingRunUpgradeChoices == 1 &&
            !state.run.surfaceExpedition.runUpgradeOfferPending,
        "an exhausted finite pool should consume exactly one mandatory choice without opening an empty board");
    require(!generateRunUpgradeOffers(state, catalog, rng) &&
            state.run.surfaceExpedition.pendingRunUpgradeChoices == 0 &&
            state.run.surfaceExpedition.runUpgradeOfferCount == 0,
        "queued choices should drain deterministically when every eligible upgrade is installed");
}

void droneGraftOffersAreDistinctPerCompatibleSlot()
{
    ContentCatalog catalog = createDefaultContent();
    catalog.surfaceUpgrades.clear();
    catalog.droneSynergies.clear();
    GameState state = createNewGame(catalog, 6431);
    state.screen = Screen::SurfaceUpgrade;
    state.meta.unlockKeys = {content::unlock::droneBay, content::unlock::perimeterDrones};
    state.meta.droneBaySlots = 2;
    state.meta.ownedDroneIds = {content::drone::miningDrone, content::drone::miningDrone};
    state.meta.equippedDroneIds = state.meta.ownedDroneIds;
    state.run.surfaceExpedition.runDroneRanks = {{content::drone::miningDrone, 3}};
    state.run.surfaceExpedition.pendingRunUpgradeChoices = 1;
    Random rng(6432);
    require(generateRunUpgradeOffers(state, catalog, rng), "compatible empty drone slots should create graft candidates");
    const SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    require(expedition.runUpgradeOfferCount == 3,
        "four slot-specific Mining graft candidates should produce a three-card board");
    bool sameGraftDifferentSlots = false;
    for (int left = 0; left < expedition.runUpgradeOfferCount; ++left) {
        const RunUpgradeOffer& a = expedition.runUpgradeOffers[static_cast<std::size_t>(left)];
        require(a.kind == RunUpgradeKind::DroneGraft && (a.slotIndex == 0 || a.slotIndex == 1),
            "the exhausted filtered pool should contain only pre-bound compatible grafts");
        for (int right = left + 1; right < expedition.runUpgradeOfferCount; ++right) {
            const RunUpgradeOffer& b = expedition.runUpgradeOffers[static_cast<std::size_t>(right)];
            sameGraftDifferentSlots = sameGraftDifferentSlots ||
                (a.definitionId == b.definitionId && a.slotIndex != b.slotIndex);
        }
    }
    require(sameGraftDifferentSlots,
        "duplicate Drone types must allow the same graft definition to target separate slots");
    const RunUpgradeOffer chosen = expedition.runUpgradeOffers[0];
    require(chooseRunUpgrade(state, catalog, 0) &&
            state.run.surfaceExpedition.droneModuleAssignments.size() == 1 &&
            state.run.surfaceExpedition.droneModuleAssignments.front().equippedFrame == chosen.slotIndex,
        "choosing a graft should install it directly on its offered slot without assignment UI");
}

void postExtractionLevelUpDraftRestoresWithoutSurfaceRuntime()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 6433);
    SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    expedition.active = false;
    expedition.expeditionLevel = 4;
    expedition.expeditionExperience = 24.0;
    expedition.pendingRunUpgradeChoices = 1;
    expedition.runUpgradeOffers[0] = {
        RunUpgradeKind::Rig,
        content::surfaceUpgrade::thermalDrillJackets,
        1,
        -1};
    expedition.runUpgradeOfferCount = 1;
    expedition.runUpgradeOfferPending = true;
    expedition.runUpgradeReturnScreen = Screen::Hangar;
    state.screen = Screen::SurfaceUpgrade;

    const std::optional<SaveData> save = deserializeSaveData(
        serializeSaveData(captureSaveData(state)));
    require(save.has_value(), "a post-extraction Level Up draft should serialize as a valid v14 save");

    GameState restored = createNewGame(catalog, 1);
    restoreSaveData(restored, catalog, *save);
    require(restored.screen == Screen::SurfaceUpgrade,
        "an open post-extraction Level Up draft should restore even after Surface runtime ends");
    require(!restored.run.surfaceExpedition.active &&
            restored.run.surfaceExpedition.runUpgradeOfferPending &&
            restored.run.surfaceExpedition.runUpgradeOfferCount == 1 &&
            restored.run.surfaceExpedition.runUpgradeReturnScreen == Screen::Hangar,
        "post-extraction draft offers and their eventual return screen should round trip intact");
}

void selectedSurfaceUpgradesModifyMiningAndSurfaceStats()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState baseline = createNewGame(catalog, 644);
    baseline.run.destinationIndex = 2;
    startSurfaceExpedition(baseline, catalog);

    GameState upgraded = baseline;
    upgraded.run.surfaceExpedition.runRigUpgradeRanks = {
        {content::surfaceUpgrade::thermalDrillJackets, 1},
        {content::surfaceUpgrade::widebandPulse, 1},
        {content::surfaceUpgrade::cargoSkids, 1},
        {content::surfaceUpgrade::shockMounts, 1},
        {content::surfaceUpgrade::oreScentArray, 1}
    };

    const MiningDrillStats baselineStats = miningDrillStats(baseline, catalog);
    const MiningDrillStats upgradedStats = miningDrillStats(upgraded, catalog);
    require(upgradedStats.heatRiseScale < baselineStats.heatRiseScale, "thermal field upgrades should reduce mining heat rise");
    require(upgradedStats.scannerRadius > baselineStats.scannerRadius, "scanner field upgrades should widen pulse reveal radius");
    require(upgradedStats.integrityRelief > baselineStats.integrityRelief, "shock mounts should improve mining durability");
    require(upgradedStats.hardRockBounceRelief > baselineStats.hardRockBounceRelief, "shock mounts should reduce hard-rock recoil");
    require(upgradedStats.oreYieldChance > baselineStats.oreYieldChance, "ore field upgrades should improve yield odds");
    require(surfaceToolEffects(upgraded.meta).hazardRelief >= surfaceToolEffects(baseline.meta).hazardRelief, "cargo field upgrades should reduce Push Deeper hazard risk");

    const SurfaceExpeditionPresentation presentation = surfaceExpeditionPresentation(upgraded, catalog);
    require(!presentation.selectedUpgradeNames.empty(), "surface presentation should expose selected field upgrades");
    require(!presentation.details.empty(), "surface details should list field upgrades");
}

void surfaceUpgradesAndDronesModifyScanMiniGame()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState baseline = createNewGame(catalog, 1933);
    baseline.run.destinationIndex = 2;
    startSurfaceExpedition(baseline, catalog);
    baseline.run.surfaceExpedition.hazard = 0.35;

    GameState upgraded = baseline;
    upgraded.run.surfaceExpedition.runRigUpgradeRanks = {
        {content::surfaceUpgrade::widebandPulse, 1},
        {content::surfaceUpgrade::oreScentArray, 1},
        {content::surfaceUpgrade::deepEchoMapper, 1}
    };
    upgraded.meta.unlockKeys.push_back(content::unlock::droneBay);
    upgraded.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
    upgraded.meta.droneBaySlots = 1;
    upgraded.meta.equippedDroneIds = {content::drone::surveyDrone};

    Random baselineRng(1934);
    Random upgradedRng(1934);
    require(startSurfaceScanRun(baseline, baselineRng).applied, "baseline scan should start");
    require(startSurfaceScanRun(upgraded, upgradedRng).applied, "upgraded scan should start");
    require(upgraded.run.surfaceScan.maxPulses == baseline.run.surfaceScan.maxPulses,
        "temporary scanner support must not extend the permanent Survey Array rating");
    require(upgraded.run.surfaceScan.signal > baseline.run.surfaceScan.signal, "scanner support should improve starting scan signal");
    require(upgraded.run.surfaceScan.interference <= baseline.run.surfaceScan.interference, "scanner support should soften starting interference");
    require(upgraded.run.surfaceScan.bustRisk < baseline.run.surfaceScan.bustRisk, "scanner support should reduce scan bust risk");

    baseline.run.surfaceScan.bustRisk = 0.0;
    upgraded.run.surfaceScan.bustRisk = 0.0;
    require(pulseSurfaceScan(baseline, baselineRng).applied, "baseline scan pulse should resolve");
    require(pulseSurfaceScan(upgraded, upgradedRng).applied, "upgraded scan pulse should resolve");
    require(upgraded.run.surfaceScan.signal > baseline.run.surfaceScan.signal, "scanner support should continue improving signal after a pulse");
    require(upgraded.run.surfaceScan.hazardDelta <= baseline.run.surfaceScan.hazardDelta, "hazard-relief scanner upgrades should reduce scan hazard creep");
    require(upgraded.run.surfaceScan.bustRisk < baseline.run.surfaceScan.bustRisk, "scanner support should keep post-pulse bust risk lower");
}

void surfaceUpgradesAndDronesModifyPushDeeperMiniGame()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState baseline = createNewGame(catalog, 1935);
    baseline.run.destinationIndex = 2;
    startSurfaceExpedition(baseline, catalog);
    baseline.run.surfaceExpedition.hazard = 0.35;
    baseline.run.surfaceExpedition.depthProspects.push_back({1, 1});

    GameState upgraded = baseline;
    upgraded.run.surfaceExpedition.runRigUpgradeRanks = {
        {content::surfaceUpgrade::thermalDrillJackets, 1},
        {content::surfaceUpgrade::shockMounts, 1},
        {content::surfaceUpgrade::recoilBraces, 1},
        {content::surfaceUpgrade::oreHopper, 1}
    };
    upgraded.meta.unlockKeys.push_back(content::unlock::droneBay);
    upgraded.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
    upgraded.meta.droneBaySlots = 1;
    upgraded.meta.equippedDroneIds = {content::drone::hazardDrone};

    Random baselineRng(1936);
    Random upgradedRng(1936);
    require(startSurfacePushRun(baseline, baselineRng).applied, "baseline Push Deeper should start");
    require(startSurfacePushRun(upgraded, upgradedRng).applied, "upgraded Push Deeper should start");
    require(upgraded.run.surfacePush.maxSteps == baseline.run.surfacePush.maxSteps,
        "temporary structural support must not extend the permanent Bore System rating");
    require(upgraded.run.surfacePush.pressure <= baseline.run.surfacePush.pressure, "structural support should reduce starting Push Deeper pressure");
    require(upgraded.run.surfacePush.collapseRisk < baseline.run.surfacePush.collapseRisk, "structural support should reduce starting collapse risk");

    baseline.run.surfacePush.collapseRisk = 0.0;
    upgraded.run.surfacePush.collapseRisk = 0.0;
    require(pushSurfaceDepthStep(baseline, baselineRng).applied, "baseline Push Deeper step should resolve");
    require(pushSurfaceDepthStep(upgraded, upgradedRng).applied, "upgraded Push Deeper step should resolve");
    require(upgraded.run.surfacePush.pressure < baseline.run.surfacePush.pressure, "structural support should slow pressure growth");
    require(upgraded.run.surfacePush.hazardDelta <= baseline.run.surfacePush.hazardDelta, "field support should reduce Push Deeper hazard creep");
    require(upgraded.run.surfacePush.collapseRisk < baseline.run.surfacePush.collapseRisk, "structural support should keep post-step collapse risk lower");
}

void surfaceDepthRatingsReplaceTemporaryEnvelopeBonuses()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState baseline = createNewGame(catalog, 1937);
    baseline.run.destinationIndex = 2;
    startSurfaceExpedition(baseline, catalog);
    baseline.run.surfaceExpedition.supply = 10;

    const int supplyBeforeBlockedDig = baseline.run.surfaceExpedition.supply;
    Random blockedRng(1938);
    const SurfaceActionOutcome blocked = startSurfacePushRun(baseline, blockedRng);
    require(!blocked.applied &&
            baseline.run.surfaceExpedition.supply == supplyBeforeBlockedDig &&
            blocked.message.find("Survey level +1") != std::string::npos,
        "Dig must reject an unsurveyed next level before spending action kits");

    baseline.run.surfaceExpedition.depthProspects = {
        {1, 1},
        {2, 2}
    };
    baseline.meta.surfaceDepthUpgrades.surveyArray = 1;
    baseline.meta.surfaceDepthUpgrades.boreSystem = 1;

    auto scanPushLimitPair = [](const GameState& state, int seed) {
        GameState scanState = state;
        GameState pushState = state;
        scanState.run.surfaceExpedition.depthProspects.clear();
        Random scanRng(seed);
        Random pushRng(seed + 1);
        require(startSurfaceScanRun(scanState, scanRng).applied, "scan should start for depth-limit parity");
        require(startSurfacePushRun(pushState, pushRng).applied, "push should start for depth-limit parity");
        return std::pair<int, int> {
            scanState.run.surfaceScan.maxPulses,
            pushState.run.surfacePush.maxSteps
        };
    };

    const auto baselineLimits = scanPushLimitPair(baseline, 1938);
    require(baselineLimits.first == 3 && baselineLimits.second == 2,
        "rank I Survey and Bore systems should expose current through depth +2 and two legal Dig steps");

    GameState scannerUpgraded = baseline;
    scannerUpgraded.run.surfaceExpedition.runRigUpgradeRanks = {
        {content::surfaceUpgrade::widebandPulse, 1},
        {content::surfaceUpgrade::deepEchoMapper, 1}
    };
    scannerUpgraded.meta.unlockKeys.push_back(content::unlock::droneBay);
    scannerUpgraded.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
    scannerUpgraded.meta.droneBaySlots = 1;
    scannerUpgraded.meta.equippedDroneIds = {content::drone::surveyDrone};
    const auto scannerLimits = scanPushLimitPair(scannerUpgraded, 1939);
    require(scannerLimits == baselineLimits,
        "temporary scanner upgrades and Survey Drones must not change hard Survey or Bore limits");

    GameState structureUpgraded = baseline;
    structureUpgraded.run.surfaceExpedition.runRigUpgradeRanks = {
        {content::surfaceUpgrade::thermalDrillJackets, 1},
        {content::surfaceUpgrade::shockMounts, 1},
        {content::surfaceUpgrade::recoilBraces, 1}
    };
    structureUpgraded.meta.unlockKeys.push_back(content::unlock::droneBay);
    structureUpgraded.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
    structureUpgraded.meta.droneBaySlots = 1;
    structureUpgraded.meta.equippedDroneIds = {content::drone::hazardDrone};
    const auto structureLimits = scanPushLimitPair(structureUpgraded, 1940);
    require(structureLimits == baselineLimits,
        "temporary structural upgrades and Hazard Drones must not change hard Survey or Bore limits");
}

void permanentSurfaceDepthRefitsUnlockAndPersist()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 19401);
    state.meta.unlockKeys.push_back(content::unlock::surfaceProbes);
    state.meta.unlockKeys.push_back(content::unlock::surfaceDrills);
    state.run.refitEntitled = true;
    state.run.credits = 200.0;

    const ShipModule* surveyOne = catalog.findModule(content::module::surveyArray1);
    const ShipModule* surveyTwo = catalog.findModule(content::module::surveyArray2);
    const ShipModule* surveyThree = catalog.findModule(content::module::surveyArray3);
    const ShipModule* boreOne = catalog.findModule(content::module::boreSystem1);
    const ShipModule* fuelLoopOne = catalog.findModule(content::module::rigFuelLoop1);
    const ShipModule* fuelLoopTwo = catalog.findModule(content::module::rigFuelLoop2);
    const ShipModule* fuelLoopThree = catalog.findModule(content::module::rigFuelLoop3);
    require(surveyOne != nullptr && surveyTwo != nullptr && surveyThree != nullptr &&
            boreOne != nullptr && fuelLoopOne != nullptr && fuelLoopTwo != nullptr &&
            fuelLoopThree != nullptr,
        "all permanent Survey Array, Bore System, and Rig Fuel Loop ranks must resolve from content");
    require(moduleOfferCost(*surveyOne) == 22 && moduleOfferCost(*surveyTwo) == 34 &&
            moduleOfferCost(*surveyThree) == 62 && moduleOfferCost(*fuelLoopOne) == 22 &&
            moduleOfferCost(*fuelLoopTwo) == 34 && moduleOfferCost(*fuelLoopThree) == 62,
        "all surface progression ranks must use the selected Common, Uncommon, and Rare Refit costs");

    Random rng(19402);
    generateModuleOffers(state, catalog, rng);
    require(state.run.offerModuleIds[0] == content::module::surveyArray1 &&
            state.run.offerModuleIds[1] == content::module::boreSystem1 &&
            state.run.offerModuleIds[2] == content::module::rigFuelLoop1,
        "the next missing rank from all three researched surface chains must be guaranteed in Refit");

    GameState lowCreditState = createNewGame(catalog, 19403);
    lowCreditState.meta.unlockKeys.push_back(content::unlock::surfaceProbes);
    lowCreditState.meta.unlockKeys.push_back(content::unlock::surfaceDrills);
    lowCreditState.meta.surfaceDepthUpgrades = {1, 1};
    lowCreditState.meta.rigFuelLoop.rank = 1;
    lowCreditState.meta.ownedModuleIds.push_back(content::module::surveyArray1);
    lowCreditState.meta.ownedModuleIds.push_back(content::module::boreSystem1);
    lowCreditState.meta.ownedModuleIds.push_back(content::module::rigFuelLoop1);
    lowCreditState.run.refitEntitled = true;
    lowCreditState.run.credits = 22.0;
    generateModuleOffers(lowCreditState, catalog, rng);
    require(lowCreditState.run.offerModuleIds[0] == content::module::surveyArray2 &&
            lowCreditState.run.offerModuleIds[1] == content::module::boreSystem2 &&
            lowCreditState.run.offerModuleIds[2] == content::module::rigFuelLoop2,
        "the affordable-offer fallback must not replace guaranteed surface progression ranks");

    require(buyOffer(state, catalog, 0) &&
            state.meta.surfaceDepthUpgrades.surveyArray == 1 &&
            surfaceDepthRating(state, SurfaceDepthUpgradeKind::SurveyArray) == 2,
        "installing Survey Array I must permanently raise survey depth to +2");
    require(std::find(
                state.run.equippedModuleIds.begin(),
                state.run.equippedModuleIds.end(),
                content::module::surveyArray1) == state.run.equippedModuleIds.end(),
        "permanent surface depth systems must consume no equipment slot");
    require(buyOffer(state, catalog, 1) &&
            state.meta.surfaceDepthUpgrades.boreSystem == 1,
        "the same Refit visit must allow one Bore purchase after a Survey purchase");
    require(buyOffer(state, catalog, 2) &&
            state.meta.rigFuelLoop.rank == 1,
        "the same Refit visit must allow one Rig Fuel Loop purchase after the depth purchases");
    generateModuleOffers(state, catalog, rng);
    require(std::find(state.run.offerModuleIds.begin(), state.run.offerModuleIds.end(), content::module::surveyArray2) == state.run.offerModuleIds.end() &&
            std::find(state.run.offerModuleIds.begin(), state.run.offerModuleIds.end(), content::module::boreSystem2) == state.run.offerModuleIds.end() &&
            std::find(state.run.offerModuleIds.begin(), state.run.offerModuleIds.end(), content::module::rigFuelLoop2) == state.run.offerModuleIds.end(),
        "a Refit reroll must not expose the next rank of a chain already purchased this visit");

    beginRefitVisit(state);
    generateModuleOffers(state, catalog, rng);
    require(state.run.offerModuleIds[0] == content::module::surveyArray2 &&
            state.run.offerModuleIds[1] == content::module::boreSystem2 &&
            state.run.offerModuleIds[2] == content::module::rigFuelLoop2,
        "all following surface ranks must wait for the next Refit visit");

    state.meta.surfaceDepthUpgrades.surveyArray = 3;
    state.meta.surfaceDepthUpgrades.boreSystem = 2;
    state.meta.rigFuelLoop.rank = 2;
    state.run.surfaceRefitPurchases.rigFuelLoop = true;
    const SaveData captured = captureSaveData(state);
    const std::optional<SaveData> decoded = deserializeSaveData(serializeSaveData(captured));
    require(decoded.has_value() &&
            decoded->surfaceDepthUpgrades.surveyArray == 3 &&
            decoded->surfaceDepthUpgrades.boreSystem == 2 &&
            decoded->rigFuelLoop.rank == 2 &&
            decoded->surfaceRefitPurchases.rigFuelLoop,
        "version-14 saves must round-trip all permanent surface ranks and active Refit purchase limits");
    SaveData old = captured;
    old.version = 13;
    require(!deserializeSaveData(serializeSaveData(old)).has_value(),
        "version-13 saves must be rejected at the requested fresh-campaign boundary");
}

void surfaceDepthTutorialAndSafetyGatesAreHard()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState tutorial = createNewGame(catalog, 19411);
    tutorial.run.destinationIndex = 2;
    startSurfaceExpedition(tutorial, catalog);
    tutorial.run.surfaceExpedition.supply = 10;
    Random rng(19412);

    require(startSurfaceScanRun(tutorial, rng).applied &&
            tutorial.run.surfaceScan.maxPulses == 2,
        "the base Survey Array must map the current level and depth +1 only");
    tutorial.run.surfaceScan.bustRisk = 0.0;
    require(pulseSurfaceScan(tutorial, rng).applied,
        "the tutorial should allow its current-level survey pulse");
    require(bankSurfaceScan(tutorial).applied &&
            !surfaceOpsTutorialDigUnlocked(tutorial),
        "logging only the current level must not unlock Dig");
    const int supplyBeforeUnsurveyedDig = tutorial.run.surfaceExpedition.supply;
    const SurfaceActionOutcome unsurveyed = startSurfacePushRun(tutorial, rng);
    require(!unsurveyed.applied &&
            tutorial.run.surfaceExpedition.supply == supplyBeforeUnsurveyedDig,
        "an unsurveyed Dig must fail without consuming supplies");

    require(startSurfaceScanRun(tutorial, rng).applied,
        "the tutorial should allow another Survey attempt");
    tutorial.run.surfaceScan.bustRisk = 0.0;
    require(pulseSurfaceScan(tutorial, rng).applied,
        "the repeated Survey should map the current level");
    tutorial.run.surfaceScan.bustRisk = 0.0;
    require(pulseSurfaceScan(tutorial, rng).applied,
        "the repeated Survey should map depth +1");
    require(bankSurfaceScan(tutorial).applied &&
            surfaceOpsTutorialDigUnlocked(tutorial),
        "Dig must unlock only after depth +1 is successfully logged");

    const int supplyBeforeSurveyLimit = tutorial.run.surfaceExpedition.supply;
    const SurfaceActionOutcome surveyLimit = startSurfaceScanRun(tutorial, rng);
    require(!surveyLimit.applied &&
            tutorial.run.surfaceExpedition.supply == supplyBeforeSurveyLimit &&
            surveyLimit.message.find("Survey Array limit +1 reached") != std::string::npos,
        "a fully mapped Survey Array envelope must reject another Survey before spending an action kit");
    const SurfaceExpeditionPresentation limitedSurveyPresentation =
        surfaceExpeditionPresentation(tutorial, catalog);
    const auto limitedSurveyAction = std::find_if(
        limitedSurveyPresentation.actions.begin(),
        limitedSurveyPresentation.actions.end(),
        [](const SurfaceActionPreviewPresentation& action) {
            return action.title == text::buttons::surveySite;
        });
    require(limitedSurveyAction != limitedSurveyPresentation.actions.end() &&
            !limitedSurveyAction->action.enabled &&
            limitedSurveyAction->action.label.find("Survey limit +1 reached") != std::string::npos &&
            limitedSurveyAction->availability == "Upgrade Survey Array to survey and dig deeper",
        "Surface Ops must visibly disable Survey when every reachable layer is already mapped");

    const int depthBeforeEmptyBank = tutorial.run.surfaceExpedition.depth;
    require(startSurfacePushRun(tutorial, rng).applied,
        "a surveyed base-depth Dig should start");
    require(!bankSurfacePush(tutorial).applied &&
            tutorial.run.surfaceExpedition.depth == depthBeforeEmptyBank,
        "returning before a Dig step must never advance the start depth");
    tutorial.run.surfacePush.collapseRisk = 0.0;
    require(pushSurfaceDepthStep(tutorial, rng).applied &&
            bankSurfacePush(tutorial).applied &&
            tutorial.run.surfaceExpedition.depth == 1,
        "one surveyed base-rated Dig step should bank depth +1");
    const int supplyBeforeRepeat = tutorial.run.surfaceExpedition.supply;
    require(!startSurfacePushRun(tutorial, rng).applied &&
            tutorial.run.surfaceExpedition.supply == supplyBeforeRepeat,
        "reopening Dig must not refresh or bypass the absolute Survey Array limit");

    GameState limits = createNewGame(catalog, 19413);
    limits.run.destinationIndex = 2;
    startSurfaceExpedition(limits, catalog);
    limits.meta.chapter = GameChapter::RedFrontier;
    limits.meta.surfaceDepthUpgrades.surveyArray = 1;
    limits.run.surfaceExpedition.depthProspects = {{1, 1}, {2, 2}};
    limits.run.surfaceExpedition.depth = 1;
    ui::briefings::acknowledge(
        limits.meta.acknowledgedActivityBriefingIds,
        ui::briefings::surfaceSurveyComplete);
    const int supplyBeforeBoreLimitedSurvey = limits.run.surfaceExpedition.supply;
    const SurfaceActionOutcome boreLimitedSurvey = startSurfaceScanRun(limits, rng);
    require(!boreLimitedSurvey.applied &&
            limits.run.surfaceExpedition.supply == supplyBeforeBoreLimitedSurvey &&
            boreLimitedSurvey.message.find("Bore System limit +1 reached") != std::string::npos,
        "Survey must stop when the Bore System cannot use another mapped layer");
    const SurfaceExpeditionPresentation boreLimitedPresentation =
        surfaceExpeditionPresentation(limits, catalog);
    const auto boreLimitedSurveyAction = std::find_if(
        boreLimitedPresentation.actions.begin(),
        boreLimitedPresentation.actions.end(),
        [](const SurfaceActionPreviewPresentation& action) {
            return action.title == text::buttons::surveySite;
        });
    const auto boreLimitedDigAction = std::find_if(
        boreLimitedPresentation.actions.begin(),
        boreLimitedPresentation.actions.end(),
        [](const SurfaceActionPreviewPresentation& action) {
            return action.title == text::buttons::pushDeeper;
        });
    require(boreLimitedSurveyAction != boreLimitedPresentation.actions.end() &&
            boreLimitedDigAction != boreLimitedPresentation.actions.end() &&
            !boreLimitedSurveyAction->action.enabled &&
            !boreLimitedDigAction->action.enabled &&
            boreLimitedSurveyAction->action.label == "Bore limit +1 reached" &&
            boreLimitedDigAction->action.label == "Bore limit +1 reached" &&
            boreLimitedSurveyAction->availability ==
                "Upgrade Bore System to survey and dig deeper",
        "Survey and Dig must expose the same Bore bottleneck when their permanent ranks diverge");
    SurfaceDepthCapability boreBlocked = surfaceDepthCapability(limits, catalog, 2);
    require(!boreBlocked.canDig && boreBlocked.blocker == SurfaceDepthBlocker::BoreRating,
        "a surveyed level beyond the Bore System rating must remain physically unreachable");

    limits.meta.surfaceDepthUpgrades.boreSystem = 1;
    const SurfaceDepthCapability caution = surfaceDepthCapability(limits, catalog, 2);
    require(caution.canDig &&
            caution.returnSafety.severity != SurfaceReturnSafetySeverity::Critical,
        "a caution-range surveyed level within both ratings must remain selectable");

    limits.meta.surfaceDepthUpgrades.surveyArray = 2;
    limits.meta.surfaceDepthUpgrades.boreSystem = 2;
    limits.run.surfaceExpedition.depth = 2;
    limits.run.surfaceExpedition.depthProspects.push_back({1, 3});
    const SurfaceDepthCapability critical = surfaceDepthCapability(limits, catalog, 3);
    require(!critical.canDig &&
            critical.blocker == SurfaceDepthBlocker::ReturnCritical &&
            (critical.returnSafety.oxygenCritical || critical.returnSafety.fuelCritical),
        "a surveyed and physically reachable layer must still be blocked when return endurance is critical; blocker=" +
            std::to_string(static_cast<int>(critical.blocker)) +
            " severity=" + std::to_string(static_cast<int>(critical.returnSafety.severity)) +
            " oxygen=" + std::to_string(critical.returnSafety.oxygenSeconds) +
            " return=" + std::to_string(critical.returnSafety.estimatedReturnSeconds));
    const int supplyBeforeCritical = limits.run.surfaceExpedition.supply;
    require(!startSurfacePushRun(limits, rng).applied &&
            limits.run.surfaceExpedition.supply == supplyBeforeCritical,
        "critical-return Dig must be rejected before action kits are spent");
}

void surfaceScanTimingWindowsTightenByMappedDepth()
{
    using namespace tuning::research;
    require(
        std::abs(surfaceScanGoodWindowHalfAngleForDepth(0) - scanGoodWindowHalfAngleRadians) < 0.000001,
        "the first surface scan layer should keep the authored good window");
    require(
        std::abs(surfaceScanPerfectWindowHalfAngleForDepth(0) - scanPerfectWindowHalfAngleRadians) < 0.000001,
        "the first surface scan layer should keep the authored perfect window");
    require(
        surfaceScanGoodWindowHalfAngleForDepth(1) < surfaceScanGoodWindowHalfAngleForDepth(0)
            && surfaceScanPerfectWindowHalfAngleForDepth(1) < surfaceScanPerfectWindowHalfAngleForDepth(0),
        "each newly mapped scan layer should tighten both timing windows");
    require(
        surfaceScanGoodWindowHalfAngleForDepth(20) >= scanGoodWindowMinimumHalfAngleRadians
            && surfaceScanPerfectWindowHalfAngleForDepth(20) >= scanPerfectWindowMinimumHalfAngleRadians,
        "deep scan windows should respect their playable minimums");

    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 1941);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    state.run.surfaceExpedition.supply = 10;
    Random rng(1942);
    require(startSurfaceScanRun(state, rng).applied, "surface scan should start for depth-timing coverage");

    // A mapped layer advances the next timing test to depth +1. Its midpoint
    // between Perfect and Good should remain a Good result at that new depth.
    state.run.surfaceScan.depthProspects.push_back({});
    const double depthOneGood = surfaceScanGoodWindowHalfAngleForDepth(1);
    const double depthOnePerfect = surfaceScanPerfectWindowHalfAngleForDepth(1);
    state.run.surfaceScan.elapsedSeconds =
        (scanWindowCenterRadians + (depthOneGood + depthOnePerfect) * 0.5) /
        scanSweepRadiansPerSecond;
    pulseSurfaceScan(state, rng);
    require(
        state.run.surfaceScan.lastPulseGrade == SurfaceScanPulseGrade::Good,
        "surface scan grading should use the tighter current-depth window");
}


void runUpgradesSurviveEmergencyRecall()
{
    const ContentCatalog catalog = createDefaultContent();

    GameState brokenBit = createNewGame(catalog, 648);
    brokenBit.run.destinationIndex = 2;
    startSurfaceExpedition(brokenBit, catalog);
    prepareMiningSiteForTest(brokenBit);
    brokenBit.run.surfaceExpedition.runRigUpgradeRanks = {{content::surfaceUpgrade::shockMounts, 1}};
    require(startMiningRun(brokenBit, catalog).applied, "mining should start for drill break upgrade test");
    brokenBit.run.mining.drillIntegrity = 0.0;
    updateMiningRun(brokenBit, catalog, 0.08);
    require(!brokenBit.run.mining.failurePending, "broken drill bit should not force emergency recall");
    require(runRigUpgradeRank(brokenBit, content::surfaceUpgrade::shockMounts) == 1,
        "run upgrades should survive a broken drill bit");

    GameState recalled = createNewGame(catalog, 649);
    recalled.run.destinationIndex = 2;
    startSurfaceExpedition(recalled, catalog);
    prepareMiningSiteForTest(recalled);
    recalled.run.surfaceExpedition.runRigUpgradeRanks = {
        {content::surfaceUpgrade::shockMounts, 1},
        {content::surfaceUpgrade::oreHopper, 1}
    };
    recalled.run.surfaceExpedition.expeditionLevel = 3;
    recalled.run.surfaceExpedition.expeditionExperience = 7.0;
    recalled.run.surfaceExpedition.runDroneRanks = {{content::drone::miningDrone, 2}};
    recalled.run.surfaceExpedition.droneModuleAssignments = {
        {0, content::drone::miningDrone, DroneModuleKind::CombatDrill}};
    recalled.run.surfaceExpedition.selectedSynergyIds = {"long_haul_rig"};
    require(startMiningRun(recalled, catalog).applied, "mining should start for emergency recall upgrade test");
    recalled.run.mining.droneHealth = 0.0;
    updateMiningRun(recalled, catalog, 0.08);
    require(
        recalled.run.mining.rigDisabled &&
            recalled.run.mining.operatorMode == MiningOperatorMode::Jetpack &&
            recalled.run.mining.operatorPresent &&
            !recalled.run.mining.failurePending,
        "zero rig health should emergency-eject the operator instead of ending the run");
    require(finishMiningRun(recalled, catalog, true).applied, "emergency recall should be acknowledgeable");
    require(runRigUpgradeRank(recalled, content::surfaceUpgrade::shockMounts) == 1 &&
            runRigUpgradeRank(recalled, content::surfaceUpgrade::oreHopper) == 1,
        "emergency recall should preserve run-scoped upgrades");
    require(recalled.run.surfaceExpedition.expeditionLevel == 3 &&
            std::abs(recalled.run.surfaceExpedition.expeditionExperience - 7.0) < 0.001 &&
            expeditionDroneRank(recalled, content::drone::miningDrone) == 2 &&
            recalled.run.surfaceExpedition.droneModuleAssignments.size() == 1 &&
            recalled.run.surfaceExpedition.selectedSynergyIds == std::vector<std::string>{"long_haul_rig"},
        "emergency recall should preserve XP, temporary Drone ranks, grafts, and selected synergies together");
}

void runUpgradeLifetimeFollowsTheTransport()
{
    const ContentCatalog catalog = createDefaultContent();
    auto seedBuild = [](GameState& state) {
        SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
        expedition.expeditionLevel = 4;
        expedition.expeditionExperience = 11.0;
        expedition.pendingRunUpgradeChoices = 1;
        expedition.runRigUpgradeRanks = {{content::surfaceUpgrade::widebandPulse, 2}};
        expedition.runDroneRanks = {{content::drone::surveyDrone, 3}};
        expedition.droneModuleAssignments = {
            {0, content::drone::surveyDrone, DroneModuleKind::PulseStrike}};
        expedition.selectedSynergyIds = {"pathfinder_loop"};
    };
    auto requireBuild = [](const GameState& state, std::string_view transition) {
        const SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
        require(expedition.expeditionLevel == 4 &&
                std::abs(expedition.expeditionExperience - 11.0) < 0.001 &&
                expedition.pendingRunUpgradeChoices == 1 &&
                runRigUpgradeRank(state, content::surfaceUpgrade::widebandPulse) == 2 &&
                expeditionDroneRank(state, content::drone::surveyDrone) == 3 &&
                expedition.droneModuleAssignments.size() == 1 &&
                expedition.selectedSynergyIds == std::vector<std::string>{"pathfinder_loop"},
            std::string("the complete run build should survive ") + std::string(transition));
    };

    GameState state = createNewGame(catalog, 6491);
    state.run.destinationIndex = 2;
    seedBuild(state);
    startSurfaceExpedition(state, catalog);
    requireBuild(state, "landing");
    require(extractSurfacePayload(state, catalog).applied, "a surface extraction should resolve for the run-lifetime test");
    requireBuild(state, "safe Surface extraction");

    LaunchOutcome survived;
    survived.type = LaunchResultType::SafeEject;
    survived.recoveryMethod = RecoveryMethod::ReturnHome;
    survived.destinationId = content::destination::mars;
    applyLaunchOutcome(state, catalog, survived);
    requireBuild(state, "a survived launch");

    startNewExpedition(state, catalog);
    require(state.run.surfaceExpedition.expeditionLevel == 1 &&
            state.run.surfaceExpedition.expeditionExperience == 0.0 &&
            state.run.surfaceExpedition.pendingRunUpgradeChoices == 0 &&
            state.run.surfaceExpedition.runRigUpgradeRanks.empty() &&
            state.run.surfaceExpedition.runDroneRanks.empty() &&
            state.run.surfaceExpedition.droneModuleAssignments.empty() &&
            state.run.surfaceExpedition.selectedSynergyIds.empty(),
        "New Expedition should reset XP and every temporary upgrade family atomically");

    GameState destroyed = createNewGame(catalog, 6492);
    seedBuild(destroyed);
    LaunchOutcome loss;
    loss.type = LaunchResultType::Destroyed;
    loss.destinationId = content::destination::earthOrbit;
    applyLaunchOutcome(destroyed, catalog, loss);
    require(!destroyed.run.active &&
            destroyed.run.surfaceExpedition.expeditionLevel == 1 &&
            destroyed.run.surfaceExpedition.runRigUpgradeRanks.empty() &&
            destroyed.run.surfaceExpedition.runDroneRanks.empty() &&
            destroyed.run.surfaceExpedition.droneModuleAssignments.empty() &&
            destroyed.run.surfaceExpedition.selectedSynergyIds.empty(),
        "Transport destruction should clear XP progression and every temporary upgrade family");
}

void miningDepletionAtShipGracefullyEndsRun()
{
    const ContentCatalog catalog = createDefaultContent();
    auto startParkedAtShip = [&catalog](GameState& state, int seed) {
        state = createNewGame(catalog, seed);
        state.run.destinationIndex = 2;
        state.meta.chapter = GameChapter::RedFrontier;
        startSurfaceExpedition(state, catalog);
        prepareMiningSiteForTest(state);
        require(startMiningRun(state, catalog).applied, "mining run should start for depletion-at-ship test");
        state.run.mining.droneX = state.run.mining.returnZoneX;
        state.run.mining.droneY = state.run.mining.returnZoneY;
    };

    GameState oxygen;
    startParkedAtShip(oxygen, 650);
    oxygen.run.mining.oxygenSeconds = 0.01;
    updateMiningRun(oxygen, catalog, 0.08);
    require(oxygen.screen == Screen::SurfaceExpedition, "oxygen depletion at the ship should return to surface ops");
    require(!oxygen.run.mining.active, "oxygen depletion at the ship should finish the mining run");
    require(!oxygen.run.mining.failurePending, "oxygen depletion at the ship should not start failure recall");

    GameState fuel;
    startParkedAtShip(fuel, 651);
    fuel.run.surfaceExpedition.rigFuel = 0.0;
    fuel.run.mining.oxygenSeconds = 10.0;
    fuel.run.mining.fuelCycleProgress = 1.0;
    updateMiningRun(fuel, catalog, 0.08);
    require(fuel.screen == Screen::SurfaceExpedition, "fuel depletion at the ship should return to surface ops");
    require(!fuel.run.mining.active, "fuel depletion at the ship should finish the mining run");
    require(!fuel.run.mining.failurePending, "fuel depletion at the ship should not start failure recall");
}

void droneBayUnlocksSlotsLoadoutsAndMiningEffects()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState locked = createNewGame(catalog, 647);
    ensureDroneBayState(locked, catalog);
    require(locked.meta.droneBaySlots == 0, "locked drone bay should not expose slots");
    require(locked.meta.ownedDroneIds.empty(), "locked drone bay should not seed owned drones");

    GameState state = createNewGame(catalog, 648);
    state.run.destinationIndex = 2;
    activateOnlyCrew(state, content::astronaut::marco);
    startSurfaceExpedition(state, catalog);
    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    state.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
    state.meta.materials.common = 20;
    ensureDroneBayState(state, catalog);
    require(state.meta.droneBaySlots == 1, "drone bay unlock should initialize one slot");
    require(state.meta.ownedDroneIds.empty(), "the Drone Bay unlock should expose Mining, Resource, and Survey drones as purchasable offers");
    require(catalog.findMiniDrone(content::drone::miningDrone) != nullptr, "mining drone id should resolve");
    require(catalog.findMiniDrone(content::drone::resourceDrone) != nullptr, "resource drone id should resolve");
    require(catalog.findMiniDrone(content::drone::surveyDrone) != nullptr, "survey drone id should resolve");
    require(catalog.findMiniDrone(content::drone::hazardDrone) != nullptr, "hazard drone id should resolve");

    const MiningDrillStats baseline = miningDrillStats(state, catalog);
    require(std::abs(baseline.oxygenSeconds - tuning::mining::oxygenSeconds) < 0.000001, "baseline mining oxygen should use the short starter tank");
    require(equipMiniDrone(state, catalog, 0), "first equipped drone should fit in the starter slot");
    require(!equipMiniDrone(state, catalog, 0), "matching drones should still respect available slot count");
    const MiningDrillStats miningSupported = miningDrillStats(state, catalog);
    require(miningSupported.passiveDroneMiningRate > baseline.passiveDroneMiningRate, "mining drone should add passive mining support");

    state.meta.materials.common = 10;
    state.meta.materials.rare = 10;
    state.meta.materials.exotic = 10;
    require(!upgradeDroneSlot(state, catalog), "paid expansion should not bypass the Mars Slot 2 objective");
    GameState earnedSlotTwo = state;
    earnedSlotTwo.meta.droneBaySlots = 2;
    require(canUpgradeDroneSlot(earnedSlotTwo)
            && upgradeDroneSlot(earnedSlotTwo, catalog)
            && earnedSlotTwo.meta.droneBaySlots == 3,
        "any valid two-slot state should permit the generic paid Slot 3 expansion when materials are available");
    state.meta.unlockKeys.push_back(content::unlock::routeMars);
    require(
        performScenarioAction(
            state,
            catalog,
            content::scenario::marsBayExpansion,
            "briefing",
            ScenarioActionKind::AcknowledgeBriefing).applied,
        "the Mars slot fixture should acknowledge the authored scenario briefing");
    require(
        recordScenarioEvent(
            state,
            catalog,
            {ScenarioEventKind::SafeMaterialDelivered,
             {},
             {},
             content::destination::mars,
             "common",
             tuning::research::marsBayCommonOreGoal,
             0}),
        "the Mars slot fixture should complete its delivery through a generic scenario event");
    require(state.run.surfaceExpedition.expeditionLevel == 2 &&
            std::abs(state.run.surfaceExpedition.expeditionExperience) < 0.001 &&
            state.run.surfaceExpedition.pendingRunUpgradeChoices == 1,
        "completing an authored material-delivery objective should award exactly 10 expedition XP");
    require(claimMarsBayExpansion(state, catalog), "the completed Mars objective should explicitly fabricate Slot 2");
    require(state.meta.droneBaySlots == 2 && state.meta.equippedDroneIds.size() == 1,
        "Mars should add an empty second slot without assigning another drone");
    state.meta.materials.common = 20;
    require(equipMiniDrone(state, catalog, 0), "an open slot should build and assign a paid duplicate Support Drone");
    require(ownedMiniDroneCount(state, content::drone::miningDrone) == 2
            && equippedMiniDroneCount(state, content::drone::miningDrone) == 2
            && state.meta.materials.common == 0,
        "a duplicate Support Drone should consume its material cost and occupy the open slot");
    require(unequipMiniDroneSlot(state, catalog, 1), "the duplicate slot should be independently removable");
    state.meta.materials.common = 20;
    require(equipMiniDrone(state, catalog, 1), "a different support drone should fit in Slot 2");
    const MiningDrillStats resourceSupported = miningDrillStats(state, catalog);
    require(resourceSupported.oxygenSeconds > miningSupported.oxygenSeconds, "resource drone should extend oxygen");
    const MiniDroneLoadoutEffects beforeTune = miniDroneLoadoutEffects(state, catalog);
    state.run.surfaceExpedition.runDroneRanks = {{content::drone::miningDrone, 2}};
    require(expeditionDroneRank(state, content::drone::miningDrone) == 2,
        "a run-scoped Drone rank should advance every Prospector copy to Mk II");
    const MiniDroneLoadoutEffects afterTune = miniDroneLoadoutEffects(state, catalog);
    require(afterTune.passiveMiningRate > beforeTune.passiveMiningRate,
        "run-scoped Drone ranks should scale passive mining output");

    for (int i = 0; i < 4; ++i) {
        state.meta.materials.common = 99;
        state.meta.materials.rare = 99;
        state.meta.materials.exotic = 99;
        upgradeDroneSlot(state, catalog);
    }
    require(state.meta.droneBaySlots == 6, "drone bay slots should cap at six");
    require(!upgradeDroneSlot(state, catalog), "maxed drone bay should reject further slot upgrades");

    const std::string serialized = serializeSaveData(captureSaveData(state));
    const auto save = deserializeSaveData(serialized);
    require(save.has_value(), "drone bay save should parse");

    GameState restored = createNewGame(catalog, 1);
    restoreSaveData(restored, catalog, *save);
    require(restored.meta.droneBaySlots == state.meta.droneBaySlots, "drone bay slots should round trip");
    require(restored.meta.ownedDroneIds == state.meta.ownedDroneIds, "owned drones should round trip");
    require(restored.meta.equippedDroneIds == state.meta.equippedDroneIds, "equipped drones should round trip");
    require(expeditionDroneRank(restored, content::drone::miningDrone) == 2,
        "run-scoped Drone ranks should round trip with the active expedition");

    restored.meta.unlockKeys.push_back(content::unlock::perimeterDrones);
    ensureDroneBayState(restored, catalog);
    require(std::find(restored.meta.ownedDroneIds.begin(), restored.meta.ownedDroneIds.end(), content::drone::attackDrone) == restored.meta.ownedDroneIds.end(), "post-solar unlock should expose attack drones as purchasable offers");
    require(std::find(restored.meta.ownedDroneIds.begin(), restored.meta.ownedDroneIds.end(), content::drone::defenseDrone) == restored.meta.ownedDroneIds.end(), "post-solar unlock should expose defense drones as purchasable offers");
    const auto attackIndex = std::find_if(catalog.miniDrones.begin(), catalog.miniDrones.end(), [](const MiniDrone& drone) {
        return drone.role == MiniDroneRole::Attack;
    });
    require(attackIndex != catalog.miniDrones.end(), "default content should include an Attack drone");
    restored.meta.droneBaySlots = 2;
    restored.meta.ownedDroneIds.push_back(content::drone::attackDrone);
    restored.meta.ownedDroneIds.push_back(content::drone::defenseDrone);
    restored.meta.equippedDroneIds = {content::drone::attackDrone, content::drone::defenseDrone};
    ensureDroneBayState(restored, catalog);
    const auto defenseIndex = std::find_if(catalog.miniDrones.begin(), catalog.miniDrones.end(), [](const MiniDrone& drone) {
        return drone.role == MiniDroneRole::Defense;
    });
    require(defenseIndex != catalog.miniDrones.end(), "default content should include a Defense drone");
    restored.run.surfaceExpedition.selectedSynergyIds = {"killbox_screen"};
    const MiniDroneLoadoutEffects uncoordinated = miniDroneLoadoutEffects(restored, catalog);
    require(uncoordinated.synergyNames.empty(),
        "a selected combat formation should remain dormant before its required research");
    restored.meta.unlockKeys.push_back(content::unlock::perimeterCoordination);
    const MiniDroneLoadoutEffects coordinated = miniDroneLoadoutEffects(restored, catalog);
    require(!coordinated.synergyNames.empty(),
        "perimeter coordination should activate a previously selected compatible formation");

    const ResearchProject* perimeterProject = catalog.findResearchProject(content::research::perimeterDroneNetwork);
    require(perimeterProject != nullptr && perimeterProject->unlockKey == content::unlock::perimeterDrones &&
            perimeterProject->rewardUnlockKey == content::unlock::perimeterCoordination,
        "Perimeter Drone Network should consume the Arkfall kit unlock and reward advanced coordination");
}

void firstMiningContractBuildsAndCelebratesProspector()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 0xC0A11);
    state.run.destinationIndex = 2;
    state.meta.furthestTier = 1;
    require(acknowledgeCampaignObjectiveBriefing(state, CampaignObjectiveId::LunarProspector),
        "the mandatory lunar mining briefing should acknowledge explicitly");

    const auto recoverMiningOre = [](GameState& target, int common) {
        for (int seed = 1; seed <= 512; ++seed) {
            GameState candidate = target;
            candidate.run.surfaceExpedition = {};
            candidate.run.surfaceExpedition.active = true;
            candidate.run.surfaceExpedition.destinationId = content::destination::moon;
            candidate.run.surfaceExpedition.temporaryMaterials.common = common;
            candidate.run.surfaceExpedition.bankedMiningMaterials.common = common;
            candidate.run.surfaceExpedition.bankedMiningArenaValid = true;
            candidate.run.surfaceExpedition.bankedMiningProgressionEligible = true;
            candidate.run.surfaceExpedition.bankedMiningArenaMetadata.act = MiningAct::ActOne;
            candidate.run.surfaceExpedition.bankedMiningArenaMetadata.difficulty = 1;
            candidate.run.surfaceExpedition.bankedMiningArenaMetadata.seed = 0xC0A11;
            Random rng(seed);
            const SurfaceActionOutcome outcome = extractSurfacePayload(candidate);
            if (outcome.cargoRecovered) {
                target = std::move(candidate);
                return outcome;
            }
        }
        throw std::runtime_error("test should find a clean Prospector extraction seed");
    };

    require(tuning::research::prospectorCommonOreGoal == 30,
        "the lunar Prospector contract should require thirty safely delivered Common Ore");
    const SurfaceActionOutcome firstRecovery = recoverMiningOre(state, 10);
    require(firstRecovery.applied && !firstRecovery.prospectorUnlocked,
        "a partial Prospector contract should bank progress without unlocking the drone");
    require(state.meta.prospectorCommonOreRecovered == 10,
        "only safely extracted mining ore should advance the Prospector contract");
    require(state.meta.materials.common == 0,
        "Prospector contract ore should be committed to fabrication instead of ordinary research spend");
    require(firstRecovery.materialCommitted.common == 10,
        "a deterministic return should commit the delivered contract ore");
    require(!hasUnlock(state.meta, content::unlock::droneBay),
        "the Prospector should remain locked before all contract ore is home");

    const SurfaceActionOutcome completion = recoverMiningOre(state, 20);
    require(!completion.prospectorUnlocked && canClaimLunarProspector(state),
        "the thirtieth safely extracted Common Ore should create a claim-ready contract without applying its reward");
    require(state.meta.prospectorCommonOreRecovered == tuning::research::prospectorCommonOreGoal,
        "Prospector progress should stop at its stated goal");
    require(!hasUnlock(state.meta, content::unlock::droneBay),
        "reaching the lunar goal must not implicitly install the Prospector");
    // The recovered payload came from the Moon; surface-return presentation
    // should therefore resolve the active lunar contract rather than the
    // test fixture's provisional next-route index.
    state.run.destinationIndex = 1;
    state.screen = Screen::Hangar;
    Random launchRng(0xC0A11);
    const PreparedLaunch launch = prepareLaunch(state, catalog, launchRng);
    std::string html = buildGamePanelHtml({state, catalog, launch, launch});
    const std::string lunarClaimModalId =
        "scenario_" + std::string(content::scenario::lunarProspector) + "_delivery";
    const std::string lunarClaimAction = ui::actions::scenarioAction(
        content::scenario::lunarProspector,
        "delivery",
        static_cast<int>(ScenarioActionKind::ClaimReward));
    require(html.find("data-modal=\"" + lunarClaimModalId + "\" data-auto-modal=\"1\"") != std::string::npos
            && html.find("data-modal-dismissible=\"0\"") != std::string::npos
            && html.find("data-rr-action=\"" + lunarClaimAction + "\"") != std::string::npos,
        "the ready lunar contract should receive a mandatory explicit-claim modal");
    require(claimLunarProspector(state, catalog), "Install Prospector Mk I should explicitly claim the ready contract");
    require(hasUnlock(state.meta, content::unlock::droneBay)
            && !hasUnlock(state.meta, content::unlock::droneSupportSuite)
            && !hasUnlock(state.meta, content::unlock::ioHazardDrone),
        "the explicit lunar claim should unlock only the Prospector support package");
    require(state.meta.droneBaySlots == 1
            && state.meta.ownedDroneIds.empty()
            && state.meta.equippedDroneIds.empty()
            && state.meta.materials.common == 20,
        "the claim should fund, but not silently fabricate, the first Prospector Support Drone");
    const DroneOpsPresentation firstDroneOps = droneOpsPresentation(state, catalog);
    require(!firstDroneOps.drones.empty() &&
            !firstDroneOps.drones.front().action.actionId.empty(),
        "the first Drone Ops fabrication action should expose a stable action id");
    const int miningDroneIndex = static_cast<int>(std::find_if(
        catalog.miniDrones.begin(), catalog.miniDrones.end(), [](const MiniDrone& drone) {
            return drone.id == content::drone::miningDrone;
        }) - catalog.miniDrones.begin());
    require(equipMiniDrone(state, catalog, miningDroneIndex),
        "Drone Ops should let the player spend the contract fabrication grant on Prospector Mk I");
    require(state.meta.ownedDroneIds == std::vector<std::string>{content::drone::miningDrone}
            && state.meta.equippedDroneIds == std::vector<std::string>{content::drone::miningDrone}
            && state.meta.materials.common == 0,
        "fabricating Prospector Mk I should teach the first purchase and assignment together");

    const std::string serialized = serializeSaveData(captureSaveData(state));
    const auto save = deserializeSaveData(serialized);
    require(save.has_value(), "pending Prospector completion should serialize");
    GameState restored = createNewGame(catalog, 1);
    restoreSaveData(restored, catalog, *save);
    require(restored.meta.prospectorCommonOreRecovered == tuning::research::prospectorCommonOreGoal
            && restored.meta.lunarProspectorClaimed,
        "claimed Prospector progress should survive a save roundtrip");

    ui::briefings::acknowledge(restored.meta.acknowledgedActivityBriefingIds, ui::briefings::prospectorComplete);
    restored.screen = Screen::Hangar;
    Random restoredLaunchRng(1);
    const PreparedLaunch restoredLaunch = prepareLaunch(restored, catalog, restoredLaunchRng);
    html = buildGamePanelHtml({restored, catalog, restoredLaunch, restoredLaunch});
    require(html.find(lunarClaimModalId) == std::string::npos,
        "the Prospector celebration should not repeat after acknowledgment");

}

void explicitSolarCampaignObjectivesGateRewardsAndRoutes()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 0x10A7);
    state.run.destinationIndex = 1;
    state.meta.furthestTier = 1;
    state.meta.launchLessons.stage = LaunchTrainingStage::MarsTransfer;
    state.meta.launchUpgrades.fuelTanks = 2;

    FrontierGateStatus gate = frontierGateStatus(state, catalog);
    require(gate.kind == FrontierGateKind::ScenarioRequirement && !gate.satisfied &&
            gate.scenarioId == content::scenario::lunarProspector && gate.scenarioStepId == "delivery",
        "Mars should be gated by the explicit Lunar Prospector scenario requirement");
    state.meta.launchUpgrades.fuelTanks = 1;
    gate = frontierGateStatus(state, catalog);
    require(gate.kind == FrontierGateKind::ScenarioRequirement && !gate.satisfied,
        "an authored route requirement must remain the visible Mars blocker before ship hardware readiness");
    Random lockedMarsPanelRng(0x10A7);
    const PreparedLaunch lockedMarsPrepared = prepareLaunch(state, catalog, lockedMarsPanelRng);
    const std::string lockedMarsPanel = buildGamePanelHtml({
        state,
        catalog,
        lockedMarsPrepared,
        lockedMarsPrepared});
    const std::size_t lockedMarsLabel = lockedMarsPanel.find("Mars: Unavailable");
    const std::size_t lockedMarsButton = lockedMarsLabel == std::string::npos
        ? std::string::npos
        : lockedMarsPanel.rfind("<button", lockedMarsLabel);
    const std::size_t lockedMarsButtonEnd = lockedMarsLabel == std::string::npos
        ? std::string::npos
        : lockedMarsPanel.find("</button>", lockedMarsLabel);
    require(lockedMarsButton != std::string::npos
            && lockedMarsButtonEnd != std::string::npos
            && lockedMarsPanel.substr(
                lockedMarsButton,
                lockedMarsButtonEnd - lockedMarsButton).find(" disabled") != std::string::npos
            && lockedMarsPanel.substr(
                lockedMarsButton,
                lockedMarsButtonEnd - lockedMarsButton).find("data-ui-modal") == std::string::npos
            && lockedMarsPanel.substr(
                lockedMarsButton,
                lockedMarsButtonEnd - lockedMarsButton).find("data-rr-action") == std::string::npos,
        "a mission-locked destination must render as Unavailable without a details or action target");

    GameState genericLockedRoute = createNewGame(catalog, 0x10A9);
    genericLockedRoute.run.destinationIndex = 4;
    genericLockedRoute.meta.furthestTier = 4;
    genericLockedRoute.meta.launchLessons.stage = LaunchTrainingStage::Complete;
    genericLockedRoute.run.frontierReadiness = 0;
    genericLockedRoute.screen = Screen::Hangar;
    Random genericLockedPanelRng(0x10A9);
    const PreparedLaunch genericLockedPrepared = prepareLaunch(
        genericLockedRoute,
        catalog,
        genericLockedPanelRng);
    const std::string genericLockedPanel = buildGamePanelHtml({
        genericLockedRoute,
        catalog,
        genericLockedPrepared,
        genericLockedPrepared});
    require(
        frontierGateStatus(genericLockedRoute, catalog).kind == FrontierGateKind::FlightData
            && !frontierGateStatus(genericLockedRoute, catalog).satisfied
            && genericLockedPanel.find(
                "<button class=\"disabled rr-text-button\" disabled><span class=\"rr-button-label\">Uranus: Unavailable</span></button>")
                != std::string::npos,
        "a generic Flight Data lock must use the same disabled Unavailable destination treatment");

    GameState lockedJupiterRoute = createNewGame(catalog, 0x10AA);
    lockedJupiterRoute.run.destinationIndex = 2;
    lockedJupiterRoute.meta.furthestTier = 2;
    lockedJupiterRoute.meta.launchLessons.stage = LaunchTrainingStage::HullIntegrity;
    lockedJupiterRoute.meta.launchUpgrades.fuelTanks = 3;
    lockedJupiterRoute.meta.unlockKeys.push_back(content::unlock::routeMars);
    lockedJupiterRoute.screen = Screen::Hangar;
    Random lockedJupiterPanelRng(0x10AA);
    const PreparedLaunch lockedJupiterPrepared = prepareLaunch(
        lockedJupiterRoute,
        catalog,
        lockedJupiterPanelRng);
    const std::string lockedJupiterPanel = buildGamePanelHtml({
        lockedJupiterRoute,
        catalog,
        lockedJupiterPrepared,
        lockedJupiterPrepared});
    require(
        lockedJupiterPanel.find("Jupiter: Unavailable") != std::string::npos
            && lockedJupiterPanel.find("JUPITER OPTIONS") == std::string::npos
            && lockedJupiterPanel.find("Review options") == std::string::npos
            && lockedJupiterPanel.find("Transfer: Jupiter") == std::string::npos
            && lockedJupiterPanel.find("Begin Mars Slingshot") == std::string::npos,
        "an authored Jupiter lock must suppress its special transfer details and actions until the objective opens the route");

    state.meta.launchUpgrades.fuelTanks = 2;
    require(acknowledgeCampaignObjectiveBriefing(state, CampaignObjectiveId::LunarProspector),
        "the lunar objective briefing should require an explicit acknowledgment");
    require(tuning::research::prospectorCommonOreGoal == 30
            && tuning::research::marsBayCommonOreGoal == 40,
        "early campaign contracts should use the tuned thirty and forty ore requirements");
    SaveData partialContractSave = captureSaveData(state);
    partialContractSave.version = save_schema::currentVersion;
    partialContractSave.prospectorCommonOreRecovered = 3;
    GameState restoredPartialContract = createNewGame(catalog, 0x10A8);
    restoreSaveData(restoredPartialContract, catalog, partialContractSave);
    require(restoredPartialContract.meta.prospectorCommonOreRecovered == 3
            && !restoredPartialContract.meta.lunarProspectorClaimed,
        "an unfinished current save should retain its delivered contract ore and continue toward the new goal");
    state.meta.materials.common = 30;
    require(creditCampaignCommonOre(state, content::destination::mars, 30) == 0,
        "Mars ore must not satisfy the destination-attributed lunar contract");
    require(creditCampaignCommonOre(state, content::destination::moon, 30) == 30
            && state.meta.materials.common == 0,
        "safely extracted lunar ore should be reserved instead of entering the spendable pool");
    require(canClaimLunarProspector(state) && claimLunarProspector(state, catalog),
        "the delivered lunar goal should become an explicit Prospector claim");
    state.meta.materials.common = 20;
    require(equipMiniDrone(state, catalog, 0),
        "the campaign fixture should fabricate Prospector before continuing to Mars");
    require(frontierGateStatus(state, catalog).satisfied,
        "claiming Prospector Mk I should satisfy the Mars frontier gate");

    state.run.destinationIndex = 2;
    state.meta.furthestTier = 2;
    startSurfaceExpedition(state, catalog);
    const CampaignObjectiveStatus freshMarsObjective =
        campaignObjectiveStatus(state, CampaignObjectiveId::MarsBayExpansion);
    require(state.meta.droneBaySlots == 1
            && freshMarsObjective.state == CampaignObjectiveState::Active
            && freshMarsObjective.current == 0
            && freshMarsObjective.required == tuning::research::marsBayCommonOreGoal
            && !state.meta.marsBayExpansionClaimed,
        "a fresh Mars arrival should keep one bay slot and start the explicit 0/40 expansion objective");
    state.screen = Screen::SurfaceExpedition;
    require(acknowledgeCampaignObjectiveBriefing(state, CampaignObjectiveId::MarsBayExpansion),
        "the Mars bay objective should have its own briefing");
    state.run.surfaceExpedition = {};
    state.screen = Screen::Hangar;
    state.meta.materials.common = 40;
    require(creditCampaignCommonOre(state, content::destination::mars, 40) == 40,
        "only safely extracted Mars ore should fill the Mars contract");
    require(claimMarsBayExpansion(state, catalog)
            && state.meta.droneBaySlots == 2
            && state.meta.equippedDroneIds == std::vector<std::string>{content::drone::miningDrone},
        "the Mars claim should fabricate an empty Slot 2 without duplicating the Prospector");
    require(state.meta.equippedDroneIds == std::vector<std::string>{content::drone::miningDrone},
        "the Mars reward should leave the new slot empty until the player assigns or builds a Support Drone");
    const ScenarioInstance* marsScenario =
        findScenarioInstance(state.meta, content::scenario::marsBayExpansion);
    const ScenarioStepProgress* funding = marsScenario == nullptr
        ? nullptr
        : findScenarioStepProgress(*marsScenario, "funding");
    require(funding != nullptr && !funding->briefingAcknowledged &&
            scenarioStepState(
                state,
                catalog,
                content::scenario::marsBayExpansion,
                "funding") == ScenarioStepState::Active,
        "claiming the Mars contract must reveal one saved Jupiter funding briefing without granting another reward");

    GameState preReviewJupiterWindow = state;
    preReviewJupiterWindow.meta.launchLessons.stage = LaunchTrainingStage::HullIntegrity;
    Random preReviewJupiterPanelRng(0x10AB);
    const PreparedLaunch preReviewJupiterPrepared = prepareLaunch(
        preReviewJupiterWindow,
        catalog,
        preReviewJupiterPanelRng);
    const std::string preReviewJupiterPanel = buildGamePanelHtml({
        preReviewJupiterWindow,
        catalog,
        preReviewJupiterPrepared,
        preReviewJupiterPrepared});
    require(
        preReviewJupiterPanel.find("Review Options") != std::string::npos
            && preReviewJupiterPanel.find("Begin Mars Slingshot") == std::string::npos,
        "the Mars Hangar must present the authored Jupiter review before offering the slingshot action");

    const ScenarioActionOutcome fundingAcknowledged = performScenarioAction(
        state,
        catalog,
        content::scenario::marsBayExpansion,
        "funding",
        ScenarioActionKind::AcknowledgeBriefing);
    require(fundingAcknowledged.applied &&
            scenarioHasCompletedStep(
                state,
                content::scenario::marsBayExpansion,
                "funding"),
        "the Jupiter funding briefing must acknowledge once through the existing scenario action system");
    GameState reviewedJupiterWindow = state;
    reviewedJupiterWindow.meta.launchLessons.stage = LaunchTrainingStage::HullIntegrity;
    Random reviewedJupiterPanelRng(0x10AC);
    const PreparedLaunch reviewedJupiterPrepared = prepareLaunch(
        reviewedJupiterWindow,
        catalog,
        reviewedJupiterPanelRng);
    const std::string reviewedJupiterPanel = buildGamePanelHtml({
        reviewedJupiterWindow,
        catalog,
        reviewedJupiterPrepared,
        reviewedJupiterPrepared});
    require(
        reviewedJupiterPanel.find("Begin Mars Slingshot") != std::string::npos,
        "reviewing the Jupiter options must reveal the now-actionable Mars slingshot control");

    state.run.destinationIndex = 3;
    state.meta.furthestTier = 3;
    require(commissionIoHazardDrone(state, catalog),
        "the Io briefing action should explicitly commission Hazard Drone Mk I");
    const ScenarioInstance* ioScenario =
        findScenarioInstance(state.meta, content::scenario::volcanicDescent);
    const ScenarioStepProgress* ioCommission = ioScenario == nullptr
        ? nullptr
        : findScenarioStepProgress(*ioScenario, "commission");
    require(
        ioCommission != nullptr && ioCommission->briefingAcknowledged &&
            ioCommission->completed && ioCommission->claimed,
        "a mandatory manual scenario action should acknowledge and complete its named directive in one click");
    require(hasUnlock(state.meta, content::unlock::ioHazardDrone)
            && !hasUnlock(state.meta, content::unlock::droneSupportSuite)
            && state.meta.equippedDroneIds == std::vector<std::string>{
                content::drone::miningDrone,
                content::drone::hazardDrone},
        "Io commissioning should grant and fill the empty slot with Hazard only");

    // A recovery site is campaign context for the normal mining loop, not a
    // replacement verb. Surface Ops must retain Mine alongside its named
    // recovery action so players can use the familiar activity entry point.
    GameState ioSurface = state;
    startSurfaceExpedition(ioSurface, catalog);
    require(ioSurface.run.surfaceExpedition.active,
        "Io recovery presentation requires an active Surface Ops loop");
    ioSurface.screen = Screen::SurfaceExpedition;
    Random ioPanelRng(0x10F00D);
    const PreparedLaunch ioPanelLaunch = prepareLaunch(ioSurface, catalog, ioPanelRng);
    const std::string ioSurfaceHtml = buildGamePanelHtml({ioSurface, catalog, ioPanelLaunch, ioPanelLaunch});
    require(ioSurfaceHtml.find(std::string(text::buttons::mineDeposit)) != std::string::npos,
        "a named Io recovery should retain the normal Mine action");

    ArtifactRecord ioArtifact {
        "io_minor_artifact",
        content::destination::jupiter,
        false,
        ArtifactKind::Boost,
        ArtifactRewardType::None,
        1.0,
        false,
    };
    require(creditRecoveredIoArtifact(state, ioArtifact)
            && !creditRecoveredIoArtifact(state, ioArtifact)
            && state.meta.ioArtifactRecovered
            && state.run.surfaceExpedition.runDroneRanks.empty(),
        "a protected objective should record exactly once without granting a free permanent Drone rank");

    require(startSaturnSlingshotRun(state, catalog)
            && state.screen == Screen::Flyby
            && state.run.flyby.purpose == FlybyPurpose::SaturnSlingshot,
        "the Io objective should launch a distinct departure Flyby");
    state.run.flyby.completed = true;
    state.run.flyby.result = FlybyGrade::Good;
    completeFlybyRun(state, catalog);
    require(state.screen == Screen::Hangar
            && state.meta.saturnSlingshotFailed
            && !state.meta.saturnRouteUnlocked,
        "a non-Perfect departure Flyby should return to Hangar with Saturn locked");
    require(acknowledgeSaturnSlingshotFailure(state),
        "the first failed slingshot explanation should persist until acknowledged");

    require(startSaturnSlingshotRun(state, catalog), "a failed slingshot should remain retryable");
    state.run.flyby.completed = true;
    state.run.flyby.result = FlybyGrade::Perfect;
    completeFlybyRun(state, catalog);
    require(canClaimSaturnCourse(state)
            && !state.meta.saturnRouteUnlocked
            && claimSaturnCourse(state, catalog),
        "Perfect should create a separate explicit Lock Saturn Course claim");
    gate = frontierGateStatus(state, catalog);
    require(gate.kind == FrontierGateKind::ScenarioRequirement && gate.satisfied,
        "claiming the saved Perfect solution should permanently satisfy Saturn's authored route requirement");
}

void campaignStateRoundTripsAtCurrentVersion()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 0x7007);
    state.run.destinationIndex = 1;
    state.meta.furthestTier = 1;
    require(acknowledgeCampaignObjectiveBriefing(state, CampaignObjectiveId::LunarProspector),
        "the normalized campaign fixture should acknowledge the Lunar briefing");
    state.meta.materials.common = tuning::research::prospectorCommonOreGoal;
    require(creditCampaignCommonOre(
                state,
                content::destination::moon,
                tuning::research::prospectorCommonOreGoal) == tuning::research::prospectorCommonOreGoal &&
            claimLunarProspector(state, catalog),
        "the normalized campaign fixture should claim the Lunar contract through scenario actions");
    state.run.destinationIndex = 2;
    state.meta.furthestTier = 2;
    require(acknowledgeCampaignObjectiveBriefing(state, CampaignObjectiveId::MarsBayExpansion),
        "the normalized campaign fixture should acknowledge the Mars briefing");
    state.meta.materials.common = tuning::research::marsBayCommonOreGoal;
    require(creditCampaignCommonOre(
                state,
                content::destination::mars,
                tuning::research::marsBayCommonOreGoal) == tuning::research::marsBayCommonOreGoal &&
            claimMarsBayExpansion(state, catalog),
        "the normalized campaign fixture should claim the Mars contract through scenario actions");
    state.run.destinationIndex = 3;
    state.meta.furthestTier = 3;
    require(commissionIoHazardDrone(state, catalog),
        "the normalized campaign fixture should commission its configured Hazard support");
    ArtifactRecord recoveredObjective {
        "io_minor_artifact",
        content::destination::jupiter,
        false,
        ArtifactKind::Boost,
        ArtifactRewardType::None,
        1.0,
        false,
    };
    require(creditRecoveredIoArtifact(state, recoveredObjective),
        "the normalized campaign fixture should resolve its protected objective through the scenario dispatcher");
    require(startSaturnSlingshotRun(state, catalog),
        "the normalized campaign fixture should begin an active departure Flyby");
    state.run.flyby.completed = true;
    state.run.flyby.result = FlybyGrade::Good;
    completeFlybyRun(state, catalog);
    require(acknowledgeSaturnSlingshotFailure(state) && startSaturnSlingshotRun(state, catalog),
        "the normalized campaign fixture should persist an acknowledged failed challenge before its retry");
    const SaveData activeSave = captureSaveData(state);
    require(activeSave.version == save_schema::currentVersion && activeSave.screen == Screen::Hangar,
        "saving during the special Flyby should normalize safely to Hangar");
    const std::optional<SaveData> parsed = deserializeSaveData(serializeSaveData(activeSave));
    require(parsed.has_value(), "current campaign state should deserialize");
    GameState restored = createNewGame(catalog, 1);
    restoreSaveData(restored, catalog, *parsed);
    require(restored.meta.lunarProspectorClaimed
            && restored.meta.marsBayExpansionClaimed
            && restored.meta.ioHazardDroneCommissioned
            && restored.meta.ioArtifactRecovered
            && restored.meta.saturnSlingshotFailureAcknowledged
            && canStartSaturnSlingshot(restored, catalog),
        "current campaign flags and a retryable normalized slingshot should survive roundtrip");

}

void scenarioUiActionsDoNotAwardExpeditionExperience()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 6481);
    state.meta.unlockKeys.push_back(content::unlock::routeJupiter);
    ensureScenarioInstances(state, catalog);

    const ScenarioActionOutcome outcome = performScenarioAction(
        state,
        catalog,
        content::scenario::volcanicDescent,
        "commission",
        ScenarioActionKind::BeginActivity);
    require(outcome.applied,
        "the manual Hazard Drone commissioning action should resolve in the UI-action XP regression");
    require(hasUnlock(state.meta, content::unlock::droneBay)
            && state.meta.droneBaySlots == 1
            && state.meta.ownedDroneIds == std::vector<std::string>{content::drone::hazardDrone}
            && state.meta.equippedDroneIds == std::vector<std::string>{content::drone::hazardDrone},
        "Io commissioning should provide a usable bay and assigned Hazard frame even when route access came from outside the early campaign");
    require(state.run.surfaceExpedition.expeditionLevel == 1 &&
            state.run.surfaceExpedition.expeditionExperience == 0.0 &&
            state.run.surfaceExpedition.pendingRunUpgradeChoices == 0,
        "briefings, manual actions, equipment assignment, and other UI actions must not grant expedition XP");

    ScenarioInstance* volcanic = findScenarioInstance(state.meta, content::scenario::volcanicDescent);
    require(volcanic != nullptr, "the Io reward repair fixture requires its authored scenario instance");
    volcanic->awardedRewardIds = {
        std::string(content::scenario::volcanicDescent) + "/commission/0",
        std::string(content::scenario::volcanicDescent) + "/commission/1"};
    state.meta.unlockKeys.erase(
        std::remove(state.meta.unlockKeys.begin(), state.meta.unlockKeys.end(), content::unlock::droneBay),
        state.meta.unlockKeys.end());
    state.meta.droneBaySlots = 0;
    state.meta.ownedDroneIds.clear();
    state.meta.equippedDroneIds.clear();

    ensureScenarioInstances(state, catalog);
    require(hasUnlock(state.meta, content::unlock::droneBay)
            && state.meta.droneBaySlots == 1
            && state.meta.ownedDroneIds == std::vector<std::string>{content::drone::hazardDrone}
            && state.meta.equippedDroneIds == std::vector<std::string>{content::drone::hazardDrone},
        "a claimed Io commission should repair missing permanent bay and Hazard rewards from a partial save");

    state.meta.equippedDroneIds.clear();
    ensureScenarioInstances(state, catalog);
    require(state.meta.equippedDroneIds.empty(),
        "persistent reward repair should preserve a player's later choice to unequip an owned frame");
}


void scenarioAndCocoonStateRoundTrips()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 0x9009);

    ScenarioInstance generated;
    generated.id = "generated_fixture_42";
    generated.definitionId = content::scenario::generatedTemplate;
    generated.definitionVersion = 3;
    generated.source = ScenarioSource::Procedural;
    generated.factoryId = "fixture_factory";
    generated.factoryVersion = 2;
    generated.seed = 0x900942;
    generated.resolvedParameters = {
        "alpha|beta",
        "gamma^delta",
        "epsilon:semicolon",
    };
    ScenarioStepProgress generatedStep;
    generatedStep.id = "recover^payload";
    generatedStep.progress = 7;
    generatedStep.briefingAcknowledged = true;
    generatedStep.completed = true;
    generatedStep.claimed = true;
    generatedStep.failureSeen = true;
    generatedStep.failureAcknowledged = true;
    generated.steps = {generatedStep};
    generated.awardedRewardIds = {
        "generated_fixture_42/recover^payload/0",
        "generated_fixture_42/recover^payload/1",
    };
    generated.completed = true;
    state.meta.scenarios.push_back(generated);
    state.meta.miningSites = {
        {
            "roundtrip_site",
            content::destination::jupiter,
            MiningAct::ActOne,
            8,
            0x900943,
            MiningGateType::HazardCocoon,
            {},
            true,
            false,
            false,
        },
        {
            "roundtrip_legacy_site",
            content::destination::jupiter,
            MiningAct::ActOne,
            8,
            0x900944,
            MiningGateType::HazardCocoon,
            "legacy_payload",
            true,
            true,
            true,
        },
    };

    state.run.surfaceExpedition.active = true;
    state.run.surfaceExpedition.destinationId =
        content::destination::jupiter;
    state.run.surfaceExpedition.pendingScenarioId =
        content::scenario::volcanicDescent;
    state.run.surfaceExpedition.pendingScenarioStepId = "recovery";
    state.run.surfaceExpedition.pendingMiningSiteDefinitionId =
        content::miningSite::thermalLayeredRecovery;

    const auto makeTerrain = [](int depthZone) {
        MiningTerrain terrain;
        terrain.width = 11;
        terrain.height = 11;
        terrain.depthZone = depthZone;
        terrain.cells.assign(
            static_cast<std::size_t>(terrain.width * terrain.height),
            MiningCell {});
        return terrain;
    };
    MiningRunState& mining = state.run.mining;
    mining.active = true;
    mining.arenaMetadata.act = MiningAct::ActTwo;
    mining.arenaMetadata.act = MiningAct::ActTwo;
    state.meta.equippedDroneIds = {content::drone::surveyDrone};
    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    state.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
    mining.destinationId = content::destination::jupiter;
    mining.scenarioId = content::scenario::volcanicDescent;
    mining.scenarioStepId = "recovery";
    mining.miningSiteDefinitionId =
        content::miningSite::thermalLayeredRecovery;
    mining.miningSiteBiome = MiningSiteBiome::ThermalLava;
    mining.siteBaselineOxygenSeconds = 60.0;
    mining.depthZone = 1;
    mining.entryDepthZone = 0;
    mining.deepestDepthZone = 1;
    mining.terrain = makeTerrain(1);
    mining.gate.active = true;
    mining.gate.type = MiningGateType::HazardCocoon;
    mining.gate.cocoonDefinitionId = "roundtrip_cocoon";
    mining.gate.cocoonDefinitionVersion = 4;
    mining.gate.protectedObjective = {
        ProtectedObjectiveKind::Artifact,
        "roundtrip_payload",
    };
    mining.gate.activeCocoonLayer = 0;
    mining.gate.cocoonLayers = {
        {
            "outer",
            "OUTER",
            4,
            2,
            1,
            true,
            false,
            MiningCocoonCompletionRule::TreatAndExcavate,
            MiningCocoonRevealPolicy::OnAnyCellDiscovered,
        },
        {
            "inner",
            "INNER",
            4,
            4,
            2,
            false,
            false,
            MiningCocoonCompletionRule::TreatOnly,
            MiningCocoonRevealPolicy::AfterPreviousLayerCompleted,
        },
    };
    MiningCell* activeTagged = miningCellAt(mining.terrain, 2, 2);
    require(activeTagged != nullptr, "active cocoon cell should exist");
    activeTagged->material = MiningCellMaterial::HazardPocket;
    activeTagged->hazard = true;
    activeTagged->gateAssociated = true;
    activeTagged->cocoonLayer = 0;
    activeTagged->revealed = true;

    MiningDepthLayerState cached;
    cached.depthZone = 0;
    cached.terrain = makeTerrain(0);
    cached.gate = mining.gate;
    cached.gate.activeCocoonLayer = 1;
    cached.gate.cocoonLayers[0].remaining = 0;
    cached.gate.cocoonLayers[0].completed = true;
    cached.gate.cocoonLayers[1].remaining = 3;
    cached.gate.cocoonLayers[1].revealed = true;
    MiningCell* cachedTagged = miningCellAt(cached.terrain, 3, 3);
    require(cachedTagged != nullptr, "cached cocoon cell should exist");
    cachedTagged->material = MiningCellMaterial::CommonOre;
    cachedTagged->gateAssociated = true;
    cachedTagged->cocoonLayer = 1;
    cachedTagged->revealed = true;
    mining.depthLayers = {cached};
    state.screen = Screen::Mining;

    const SaveData captured = captureSaveData(state);
    require(captured.version == save_schema::currentVersion, "new saves should use the current schema version");
    const std::optional<SaveData> parsed =
        deserializeSaveData(serializeSaveData(captured));
    require(parsed.has_value(), "current scenario and cocoon state should deserialize");

    GameState restored = createNewGame(catalog, 0x900A);
    restoreSaveData(restored, catalog, *parsed);
    const ScenarioInstance* restoredGenerated =
        findScenarioInstance(restored.meta, generated.id);
    require(
        restoredGenerated != nullptr &&
            restoredGenerated->definitionId == generated.definitionId &&
            restoredGenerated->definitionVersion == generated.definitionVersion &&
            restoredGenerated->source == ScenarioSource::Procedural &&
            restoredGenerated->factoryId == generated.factoryId &&
            restoredGenerated->factoryVersion == generated.factoryVersion &&
            restoredGenerated->seed == generated.seed &&
            restoredGenerated->resolvedParameters == generated.resolvedParameters &&
            restoredGenerated->awardedRewardIds == generated.awardedRewardIds &&
            restoredGenerated->completed,
        "a procedural scenario instance should round trip without collapsing its runtime ID into its definition ID");
    const ScenarioStepProgress* restoredGeneratedStep =
        restoredGenerated == nullptr
        ? nullptr
        : findScenarioStepProgress(*restoredGenerated, generatedStep.id);
    require(
        restoredGeneratedStep != nullptr &&
            restoredGeneratedStep->progress == generatedStep.progress &&
            restoredGeneratedStep->briefingAcknowledged &&
            restoredGeneratedStep->completed &&
            restoredGeneratedStep->claimed &&
            restoredGeneratedStep->failureSeen &&
            restoredGeneratedStep->failureAcknowledged,
        "scenario step progress and first-failure acknowledgement should round trip");
    require(
        restored.meta.miningSites.size() == 2 &&
            restored.meta.miningSites[0].siteId == "roundtrip_site" &&
            restored.meta.miningSites[0].artifactId.empty() &&
            !restored.meta.miningSites[0].legacyMigrated &&
            restored.meta.miningSites[1].siteId ==
                "roundtrip_legacy_site" &&
            restored.meta.miningSites[1].legacyMigrated,
        "generic and compatibility-tagged mining-site progress should retain identity and provenance");
    require(
        restored.run.surfaceExpedition.pendingScenarioId ==
                content::scenario::volcanicDescent &&
            restored.run.surfaceExpedition.pendingScenarioStepId == "recovery" &&
            restored.run.surfaceExpedition.pendingMiningSiteDefinitionId ==
                content::miningSite::thermalLayeredRecovery,
        "a staged scenario mining launch should survive reload");
    require(
        restored.run.mining.scenarioId ==
                content::scenario::volcanicDescent &&
            restored.run.mining.scenarioStepId == "recovery" &&
            restored.run.mining.miningSiteDefinitionId ==
                content::miningSite::thermalLayeredRecovery &&
            restored.run.mining.miningSiteBiome ==
                MiningSiteBiome::ThermalLava &&
            std::abs(
                restored.run.mining.siteBaselineOxygenSeconds -
                60.0) < 0.0001,
        "active mining should retain its generic scenario, site, biome, and oxygen context");
    const MiningCell* restoredActiveTagged =
        miningCellAt(restored.run.mining.terrain, 2, 2);
    require(
        restored.run.mining.gate.cocoonDefinitionId ==
                "roundtrip_cocoon" &&
            restored.run.mining.gate.cocoonDefinitionVersion == 4 &&
            restored.run.mining.gate.protectedObjective.id ==
                "roundtrip_payload",
        "active cocoon definition and protected-objective identity should round trip");
    require(
        restored.run.mining.gate.cocoonLayers.size() == 2 &&
            restored.run.mining.gate.cocoonLayers[1].completionRule ==
                MiningCocoonCompletionRule::TreatOnly &&
            restored.run.mining.gate.cocoonLayers[1].revealPolicy ==
                MiningCocoonRevealPolicy::AfterPreviousLayerCompleted,
        "active cocoon layer progress, completion rules, and reveal policies should round trip");
    require(
        restoredActiveTagged != nullptr &&
            restoredActiveTagged->cocoonLayer == 0,
        "active cocoon cell tags should round trip");
    require(
        restored.run.mining.depthLayers.size() == 1 &&
            restored.run.mining.depthLayers[0].gate.activeCocoonLayer == 1 &&
            restored.run.mining.depthLayers[0].gate.cocoonLayers.size() == 2,
        "cached depth layers should retain independent cocoon progress");
    const MiningCell* restoredCachedTagged =
        restored.run.mining.depthLayers.empty()
        ? nullptr
        : miningCellAt(
              restored.run.mining.depthLayers[0].terrain,
              3,
              3);
    require(
        restoredCachedTagged != nullptr &&
            restoredCachedTagged->cocoonLayer == 1 &&
            restoredCachedTagged->revealed,
        "cached-depth cocoon tags and visibility should round trip");

}

void proceduralScenarioTemplatesStayDormantUntilInstanced()
{
    ContentCatalog catalog = createDefaultContent();
    Destination routeFixture;
    routeFixture.id = "procedural_route_destination";
    routeFixture.name = "Procedural Route Fixture";
    routeFixture.tier = 2;
    routeFixture.routeRequirementKeys = {"procedural_route_key"};
    catalog.destinations.push_back(std::move(routeFixture));
    std::string validationError;
    require(
        validateScenarioCatalog(catalog, &validationError),
        "the default catalog should validate its non-default procedural template");
    require(
        makeProceduralScenarioInstance(
            catalog,
            "generated_mining",
            0xBAD,
            {"step.delivery.unsupported=1"}).id.empty(),
        "a factory should reject malformed resolved parameters before they can enter a save");
    require(
        makeProceduralScenarioInstance(
            catalog,
            "generated_mining",
            0xBAD + 1,
            {"step.delivery.reward_count=1",
             "step.delivery.reward.0.kind=inventory_resources"}).id.empty(),
        "a factory should reject an inventory reward with no positive material grant");
    require(
        makeProceduralScenarioInstance(
            catalog,
            "generated_mining",
            0xBAD + 2,
            {"step.delivery.reward_count=1",
             "step.delivery.reward.0.kind=route_access",
             "step.delivery.reward.0.id=unknown_route"}).id.empty(),
        "a factory should reject route access that does not resolve to catalog destination requirements");

    GameState state = createNewGame(catalog, 0x515C);
    require(
        findScenarioInstance(state.meta, content::scenario::generatedTemplate) == nullptr,
        "a factory template should not appear as a live starter scenario");

    ScenarioDefinition* templateDefinition = nullptr;
    for (ScenarioDefinition& definition : catalog.scenarios) {
        if (definition.id == content::scenario::generatedTemplate) {
            templateDefinition = &definition;
            break;
        }
    }
    require(templateDefinition != nullptr, "the procedural factory template should be authored in the catalog");

    ScenarioInstance instance = makeProceduralScenarioInstance(
        catalog,
        "generated_mining",
        0xDECAFBAD,
        {
            "destination=procedural_fixture_destination",
            "step.delivery.required_progress=7",
            "step.delivery.reward_count=2",
            "step.delivery.reward.0.kind=inventory_resources",
            "step.delivery.reward.0.materials.common=3",
            "step.delivery.reward.0.materials.rare=1",
            "step.delivery.reward.1.kind=route_access",
            "step.delivery.reward.1.id=procedural_route_destination"
        });
    require(
        !instance.id.empty() && instance.definitionId == content::scenario::generatedTemplate &&
            instance.source == ScenarioSource::Procedural,
        "a factory should create a concrete procedural scenario instance");
    const std::string instanceId = instance.id;
    state.meta.scenarios.push_back(std::move(instance));
    ensureScenarioInstances(state, catalog);
    require(
        findScenarioInstance(state.meta, instanceId) != nullptr,
        "scenario initialization must preserve a concrete procedural instance");
    const ScenarioInstance* persisted = findScenarioInstance(state.meta, instanceId);
    require(persisted != nullptr, "the procedural fixture should retain its resolved values");
    const ScenarioDefinition resolvedDelivery = resolveScenarioDefinition(*templateDefinition, *persisted);
    const ScenarioStepDefinition* resolvedDeliveryStep =
        findScenarioStepDefinition(resolvedDelivery, "delivery");
    require(
            resolvedDelivery.destinationId == "procedural_fixture_destination" &&
            resolvedDeliveryStep != nullptr && resolvedDeliveryStep->requiredProgress == 7 &&
            resolvedDeliveryStep->rewards.size() == 2 &&
            resolvedDeliveryStep->rewards[0].kind == ScenarioRewardKind::InventoryResources &&
            resolvedDeliveryStep->rewards[0].materials.common == 3 &&
            resolvedDeliveryStep->rewards[0].materials.rare == 1 &&
            resolvedDeliveryStep->rewards[1].kind == ScenarioRewardKind::RouteAccess &&
            resolvedDeliveryStep->rewards[1].id == "procedural_route_destination",
        "a factory should persist and materialize typed procedural destination, material, and route reward parameters");
    const ScenarioObjectivePresentation presentation = scenarioObjectiveForDestination(
        state,
        catalog,
        "procedural_fixture_destination");
    require(
        presentation.available && presentation.scenarioId == instanceId && presentation.stepId == "delivery" &&
            presentation.action == ScenarioActionKind::None,
        "a procedural passive delivery objective should route by instance without exposing its claim action early");
    require(
        !performScenarioAction(
             state,
             catalog,
             instanceId,
             "delivery",
             ScenarioActionKind::BeginActivity).applied,
        "the generic dispatcher should reject starting a passive delivery objective as an activity");
    const Destination* routeDestination = catalog.findDestination("procedural_route_destination");
    require(routeDestination != nullptr, "the procedural route reward fixture should exist in the catalog");
    const ScenarioRouteRequirementStatus routeBeforeClaim =
        scenarioRouteRequirementStatus(state, catalog, *routeDestination);
    require(
        !routeBeforeClaim.satisfied && routeBeforeClaim.scenarioId == instanceId &&
            routeBeforeClaim.stepId == "delivery",
        "route evaluation should identify a generic RouteAccess reward before it is claimed");
    ScenarioInstance unrelatedInstance = makeProceduralScenarioInstance(
        catalog,
        "generated_mining",
        0xDECAFBAD + 1,
        {"destination=procedural_fixture_destination"});
    require(!unrelatedInstance.id.empty(), "a second factory result should be independently addressable");
    const std::string unrelatedId = unrelatedInstance.id;
    state.meta.scenarios.push_back(std::move(unrelatedInstance));
    require(
        recordScenarioEvent(
            state,
            catalog,
            {ScenarioEventKind::SafeMaterialDelivered, instanceId, "delivery",
             "procedural_fixture_destination", "common", 7, 0}) &&
            scenarioStepState(state, catalog, instanceId, "delivery") == ScenarioStepState::ReadyToClaim,
        "an arbitrary authored destination should use the same procedural safe-delivery event path");
    require(
        scenarioStepState(state, catalog, unrelatedId, "delivery") == ScenarioStepState::Active,
        "an event addressed to one procedural runtime instance must not advance another instance of its template");
    require(
        performScenarioAction(
            state,
            catalog,
            instanceId,
            "delivery",
            ScenarioActionKind::ClaimReward).applied &&
            state.meta.materials.common == 3 && state.meta.materials.rare == 1 &&
            hasUnlock(state.meta, "procedural_route_key") &&
            scenarioRouteRequirementStatus(state, catalog, *routeDestination).satisfied,
        "a procedurally resolved material and route reward should be claimed exactly through the shared scenario dispatcher");
    const auto proceduralSave = deserializeSaveData(serializeSaveData(captureSaveData(state)));
    require(proceduralSave.has_value(), "a procedural scenario instance should serialize");
    GameState restoredProcedural = createNewGame(catalog, 0x515D);
    restoreSaveData(restoredProcedural, catalog, *proceduralSave);
    const ScenarioInstance* restoredInstance =
        findScenarioInstance(restoredProcedural.meta, instanceId);
    require(
        restoredInstance != nullptr && restoredInstance->source == ScenarioSource::Procedural &&
            restoredInstance->factoryId == "generated_mining" &&
            restoredInstance->resolvedParameters == persisted->resolvedParameters &&
            resolveScenarioDefinition(*templateDefinition, *restoredInstance).destinationId ==
                "procedural_fixture_destination",
        "a procedural factory instance should reload its resolved values without rerolling");
    state = std::move(restoredProcedural);

    templateDefinition->steps.push_back({
        "flyby",
        {},
        "PROCEDURAL FIXTURE",
        "Perfect Pass",
        "Complete a configured Flyby challenge.",
        "REWARD // CONFIGURED BY FACTORY",
        "Begin Challenge",
        {},
        ScenarioEventKind::FlybyFinished,
        {},
        {},
        1,
        static_cast<int>(FlybyGrade::Perfect),
        false,
        false,
        true,
        ScenarioActionKind::BeginActivity,
        {},
        {}
    });
    ScenarioInstance* procedural = findScenarioInstance(state.meta, instanceId);
    require(procedural != nullptr, "the procedural Flyby fixture should retain its runtime instance");
    procedural->steps.push_back({"flyby"});
    procedural->resolvedParameters.push_back("destination=moon");
    state.run.destinationIndex = 1;
    state.meta.furthestTier = 1;
    require(
        startScenarioFlybyRun(state, catalog, instanceId, "flyby") &&
            state.run.flyby.purpose == FlybyPurpose::ScenarioChallenge &&
            state.run.flyby.scenarioId == instanceId && state.run.flyby.scenarioStepId == "flyby",
        "a generic Flyby challenge should start from a procedural runtime scenario ID");
    state.run.flyby.completed = true;
    state.run.flyby.result = FlybyGrade::Perfect;
    completeFlybyRun(state, catalog);
    require(
        scenarioStepState(state, catalog, instanceId, "flyby") == ScenarioStepState::Complete,
        "a completed procedural Flyby should record its result against the runtime scenario instance");

    ScenarioInstance obsoleteTemplate;
    obsoleteTemplate.id = content::scenario::generatedTemplate;
    obsoleteTemplate.definitionId = content::scenario::generatedTemplate;
    obsoleteTemplate.source = ScenarioSource::Authored;
    state.meta.scenarios.push_back(std::move(obsoleteTemplate));
    ensureScenarioInstances(state, catalog);
    require(
        findScenarioInstance(state.meta, content::scenario::generatedTemplate) == nullptr,
        "initialization should remove the obsolete auto-instantiated template from development saves");

    templateDefinition->instantiateByDefault = true;
    validationError.clear();
    require(
        !validateScenarioCatalog(catalog, &validationError) && !validationError.empty(),
        "a scenario factory must reject a template that would auto-instantiate as a live contract");
}



void surfaceSiteProfilesChangeExpeditionRules()
{
    const ContentCatalog catalog = createDefaultContent();

    GameState survey = createNewGame(catalog, 1);
    survey.run.destinationIndex = 2;
    startSurfaceExpedition(survey, catalog);
    require(survey.run.surfaceExpedition.siteProfile == SurfaceSiteProfile::SurveyBasin, "seeded fallback should generate survey basin profile");
    Random surveyRng(1001);
    const SurfaceActionOutcome surveyOutcome = surveySurfaceSite(survey, surveyRng);
    require(surveyOutcome.materialDelta.common >= tuning::research::surveyCommonGain + tuning::research::siteSurveyBasinSurveyBonus, "survey basin should improve survey returns");

    GameState ore = createNewGame(catalog, 2);
    ore.run.destinationIndex = 2;
    startSurfaceExpedition(ore, catalog);
    require(ore.run.surfaceExpedition.siteProfile == SurfaceSiteProfile::OreShelf, "seeded fallback should generate ore shelf profile");
    Random oreRng(1002);
    const SurfaceActionOutcome mineOutcome = mineSurfaceDeposit(ore, oreRng);
    require(mineOutcome.materialDelta.common >= tuning::research::mineCommonGain + tuning::research::siteOreShelfMineBonus, "ore shelf should improve mining returns");

    GameState fracture = createNewGame(catalog, 3);
    fracture.run.destinationIndex = 2;
    startSurfaceExpedition(fracture, catalog);
    require(fracture.run.surfaceExpedition.siteProfile == SurfaceSiteProfile::FractureField, "seeded fallback should generate fracture field profile");
    require(fracture.run.surfaceExpedition.siteProfile == SurfaceSiteProfile::FractureField, "fracture field remains a distinct higher-hazard site profile");
}

void surfaceHazardsCreateEnvironmentalSetbacks()
{
    const ContentCatalog catalog = createDefaultContent();

    auto triggerSurveyHazard = [&catalog]() {
        for (int seed = 1; seed < 400; ++seed) {
            GameState state = createNewGame(catalog, seed);
            state.run.destinationIndex = 2;
            startSurfaceExpedition(state, catalog);
            state.run.surfaceExpedition.siteProfile = SurfaceSiteProfile::SurveyBasin;
            state.run.surfaceExpedition.hazard = 10.0;
            const int supplyBefore = state.run.surfaceExpedition.supply;
            Random rng(seed);
            const SurfaceActionOutcome outcome = surveySurfaceSite(state, rng);
            if (outcome.hazardTriggered) {
                require(outcome.hazardDelta > 0.0, "survey hazard should raise site hazard");
                require(outcome.supplyDelta == -(tuning::research::surveySupplyCost + tuning::research::dustHazardSupplyLoss), "survey hazard should spend extra supply when possible");
                require(state.run.surfaceExpedition.supply == supplyBefore + outcome.supplyDelta, "survey hazard supply delta should match expedition state");
                return true;
            }
        }
        return false;
    };

    auto triggerMineHazard = [&catalog]() {
        for (int seed = 1; seed < 400; ++seed) {
            GameState state = createNewGame(catalog, seed);
            state.run.destinationIndex = 2;
            startSurfaceExpedition(state, catalog);
            state.run.surfaceExpedition.siteProfile = SurfaceSiteProfile::SurveyBasin;
            state.run.surfaceExpedition.hazard = 10.0;
            Random rng(seed);
            const SurfaceActionOutcome outcome = mineSurfaceDeposit(state, rng);
            if (outcome.hazardTriggered) {
                require(outcome.cargoDelta == 3, "mine hazard should reduce the net cargo delta");
                return true;
            }
        }
        return false;
    };

    auto triggerPushHazard = [&catalog]() {
        for (int seed = 1; seed < 400; ++seed) {
            GameState state = createNewGame(catalog, seed);
            state.run.destinationIndex = 2;
            startSurfaceExpedition(state, catalog);
            state.run.surfaceExpedition.siteProfile = SurfaceSiteProfile::SurveyBasin;
            state.run.surfaceExpedition.hazard = 10.0;
            Random rng(seed);
            const SurfaceActionOutcome outcome = pushSurfaceDeeper(state, rng);
            if (outcome.hazardTriggered) {
                require(outcome.hazardDelta == tuning::research::unstableTerrainHazardIncrease, "push hazard should report the extra terrain hazard");
                return true;
            }
        }
        return false;
    };

    require(triggerSurveyHazard(), "high-hazard surveys should be able to trigger environmental setbacks");
    require(triggerMineHazard(), "high-hazard mining should be able to trigger environmental setbacks");
    require(triggerPushHazard(), "high-hazard deeper pushes should be able to trigger environmental setbacks");
}

void surfaceEventsCreateSmallRunVariation()
{
    const ContentCatalog catalog = createDefaultContent();

    auto triggerEvent = [&catalog](SurfaceEventType expected) {
        for (int seed = 1; seed < 8000; ++seed) {
            GameState state = createNewGame(catalog, seed);
            state.run.destinationIndex = 2;
            startSurfaceExpedition(state, catalog);
            state.run.surfaceExpedition.siteProfile = SurfaceSiteProfile::SurveyBasin;
            state.run.surfaceExpedition.hazard = 0.0;
            const int supplyBefore = state.run.surfaceExpedition.supply;
            const int blueprintsBefore = state.meta.blueprintProgress;
            Random rng(seed);
            const SurfaceActionOutcome outcome = surveySurfaceSite(state, rng);
            if (outcome.eventType != expected) {
                continue;
            }

            require(!outcome.hazardTriggered, "surface events should not stack on top of hazards");
            if (expected == SurfaceEventType::EquipmentFailure) {
                require(state.run.surfaceExpedition.supply == supplyBefore + outcome.supplyDelta, "equipment event supply delta should match expedition state");
                require(outcome.supplyDelta == -(tuning::research::surveySupplyCost + tuning::research::surfaceEquipmentFailureSupplyLoss), "equipment event should consume spare supply");
            } else if (expected == SurfaceEventType::UnexpectedDeposit) {
                require(outcome.materialDelta.common >= tuning::research::surveyCommonGain + tuning::research::siteSurveyBasinSurveyBonus + tuning::research::surfaceDepositCommonGain, "deposit event should add material yield");
            } else if (expected == SurfaceEventType::CrewDiscovery) {
                require(outcome.blueprintDelta == tuning::research::surfaceCrewDiscoveryBlueprintGain, "crew discovery should report blueprint gain");
                require(state.meta.blueprintProgress == blueprintsBefore + outcome.blueprintDelta, "crew discovery should bank blueprint progress");
            }
            return true;
        }
        return false;
    };

    require(triggerEvent(SurfaceEventType::EquipmentFailure), "surface actions should sometimes trigger equipment failure events");
    require(triggerEvent(SurfaceEventType::UnexpectedDeposit), "surface actions should sometimes trigger unexpected deposit events");
    require(triggerEvent(SurfaceEventType::CrewDiscovery), "surface actions should sometimes trigger crew discovery events");
}

void enemyContactStartsBeyondSolarSystemAndCanBeMitigated()
{
    const ContentCatalog catalog = createDefaultContent();

    GameState mars = createNewGame(catalog, 1201);
    mars.run.destinationIndex = 2;
    startSurfaceExpedition(mars, catalog);
    require(!mars.run.surfaceExpedition.enemyEncountersEnabled, "Mars expedition should not enable enemy contact");
    require(surfaceEnemyEncounterChance(mars) == 0.0, "solar-system expeditions should have no contact risk");

    GameState nearbyStar = createNewGame(catalog, 1202);
    nearbyStar.run.destinationIndex = 4;
    nearbyStar.meta.chapter = GameChapter::Arkfall;
    nearbyStar.meta.unlockKeys.push_back(content::unlock::deepSpace);
    startSurfaceExpedition(nearbyStar, catalog);
    nearbyStar.run.surfaceExpedition.siteProfile = SurfaceSiteProfile::SurveyBasin;
    nearbyStar.run.surfaceExpedition.hazard = 0.0;
    nearbyStar.run.surfaceExpedition.supply = 20;
    const double baselineRisk = surfaceEnemyEncounterChance(nearbyStar);
    require(nearbyStar.run.surfaceExpedition.enemyEncountersEnabled, "Nearby Star expedition should enable enemy contact");
    require(baselineRisk > 0.0, "Nearby Star expedition should expose contact risk");

    GameState defended = nearbyStar;
    defended.meta.unlockKeys.push_back(content::unlock::perimeterDrones);
    require(surfaceEnemyEncounterChance(defended) < baselineRisk, "perimeter drones should reduce enemy contact risk");

    auto triggerEnemyContact = [&catalog]() {
        for (int seed = 1; seed < 8000; ++seed) {
            GameState state = createNewGame(catalog, seed);
            state.run.destinationIndex = 4;
            state.meta.chapter = GameChapter::Arkfall;
            state.meta.unlockKeys.push_back(content::unlock::deepSpace);
            startSurfaceExpedition(state, catalog);
            state.run.surfaceExpedition.siteProfile = SurfaceSiteProfile::SurveyBasin;
            state.run.surfaceExpedition.hazard = 0.0;
            state.run.surfaceExpedition.supply = 20;
            const int supplyBefore = state.run.surfaceExpedition.supply;
            Random rng(seed);
            const SurfaceActionOutcome outcome = surveySurfaceSite(state, rng);
            if (outcome.eventType != SurfaceEventType::EnemyContact) {
                continue;
            }

            require(outcome.enemyEncounter, "enemy contact event should set the encounter flag");
            require(state.run.surfaceExpedition.supply == supplyBefore + outcome.supplyDelta, "enemy contact supply delta should match expedition state");
            require(outcome.supplyDelta == -(tuning::research::surveySupplyCost + tuning::research::surfaceEnemySupplyLoss), "enemy contact should consume supply in addition to the action");
            const MiningArenaRules contactRules = resolveMiningArenaRules({MiningAct::ActTwo, 1, 0});
            require(std::abs(outcome.hazardDelta - tuning::research::surfaceEnemyHazardIncrease * std::max(1.0, contactRules.enemyDamageScale)) < 0.000001,
                "enemy contact hazard should match the upcoming arena's threat scale");
            return true;
        }
        return false;
    };

    require(triggerEnemyContact(), "post-solar expeditions should sometimes trigger enemy contact events");
}

void surfaceExpeditionBanksMaterialsAndDefersEnemies()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 707);
    state.run.destinationIndex = 2;
    Random rng(707);

    startSurfaceExpedition(state, catalog);
    require(state.run.surfaceExpedition.active, "Mars surface expedition should start");
    require(!state.run.surfaceExpedition.enemyEncountersEnabled, "solar-system expeditions should not enable enemies");

    const SurfaceActionOutcome survey = surveySurfaceSite(state, rng);
    const SurfaceActionOutcome mine = mineSurfaceDeposit(state, rng);
    const SurfaceActionOutcome push = pushSurfaceDeeper(state, rng);
    require(survey.applied && mine.applied && push.applied, "surface actions should consume supply while active");
    require(state.run.surfaceExpedition.cargo > 0, "surface actions should build a return cargo payload");

    const SurfaceActionOutcome extraction = extractSurfacePayload(state);
    require(extraction.applied, "surface extraction should resolve");
    require(!state.run.surfaceExpedition.active, "extraction should end the active surface expedition");
    require(state.meta.materials.common > 0, "extraction should bank at least partial material progress");
    require(extraction.cargoRecovered, "normal surface return should recover all Ship cargo deterministically");

    GameState deepSpace = createNewGame(catalog, 808);
    deepSpace.run.destinationIndex = 4;
    startSurfaceExpedition(deepSpace, catalog);
    require(deepSpace.run.surfaceExpedition.enemyEncountersEnabled, "enemy encounters should wait until the Nearby Star tier");
}

void surfaceExpeditionRoundTripsThroughSave()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 9090);
    state.run.destinationIndex = 2;
    state.screen = Screen::SurfaceExpedition;
    startSurfaceExpedition(state, catalog);
    state.run.surfaceExpedition.rigFuel = 2.0;
    state.run.surfaceExpedition.prospectMaterials = {.common = 1, .rare = 2, .exotic = 1};
    state.run.surfaceExpedition.prospectArtifacts = 1;
    state.run.surfaceExpedition.depthProspects.push_back({1, 1, {.common = 0, .rare = 1, .exotic = 1}, 1});
    Random rng(9091);
    require(surveySurfaceSite(state, rng).applied, "test setup should gather a surface payload");

    const std::string text = serializeSaveData(captureSaveData(state));
    const auto save = deserializeSaveData(text);
    require(save.has_value(), "surface expedition save should parse");

    GameState restored = createNewGame(catalog, 1);
    restoreSaveData(restored, catalog, *save);

    require(restored.screen == Screen::SurfaceExpedition, "active surface expedition screen should round trip");
    require(restored.run.surfaceExpedition.active, "active surface expedition state should round trip");
    require(restored.run.surfaceExpedition.destinationId == content::destination::mars, "surface destination should round trip");
    require(restored.run.surfaceExpedition.siteProfile == state.run.surfaceExpedition.siteProfile, "surface site profile should round trip");
    require(nearlyEqual(restored.run.surfaceExpedition.rigFuel, 2.0), "surface rig fuel should round trip");
    require(nearlyEqual(restored.run.surfaceExpedition.rigFuelCapacity, state.run.surfaceExpedition.rigFuelCapacity), "surface rig fuel capacity should round trip");
    require(restored.run.surfaceExpedition.miningSitePrepared, "surface mining preparation should round trip");
    require(!restored.run.surfaceExpedition.miningRunUsed, "unused surface mining run should round trip");
    require(restored.run.surfaceExpedition.temporaryMaterials.common == state.run.surfaceExpedition.temporaryMaterials.common, "temporary surface materials should round trip");
    require(restored.run.surfaceExpedition.prospectMaterials.rare == 2, "surface material prospects should round trip");
    require(restored.run.surfaceExpedition.prospectArtifacts == 1, "surface artifact prospects should round trip");
    require(restored.run.surfaceExpedition.depthProspects.size() == 1, "surface depth prospects should round trip");
    require(restored.run.surfaceExpedition.depthProspects.front().absoluteDepth == 1, "surface depth prospect layer should round trip");
    require(restored.run.surfaceExpedition.depthProspects.front().possibleMaterials.exotic == 1, "surface depth prospect materials should round trip");
    require(restored.run.surfaceExpedition.depthProspects.front().possibleArtifacts == 1, "surface depth prospect artifacts should round trip");
    require(restored.run.surfaceExpedition.logEntries == state.run.surfaceExpedition.logEntries, "surface mission log should round trip");
}

void surfaceMiningUsesRigFuelAndRunsOnce()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 92928);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);

    const int supplyBeforeMining = state.run.surfaceExpedition.supply;
    const double fuelBeforeMining = state.run.surfaceExpedition.rigFuel;
    const SurfaceActionOutcome started = startMiningRun(state, catalog);
    require(started.applied, "fresh surface mining should start when rig fuel is available");
    require(state.run.surfaceExpedition.supply == supplyBeforeMining, "starting mining should not spend action kits");
    require(nearlyEqual(state.run.surfaceExpedition.rigFuel, fuelBeforeMining - 1.0), "starting mining should spend one rig fuel");
    require(state.run.surfaceExpedition.miningRunUsed, "starting mining should consume the one mining run for this loop");

    state.screen = Screen::SurfaceExpedition;
    state.run.mining.active = false;
    const int supplyAfterMining = state.run.surfaceExpedition.supply;
    const int depthAfterMining = state.run.surfaceExpedition.depth;
    Random rng(92928);
    const SurfaceActionOutcome pushAfterMining = pushSurfaceDeeper(state, rng);
    require(!pushAfterMining.applied, "pushing deeper should be blocked after the mining run is used");
    require(state.run.surfaceExpedition.supply == supplyAfterMining, "blocked post-mining push should not spend action kits");
    require(state.run.surfaceExpedition.depth == depthAfterMining, "blocked post-mining push should not increase depth");

    const SurfaceActionOutcome surveyAfterMining = surveySurfaceSite(state, rng);
    require(!surveyAfterMining.applied, "surveying should be blocked after the mining run is used");
    require(state.run.surfaceExpedition.supply == supplyAfterMining, "blocked post-mining survey should not spend action kits");

    const SurfaceActionOutcome scanAfterMining = startSurfaceScanRun(state, rng);
    require(!scanAfterMining.applied, "surface scan should be blocked after the mining run is used");
    require(state.run.surfaceExpedition.supply == supplyAfterMining, "blocked post-mining scan should not spend action kits");

    const SurfaceActionOutcome repeated = startMiningRun(state, catalog);
    require(!repeated.applied, "mining should only start once per surface loop");
}

void physicalMiningArtifactsAreSingleAndDeliveryGated()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 92929);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    state.run.surfaceExpedition.prospectArtifacts = 3;

    require(
        startMiningRun(state, catalog, {MiningAct::ActOne, 8, 92929, true, MiningGateType::None}, true).applied,
        "prospected artifact should start a mining run at an artifact-enabled tier");
    require(state.run.mining.artifact.present, "prospected artifact should create one physical artifact object");
    int artifactTiles = 0;
    for (const MiningCell& cell : state.run.mining.terrain.cells) {
        artifactTiles += cell.material == MiningCellMaterial::ArtifactCache ? 1 : 0;
    }
    require(artifactTiles <= 1, "mining terrain should contain at most one artifact cache tile");
    require(state.run.mining.temporaryArtifacts.empty(), "artifact should not enter payload before delivery");

    MiningArtifactObject& artifact = state.run.mining.artifact;
    artifact.revealed = true;
    state.run.mining.droneX = artifact.x;
    state.run.mining.droneY = artifact.y - 2.0;
    state.run.mining.aimDirX = 0.0;
    state.run.mining.aimDirY = 1.0;
    state.run.mining.drilling = true;
    const double healthBeforeDrill = artifact.health;
    updateMiningRun(state, catalog, 0.08);
    require(state.run.mining.artifact.health < healthBeforeDrill, "drilling an artifact cache should damage the artifact");
    require(state.run.mining.temporaryArtifacts.empty(), "drilling should still not recover the artifact directly");

    state.run.mining.drilling = false;
    state.run.mining.artifact.state = MiningArtifactState::Loose;
    state.run.mining.artifact.tethered = true;
    state.run.mining.artifact.x = state.run.mining.returnZoneX;
    state.run.mining.artifact.y = state.run.mining.returnZoneY;
    state.run.mining.droneX = state.run.mining.artifact.x;
    state.run.mining.droneY = state.run.mining.artifact.y;
    updateMiningRun(state, catalog, 0.08);
    require(state.run.mining.artifact.state == MiningArtifactState::Delivered, "tethered artifact should deliver at the ship zone");
    require(state.run.mining.temporaryArtifacts.empty(), "delivered artifact should not stay carried after auto-bank");
    require(state.run.mining.stowedArtifacts.size() == 1, "delivered artifact should bank at the ship");
    require(state.run.mining.stowedCargo >= tuning::mining::artifactCargo, "delivered artifact should add banked cargo weight");
}

void miningArtifactTetherAndDestructionRules()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 92930);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    state.run.surfaceExpedition.prospectArtifacts = 1;
    require(
        startMiningRun(state, catalog, {MiningAct::ActOne, 8, 92930, true, MiningGateType::None}, true).applied,
        "artifact tether test should start mining at an artifact-enabled tier");

    MiningArtifactObject& artifact = state.run.mining.artifact;
    artifact.revealed = true;
    state.run.mining.droneX = artifact.x;
    state.run.mining.droneY = artifact.y - 1.0;
    toggleMiningTether(state);
    require(state.run.mining.artifact.tethered, "T should attach to a nearby exposed artifact");
    toggleMiningTether(state);
    require(!state.run.mining.artifact.tethered, "T should detach an attached artifact");

    state.run.mining.droneX = 24.0;
    state.run.mining.droneY = 10.0;
    state.run.mining.artifact.x = state.run.mining.droneX + 5.0;
    state.run.mining.artifact.y = state.run.mining.droneY;
    toggleMiningTether(state);
    require(state.run.mining.artifact.tethered, "the doubled tether should attach to an exposed artifact five cells away");
    require(
        std::abs(tuning::mining::artifactTetherRangeCells - 6.8) < 0.000001 &&
            std::abs(tuning::mining::artifactTetherRestLengthCells - 1.70) < 0.000001,
        "artifact tether tuning should double both reach and trailing length");
    for (MiningCell& cell : state.run.mining.terrain.cells) {
        cell = {MiningCellMaterial::Empty, 0.0, 0.0, true, false};
    }
    state.run.mining.artifact.state = MiningArtifactState::Loose;
    state.run.mining.artifact.x = state.run.mining.droneX + 1.5;
    state.run.mining.artifact.velocityX = 0.0;
    state.run.mining.artifact.velocityY = 0.0;
    const double slackArtifactX = state.run.mining.artifact.x;
    updateMiningRun(state, catalog, 0.08);
    require(
        std::abs(state.run.mining.artifact.x - slackArtifactX) < 0.000001,
        "the longer tether should leave a nearby loose artifact trailing in its slack envelope");
    state.run.mining.artifact.x = state.run.mining.droneX + 2.5;
    state.run.mining.artifact.velocityX = 0.0;
    updateMiningRun(state, catalog, 0.08);
    require(state.run.mining.artifact.velocityX < 0.0, "the longer tether should pull once an artifact moves beyond its trailing length");

    state.run.mining.artifact.state = MiningArtifactState::Destroyed;
    state.run.mining.artifact.tethered = true;
    state.run.mining.artifact.x = state.run.mining.returnZoneX;
    state.run.mining.artifact.y = state.run.mining.returnZoneY;
    updateMiningRun(state, catalog, 0.08);
    require(state.run.mining.temporaryArtifacts.empty(), "destroyed artifact should not deliver");
}

void miningArtifactRewardsResolveOnExtraction()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState story = createNewGame(catalog, 92931);
    story.run.destinationIndex = 4;
    startSurfaceExpedition(story, catalog);
    story.run.surfaceExpedition.hazard = 0.0;
    story.run.surfaceExpedition.cargo = 0;
    story.run.surfaceExpedition.supply = 10;
    story.run.surfaceExpedition.rigFuel = story.run.surfaceExpedition.rigFuelCapacity;
    story.run.surfaceExpedition.temporaryArtifacts.push_back({"story_artifact", content::destination::nearbyStar, false, ArtifactKind::Story, ArtifactRewardType::None, 1.0, false});
    story.meta.ark.condition = ArkCondition::DamagedStranded;
    story.meta.ark.hullDamage = 72;
    Random storyRng(1);
    const SurfaceActionOutcome storyOutcome = extractSurfacePayload(story);
    require(storyOutcome.cargoRecovered, "story artifact test should recover cargo");
    require(story.meta.ark.repairProgress == tuning::mining::artifactStoryArkRepair, "story artifact should add Ark repair progress");
    require(story.meta.ark.hullDamage == 72 - tuning::mining::artifactStoryHullRepair, "story artifact should repair Ark hull damage");
    require(!story.meta.artifacts.empty() && story.meta.artifacts.front().rewardApplied, "story artifact reward should be marked applied");

    GameState boost = createNewGame(catalog, 92932);
    boost.run.destinationIndex = 2;
    startSurfaceExpedition(boost, catalog);
    boost.run.surfaceExpedition.hazard = 0.0;
    boost.run.surfaceExpedition.cargo = 0;
    boost.run.surfaceExpedition.supply = 10;
    boost.run.surfaceExpedition.rigFuel = boost.run.surfaceExpedition.rigFuelCapacity;
    boost.run.surfaceExpedition.temporaryArtifacts.push_back({"boost_artifact", content::destination::mars, false, ArtifactKind::Boost, ArtifactRewardType::Credits, 1.0, false});
    const double creditsBefore = boost.run.credits;
    Random boostRng(2);
    const SurfaceActionOutcome boostOutcome = extractSurfacePayload(boost);
    require(boostOutcome.cargoRecovered, "boost artifact test should recover cargo");
    require(boost.run.credits > creditsBefore, "credit artifact should grant credits after extraction");
    require(!boost.meta.artifacts.empty() && boost.meta.artifacts.front().rewardApplied, "boost artifact reward should be marked applied");
}

void miningArtifactSaveRoundTrips()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 92933);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    state.run.surfaceExpedition.prospectArtifacts = 1;
    require(
        startMiningRun(state, catalog, {MiningAct::ActOne, 8, 92933, true, MiningGateType::None}, true).applied,
        "artifact save test should start mining at an artifact-enabled tier");
    state.run.mining.artifact.state = MiningArtifactState::Loose;
    state.run.mining.artifact.tethered = true;
    state.run.mining.artifact.health = 0.64;
    state.run.mining.artifact.velocityX = 1.2;
    state.run.mining.artifact.velocityY = -0.4;
    state.meta.ark.repairProgress = 2;

    const std::string saveText = serializeSaveData(captureSaveData(state));
    const std::optional<SaveData> save = deserializeSaveData(saveText);
    require(save.has_value(), "artifact save should deserialize");
    GameState restored = createNewGame(catalog, 92934);
    restoreSaveData(restored, catalog, *save);
    require(restored.run.mining.artifact.present, "active mining artifact should round trip");
    require(restored.run.mining.artifact.state == MiningArtifactState::Loose, "artifact state should round trip");
    require(restored.run.mining.artifact.tethered, "artifact tether state should round trip");
    require(std::abs(restored.run.mining.artifact.health - 0.64) < 0.0001, "artifact health should round trip");
    require(restored.meta.ark.repairProgress == 2, "Ark repair progress should round trip");
}

void surfaceScanMiniGameBanksSurveyPayload()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 94101);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    Random rng(94102);

    const int supplyBefore = state.run.surfaceExpedition.supply;
    const SurfaceActionOutcome started = startSurfaceScanRun(state, rng);
    require(started.applied, "surface scan mini-game should start from Surface Ops");
    require(state.screen == Screen::SurfaceScan, "starting scan should move to the scan screen");
    require(state.run.surfaceExpedition.supply == supplyBefore - tuning::research::surveySupplyCost, "starting scan should spend the survey action kit");

    Random panelRng(94103);
    const PreparedLaunch prepared = prepareLaunch(state, catalog, panelRng);
    const std::string initialPanel = buildGamePanelHtml({state, catalog, prepared, prepared});
    require(initialPanel.find("surface-scan-rail") != std::string::npos &&
            initialPanel.find("data-rr-action=\"surface_scan_pulse\"") != std::string::npos,
        "surface scan should expose its compact rail and pulse action");

    const MaterialInventory ownedMaterialsBefore = state.meta.materials;
    const std::size_t ownedArtifactsBefore = state.meta.artifacts.size();
    const int blueprintProgressBefore = state.meta.blueprintProgress;
    const int progressionBefore = state.meta.prospectorCommonOreRecovered;
    const double creditsBefore = state.run.credits;
    const int cargoBefore = state.run.surfaceExpedition.cargo;
    const MaterialInventory recoveredMaterialsBefore = state.run.surfaceExpedition.temporaryMaterials;
    const std::size_t recoveredArtifactsBefore = state.run.surfaceExpedition.temporaryArtifacts.size();

    state.run.surfaceScan.elapsedSeconds =
        (tuning::research::scanWindowCenterRadians +
            tuning::research::scanGoodWindowHalfAngleRadians * 0.5) /
        tuning::research::scanSweepRadiansPerSecond;
    const SurfaceActionOutcome pulse = pulseSurfaceScan(state, rng);
    require(pulse.applied, "scan pulse should resolve while scan is active");
    require(state.run.surfaceScan.lastPulseGrade == SurfaceScanPulseGrade::Good,
        "a pulse inside the yellow window should be graded Good");
    require(!state.run.surfaceScan.depthProspects.empty() &&
            state.run.surfaceScan.depthProspects.back().informationPercent == tuning::research::scanGoodInformationPercent,
        "a Good pulse should bank an 80 percent level forecast");
    require(state.run.surfaceScan.successFanfareSeconds > 0.0,
        "a Good pulse should start its success fanfare");
    require(state.run.surfaceScan.temporaryMaterials.common > 0, "scan pulse should stage common materials");
    require(state.meta.materials.common == ownedMaterialsBefore.common
            && state.meta.materials.rare == ownedMaterialsBefore.rare
            && state.meta.materials.exotic == ownedMaterialsBefore.exotic,
        "scan pulses should not mutate owned meta materials");
    require(state.meta.artifacts.size() == ownedArtifactsBefore, "scan pulses should not grant owned artifacts");
    require(state.meta.blueprintProgress == blueprintProgressBefore, "scan pulses should not grant blueprint progression");
    require(state.meta.prospectorCommonOreRecovered == progressionBefore, "scan pulses should not advance mining progression");
    require(state.run.credits == creditsBefore, "scan pulses should not grant mission credits");
    require(state.run.surfaceExpedition.cargo == cargoBefore, "scan pulses should not grant expedition cargo");
    require(state.run.surfaceExpedition.temporaryMaterials.common == recoveredMaterialsBefore.common
            && state.run.surfaceExpedition.temporaryMaterials.rare == recoveredMaterialsBefore.rare
            && state.run.surfaceExpedition.temporaryMaterials.exotic == recoveredMaterialsBefore.exotic,
        "scan pulses should not move forecasts into recovered expedition materials");
    require(state.run.surfaceExpedition.temporaryArtifacts.size() == recoveredArtifactsBefore,
        "scan pulses should not move forecast artifacts into the recovered expedition payload");

    while (!state.run.surfaceScan.completed) {
        state.run.surfaceScan.elapsedSeconds =
            tuning::research::scanWindowCenterRadians /
            tuning::research::scanSweepRadiansPerSecond;
        require(pulseSurfaceScan(state, rng).applied,
            "each available Survey pulse should resolve before the scan limit");
    }
    const SurfaceScanRailPresentation limitedPresentation = surfaceScanRailPresentation(state);
    require(limitedPresentation.objective.find("SURVEY SCAN LIMIT REACHED") != std::string::npos
            && !limitedPresentation.actions.empty()
            && limitedPresentation.actions.front().enabled
            && limitedPresentation.actions.front().actionId == ui::actions::surfaceScanPulse
            && limitedPresentation.actions.front().label == "SURVEY SCAN LIMIT REACHED",
        "an exhausted Survey should keep its pulse focus target and visibly explain the scan limit");
    const int pulsesAtLimit = state.run.surfaceScan.pulses;
    const SurfaceActionOutcome exhaustedPulse = pulseSurfaceScan(state, rng);
    require(!exhaustedPulse.applied
            && state.run.surfaceScan.pulses == pulsesAtLimit
            && exhaustedPulse.message.find("Survey scan limit reached") != std::string::npos,
        "pressing Survey again at the limit should report the limit without consuming another pulse");

    const SurfaceActionOutcome banked = bankSurfaceScan(state);
    require(banked.applied, "banking scan should resolve");
    require(state.screen == Screen::SurfaceExpedition, "banking scan should return to Surface Ops");
    require(state.run.surfaceExpedition.miningSitePrepared, "banked scan should open the mining site");
    require(state.run.surfaceExpedition.temporaryMaterials.common == 0, "banked scan should not grant recovered materials before mining");
    require(state.run.surfaceExpedition.prospectMaterials.common > 0, "banked scan should tag common material prospects");

    require(startMiningRun(state, catalog).applied, "scan prospects should open the mining run");
    const bool foundProspectedOre = std::any_of(
        state.run.mining.terrain.cells.begin(),
        state.run.mining.terrain.cells.end(),
        [](const MiningCell& cell) {
            return cell.material == MiningCellMaterial::CommonOre && cell.revealed;
        });
    require(foundProspectedOre, "scan prospects should appear as revealed ore in mining terrain");
}


void surfacePushMiniGameBanksDepthRoute()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 94201);
    state.meta.chapter = GameChapter::RedFrontier;
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    state.meta.surfaceDepthUpgrades.surveyArray = 1;
    state.meta.surfaceDepthUpgrades.boreSystem = 1;
    state.run.surfaceExpedition.depthProspects = {{1, 1}, {2, 2}};
    Random rng(94202);

    const int supplyBefore = state.run.surfaceExpedition.supply;
    const SurfaceActionOutcome started = startSurfacePushRun(state, rng);
    require(started.applied, "surface push mini-game should start from Surface Ops");
    require(state.screen == Screen::SurfacePush, "starting Push Deeper should move to its phase board");
    require(state.run.surfaceExpedition.supply == supplyBefore - tuning::research::pushSupplyCost, "starting push should spend push action kits");

    state.run.surfacePush.collapseRisk = 1.0;
    const SurfaceActionOutcome step = pushSurfaceDepthStep(state, rng);
    require(step.applied, "Push Deeper step should resolve while active");
    require(!step.hazardTriggered && state.run.surfacePush.steps == 1,
        "the first pushed layer should resolve before collapse risk begins");
    require(state.run.surfacePush.depthGain > 0, "Push Deeper should stage a depth gain");
    require(state.run.surfacePush.temporaryMaterials.rare > 0, "Push Deeper should stage richer materials");
    require(!state.run.surfacePush.rewardMarkers.empty(), "Push Deeper should record stable visual reward markers");

    const std::vector<MiningCellMaterial> markersAfterFirstStep = state.run.surfacePush.rewardMarkers;
    state.run.surfacePush.collapseRisk = 0.0;
    require(pushSurfaceDepthStep(state, rng).applied, "second Push Deeper step should resolve for marker stability");
    require(
        std::equal(markersAfterFirstStep.begin(), markersAfterFirstStep.end(), state.run.surfacePush.rewardMarkers.begin()),
        "later Push Deeper rewards should not rewrite previously displayed marker types");

    const int depthBeforeBank = state.run.surfaceExpedition.depth;
    const SurfaceActionOutcome banked = bankSurfacePush(state);
    require(banked.applied, "banking Push Deeper should resolve");
    require(state.screen == Screen::SurfaceExpedition, "banking Push Deeper should return to Surface Ops");
    require(state.run.surfaceExpedition.depth > depthBeforeBank, "banked Push Deeper should increase expedition depth");
    require(state.run.surfaceExpedition.miningSitePrepared, "banked Push Deeper should keep the mining site open");
    require(state.run.surfaceExpedition.temporaryMaterials.rare == 0, "banked Push Deeper should not grant recovered materials before mining");
    require(state.run.surfaceExpedition.prospectMaterials.rare > 0, "banked Push Deeper should tag richer material prospects");

    require(startMiningRun(state, catalog).applied, "Push Deeper prospects should open the mining run");
    require(state.run.mining.entryDepthZone == 0, "the mining ship should remain fixed at surface depth zero");
    require(state.run.mining.rigDepthZone == state.run.mining.depthZone,
        "the Mining Rig should deploy directly at the banked pushed depth");
    const bool foundDeepProspect = std::any_of(
        state.run.mining.terrain.cells.begin(),
        state.run.mining.terrain.cells.end(),
        [](const MiningCell& cell) {
            return (cell.material == MiningCellMaterial::RareOre || cell.material == MiningCellMaterial::ArtifactCache) && cell.revealed;
        });
    require(foundDeepProspect, "Push Deeper prospects should appear as revealed rich targets in mining terrain");
}

void surfacePushLaterCollapseLosesUncommittedRoute()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 94211);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    state.meta.surfaceDepthUpgrades.surveyArray = 1;
    state.meta.surfaceDepthUpgrades.boreSystem = 1;
    state.run.surfaceExpedition.depthProspects = {{1, 1}, {2, 2}};
    Random rng(94212);

    require(startSurfacePushRun(state, rng).applied, "collapse test should start Push Deeper");
    state.run.surfacePush.collapseRisk = 1.0;
    require(pushSurfaceDepthStep(state, rng).applied, "safe first layer should resolve");
    require(state.run.surfacePush.steps == 1 && !state.run.surfacePush.busted,
        "a forced collapse roll must not affect layer one");
    state.run.surfacePush.collapseRisk = 1.0;
    const SurfaceActionOutcome collapse = pushSurfaceDepthStep(state, rng);
    require(collapse.applied && collapse.hazardTriggered,
        "collapse risk should apply when attempting layer two");
    require(state.run.surfacePush.busted && state.run.surfacePush.completed,
        "a later collapse should close and bust the unbanked route");
}

void scannedArtifactBecomesARecoverableMiningTarget()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 94221);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    state.run.surfaceExpedition.supply = 10;
    const int baseDepth = state.run.surfaceExpedition.depth;
    state.meta.surfaceDepthUpgrades.surveyArray = 1;
    state.meta.surfaceDepthUpgrades.boreSystem = 1;
    state.run.surfaceExpedition.depthProspects = {
        {1, baseDepth + 1},
        {2, baseDepth + 2, {}, 1}
    };
    Random rng(94222);

    require(startSurfacePushRun(state, rng).applied, "artifact route should start Push Deeper");
    state.run.surfacePush.collapseRisk = 1.0;
    require(pushSurfaceDepthStep(state, rng).applied, "artifact route should resolve its safe first layer");
    state.run.surfacePush.collapseRisk = 0.0;
    require(pushSurfaceDepthStep(state, rng).applied, "artifact forecast layer should push successfully");
    require(state.run.surfacePush.temporaryArtifacts.size() == 1,
        "a scanned artifact should be confirmed without another random roll");
    require(std::find(
            state.run.surfacePush.rewardMarkers.begin(),
            state.run.surfacePush.rewardMarkers.end(),
            MiningCellMaterial::ArtifactCache) != state.run.surfacePush.rewardMarkers.end(),
        "a confirmed artifact should keep a stable Push Deeper marker");

    require(bankSurfacePush(state).applied, "confirmed artifact route should bank");
    require(state.run.surfaceExpedition.depth == baseDepth + 2 &&
            state.run.surfaceExpedition.prospectArtifacts == 1,
        "banking should bind the confirmed artifact to the selected start depth");
    require(startMiningRun(state, catalog).applied, "confirmed artifact route should start mining");
    MiningRunState& mining = state.run.mining;
    require(mining.depthZone == baseDepth + 2 && mining.entryDepthZone == 0,
        "the rig should start at the selected depth while the ship stays on the surface");
    require(mining.artifact.present && mining.artifact.revealed &&
            mining.artifact.state == MiningArtifactState::Embedded,
        "the banked artifact should become a revealed recoverable mining object");
    const MiningCell* artifactCell = miningCellAt(
        mining.terrain,
        static_cast<int>(std::floor(mining.artifact.x)),
        static_cast<int>(std::floor(mining.artifact.y)));
    require(artifactCell != nullptr &&
            artifactCell->material == MiningCellMaterial::ArtifactCache &&
            artifactCell->revealed,
        "terrain normalization must not overwrite the guaranteed artifact cell");

    const MiningArenaRules rules = resolveMiningArenaRules({
        mining.arenaMetadata.act,
        mining.arenaMetadata.difficulty,
        mining.arenaMetadata.seed
    });
    require(!rules.mechanics.artifactTethering,
        "the scanned-artifact tether regression should begin before the authored tether tutorial");
    mining.droneX = mining.artifact.x - 0.5;
    mining.droneY = mining.artifact.y - 0.5;
    toggleMiningTether(state);
    require(mining.artifact.tethered,
        "a scanned artifact should tether from the Mining Rig before the global tutorial flag unlocks");
    toggleMiningTether(state);

    mining.operatorMode = MiningOperatorMode::Jetpack;
    mining.operatorPresent = true;
    mining.operatorX = mining.artifact.x - 0.5;
    mining.operatorY = mining.artifact.y - 0.5;
    toggleMiningTether(state);
    require(mining.artifact.tethered,
        "a scanned artifact should tether from EVA before the global tutorial flag unlocks");
}

void poiGuidancePrioritizesSafetyAndTracksRecoverableArtifacts()
{
    MiningRunState mining;
    mining.active = true;
    mining.depthZone = 2;
    mining.entryDepthZone = 0;
    mining.returnZoneX = 8.0;
    mining.returnZoneY = 3.0;
    mining.oxygenSeconds = 100.0;
    mining.droneHealth = 1.0;
    mining.drillIntegrity = 1.0;
    mining.artifact.present = true;
    mining.artifact.revealed = true;
    mining.artifact.state = MiningArtifactState::Embedded;
    mining.artifact.x = 12.5;
    mining.artifact.y = 18.5;

    PoiGuidanceTarget guidance = miningPoiGuidanceTarget(
        mining, 100.0, tuning::launch::warningCautionThreshold, false);
    require(guidance.active && guidance.kind == PoiGuidanceKind::Artifact &&
            guidance.direction == PoiGuidanceDirection::WorldTarget,
        "a revealed recoverable artifact on the active layer should receive dynamic guidance");

    mining.oxygenSeconds = 37.0;
    guidance = miningPoiGuidanceTarget(
        mining, 100.0, tuning::launch::warningCautionThreshold, false);
    require(guidance.active && guidance.kind == PoiGuidanceKind::Ship &&
            guidance.direction == PoiGuidanceDirection::Ascend,
        "oxygen caution should override artifact guidance and point upward below surface");

    mining.oxygenSeconds = 100.0;
    mining.droneHealth = 0.37;
    guidance = miningPoiGuidanceTarget(
        mining, 100.0, tuning::launch::warningCautionThreshold, false);
    require(guidance.active && guidance.kind == PoiGuidanceKind::Ship,
        "mining rig integrity caution should override artifact guidance");

    mining.droneHealth = 1.0;
    mining.operatorMode = MiningOperatorMode::Jetpack;
    mining.operatorPresent = true;
    mining.operatorIntegrity = 0.37;
    guidance = miningPoiGuidanceTarget(
        mining, 100.0, tuning::launch::warningCautionThreshold, false);
    require(guidance.active && guidance.kind == PoiGuidanceKind::Ship,
        "active operator suit integrity caution should override artifact guidance");

    mining.operatorMode = MiningOperatorMode::Rig;
    mining.operatorPresent = false;
    mining.operatorIntegrity = 1.0;
    mining.drillIntegrity = 0.37;
    guidance = miningPoiGuidanceTarget(
        mining, 100.0, tuning::launch::warningCautionThreshold, false);
    require(guidance.active && guidance.kind == PoiGuidanceKind::Ship,
        "drill integrity caution should override artifact guidance");

    mining.drillIntegrity = 1.0;
    mining.oxygenSeconds = 37.0;
    mining.depthZone = 0;
    guidance = miningPoiGuidanceTarget(
        mining, 100.0, tuning::launch::warningCautionThreshold, false);
    require(guidance.active && guidance.direction == PoiGuidanceDirection::WorldTarget &&
            guidance.x == mining.returnZoneX && guidance.y == mining.returnZoneY,
        "surface safety guidance should point directly to the ship");
    require(!miningPoiGuidanceTarget(
                 mining, 100.0, tuning::launch::warningCautionThreshold, true).active,
        "surface ship guidance should disappear inside the return zone");

    mining.oxygenSeconds = 100.0;
    mining.depthZone = 2;
    mining.artifact.state = MiningArtifactState::Delivered;
    guidance = miningPoiGuidanceTarget(
        mining, 100.0, tuning::launch::warningCautionThreshold, false);
    require(!guidance.active, "delivered artifacts should not retain POI guidance");
    mining.artifact.state = MiningArtifactState::Destroyed;
    guidance = miningPoiGuidanceTarget(
        mining, 100.0, tuning::launch::warningCautionThreshold, false);
    require(!guidance.active, "destroyed artifacts should not retain POI guidance");

    MiningDepthLayerState upperLayer;
    upperLayer.depthZone = 1;
    upperLayer.artifact.present = true;
    upperLayer.artifact.revealed = true;
    upperLayer.artifact.state = MiningArtifactState::Loose;
    mining.depthLayers = {upperLayer};
    guidance = miningPoiGuidanceTarget(
        mining, 100.0, tuning::launch::warningCautionThreshold, false);
    require(guidance.active && guidance.direction == PoiGuidanceDirection::Ascend,
        "an artifact on an upper cached layer should lead to the ascent boundary");
    mining.depthLayers.front().depthZone = 3;
    guidance = miningPoiGuidanceTarget(
        mining, 100.0, tuning::launch::warningCautionThreshold, false);
    require(guidance.active && guidance.direction == PoiGuidanceDirection::Descend,
        "an artifact on a lower cached layer should lead to the descent boundary");

    PoiGuidanceTarget futureTarget;
    futureTarget.active = true;
    futureTarget.kind = PoiGuidanceKind::Boss;
    futureTarget.label = "BOSS";
    require(futureTarget.kind == PoiGuidanceKind::Boss,
        "future story and boss guidance should use the same dynamic-label interface");
}

void surfaceScanForecastsPushDepthLayers()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 94301);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    state.run.surfaceExpedition.supply = 10;
    Random rng(94302);

    require(startSurfaceScanRun(state, rng).applied, "scan forecast test should start scanning");
    state.run.surfaceScan.bustRisk = 0.0;
    require(pulseSurfaceScan(state, rng).applied, "first scan should map current layer");
    state.run.surfaceScan.bustRisk = 0.0;
    require(pulseSurfaceScan(state, rng).applied, "second scan should map first push layer");
    require(state.run.surfaceScan.depthProspects.size() == 2, "scan should record one forecast per pulse");
    require(state.run.surfaceScan.depthProspects[0].depthOffset == 0, "first scan forecast should describe the current mining layer");
    require(state.run.surfaceScan.depthProspects[1].depthOffset == 1, "second scan forecast should describe the first pushed layer");

    const SurfaceActionOutcome bankedScan = bankSurfaceScan(state);
    require(bankedScan.applied, "banked scan should preserve layer forecasts");
    require(state.run.surfaceExpedition.depthProspects.size() == 2, "banked scan should store mapped depth forecasts");
    require(state.run.surfaceExpedition.prospectMaterials.common > 0, "current-layer scan forecast should tag current mining prospects");

    const int startingDepth = state.run.surfaceExpedition.depth;
    require(startSurfacePushRun(state, rng).applied, "push should start after banked scan");
    state.run.surfacePush.collapseRisk = 0.0;
    require(pushSurfaceDepthStep(state, rng).applied, "first push should resolve against the +1 forecast");
    require(!state.run.surfacePush.rewardMarkers.empty(), "pushed layer should expose actual reward markers");
    require(
        std::all_of(state.run.surfacePush.rewardMarkerDepthOffsets.begin(), state.run.surfacePush.rewardMarkerDepthOffsets.end(), [](int offset) {
            return offset == 1;
        }),
        "actual push reward markers should line up with the pushed layer");

    const SurfaceActionOutcome bankedPush = bankSurfacePush(state);
    require(bankedPush.applied, "banked push should commit the pushed layer");
    require(state.run.surfaceExpedition.depth == startingDepth + 1, "banked push should move mining to the scanned +1 layer");
    require(state.run.surfaceExpedition.prospectMaterials.rare > 0, "banked push should replace forecast-only tags with actual pushed finds");

    require(startMiningRun(state, catalog).applied, "forecasted pushed layer should open mining");
    require(state.run.mining.depthZone == startingDepth + 1, "mining should start at the pushed layer depth");
    require(state.run.surfaceExpedition.depthProspects.empty(), "starting mining should consume banked layer forecasts");
}

void thermalSurfacePushStillMapsResourceLayers()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 94305);
    state.run.destinationIndex = 3;
    state.meta.furthestTier = 3;
    startSurfaceExpedition(state, catalog);
    state.run.surfaceExpedition.pendingScenarioId =
        std::string(content::scenario::volcanicDescent);
    state.run.surfaceExpedition.pendingScenarioStepId = "recovery";
    state.run.surfaceExpedition.pendingMiningSiteDefinitionId =
        std::string(content::miningSite::thermalLayeredRecovery);
    state.run.surfaceExpedition.supply = 10;
    Random rng(94306);

    require(startSurfaceScanRun(state, rng).applied,
        "thermal route should still allow its resource layers to be scanned");
    state.run.surfaceScan.bustRisk = 0.0;
    require(pulseSurfaceScan(state, rng).applied,
        "thermal scan should map its current layer");
    const MaterialInventory mapped =
        state.run.surfaceScan.depthProspects.empty()
        ? MaterialInventory{}
        : state.run.surfaceScan.depthProspects.front().possibleMaterials;
    require(mapped.common > 0 || mapped.rare > 0 || mapped.exotic > 0,
        "thermal scan should display the resources sealed behind Hazard treatment");
    state.run.surfaceScan.bustRisk = 0.0;
    const double goodOffset =
        (tuning::research::surfaceScanPerfectWindowHalfAngleForDepth(1) +
         tuning::research::surfaceScanGoodWindowHalfAngleForDepth(1)) *
        0.5;
    state.run.surfaceScan.elapsedSeconds =
        (tuning::research::scanWindowCenterRadians + goodOffset) /
        tuning::research::scanSweepRadiansPerSecond;
    require(pulseSurfaceScan(state, rng).applied,
        "thermal scan should map the first diggable layer before Dig unlocks");
    require(
        state.run.surfaceScan.lastPulseGrade == SurfaceScanPulseGrade::Good &&
            state.run.surfaceScan.depthProspects.size() >= 2 &&
            state.run.surfaceScan.depthProspects[1].possibleArtifacts == 1,
        "a good Thermal survey must preserve the authored protected-objective signal at its +1 depth");
    require(bankSurfaceScan(state).applied,
        "thermal resource forecast should remain available to Push Deeper");

    state.run.surfaceExpedition.depthProspects[1].possibleArtifacts = 0;
    require(startSurfacePushRun(state, rng).applied,
        "thermal Push Deeper route should start");
    require(
        state.run.surfaceExpedition.depthProspects[1].possibleArtifacts == 1,
        "starting Dig should repair an already-surveyed authored objective forecast from an older save");
    state.run.surfacePush.collapseRisk = 0.0;
    require(pushSurfaceDepthStep(state, rng).applied,
        "thermal route should resolve its guaranteed first layer");
    require(state.run.surfacePush.temporaryMaterials.common >= 1
            && state.run.surfacePush.temporaryMaterials.rare >= 1,
        "thermal first layer should retain the same guaranteed resource value");
    require(!state.run.surfacePush.rewardMarkers.empty()
            && state.run.surfacePush.rewardMarkers.size()
                == state.run.surfacePush.rewardMarkerDepthOffsets.size(),
        "thermal Push Deeper should send colored resource markers to presentation");
}

void surfaceScanBustAndAbortDiscardForecasts()
{
    const ContentCatalog catalog = createDefaultContent();

    GameState missed = createNewGame(catalog, 94311);
    missed.run.destinationIndex = 2;
    startSurfaceExpedition(missed, catalog);
    ui::briefings::acknowledge(
        missed.meta.acknowledgedActivityBriefingIds,
        ui::briefings::surfaceSurveyComplete);
    Random bustRng(94312);
    require(startSurfaceScanRun(missed, bustRng).applied, "miss-state scan should start");
    missed.run.surfaceScan.elapsedSeconds = 3.14159265358979323846 / tuning::research::scanSweepRadiansPerSecond;
    const SurfaceActionOutcome missPulse = pulseSurfaceScan(missed, bustRng);
    require(missPulse.applied && !missPulse.hazardTriggered, "a scan miss should spend a pulse without becoming a hazard");
    require(missed.run.surfaceScan.lastPulseGrade == SurfaceScanPulseGrade::Miss &&
            missed.run.surfaceScan.pulses == 1 && missed.run.surfaceScan.active &&
            missed.run.surfaceScan.depthProspects.empty() &&
            missed.run.surfaceScan.missFanfareSeconds > 0.0,
        "a scan miss should reveal no data and leave the same level available to retry");
    missed.run.surfaceScan.elapsedSeconds = tuning::research::scanWindowCenterRadians /
        tuning::research::scanSweepRadiansPerSecond;
    require(pulseSurfaceScan(missed, bustRng).applied, "a retry inside the green window should resolve");
    require(missed.run.surfaceScan.lastPulseGrade == SurfaceScanPulseGrade::Perfect &&
            missed.run.surfaceScan.depthProspects.size() == 1 &&
            missed.run.surfaceScan.depthProspects.front().depthOffset == 0 &&
            missed.run.surfaceScan.depthProspects.front().informationPercent == tuning::research::scanPerfectInformationPercent &&
            missed.run.surfaceScan.successFanfareSeconds > 0.0 &&
            missed.run.surfaceScan.missFanfareSeconds == 0.0,
        "a perfect retry should map all information for the still-current level");

    GameState aborted = createNewGame(catalog, 94321);
    aborted.run.destinationIndex = 2;
    startSurfaceExpedition(aborted, catalog);
    Random abortRng(94322);
    require(startSurfaceScanRun(aborted, abortRng).applied, "abort-state scan should start");
    const double abortHazardBefore = aborted.run.surfaceExpedition.hazard;
    const MaterialInventory abortOwnedBefore = aborted.meta.materials;
    aborted.run.surfaceScan.maxPulses = 1;
    aborted.run.surfaceScan.bustRisk = 0.0;
    require(pulseSurfaceScan(aborted, abortRng).applied, "abort-state scan should map a temporary forecast");
    require(aborted.run.surfaceScan.completed, "final supported pulse should expose the completed scan state");
    require(!aborted.run.surfaceScan.depthProspects.empty(), "abort-state scan should hold a temporary layer forecast");
    require(abortSurfaceScan(aborted).applied, "active scan should abort");
    require(aborted.screen == Screen::SurfaceExpedition, "aborted scan should return to Surface Ops");
    require(!aborted.run.surfaceExpedition.miningSitePrepared, "aborted scan should not prepare Mining");
    require(aborted.run.surfaceExpedition.depthProspects.empty(), "aborted scan should discard temporary depth forecasts");
    require(aborted.run.surfaceExpedition.prospectMaterials.common == 0
            && aborted.run.surfaceExpedition.prospectMaterials.rare == 0
            && aborted.run.surfaceExpedition.prospectMaterials.exotic == 0,
        "aborted scan should not tag material prospects");
    require(std::abs(aborted.run.surfaceExpedition.hazard - abortHazardBefore) < 0.0001,
        "aborted scan should discard unbanked pulse hazard");
    require(aborted.meta.materials.common == abortOwnedBefore.common
            && aborted.meta.materials.rare == abortOwnedBefore.rare
            && aborted.meta.materials.exotic == abortOwnedBefore.exotic,
        "aborted scan should not mutate owned resources");
}

void surfaceMissionLogIsBounded()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 9393);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    require(!state.run.surfaceExpedition.logEntries.empty(), "surface expedition should log the starting site profile");

    state.run.surfaceExpedition.supply = 20;
    Random rng(9394);
    for (int i = 0; i < tuning::research::surfaceLogEntryLimit + 3; ++i) {
        require(surveySurfaceSite(state, rng).applied, "surface survey should keep logging while supply remains");
    }

    require(static_cast<int>(state.run.surfaceExpedition.logEntries.size()) == tuning::research::surfaceLogEntryLimit, "surface mission log should keep only recent entries");
}

void miningTerrainIsDeterministicAndDepthScales()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 91919);
    state.meta.chapter = GameChapter::Breakthrough;
    const Destination& outerPlanets = catalog.destinations[3];
    const MiningTerrain a = generateMiningTerrain(state, outerPlanets, SurfaceSiteProfile::OreShelf, 1);
    const MiningTerrain b = generateMiningTerrain(state, outerPlanets, SurfaceSiteProfile::OreShelf, 1);
    require(a.width == tuning::mining::terrainWidth && a.height == tuning::mining::terrainHeight, "mining terrain should use the active-zone dimensions");
    require(a.cells.size() == b.cells.size(), "matching mining terrain should have matching cell counts");
    int elementalHazards = 0;
    for (std::size_t i = 0; i < a.cells.size(); ++i) {
        require(a.cells[i].material == b.cells[i].material, "mining terrain generation should be deterministic");
        require(std::abs(a.cells[i].maxToughness - b.cells[i].maxToughness) < 0.000001, "mining toughness should be deterministic");
        require(a.cells[i].hazardAffinity == b.cells[i].hazardAffinity, "mining hazard affinities should be deterministic");
        if (a.cells[i].material == MiningCellMaterial::HazardPocket) {
            ++elementalHazards;
            require(a.cells[i].hazardAffinity == MiningElementalAffinity::Thermal || a.cells[i].hazardAffinity == MiningElementalAffinity::Cryo,
                "Act 1 Pressure hazards should stay in the Thermal/Cryo teaching set");
        }
    }
    const auto hasReturnShaft = [](const MiningTerrain& terrain) {
        const int leftX = terrain.width / 2 - 1;
        for (int y = 0; y < terrain.height - 1; ++y) {
            for (int x = leftX; x <= leftX + 1; ++x) {
                const MiningCell* cell = miningCellAt(terrain, x, y);
                if (cell == nullptr || cell->material != MiningCellMaterial::Empty ||
                    cell->feature != MiningCellFeature::MainTunnel || cell->suitOnlyPassage) {
                    return false;
                }
            }
        }
        return true;
    };
    require(!hasReturnShaft(a),
        "a fresh mining layer should stay normal until the player leaves it for a deeper depth");
    require(elementalHazards > 0, "Act 1 Pressure terrain should include environmental hazard pockets");
    const MiningArenaRules actOneEarly = resolveMiningArenaRules({MiningAct::ActOne, 1, 1});
    const MiningArenaRules actOneLate = resolveMiningArenaRules({MiningAct::ActOne, 10, 1});
    require(actOneLate.terrainToughnessScale > actOneEarly.terrainToughnessScale,
        "arena difficulty should replace raw depth as the terrain toughness progression");
    require(std::none_of(a.cells.begin(), a.cells.end(), [](const MiningCell& cell) {
        return cell.enemy != MiningEnemyType::None;
    }), "Act 1 mining terrain should not seed hostile metadata");

    for (int difficulty = 1; difficulty <= 10; ++difficulty) {
        const MiningArenaRules actTwo = resolveMiningArenaRules({MiningAct::ActTwo, difficulty, 1});
        require(!miningAffinityAllowed(actTwo, MiningElementalAffinity::Radiation), "Act 2 should never expose Radiation");
        require(miningAffinityAllowed(actTwo, MiningElementalAffinity::Toxic) == (difficulty >= 9),
            "Act 2 Toxic affinity should wait for Mastery levels");
    }
    require(!miningAffinityAllowed(resolveMiningArenaRules({MiningAct::ActThree, 1, 1}), MiningElementalAffinity::Radiation),
        "Act 3 level 1 should introduce Mammals before Radiation");
    require(miningAffinityAllowed(resolveMiningArenaRules({MiningAct::ActThree, 2, 1}), MiningElementalAffinity::Radiation),
        "Act 3 level 2 should introduce Radiation");
}

void hostileMiningTerrainGeneratesPreDugEnemyStructures()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 91920);
    state.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    state.meta.ark.condition = ArkCondition::DamagedStranded;
    state.meta.chapter = GameChapter::Ascent;
    const Destination& nearbyGalaxy = *catalog.findDestination(content::destination::nearbyGalaxy);

    const MiningTerrain terrain = generateMiningTerrain(state, nearbyGalaxy, SurfaceSiteProfile::OreShelf, 1);
    const MiningTerrain repeat = generateMiningTerrain(state, nearbyGalaxy, SurfaceSiteProfile::OreShelf, 1);

    int mainTunnel = 0;
    int branchTunnel = 0;
    int encounterZones = 0;
    int rooms = 0;
    int organicBurrows = 0;
    int bossChambers = 0;
    int enemyCells = 0;
    int mammalCells = 0;
    int rewardCells = 0;
    int bossRewardCells = 0;
    for (std::size_t i = 0; i < terrain.cells.size(); ++i) {
        const MiningCell& cell = terrain.cells[i];
        const MiningCell& repeated = repeat.cells[i];
        require(cell.material == repeated.material, "hostile terrain material generation should be deterministic");
        require(cell.feature == repeated.feature, "hostile tunnel features should be deterministic");
        require(cell.enemy == repeated.enemy, "hostile enemy zones should be deterministic");
        mainTunnel += cell.feature == MiningCellFeature::MainTunnel ? 1 : 0;
        branchTunnel += cell.feature == MiningCellFeature::BranchTunnel ? 1 : 0;
        encounterZones += cell.feature == MiningCellFeature::EncounterZone ? 1 : 0;
        rooms += cell.feature == MiningCellFeature::TreasureVault || cell.feature == MiningCellFeature::MinibossLair || cell.feature == MiningCellFeature::HiveNest || cell.feature == MiningCellFeature::BossChamber ? 1 : 0;
        organicBurrows += cell.feature == MiningCellFeature::OrganicBurrow ? 1 : 0;
        bossChambers += cell.feature == MiningCellFeature::BossChamber ? 1 : 0;
        enemyCells += cell.enemy != MiningEnemyType::None ? 1 : 0;
        mammalCells += cell.enemy == MiningEnemyType::Mammal ? 1 : 0;
        rewardCells += (cell.feature == MiningCellFeature::TreasureVault || cell.feature == MiningCellFeature::MinibossLair || cell.feature == MiningCellFeature::HiveNest || cell.feature == MiningCellFeature::BossChamber) && miningMaterialSolid(cell.material) ? 1 : 0;
        bossRewardCells += cell.feature == MiningCellFeature::BossChamber && miningMaterialSolid(cell.material) ? 1 : 0;
    }

    require(mainTunnel > 10, "hostile mining terrain should include pre-dug main tunnels");
    require(branchTunnel > 10, "hostile mining terrain should include branching tunnels");
    require(encounterZones > 0, "hostile mining terrain should include enemy encounter zones");
    require(rooms > 0, "hostile mining terrain should include specialized rooms");
    require(organicBurrows > 0, "hostile mammal lanes should carve organic burrows");
    require(bossChambers > 0, "hostile mammal lanes should create larger boss chambers");
    require(enemyCells > 0, "hostile mining terrain should assign enemy families to encounter structures");
    require(mammalCells > 0, "hostile boss chambers should assign mammal enemies");
    require(rewardCells > 0, "hostile specialized rooms should seed rich deposits");
    require(bossRewardCells > 0, "hostile boss chambers should seed advanced-tech deposits");
    require(miningCellFeatureName(MiningCellFeature::TreasureVault) == std::string_view("Treasure vault"), "mining feature names should describe special rooms");
    require(miningCellFeatureName(MiningCellFeature::OrganicBurrow) == std::string_view("Organic burrow"), "mining feature names should describe mammal burrows");
    require(miningEnemyTypeName(MiningEnemyType::Elemental) == std::string_view("Elemental monsters"), "mining enemy names should cover elemental threats");
}

void actBasedMiningEnemyProgressionIsEnforced()
{
    const ContentCatalog catalog = createDefaultContent();
    auto startArena = [&](MiningAct act, int difficulty, std::uint64_t seed) {
        GameState state = createNewGame(catalog, seed + 1000);
        const int destinationIndex = act == MiningAct::ActOne ? 1 : (act == MiningAct::ActTwo ? 4 : 5);
        state.run.destinationIndex = destinationIndex;
        state.meta.chapter = act == MiningAct::ActOne
            ? GameChapter::LunarProgram
            : (act == MiningAct::ActTwo ? GameChapter::Arkfall : GameChapter::VoidCompass);
        startSurfaceExpedition(state, catalog);
        prepareMiningSiteForTest(state);
        state.run.surfaceExpedition.rigFuel = std::max(1.0, state.run.surfaceExpedition.rigFuel);
        const SurfaceActionOutcome outcome = startMiningRun(state, catalog, {act, difficulty, seed}, false);
        require(outcome.applied, "explicit mining arena requests should start through the shared initializer");
        return state;
    };

    for (const MiningAct act : {MiningAct::ActOne, MiningAct::ActTwo, MiningAct::ActThree}) {
        for (int difficulty = 1; difficulty <= 10; ++difficulty) {
            GameState state = startArena(act, difficulty, 88000 + static_cast<std::uint64_t>(static_cast<int>(act) * 100 + difficulty));
            const MiningArenaRules rules = resolveMiningArenaRules({act, difficulty, state.run.mining.arenaMetadata.seed});
            const int activeEnemies = static_cast<int>(std::count_if(state.run.mining.enemies.begin(), state.run.mining.enemies.end(), [](const MiningEnemy& enemy) {
                return enemy.active;
            }));
            const int activeSpawners = static_cast<int>(std::count_if(state.run.mining.enemies.begin(), state.run.mining.enemies.end(), [](const MiningEnemy& enemy) {
                return enemy.active && enemy.type == MiningEnemyType::Spawner;
            }));
            require(activeEnemies <= rules.maxActiveEnemies, "initial enemy population should respect the act/level active cap");
            require(activeSpawners <= rules.maxSpawners, "procedural spawners should respect the act/level spawner cap");

            for (const MiningCell& cell : state.run.mining.terrain.cells) {
                require(miningRoomFeatureAllowed(rules, cell.feature), "terrain should not stamp a room before its progression gate");
                if (cell.enemy != MiningEnemyType::None) {
                    require(miningEnemyAllowed(rules, cell.enemy), "terrain should not stamp an enemy family before its progression gate");
                }
                if (cell.hazardAffinity != MiningElementalAffinity::None) {
                    require(miningAffinityAllowed(rules, cell.hazardAffinity), "terrain should not stamp an affinity before its progression gate");
                }
            }
            for (const MiningEnemy& enemy : state.run.mining.enemies) {
                require(miningEnemyAllowed(rules, enemy.type), "spawned enemies should come from the resolved roster");
                if (enemy.affinity != MiningElementalAffinity::None) {
                    require(miningAffinityAllowed(rules, enemy.affinity), "spawned Elementals should use only resolved affinities");
                }
            }

            if (act == MiningAct::ActOne) {
                require(state.run.mining.enemies.empty(), "Act 1 should never spawn combat encounters");
            }
            if (act == MiningAct::ActTwo) {
                require(std::none_of(state.run.mining.enemies.begin(), state.run.mining.enemies.end(), [](const MiningEnemy& enemy) {
                    return enemy.type == MiningEnemyType::Mammal || enemy.affinity == MiningElementalAffinity::Radiation;
                }), "Act 2 should exclude Mammals and Radiation");
                require(std::none_of(state.run.mining.terrain.cells.begin(), state.run.mining.terrain.cells.end(), [](const MiningCell& cell) {
                    return cell.feature == MiningCellFeature::BossChamber || cell.enemy == MiningEnemyType::Mammal || cell.hazardAffinity == MiningElementalAffinity::Radiation;
                }), "Act 2 terrain should exclude boss chambers, Mammals, and Radiation");
                if (difficulty <= 3) {
                    require(std::all_of(state.run.mining.enemies.begin(), state.run.mining.enemies.end(), [](const MiningEnemy& enemy) {
                        return enemy.type == MiningEnemyType::Ant;
                    }), "Act 2 Learn should contain only Ant melee contacts");
                }
                if (difficulty < 10) {
                    require(activeSpawners == 0, "Act 2 spawners should wait until level 10");
                }
            }
            if (act == MiningAct::ActThree && difficulty < 7) {
                require(std::none_of(state.run.mining.terrain.cells.begin(), state.run.mining.terrain.cells.end(), [](const MiningCell& cell) {
                    return cell.feature == MiningCellFeature::BossChamber;
                }), "Act 3 boss chambers should wait until Pressure levels");
            }
        }
    }

    GameState actTwoOne = startArena(MiningAct::ActTwo, 1, 991001);
    GameState actTwoThree = startArena(MiningAct::ActTwo, 3, 991001);
    require(!actTwoOne.run.mining.enemies.empty() && !actTwoThree.run.mining.enemies.empty(), "Act 2 Learn arenas should seed Ant contacts");
    const MiningEnemy& earlyAnt = actTwoOne.run.mining.enemies.front();
    const MiningEnemy& lateAnt = actTwoThree.run.mining.enemies.front();
    require(earlyAnt.type == MiningEnemyType::Ant && lateAnt.type == MiningEnemyType::Ant, "Act 2 Learn scaling comparison should use the Ant archetype");
    require(std::abs(earlyAnt.speed - lateAnt.speed) < 0.000001 && std::abs(earlyAnt.speed - 2.0) < 0.000001,
        "difficulty scaling should preserve archetype-defined enemy speed");
    require(lateAnt.maxHealth > earlyAnt.maxHealth && lateAnt.damagePerSecond > earlyAnt.damagePerSecond,
        "Act 2 health and damage pressure should rise monotonically within the band");
    require(std::abs(earlyAnt.maxHealth - 5.0 * 0.70) < 0.000001 && std::abs(lateAnt.maxHealth - 5.0 * 0.86) < 0.000001,
        "Act 2 enemy health should use the progression resolver scale");
    require(std::abs(earlyAnt.damagePerSecond - 0.62 * 0.65) < 0.000001 && std::abs(lateAnt.damagePerSecond - 0.62 * 0.79) < 0.000001,
        "Act 2 enemy damage should use the progression resolver scale");

    GameState spawnerArena = startArena(MiningAct::ActThree, 10, 991010);
    const MiningArenaRules spawnerRules = resolveMiningArenaRules({MiningAct::ActThree, 10, 991010});
    const int initialSpawners = static_cast<int>(std::count_if(spawnerArena.run.mining.enemies.begin(), spawnerArena.run.mining.enemies.end(), [](const MiningEnemy& enemy) {
        return enemy.active && enemy.type == MiningEnemyType::Spawner;
    }));
    require(initialSpawners > 0 && initialSpawners <= spawnerRules.maxSpawners, "Act 3 Mastery should procedurally place bounded spawners");
    spawnerArena.run.mining.oxygenSeconds = 1000.0;
    spawnerArena.run.mining.droneHealth = 1000.0;
    for (int tick = 0; tick < 120; ++tick) {
        updateMiningRun(spawnerArena, catalog, 0.5);
    }
    const int activeAfterSpawns = static_cast<int>(std::count_if(spawnerArena.run.mining.enemies.begin(), spawnerArena.run.mining.enemies.end(), [](const MiningEnemy& enemy) {
        return enemy.active;
    }));
    require(activeAfterSpawns <= spawnerRules.maxActiveEnemies, "reinforcement waves should never exceed the resolved active-enemy cap");
}

void hostileMiningRunSpawnsEnemiesAndPassiveDefenses()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 91921);
    state.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    state.meta.ark.condition = ArkCondition::DamagedStranded;
    state.meta.chapter = GameChapter::Arkfall;
    state.meta.ark.fuelReserve = tuning::ark::hostileSystemFuelReserve;
    state.meta.unlockKeys.push_back(content::unlock::deepSpace);
    state.run.destinationIndex = 4;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "hostile mining run should start");
    require(!state.run.mining.enemies.empty(), "hostile mining run should spawn enemies from encounter structures");

    MiningEnemy& attacker = state.run.mining.enemies.front();
    attacker.type = MiningEnemyType::Ant;
    attacker.x = state.run.mining.droneX + 0.2;
    attacker.y = state.run.mining.droneY;
    attacker.health = 100.0;
    attacker.maxHealth = 100.0;
    attacker.damagePerSecond = 1.0;
    attacker.speed = 0.0;
    const double healthBefore = state.run.mining.droneHealth;
    updateMiningRun(state, catalog, 1.0);
    require(state.run.mining.enemyDamageTaken > 0.0, "nearby enemies should damage the mining drone");
    require(state.run.mining.droneHealth < healthBefore, "enemy contact should reduce drone health");
    require(!state.run.mining.damageNumbers.empty(), "enemy melee hits should create rig damage numbers");

    GameState defended = createNewGame(catalog, 91922);
    defended.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    defended.meta.ark.condition = ArkCondition::DamagedStranded;
    defended.meta.chapter = GameChapter::Arkfall;
    defended.meta.ark.fuelReserve = tuning::ark::hostileSystemFuelReserve;
    defended.meta.unlockKeys.push_back(content::unlock::deepSpace);
    defended.meta.unlockKeys.push_back(content::unlock::droneBay);
    defended.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
    defended.meta.unlockKeys.push_back(content::unlock::perimeterDrones);
    defended.meta.unlockKeys.push_back(content::unlock::perimeterCoordination);
    ensureDroneBayState(defended, catalog);
    defended.meta.droneBaySlots = 2;
    defended.meta.equippedDroneIds = {content::drone::attackDrone, content::drone::defenseDrone};
    defended.run.destinationIndex = 4;
    startSurfaceExpedition(defended, catalog);
    prepareMiningSiteForTest(defended);
    require(startMiningRun(defended, catalog).applied, "defended hostile mining run should start");
    defended.run.mining.enemies = {
        {MiningEnemyType::Beetle, MiningCellFeature::MinibossLair, defended.run.mining.droneX + 2.0, defended.run.mining.droneY, 0.0, 0.0, 0.5, 10.0, 0.20, 0.0, 0.0, 0.0, true}
    };
    for (int tick = 0; tick < 10 && defended.run.mining.enemiesDefeated == 0; ++tick) {
        updateMiningRun(defended, catalog, 0.08);
    }
    require(defended.run.mining.enemiesDefeated == 1, "passive sentry defenses should defeat weakened enemies");
    require(defended.run.mining.defenseDamageDealt > 0.0, "passive sentry defenses should report damage dealt");
    require(!defended.run.mining.combatProjectiles.empty(), "passive sentry defenses should create allied projectile visuals");
    const MiningProjectileVisual& alliedProjectile = defended.run.mining.combatProjectiles.front();
    const auto attackAgent = std::find_if(
        defended.run.mining.miniDrones.begin(),
        defended.run.mining.miniDrones.end(),
        [](const MiningMiniDroneAgent& agent) { return agent.role == MiniDroneRole::Attack; });
    require(attackAgent != defended.run.mining.miniDrones.end(), "equipped Attack drone should create an independent mining agent");
    require(
        std::hypot(alliedProjectile.startX - attackAgent->x, alliedProjectile.startY - attackAgent->y) > 0.45,
        "allied projectiles should start from an Attack drone weapon hardpoint");
    const MiningRunPresentation defendedMining = miningRunPresentation(defended, catalog);
    require(defendedMining.combatMetrics.size() >= 3,
        "live mining combat strip should expose projectile, defeat, and support damage metrics");
    require(std::any_of(defended.run.mining.damageNumbers.begin(), defended.run.mining.damageNumbers.end(), [](const MiningDamageNumber& number) {
        return number.team == MiningCombatTeam::Allied;
    }), "passive sentry defenses should create allied damage numbers");
    require(std::any_of(defended.run.mining.damageNumbers.begin(), defended.run.mining.damageNumbers.end(), [](const MiningDamageNumber& number) {
        return number.kind == MiningCombatTextKind::Defeat;
    }), "enemy defeats should create a distinct defeat popup");
    require(std::any_of(defended.run.mining.damageNumbers.begin(), defended.run.mining.damageNumbers.end(), [](const MiningDamageNumber& number) {
        return number.kind == MiningCombatTextKind::RareReward || number.kind == MiningCombatTextKind::ExoticReward;
    }), "enemy defeat rewards should create material popup text");
    require(defended.run.mining.temporaryMaterials.rare > 0 || defended.run.mining.temporaryMaterials.exotic > 0, "miniboss defeats should grant upgrade-grade rewards");
}

void miningEnemySpawnersAreGenericCappedAndDestructible()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 91928);
    state.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    state.meta.ark.condition = ArkCondition::DamagedStranded;
    state.meta.chapter = GameChapter::LastCampfire;
    state.meta.ark.fuelReserve = tuning::ark::hostileSystemFuelReserve;
    state.meta.unlockKeys.push_back(content::unlock::deepSpace);
    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    state.meta.unlockKeys.push_back(content::unlock::perimeterDrones);
    state.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
    state.meta.unlockKeys.push_back(content::unlock::perimeterCoordination);
    ensureDroneBayState(state, catalog);
    state.meta.droneBaySlots = 1;
    state.meta.equippedDroneIds = {content::drone::attackDrone};
    state.run.destinationIndex = 4;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "spawner mining run should start");
    clearMiningTerrainForEvaTest(state.run.mining);

    const double spawnerX = state.run.mining.droneX + 3.0;
    const double spawnerY = state.run.mining.droneY + 1.0;
    state.run.mining.enemies.push_back(createMiningEnemySpawner(
        spawnerX,
        spawnerY,
        1000.0,
        MiningEnemyType::Elemental,
        3,
        0.16,
        MiningElementalAffinity::Toxic));
    require(state.run.mining.enemies.front().type == MiningEnemyType::Spawner &&
        state.run.mining.enemies.front().health == 1000.0,
        "enemy spawners should use normal enemy health and targeting state");

    updateMiningRun(state, catalog, 0.08);
    updateMiningRun(state, catalog, 0.06);
    require(state.run.mining.enemies.size() == 1, "spawners should wait for their configured interval before producing an enemy");
    updateMiningRun(state, catalog, 0.03);
    require(state.run.mining.enemies.size() == 2, "spawners should produce the configured enemy when their interval elapses");
    require(state.run.mining.enemies.back().type == MiningEnemyType::Elemental &&
        state.run.mining.enemies.back().affinity == MiningElementalAffinity::Toxic,
        "spawners should support enemy-specific configuration without type-specific spawn code");
    for (int tick = 0; tick < 10; ++tick) {
        updateMiningRun(state, catalog, 0.08);
    }
    require(state.run.mining.enemies.front().spawn.spawned == 3 && state.run.mining.enemies.size() == 4,
        "spawners should stop at their configured lifetime spawn cap");
    for (int tick = 0; tick < 50; ++tick) {
        updateMiningRun(state, catalog, 0.08);
    }
    require(state.run.mining.enemies.front().spawn.spawned == 3 && state.run.mining.enemies.size() == 4,
        "elapsed time should never let a spawner exceed its maximum spawn count");

    const SaveData save = captureSaveData(state);
    GameState restored = createNewGame(catalog, 919280);
    restoreSaveData(restored, catalog, save);
    require(!restored.run.mining.enemies.empty() && restored.run.mining.enemies.front().type == MiningEnemyType::Spawner,
        "active enemy spawners should round trip through saves");
    require(restored.run.mining.enemies.front().spawn.enemyType == MiningEnemyType::Elemental &&
        restored.run.mining.enemies.front().spawn.affinity == MiningElementalAffinity::Toxic &&
        restored.run.mining.enemies.front().spawn.maxSpawns == 3 &&
        restored.run.mining.enemies.front().spawn.spawned == 3,
        "spawner type, affinity, cap, and progress should round trip through saves");

    state.run.mining.enemies.clear();
    for (MiningMiniDroneAgent& agent : state.run.mining.miniDrones) {
        agent.targetEnemyIndex = -1;
        agent.actionCooldownSeconds = 0.0;
    }
    state.run.mining.enemies.push_back(createMiningEnemySpawner(
        spawnerX,
        spawnerY,
        0.25,
        MiningEnemyType::Ant,
        5,
        10.0));
    for (int tick = 0; tick < 120 && state.run.mining.enemies.front().active; ++tick) {
        updateMiningRun(state, catalog, 0.08);
    }
    require(!state.run.mining.enemies.front().active,
        "passive combat should target and destroy a spawner through the normal enemy damage path");
    require(state.run.mining.enemies.front().spawn.spawned == 0,
        "destroyed spawners should stop producing enemies immediately");
}

void rangedMiningEnemiesShootAndCombatVisualsExpire()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 91929);
    state.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    state.meta.ark.condition = ArkCondition::DamagedStranded;
    state.meta.ark.fuelReserve = tuning::ark::hostileSystemFuelReserve;
    state.meta.unlockKeys.push_back(content::unlock::deepSpace);
    state.run.destinationIndex = 4;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "ranged enemy mining run should start");
    state.run.mining.enemies = {
        {MiningEnemyType::Flying, MiningCellFeature::HiveNest, state.run.mining.droneX + 5.0, state.run.mining.droneY, 0.0, 0.0, 40.0, 40.0, 0.0, 0.0, 1.0, 0.0, true}
    };
    const double healthBefore = state.run.mining.droneHealth;
    updateMiningRun(state, catalog, 0.08);
    require(state.run.mining.droneHealth < healthBefore, "ranged enemies should damage the drone from standoff range");
    require(!state.run.mining.combatProjectiles.empty(), "ranged enemies should create enemy projectile visuals");
    require(std::any_of(state.run.mining.damageNumbers.begin(), state.run.mining.damageNumbers.end(), [](const MiningDamageNumber& number) {
        return number.team == MiningCombatTeam::Enemy && number.rigDamage;
    }), "ranged enemy shots should create rig damage numbers");

    for (int tick = 0; tick < 30; ++tick) {
        updateMiningRun(state, catalog, 0.08);
    }
    require(state.run.mining.combatProjectiles.size() <= static_cast<std::size_t>(tuning::mining::maxCombatProjectiles), "combat projectile visuals should stay capped");
    require(state.run.mining.damageNumbers.size() <= static_cast<std::size_t>(tuning::mining::maxDamageNumbers), "combat damage numbers should stay capped");
}

void attackDroneCombatCanCritAndEnemyCooldownPersists()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 91930);
    state.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    state.meta.ark.condition = ArkCondition::DamagedStranded;
    state.meta.ark.fuelReserve = tuning::ark::hostileSystemFuelReserve;
    state.meta.unlockKeys.push_back(content::unlock::deepSpace);
    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    state.meta.unlockKeys.push_back(content::unlock::perimeterDrones);
    state.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
    state.meta.unlockKeys.push_back(content::unlock::perimeterCoordination);
    ensureDroneBayState(state, catalog);
    state.meta.droneBaySlots = 1;
    state.meta.equippedDroneIds = {content::drone::attackDrone};
    state.run.destinationIndex = 4;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "attack drone crit mining run should start");
    state.run.mining.enemies = {
        {MiningEnemyType::Beetle, MiningCellFeature::EncounterZone, state.run.mining.droneX + 3.0, state.run.mining.droneY, 0.0, 0.0, 200.0, 200.0, 0.0, 0.0, 0.0, 0.0, true}
    };
    bool sawCrit = false;
    for (int tick = 0; tick < 80 && !sawCrit; ++tick) {
        updateMiningRun(state, catalog, 0.08);
        sawCrit = std::any_of(state.run.mining.damageNumbers.begin(), state.run.mining.damageNumbers.end(), [](const MiningDamageNumber& number) {
            return number.team == MiningCombatTeam::Allied && number.critical;
        });
    }
    require(sawCrit, "attack drone fire should be able to create critical damage text");

    state.run.mining.enemies.front().attackCooldownSeconds = 0.42;
    const SaveData save = captureSaveData(state);
    GameState restored = createNewGame(catalog, 91931);
    restoreSaveData(restored, catalog, save);
    require(!restored.run.mining.enemies.empty(), "enemy cooldown save test should restore active enemies");
    require(std::abs(restored.run.mining.enemies.front().attackCooldownSeconds - 0.42) < 0.000001, "enemy attack cooldown should round trip through saves");
}

void miningMiniDronesFollowIndependentRolePositions()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 91932);
    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    state.meta.unlockKeys.push_back(content::unlock::perimeterDrones);
    state.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
    state.meta.unlockKeys.push_back(content::unlock::ioHazardDrone);
    ensureDroneBayState(state, catalog);
    state.meta.droneBaySlots = 6;
    state.meta.equippedDroneIds = {
        content::drone::miningDrone,
        content::drone::resourceDrone,
        content::drone::surveyDrone,
        content::drone::hazardDrone,
        content::drone::attackDrone,
        content::drone::defenseDrone
    };
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "all-role mini-drone run should start");
    require(state.run.mining.miniDrones.size() == 6, "every equipped Support Drone should create one independent agent");
    clearMiningTerrainForEvaTest(state.run.mining);

    std::vector<std::pair<double, double>> positions;
    for (const MiningMiniDroneAgent& agent : state.run.mining.miniDrones) {
        positions.push_back({agent.x, agent.y});
    }
    setMiningAim(state, 0.0, 0.8);
    for (std::size_t i = 0; i < positions.size(); ++i) {
        require(std::abs(state.run.mining.miniDrones[i].x - positions[i].first) < 0.000001,
            "changing the main drill aim should not rotate mini-drone x positions");
        require(std::abs(state.run.mining.miniDrones[i].y - positions[i].second) < 0.000001,
            "changing the main drill aim should not rotate mini-drone y positions");
    }

    state.run.mining.enemies.clear();
    for (int tick = 0; tick < 20; ++tick) {
        updateMiningRun(state, catalog, 0.08);
    }
    const auto resource = std::find_if(state.run.mining.miniDrones.begin(), state.run.mining.miniDrones.end(), [](const MiningMiniDroneAgent& agent) {
        return agent.role == MiniDroneRole::Resource;
    });
    const auto survey = std::find_if(state.run.mining.miniDrones.begin(), state.run.mining.miniDrones.end(), [](const MiningMiniDroneAgent& agent) {
        return agent.role == MiniDroneRole::Survey;
    });
    const auto hazard = std::find_if(state.run.mining.miniDrones.begin(), state.run.mining.miniDrones.end(), [](const MiningMiniDroneAgent& agent) {
        return agent.role == MiniDroneRole::Hazard;
    });
    require(resource != state.run.mining.miniDrones.end(), "Resource drone agent should remain available");
    require(survey != state.run.mining.miniDrones.end(), "Survey drone agent should remain available");
    require(hazard != state.run.mining.miniDrones.end(), "Hazard drone agent should remain available");
    const MiniDroneAnchorFrame anchor = resolveMiniDroneAnchor(state.run.mining);
    const auto followsRoleRing = [&](const MiningMiniDroneAgent& agent) {
        const double distance = std::hypot(
            agent.x - anchor.x,
            agent.y - anchor.y);
        return std::abs(distance - miniDroneOrbitRadius(agent.role)) < 0.38;
    };
    require(followsRoleRing(*resource),
        "Resource drone should hold its configured collection orbit around the controlled actor");
    require(followsRoleRing(*survey),
        "Survey drone should hold its wider scouting orbit around the controlled actor");
    require(followsRoleRing(*hazard),
        "Hazard drone should hold its configured remediation orbit around the controlled actor");
    require(
        (hazard->behavior == MiningMiniDroneBehavior::Following ||
            hazard->behavior == MiningMiniDroneBehavior::Returning) &&
            hazard->targetCellX < 0 &&
            hazard->targetCellY < 0,
        "Hazard drone without an eligible task should remain in or return to its anchor orbit");
}

void hazardDroneTreatsAffinityLadderAndBatches()
{
    const ContentCatalog catalog = createDefaultContent();
    auto runFirstTreatment = [&](int level, MiningElementalAffinity affinity, int clusterSize) {
        GameState state = createNewGame(catalog, 93000 + level * 17 + static_cast<int>(affinity));
        state.meta.unlockKeys.push_back(content::unlock::droneBay);
        state.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
        state.meta.unlockKeys.push_back(content::unlock::ioHazardDrone);
        state.meta.ownedDroneIds.push_back(content::drone::hazardDrone);
        ensureDroneBayState(state, catalog);
        state.meta.droneBaySlots = 1;
        state.meta.equippedDroneIds = {content::drone::hazardDrone};
        state.run.destinationIndex = 2;
        startSurfaceExpedition(state, catalog);
        state.run.surfaceExpedition.runDroneRanks = {{content::drone::hazardDrone, level}};
        prepareMiningSiteForTest(state);
        require(startMiningRun(state, catalog).applied, "hazard treatment test run should start");
        MiningRunState& mining = state.run.mining;
        clearMiningTerrainForEvaTest(mining);
        if (level == 1) {
            const auto hazardAgent = std::find_if(
                mining.miniDrones.begin(),
                mining.miniDrones.end(),
                [](const MiningMiniDroneAgent& agent) { return agent.role == MiniDroneRole::Hazard; });
            require(hazardAgent != mining.miniDrones.end(), "the Io priority fixture requires an active Hazard Support Drone");
            const int priorityY = std::clamp(
                static_cast<int>(std::floor(mining.droneY)) + 1,
                1,
                mining.terrain.height - 2);
            const int ordinaryX = std::clamp(
                static_cast<int>(std::floor(mining.droneX)) + 1,
                1,
                mining.terrain.width - 3);
            const int farSealX = ordinaryX + 1;
            const int nearSealX = ordinaryX + 2;
            MiningCell* ordinaryLava = miningCellAt(mining.terrain, ordinaryX, priorityY);
            MiningCell* farStorySeal = miningCellAt(mining.terrain, farSealX, priorityY);
            MiningCell* nearStorySeal = miningCellAt(mining.terrain, nearSealX, priorityY);
            require(ordinaryLava != nullptr && farStorySeal != nullptr && nearStorySeal != nullptr, "the Io priority cells should exist");
            const double priorityToughness = miningMaterialToughness(MiningCellMaterial::HazardPocket, mining.depthZone);
            MiningCocoonLayerProgress priorityLayer;
            priorityLayer.id = "priority_layer";
            priorityLayer.label = "PROTECTED LAYER";
            priorityLayer.total = 2;
            priorityLayer.remaining = 2;
            priorityLayer.requiredHazardMark = 1;
            priorityLayer.revealed = true;
            mining.gate.cocoonLayers = {priorityLayer};
            mining.gate.activeCocoonLayer = 0;
            *ordinaryLava = {MiningCellMaterial::HazardPocket, priorityToughness, priorityToughness, true, true};
            ordinaryLava->hazardAffinity = MiningElementalAffinity::Thermal;
            *farStorySeal = {MiningCellMaterial::HazardPocket, priorityToughness, priorityToughness, true, true};
            farStorySeal->hazardAffinity = MiningElementalAffinity::Thermal;
            farStorySeal->cocoonLayer = 0;
            *nearStorySeal = {MiningCellMaterial::HazardPocket, priorityToughness, priorityToughness, true, true};
            nearStorySeal->hazardAffinity = MiningElementalAffinity::Thermal;
            nearStorySeal->cocoonLayer = 0;
            mining.artifact.present = true;
            mining.artifact.x = static_cast<double>(nearSealX) + 0.5;
            mining.artifact.y = static_cast<double>(priorityY) + 0.5;
            HazardDroneCoordinator coordinator(mining);
            coordinator.synchronizeAssignments();
            require(coordinator.acquireAssignment(*hazardAgent)
                    && hazardAgent->targetCellX == nearSealX
                    && hazardAgent->targetCellY == priorityY,
                "Hazard Drone should prioritize the closest Io artifact-seal segment over ordinary lava and distant seal segments");
            coordinator.releaseAssignment(*hazardAgent);
            const int nearbyHazardX = ordinaryX;
            const int distantHazardX = std::min(mining.terrain.width - 2, ordinaryX + 5);
            MiningCell* nearbyHazard = miningCellAt(mining.terrain, nearbyHazardX, priorityY + 2);
            MiningCell* distantHazard = miningCellAt(mining.terrain, distantHazardX, priorityY + 2);
            require(nearbyHazard != nullptr && distantHazard != nullptr, "the player-priority hazard cells should exist");
            *nearbyHazard = {MiningCellMaterial::HazardPocket, priorityToughness, priorityToughness, true, true};
            nearbyHazard->hazardAffinity = MiningElementalAffinity::Thermal;
            *distantHazard = {MiningCellMaterial::HazardPocket, priorityToughness, priorityToughness, true, true};
            distantHazard->hazardAffinity = MiningElementalAffinity::Thermal;
            *ordinaryLava = {MiningCellMaterial::Empty, 0.0, 0.0, true, false};
            *farStorySeal = {MiningCellMaterial::Empty, 0.0, 0.0, true, false};
            *nearStorySeal = {MiningCellMaterial::Empty, 0.0, 0.0, true, false};
            hazardAgent->x = static_cast<double>(distantHazardX) + 0.5;
            hazardAgent->y = static_cast<double>(priorityY) + 2.5;
            coordinator.synchronizeAssignments();
            require(coordinator.acquireAssignment(*hazardAgent)
                    && hazardAgent->targetCellX == nearbyHazardX
                    && hazardAgent->targetCellY == priorityY + 2,
                "Hazard Drone should prioritize the nearest unresolved hazard to the Mining Rig over a leftover tile beside itself");
            coordinator.releaseAssignment(*hazardAgent);
            *ordinaryLava = {MiningCellMaterial::Empty, 0.0, 0.0, true, false};
            *nearbyHazard = {MiningCellMaterial::Empty, 0.0, 0.0, true, false};
            *distantHazard = {MiningCellMaterial::Empty, 0.0, 0.0, true, false};
            hazardAgent->x = mining.droneX;
            hazardAgent->y = mining.droneY;
        }
        const int startX = std::clamp(static_cast<int>(std::floor(mining.droneX)) + 1, 1, mining.terrain.width - clusterSize - 1);
        const int y = std::clamp(static_cast<int>(std::floor(mining.droneY)) + 2, 1, mining.terrain.height - 2);
        for (int offset = 0; offset < clusterSize; ++offset) {
            MiningCell* cell = miningCellAt(mining.terrain, startX + offset, y);
            require(cell != nullptr, "hazard treatment cluster cell should exist");
            const double toughness = miningMaterialToughness(MiningCellMaterial::HazardPocket, mining.depthZone);
            *cell = {MiningCellMaterial::HazardPocket, toughness, toughness, true, true};
            cell->hazardAffinity = affinity;
        }
        MiningCell* unsupported = miningCellAt(mining.terrain, startX, y + 1);
        require(unsupported != nullptr, "unsupported hazard test cell should exist");
        const double toughness = miningMaterialToughness(MiningCellMaterial::HazardPocket, mining.depthZone);
        const bool radiationSupported = level >= 3;
        if (!radiationSupported) {
            *unsupported = {MiningCellMaterial::HazardPocket, toughness, toughness, true, true};
            unsupported->hazardAffinity = MiningElementalAffinity::Radiation;
        } else {
            *unsupported = {MiningCellMaterial::Regolith, toughness, toughness, true, false};
        }

        const MaterialInventory materialsBefore = mining.temporaryMaterials;
        int remaining = clusterSize;
        for (int tick = 0; tick < 240 && remaining == clusterSize; ++tick) {
            updateMiningRun(state, catalog, 0.05);
            remaining = 0;
            for (int offset = 0; offset < clusterSize; ++offset) {
                const MiningCell* cell = miningCellAt(mining.terrain, startX + offset, y);
                remaining += cell != nullptr && cell->material == MiningCellMaterial::HazardPocket ? 1 : 0;
            }
        }
        require(radiationSupported || unsupported->material == MiningCellMaterial::HazardPocket,
            "Hazard Drone should ignore affinities above its current Mk");
        require(mining.temporaryMaterials.common == materialsBefore.common &&
                mining.temporaryMaterials.rare == materialsBefore.rare &&
                mining.temporaryMaterials.exotic == materialsBefore.exotic,
            "hazard refinement should create a mineable tile instead of granting materials directly");
        return state;
    };

    require(std::abs(tuning::mining::hazardDroneTreatmentSeconds(1) - 1.5) < 0.000001,
        "Hazard Drone Mk I treatment should run at twice the previous three-second rate");
    GameState continuation = runFirstTreatment(1, MiningElementalAffinity::Thermal, 2);
    const auto continuationAgent = std::find_if(
        continuation.run.mining.miniDrones.begin(),
        continuation.run.mining.miniDrones.end(),
        [](const MiningMiniDroneAgent& agent) { return agent.role == MiniDroneRole::Hazard; });
    require(continuationAgent != continuation.run.mining.miniDrones.end(),
        "the continuing-treatment fixture should retain its Hazard Drone");
    HazardDroneCoordinator continuationCoordinator(continuation.run.mining);
    continuationCoordinator.synchronizeAssignments();
    require(
        continuationCoordinator.hasAssignment(*continuationAgent)
            || continuationCoordinator.acquireAssignment(*continuationAgent),
        "Hazard Drone should reserve its next eligible tile instead of idling after a treatment");
    runFirstTreatment(2, MiningElementalAffinity::Toxic, 3);
    const GameState firstMkThree = runFirstTreatment(3, MiningElementalAffinity::Radiation, 3);
    const GameState secondMkThree = runFirstTreatment(3, MiningElementalAffinity::Radiation, 3);
    require(firstMkThree.run.mining.terrain.cells.size() == secondMkThree.run.mining.terrain.cells.size() &&
            std::equal(
                firstMkThree.run.mining.terrain.cells.begin(),
                firstMkThree.run.mining.terrain.cells.end(),
                secondMkThree.run.mining.terrain.cells.begin(),
                [](const MiningCell& lhs, const MiningCell& rhs) {
                    return lhs.material == rhs.material && lhs.hazard == rhs.hazard && lhs.hazardAffinity == rhs.hazardAffinity;
                }),
        "hazard refinement results should be deterministic for the same run seed and tile coordinates");
}

void hazardDronesCrossSolidTerrainButNeverTargetHiddenCells()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 93401);
    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    state.meta.unlockKeys.push_back(content::unlock::ioHazardDrone);
    state.meta.ownedDroneIds.push_back(content::drone::hazardDrone);
    ensureDroneBayState(state, catalog);
    state.meta.droneBaySlots = 1;
    state.meta.equippedDroneIds = {content::drone::hazardDrone};
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied,
        "collisionless Hazard fixture should start an Io mining run");

    MiningRunState& mining = state.run.mining;
    clearMiningTerrainForEvaTest(mining);
    mining.droneX = 3.5;
    mining.droneY = 6.5;
    MiningMiniDroneAgent& hazardAgent = mining.miniDrones.front();
    hazardAgent.x = mining.droneX;
    hazardAgent.y = mining.droneY;
    const double hardness =
        miningMaterialToughness(MiningCellMaterial::HazardPocket, mining.depthZone);
    for (int y = 0; y < mining.terrain.height; ++y) {
        MiningCell* wall = miningCellAt(mining.terrain, 8, y);
        require(wall != nullptr, "collisionless Hazard wall should exist");
        *wall = {MiningCellMaterial::HardRock, hardness, hardness, true, false};
    }

    constexpr int targetX = 14;
    constexpr int targetY = 6;
    MiningCell* target = miningCellAt(mining.terrain, targetX, targetY);
    require(target != nullptr, "collisionless Hazard target should exist");
    MiningCocoonLayerProgress hiddenLayer;
    hiddenLayer.id = "hidden_layer";
    hiddenLayer.label = "HIDDEN LAYER";
    hiddenLayer.total = 1;
    hiddenLayer.remaining = 1;
    hiddenLayer.requiredHazardMark = 1;
    mining.gate.cocoonLayers = {hiddenLayer};
    mining.gate.activeCocoonLayer = 0;
    *target = {MiningCellMaterial::HazardPocket, hardness, hardness, false, true};
    target->hazardAffinity = MiningElementalAffinity::Thermal;
    target->cocoonLayer = 0;
    mining.artifact.present = true;
    mining.artifact.x = 15.5;
    mining.artifact.y = 6.5;

    updateMiningRun(state, catalog, 0.10);
    require(
        hazardAgent.targetCellX < 0 && target->material == MiningCellMaterial::HazardPocket,
        "a Hazard Drone must not detect or treat an unrevealed story seal");

    target->revealed = true;
    mining.gate.cocoonLayers.front().revealed = true;
    bool crossedSolidTerrain = false;
    for (int tick = 0; tick < 500; ++tick) {
        updateMiningRun(state, catalog, 0.05);
        const MiningCell* occupied = miningCellAt(
            mining.terrain,
            static_cast<int>(std::floor(hazardAgent.x)),
            static_cast<int>(std::floor(hazardAgent.y)));
        crossedSolidTerrain =
            crossedSolidTerrain ||
            (occupied != nullptr && miningMaterialSolid(occupied->material));
        if (target->material != MiningCellMaterial::HazardPocket) {
            break;
        }
    }
    require(crossedSolidTerrain,
        "a revealed story seal should send the Hazard Drone directly through blocking terrain");
    require(
        target->material != MiningCellMaterial::HazardPocket,
        "the collisionless Hazard Drone should reach and treat the revealed story seal");

    bool returnedAcrossWall = false;
    for (int tick = 0; tick < 300; ++tick) {
        updateMiningRun(state, catalog, 0.05);
        if (hazardAgent.x < 8.0) {
            returnedAcrossWall = true;
            break;
        }
    }
    require(returnedAcrossWall,
        "a Hazard Drone should return directly to the controlled actor after treatment");
}

void hazardDroneFinishesCommittedTreatmentBeforeFollowingMovedPlayer()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 93017);
    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    state.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
    state.meta.unlockKeys.push_back(content::unlock::ioHazardDrone);
    state.meta.ownedDroneIds.push_back(content::drone::hazardDrone);
    ensureDroneBayState(state, catalog);
    state.meta.droneBaySlots = 1;
    state.meta.equippedDroneIds = {content::drone::hazardDrone};
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    state.run.surfaceExpedition.runDroneRanks = {{content::drone::hazardDrone, 1}};
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied,
        "the committed Hazard Drone fixture should start a mining run");

    MiningRunState& mining = state.run.mining;
    clearMiningTerrainForEvaTest(mining);
    const auto found = std::find_if(
        mining.miniDrones.begin(),
        mining.miniDrones.end(),
        [](const MiningMiniDroneAgent& agent) {
            return agent.role == MiniDroneRole::Hazard;
        });
    require(found != mining.miniDrones.end(),
        "the committed treatment fixture should deploy a Hazard Drone");
    MiningMiniDroneAgent& hazard = *found;
    const int targetX = std::clamp(
        static_cast<int>(std::floor(mining.droneX)) + 2,
        1,
        mining.terrain.width - 2);
    const int targetY = std::clamp(
        static_cast<int>(std::floor(mining.droneY)) + 1,
        1,
        mining.terrain.height - 2);
    MiningCell* target = miningCellAt(mining.terrain, targetX, targetY);
    require(target != nullptr, "the committed hazard target should exist");
    const double toughness = miningMaterialToughness(
        MiningCellMaterial::HazardPocket,
        mining.depthZone);
    *target = {
        MiningCellMaterial::HazardPocket,
        toughness,
        toughness,
        true,
        true};
    target->hazardAffinity = MiningElementalAffinity::Thermal;

    hazard.x = static_cast<double>(targetX) + 0.5;
    hazard.y = static_cast<double>(targetY) + 0.5 -
        tuning::mining::hazardDroneWorkRangeCells * 0.72;
    hazard.targetCellX = targetX;
    hazard.targetCellY = targetY;
    hazard.behavior = MiningMiniDroneBehavior::Working;
    hazard.taskProgressSeconds =
        tuning::mining::hazardDroneTreatmentSeconds(hazard.upgradeLevel) * 0.75;

    mining.droneX = std::min(
        static_cast<double>(mining.terrain.width - 2),
        hazard.x + tuning::mining::hazardDroneAcquireRadiusCells + 1.0);
    mining.droneY = hazard.y;
    updateMiningRun(state, catalog, 0.08);
    require(
        target->material == MiningCellMaterial::HazardPocket &&
            hazard.behavior == MiningMiniDroneBehavior::Working &&
            hazard.targetCellX == targetX &&
            hazard.targetCellY == targetY &&
            hazard.finishTargetBeforeReturn,
        "moving away must commit the Hazard Drone to its current conversion instead of recalling it");

    for (int step = 0;
         step < 80 && target->material == MiningCellMaterial::HazardPocket;
         ++step) {
        updateMiningRun(state, catalog, 0.08);
    }
    require(
        target->material != MiningCellMaterial::HazardPocket &&
            hazard.behavior == MiningMiniDroneBehavior::Returning &&
            hazard.targetCellX < 0 &&
            hazard.targetCellY < 0 &&
            hazard.finishTargetBeforeReturn,
        "the committed Hazard Drone should finish exactly one conversion before following the moved player");
}

void duplicateHazardDronesCoordinatePriorityAndExactAssistance()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 93402);
    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    state.meta.unlockKeys.push_back(content::unlock::ioHazardDrone);
    state.meta.ownedDroneIds.push_back(content::drone::hazardDrone);
    ensureDroneBayState(state, catalog);
    state.meta.droneBaySlots = 2;
    state.meta.equippedDroneIds = {
        content::drone::hazardDrone,
        content::drone::hazardDrone
    };
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied,
        "duplicate Hazard Drone fixture should start an Io mining run");

    MiningRunState& mining = state.run.mining;
    clearMiningTerrainForEvaTest(mining);
    mining.droneX = 12.5;
    mining.droneY = 6.5;
    require(mining.miniDrones.size() == 2,
        "both equipped Hazard Drone copies should deploy");
    MiningMiniDroneAgent& first = mining.miniDrones[0];
    MiningMiniDroneAgent& second = mining.miniDrones[1];
    require(
        first.role == MiniDroneRole::Hazard &&
            second.role == MiniDroneRole::Hazard &&
            first.roleIndex != second.roleIndex,
        "duplicate Hazard Drones should retain independent stable identities");
    first.x = second.x = mining.droneX;
    first.y = second.y = mining.droneY;
    first.velocityX = first.velocityY = 0.0;
    second.velocityX = second.velocityY = 0.0;

    const double hardness =
        miningMaterialToughness(MiningCellMaterial::HazardPocket, mining.depthZone);
    const auto placeHazard = [&](int x, int y, bool gateAssociated) {
        MiningCell* cell = miningCellAt(mining.terrain, x, y);
        require(cell != nullptr, "Hazard priority fixture cell should exist");
        *cell = {MiningCellMaterial::HazardPocket, hardness, hardness, true, true};
        cell->hazardAffinity = MiningElementalAffinity::Thermal;
        cell->gateAssociated = gateAssociated;
        return cell;
    };
    MiningCell* oldEntranceHazard = placeHazard(2, 6, false);
    MiningCell* nearbyHazard = placeHazard(11, 6, false);
    MiningCell* storySeal = placeHazard(20, 6, true);
    mining.artifact.present = true;
    mining.artifact.x = 21.5;
    mining.artifact.y = 6.5;

    updateMiningRun(state, catalog, 0.01);
    const auto targetsStorySeal = [&](const MiningMiniDroneAgent& agent) {
        return agent.targetCellX == 20 && agent.targetCellY == 6;
    };
    const auto targetsNearbyHazard = [&](const MiningMiniDroneAgent& agent) {
        return agent.targetCellX == 11 && agent.targetCellY == 6;
    };
    require(
        (targetsStorySeal(first) && targetsNearbyHazard(second)) ||
            (targetsStorySeal(second) && targetsNearbyHazard(first)),
        "the revealed story seal should win globally while the second drone takes the nearby ordinary hazard");
    require(
        first.targetCellX != 2 && second.targetCellX != 2,
        "an ordinary hazard near the entrance but outside command radius should remain unassigned");

    *oldEntranceHazard = {MiningCellMaterial::Empty, 0.0, 0.0, true, false};
    *nearbyHazard = {MiningCellMaterial::Empty, 0.0, 0.0, true, false};
    *storySeal = {MiningCellMaterial::Empty, 0.0, 0.0, true, false};
    updateMiningRun(state, catalog, 0.01);

    MiningCell* sharedTarget = placeHazard(13, 6, false);
    for (MiningMiniDroneAgent& agent : mining.miniDrones) {
        agent.x = 13.5;
        agent.y = 6.5;
        agent.velocityX = agent.velocityY = 0.0;
        agent.targetCellX = agent.targetCellY = -1;
        agent.taskProgressSeconds = 0.0;
        agent.actionCooldownSeconds = 0.0;
        agent.behavior = MiningMiniDroneBehavior::Following;
    }
    updateMiningRun(state, catalog, 0.01);
    require(
        first.targetCellX == 13 && first.targetCellY == 6 &&
            second.targetCellX == 13 && second.targetCellY == 6,
        "duplicate Hazard Drones should assist on one eligible tile when no distinct work remains");

    HazardDroneCoordinator coordinator(mining);
    coordinator.synchronizeAssignments();
    const MiniDroneCoordinationPoint firstApproach =
        coordinator.treatmentApproachPoint(first);
    const MiniDroneCoordinationPoint secondApproach =
        coordinator.treatmentApproachPoint(second);
    require(
        std::hypot(
            firstApproach.x - secondApproach.x,
            firstApproach.y - secondApproach.y) > 0.5,
        "assistants should receive distinct approach positions around their shared target");
    first.x = firstApproach.x;
    first.y = firstApproach.y;
    second.x = secondApproach.x;
    second.y = secondApproach.y;
    first.velocityX = first.velocityY = 0.0;
    second.velocityX = second.velocityY = 0.0;
    first.taskProgressSeconds = second.taskProgressSeconds = 0.0;

    for (int tick = 0; tick < 14; ++tick) {
        updateMiningRun(state, catalog, 0.05);
    }
    require(
        sharedTarget->material == MiningCellMaterial::HazardPocket,
        "two assistants should not finish before their exact two-times treatment duration");
    for (int tick = 0; tick < 2; ++tick) {
        updateMiningRun(state, catalog, 0.05);
    }
    require(
        sharedTarget->material != MiningCellMaterial::HazardPocket,
        "two working Hazard Drones should finish at exactly twice one-drone throughput");
}

void hazardDroneAssignmentsNormalizeAcrossSaveRoundTrips()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 93403);
    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    state.meta.unlockKeys.push_back(content::unlock::ioHazardDrone);
    state.meta.ownedDroneIds.push_back(content::drone::hazardDrone);
    ensureDroneBayState(state, catalog);
    state.meta.droneBaySlots = 2;
    state.meta.equippedDroneIds = {
        content::drone::hazardDrone,
        content::drone::hazardDrone
    };
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied,
        "Hazard save-normalization fixture should start an Io mining run");

    MiningRunState& mining = state.run.mining;
    clearMiningTerrainForEvaTest(mining);
    constexpr int targetX = 10;
    constexpr int targetY = 8;
    MiningCell* target = miningCellAt(mining.terrain, targetX, targetY);
    require(target != nullptr, "Hazard save-normalization target should exist");
    const double hardness =
        miningMaterialToughness(MiningCellMaterial::HazardPocket, mining.depthZone);
    *target = {
        MiningCellMaterial::HazardPocket,
        hardness,
        hardness,
        true,
        true
    };
    target->hazardAffinity = MiningElementalAffinity::Thermal;
    require(mining.miniDrones.size() == 2,
        "Hazard save-normalization fixture should deploy both copies");
    for (MiningMiniDroneAgent& agent : mining.miniDrones) {
        agent.targetCellX = targetX;
        agent.targetCellY = targetY;
        agent.taskProgressSeconds = 0.45;
        agent.behavior = MiningMiniDroneBehavior::Working;
    }

    const auto saved = deserializeSaveData(serializeSaveData(captureSaveData(state)));
    require(saved.has_value(), "valid Hazard assignments should serialize");
    GameState restored = createNewGame(catalog, 93404);
    restoreSaveData(restored, catalog, *saved);
    require(restored.run.mining.miniDrones.size() == 2,
        "restoring a duplicate Hazard squad should keep both independent agents");
    for (const MiningMiniDroneAgent& agent : restored.run.mining.miniDrones) {
        require(
            agent.role == MiniDroneRole::Hazard &&
                agent.targetCellX == targetX &&
                agent.targetCellY == targetY &&
                std::abs(agent.taskProgressSeconds - 0.45) < 0.0001,
            "a valid revealed Hazard target should preserve shared partial treatment progress");
    }
    require(
        restored.run.mining.miniDrones[0].roleIndex !=
            restored.run.mining.miniDrones[1].roleIndex,
        "restored duplicate Hazard Drones should retain distinct stable identities");

    MiningCell* restoredTarget =
        miningCellAt(restored.run.mining.terrain, targetX, targetY);
    require(restoredTarget != nullptr, "restored Hazard target should exist");
    restoredTarget->revealed = false;
    const auto hiddenSaved =
        deserializeSaveData(serializeSaveData(captureSaveData(restored)));
    require(hiddenSaved.has_value(), "hidden Hazard assignment fixture should serialize");
    GameState hiddenRestored = createNewGame(catalog, 93405);
    restoreSaveData(hiddenRestored, catalog, *hiddenSaved);
    for (const MiningMiniDroneAgent& agent : hiddenRestored.run.mining.miniDrones) {
        require(
            agent.targetCellX < 0 &&
                agent.targetCellY < 0 &&
                agent.taskProgressSeconds == 0.0 &&
                agent.behavior == MiningMiniDroneBehavior::Returning,
            "restoring a hidden Hazard target should clear obsolete work without losing the drone");
    }
}

void miningHazardAffinitiesApplyOnlyOnDrillContact()
{
    const ContentCatalog catalog = createDefaultContent();
    auto contactState = [&](MiningElementalAffinity affinity) {
        GameState state = createNewGame(catalog, 93100 + static_cast<int>(affinity));
        state.run.destinationIndex = 2;
        startSurfaceExpedition(state, catalog);
        prepareMiningSiteForTest(state);
        const MiningArenaRequest request = affinity == MiningElementalAffinity::Radiation
            ? MiningArenaRequest {MiningAct::ActThree, 2, 93100 + static_cast<std::uint64_t>(affinity)}
            : MiningArenaRequest {
                  MiningAct::ActOne,
                  affinity == MiningElementalAffinity::Toxic ? 9 : 7,
                  93100 + static_cast<std::uint64_t>(affinity)};
        require(startMiningRun(state, catalog, request, false).applied, "hazard contact test run should start at an affinity-enabled tier");
        MiningRunState& mining = state.run.mining;
        for (MiningCell& cell : mining.terrain.cells) {
            cell = {MiningCellMaterial::Empty, 0.0, 0.0, true, false};
        }
        mining.droneX = 32.0;
        mining.droneY = 8.0;
        MiningCell* hazard = miningCellAt(mining.terrain, 33, 8);
        require(hazard != nullptr, "hazard contact test cell should exist");
        *hazard = {MiningCellMaterial::HazardPocket, 100.0, 100.0, true, true};
        hazard->hazardAffinity = affinity;
        setMiningMove(state, 1.0, 0.0);
        setMiningMove(state, 0.0, 0.0);
        setMiningDrilling(state, true);
        updateMiningRun(state, catalog, 0.10);
        return state;
    };

    const GameState thermal = contactState(MiningElementalAffinity::Thermal);
    require(thermal.run.mining.drillHeat > 0.0, "thermal hazard contact should add drill heat");
    const GameState cryo = contactState(MiningElementalAffinity::Cryo);
    require(cryo.run.mining.movementSlowSeconds > 0.0 && cryo.run.mining.movementSlowScale < 1.0,
        "cryo hazard contact should slow the rig");
    const GameState toxic = contactState(MiningElementalAffinity::Toxic);
    require(toxic.run.mining.drillIntegrity < 1.0, "toxic hazard contact should damage drill integrity");
    const GameState radiation = contactState(MiningElementalAffinity::Radiation);
    require(radiation.run.mining.hazardDelta > 0.0, "radiation hazard contact should increase extraction hazard");

    GameState nearbyOnly = contactState(MiningElementalAffinity::Thermal);
    nearbyOnly.run.mining.drillHeat = 0.0;
    nearbyOnly.run.mining.droneX = 32.5;
    nearbyOnly.run.mining.droneY = 8.5;
    MiningCell* nearbyHazard = miningCellAt(nearbyOnly.run.mining.terrain, 33, 8);
    require(nearbyHazard != nullptr, "nearby thermal test cell should exist");
    *nearbyHazard = {MiningCellMaterial::HazardPocket, 100.0, 100.0, true, true};
    nearbyHazard->hazardAffinity = MiningElementalAffinity::Thermal;
    const double healthBeforeProximity = nearbyOnly.run.mining.droneHealth;
    setMiningDrilling(nearbyOnly, false);
    updateMiningRun(nearbyOnly, catalog, 0.20);
    require(nearbyOnly.run.mining.drillHeat > 0.0, "nearby thermal hazards should heat the rig even while it is not drilling");
    require(nearbyOnly.run.mining.droneHealth < healthBeforeProximity, "nearby thermal hazards should visibly damage rig health while the Hazard Drone is still treating them");
}


void miningAndSurveyDroneAgentsPerformWorldActions()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 91933);
    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    state.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
    ensureDroneBayState(state, catalog);
    state.meta.droneBaySlots = 2;
    state.meta.equippedDroneIds = {content::drone::miningDrone, content::drone::surveyDrone};
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(
        startMiningRun(state, catalog, {MiningAct::ActOne, 3, 91933}, false).applied,
        "mining and survey agent run should start with scanner mechanics enabled");
    state.run.mining.enemies.clear();
    state.run.mining.oxygenSeconds = 100.0;

    auto miningAgent = std::find_if(state.run.mining.miniDrones.begin(), state.run.mining.miniDrones.end(), [](const MiningMiniDroneAgent& agent) {
        return agent.role == MiniDroneRole::Mining;
    });
    auto surveyAgent = std::find_if(state.run.mining.miniDrones.begin(), state.run.mining.miniDrones.end(), [](const MiningMiniDroneAgent& agent) {
        return agent.role == MiniDroneRole::Survey;
    });
    require(miningAgent != state.run.mining.miniDrones.end() && surveyAgent != state.run.mining.miniDrones.end(),
        "equipped mining and survey roles should create agents");

    for (MiningCell& cell : state.run.mining.terrain.cells) {
        cell = {};
        cell.revealed = true;
    }
    state.run.mining.gravityStrength = 0.0;
    const int firstOreX = std::clamp(static_cast<int>(std::floor(state.run.mining.droneX)) - 2, 1, state.run.mining.terrain.width - 2);
    const int secondOreX = std::clamp(static_cast<int>(std::floor(state.run.mining.droneX)) + 2, 1, state.run.mining.terrain.width - 2);
    const int oreY = std::clamp(static_cast<int>(std::floor(state.run.mining.droneY)) + 2, 1, state.run.mining.terrain.height - 2);
    *miningCellAt(state.run.mining.terrain, firstOreX, oreY) = {MiningCellMaterial::CommonOre, 3.0, 3.0, true, false};
    *miningCellAt(state.run.mining.terrain, secondOreX, oreY) = {MiningCellMaterial::CommonOre, 3.0, 3.0, true, false};
    updateMiningRun(state, catalog, 0.08);
    std::vector<std::pair<int, int>> miningTargets;
    for (const MiningMiniDroneAgent& agent : state.run.mining.miniDrones) {
        if (agent.role == MiniDroneRole::Mining) {
            miningTargets.push_back({agent.targetCellX, agent.targetCellY});
        }
    }
    require(miningTargets.size() == 1 && miningTargets.front().first >= 0,
        "the Prospector Support Drone should acquire real terrain work");

    const int targetX = miningAgent->targetCellX;
    const int targetY = miningAgent->targetCellY;
    MiningCell* target = miningCellAt(state.run.mining.terrain, targetX, targetY);
    require(target != nullptr, "mining agent test target should exist");
    target->material = MiningCellMaterial::CommonOre;
    target->maxToughness = 0.05;
    target->remainingToughness = 0.05;
    target->revealed = true;
    target->hazard = false;
    miningAgent->x = static_cast<double>(targetX) + 0.25;
    miningAgent->y = static_cast<double>(targetY) + 0.5;
    miningAgent->targetCellX = targetX;
    miningAgent->targetCellY = targetY;
    miningAgent->behavior = MiningMiniDroneBehavior::Working;
    miningAgent->taskProgressSeconds = 0.0;
    const int brokenBefore = state.run.mining.cellsBroken;
    const double miningWorkSeconds = tuning::mining::miningDroneWorkSeconds(
        miningAgent->upgradeLevel,
        MiningCellMaterial::CommonOre);
    double elapsedMiningWork = 0.0;
    while (elapsedMiningWork + 0.08 < miningWorkSeconds) {
        updateMiningRun(state, catalog, 0.08);
        elapsedMiningWork += 0.08;
    }
    require(miningCellAt(state.run.mining.terrain, targetX, targetY)->material == MiningCellMaterial::CommonOre,
        "Mining drones should spend their full work cycle on an assigned terrain cell");
    for (int step = 0; step < 3 &&
        miningCellAt(state.run.mining.terrain, targetX, targetY)->material != MiningCellMaterial::Empty; ++step) {
        updateMiningRun(state, catalog, 0.08);
    }
    require(miningCellAt(state.run.mining.terrain, targetX, targetY)->material == MiningCellMaterial::Empty,
        "Mining drone should break its assigned terrain cell instead of granting synthetic materials");
    require(state.run.mining.cellsBroken == brokenBefore + 1,
        "Mining drone terrain work should use the shared cell-break accounting");
    require(
        (miningAgent->behavior == MiningMiniDroneBehavior::Following ||
            miningAgent->behavior == MiningMiniDroneBehavior::Returning) &&
            miningAgent->targetCellX < 0,
        "a Mining drone should resume or return to its controlled-actor orbit after completing local work");

    target->material = MiningCellMaterial::CommonOre;
    target->maxToughness = 3.0;
    target->remainingToughness = 3.0;
    target->revealed = true;
    miningAgent->targetCellX = targetX;
    miningAgent->targetCellY = targetY;
    miningAgent->behavior = MiningMiniDroneBehavior::Working;
    miningAgent->taskProgressSeconds = miningWorkSeconds * 0.75;
    state.run.mining.droneX = std::min(
        static_cast<double>(state.run.mining.terrain.width - 2),
        miningAgent->x + tuning::mining::miningDroneLeashRadiusCells + 1.0);
    updateMiningRun(state, catalog, 0.08);
    require(
        target->material == MiningCellMaterial::CommonOre &&
            miningAgent->behavior == MiningMiniDroneBehavior::Working &&
            miningAgent->finishTargetBeforeReturn,
        "moving the rig beyond the leash should commit the Prospector to its current ore");
    for (int step = 0; step < 80 && target->material != MiningCellMaterial::Empty; ++step) {
        updateMiningRun(state, catalog, 0.08);
    }
    require(
        target->material == MiningCellMaterial::Empty &&
            miningAgent->behavior == MiningMiniDroneBehavior::Returning &&
            miningAgent->targetCellX < 0 &&
            miningAgent->targetCellY < 0,
        "a committed Prospector should finish exactly one ore before returning to the moved rig");
    require(miningAgent->returnPathFailureSeconds == 0.0,
        "a reachable Prospector return must not start the safe-recall timer");

    const int scanX = std::clamp(static_cast<int>(std::floor(state.run.mining.droneX)), 1, state.run.mining.terrain.width - 2);
    const int scanY = std::clamp(static_cast<int>(std::floor(state.run.mining.droneY + 10.0)), 1, state.run.mining.terrain.height - 2);
    surveyAgent->x = static_cast<double>(scanX) + 0.5;
    surveyAgent->y = static_cast<double>(scanY) - 1.0;
    MiningCell* remoteCell = miningCellAt(state.run.mining.terrain, scanX, scanY);
    require(remoteCell != nullptr, "remote survey cell should exist");
    remoteCell->revealed = false;
    require(std::hypot(
        static_cast<double>(scanX) + 0.5 - state.run.mining.droneX,
        static_cast<double>(scanY) + 0.5 - state.run.mining.droneY) > miningDrillStats(state, catalog).scannerRadius,
        "survey test cell should sit outside the main rig scan radius");
    pulseMiningScanner(state, catalog);
    require(remoteCell->revealed,
        "Survey drone should add its own remote scanner origin to the pulse reveal");
}

void prospectorSafeRecallRecoversFromBlockedReturnPath()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 91934);
    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    ensureDroneBayState(state, catalog);
    state.meta.droneBaySlots = 1;
    state.meta.equippedDroneIds = {content::drone::miningDrone};
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog, {MiningAct::ActOne, 3, 91934}, false).applied,
        "safe-recall fixture should start an active mining run");

    MiningRunState& mining = state.run.mining;
    mining.gravityStrength = 0.0;
    mining.oxygenSeconds = 100.0;
    for (MiningCell& cell : mining.terrain.cells) {
        cell = {};
        cell.material = MiningCellMaterial::Bedrock;
        cell.revealed = true;
    }
    const int strandedX = 5;
    const int strandedY = 10;
    const int rigX = 25;
    const int rigY = 10;
    *miningCellAt(mining.terrain, strandedX, strandedY) = {};
    *miningCellAt(mining.terrain, rigX, rigY) = {};
    mining.droneX = static_cast<double>(rigX) + 0.5;
    mining.droneY = static_cast<double>(rigY) + 0.5;
    mining.rigDepthZone = mining.depthZone;
    MiningMiniDroneAgent& prospector = mining.miniDrones.front();
    prospector.x = static_cast<double>(strandedX) + 0.5;
    prospector.y = static_cast<double>(strandedY) + 0.5;
    prospector.velocityX = 0.0;
    prospector.velocityY = 0.0;
    prospector.behavior = MiningMiniDroneBehavior::Returning;
    prospector.haulMaterials.common = 1;
    prospector.uncreditedHaulMaterials.common = 1;

    for (int step = 0;
         step < 20 && prospector.behavior != MiningMiniDroneBehavior::RecoveringToRig;
         ++step) {
        updateMiningRun(state, catalog, 0.10);
    }
    require(
        prospector.behavior == MiningMiniDroneBehavior::RecoveringToRig &&
            prospector.returnPathFailureSeconds >=
                tuning::mining::miningDroneReturnPathFailureSeconds &&
            prospector.haulMaterials.common == 1 &&
            prospector.uncreditedHaulMaterials.common == 1,
        "a Prospector stranded behind solid terrain should safely recall without losing its cargo");

    Random panelRng(91934);
    const PreparedLaunch panelLaunch = prepareLaunch(state, catalog, panelRng);
    const std::string recallingPanel = buildGamePanelHtml({state, catalog, panelLaunch, panelLaunch});
    require(recallingPanel.find("SAFE RECALLING TO RIG") != std::string::npos,
        "the Mining HUD should explain that a stranded Prospector is using safe recall");

    const auto save = deserializeSaveData(serializeSaveData(captureSaveData(state)));
    require(save.has_value(), "safe-recall mining save should parse");
    GameState restored = createNewGame(catalog, 91935);
    restoreSaveData(restored, catalog, *save);
    const auto restoredProspector = std::find_if(
        restored.run.mining.miniDrones.begin(),
        restored.run.mining.miniDrones.end(),
        [](const MiningMiniDroneAgent& agent) {
            return agent.role == MiniDroneRole::Mining;
        });
    require(
        restoredProspector != restored.run.mining.miniDrones.end() &&
            restoredProspector->behavior == MiningMiniDroneBehavior::RecoveringToRig &&
            restoredProspector->returnPathFailureSeconds >=
                tuning::mining::miningDroneReturnPathFailureSeconds &&
            restoredProspector->haulMaterials.common == 1,
        "safe-recall behavior, timer, and cargo should survive an active version-14 save");

    for (int step = 0;
         step < 40 && prospector.behavior == MiningMiniDroneBehavior::RecoveringToRig;
         ++step) {
        updateMiningRun(state, catalog, 0.10);
    }
    require(
        prospector.behavior != MiningMiniDroneBehavior::RecoveringToRig &&
            std::hypot(prospector.x - mining.droneX, prospector.y - mining.droneY) < 1.0 &&
            prospector.haulMaterials.common == 1,
        "safe recall should return the Prospector to an open rig rally point with cargo intact");
}

void surveyDroneRunsAnchoredPriorityScanCycles()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 91936);
    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    state.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
    ensureDroneBayState(state, catalog);
    state.meta.droneBaySlots = 1;
    state.meta.equippedDroneIds = {content::drone::surveyDrone};
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(
        startMiningRun(state, catalog, {MiningAct::ActOne, 3, 91936}, false).applied,
        "survey cycle mining run should start with scanner mechanics enabled");
    state.run.mining.enemies.clear();
    state.run.mining.oxygenSeconds = 100.0;

    for (MiningCell& cell : state.run.mining.terrain.cells) {
        cell = {};
        cell.revealed = true;
    }
    state.run.mining.gravityStrength = 0.0;
    const int anchorX = std::clamp(static_cast<int>(std::floor(state.run.mining.droneX)), 6, state.run.mining.terrain.width - 7);
    const int anchorY = std::clamp(static_cast<int>(std::floor(state.run.mining.droneY)), 2, state.run.mining.terrain.height - 13);
    state.run.mining.droneX = static_cast<double>(anchorX) + 0.5;
    state.run.mining.droneY = static_cast<double>(anchorY) + 0.5;
    const int artifactX = anchorX - 4;
    const int artifactY = anchorY + 3;
    const int exoticX = anchorX + 4;
    const int exoticY = anchorY + 10;
    const int rareX = anchorX - 2;
    const int rareY = anchorY + 8;
    *miningCellAt(state.run.mining.terrain, artifactX, artifactY) =
        {MiningCellMaterial::ArtifactCache, 4.0, 4.0, false, false};
    *miningCellAt(state.run.mining.terrain, exoticX, exoticY) =
        {MiningCellMaterial::ExoticVein, 4.0, 4.0, false, false};
    *miningCellAt(state.run.mining.terrain, rareX, rareY) =
        {MiningCellMaterial::RareOre, 4.0, 4.0, false, false};

    updateMiningRun(state, catalog, 0.05);
    auto survey = std::find_if(state.run.mining.miniDrones.begin(), state.run.mining.miniDrones.end(), [](const MiningMiniDroneAgent& agent) {
        return agent.role == MiniDroneRole::Survey;
    });
    require(survey != state.run.mining.miniDrones.end(), "survey cycle should create a Survey drone agent");
    require(survey->targetCellX == artifactX && survey->targetCellY == artifactY,
        "Survey drone should prioritize an anchored artifact signature over other materials");
    require(survey->targetCellY > state.run.mining.droneY &&
        std::abs(static_cast<double>(survey->targetCellX) + 0.5 - state.run.mining.droneX) <= tuning::mining::surveyDroneAnchorHalfWidthCells,
        "Survey target should remain ahead of and laterally anchored to the main rig");

    survey->x = static_cast<double>(artifactX) + 0.5;
    survey->y = static_cast<double>(artifactY) + 0.5;
    survey->velocityX = 0.0;
    survey->velocityY = 0.0;
    updateMiningRun(state, catalog, 0.05);
    require(!miningCellAt(state.run.mining.terrain, artifactX, artifactY)->revealed &&
        survey->behavior == MiningMiniDroneBehavior::Scouting && survey->taskProgressSeconds > 0.0,
        "Survey drones should settle at a target before firing their local scan pulse");
    for (int step = 0; step < 10 && !miningCellAt(state.run.mining.terrain, artifactX, artifactY)->revealed; ++step) {
        updateMiningRun(state, catalog, 0.05);
    }
    require(miningCellAt(state.run.mining.terrain, artifactX, artifactY)->revealed,
        "Survey drone arrival should pulse-reveal a local area");
    require(survey->targetCellX < 0 && survey->actionCooldownSeconds > 0.0 &&
        survey->behavior == MiningMiniDroneBehavior::Returning,
        "Survey drone should clear its assignment and recharge while returning toward the rig");

    const MiniDroneCoordinationPoint rechargedHome =
        miniDroneOrbitPoint(state.run.mining, *survey);
    survey->x = rechargedHome.x;
    survey->y = rechargedHome.y;
    survey->velocityX = 0.0;
    survey->velocityY = 0.0;
    survey->actionCooldownSeconds = 0.0;
    survey->behavior = MiningMiniDroneBehavior::Following;
    updateMiningRun(state, catalog, 0.05);
    require(survey->targetCellX == exoticX && survey->targetCellY == exoticY,
        "recharged Survey drone should choose the next highest-value deeper anchored signature");
    require(survey->targetCellY > artifactY,
        "successive Survey assignments should progress deeper when the next priority signature is deeper");

    for (MiningCell& cell : state.run.mining.terrain.cells) {
        cell.revealed = true;
    }
    survey->targetCellX = -1;
    survey->targetCellY = -1;
    survey->actionCooldownSeconds = 0.0;
    const MiniDroneCoordinationPoint idleHome =
        miniDroneOrbitPoint(state.run.mining, *survey);
    survey->x = idleHome.x;
    survey->y = idleHome.y;
    survey->velocityX = 0.0;
    survey->velocityY = 0.0;
    survey->behavior = MiningMiniDroneBehavior::Following;
    updateMiningRun(state, catalog, 0.05);
    require(survey->behavior == MiningMiniDroneBehavior::Scouting && survey->actionCooldownSeconds > 0.0,
        "Survey drone should pulse from its active-actor orbit and recharge when no forward signature remains");
    require(survey->surveyPulseSeconds > 0.0,
        "autonomous Survey pulses should expose their local scanner presentation state");

    const SaveData save = captureSaveData(state);
    GameState restored = createNewGame(catalog, 91937);
    restoreSaveData(restored, catalog, save);
    const auto restoredSurvey = std::find_if(restored.run.mining.miniDrones.begin(), restored.run.mining.miniDrones.end(), [](const MiningMiniDroneAgent& agent) {
        return agent.role == MiniDroneRole::Survey;
    });
    require(restoredSurvey != restored.run.mining.miniDrones.end() &&
        std::abs(restoredSurvey->surveyPulseSeconds - survey->surveyPulseSeconds) < 0.000001 &&
        std::abs(restoredSurvey->actionCooldownSeconds - survey->actionCooldownSeconds) < 0.000001,
        "Survey pulse and recharge state should survive an active mining save");
}

void surveyDronesMaintainCoordinatedSearchLanes()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 91943);
    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    state.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
    ensureDroneBayState(state, catalog);
    state.meta.droneBaySlots = 5;
    state.meta.equippedDroneIds = {
        content::drone::surveyDrone,
        content::drone::surveyDrone,
        content::drone::surveyDrone,
        content::drone::surveyDrone,
        content::drone::surveyDrone
    };
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(
        startMiningRun(state, catalog, {MiningAct::ActOne, 3, 91937}, false).applied,
        "coordinated Survey drone run should start with scanner mechanics enabled");
    state.run.mining.enemies.clear();
    state.run.mining.oxygenSeconds = 100.0;

    std::vector<MiningMiniDroneAgent*> surveyDrones;
    for (MiningMiniDroneAgent& agent : state.run.mining.miniDrones) {
        if (agent.role == MiniDroneRole::Survey) {
            surveyDrones.push_back(&agent);
        }
    }
    std::sort(surveyDrones.begin(), surveyDrones.end(), [](const MiningMiniDroneAgent* lhs, const MiningMiniDroneAgent* rhs) {
        return lhs->roleIndex < rhs->roleIndex;
    });
    require(surveyDrones.size() == 5, "duplicate Survey drones should each create an independent agent");
    for (std::size_t i = 0; i < surveyDrones.size(); ++i) {
        const MiniDroneCoordinationPoint expectedOrbit =
            miniDroneOrbitPoint(state.run.mining, *surveyDrones[i]);
        require(
            surveyDrones[i]->stableFormationSlot == static_cast<int>(i),
            "Survey drone idle stations should retain stable count-aware formation slots");
        require(
            std::hypot(
                surveyDrones[i]->x - expectedOrbit.x,
                surveyDrones[i]->y - expectedOrbit.y) < 0.000001,
            "Survey drone idle stations should initialize at their terrain-projected role orbit");
        for (std::size_t earlier = 0; earlier < i; ++earlier) {
            require(
                std::hypot(
                    surveyDrones[i]->x - surveyDrones[earlier]->x,
                    surveyDrones[i]->y - surveyDrones[earlier]->y) >
                    tuning::mining::miniDroneSameRoleSpacingCells,
                "idle Survey drones should remain visibly separated around their shared orbit");
        }
    }

    for (MiningCell& cell : state.run.mining.terrain.cells) {
        cell = {};
        cell.revealed = true;
    }
    state.run.mining.gravityStrength = 0.0;
    const int targetY = std::clamp(
        static_cast<int>(std::floor(state.run.mining.droneY + 7.0)),
        1,
        state.run.mining.terrain.height - 2);
    for (std::size_t i = 0; i < surveyDrones.size(); ++i) {
        const double laneCenter = state.run.mining.droneX + tuning::mining::surveyDroneFormationOffsetCells(
            static_cast<int>(i),
            static_cast<int>(surveyDrones.size()));
        const int laneX = std::clamp(
            static_cast<int>(std::floor(laneCenter)),
            1,
            state.run.mining.terrain.width - 2);
        *miningCellAt(state.run.mining.terrain, laneX, targetY) =
            {MiningCellMaterial::CommonOre, 4.0, 4.0, false, false};
        surveyDrones[i]->targetCellX = -1;
        surveyDrones[i]->targetCellY = -1;
        surveyDrones[i]->behavior = MiningMiniDroneBehavior::Following;
        surveyDrones[i]->actionCooldownSeconds = 0.0;
        surveyDrones[i]->velocityX = 0.0;
        surveyDrones[i]->velocityY = 0.0;
    }
    std::vector<std::pair<double, double>> positionsBeforeAssignment;
    for (const MiningMiniDroneAgent* agent : surveyDrones) {
        positionsBeforeAssignment.push_back({agent->x, agent->y});
    }
    updateMiningRun(state, catalog, 0.05);
    for (std::size_t i = 0; i < surveyDrones.size(); ++i) {
        const double laneCenter = state.run.mining.droneX + tuning::mining::surveyDroneFormationOffsetCells(
            static_cast<int>(i),
            static_cast<int>(surveyDrones.size()));
        require(surveyDrones[i]->targetCellX >= 0 &&
            std::abs(static_cast<double>(surveyDrones[i]->targetCellX) + 0.5 - laneCenter) <=
                tuning::mining::surveyDroneSearchLaneHalfWidthCells,
            "Survey drones should acquire unrevealed signatures inside their assigned search lane");
        if (i > 0) {
            require(surveyDrones[i]->targetCellX > surveyDrones[i - 1]->targetCellX,
                "Survey target assignments should preserve the formation's left-to-right lane order");
        }
        const double displacement = std::hypot(
            surveyDrones[i]->x - positionsBeforeAssignment[i].first,
            surveyDrones[i]->y - positionsBeforeAssignment[i].second);
        require(displacement > 0.0 && displacement < 0.05,
            "Survey drones should accelerate deliberately instead of snapping toward new scan targets");
    }

    for (MiningCell& cell : state.run.mining.terrain.cells) {
        cell.revealed = true;
    }
    for (int step = 0; step < 180; ++step) {
        updateMiningRun(state, catalog, 0.05);
    }
    for (std::size_t i = 0; i < surveyDrones.size(); ++i) {
        const MiniDroneCoordinationPoint expectedOrbit =
            miniDroneOrbitPoint(state.run.mining, *surveyDrones[i]);
        require(
            std::hypot(
                surveyDrones[i]->x - expectedOrbit.x,
                surveyDrones[i]->y - expectedOrbit.y) < 0.65,
            "idle Survey drones should settle back onto their moving active-actor orbit");
        for (std::size_t earlier = 0; earlier < i; ++earlier) {
            require(
                std::hypot(
                    surveyDrones[i]->x - surveyDrones[earlier]->x,
                    surveyDrones[i]->y - surveyDrones[earlier]->y) >
                    tuning::mining::miniDroneSameRoleSpacingCells,
                "idle Survey drones should preserve their stable separation around the orbit");
        }
    }
}

void resourceDroneRunsTimedMaterialShuttles()
{
    const ContentCatalog catalog = createDefaultContent();
    auto createResourceRun = [&](std::uint64_t seed, int upgradeLevel) {
        GameState state = createNewGame(catalog, seed);
        state.meta.unlockKeys.push_back(content::unlock::droneBay);
        state.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
        ensureDroneBayState(state, catalog);
        state.meta.droneBaySlots = 1;
        state.meta.equippedDroneIds = {content::drone::resourceDrone};
        state.run.destinationIndex = 2;
        startSurfaceExpedition(state, catalog);
        state.run.surfaceExpedition.runDroneRanks = {{content::drone::resourceDrone, upgradeLevel}};
        prepareMiningSiteForTest(state);
        require(startMiningRun(state, catalog).applied, "resource shuttle mining run should start");
        clearMiningTerrainForEvaTest(state.run.mining);
        state.run.mining.droneX = std::min(
            static_cast<double>(state.run.mining.terrain.width - 4),
            state.run.mining.returnZoneX +
                tuning::mining::returnZoneRadiusCells + 4.0);
        state.run.mining.droneY = state.run.mining.returnZoneY;
        return state;
    };

    GameState state = createResourceRun(91938, 1);
    auto resource = std::find_if(state.run.mining.miniDrones.begin(), state.run.mining.miniDrones.end(), [](const MiningMiniDroneAgent& agent) {
        return agent.role == MiniDroneRole::Resource;
    });
    require(resource != state.run.mining.miniDrones.end(), "resource shuttle should create a Resource drone agent");
    resource->x = state.run.mining.droneX + tuning::mining::resourceDroneCollectionRadiusCells;
    resource->y = state.run.mining.droneY;
    resource->velocityX = 0.0;
    resource->velocityY = 0.0;
    resource->behavior = MiningMiniDroneBehavior::Following;
    state.run.mining.temporaryMaterials = {.common = 4, .rare = 2, .exotic = 2};
    state.run.mining.cargo = 4 * tuning::mining::commonCargo +
        2 * tuning::mining::rareCargo + 2 * tuning::mining::exoticCargo;

    updateMiningRun(state, catalog, 0.05);
    require(resource->behavior == MiningMiniDroneBehavior::Working &&
        resource->actionCooldownSeconds > 0.0 &&
        resource->haulMaterials.common + resource->haulMaterials.rare + resource->haulMaterials.exotic == 0,
        "Resource drone should simulate fill time before loading its first chunk");
    const double mk1TransferDelay = resource->actionCooldownSeconds;
    for (int step = 0; step < 10 && resource->haulMaterials.exotic == 0; ++step) {
        updateMiningRun(state, catalog, 0.08);
    }
    require(resource->haulMaterials.exotic == 1 && state.run.mining.temporaryMaterials.exotic == 1,
        "Resource drone should load exactly one color-coded material chunk per transfer interval");
    require(state.run.mining.cargo == 4 * tuning::mining::commonCargo +
        2 * tuning::mining::rareCargo + tuning::mining::exoticCargo,
        "loading a Resource drone should remove that chunk's cargo mass from the main rig");

    const SaveData transitSave = captureSaveData(state);
    GameState restoredTransit = createNewGame(catalog, 91939);
    restoreSaveData(restoredTransit, catalog, transitSave);
    const auto restoredResource = std::find_if(restoredTransit.run.mining.miniDrones.begin(), restoredTransit.run.mining.miniDrones.end(), [](const MiningMiniDroneAgent& agent) {
        return agent.role == MiniDroneRole::Resource;
    });
    require(restoredResource != restoredTransit.run.mining.miniDrones.end() &&
        restoredResource->haulMaterials.exotic == resource->haulMaterials.exotic,
        "Resource drone in-transit manifest should survive an active mining save");

    for (int step = 0; step < 160 &&
        resource->haulMaterials.common + resource->haulMaterials.rare +
            resource->haulMaterials.exotic <
            tuning::mining::resourceDroneCapacityChunks; ++step) {
        updateMiningRun(state, catalog, 0.08);
    }
    updateMiningRun(state, catalog, 0.08);
    require(resource->behavior == MiningMiniDroneBehavior::DeliveringToShip &&
        resource->haulMaterials.common + resource->haulMaterials.rare + resource->haulMaterials.exotic ==
            tuning::mining::resourceDroneCapacityChunks,
        "a full Resource drone should begin autonomous delivery without waiting for the active actor");
    const int stowedBeforeUnload = state.run.mining.stowedMaterials.common +
        state.run.mining.stowedMaterials.rare + state.run.mining.stowedMaterials.exotic;
    for (int step = 0; step < 160 && resource->behavior == MiningMiniDroneBehavior::DeliveringToShip; ++step) {
        updateMiningRun(state, catalog, 0.08);
    }
    require(state.run.mining.stowedMaterials.common + state.run.mining.stowedMaterials.rare +
        state.run.mining.stowedMaterials.exotic == stowedBeforeUnload + tuning::mining::resourceDroneCapacityChunks,
        "Resource drone should unload its complete manifest after deterministic Ship transit");
    require(resource->haulMaterials.common + resource->haulMaterials.rare + resource->haulMaterials.exotic == 0,
        "Resource drone should clear its manifest at the ship");
    require(state.run.mining.stowedMaterials.common == 4 &&
        state.run.mining.stowedMaterials.rare == 2 &&
        state.run.mining.stowedMaterials.exotic == 2,
        "Resource drone drop-off should bank the exact transported material mix");

    GameState upgraded = createResourceRun(91940, 3);
    auto upgradedResource = std::find_if(upgraded.run.mining.miniDrones.begin(), upgraded.run.mining.miniDrones.end(), [](const MiningMiniDroneAgent& agent) {
        return agent.role == MiniDroneRole::Resource;
    });
    require(upgradedResource != upgraded.run.mining.miniDrones.end(), "upgraded Resource drone should create an agent");
    upgradedResource->x = upgraded.run.mining.droneX + tuning::mining::resourceDroneCollectionRadiusCells;
    upgradedResource->y = upgraded.run.mining.droneY;
    upgradedResource->velocityX = 0.0;
    upgradedResource->velocityY = 0.0;
    upgraded.run.mining.temporaryMaterials.common = 1;
    upgraded.run.mining.cargo = tuning::mining::commonCargo;
    updateMiningRun(upgraded, catalog, 0.05);
    require(upgradedResource->actionCooldownSeconds < mk1TransferDelay,
        "Resource drone upgrades should reduce the per-chunk load and drop-off interval");
}

void resourceDronesCollectInMovingFormation()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 91944);
    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    state.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
    ensureDroneBayState(state, catalog);
    state.meta.droneBaySlots = 5;
    state.meta.equippedDroneIds = {
        content::drone::resourceDrone,
        content::drone::resourceDrone,
        content::drone::resourceDrone,
        content::drone::resourceDrone,
        content::drone::resourceDrone
    };
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "moving Resource collection run should start");
    state.run.mining.enemies.clear();
    state.run.mining.oxygenSeconds = 100.0;

    std::vector<MiningMiniDroneAgent*> resourceDrones;
    for (MiningMiniDroneAgent& agent : state.run.mining.miniDrones) {
        if (agent.role == MiniDroneRole::Resource) {
            resourceDrones.push_back(&agent);
        }
    }
    std::sort(resourceDrones.begin(), resourceDrones.end(), [](const MiningMiniDroneAgent* lhs, const MiningMiniDroneAgent* rhs) {
        return lhs->roleIndex < rhs->roleIndex;
    });
    require(resourceDrones.size() == 5, "duplicate Resource drones should each create an independent agent");
    clearMiningTerrainForEvaTest(state.run.mining);
    state.run.mining.droneX = std::min(
        static_cast<double>(state.run.mining.terrain.width - 4),
        state.run.mining.returnZoneX +
            tuning::mining::returnZoneRadiusCells + 4.0);
    state.run.mining.droneY = state.run.mining.returnZoneY;
    state.run.mining.rigVelocityX = 0.0;
    state.run.mining.rigVelocityY = 0.0;
    for (MiningMiniDroneAgent* agent : resourceDrones) {
        const MiniDroneCoordinationPoint orbit =
            miniDroneOrbitPoint(state.run.mining, *agent);
        agent->x = orbit.x;
        agent->y = orbit.y;
        agent->velocityX = state.run.mining.rigVelocityX;
        agent->velocityY = state.run.mining.rigVelocityY;
        agent->behavior = MiningMiniDroneBehavior::Following;
    }
    for (const MiningMiniDroneAgent* agent : resourceDrones) {
        require(std::abs(std::hypot(
            agent->x - state.run.mining.droneX,
            agent->y - state.run.mining.droneY) - tuning::mining::resourceDroneCollectionRadiusCells) < 0.001,
            "Resource drones should initialize on the close collection ring");
    }
    for (std::size_t lhs = 0; lhs < resourceDrones.size(); ++lhs) {
        for (std::size_t rhs = lhs + 1; rhs < resourceDrones.size(); ++rhs) {
            require(std::hypot(
                resourceDrones[lhs]->x - resourceDrones[rhs]->x,
                resourceDrones[lhs]->y - resourceDrones[rhs]->y) >=
                    tuning::mining::resourceDroneMinimumSpacingCells,
                "Resource collection slots should prevent drones from stacking on the rig");
        }
    }

    state.run.mining.temporaryMaterials.common = 30;
    state.run.mining.cargo = 30 * tuning::mining::commonCargo;
    state.run.mining.rigVelocityX = 0.5;
    for (int step = 0; step < 32; ++step) {
        updateMiningRun(state, catalog, 0.05);
    }

    int collectedChunks = 0;
    for (const MiningMiniDroneAgent* agent : resourceDrones) {
        collectedChunks += agent->haulMaterials.common;
        require(agent->haulMaterials.common > 0 && agent->behavior == MiningMiniDroneBehavior::Working,
            "each Resource drone should keep collecting while its moving formation tracks the rig");
        require(std::hypot(agent->x - state.run.mining.droneX, agent->y - state.run.mining.droneY) < 2.55,
            "Resource drones should remain attached to the rig collection perimeter while it moves");
    }
    require(collectedChunks > 0 && state.run.mining.temporaryMaterials.common == 30 - collectedChunks,
        "moving Resource drones should transfer real material chunks without waiting for the rig to stop");
    for (std::size_t lhs = 0; lhs < resourceDrones.size(); ++lhs) {
        for (std::size_t rhs = lhs + 1; rhs < resourceDrones.size(); ++rhs) {
            require(std::hypot(
                resourceDrones[lhs]->x - resourceDrones[rhs]->x,
                resourceDrones[lhs]->y - resourceDrones[rhs]->y) > 1.35,
                "moving Resource drones should preserve their individual ring positions");
        }
    }
}

void miningDroneRunsTimedCapacityShuttles()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 91941);
    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    state.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
    ensureDroneBayState(state, catalog);
    state.meta.droneBaySlots = 1;
    state.meta.equippedDroneIds = {content::drone::miningDrone};
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    state.run.surfaceExpedition.runDroneRanks = {{content::drone::miningDrone, 1}};
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "mining shuttle run should start");
    state.run.mining.enemies.clear();
    state.run.mining.oxygenSeconds = 100.0;

    auto miningDrone = std::find_if(state.run.mining.miniDrones.begin(), state.run.mining.miniDrones.end(), [](const MiningMiniDroneAgent& agent) {
        return agent.role == MiniDroneRole::Mining;
    });
    require(miningDrone != state.run.mining.miniDrones.end(), "mining shuttle run should create a Mining drone agent");
    require(tuning::mining::miningDroneCapacityChunks(1) == 3 &&
        tuning::mining::miningDroneCapacityChunks(2) == 5 &&
        tuning::mining::miningDroneCapacityChunks(3) == 7,
        "Mining drone upgrades should increase haul capacity from three to five to seven chunks");
    require(
        tuning::mining::miningDroneWorkSeconds(3, MiningCellMaterial::CommonOre) <
            tuning::mining::miningDroneWorkSeconds(2, MiningCellMaterial::CommonOre) &&
        tuning::mining::miningDroneWorkSeconds(2, MiningCellMaterial::CommonOre) <
            tuning::mining::miningDroneWorkSeconds(1, MiningCellMaterial::CommonOre),
        "Mining drone upgrades should reduce the per-cell mining cycle");

    for (MiningCell& cell : state.run.mining.terrain.cells) {
        cell = {};
        cell.revealed = true;
    }
    state.run.mining.gravityStrength = 0.0;
    state.run.mining.droneX = std::min(
        static_cast<double>(state.run.mining.terrain.width - 4),
        state.run.mining.returnZoneX +
            tuning::mining::returnZoneRadiusCells + 4.0);
    state.run.mining.droneY = state.run.mining.returnZoneY;
    const int targetX = std::clamp(
        static_cast<int>(std::floor(state.run.mining.droneX)) + 1,
        1,
        state.run.mining.terrain.width - 2);
    const int targetY = std::clamp(
        static_cast<int>(std::floor(state.run.mining.droneY)) + 1,
        1,
        state.run.mining.terrain.height - 2);
    *miningCellAt(state.run.mining.terrain, targetX, targetY) =
        {MiningCellMaterial::CommonOre, 3.0, 3.0, true, false};
    miningDrone->x = static_cast<double>(targetX) + 0.25;
    miningDrone->y = static_cast<double>(targetY) + 0.5;
    miningDrone->velocityX = 0.0;
    miningDrone->velocityY = 0.0;
    miningDrone->targetCellX = targetX;
    miningDrone->targetCellY = targetY;
    miningDrone->behavior = MiningMiniDroneBehavior::Working;
    miningDrone->taskProgressSeconds = 0.0;

    updateMiningRun(state, catalog, 0.10);
    require(miningDrone->taskProgressSeconds > 0.0 &&
        miningCellAt(state.run.mining.terrain, targetX, targetY)->material == MiningCellMaterial::CommonOre,
        "Mining drones should visibly work over time instead of instantly breaking ore");
    miningDrone->haulMaterials.rare = 1;
    const SaveData activeSave = captureSaveData(state);
    GameState restored = createNewGame(catalog, 91942);
    restoreSaveData(restored, catalog, activeSave);
    const auto restoredMiningDrone = std::find_if(restored.run.mining.miniDrones.begin(), restored.run.mining.miniDrones.end(), [](const MiningMiniDroneAgent& agent) {
        return agent.role == MiniDroneRole::Mining;
    });
    require(restoredMiningDrone != restored.run.mining.miniDrones.end() &&
        std::abs(restoredMiningDrone->taskProgressSeconds - miningDrone->taskProgressSeconds) < 0.000001 &&
        restoredMiningDrone->haulMaterials.rare == 1,
        "Mining drone work progress and carried manifest should survive an active mining save");

    const int capacity = tuning::mining::miningDroneCapacityChunks(miningDrone->upgradeLevel);
    miningDrone->haulMaterials = {};
    miningDrone->haulMaterials.common = capacity - 1;
    miningDrone->taskProgressSeconds =
        tuning::mining::miningDroneWorkSeconds(miningDrone->upgradeLevel, MiningCellMaterial::CommonOre) - 0.02;
    updateMiningRun(state, catalog, 0.05);
    require(miningCellAt(state.run.mining.terrain, targetX, targetY)->material == MiningCellMaterial::Empty &&
        miningDrone->haulMaterials.common == capacity &&
        miningDrone->targetCellX < 0,
        "a Mining drone should finish its assigned ore into its own full manifest");
    require(state.run.mining.temporaryMaterials.common == 0 && state.run.mining.stowedMaterials.common == 0,
        "Mining drone ore should remain in its own manifest until ship drop-off");

    updateMiningRun(state, catalog, 0.05);
    require(
        miningDrone->behavior == MiningMiniDroneBehavior::DeliveringToShip,
        "a full Mining drone should immediately begin autonomous Ship delivery");
    const int stowedBeforeUnload = state.run.mining.stowedMaterials.common;
    const SaveData transitSave = captureSaveData(state);
    GameState restoredTransit = createNewGame(catalog, 91943);
    restoreSaveData(restoredTransit, catalog, transitSave);
    const auto restoredTransitDrone = std::find_if(restoredTransit.run.mining.miniDrones.begin(), restoredTransit.run.mining.miniDrones.end(), [](const MiningMiniDroneAgent& agent) {
        return agent.role == MiniDroneRole::Mining;
    });
    require(restoredTransitDrone != restoredTransit.run.mining.miniDrones.end() &&
        restoredTransitDrone->behavior == MiningMiniDroneBehavior::DeliveringToShip,
        "Mining drone Ship-delivery transit should survive save/load");
    for (int step = 0; step < 220 && miningDrone->behavior == MiningMiniDroneBehavior::DeliveringToShip; ++step) {
        updateMiningRun(state, catalog, 0.08);
    }
    require(miningDrone->haulMaterials.common == 0 &&
        state.run.mining.stowedMaterials.common == stowedBeforeUnload + capacity &&
        miningDrone->behavior == MiningMiniDroneBehavior::ReturningFromShip,
        "Mining drones should bank their full manifest after deterministic Ship transit");
}

void defenseDronesCoordinateChargedShieldArcs()
{
    constexpr double pi = 3.14159265358979323846;
    MiningRunState perimeter;
    perimeter.terrain.width = 64;
    perimeter.terrain.height = 64;
    perimeter.droneX = 32.0;
    perimeter.droneY = 20.0;
    for (int index = 0; index < 6; ++index) {
        const double angle = 2.0 * pi * static_cast<double>(index) / 6.0;
        MiningMiniDroneAgent agent;
        agent.role = MiniDroneRole::Defense;
        agent.roleIndex = index;
        agent.upgradeLevel = 1;
        agent.defenseAngleRadians = angle;
        agent.defenseAngleInitialized = true;
        agent.x = perimeter.droneX + std::cos(angle) * tuning::mining::defenseDroneGuardDistanceCells;
        agent.y = perimeter.droneY + std::sin(angle) * tuning::mining::defenseDroneGuardDistanceCells;
        perimeter.miniDrones.push_back(agent);
    }

    DefenseDroneCoordinator perimeterCoordinator(perimeter);
    perimeterCoordinator.synchronizeAssignments();
    perimeterCoordinator.advanceFormation(0.05);
    for (std::size_t lhs = 0; lhs < perimeter.miniDrones.size(); ++lhs) {
        const MiningMiniDroneAgent& agent = perimeter.miniDrones[lhs];
        const MiniDroneCoordinationPoint point = perimeterCoordinator.formationPoint(agent);
        require(std::abs(std::hypot(point.x - perimeter.droneX, point.y - perimeter.droneY) -
                    tuning::mining::defenseDroneGuardDistanceCells) < 0.001,
            "Defense drones should hold a common perimeter radius around the rig");
        for (std::size_t rhs = lhs + 1; rhs < perimeter.miniDrones.size(); ++rhs) {
            require(std::hypot(
                    perimeter.miniDrones[lhs].x - perimeter.miniDrones[rhs].x,
                    perimeter.miniDrones[lhs].y - perimeter.miniDrones[rhs].y) > 1.70,
                "six Defense drones should occupy distinct perimeter positions");
        }
    }
    for (int direction = 0; direction < 24; ++direction) {
        const double angle = 2.0 * pi * static_cast<double>(direction) / 24.0;
        const DefenseShieldImpact impact = perimeterCoordinator.absorbIncomingDamage(
            perimeter.droneX + std::cos(angle) * 6.0,
            perimeter.droneY + std::sin(angle) * 6.0,
            0.001);
        require(impact.interceptor != nullptr && impact.remainingDamage <= 0.000001,
            "six coordinated Defense arcs should provide continuous coverage around the rig");
    }

    MiningRunState recharge;
    recharge.terrain.width = 64;
    recharge.terrain.height = 64;
    recharge.droneX = 32.0;
    recharge.droneY = 20.0;
    MiningMiniDroneAgent baseShield;
    baseShield.role = MiniDroneRole::Defense;
    baseShield.roleIndex = 0;
    baseShield.upgradeLevel = 1;
    baseShield.x = recharge.droneX + tuning::mining::defenseDroneGuardDistanceCells;
    baseShield.y = recharge.droneY;
    baseShield.defenseAngleRadians = 0.0;
    baseShield.defenseAngleInitialized = true;
    recharge.miniDrones.push_back(baseShield);
    DefenseDroneCoordinator rechargeCoordinator(recharge);
    rechargeCoordinator.synchronizeAssignments();
    const double baseHitPoints = tuning::mining::defenseDroneShieldHitPoints(1);
    const DefenseShieldImpact broken = rechargeCoordinator.absorbIncomingDamage(
        recharge.droneX + 6.0,
        recharge.droneY,
        baseHitPoints + 0.01);
    require(std::abs(broken.absorbedDamage - baseHitPoints) < 0.000001 &&
            std::abs(broken.remainingDamage - 0.01) < 0.000001 &&
            recharge.miniDrones[0].shieldCharge == 0.0,
        "a Defense arc should absorb only its available charge and pass overflow to the rig");
    const double baseRecharge = tuning::mining::defenseDroneRechargeSeconds(1);
    require(std::abs(recharge.miniDrones[0].shieldRechargeSeconds - baseRecharge) < 0.000001,
        "breaking a Defense arc should start its level-scaled recharge timer");
    rechargeCoordinator.advanceFormation(baseRecharge - 0.05);
    require(recharge.miniDrones[0].shieldCharge == 0.0,
        "a broken Defense arc should remain offline until recharge completes");
    rechargeCoordinator.advanceFormation(0.06);
    require(recharge.miniDrones[0].shieldCharge == 1.0,
        "a Defense arc should restore to full charge after its recharge timer");

    MiningRunState upgraded = recharge;
    upgraded.miniDrones[0].upgradeLevel = 3;
    upgraded.miniDrones[0].shieldCharge = 1.0;
    upgraded.miniDrones[0].shieldRechargeSeconds = 0.0;
    DefenseDroneCoordinator upgradedCoordinator(upgraded);
    upgradedCoordinator.synchronizeAssignments();
    const DefenseShieldImpact upgradedHit = upgradedCoordinator.absorbIncomingDamage(
        upgraded.droneX + 6.0,
        upgraded.droneY,
        baseHitPoints + 0.01);
    require(upgradedHit.remainingDamage <= 0.000001 && upgraded.miniDrones[0].shieldCharge > 0.0,
        "upgraded Defense arcs should have more shield hit points");
    require(tuning::mining::defenseDroneRechargeSeconds(3) < baseRecharge,
        "upgraded Defense arcs should recharge faster");

    MiningEnemy overhead;
    overhead.type = MiningEnemyType::Flying;
    overhead.x = recharge.droneX;
    overhead.y = recharge.droneY + 5.0;
    overhead.active = true;
    recharge.enemies = {overhead};
    recharge.miniDrones[0].defenseAngleRadians = 0.0;
    recharge.miniDrones[0].upgradeLevel = 1;
    rechargeCoordinator.synchronizeAssignments();
    rechargeCoordinator.advanceFormation(0.50);
    const double baseTurn = recharge.miniDrones[0].defenseAngleRadians;
    upgraded.enemies = {overhead};
    upgraded.miniDrones[0].defenseAngleRadians = 0.0;
    upgradedCoordinator.synchronizeAssignments();
    upgradedCoordinator.advanceFormation(0.50);
    require(upgraded.miniDrones[0].defenseAngleRadians > baseTurn &&
            upgraded.miniDrones[0].defenseAngleRadians < pi * 0.5,
        "Defense formation slerp should remain gradual while improving with drone level");
}

void attackAndDefenseDroneAgentsOwnCombatBehavior()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 91934);
    state.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    state.meta.ark.condition = ArkCondition::DamagedStranded;
    state.meta.ark.fuelReserve = tuning::ark::hostileSystemFuelReserve;
    state.meta.unlockKeys.push_back(content::unlock::deepSpace);
    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    state.meta.unlockKeys.push_back(content::unlock::perimeterDrones);
    state.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
    ensureDroneBayState(state, catalog);
    state.meta.droneBaySlots = 4;
    state.meta.equippedDroneIds = {
        content::drone::attackDrone,
        content::drone::attackDrone,
        content::drone::attackDrone,
        content::drone::defenseDrone
    };
    state.run.destinationIndex = 4;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "combat agent run should start");
    clearMiningTerrainForEvaTest(state.run.mining);
    require(toggleMiningOperator(state),
        "combat mini-drone fixture should enter EVA to exercise active-operator anchoring");
    state.run.mining.operatorX =
        static_cast<double>(state.run.mining.terrain.width) * 0.50;
    state.run.mining.operatorY =
        static_cast<double>(state.run.mining.terrain.height) * 0.35;
    state.run.mining.operatorVelocityX = 0.0;
    state.run.mining.operatorVelocityY = 0.0;
    for (MiningMiniDroneAgent& agent : state.run.mining.miniDrones) {
        const MiniDroneCoordinationPoint orbit =
            miniDroneOrbitPoint(state.run.mining, agent);
        agent.x = orbit.x;
        agent.y = orbit.y;
        agent.velocityX = 0.0;
        agent.velocityY = 0.0;
        agent.behavior = MiningMiniDroneBehavior::Returning;
    }

    const MiniDroneAnchorFrame initialCombatAnchor =
        resolveMiniDroneAnchor(state.run.mining);
    MiningEnemy first;
    first.type = MiningEnemyType::Flying;
    first.x = initialCombatAnchor.x + 4.0;
    first.y = initialCombatAnchor.y;
    first.health = 100.0;
    first.maxHealth = 100.0;
    first.speed = 0.0;
    first.damagePerSecond = 1.0;
    first.active = true;
    MiningEnemy second = first;
    second.type = MiningEnemyType::Beetle;
    second.x = initialCombatAnchor.x + 5.0;
    second.damagePerSecond = 0.0;
    state.run.mining.enemies = {first, second};
    auto alignedDefense = std::find_if(
        state.run.mining.miniDrones.begin(),
        state.run.mining.miniDrones.end(),
        [](const MiningMiniDroneAgent& agent) {
            return agent.role == MiniDroneRole::Defense;
        });
    require(alignedDefense != state.run.mining.miniDrones.end(),
        "combat loadout should create a Defense agent before combat begins");
    alignedDefense->x =
        initialCombatAnchor.x + tuning::mining::defenseDroneGuardDistanceCells;
    alignedDefense->y = initialCombatAnchor.y;
    alignedDefense->velocityX = 0.0;
    alignedDefense->velocityY = 0.0;
    alignedDefense->defenseAngleRadians = 0.0;
    alignedDefense->defenseAngleInitialized = true;
    updateMiningRun(state, catalog, 0.08);

    auto attack = std::find_if(state.run.mining.miniDrones.begin(), state.run.mining.miniDrones.end(), [](const MiningMiniDroneAgent& agent) {
        return agent.role == MiniDroneRole::Attack;
    });
    auto defense = std::find_if(state.run.mining.miniDrones.begin(), state.run.mining.miniDrones.end(), [](const MiningMiniDroneAgent& agent) {
        return agent.role == MiniDroneRole::Defense;
    });
    require(attack != state.run.mining.miniDrones.end() && defense != state.run.mining.miniDrones.end(),
        "combat loadout should create Attack and Defense agents");
    const int attackCount = static_cast<int>(std::count_if(state.run.mining.miniDrones.begin(), state.run.mining.miniDrones.end(), [](const MiningMiniDroneAgent& agent) {
        return agent.role == MiniDroneRole::Attack;
    }));
    require(attackCount == 3, "combat loadout should preserve duplicate Attack drone agents");
    require(std::all_of(state.run.mining.miniDrones.begin(), state.run.mining.miniDrones.end(), [](const MiningMiniDroneAgent& agent) {
        return agent.role != MiniDroneRole::Attack ||
            (agent.targetEnemyIndex == 0 && agent.behavior == MiningMiniDroneBehavior::Engaging);
    }), "Attack drones should coordinate on the closest shared focus target");
    const auto alliedShot = std::find_if(state.run.mining.combatProjectiles.begin(), state.run.mining.combatProjectiles.end(), [](const MiningProjectileVisual& projectile) {
        return projectile.team == MiningCombatTeam::Allied;
    });
    require(alliedShot != state.run.mining.combatProjectiles.end(), "Attack drone should fire while engaging its target");
    require(
        std::hypot(alliedShot->startX - attack->x, alliedShot->startY - attack->y) > 0.45,
        "Attack drone projectiles should originate from a weapon hardpoint instead of its center");
    const auto enemyShot = std::find_if(state.run.mining.combatProjectiles.begin(), state.run.mining.combatProjectiles.end(), [](const MiningProjectileVisual& projectile) {
        return projectile.team == MiningCombatTeam::Enemy;
    });
    require(enemyShot != state.run.mining.combatProjectiles.end(), "ranged enemy should fire at the guarded rig");
    const MiniDroneAnchorFrame guardedAnchor =
        resolveMiniDroneAnchor(state.run.mining);
    require(std::abs(std::hypot(
            enemyShot->endX - guardedAnchor.x,
            enemyShot->endY - guardedAnchor.y) -
        (tuning::mining::defenseDroneGuardDistanceCells + tuning::mining::defenseDroneShieldArcOffsetCells)) < 0.001,
        "enemy projectiles should terminate at the Defense drone's outer shield arc around the active operator");
    require(defense->shieldCharge < 1.0 && defense->shieldImpactSeconds > 0.0,
        "intercepted fire should consume the selected Defense arc and trigger impact feedback");
    require(state.run.mining.environmentalShieldAbsorbed > 0.0,
        "Defense drone interception should contribute to absorbed damage accounting");

    state.run.mining.enemies[0].damagePerSecond = 0.0;
    for (int step = 0; step < 100; ++step) {
        updateMiningRun(state, catalog, 0.05);
    }
    std::vector<const MiningMiniDroneAgent*> formation;
    for (const MiningMiniDroneAgent& agent : state.run.mining.miniDrones) {
        if (agent.role == MiniDroneRole::Attack) {
            formation.push_back(&agent);
            require(std::abs(std::hypot(agent.x - state.run.mining.enemies[0].x, agent.y - state.run.mining.enemies[0].y) -
                tuning::mining::attackDroneStandoffCells) < 0.45,
                "Attack drones should hold their assigned standoff ring around the focus target");
        }
    }
    for (std::size_t lhs = 0; lhs < formation.size(); ++lhs) {
        for (std::size_t rhs = lhs + 1; rhs < formation.size(); ++rhs) {
            require(std::hypot(formation[lhs]->x - formation[rhs]->x, formation[lhs]->y - formation[rhs]->y) > 1.5,
                "Attack drone formation slots should prevent agents from stacking on one another");
        }
    }

    const MiniDroneAnchorFrame retargetAnchor =
        resolveMiniDroneAnchor(state.run.mining);
    state.run.mining.enemies[0].x = retargetAnchor.x + 6.0;
    state.run.mining.enemies[1].x = retargetAnchor.x + 2.0;
    updateMiningRun(state, catalog, 0.08);
    require(attack->targetEnemyIndex == 0,
        "Attack drone should keep its target until that enemy is defeated");

    state.run.mining.enemies[0].health = 0.01;
    attack->actionCooldownSeconds = 0.0;
    updateMiningRun(state, catalog, 0.08);
    require(!state.run.mining.enemies[0].active, "Attack drones should finish their shared focus target");
    updateMiningRun(state, catalog, 0.08);
    require(std::all_of(state.run.mining.miniDrones.begin(), state.run.mining.miniDrones.end(), [](const MiningMiniDroneAgent& agent) {
        return agent.role != MiniDroneRole::Attack ||
            (agent.targetEnemyIndex == 1 && agent.behavior == MiningMiniDroneBehavior::Engaging);
    }), "Attack drones should coordinate on a new visible target after the focus target dies");

    state.run.mining.enemies[1].active = false;
    for (int step = 0; step < 80; ++step) {
        updateMiningRun(state, catalog, 0.05);
        for (const MiningMiniDroneAgent& agent : state.run.mining.miniDrones) {
            if (agent.role != MiniDroneRole::Attack) {
                continue;
            }
            const MiniDroneAnchorFrame returnAnchor =
                resolveMiniDroneAnchor(state.run.mining, agent.anchorTarget);
            require(std::hypot(agent.x - returnAnchor.x, agent.y - returnAnchor.y) >=
                tuning::mining::attackDroneRigClearanceCells - 0.001,
                "returning Attack drones should never cross the active operator's clearance perimeter");
        }
    }
    std::vector<const MiningMiniDroneAgent*> returnedAttackDrones;
    for (const MiningMiniDroneAgent& agent : state.run.mining.miniDrones) {
        if (agent.role == MiniDroneRole::Attack) {
            returnedAttackDrones.push_back(&agent);
            require(agent.behavior == MiningMiniDroneBehavior::Following,
                "Attack drones should settle into their home perimeter when no target is visible");
        }
    }
    for (std::size_t lhs = 0; lhs < returnedAttackDrones.size(); ++lhs) {
        for (std::size_t rhs = lhs + 1; rhs < returnedAttackDrones.size(); ++rhs) {
            require(std::hypot(
                returnedAttackDrones[lhs]->x - returnedAttackDrones[rhs]->x,
                returnedAttackDrones[lhs]->y - returnedAttackDrones[rhs]->y) > 1.5,
                "returned Attack drones should occupy distinct perimeter slots");
        }
    }
    double minimumFormationSpacing = 1.0e9;
    double maximumFormationSpacing = 0.0;
    const MiniDroneAnchorFrame settledAnchor =
        resolveMiniDroneAnchor(state.run.mining);
    for (std::size_t lhs = 0; lhs < returnedAttackDrones.size(); ++lhs) {
        require(std::abs(std::hypot(
            returnedAttackDrones[lhs]->x - settledAnchor.x,
            returnedAttackDrones[lhs]->y - settledAnchor.y) - tuning::mining::attackDroneHomeRadiusCells) < 0.08,
            "returned Attack drones should settle on the count-aware home radius");
        for (std::size_t rhs = lhs + 1; rhs < returnedAttackDrones.size(); ++rhs) {
            const double spacing = std::hypot(
                returnedAttackDrones[lhs]->x - returnedAttackDrones[rhs]->x,
                returnedAttackDrones[lhs]->y - returnedAttackDrones[rhs]->y);
            minimumFormationSpacing = std::min(minimumFormationSpacing, spacing);
            maximumFormationSpacing = std::max(maximumFormationSpacing, spacing);
        }
    }
    require(maximumFormationSpacing - minimumFormationSpacing < 0.08,
        "Attack drone home slots should be evenly spaced for the equipped drone count");

    std::vector<std::pair<double, double>> positionsBeforeRigMove;
    for (const MiningMiniDroneAgent* agent : returnedAttackDrones) {
        positionsBeforeRigMove.push_back({agent->x, agent->y});
    }
    state.run.mining.operatorX += 0.60;
    updateMiningRun(state, catalog, 0.05);
    for (std::size_t i = 0; i < returnedAttackDrones.size(); ++i) {
        const double movement = std::hypot(
            returnedAttackDrones[i]->x - positionsBeforeRigMove[i].first,
            returnedAttackDrones[i]->y - positionsBeforeRigMove[i].second);
        require(movement > 0.001 && movement < 0.30,
            "Attack drone formation should begin following smoothly instead of mirroring the rig displacement");
    }
    for (int step = 0; step < 50; ++step) {
        updateMiningRun(state, catalog, 0.05);
    }
    const MiniDroneAnchorFrame movedAnchor =
        resolveMiniDroneAnchor(state.run.mining);
    for (const MiningMiniDroneAgent* agent : returnedAttackDrones) {
        require(std::abs(std::hypot(
            agent->x - movedAnchor.x,
            agent->y - movedAnchor.y) - tuning::mining::attackDroneHomeRadiusCells) < 0.08,
            "Attack drones should smoothly settle back onto the moving active-operator formation");
    }

    defense->shieldCharge = 0.42;
    defense->shieldRechargeSeconds = 1.75;
    defense->shieldImpactSeconds = 0.18;
    const std::string serialized = serializeSaveData(captureSaveData(state));
    const auto save = deserializeSaveData(serialized);
    require(save.has_value(), "combat mini-drone save should parse");
    GameState restored = createNewGame(catalog, 91935);
    restoreSaveData(restored, catalog, *save);
    require(restored.run.mining.miniDrones.size() == state.run.mining.miniDrones.size(),
        "independent mini-drone agents should round trip through active mining saves");
    require(restored.run.mining.miniDrones.front().behavior == state.run.mining.miniDrones.front().behavior,
        "mini-drone behavior state should survive an active mining save");
    const auto restoredDefense = std::find_if(
        restored.run.mining.miniDrones.begin(),
        restored.run.mining.miniDrones.end(),
        [](const MiningMiniDroneAgent& agent) { return agent.role == MiniDroneRole::Defense; });
    require(restoredDefense != restored.run.mining.miniDrones.end() &&
            std::abs(restoredDefense->shieldCharge - 0.42) < 0.000001 &&
            std::abs(restoredDefense->shieldRechargeSeconds - 1.75) < 0.000001 &&
            restoredDefense->defenseAngleInitialized,
        "Defense angle, charge, and recharge state should survive an active mining save");
}

void elementalMiningCombatAppliesAffinityAndAreaDefenses()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 91923);
    state.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    state.meta.ark.condition = ArkCondition::DamagedStranded;
    state.meta.ark.fuelReserve = tuning::ark::hostileSystemFuelReserve;
    state.meta.unlockKeys.push_back(content::unlock::deepSpace);
    state.run.destinationIndex = 4;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "elemental mining run should start");
    state.run.mining.drillHeat = 0.0;
    state.run.mining.enemies = {
        {MiningEnemyType::Elemental, MiningCellFeature::EncounterZone, state.run.mining.droneX + 0.2, state.run.mining.droneY, 0.0, 0.0, 40.0, 40.0, 0.0, 0.0, 1.0, tuning::mining::enemyElementalRadiusCells, true, MiningElementalAffinity::Thermal}
    };
    const double heatBefore = state.run.mining.drillHeat;
    updateMiningRun(state, catalog, 0.08);
    require(state.run.mining.elementalExposureSeconds > 0.0, "elemental contact should track exposure time");
    require(state.run.mining.drillHeat > heatBefore, "thermal elementals should heat the drill while in their area");
    require(state.run.mining.enemyDamageTaken > 0.0, "elemental contact should still damage the mining rig");
    require(miningElementalAffinityName(MiningElementalAffinity::Thermal) == std::string_view("Thermal"), "elemental affinity names should describe thermal threats");

    GameState cryo = createNewGame(catalog, 91924);
    cryo.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    cryo.meta.ark.condition = ArkCondition::DamagedStranded;
    cryo.meta.ark.fuelReserve = tuning::ark::hostileSystemFuelReserve;
    cryo.meta.unlockKeys.push_back(content::unlock::deepSpace);
    cryo.run.destinationIndex = 4;
    startSurfaceExpedition(cryo, catalog);
    prepareMiningSiteForTest(cryo);
    require(startMiningRun(cryo, catalog).applied, "cryo elemental mining run should start");
    cryo.run.mining.moveX = 1.0;
    cryo.run.mining.enemies = {
        {MiningEnemyType::Elemental, MiningCellFeature::EncounterZone, cryo.run.mining.droneX + 0.2, cryo.run.mining.droneY, 0.0, 0.0, 40.0, 40.0, 0.0, 0.0, 0.0, tuning::mining::enemyElementalRadiusCells, true, MiningElementalAffinity::Cryo}
    };
    updateMiningRun(cryo, catalog, 0.08);
    require(cryo.run.mining.movementSlowSeconds > 0.0, "cryo elementals should apply a movement slow timer");
    require(cryo.run.mining.movementSlowScale < 1.0, "cryo elementals should reduce movement scale");

    GameState defended = createNewGame(catalog, 91925);
    defended.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    defended.meta.ark.condition = ArkCondition::DamagedStranded;
    defended.meta.ark.fuelReserve = tuning::ark::hostileSystemFuelReserve;
    defended.meta.unlockKeys.push_back(content::unlock::deepSpace);
    defended.meta.unlockKeys.push_back(content::unlock::droneBay);
    defended.meta.unlockKeys.push_back(content::unlock::perimeterDrones);
    defended.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
    defended.meta.unlockKeys.push_back(content::unlock::perimeterCoordination);
    ensureDroneBayState(defended, catalog);
    defended.meta.droneBaySlots = 2;
    defended.meta.equippedDroneIds = {content::drone::attackDrone, content::drone::defenseDrone};
    defended.run.destinationIndex = 4;
    startSurfaceExpedition(defended, catalog);
    prepareMiningSiteForTest(defended);
    require(startMiningRun(defended, catalog).applied, "area-control mining run should start");
    defended.run.mining.enemies = {
        {MiningEnemyType::Ant, MiningCellFeature::EncounterZone, defended.run.mining.droneX + 0.2, defended.run.mining.droneY, 0.0, 0.0, 20.0, 20.0, 0.0, 0.0, 1.0, 0.0, true},
        {MiningEnemyType::Ant, MiningCellFeature::EncounterZone, defended.run.mining.droneX + 3.0, defended.run.mining.droneY, 0.0, 0.0, 20.0, 20.0, 0.0, 0.0, 0.0, 0.0, true}
    };
    updateMiningRun(defended, catalog, 0.08);
    require(defended.run.mining.areaControlDamageDealt > 0.0, "attack drones should apply area-control damage around the rig");
    require(defended.run.mining.reactiveArmorDamageDealt > 0.0, "defense drones should retaliate against contact enemies");
    require(defended.run.mining.environmentalShieldAbsorbed > 0.0, "environmental shields should absorb incoming enemy damage");
    require(defended.run.mining.enemies[1].health < defended.run.mining.enemies[1].maxHealth, "area-control fields should damage non-targeted nearby enemies");
}

void themedAffinityMechanicsStayRestrictedToElementalsAndTrueElites()
{
    const MiningEnemy ordinary = createMiningEnemy(
        MiningEnemyType::Ant,
        MiningCellFeature::EncounterZone,
        1.0,
        1.0,
        MiningElementalAffinity::Thermal);
    require(ordinary.affinity == MiningElementalAffinity::None && !ordinary.elite,
        "ordinary themed enemies should remain cosmetic and reject affinity mechanics");

    const MiningEnemy elemental = createMiningEnemy(
        MiningEnemyType::Elemental,
        MiningCellFeature::EncounterZone,
        1.0,
        1.0,
        MiningElementalAffinity::Thermal);
    require(elemental.affinity == MiningElementalAffinity::Thermal && elemental.effectRadius > 0.0,
        "Elementals should keep the site's matching affinity mechanics");

    const MiningEnemy miniboss = createMiningEnemy(
        MiningEnemyType::Beetle,
        MiningCellFeature::MinibossLair,
        1.0,
        1.0,
        MiningElementalAffinity::Cryo);
    require(miniboss.elite && miniboss.affinity == MiningElementalAffinity::Cryo &&
            miniboss.effectRadius >= tuning::mining::enemyElementalRadiusCells,
        "true themed elites should inherit the existing affinity field and tuning");
}

void mammalBossChambersGrantAdvancedRewards()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 91926);
    state.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    state.meta.ark.condition = ArkCondition::DamagedStranded;
    state.meta.ark.fuelReserve = tuning::ark::hostileSystemFuelReserve;
    state.meta.unlockKeys.push_back(content::unlock::deepSpace);
    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    state.meta.unlockKeys.push_back(content::unlock::perimeterDrones);
    state.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
    ensureDroneBayState(state, catalog);
    state.meta.droneBaySlots = 1;
    state.meta.equippedDroneIds = {content::drone::attackDrone};
    state.run.destinationIndex = 5;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(
        startMiningRun(state, catalog, {MiningAct::ActThree, 10, 91926}, false).applied,
        "mammal boss mining run should start at the Act 3 mastery tier");
    const int blueprintBefore = state.meta.blueprintProgress;
    state.run.mining.enemies = {
        {MiningEnemyType::Mammal, MiningCellFeature::BossChamber, state.run.mining.droneX + 2.0, state.run.mining.droneY, 0.0, 0.0, 0.5, 35.0, 0.20, 0.0, 0.0, 0.0, true}
    };
    for (int tick = 0; tick < 10 && state.run.mining.enemiesDefeated == 0; ++tick) {
        updateMiningRun(state, catalog, 0.08);
    }
    require(state.run.mining.enemiesDefeated == 1, "passive defenses should defeat weakened mammal boss test enemies");
    require(
        state.run.mining.temporaryMaterials.rare > 0 &&
            state.run.mining.temporaryMaterials.rare <= state.run.mining.rewardBudget.rareCap,
        "mammal boss chambers should grant rare materials within the shared arena cap");
    require(
        state.run.mining.temporaryMaterials.exotic > 0 &&
            state.run.mining.temporaryMaterials.exotic <= state.run.mining.rewardBudget.exoticCap,
        "mammal boss chambers should grant exotic materials within the shared arena cap");
    require(state.meta.blueprintProgress >= blueprintBefore + 2, "mammal boss chambers should recover advanced tech progress");
}

void enemyMovementTypesHaveDistinctBehavior()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState flying = createNewGame(catalog, 91927);
    flying.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    flying.meta.ark.condition = ArkCondition::DamagedStranded;
    flying.meta.ark.fuelReserve = tuning::ark::hostileSystemFuelReserve;
    flying.meta.unlockKeys.push_back(content::unlock::deepSpace);
    flying.run.destinationIndex = 4;
    startSurfaceExpedition(flying, catalog);
    prepareMiningSiteForTest(flying);
    require(startMiningRun(flying, catalog).applied, "flying behavior mining run should start");
    flying.run.mining.enemies = {
        {MiningEnemyType::Flying, MiningCellFeature::EncounterZone, flying.run.mining.droneX + 4.0, flying.run.mining.droneY, 0.0, 0.0, 40.0, 40.0, 0.0, 3.1, 0.0, 0.0, true}
    };
    updateMiningRun(flying, catalog, 0.08);
    require(flying.run.mining.enemies.front().velocityX < 0.0, "flying enemies should still home toward the drone");
    require(std::abs(flying.run.mining.enemies.front().velocityY) > 0.05, "flying enemies should dart laterally while pursuing");

    GameState mammal = createNewGame(catalog, 91928);
    mammal.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
    mammal.meta.ark.condition = ArkCondition::DamagedStranded;
    mammal.meta.ark.fuelReserve = tuning::ark::hostileSystemFuelReserve;
    mammal.meta.unlockKeys.push_back(content::unlock::deepSpace);
    mammal.run.destinationIndex = 5;
    startSurfaceExpedition(mammal, catalog);
    prepareMiningSiteForTest(mammal);
    require(startMiningRun(mammal, catalog).applied, "mammal burrow behavior mining run should start");
    const int burrowX = static_cast<int>(std::floor(mammal.run.mining.droneX + 1.0));
    const int burrowY = static_cast<int>(std::floor(mammal.run.mining.droneY));
    if (MiningCell* current = miningCellAt(mammal.run.mining.terrain, burrowX + 1, burrowY)) {
        current->material = MiningCellMaterial::Empty;
        current->remainingToughness = 0.0;
        current->maxToughness = 0.0;
        current->revealed = true;
    }
    if (MiningCell* blocked = miningCellAt(mammal.run.mining.terrain, burrowX, burrowY)) {
        blocked->material = MiningCellMaterial::Regolith;
        blocked->remainingToughness = 0.2;
        blocked->maxToughness = 0.2;
        blocked->revealed = false;
        blocked->feature = MiningCellFeature::None;
        blocked->enemy = MiningEnemyType::None;
    }
    mammal.run.mining.enemies = {
        {MiningEnemyType::Mammal, MiningCellFeature::OrganicBurrow, static_cast<double>(burrowX) + 1.02, static_cast<double>(burrowY) + 0.5, 0.0, 0.0, 40.0, 40.0, 0.0, 1.45, 0.0, 0.0, true}
    };
    updateMiningRun(mammal, catalog, 0.08);
    const MiningCell* burrow = miningCellAt(mammal.run.mining.terrain, burrowX, burrowY);
    require(burrow != nullptr && burrow->material == MiningCellMaterial::Empty, "mammal enemies should burrow through weak non-bedrock cells");
    require(burrow != nullptr && burrow->feature == MiningCellFeature::OrganicBurrow, "mammal burrowing should mark organic tunnel tiles");
    require(burrow != nullptr && burrow->enemy == MiningEnemyType::Mammal, "mammal burrowing should seed mammal tunnel metadata");
}


void miningDrillBreaksCellsAndMarksChunks()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 92929);
    state.run.destinationIndex = 2;
    activateOnlyCrew(state, content::astronaut::marco);
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    const int supplyBefore = state.run.surfaceExpedition.supply;
    const double fuelBefore = state.run.surfaceExpedition.rigFuel;
    const SurfaceActionOutcome started = startMiningRun(state, catalog);
    require(started.applied, "mining should start when the site is prepared");
    require(state.screen == Screen::Mining, "starting mining should move to the mining screen");
    require(state.run.surfaceExpedition.supply == supplyBefore, "starting mining should not spend action kits");
    require(started.fuelDelta == -1, "starting mining should report rig fuel spent");
    require(nearlyEqual(state.run.surfaceExpedition.rigFuel, fuelBefore - 1.0), "starting mining should spend one rig fuel");

    MiningRunState& mining = state.run.mining;
    setMiningAim(state, 1.0, mining.droneY / static_cast<double>(mining.terrain.height - 1));
    setMiningMove(state, -1.0, 0.0);
    require(mining.hullDirX < -0.99 && std::abs(mining.hullDirY) < 0.01, "hull heading should follow movement input");
    require(mining.aimDirX < -0.99 && std::abs(mining.aimDirY) < 0.01, "drill direction should remain fixed to the hull heading");
    setMiningMove(state, 0.0, 0.0);
    require(mining.hullDirX < -0.99, "hull heading should persist after movement stops");
    require(mining.aimDirX < -0.99, "drill direction should persist with the stopped hull heading");

    require(std::abs(mining.oxygenSeconds - tuning::mining::oxygenSeconds) < 0.000001, "starter mining run should begin with configured oxygen");
    require(mining.fuelSpent == 1, "active mining run should track the deployment fuel spend");
    MiningCell* ore = miningCellAt(mining.terrain, 33, 4);
    require(ore != nullptr, "test ore cell should exist");
    *ore = {MiningCellMaterial::CommonOre, 0.25, 0.25, true, false};
    MiningCell* farOre = miningCellAt(mining.terrain, 34, 4);
    require(farOre != nullptr, "test far ore cell should exist");
    *farOre = {MiningCellMaterial::CommonOre, 0.45, 0.45, true, false};
    std::fill(mining.terrain.dirtyChunks.begin(), mining.terrain.dirtyChunks.end(), 0);
    mining.droneX = 32.0;
    mining.droneY = 4.0;
    setMiningMove(state, 1.0, 0.0);
    setMiningMove(state, 0.0, 0.0);
    setMiningDrilling(state, true);
    for (int i = 0; i < 8; ++i) {
        updateMiningRun(state, catalog, 0.08);
    }

    require(ore->material == MiningCellMaterial::Empty, "drilling should break depleted terrain cells");
    require(farOre->material == MiningCellMaterial::Empty, "drilling should damage every solid tile under the drill footprint instead of one tile at a time");
    require(state.run.mining.temporaryMaterials.common > 0, "breaking common ore should add common material");
    require(state.run.mining.cargo > 0, "breaking ore should add cargo");
    require(
        std::any_of(mining.terrain.dirtyChunks.begin(), mining.terrain.dirtyChunks.end(), [](std::uint8_t value) { return value != 0; }),
        "drilling should mark the changed chunk dirty");
}

void miningUsesRigFuelReserve()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState blocked = createNewGame(catalog, 92933);
    blocked.run.destinationIndex = 2;
    startSurfaceExpedition(blocked, catalog);
    prepareMiningSiteForTest(blocked);
    blocked.run.surfaceExpedition.rigFuel = 0.0;
    const SurfaceActionOutcome blockedStart = startMiningRun(blocked, catalog);
    require(!blockedStart.applied, "mining should not start without rig fuel");

    GameState state = createNewGame(catalog, 92934);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    state.run.surfaceExpedition.rigFuel = 1.0;
    require(
        startMiningRun(state, catalog, {MiningAct::ActOne, 2, 92934}, true).applied,
        "mining should start with one rig fuel at an oxygen/fuel-enabled tier");
    require(nearlyEqual(state.run.surfaceExpedition.rigFuel, 0.0), "deployment should spend the available rig fuel");
    require(
        state.run.mining.oxygenSeconds * tuning::mining::fuelCycleProgressPerSecond > 1.0,
        "baseline oxygen should allow runs long enough to complete another fuel cycle");

    for (int i = 0; i < 190 && !state.run.mining.failurePending; ++i) {
        updateMiningRun(state, catalog, 0.08);
    }

    require(state.run.mining.failurePending, "mining should recall when upgraded oxygen outlasts rig fuel");
}

void rigFuelLoopRanksControlOperatingCadence()
{
    const ContentCatalog catalog = createDefaultContent();
    const std::array<double, 4> expectedCycleSeconds {15.0, 18.0, 21.0, 24.0};
    for (int rank = 0; rank <= 3; ++rank) {
        GameState state = createNewGame(catalog, 92940 + rank);
        state.meta.rigFuelLoop.rank = rank;
        state.run.destinationIndex = 2;
        startSurfaceExpedition(state, catalog);
        prepareMiningSiteForTest(state);
        state.run.surfaceExpedition.rigFuel = 5.0;
        const double fuelBeforeDeployment = state.run.surfaceExpedition.rigFuel;
        require(
            startMiningRun(
                state,
                catalog,
                {MiningAct::ActOne, 2, static_cast<std::uint64_t>(92940 + rank)},
                false).applied,
            "each Rig Fuel Loop rank should start an oxygen/fuel-enabled Mining run");
        require(
            nearlyEqual(
                state.run.surfaceExpedition.rigFuel,
                fuelBeforeDeployment - 1.0) &&
                state.run.mining.fuelSpent == 1,
            "Mining Rig deployment must remain exactly one fuel at every Fuel Loop rank");
        require(
            nearlyEqual(miningRigFuelCycleSeconds(state), expectedCycleSeconds[rank]),
            "Fuel Loop ranks zero through three must map to 15, 18, 21, and 24 seconds");

        state.run.mining.enemies.clear();
        state.run.mining.swarm = {};
        const double cycleStart = state.run.mining.elapsedSeconds;
        while (state.run.mining.fuelSpent == 1 &&
               state.run.mining.elapsedSeconds - cycleStart < expectedCycleSeconds[rank] + 0.5) {
            updateMiningRun(state, catalog, 0.08);
        }
        const double elapsedToBurn = state.run.mining.elapsedSeconds - cycleStart;
        require(
            state.run.mining.fuelSpent == 2 &&
                elapsedToBurn >= expectedCycleSeconds[rank] - 0.08 &&
                elapsedToBurn <= expectedCycleSeconds[rank] + 0.08,
            "each Fuel Loop rank must burn its next fuel at the configured unloaded cadence");
    }

    GameState baseline = createNewGame(catalog, 92950);
    baseline.meta.chapter = GameChapter::RedFrontier;
    baseline.run.destinationIndex = 2;
    startSurfaceExpedition(baseline, catalog);
    baseline.run.surfaceExpedition.rigFuel = 10.0;
    GameState efficient = baseline;
    efficient.meta.rigFuelLoop.rank = 3;
    const SurfaceReturnSafetyAssessment baselineForecast =
        surfaceReturnSafetyAssessment(baseline, catalog, 3);
    const SurfaceReturnSafetyAssessment efficientForecast =
        surfaceReturnSafetyAssessment(efficient, catalog, 3);
    require(
        baselineForecast.oxygenSeconds == efficientForecast.oxygenSeconds &&
            baselineForecast.estimatedReturnSeconds == efficientForecast.estimatedReturnSeconds,
        "Fuel Loop progression must not alter the oxygen or traversal estimate");
    require(
        efficientForecast.fuelCycleSeconds > baselineForecast.fuelCycleSeconds &&
            efficientForecast.fuelNeededAfterDeployment <
                baselineForecast.fuelNeededAfterDeployment,
        "Fuel Loop progression must improve the fuel-based return forecast");
}

void miningDrillFootprintCapsWearToWorstContact()
{
    const ContentCatalog catalog = createDefaultContent();
    auto prepareState = [&](bool secondRock) {
        GameState state = createNewGame(catalog, 92932);
        state.run.destinationIndex = 2;
        startSurfaceExpedition(state, catalog);
        prepareMiningSiteForTest(state);
        require(
            startMiningRun(state, catalog, {MiningAct::ActOne, 4, 92932}, false).applied,
            "mining should start for footprint wear test with integrity enabled");

        MiningRunState& mining = state.run.mining;
        for (MiningCell& cell : mining.terrain.cells) {
            cell = {MiningCellMaterial::Empty, 0.0, 0.0, true, false};
        }

        MiningCell* rock = miningCellAt(mining.terrain, 33, 4);
        require(rock != nullptr, "primary rock should exist");
        *rock = {MiningCellMaterial::HardRock, 40.0, 40.0, true, false};
        if (secondRock) {
            MiningCell* extraRock = miningCellAt(mining.terrain, 34, 4);
            require(extraRock != nullptr, "secondary rock should exist");
            *extraRock = {MiningCellMaterial::HardRock, 40.0, 40.0, true, false};
        }

        mining.droneX = 32.0;
        mining.droneY = 4.0;
        mining.drillHeat = 0.96;
        mining.drillIntegrity = 1.0;
        setMiningMove(state, 1.0, 0.0);
        setMiningMove(state, 0.0, 0.0);
        setMiningDrilling(state, true);
        updateMiningRun(state, catalog, 0.08);
        return state;
    };

    GameState singleContact = prepareState(false);
    GameState doubleContact = prepareState(true);
    const double singleLoss = 1.0 - singleContact.run.mining.drillIntegrity;
    const double doubleLoss = 1.0 - doubleContact.run.mining.drillIntegrity;
    require(doubleLoss <= singleLoss + 0.000001, "multiple footprint contacts should not stack drill integrity wear");

    const MiningCell* first = miningCellAt(doubleContact.run.mining.terrain, 33, 4);
    const MiningCell* second = miningCellAt(doubleContact.run.mining.terrain, 34, 4);
    require(first != nullptr && first->remainingToughness < first->maxToughness, "first hard rock should still take drill damage");
    require(second != nullptr && second->remainingToughness < second->maxToughness, "second hard rock should still take drill damage");
}

void miningMovementGrindsSoftTerrainAndRecoilsFromHardTerrain()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 92931);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(
        startMiningRun(state, catalog, {MiningAct::ActOne, 4, 92931}, false).applied,
        "mining should start for movement feel test with contact rebound enabled");

    MiningRunState& mining = state.run.mining;
    clearMiningTerrainForEvaTest(mining);
    const double softContactStartX =
        33.0 - tuning::mining::rigColliderRadiusCells - 0.12;
    mining.droneX = softContactStartX;
    mining.droneY = 10.0;
    MiningCell* soft = miningCellAt(mining.terrain, 33, 10);
    require(soft != nullptr, "soft contact cell should exist");
    *soft = {MiningCellMaterial::Regolith, 3.0, 3.0, false, false};
    setMiningMove(state, 1.0, 0.0);
    setMiningDrilling(state, true);
    updateMiningRun(state, catalog, 0.08);

    require(mining.droneX > softContactStartX, "drilling into regolith should let the drone grind forward slowly");
    require(static_cast<int>(std::floor(mining.droneX)) == 32, "the drone should not occupy unbroken regolith before the drill clears it");
    require(mining.contactIntensity > 0.0, "soft contact should set mining feedback intensity");
    require(soft->remainingToughness < soft->maxToughness, "pushing into regolith while drilling should do terrain work");

    for (int i = 0; i < 12 && soft->material != MiningCellMaterial::Empty; ++i) {
        updateMiningRun(state, catalog, 0.08);
    }

    require(soft->material == MiningCellMaterial::Empty, "continued drilling should visibly clear soft terrain before the drone passes through");

    const double hardContactStartX =
        33.0 - tuning::mining::rigColliderRadiusCells - 0.03;
    mining.droneX = hardContactStartX;
    mining.droneY = 12.0;
    MiningCell* hard = miningCellAt(mining.terrain, 33, 12);
    require(hard != nullptr, "hard contact cell should exist");
    *hard = {MiningCellMaterial::HardRock, 8.0, 8.0, false, false};
    mining.contactIntensity = 0.0;
    mining.contactBounce = 0.0;
    mining.contactBounceVelocity = 0.0;
    mining.contactBounceCooldown = 0.0;
    mining.contactSpeedRecovery = 1.0;
    setMiningMove(state, 1.0, 0.0);
    setMiningDrilling(state, true);
    GameState dampedState = state;
    dampedState.run.surfaceExpedition.runRigUpgradeRanks = {{content::surfaceUpgrade::shockMounts, 1}};
    updateMiningRun(state, catalog, 0.08);
    updateMiningRun(dampedState, catalog, 0.08);

    const double hardContactBoundary = 33.0 - tuning::mining::rigColliderRadiusCells;
    require(
        mining.droneX > hardContactStartX + 0.02 && mining.droneX < hardContactBoundary + 0.001,
        "a hard-rock collision should sweep the rig to the physical boundary instead of leaving a full movement-step gap");
    require(mining.recoilX < 0.0, "hard contact should push feedback opposite travel");
    require(mining.contactIntensity > 0.5, "hard contact should produce stronger mining feedback");
    require(
        mining.contactIndicatorSeconds > 0.0 && mining.contactIndicatorDirX > 0.99 &&
            std::abs(mining.contactIndicatorDirY) < 0.01,
        "a player-driven rig collision should retain a short-lived indicator on the contacted edge");
    require(
        mining.contactBounce > 0.0 || mining.contactBounceVelocity > 0.0 || mining.contactBounceCooldown > 0.0,
        "hard contact should trigger a damped bounce impulse");
    require(
        std::abs(tuning::mining::hardTerrainBounceImpulse - 54.0) < 0.000001 &&
            std::abs(tuning::mining::contactBounceMaxCells - 2.24) < 0.000001,
        "baseline hard-contact rebound should use the fourfold floaty tuning");
    require(
        dampedState.run.mining.contactBounceVelocity < mining.contactBounceVelocity,
        "shock mounts should reduce the amplified hard-contact rebound");
    require(hard->remainingToughness < hard->maxToughness, "hard contact should still drill the terrain");

    hard->material = MiningCellMaterial::HardRock;
    hard->maxToughness = miningMaterialToughness(MiningCellMaterial::HardRock, 0);
    hard->remainingToughness = hard->maxToughness;
    hard->revealed = false;
    mining.droneX = 32.85;
    mining.droneY = 12.0;
    setMiningMove(state, 1.0, 0.0);
    setMiningDrilling(state, true);
    for (int i = 0; i < 4; ++i) {
        updateMiningRun(state, catalog, 0.08);
    }
    require(hard->material == MiningCellMaterial::HardRock, "hard rock should require several hard contacts before breaking");
    for (int i = 0; i < 14 && hard->material != MiningCellMaterial::Empty; ++i) {
        updateMiningRun(state, catalog, 0.08);
    }
    require(hard->material == MiningCellMaterial::Empty, "default hard rock should clear in a short arcade burst");

    for (MiningCell& cell : mining.terrain.cells) {
        cell = {MiningCellMaterial::Empty, 0.0, 0.0, true, false};
    }
    mining.droneX = 20.0;
    mining.droneY = 12.0;
    mining.contactSpeedRecovery = 0.0;
    setMiningMove(state, 1.0, 0.0);
    setMiningDrilling(state, false);
    const double recoveryStartX = mining.droneX;
    updateMiningRun(state, catalog, 0.08);
    const double firstRecoveryStep = mining.droneX - recoveryStartX;
    double finalRecoveryStep = 0.0;
    for (int i = 0; i < 7; ++i) {
        const double previousX = mining.droneX;
        updateMiningRun(state, catalog, 0.08);
        finalRecoveryStep = mining.droneX - previousX;
    }
    require(firstRecoveryStep > 0.0, "post-contact recovery should keep the drone responsive");
    require(finalRecoveryStep > firstRecoveryStep * 1.35, "post-contact movement should ramp smoothly back toward full speed");
    require(mining.contactSpeedRecovery >= 0.999, "post-contact movement should recover its full-speed ceiling");
}

void miningDrillTargetsFirstSolidCellOnRay()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 92930);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "mining should start for targeting test");

    MiningRunState& mining = state.run.mining;
    mining.droneX = 32.9;
    mining.droneY = 10.0;
    MiningCell* nearOre = miningCellAt(mining.terrain, 33, 10);
    MiningCell* farOre = miningCellAt(mining.terrain, 34, 10);
    require(nearOre != nullptr && farOre != nullptr, "targeting test cells should exist");
    *nearOre = {MiningCellMaterial::CommonOre, 1.0, 1.0, true, false};
    *farOre = {MiningCellMaterial::RareOre, 1.0, 1.0, true, false};

    setMiningMove(state, 1.0, 0.0);
    setMiningMove(state, 0.0, 0.0);

    require(mining.targetCellX == 33 && mining.targetCellY == 10, "drill targeting should stop at the first solid cell on the ray");
    require(mining.targetTipX < 34.0, "drill visual tip should stop before the far cell when terrain blocks the ray");
}

void miningCompletionFeedsSurfacePayload()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 93939);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "mining should start for completion test");
    state.run.mining.temporaryMaterials.common = 2;
    state.run.mining.temporaryMaterials.rare = 1;
    state.run.mining.cargo = 4;
    state.run.mining.hazardDelta = 0.05;
    state.run.mining.droneX = state.run.mining.returnZoneX;
    state.run.mining.droneY = state.run.mining.returnZoneY;

    const SurfaceActionOutcome finished = finishMiningRun(state, catalog, false);
    require(finished.applied, "finishing mining should produce a surface action outcome");
    require(state.screen == Screen::SurfaceExpedition, "finishing mining should return to surface expedition");
    require(!state.run.mining.active, "finishing mining should clear the active mining run");
    require(state.run.surfaceExpedition.temporaryMaterials.common == 2, "mined common material should move to surface payload");
    require(state.run.surfaceExpedition.temporaryMaterials.rare == 1, "mined rare material should move to surface payload");
    require(state.run.surfaceExpedition.cargo == 4, "mined cargo should move to surface payload");
    require(state.run.surfaceExpedition.hazard > tuning::research::baseHazard, "mining hazard should affect extraction pressure");
}

void miningBrokenDrillBitDisablesDrillingOnly()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 94949);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(
        startMiningRun(state, catalog, {MiningAct::ActOne, 4, 94949}, false).applied,
        "mining should start for drill failure test with scanner and integrity mechanics enabled");

    state.run.mining.droneX = state.run.mining.returnZoneX + tuning::mining::returnZoneRadiusCells + 1.0;
    state.run.mining.droneY = state.run.mining.returnZoneY;
    state.run.mining.drillIntegrity = 0.0;
    state.run.mining.drilling = true;
    updateMiningRun(state, catalog, 0.08);

    require(state.screen == Screen::Mining, "broken drill bit should stay on mining screen");
    require(state.run.mining.active, "broken drill bit should keep the mining run active");
    require(!state.run.mining.failurePending, "broken drill bit should not force a recall");
    require(!state.run.mining.drilling, "broken drill bit should disable drilling");
    pulseMiningScanner(state, catalog);
    require(state.run.mining.scannerPulseSeconds > 0.0, "broken drill bit should still allow scanner pulses");

    Random rng(94949);
    const PreparedLaunch prepared = prepareLaunch(state, catalog, rng);
    const std::string html = buildGamePanelHtml({state, catalog, prepared, prepared});
    require(html.find("mining-vital-broken") != std::string::npos, "broken drill bit should use the flashing red HUD treatment");
    require(html.find("data-auto-modal=\"1\"") == std::string::npos, "broken drill bit should not open the failure modal");

    state.run.mining.failurePending = true;
    state.run.mining.failureMessage = "Drone health lost. Emergency recall fired.";
    const std::string failureHtml = buildGamePanelHtml({state, catalog, prepared, prepared});
    require(failureHtml.find("data-modal-dismissible=\"0\"") != std::string::npos,
        "forced emergency-recall modal should expose only its recovery action, not a close path");
    require(failureHtml.find("data-ui-focus-id=\"action:mining_failure_ack\" data-ui-default-focus=\"1\"") != std::string::npos,
        "forced emergency-recall modal should explicitly focus its controller recovery action");
}

void miningShipRepairsUseBankedMaterialsProportionally()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 94950);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(
        startMiningRun(state, catalog, {MiningAct::ActOne, 4, 94950}, false).applied,
        "mining should start for ship repair test with field repairs enabled");

    MiningRunState& mining = state.run.mining;
    mining.drillIntegrity = 0.0;
    mining.drillBreakNotified = true;
    mining.droneHealth = 0.5;
    mining.stowedMaterials.common = 10;
    mining.stowedCargo = 10;
    require(miningDrillRepairCost(mining) == 4, "a broken bit should cost the full four common materials");
    require(miningDroneRepairCost(mining) == 3, "a half-damaged drone should cost half of its six-common rebuild cost");
    require(!repairMiningDrill(state), "field repair should be rejected away from the ship");
    require(mining.stowedMaterials.common == 10, "rejected field repair should not spend banked materials");

    mining.droneX = mining.returnZoneX;
    mining.droneY = mining.returnZoneY;
    const MiningRunPresentation service = miningRunPresentation(state, catalog);
    const auto drillAction = std::find_if(service.actions.begin(), service.actions.end(), [](const PanelButtonPresentation& action) {
        return action.actionId == ui::actions::miningRepairDrill;
    });
    const auto droneAction = std::find_if(service.actions.begin(), service.actions.end(), [](const PanelButtonPresentation& action) {
        return action.actionId == ui::actions::miningRepairDrone;
    });
    require(drillAction != service.actions.end() && drillAction->enabled &&
            droneAction != service.actions.end() && droneAction->enabled,
        "ship radius should expose funded drill and drone repair actions");
    Random repairRng(94950);
    const PreparedLaunch repairLaunch = prepareLaunch(state, catalog, repairRng);
    const std::string repairHtml = buildGamePanelHtml({state, catalog, repairLaunch, repairLaunch});
    require(repairHtml.find("data-mining-ship-service=\"1\"") != std::string::npos, "docked repairs should emit a spatial ship-service marker");
    require(repairHtml.find("data-mining-return-x=") != std::string::npos && repairHtml.find("data-mining-return-y=") != std::string::npos, "ship-service marker should expose the return-zone projection anchor");
    require(repairHtml.find("data-rr-action=\"mining_repair_drill\"") != std::string::npos, "docked drill repair should render as a native command-dock button");
    require(repairHtml.find("data-rr-action=\"mining_repair_drone\"") != std::string::npos, "docked drone repair should render as a native command-dock button");

    require(repairMiningDrill(state), "funded ship service should repair a broken drill bit");
    require(mining.drillIntegrity == 1.0 && !mining.drillBreakNotified, "drill repair should restore integrity and clear the broken latch");
    require(mining.stowedMaterials.common == 6 && mining.stowedCargo == 6, "drill repair should consume banked common materials and their cargo mass");
    require(repairMiningDrone(state), "funded ship service should repair drone damage");
    require(mining.droneHealth == 1.0, "drone repair should restore full health");
    require(mining.stowedMaterials.common == 3 && mining.stowedCargo == 3, "drone repair should consume its proportional material and cargo cost");

    mining.drillIntegrity = 0.5;
    mining.stowedMaterials.common = 1;
    require(miningDrillRepairCost(mining) == 2, "half drill damage should cost half of a full rebuild");
    require(!repairMiningDrill(state), "unfunded ship repair should be rejected by game logic");
}

void transferFuelPersistsAndBecomesRigFuel()
{
    const ContentCatalog catalog = createDefaultContent();

    GameState arrival = createNewGame(catalog, 0xF113);
    arrival.run.destinationIndex = 1;
    arrival.screen = Screen::ArrivalOps;
    LaunchOutcome outcome;
    outcome.destinationId = content::destination::moon;
    outcome.recoveryMethod = RecoveryMethod::TransferArrival;
    outcome.frontierTransfer = true;
    outcome.transferFuelRemaining = 2.7;
    outcome.transferFuelCapacity = 15.0;
    startArrivalOps(arrival, outcome);

    const auto savedArrival = deserializeSaveData(serializeSaveData(captureSaveData(arrival)));
    require(savedArrival.has_value(), "Arrival Ops fuel save should parse");
    GameState restoredArrival = createNewGame(catalog, 1);
    restoreSaveData(restoredArrival, catalog, *savedArrival);
    require(nearlyEqual(restoredArrival.run.arrivalOps.transferFuelRemaining, 2.7) &&
            nearlyEqual(restoredArrival.run.arrivalOps.transferFuelCapacity, 15.0),
        "Arrival Ops must preserve transfer fuel remaining and capacity across save/load");

    introduceArrivalFlybyForTest(restoredArrival);
    startArrivalFlybyRun(restoredArrival, catalog);
    require(restoredArrival.run.flyby.active, "arrival flyby should start for fuel preservation coverage");
    abortFlybyRun(restoredArrival, catalog);
    require(nearlyEqual(restoredArrival.run.arrivalOps.transferFuelRemaining, 2.7) &&
            nearlyEqual(restoredArrival.run.arrivalOps.transferFuelCapacity, 15.0),
        "entering and leaving a flyby must not erase arrival transfer fuel");

    introduceArrivalFlybyForTest(restoredArrival);
    startArrivalFlybyRun(restoredArrival, catalog);
    restoredArrival.run.flyby.completed = true;
    restoredArrival.run.flyby.result = FlybyGrade::Good;
    completeFlybyRun(restoredArrival, catalog);
    require(nearlyEqual(restoredArrival.run.arrivalOps.transferFuelRemaining, 2.7),
        "completing a flyby must preserve arrival transfer fuel");

    startArrivalOrbitRun(restoredArrival, catalog);
    require(restoredArrival.run.orbit.active, "arrival orbit should start for fuel preservation coverage");
    abortOrbitRun(restoredArrival);
    require(nearlyEqual(restoredArrival.run.arrivalOps.transferFuelRemaining, 2.7) &&
            nearlyEqual(restoredArrival.run.arrivalOps.transferFuelCapacity, 15.0),
        "entering and leaving orbit must not erase arrival transfer fuel");

    startSurfaceExpedition(restoredArrival, catalog);
    require(nearlyEqual(restoredArrival.run.surfaceExpedition.expeditionPackFuel, 3.0) &&
            nearlyEqual(restoredArrival.run.surfaceExpedition.transferFuelRecovered, 2.7) &&
            nearlyEqual(restoredArrival.run.surfaceExpedition.rigFuel, 5.7),
        "landing must combine the isolated three-unit rig pack with exact transfer fuel remaining");

    for (const auto [remaining, expected] : {
             std::pair {0.0, 3.0},
             std::pair {2.0, 5.0},
             std::pair {5.0, 8.0}}) {
        GameState state = createNewGame(catalog, static_cast<std::uint64_t>(0xF200 + expected));
        state.run.destinationIndex = 1;
        state.run.arrivalOps = {true, content::destination::moon, remaining, 15.0};
        startSurfaceExpedition(state, catalog);
        require(nearlyEqual(state.run.surfaceExpedition.rigFuel, expected),
            "surface rig fuel must equal the three-unit expedition pack plus transfer fuel remaining");
    }

    GameState upgraded = createNewGame(catalog, 0xF114);
    upgraded.meta.launchUpgrades.fuelTanks = 2;
    const double baseEndurance = nominalSurfaceRigFuelCapacity(arrival, catalog, content::destination::moon);
    const double upgradedEndurance = nominalSurfaceRigFuelCapacity(upgraded, catalog, content::destination::moon);
    require(upgradedEndurance > baseEndurance,
        "larger transfer tanks must increase possible Mining Rig endurance through arrival surplus");

    GameState ark = createNewGame(catalog, 0xF115);
    ark.run.destinationIndex = 1;
    ark.meta.ark.condition = ArkCondition::DerelictOperable;
    ark.meta.ark.fuelReserve = 7;
    ark.run.arrivalOps = {true, content::destination::moon, 2.0, 15.0};
    startSurfaceExpedition(ark, catalog);
    require(nearlyEqual(ark.run.surfaceExpedition.expeditionPackFuel, 3.0) &&
            nearlyEqual(ark.run.surfaceExpedition.rigFuel, 5.0) &&
            ark.meta.ark.fuelReserve == 4,
        "Ark expeditions must debit at most three pack units and add recovered transfer fuel");

    ark.run.surfaceExpedition.rigFuel = 0.0;
    require(extractSurfacePayload(ark, catalog).applied,
        "empty rig fuel must never block the protected return stage or surface extraction");
}


void miningShipBankingLeaveAndEmergencyRecallRules()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 95959);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    state.run.surfaceExpedition.runRigUpgradeRanks = {
        {content::surfaceUpgrade::cargoSkids, 1},
        {content::surfaceUpgrade::emergencyWinch, 1}
    };
    prepareMiningSiteForTest(state);
    require(
        startMiningRun(state, catalog, {MiningAct::ActOne, 2, 95959}, false).applied,
        "mining should start for ship banking test with oxygen enabled");

    state.run.mining.temporaryMaterials.common = 2;
    state.run.mining.cargo = 2;
    state.run.mining.oxygenSeconds = 3.0;
    state.run.mining.oxygenDepletedNotified = true;
    const double oxygenCapacity = miningDrillStats(state, catalog).oxygenSeconds;
    state.run.mining.droneX = state.run.mining.returnZoneX;
    state.run.mining.droneY = state.run.mining.returnZoneY;
    updateMiningRun(state, catalog, 0.08);
    require(state.run.mining.temporaryMaterials.common == 0, "ship zone should clear carried materials after banking");
    require(state.run.mining.cargo == 0, "ship zone should clear carried cargo after banking");
    require(state.run.mining.stowedMaterials.common == 2, "ship zone should stow carried materials");
    require(state.run.mining.stowedCargo == 2, "ship zone should stow carried cargo");
    require(
        std::abs(state.run.mining.oxygenSeconds - oxygenCapacity) < 0.000001,
        "banking payload at the ship should refill the current oxygen capacity");
    require(!state.run.mining.oxygenDepletedNotified, "oxygen refill should reset the depletion warning latch");

    state.run.mining.oxygenSeconds = 3.0;
    updateMiningRun(state, catalog, 0.08);
    require(
        state.run.mining.oxygenSeconds < 3.0,
        "entering the ship zone without carried payload should not grant a free oxygen refill");

    MiningRunPresentation atShip = miningRunPresentation(state, catalog);
    require(std::any_of(atShip.actions.begin(), atShip.actions.end(), [](const PanelButtonPresentation& action) {
        return action.actionId == ui::actions::miningStow;
    }), "Leave should appear inside the ship radius");
    require(std::none_of(atShip.actions.begin(), atShip.actions.end(), [](const PanelButtonPresentation& action) {
        return action.actionId == ui::actions::miningAbort;
    }), "Emergency recall should not appear inside the ship radius");

    state.run.mining.droneX = state.run.mining.returnZoneX + tuning::mining::returnZoneRadiusCells * 0.95;
    state.run.mining.droneY = state.run.mining.returnZoneY;
    require(miningAtReturnZone(state.run.mining),
        "the visible loading radius should accept a mining rig near the outer service ring");

    state.run.mining.droneX = state.run.mining.returnZoneX + tuning::mining::returnZoneRadiusCells + 1.0;
    state.run.mining.temporaryMaterials.rare = 1;
    state.run.mining.cargo = 2;
    MiningRunPresentation away = miningRunPresentation(state, catalog);
    require(std::any_of(away.actions.begin(), away.actions.end(), [](const PanelButtonPresentation& action) {
        return action.actionId == ui::actions::miningAbort;
    }), "Emergency recall should appear away from the ship radius");
    require(std::none_of(away.actions.begin(), away.actions.end(), [](const PanelButtonPresentation& action) {
        return action.actionId == ui::actions::miningStow;
    }), "Leave should not appear away from the ship radius");

    const SurfaceActionOutcome recalled = finishMiningRun(state, catalog, true);
    require(recalled.applied, "emergency recall should finish the mining run");
    require(state.run.surfaceExpedition.temporaryMaterials.common == 2, "emergency recall should preserve banked materials");
    require(state.run.surfaceExpedition.temporaryMaterials.rare == 0, "emergency recall should lose carried materials");
    require(state.run.surfaceExpedition.cargo == 2, "emergency recall should preserve only banked cargo");
    require(runRigUpgradeRank(state, content::surfaceUpgrade::cargoSkids) == 1 &&
            runRigUpgradeRank(state, content::surfaceUpgrade::emergencyWinch) == 1,
        "emergency recall should preserve temporary run upgrades");
    require(recalled.hazardDelta >= tuning::mining::emergencyRecallHazardPenalty - 0.000001, "emergency recall should add the steep hazard penalty");
}

void miningSwarmNestPreviewAndPersistence()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 0x5A11);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);

    const MiningArenaRules earlyRules = resolveMiningArenaRules({MiningAct::ActTwo, 4, 19});
    require(!miningSwarmPreview(state, catalog, earlyRules, 0).available,
        "Swarms must not appear before Act 2 Level 5");

    MiningArenaRequest request {MiningAct::ActTwo, 5, 0};
    MiningSwarmPreview preview;
    for (std::uint64_t seed = 1; seed < 4096 && !preview.available; ++seed) {
        request.seed = seed;
        preview = miningSwarmPreview(state, catalog, resolveMiningArenaRules(request), 0);
    }
    require(preview.available && preview.depthZone >= 2 && preview.depthZone <= 4,
        "eligible Swarm Nest should deterministically select a depth two to four levels below the start");
    const MiningSwarmPreview repeat = miningSwarmPreview(state, catalog, resolveMiningArenaRules(request), 0);
    require(repeat.available && repeat.seed == preview.seed && repeat.depthZone == preview.depthZone &&
            std::abs(repeat.artifactChance - preview.artifactChance) < 0.000001,
        "Swarm preview must be repeatable for the same expedition seed");

    GameState lucky = state;
    lucky.run.surfaceExpedition.runRigUpgradeRanks = {{content::surfaceUpgrade::widebandPulse, 1}};
    const MiningSwarmPreview luckyPreview = miningSwarmPreview(lucky, catalog, resolveMiningArenaRules(request), 0);
    require(luckyPreview.available && luckyPreview.artifactChance >= preview.artifactChance,
        "current scanner luck modifiers should never reduce Swarm artifact chance");
    require(luckyPreview.artifactChance <= 0.35,
        "Swarm artifact chance should respect the advertised cap");

    require(startMiningRun(state, catalog, request, false).applied,
        "eligible mining run should begin for Swarm persistence coverage");
    require(state.run.mining.swarm.enabled && state.run.mining.swarm.depthZone == preview.depthZone,
        "the generated mining run should retain its previewed Swarm Nest");
    MiningRunState& mining = state.run.mining;
    mining.depthZone = mining.swarm.depthZone;
    mining.terrain.depthZone = mining.depthZone;
    mining.droneX = static_cast<double>(mining.terrain.width) * 0.5;
    mining.droneY = static_cast<double>(mining.terrain.height) * 0.25;
    mining.enemies.clear();
    updateMiningRun(state, catalog, 0.08);
    mining.droneX = static_cast<double>(mining.swarm.triggerX);
    mining.droneY = static_cast<double>(mining.swarm.chamberY);
    mining.droneHealth = 100000.0;
    mining.oxygenSeconds = 1000.0;
    mining.miniDrones.clear();
    for (int step = 0; step < 80 && mining.swarm.spawnedInWave == 0; ++step) {
        updateMiningRun(state, catalog, 0.08);
    }
    const auto firstSwarmEnemy = std::find_if(
        mining.enemies.begin(),
        mining.enemies.end(),
        [](const MiningEnemy& enemy) { return enemy.active && enemy.swarmAssociated; });
    require(firstSwarmEnemy != mining.enemies.end(), "Swarm wave should begin after its warning");
    require(
        firstSwarmEnemy->x < 0.0 || firstSwarmEnemy->x > static_cast<double>(mining.terrain.width) ||
            firstSwarmEnemy->y < 0.0 || firstSwarmEnemy->y > static_cast<double>(mining.terrain.height),
        "Swarm enemies should begin beyond the visible mine bounds instead of appearing beside the player");
    for (int step = 0; step < 160 && mining.swarm.spawnedInWave < 32; ++step) {
        updateMiningRun(state, catalog, 0.08);
    }
    const int openingSwarmEnemies = static_cast<int>(std::count_if(
        mining.enemies.begin(),
        mining.enemies.end(),
        [](const MiningEnemy& enemy) { return enemy.active && enemy.swarmAssociated; }));
    require(mining.swarm.wave == 1 && mining.swarm.waveSize == 32,
        "Act 2 Combine Swarm Nests should open with a 32-enemy horde");
    require(openingSwarmEnemies >= 30,
        "Swarm Nests should use their horde cap instead of the four-enemy procedural cap");
    const int swarmEnemiesInsideChamber = static_cast<int>(std::count_if(
        mining.enemies.begin(),
        mining.enemies.end(),
        [&](const MiningEnemy& enemy) {
            return enemy.active && enemy.swarmAssociated &&
                std::abs(enemy.x - mining.swarm.cacheX) <=
                    static_cast<double>(tuning::mining::swarmChamberHalfWidthCells) &&
                std::abs(enemy.y - mining.swarm.cacheY) <=
                    static_cast<double>(tuning::mining::swarmChamberHalfHeightCells);
        }));
    require(swarmEnemiesInsideChamber >= 16,
        "Off-screen Swarm enemies should complete their radial ingress instead of remaining beyond the mine");

    mining.gravityStrength = 0.0;
    mining.rigVelocityX = 0.0;
    mining.rigVelocityY = 0.0;
    const auto verifySwarmRetreat = [&](MiningEnemyType type, double startRadius, double cooldown) {
        const auto found = std::find_if(
            mining.enemies.begin(),
            mining.enemies.end(),
            [](const MiningEnemy& enemy) { return enemy.active && enemy.swarmAssociated; });
        require(found != mining.enemies.end(), "Swarm retreat fixture requires an active enemy");
        const std::size_t enemyIndex = static_cast<std::size_t>(std::distance(mining.enemies.begin(), found));
        MiningEnemy& enemy = *found;
        enemy.type = type;
        enemy.maxHealth = 1000.0;
        enemy.health = enemy.maxHealth;
        enemy.attackCooldownSeconds = cooldown;
        constexpr double goldenAngle = 2.39996322973;
        const double orbitDirection = enemyIndex % 2 == 0 ? 1.0 : -1.0;
        const double slotAngle = std::fmod(
            static_cast<double>(enemyIndex + 1) * goldenAngle +
                static_cast<double>(mining.swarm.wave) * 0.61 +
                mining.elapsedSeconds * tuning::mining::swarmOrbitRadiansPerSecond * orbitDirection,
            6.28318530718);
        enemy.x = mining.droneX + std::cos(slotAngle) * startRadius;
        enemy.y = mining.droneY +
            std::sin(slotAngle) * startRadius * tuning::mining::swarmVerticalRingScale;
        const double before = std::hypot(enemy.x - mining.droneX, enemy.y - mining.droneY);
        updateMiningRun(state, catalog, 0.08);
        const double after = std::hypot(enemy.x - mining.droneX, enemy.y - mining.droneY);
        require(after > before,
            "Swarm enemies on attack cooldown should retreat from the player before re-engaging");
        if (type == MiningEnemyType::Flying) {
            require(std::isfinite(enemy.velocityX) && std::isfinite(enemy.velocityY),
                "Swarm flying enemies should retain finite movement after reduced-speed steering");
        }
    };
    verifySwarmRetreat(
        MiningEnemyType::Ant,
        0.60,
        tuning::mining::swarmMeleeAttackIntervalSeconds);
    verifySwarmRetreat(
        MiningEnemyType::Flying,
        2.00,
        tuning::mining::swarmRangedAttackIntervalSeconds);

    for (int step = 0; step < 900 && !mining.swarm.cacheExposed; ++step) {
        updateMiningRun(state, catalog, 0.08);
        for (MiningEnemy& enemy : mining.enemies) {
            if (enemy.swarmAssociated) {
                enemy.active = false;
            }
        }
    }
    require(mining.swarm.cacheExposed, "clearing three Swarm waves should expose the cache");
    mining.droneX = mining.swarm.cacheX;
    mining.droneY = mining.swarm.cacheY;
    updateMiningRun(state, catalog, 0.08);
    require(mining.swarm.cacheClaimed && mining.cargo > 0,
        "the exposed Swarm cache should be a physical, recoverable payload");
    state.run.mining.swarm.alerted = true;
    state.run.mining.swarm.wave = 2;
    state.run.mining.swarm.spawnedInWave = 3;
    state.run.mining.swarm.cacheExposed = true;
    const auto saved = deserializeSaveData(serializeSaveData(captureSaveData(state)));
    require(saved.has_value(), "Swarm mining save should deserialize");
    GameState restored = createNewGame(catalog, 0x5A12);
    restoreSaveData(restored, catalog, *saved);
    require(restored.run.mining.swarm.enabled && restored.run.mining.swarm.alerted &&
            restored.run.mining.swarm.wave == 2 && restored.run.mining.swarm.cacheExposed &&
            restored.run.mining.swarm.seed == state.run.mining.swarm.seed,
        "active Swarm wave and cache state should survive save/load without rerolling");
}

void miningOxygenDrainsRigHealthBeforeEmergencyEjection()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 95960);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(
        startMiningRun(state, catalog, {MiningAct::ActOne, 2, 95960}, false).applied,
        "mining should start for oxygen drain test with oxygen pressure enabled");

    state.run.mining.oxygenSeconds = 0.0;
    const double healthBefore = state.run.mining.droneHealth;
    updateMiningRun(state, catalog, 0.08);
    require(!state.run.mining.failurePending, "zero oxygen should not recall immediately");
    require(state.run.mining.droneHealth < healthBefore, "zero oxygen should drain drone health");

    for (int i = 0; i < 260 && !state.run.mining.rigDisabled; ++i) {
        updateMiningRun(state, catalog, 0.08);
    }
    require(state.run.mining.rigDisabled,
        "rig health reaching zero should disable the rig");
    require(state.run.mining.operatorMode == MiningOperatorMode::Jetpack,
        "rig destruction should emergency-eject the operator into EVA");
    require(!state.run.mining.failurePending && state.run.mining.active,
        "a successful emergency ejection should preserve the active deployment");
}

void miningLoadBurdenAndUpgradeRelief()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState loaded = createNewGame(catalog, 95961);
    loaded.run.destinationIndex = 2;
    startSurfaceExpedition(loaded, catalog);
    prepareMiningSiteForTest(loaded);
    require(
        startMiningRun(loaded, catalog, {MiningAct::ActOne, 5, 95961}, false).applied,
        "mining should start for load burden test with cargo drag enabled");

    MiningLoadStats emptyLoad = miningLoadStats(loaded, catalog);
    loaded.run.mining.cargo = 9;
    MiningLoadStats heavyLoad = miningLoadStats(loaded, catalog);
    require(emptyLoad.speedMultiplier == 1.0, "empty mining load should not slow the drone");
    require(heavyLoad.currentLoad == 9.0, "carried cargo should count as load");
    require(heavyLoad.speedMultiplier < 1.0, "carried load should slow the drone");
    require(heavyLoad.fuelConsumptionMultiplier > 1.0, "carried load should increase fuel consumption rate");
    require(
        miningRigFuelConsumptionPerSecond(
            loaded,
            heavyLoad.fuelConsumptionMultiplier) >
            miningRigFuelConsumptionPerSecond(
                loaded,
                emptyLoad.fuelConsumptionMultiplier),
        "heavy cargo must increase real operating fuel consumption");
    require(heavyLoad.speedMultiplier >= tuning::mining::minLoadedSpeedMultiplier, "load slowdown should keep the minimum speed floor");

    GameState upgraded = loaded;
    upgraded.run.surfaceExpedition.runRigUpgradeRanks = {
        {content::surfaceUpgrade::expandablePanniers, 1},
        {content::surfaceUpgrade::vectorNozzles, 1}
    };
    upgraded.run.equippedModuleIds = {content::module::cargoSpine, content::module::haulerThrusters};
    MiningLoadStats upgradedLoad = miningLoadStats(upgraded, catalog);
    require(upgradedLoad.freeBuffer > heavyLoad.freeBuffer, "storage upgrades should increase the free carry buffer");
    require(upgradedLoad.speedMultiplier > heavyLoad.speedMultiplier, "engine upgrades should reduce load speed penalty");
    require(
        upgradedLoad.fuelConsumptionMultiplier < heavyLoad.fuelConsumptionMultiplier,
        "engine upgrades should reduce load fuel penalty");
    require(
        miningRigFuelConsumptionPerSecond(
            upgraded,
            upgradedLoad.fuelConsumptionMultiplier) <
            miningRigFuelConsumptionPerSecond(
                loaded,
                heavyLoad.fuelConsumptionMultiplier),
        "Hauler Thrusters and Vector Nozzles must continue reducing only the heavy-load fuel surcharge");
}

void miningRefitModulesImproveDrillProfileIncrementally()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState baseline = createNewGame(catalog, 96969);
    GameState upgraded = createNewGame(catalog, 96969);
    upgraded.meta.unlockKeys.push_back(content::unlock::surfaceProbes);
    upgraded.meta.unlockKeys.push_back(content::unlock::surfaceDrills);
    upgraded.meta.unlockKeys.push_back(content::unlock::cargoRigs);
    upgraded.run.equippedModuleIds = {
        content::module::surfaceMapper,
        content::module::regolithAuger,
        content::module::oreSorter,
        content::module::coolantSleeve,
        content::module::diamondBearings,
        content::module::deepBoreFrame,
        content::module::cargoSpine,
        content::module::haulerThrusters
    };

    const MiningDrillStats baseStats = miningDrillStats(baseline, catalog);
    const MiningDrillStats upgradedStats = miningDrillStats(upgraded, catalog);
    require(
        std::abs(baseStats.heatCoolingPerSecond - tuning::mining::heatCoolingPerSecond * tuning::mining::heatCoolingMultiplier) < 0.000001,
        "base drill cooling should apply the global cooling multiplier");
    require(upgradedStats.power > baseStats.power, "mining drill modules should improve terrain break speed");
    require(upgradedStats.oreYieldChance > baseStats.oreYieldChance, "mining yield modules should add bonus ore chance");
    require(upgradedStats.heatRiseScale < baseStats.heatRiseScale, "mining cooling modules should reduce heat rise");
    require(upgradedStats.heatCoolingPerSecond > baseStats.heatCoolingPerSecond, "mining cooling modules should improve heat recovery");
    require(upgradedStats.integrityRelief > baseStats.integrityRelief, "durability modules should protect the mining drill");
    require(upgradedStats.hardRockBounceRelief > baseStats.hardRockBounceRelief, "durability modules should reduce hard-rock recoil");
    require(upgradedStats.terrainWidth > baseStats.terrainWidth, "survey modules should widen the mining terrain");
    require(upgradedStats.terrainHeight > baseStats.terrainHeight, "deep-bore modules should deepen the mining terrain");
    require(upgradedStats.storage > baseStats.storage, "cargo refits should increase mining free carry");
    require(upgradedStats.engineEfficiency > baseStats.engineEfficiency, "hauler refits should reduce load burden");

    upgraded.run.destinationIndex = 2;
    startSurfaceExpedition(upgraded, catalog);
    prepareMiningSiteForTest(upgraded);
    require(startMiningRun(upgraded, catalog).applied, "upgraded mining state should start mining");
    require(upgraded.run.mining.terrain.width == upgradedStats.terrainWidth, "mining terrain should use upgraded width");
    require(upgraded.run.mining.terrain.height == upgradedStats.terrainHeight, "mining terrain should use upgraded depth");

}


void miningEvaFixedDrillProfileIgnoresRigUpgrades()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState baseline =
        activeMiningStateForEvaTest(catalog, 0xE7A201);
    GameState upgraded = baseline;
    activateOnlyCrew(upgraded, content::astronaut::eli);
    upgraded.run.equippedModuleIds = {
        content::module::regolithAuger,
        content::module::oreSorter,
        content::module::coolantSleeve,
        content::module::diamondBearings
    };
    upgraded.run.surfaceExpedition.runRigUpgradeRanks = {
        {content::surfaceUpgrade::thermalDrillJackets, 1},
        {content::surfaceUpgrade::shockMounts, 1},
        {content::surfaceUpgrade::oreScentArray, 1},
        {content::surfaceUpgrade::oreHopper, 1}
    };
    upgraded.meta.unlockKeys.push_back(content::unlock::droneBay);
    upgraded.meta.unlockKeys.push_back(content::unlock::perimeterDrones);
    upgraded.meta.droneBaySlots = 1;
    upgraded.meta.ownedDroneIds = {content::drone::defenseDrone};
    upgraded.meta.equippedDroneIds = {content::drone::defenseDrone};
    upgraded.run.surfaceExpedition.runDroneRanks = {
        {content::drone::defenseDrone, 3}
    };

    const MiningDrillStats operatorStats =
        miningOperatorDrillStats();
    const MiningDrillStats upgradedRigStats =
        miningDrillStats(upgraded, catalog);
    require(
        std::abs(
            operatorStats.power -
            tuning::mining::baseDrillPower *
                tuning::mining::operatorDrillPowerScale) < 0.000001 &&
            std::abs(operatorStats.heatRiseScale - 1.0) < 0.000001 &&
            std::abs(
                operatorStats.heatCoolingPerSecond -
                tuning::mining::heatCoolingPerSecond *
                    tuning::mining::heatCoolingMultiplier) < 0.000001 &&
            std::abs(operatorStats.oreYieldChance) < 0.000001 &&
            std::abs(operatorStats.rareYieldChance) < 0.000001 &&
            std::abs(operatorStats.integrityRelief) < 0.000001 &&
            std::abs(operatorStats.hardRockBounceRelief) < 0.000001,
        "the EVA drill profile should expose fixed base power, heat, yield, and durability behavior");
    require(
        upgradedRigStats.power > operatorStats.power &&
            upgradedRigStats.oreYieldChance > 0.26 &&
            upgradedRigStats.heatRiseScale < operatorStats.heatRiseScale &&
            upgradedRigStats.heatCoolingPerSecond >
                operatorStats.heatCoolingPerSecond &&
            upgradedRigStats.integrityRelief > operatorStats.integrityRelief &&
            upgradedRigStats.hardRockBounceRelief >
                operatorStats.hardRockBounceRelief,
        "the isolation fixture should contain meaningful rig, surface, crew-trait, and drone drill bonuses");

    auto configureEvaTerrain = [](GameState& state, double toughness) {
        MiningRunState& mining = state.run.mining;
        clearMiningTerrainForEvaTest(mining);
        mining.operatorMode = MiningOperatorMode::Jetpack;
        mining.operatorPresent = true;
        mining.operatorIntegrity = 1.0;
        mining.operatorX = 10.30;
        mining.operatorY = 10.50;
        mining.operatorVelocityX = 0.0;
        mining.operatorVelocityY = 0.0;
        mining.droneX = 30.0;
        mining.droneY = 20.0;
        mining.rigVelocityX = 0.0;
        mining.rigVelocityY = 0.0;
        mining.drillIntegrity = 1.0;
        mining.drillHeat = 0.0;
        mining.drillThermalLock = false;
        mining.cellsBroken = 0;
        mining.richRewardsAwarded = {};
        mining.looseChunks.clear();
        mining.miniDrones.clear();
        mining.artifact = {};
        mining.combatProjectiles.clear();
        mining.operatorFireCooldownSeconds = 0.0;
        mining.firing = false;
        mining.drilling = false;
        setMiningMove(state, 0.0, 0.0);
        setMiningAim(state, 1.0, 0.0);

        MiningCell* cell = miningCellAt(mining.terrain, 11, 10);
        require(cell != nullptr, "fixed EVA drill fixture should contain its target cell");
        *cell = {};
        cell->material = MiningCellMaterial::CommonOre;
        cell->maxToughness = toughness;
        cell->remainingToughness = toughness;
        cell->revealed = true;
    };

    GameState baselineDrill = baseline;
    GameState upgradedDrill = upgraded;
    configureEvaTerrain(baselineDrill, 100.0);
    configureEvaTerrain(upgradedDrill, 100.0);
    setMiningDrilling(baselineDrill, true);
    setMiningDrilling(upgradedDrill, true);
    updateMiningRun(baselineDrill, catalog, 0.08);
    updateMiningRun(upgradedDrill, catalog, 0.08);
    const MiningCell* baselineDrillCell =
        miningCellAt(baselineDrill.run.mining.terrain, 11, 10);
    const MiningCell* upgradedDrillCell =
        miningCellAt(upgradedDrill.run.mining.terrain, 11, 10);
    require(
        baselineDrillCell != nullptr &&
            upgradedDrillCell != nullptr &&
            baselineDrillCell->remainingToughness < 100.0 &&
            std::abs(
                baselineDrillCell->remainingToughness -
                upgradedDrillCell->remainingToughness) < 0.000001,
        "EVA hand-drill terrain power should remain fixed when the parked rig is heavily upgraded");
    require(
        baselineDrill.run.mining.drillHeat > 0.0 &&
            std::abs(
                baselineDrill.run.mining.drillHeat -
                upgradedDrill.run.mining.drillHeat) < 0.000001,
        "EVA hand-drill heat rise should not inherit rig cooling bonuses");

    setMiningDrilling(baselineDrill, false);
    setMiningDrilling(upgradedDrill, false);
    baselineDrill.run.mining.drillHeat = 0.5;
    upgradedDrill.run.mining.drillHeat = 0.5;
    updateMiningRun(baselineDrill, catalog, 0.08);
    updateMiningRun(upgradedDrill, catalog, 0.08);
    const double expectedCooledHeat =
        0.5 - operatorStats.heatCoolingPerSecond * 0.08;
    require(
        std::abs(
            baselineDrill.run.mining.drillHeat -
            expectedCooledHeat) < 0.000001 &&
            std::abs(
                upgradedDrill.run.mining.drillHeat -
                expectedCooledHeat) < 0.000001,
        "EVA hand-drill cooling should always use the fixed base recovery rate");

    GameState upgradedDrillYield = upgraded;
    configureEvaTerrain(upgradedDrillYield, 0.01);
    setMiningDrilling(upgradedDrillYield, true);
    updateMiningRun(upgradedDrillYield, catalog, 0.08);
    require(
        upgradedDrillYield.run.mining.looseChunks.size() == 1,
        "EVA hand-drilled ore should use fixed base yield even when the rig has yield bonuses");

    GameState upgradedSidearm = upgraded;
    configureEvaTerrain(upgradedSidearm, 100.0);
    setMiningFire(upgradedSidearm, true);
    updateMiningRun(upgradedSidearm, catalog, 0.01);
    const MiningCell* sidearmCell =
        miningCellAt(upgradedSidearm.run.mining.terrain, 11, 10);
    const double expectedSidearmTerrainDamage =
        operatorStats.power *
        tuning::mining::denseMaterialDrillPowerScale *
        tuning::mining::operatorSidearmIntervalSeconds *
        tuning::mining::operatorSidearmTerrainOutputScale;
    require(
        sidearmCell != nullptr &&
            std::abs(
                (100.0 - sidearmCell->remainingToughness) -
                expectedSidearmTerrainDamage) < 0.000001,
        "EVA sidearm terrain output should remain 30 percent of the fixed hand-drill profile");

    GameState upgradedSidearmYield = upgraded;
    configureEvaTerrain(upgradedSidearmYield, 0.01);
    setMiningFire(upgradedSidearmYield, true);
    updateMiningRun(upgradedSidearmYield, catalog, 0.01);
    require(
        upgradedSidearmYield.run.mining.looseChunks.size() == 1,
        "EVA sidearm-broken ore should not inherit rig yield bonuses");
}

void activeMiningRoundTripsThroughSave()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 94949);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "mining should start before save");
    state.statusLine = std::string(text::status::miningStarted);
    state.run.mining.droneX = 21.5;
    state.run.mining.droneY = 9.25;
    state.run.mining.hullDirX = -0.6;
    state.run.mining.hullDirY = 0.8;
    state.run.mining.returnZoneX = 31.5;
    state.run.mining.returnZoneY = 4.25;
    state.run.mining.rigTethered = true;
    state.run.mining.droneHealth = 0.72;
    state.run.mining.fuelCycleProgress = 0.30;
    state.run.mining.fuelSpent = 2;
    state.run.mining.temporaryMaterials.exotic = 1;
    state.run.mining.stowedMaterials.common = 2;
    state.run.mining.stowedCargo = 3;
    state.run.mining.stowedArtifacts.push_back({"banked_artifact", content::destination::mars, false});
    state.run.mining.enemiesDefeated = 3;
    state.run.mining.defenseDamageDealt = 4.25;
    state.run.mining.enemyDamageTaken = 0.125;
    state.run.mining.areaControlDamageDealt = 1.5;
    state.run.mining.reactiveArmorDamageDealt = 0.75;
    state.run.mining.environmentalShieldAbsorbed = 0.25;
    state.run.mining.elementalExposureSeconds = 2.5;
    state.run.mining.movementSlowSeconds = 0.4;
    state.run.mining.movementSlowScale = 0.62;
    state.run.mining.enemyTheme = MiningEnemyTheme::Radioactive;
    state.run.mining.enemies = {
        {MiningEnemyType::Elemental, MiningCellFeature::EncounterZone, 22.5, 10.5, 1.0, -0.5, 2.5, 4.0, 0.0, 3.1, 0.48, 1.8, true, MiningElementalAffinity::Radiation}
    };
    state.run.mining.enemies.front().attackAnimationSeconds = 0.21;
    state.run.mining.enemies.front().hitAnimationSeconds = 0.13;
    state.run.mining.enemies.front().defeatAnimationSeconds = 0.31;
    if (MiningCell* cell = miningCellAt(state.run.mining.terrain, 20, 10)) {
        cell->material = MiningCellMaterial::RareOre;
        cell->maxToughness = 7.0;
        cell->remainingToughness = 3.5;
        cell->revealed = true;
        cell->feature = MiningCellFeature::BossChamber;
        cell->enemy = MiningEnemyType::Mammal;
        cell->suitOnlyPassage = true;
    }
    if (MiningCell* hazard = miningCellAt(state.run.mining.terrain, 21, 10)) {
        hazard->material = MiningCellMaterial::HazardPocket;
        hazard->maxToughness = 5.0;
        hazard->remainingToughness = 5.0;
        hazard->revealed = true;
        hazard->hazard = true;
        hazard->hazardAffinity = MiningElementalAffinity::Toxic;
    }

    const SaveData legacySave = captureSaveData(state);
    GameState legacyRestored = createNewGame(catalog, 2);
    restoreSaveData(legacyRestored, catalog, legacySave);
    require(!legacyRestored.run.mining.rigTethered,
        "a legacy save with the retired ship-to-rig tether must normalize it away");

    const std::string serialized = serializeSaveData(legacySave);
    require(serialized.find("miningFuelCycle=") != std::string::npos, "active mining saves should store normalized fuel cycle progress");
    require(serialized.find("miningFuelBurn=") == std::string::npos, "new saves should not write the legacy seconds-based fuel field");
    const auto save = deserializeSaveData(serialized);
    require(save.has_value(), "active mining save should parse");

    GameState restored = createNewGame(catalog, 1);
    restoreSaveData(restored, catalog, *save);
    require(restored.screen == Screen::Mining, "active mining screen should round trip");
    require(restored.run.surfaceExpedition.miningSitePrepared && restored.run.surfaceExpedition.miningRunUsed, "active mining restore should preserve the one-run surface state");
    require(restored.run.mining.active, "active mining state should round trip");
    require(std::abs(restored.run.mining.droneX - 21.5) < 0.000001, "mining drone x should round trip");
    require(std::abs(restored.run.mining.hullDirX + 0.6) < 0.000001, "mining hull heading x should round trip");
    require(std::abs(restored.run.mining.hullDirY - 0.8) < 0.000001, "mining hull heading y should round trip");
    require(std::abs(restored.run.mining.returnZoneX - 31.5) < 0.000001, "mining return zone x should round trip");
    require(std::abs(restored.run.mining.returnZoneY - 4.25) < 0.000001, "mining return zone y should round trip");
    require(!restored.run.mining.rigTethered,
        "canonical saves should write the retired ship-to-rig tether as inactive");
    require(std::abs(restored.run.mining.droneHealth - 0.72) < 0.000001, "mining drone health should round trip");
    require(std::abs(restored.run.mining.fuelCycleProgress - 0.30) < 0.000001, "mining fuel cycle should round trip");
    require(restored.run.mining.fuelSpent == 2, "mining fuel spend should round trip");
    require(restored.run.mining.temporaryMaterials.exotic == 1, "mining temporary materials should round trip");
    require(restored.run.mining.stowedMaterials.common == 2, "mining stowed materials should round trip");
    require(restored.run.mining.stowedCargo == 3, "mining stowed cargo should round trip");
    require(restored.run.mining.stowedArtifacts.size() == 1, "mining stowed artifacts should round trip");
    require(restored.run.mining.enemiesDefeated == 3, "mining defeated enemy count should round trip");
    require(std::abs(restored.run.mining.defenseDamageDealt - 4.25) < 0.000001, "mining defense damage should round trip");
    require(std::abs(restored.run.mining.enemyDamageTaken - 0.125) < 0.000001, "mining enemy damage should round trip");
    require(std::abs(restored.run.mining.areaControlDamageDealt - 1.5) < 0.000001, "mining area-control damage should round trip");
    require(std::abs(restored.run.mining.reactiveArmorDamageDealt - 0.75) < 0.000001, "mining reactive armor damage should round trip");
    require(std::abs(restored.run.mining.environmentalShieldAbsorbed - 0.25) < 0.000001, "mining shield absorption should round trip");
    require(std::abs(restored.run.mining.elementalExposureSeconds - 2.5) < 0.000001, "mining elemental exposure should round trip");
    require(std::abs(restored.run.mining.movementSlowSeconds - 0.4) < 0.000001, "mining slow timer should round trip");
    require(std::abs(restored.run.mining.movementSlowScale - 0.62) < 0.000001, "mining slow scale should round trip");
    require(restored.run.mining.enemyTheme == MiningEnemyTheme::Radioactive,
        "the active site's coherent enemy theme should round trip");
    require(restored.run.mining.enemies.size() == 1, "active mining enemies should round trip");
    require(restored.run.mining.enemies.front().type == MiningEnemyType::Elemental, "active mining enemy type should round trip");
    require(restored.run.mining.enemies.front().sourceFeature == MiningCellFeature::EncounterZone, "active mining enemy source feature should round trip");

    require(restored.run.mining.enemies.front().affinity == MiningElementalAffinity::Radiation, "active mining enemy affinity should round trip");
    require(std::abs(restored.run.mining.enemies.front().health - 2.5) < 0.000001, "active mining enemy health should round trip");
    require(std::abs(restored.run.mining.enemies.front().attackAnimationSeconds - 0.21) < 0.000001
            && std::abs(restored.run.mining.enemies.front().hitAnimationSeconds - 0.13) < 0.000001
            && std::abs(restored.run.mining.enemies.front().defeatAnimationSeconds - 0.31) < 0.000001,
        "enemy attack, hit, and defeat presentation timers should round trip");
    const MiningCell* restoredCell = miningCellAt(restored.run.mining.terrain, 20, 10);
    require(restoredCell != nullptr && restoredCell->material == MiningCellMaterial::RareOre, "mining terrain material should round trip");
    require(restoredCell != nullptr && std::abs(restoredCell->remainingToughness - 3.5) < 0.000001, "mining terrain toughness should round trip");
    require(restoredCell != nullptr && restoredCell->feature == MiningCellFeature::BossChamber, "mining terrain feature metadata should round trip");
    require(restoredCell != nullptr && restoredCell->enemy == MiningEnemyType::Mammal, "mining terrain enemy metadata should round trip");
    require(restoredCell != nullptr && restoredCell->suitOnlyPassage, "active mining suit-only passage metadata should round trip");
    const MiningCell* restoredHazard = miningCellAt(restored.run.mining.terrain, 21, 10);
    require(restoredHazard != nullptr && restoredHazard->material == MiningCellMaterial::HazardPocket &&
            restoredHazard->hazardAffinity == MiningElementalAffinity::Toxic,
        "mining hazard affinity should round trip with active terrain");
}

void miningEvaAndSwarmStateRoundTrips()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 0xE6A);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "EVA persistence test should start mining");

    MiningRunState& mining = state.run.mining;
    MiningCell* activeSuitPassage =
        miningCellAt(mining.terrain, 6, 6);
    require(
        activeSuitPassage != nullptr,
        "version-six EVA save should have an active-layer passage cell");
    activeSuitPassage->material = MiningCellMaterial::Empty;
    activeSuitPassage->suitOnlyPassage = true;
    mining.rigVelocityX = -1.75;
    mining.rigVelocityY = 2.25;
    mining.rigDisabled = true;
    mining.rigDepthZone = mining.depthZone;
    mining.operatorMode = MiningOperatorMode::Jetpack;
    mining.operatorPresent = true;
    mining.operatorX = 18.25;
    mining.operatorY = 12.75;
    mining.operatorVelocityX = 3.5;
    mining.operatorVelocityY = -0.75;
    mining.operatorAimDirX = 0.6;
    mining.operatorAimDirY = 0.8;
    mining.operatorIntegrity = 0.63;
    mining.operatorFireCooldownSeconds = 0.11;
    mining.operatorFirePulseSeconds = 0.07;
    mining.gravityDirectionX = 0.8;
    mining.gravityDirectionY = 0.6;
    mining.gravityStrength = 3.75;

    MiningLooseChunk activeChunk;
    activeChunk.material = MiningCellMaterial::RareOre;
    activeChunk.x = 14.5;
    activeChunk.y = 16.25;
    activeChunk.velocityX = -0.3;
    activeChunk.velocityY = 0.9;
    activeChunk.cargoValue = 3;
    mining.looseChunks = {activeChunk};

    MiningMiniDroneAgent miningAgent;
    miningAgent.role = MiniDroneRole::Mining;
    miningAgent.roleIndex = 0;
    miningAgent.x = 17.0;
    miningAgent.y = 12.0;
    miningAgent.velocityX = 0.4;
    miningAgent.velocityY = -0.2;
    miningAgent.anchorTarget = MiningAnchorTarget::Operator;
    miningAgent.stableFormationSlot = 2;
    miningAgent.orbitPhaseRadians = 1.75;
    miningAgent.haulMaterials.rare = 2;
    miningAgent.shieldCharge = 0.45;
    miningAgent.actionCooldownSeconds = 0.35;

    MiningMiniDroneAgent defenseAgent;
    defenseAgent.role = MiniDroneRole::Defense;
    defenseAgent.roleIndex = 0;
    defenseAgent.x = 19.0;
    defenseAgent.y = 13.5;
    defenseAgent.anchorTarget = MiningAnchorTarget::ControlledActor;
    defenseAgent.stableFormationSlot = 1;
    defenseAgent.orbitPhaseRadians = 4.25;
    defenseAgent.shieldCharge = 0.77;
    defenseAgent.shieldRechargeSeconds = 0.6;
    mining.miniDrones = {miningAgent, defenseAgent};

    MiningDepthLayerState cachedLayer;
    cachedLayer.depthZone = mining.depthZone + 1;
    cachedLayer.terrain = mining.terrain;
    cachedLayer.terrain.depthZone = cachedLayer.depthZone;
    MiningCell* cachedSuitPassage =
        miningCellAt(cachedLayer.terrain, 7, 7);
    require(
        cachedSuitPassage != nullptr,
        "version-six EVA save should have a cached-layer passage cell");
    cachedSuitPassage->material = MiningCellMaterial::Empty;
    cachedSuitPassage->suitOnlyPassage = true;
    MiningCell* activeNonPassage =
        miningCellAt(mining.terrain, 7, 7);
    require(
        activeNonPassage != nullptr,
        "version-six EVA save should have a distinct active-layer cell");
    activeNonPassage->suitOnlyPassage = false;
    MiningLooseChunk cachedChunk;
    cachedChunk.material = MiningCellMaterial::ExoticVein;
    cachedChunk.x = 8.5;
    cachedChunk.y = 20.5;
    cachedChunk.velocityX = 0.2;
    cachedChunk.velocityY = -0.4;
    cachedChunk.cargoValue = 5;
    cachedLayer.looseChunks = {cachedChunk};
    mining.depthLayers = {cachedLayer};
    mining.deepestDepthZone = cachedLayer.depthZone;

    const SaveData captured = captureSaveData(state);
    require(captured.version == save_schema::currentVersion, "new saves should use the current schema version");
    const std::string serialized = serializeSaveData(captured);
    require(serialized.find("miningRigState=") != std::string::npos, "version-six saves should write rig state");
    require(serialized.find("miningOperatorState=") != std::string::npos, "version-six saves should write operator state");
    require(serialized.find("miningGravity=") != std::string::npos, "version-six saves should write vector gravity");
    require(serialized.find("miningLooseChunks=") != std::string::npos, "version-six saves should write loose chunks");
    const std::optional<SaveData> parsed = deserializeSaveData(serialized);
    require(parsed.has_value(), "version-ten EVA save should deserialize");

    GameState restored = createNewGame(catalog, 0xE6B);
    restoreSaveData(restored, catalog, *parsed);
    const MiningRunState& result = restored.run.mining;
    require(result.operatorMode == MiningOperatorMode::Jetpack && result.operatorPresent,
        "active EVA mode should round trip");
    require(result.rigDisabled && result.rigDepthZone == mining.rigDepthZone,
        "parked or disabled rig state should round trip");
    require(std::abs(result.rigVelocityX + 1.75) < 0.000001 &&
            std::abs(result.rigVelocityY - 2.25) < 0.000001,
        "rig velocity should round trip");
    require(std::abs(result.operatorX - 18.25) < 0.000001 &&
            std::abs(result.operatorY - 12.75) < 0.000001 &&
            std::abs(result.operatorVelocityX - 3.5) < 0.000001 &&
            std::abs(result.operatorVelocityY + 0.75) < 0.000001,
        "operator position and velocity should round trip");
    require(std::abs(result.operatorAimDirX - 0.6) < 0.000001 &&
            std::abs(result.operatorAimDirY - 0.8) < 0.000001 &&
            std::abs(result.operatorIntegrity - 0.63) < 0.000001,
        "operator aim and integrity should round trip");
    require(std::abs(result.gravityDirectionX - 0.8) < 0.000001 &&
            std::abs(result.gravityDirectionY - 0.6) < 0.000001 &&
            std::abs(result.gravityStrength - 3.75) < 0.000001,
        "gravity direction and strength should round trip");
    require(result.looseChunks.size() == 1 &&
            result.looseChunks.front().material == MiningCellMaterial::RareOre &&
            result.looseChunks.front().cargoValue == 3 &&
            std::abs(result.looseChunks.front().velocityY - 0.9) < 0.000001,
        "active-layer loose chunks should round trip");
    const MiningCell* restoredActiveSuitPassage =
        miningCellAt(result.terrain, 6, 6);
    require(
        restoredActiveSuitPassage != nullptr &&
            restoredActiveSuitPassage->suitOnlyPassage,
        "version-six saves should preserve active-layer suit-only passages");
    require(result.depthLayers.size() == 1 &&
            result.depthLayers.front().looseChunks.size() == 1 &&
            result.depthLayers.front().looseChunks.front().material == MiningCellMaterial::ExoticVein &&
            result.depthLayers.front().looseChunks.front().cargoValue == 5,
        "cached depth-layer loose chunks should round trip");
    const MiningCell* restoredCachedSuitPassage =
        result.depthLayers.empty()
        ? nullptr
        : miningCellAt(result.depthLayers.front().terrain, 7, 7);
    require(
        restoredCachedSuitPassage != nullptr &&
            restoredCachedSuitPassage->suitOnlyPassage,
        "version-six saves should preserve cached depth-layer suit-only passages");
    require(result.miniDrones.size() == 2 &&
            result.miniDrones[0].anchorTarget == MiningAnchorTarget::Operator &&
            result.miniDrones[0].stableFormationSlot == 2 &&
            std::abs(result.miniDrones[0].orbitPhaseRadians - 1.75) < 0.000001 &&
            result.miniDrones[0].haulMaterials.rare == 2,
        "mini-drone anchor, formation, phase, and haul should round trip");
    require(result.miniDrones[1].anchorTarget == MiningAnchorTarget::ControlledActor &&
            result.miniDrones[1].stableFormationSlot == 1 &&
            std::abs(result.miniDrones[1].shieldCharge - 0.77) < 0.000001,
        "independent defense-drone state should round trip");
}

void operatorRigTetherRoundTripsThroughSave()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 0x70A);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "operator tether persistence test should start mining");

    MiningRunState& mining = state.run.mining;
    mining.operatorMode = MiningOperatorMode::Jetpack;
    mining.operatorPresent = true;
    mining.operatorX = mining.returnZoneX + 1.0;
    mining.operatorY = mining.returnZoneY;
    mining.droneX = mining.returnZoneX + 3.0;
    mining.droneY = mining.returnZoneY;
    mining.rigTethered = false;
    mining.operatorRigTethered = true;
    mining.rigDisabled = true;

    const auto parsed = deserializeSaveData(serializeSaveData(captureSaveData(state)));
    require(parsed.has_value(), "operator tether save should deserialize");
    GameState restored = createNewGame(catalog, 0x70B);
    restoreSaveData(restored, catalog, *parsed);
    require(restored.run.mining.operatorRigTethered &&
            restored.run.mining.rigDisabled &&
            !restored.run.mining.rigTethered,
        "an active EVA tow line to a disabled rig should round trip without restoring a ship-to-rig tether");
}


void miningDepthLayersAreBidirectionalAndPersistent()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 0xD37A);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    const auto hasReturnShaft = [](const MiningTerrain& terrain) {
        const int leftX = terrain.width / 2 - 1;
        for (int y = 0; y < terrain.height - 1; ++y) {
            for (int x = leftX; x <= leftX + 1; ++x) {
                const MiningCell* cell = miningCellAt(terrain, x, y);
                if (cell == nullptr || cell->material != MiningCellMaterial::Empty ||
                    cell->feature != MiningCellFeature::MainTunnel || cell->suitOnlyPassage) {
                    return false;
                }
            }
        }
        return true;
    };
    require(
        startMiningRun(state, catalog, {MiningAct::ActOne, 2, 0xD37A}, false).applied,
        "depth-route test should start an oxygen-enabled mining run");

    MiningRunState& entry = state.run.mining;
    const int entryDepth = entry.depthZone;
    const double shipX = entry.returnZoneX;
    const double shipY = entry.returnZoneY;
    MiningCell* entryMarker = miningCellAt(entry.terrain, 5, 5);
    require(entryMarker != nullptr, "entry depth should expose a persistence marker cell");
    *entryMarker = {MiningCellMaterial::RareOre, 17.0, 6.5, true, false};
    for (int y = entry.terrain.height - 5; y < entry.terrain.height; ++y) {
        for (int x = 0; x < entry.terrain.width; ++x) {
            if (MiningCell* cell = miningCellAt(entry.terrain, x, y)) {
                *cell = {MiningCellMaterial::Empty, 0.0, 0.0, true, false};
            }
        }
    }
    entry.droneX = static_cast<double>(entry.terrain.width) * 0.5;
    entry.droneY = static_cast<double>(entry.terrain.height) - 2.1;
    const double hazardBeforeDescent = entry.hazardDelta;
    updateMiningRun(state, catalog, 0.01);

    MiningRunState& deep = state.run.mining;
    require(deep.depthZone == entryDepth + 1, "crossing the lower edge should descend exactly one depth");
    require(deep.entryDepthZone == entryDepth && deep.deepestDepthZone == entryDepth + 1,
        "the entry depth should remain fixed while deepest depth advances");
    require(deep.depthLayers.size() == 1 && deep.depthLayers.front().depthZone == entryDepth,
        "descending should cache the complete entry layer");
    require(hasReturnShaft(deep.depthLayers.front().terrain) && !hasReturnShaft(deep.terrain),
        "descending should open the layer left behind without pre-carving the newly reached depth");
    require(std::abs(deep.returnZoneX - shipX) < 0.000001 && std::abs(deep.returnZoneY - shipY) < 0.000001,
        "the shuttle anchor must not move when descending");
    require(!miningAtReturnZone(deep), "the ship zone must be unavailable below the entry layer");
    require(deep.hazardDelta >= hazardBeforeDescent + tuning::mining::depthHazardRisk - 0.000001,
        "first entry into a deeper layer should add depth hazard");

    MiningCell* deepMarker = miningCellAt(deep.terrain, 6, 6);
    require(deepMarker != nullptr, "deeper depth should expose a persistence marker cell");
    *deepMarker = {MiningCellMaterial::ExoticVein, 23.0, 4.25, true, false};
    deep.enemies.clear();
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < deep.terrain.width; ++x) {
            if (MiningCell* cell = miningCellAt(deep.terrain, x, y)) {
                *cell = {MiningCellMaterial::Empty, 0.0, 0.0, true, false};
            }
        }
    }
    deep.depthTransitionCooldownSeconds = 0.0;
    deep.droneX = static_cast<double>(deep.terrain.width) * 0.5;
    deep.droneY = 2.1;
    updateMiningRun(state, catalog, 0.01);

    MiningRunState& returned = state.run.mining;
    require(returned.depthZone == entryDepth, "crossing the upper edge should return to the previous depth");
    const MiningCell* restoredEntryMarker = miningCellAt(returned.terrain, 5, 5);
    require(restoredEntryMarker != nullptr && restoredEntryMarker->material == MiningCellMaterial::RareOre &&
            std::abs(restoredEntryMarker->remainingToughness - 6.5) < 0.000001,
        "ascending should restore the previously carved entry terrain exactly");
    require(returned.depthLayers.size() == 1 && returned.depthLayers.front().depthZone == entryDepth + 1,
        "ascending should cache the deeper layer for a later revisit");
    require(hasReturnShaft(returned.terrain) && !hasReturnShaft(returned.depthLayers.front().terrain),
        "the return route should remain on the prior layer while the revisited deeper layer stays normal");
    returned.droneX = returned.returnZoneX;
    returned.droneY = returned.returnZoneY;
    require(miningAtReturnZone(returned), "the fixed shuttle should become available again on the entry layer");

    const double hazardBeforeRevisit = returned.hazardDelta;
    returned.depthTransitionCooldownSeconds = 0.0;
    returned.droneX = returned.downwardTransitionX;
    returned.droneY = static_cast<double>(returned.terrain.height) - 2.1;
    updateMiningRun(state, catalog, 0.01);
    require(state.run.mining.depthZone == entryDepth + 1, "the cached lower route should remain traversable");
    const MiningCell* restoredDeepMarker = miningCellAt(state.run.mining.terrain, 6, 6);
    require(restoredDeepMarker != nullptr && restoredDeepMarker->material == MiningCellMaterial::ExoticVein &&
            std::abs(restoredDeepMarker->remainingToughness - 4.25) < 0.000001,
        "revisiting a depth should restore its terrain instead of rerolling it");
    require(state.run.mining.enemies.empty(), "revisiting should preserve cleared enemies");
    require(std::abs(state.run.mining.hazardDelta - hazardBeforeRevisit) < 0.000001,
        "revisiting an explored depth should not charge the new-depth hazard twice");

    const std::optional<SaveData> parsed = deserializeSaveData(serializeSaveData(captureSaveData(state)));
    require(parsed.has_value(), "a mining run with cached depth layers should serialize");
    GameState restored = createNewGame(catalog, 1);
    restoreSaveData(restored, catalog, *parsed);
    require(restored.run.mining.depthZone == entryDepth + 1 && restored.run.mining.entryDepthZone == 0,
        "active depth should survive save restore while the ship normalizes to surface");
    require(restored.run.mining.depthLayers.size() == 1 && restored.run.mining.depthLayers.front().depthZone == entryDepth,
        "the cached return route should survive save restore");
    const MiningCell* savedEntryMarker = miningCellAt(restored.run.mining.depthLayers.front().terrain, 5, 5);
    require(savedEntryMarker != nullptr && savedEntryMarker->material == MiningCellMaterial::RareOre,
        "saved depth layers should preserve their modified terrain");
    require(!hasReturnShaft(restored.run.mining.terrain) &&
            hasReturnShaft(restored.run.mining.depthLayers.front().terrain),
        "active mining saves should preserve shafts only on the prior layers that earned them");

}

void miningDeploysDeepAndGeneratesTheRouteBackToSurface()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 0xD37B);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    const auto hasReturnShaft = [](const MiningTerrain& terrain) {
        const int leftX = terrain.width / 2 - 1;
        for (int y = 0; y < terrain.height - 1; ++y) {
            for (int x = leftX; x <= leftX + 1; ++x) {
                const MiningCell* cell = miningCellAt(terrain, x, y);
                if (cell == nullptr || cell->material != MiningCellMaterial::Empty ||
                    cell->feature != MiningCellFeature::MainTunnel || cell->suitOnlyPassage) {
                    return false;
                }
            }
        }
        return true;
    };
    state.run.surfaceExpedition.depth = 2;
    require(
        startMiningRun(state, catalog, {MiningAct::ActOne, 2, 0xD37B}, false).applied,
        "deep deployment test should start mining");
    require(state.run.mining.depthZone == 2 &&
            state.run.mining.entryDepthZone == 0 &&
            state.run.mining.rigDepthZone == 2,
        "the rig should begin at start depth +2 while the ship remains at surface zero");
    require(!hasReturnShaft(state.run.mining.terrain),
        "pushed-depth deployment should begin in normal mining terrain without a pre-carved shaft");

    const auto ascendOneLayer = [&]() {
        MiningRunState& mining = state.run.mining;
        mining.enemies.clear();
        for (int y = 0; y < 5; ++y) {
            for (int x = 0; x < mining.terrain.width; ++x) {
                if (MiningCell* cell = miningCellAt(mining.terrain, x, y)) {
                    *cell = {MiningCellMaterial::Empty, 0.0, 0.0, true, false};
                }
            }
        }
        mining.depthTransitionCooldownSeconds = 0.0;
        mining.droneX = static_cast<double>(mining.terrain.width) * 0.5;
        mining.droneY = 2.1;
        updateMiningRun(state, catalog, 0.01);
    };

    ascendOneLayer();
    require(state.run.mining.depthZone == 1 &&
            state.run.mining.depthLayers.size() == 1 &&
            state.run.mining.depthLayers.front().depthZone == 2,
        "ascending from a deep deployment should generate depth +1 and cache depth +2");
    require(hasReturnShaft(state.run.mining.terrain) &&
            !hasReturnShaft(state.run.mining.depthLayers.front().terrain),
        "ascending should carve the previous depth while leaving the starting depth normal");
    state.run.mining.droneX = state.run.mining.returnZoneX;
    state.run.mining.droneY = state.run.mining.returnZoneY;
    require(!miningAtReturnZone(state.run.mining),
        "the ship service zone must remain unavailable on an intermediate layer");

    ascendOneLayer();
    require(state.run.mining.depthZone == 0,
        "the generated ascent route should reach the fixed surface layer");
    require(hasReturnShaft(state.run.mining.terrain),
        "each newly reached prior layer should provide an uninterrupted route toward the surface ship");
    state.run.mining.droneX = state.run.mining.returnZoneX;
    state.run.mining.droneY = state.run.mining.returnZoneY;
    require(miningAtReturnZone(state.run.mining),
        "extraction and service should become available only at the surface ship");
}

void miningDestinationGravityAndEvaMotionUsePhysicalProfiles()
{
    ContentCatalog catalog = createDefaultContent();
    const std::array<std::pair<std::string_view, double>, 9> expectedScales {{
        {content::destination::earthOrbit, 0.15},
        {content::destination::moon, 0.35},
        {content::destination::mars, 0.60},
        {content::destination::jupiter, 1.15},
        {content::destination::saturn, 0.95},
        {content::destination::uranus, 0.80},
        {content::destination::neptune, 1.05},
        {content::destination::nearbyStar, 1.20},
        {content::destination::nearbyGalaxy, 0.25}
    }};
    for (const auto& [id, scale] : expectedScales) {
        const Destination* destination = catalog.findDestination(id);
        require(destination != nullptr, "every EVA gravity identity should resolve by stable destination id");
        require(
            std::abs(destination->gravityDirectionX) < 0.000001 &&
                std::abs(destination->gravityDirectionY - 1.0) < 0.000001 &&
                std::abs(destination->gravityScale - scale) < 0.000001,
            "destination gravity should preserve the approved downward vector and scale");
    }

    Destination* mars = nullptr;
    for (Destination& destination : catalog.destinations) {
        if (destination.id == content::destination::mars) {
            mars = &destination;
            break;
        }
    }
    require(mars != nullptr, "Mars should be available for vector-gravity startup coverage");
    mars->gravityDirectionX = 3.0;
    mars->gravityDirectionY = 4.0;
    mars->gravityScale = 0.50;

    GameState state = createNewGame(catalog, 0xE7A100);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(
        startMiningRun(
            state,
            catalog,
            {MiningAct::ActOne, 4, 0xE7A100},
            false)
            .applied,
        "vector-gravity EVA test should start mining");
    MiningRunState& mining = state.run.mining;
    require(
        std::none_of(mining.terrain.cells.begin(), mining.terrain.cells.end(), [](const MiningCell& cell) {
            return cell.suitOnlyPassage;
        }),
        "generated mining layers should not add invisible rig-only collision cells");
    require(
        std::abs(mining.gravityDirectionX - 0.60) < 0.000001 &&
            std::abs(mining.gravityDirectionY - 0.80) < 0.000001,
        "mining startup should normalize vector-valued destination gravity");
    require(
        std::abs(
            mining.gravityStrength -
            tuning::mining::baseGravityCellsPerSecondSquared * 0.50) <
            0.000001,
        "mining startup should scale base gravity by destination identity");

    clearMiningTerrainForEvaTest(mining);
    mining.gravityDirectionX = 0.60;
    mining.gravityDirectionY = 0.80;
    mining.gravityStrength =
        tuning::mining::baseGravityCellsPerSecondSquared * 0.50;
    mining.droneX = 30.0;
    mining.droneY = 12.0;
    require(toggleMiningOperator(state), "an open test chamber should permit EVA");
    mining.operatorX = 30.0;
    mining.operatorY = 12.0;
    mining.operatorVelocityX = 0.0;
    mining.operatorVelocityY = 0.0;
    setMiningMove(state, 0.0, 0.0);
    updateMiningRun(state, catalog, 0.08);
    require(
        std::abs(mining.operatorVelocityX - 0.144) < 0.002 &&
            std::abs(mining.operatorVelocityY - 0.192) < 0.002,
        "an unpowered EVA operator should accelerate along the destination gravity vector");

    mining.operatorVelocityX = 0.0;
    mining.operatorVelocityY = 0.0;
    setMiningMove(state, 1.0, 0.0);
    updateMiningRun(state, catalog, 0.08);
    const double firstThrustSpeed = std::hypot(
        mining.operatorVelocityX,
        mining.operatorVelocityY);
    require(
        firstThrustSpeed > 2.0 &&
            firstThrustSpeed <=
                tuning::mining::operatorAccelerationCellsPerSecondSquared * 0.08 +
                    0.30,
        "the EVA mobility profile should apply its high initial acceleration without an impulse jump");
    for (int tick = 0; tick < 30; ++tick) {
        updateMiningRun(state, catalog, 0.08);
    }
    const double terminalSpeed = std::hypot(
        mining.operatorVelocityX,
        mining.operatorVelocityY);
    require(
        terminalSpeed > 4.35 &&
            terminalSpeed <= tuning::mining::operatorSpeedCellsPerSecond + 0.000001,
        "EVA thrust and gravity should remain bounded by the suit maximum-speed profile");
}

void miningEvaTogglePassagesAndExtractionRulesAreSafe()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = activeMiningStateForEvaTest(catalog, 0xE7A101);
    MiningRunState& mining = state.run.mining;
    mining.droneX = 20.5;
    mining.droneY = 20.5;
    for (MiningCell& cell : mining.terrain.cells) {
        cell = {
            MiningCellMaterial::Bedrock,
            1000.0,
            1000.0,
            true,
            false
        };
    }
    require(
        !toggleMiningOperator(state),
        "rig exit should fail when no adjacent cell can safely contain the suit collider");

    clearMiningTerrainForEvaTest(mining);
    mining.droneX = 20.5;
    mining.droneY = 20.5;
    require(toggleMiningOperator(state), "rig exit should use a safe adjacent open cell");
    require(tuning::mining::operatorEntryDistanceCells == 2.50,
        "the operator re-entry radius should be doubled to 2.5 cells");
    mining.operatorX =
        mining.droneX + tuning::mining::operatorEntryDistanceCells + 0.10;
    mining.operatorY = mining.droneY;
    setMiningOperatorToggleProgress(state, 0.50);
    require(mining.operatorToggleProgress == 0.0,
        "the re-entry hold ring should stay hidden when the rig cannot be boarded");
    require(
        !toggleMiningOperator(state),
        "the operator should not board from beyond the 2.5-cell entry distance");
    mining.operatorX =
        mining.droneX + tuning::mining::operatorEntryDistanceCells - 0.05;
    setMiningOperatorToggleProgress(state, 0.50);
    require(mining.operatorToggleProgress == 0.50,
        "the re-entry hold ring should appear once boarding is possible");
    require(toggleMiningOperator(state), "the operator should board from within the entry distance");

    mining.droneX = 20.30;
    mining.droneY = 20.50;
    mining.rigVelocityX = 0.0;
    mining.rigVelocityY = 0.0;
    MiningCell* aperture = miningCellAt(mining.terrain, 21, 20);
    require(aperture != nullptr, "suit-only traversal test aperture should exist");
    aperture->suitOnlyPassage = true;
    setMiningMove(state, 1.0, 0.0);
    for (int tick = 0; tick < 10; ++tick) {
        updateMiningRun(state, catalog, 0.08);
    }
    require(
        mining.droneX > 21.25,
        "legacy suit-only metadata on an Empty cell must not create an invisible wall for the rig");

    setMiningMove(state, 0.0, 0.0);
    mining.droneX = 20.30;
    mining.droneY = 20.50;
    mining.rigVelocityX = 0.0;
    mining.rigVelocityY = 0.0;
    require(toggleMiningOperator(state), "legacy passage metadata should still permit an adjacent EVA exit");
    mining.operatorX = 20.30;
    mining.operatorY = 20.50;
    mining.operatorVelocityX = 0.0;
    mining.operatorVelocityY = 0.0;
    setMiningMove(state, 1.0, 0.0);
    for (int tick = 0; tick < 10; ++tick) {
        updateMiningRun(state, catalog, 0.08);
    }
    require(
        mining.operatorX > 21.25,
        "the smaller suit collider should traverse the same suit-only passage");

    mining.droneX = 12.0;
    mining.droneY = static_cast<double>(mining.terrain.height) - 2.2;
    mining.rigDepthZone = mining.depthZone;
    mining.rigVelocityX = 0.0;
    mining.rigVelocityY = 0.0;
    mining.operatorX = mining.droneX + tuning::mining::operatorEntryDistanceCells - 0.05;
    mining.operatorY = mining.droneY;
    mining.operatorVelocityX = 0.0;
    mining.operatorVelocityY = 0.0;
    mining.depthTransitionCooldownSeconds = 0.0;
    setMiningMove(state, 0.0, 0.0);
    updateMiningRun(state, catalog, 0.08);
    require(mining.depthZone == mining.rigDepthZone,
        "an EVA operator in re-entry range at the lower boundary should not be forced into the next depth");

    mining.operatorX = mining.returnZoneX;
    mining.operatorY = mining.returnZoneY;
    mining.droneX =
        mining.returnZoneX + tuning::mining::returnZoneRadiusCells + 2.0;
    require(
        !finishMiningRun(state, catalog, false).applied,
        "normal extraction should reject an operator who leaves a functioning rig behind");
    mining.droneX = mining.returnZoneX;
    mining.droneY = mining.returnZoneY;
    require(
        finishMiningRun(state, catalog, false).applied,
        "normal extraction should accept the operator and functioning rig together at the shuttle");
}

void miningEvaLooseChunksResourceRecoveryAndSidearmAreDeterministic()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = activeMiningStateForEvaTest(catalog, 0xE7A102);
    MiningRunState& mining = state.run.mining;
    mining.droneX = 30.0;
    mining.droneY = 20.0;
    require(toggleMiningOperator(state), "loose-chunk test should enter EVA");
    mining.operatorX = 10.30;
    mining.operatorY = 10.50;
    mining.operatorAimDirX = 1.0;
    mining.operatorAimDirY = 0.0;
    MiningCell* ore = miningCellAt(mining.terrain, 11, 10);
    require(ore != nullptr, "EVA hand-drill test ore should exist");
    *ore = {MiningCellMaterial::CommonOre, 0.01, 0.01, true, false};
    const MaterialInventory materialsBefore = mining.temporaryMaterials;
    const int cargoBefore = mining.cargo;
    setMiningDrilling(state, true);
    updateMiningRun(state, catalog, 0.08);
    setMiningDrilling(state, false);
    require(
        ore->material == MiningCellMaterial::Empty &&
            !mining.looseChunks.empty(),
        "the EVA hand drill should turn ore into a spatial loose chunk");
    require(
        mining.temporaryMaterials.common == materialsBefore.common &&
            mining.cargo == cargoBefore,
        "the zero-cargo suit should not place hand-drilled ore directly into carried payload");

    mining.droneX = mining.looseChunks.front().x;
    mining.droneY = mining.looseChunks.front().y;
    updateMiningRun(state, catalog, 0.01);
    require(
        mining.looseChunks.empty() &&
            mining.temporaryMaterials.common == materialsBefore.common + 1 &&
            mining.cargo > cargoBefore,
        "contact with the parked rig should collect a loose chunk into rig cargo");

    mining.operatorX = 10.50;
    mining.operatorY = 10.50;
    mining.operatorAimDirX = 1.0;
    mining.operatorAimDirY = 0.0;
    MiningEnemy first =
        createMiningEnemy(MiningEnemyType::Ant, MiningCellFeature::EncounterZone, 12.0, 10.5);
    MiningEnemy second =
        createMiningEnemy(MiningEnemyType::Ant, MiningCellFeature::EncounterZone, 14.0, 10.5);
    first.health = first.maxHealth = 10.0;
    second.health = second.maxHealth = 10.0;
    first.speed = second.speed = 0.0;
    first.damagePerSecond = second.damagePerSecond = 0.0;
    mining.enemies = {first, second};
    mining.alliedFireCooldownSeconds = 1.0;
    setMiningFire(state, true);
    updateMiningRun(state, catalog, 0.01);
    require(
        std::abs(mining.enemies[0].health - 7.6) < 0.000001 &&
            std::abs(mining.enemies[1].health - 10.0) < 0.000001,
        "the EVA sidearm should fire immediately and damage only the deterministic first hit");
    require(
        !mining.combatProjectiles.empty() &&
            !mining.combatProjectiles.back().critical &&
            std::any_of(mining.damageNumbers.begin(), mining.damageNumbers.end(), [](const MiningDamageNumber& number) {
                return number.team == MiningCombatTeam::Allied &&
                    !number.critical &&
                    std::abs(number.amount - tuning::mining::operatorSidearmDamage) <
                        0.000001;
            }),
        "the EVA sidearm should emit a non-critical, non-piercing hit presentation");

    GameState resourceState = createNewGame(catalog, 0xE7A103);
    resourceState.meta.unlockKeys.push_back(content::unlock::droneBay);
    resourceState.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
    ensureDroneBayState(resourceState, catalog);
    resourceState.meta.droneBaySlots = 1;
    resourceState.meta.equippedDroneIds = {content::drone::resourceDrone};
    resourceState.run.destinationIndex = 2;
    startSurfaceExpedition(resourceState, catalog);
    prepareMiningSiteForTest(resourceState);
    require(
        startMiningRun(
            resourceState,
            catalog,
            {MiningAct::ActOne, 4, 0xE7A103},
            false)
            .applied,
        "Resource-drone loose-chunk test should start mining");
    clearMiningTerrainForEvaTest(resourceState.run.mining);
    MiningMiniDroneAgent& resource =
        resourceState.run.mining.miniDrones.front();
    const MiniDroneCoordinationPoint home =
        miniDroneOrbitPoint(resourceState.run.mining, resource);
    resource.x = home.x;
    resource.y = home.y;
    resource.velocityX = 0.0;
    resource.velocityY = 0.0;
    resource.behavior = MiningMiniDroneBehavior::Working;
    resource.actionCooldownSeconds = 0.0;
    resourceState.run.mining.looseChunks.push_back(
        {MiningCellMaterial::CommonOre, home.x, home.y, 0.0, 0.0, 1, true});
    updateMiningRun(resourceState, catalog, 0.08);
    require(
        resource.haulMaterials.common == 1 &&
            resourceState.run.mining.looseChunks.empty(),
        "a Resource drone should spatially collect a nearby loose chunk into preserved haul");
}

void miningSwarmAnchorTransfersPreserveRuntimeState()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = activeMiningStateForEvaTest(catalog, 0xE7A104);
    MiningRunState& mining = state.run.mining;
    mining.miniDrones.clear();
    const std::array<MiniDroneRole, 6> roles {
        MiniDroneRole::Mining,
        MiniDroneRole::Resource,
        MiniDroneRole::Survey,
        MiniDroneRole::Hazard,
        MiniDroneRole::Attack,
        MiniDroneRole::Defense
    };
    for (std::size_t index = 0; index < roles.size(); ++index) {
        MiningMiniDroneAgent agent;
        agent.role = roles[index];
        agent.x = 8.0 + static_cast<double>(index);
        agent.y = 9.0 + static_cast<double>(index) * 0.25;
        agent.velocityX = 0.4 + static_cast<double>(index) * 0.1;
        agent.velocityY = -0.2;
        agent.targetCellX = 3;
        agent.targetCellY = 4;
        agent.targetEnemyIndex = 1;
        agent.actionCooldownSeconds = 0.7 + static_cast<double>(index) * 0.1;
        agent.shieldCharge = 0.35 + static_cast<double>(index) * 0.05;
        agent.shieldRechargeSeconds = 1.2;
        agent.shieldImpactSeconds = 0.4;
        agent.haulMaterials.common = static_cast<int>(index) + 1;
        agent.stableFormationSlot = 0;
        agent.orbitPhaseRadians = 0.20 + static_cast<double>(index) * 0.31;
        mining.miniDrones.push_back(agent);
    }
    const std::vector<MiningMiniDroneAgent> before = mining.miniDrones;
    mining.operatorMode = MiningOperatorMode::Jetpack;
    mining.operatorPresent = true;
    mining.operatorX = 24.0;
    mining.operatorY = 16.0;
    mining.operatorVelocityX = 1.1;
    mining.operatorVelocityY = -0.4;
    transferMiniDroneSwarmAnchor(
        mining,
        MiningOperatorMode::Rig,
        MiningOperatorMode::Jetpack,
        false);
    for (std::size_t index = 0; index < mining.miniDrones.size(); ++index) {
        const MiningMiniDroneAgent& agent = mining.miniDrones[index];
        const MiningMiniDroneAgent& original = before[index];
        require(
            std::abs(agent.x - original.x) < 0.000001 &&
                std::abs(agent.y - original.y) < 0.000001 &&
                std::abs(agent.velocityX - original.velocityX) < 0.000001 &&
                std::abs(agent.velocityY - original.velocityY) < 0.000001,
            "same-layer Rig-to-Operator transfer should not snap or overwrite drone motion");
        require(
            agent.haulMaterials.common == original.haulMaterials.common &&
                std::abs(agent.shieldCharge - original.shieldCharge) < 0.000001 &&
                std::abs(agent.shieldRechargeSeconds - original.shieldRechargeSeconds) <
                    0.000001 &&
                std::abs(agent.actionCooldownSeconds - original.actionCooldownSeconds) <
                    0.000001 &&
                std::abs(agent.orbitPhaseRadians - original.orbitPhaseRadians) <
                    0.000001,
            "same-layer anchor transfer should preserve haul, shield, cooldown, and orbit state");
        require(
            agent.targetCellX < 0 &&
                agent.targetCellY < 0 &&
                agent.targetEnemyIndex < 0 &&
                agent.behavior == MiningMiniDroneBehavior::Returning,
            "anchor transfer should release layer-local tasks and immediately recall the swarm");
    }
    require(
        resolveMiniDroneAnchor(mining).actor == MiningActorIdentity::Operator,
        "ControlledActor anchors should resolve to the EVA operator after transfer");

    mining.depthZone += 1;
    mining.operatorX = 31.0;
    mining.operatorY = 18.0;
    mining.operatorVelocityX = -0.8;
    mining.operatorVelocityY = 0.3;
    std::vector<MiniDroneCoordinationPoint> expected;
    for (const MiningMiniDroneAgent& agent : mining.miniDrones) {
        expected.push_back(miniDroneOrbitPoint(mining, agent));
    }
    const std::vector<MiningMiniDroneAgent> beforeDepth = mining.miniDrones;
    transferMiniDroneSwarmAnchor(
        mining,
        MiningOperatorMode::Jetpack,
        MiningOperatorMode::Jetpack,
        true);
    for (std::size_t index = 0; index < mining.miniDrones.size(); ++index) {
        const MiningMiniDroneAgent& agent = mining.miniDrones[index];
        require(
            std::abs(agent.x - expected[index].x) < 0.000001 &&
                std::abs(agent.y - expected[index].y) < 0.000001,
            "cross-depth transfer should recreate deterministic role formation positions");
        require(
            agent.haulMaterials.common ==
                    beforeDepth[index].haulMaterials.common &&
                std::abs(agent.shieldCharge - beforeDepth[index].shieldCharge) <
                    0.000001 &&
                std::abs(
                    agent.actionCooldownSeconds -
                    beforeDepth[index].actionCooldownSeconds) <
                    0.000001 &&
                std::abs(agent.orbitPhaseRadians - beforeDepth[index].orbitPhaseRadians) <
                    0.000001,
            "cross-depth transfer should preserve haul, shields, cooldowns, and orbit phases");
        require(
            std::abs(agent.velocityX - mining.operatorVelocityX) < 0.000001 &&
                std::abs(agent.velocityY - mining.operatorVelocityY) < 0.000001,
            "cross-depth formation recreation should inherit the new anchor motion");
    }
    for (std::size_t lhs = 0; lhs < expected.size(); ++lhs) {
        for (std::size_t rhs = lhs + 1; rhs < expected.size(); ++rhs) {
            require(
                std::hypot(
                    mining.miniDrones[lhs].x - mining.miniDrones[rhs].x,
                    mining.miniDrones[lhs].y - mining.miniDrones[rhs].y) >
                    0.15,
                "mixed-role orbit points should remain unique after a depth transfer");
        }
    }
}

void miningEmergencyEvaFailureAndRecoveryRulesHold()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState ejected = createNewGame(catalog, 0xE7A105);
    ejected.meta.unlockKeys.push_back(content::unlock::droneBay);
    ensureDroneBayState(ejected, catalog);
    ejected.meta.droneBaySlots = 1;
    ejected.meta.equippedDroneIds = {content::drone::miningDrone};
    ejected.run.destinationIndex = 2;
    startSurfaceExpedition(ejected, catalog);
    prepareMiningSiteForTest(ejected);
    require(
        startMiningRun(
            ejected,
            catalog,
            {MiningAct::ActOne, 4, 0xE7A105},
            false)
            .applied,
        "emergency-EVA test should start mining");
    clearMiningTerrainForEvaTest(ejected.run.mining);
    ejected.run.mining.droneHealth = 0.0;
    updateMiningRun(ejected, catalog, 0.01);
    require(
        ejected.run.mining.active &&
            ejected.run.mining.rigDisabled &&
            ejected.run.mining.operatorMode == MiningOperatorMode::Jetpack &&
            ejected.run.mining.operatorPresent &&
            !ejected.run.mining.failurePending,
        "rig destruction should emergency-eject a live operator without ending the run");
    require(
        resolveMiniDroneAnchor(ejected.run.mining).actor ==
            MiningActorIdentity::Operator,
        "the emergency-ejected swarm should resolve to the suit rather than the wreck");
    ejected.run.mining.operatorX = ejected.run.mining.returnZoneX;
    ejected.run.mining.operatorY = ejected.run.mining.returnZoneY;
    ejected.run.mining.droneX =
        ejected.run.mining.returnZoneX +
        tuning::mining::returnZoneRadiusCells + 8.0;
    require(
        finishMiningRun(ejected, catalog, false).applied,
        "an emergency-ejected operator should complete safe recovery without returning the wreck");

    GameState towedWreck = activeMiningStateForEvaTest(catalog, 0xE7A107);
    clearMiningTerrainForEvaTest(towedWreck.run.mining);
    towedWreck.run.mining.droneHealth = 0.0;
    updateMiningRun(towedWreck, catalog, 0.01);
    MiningRunState& disabledRig = towedWreck.run.mining;
    require(
        disabledRig.rigDisabled &&
            disabledRig.operatorMode == MiningOperatorMode::Jetpack &&
            disabledRig.operatorPresent,
        "the wreck-tow test should start from a disabled rig and live EVA operator");
    disabledRig.gravityStrength = 0.0;
    disabledRig.operatorX = disabledRig.droneX + 4.0;
    disabledRig.operatorY = disabledRig.droneY;
    const MiningTetherTargetResolution disabledRigTarget = resolveMiningTetherTarget(disabledRig);
    require(
        disabledRigTarget.target == MiningTetherTarget::MiningRig &&
            disabledRigTarget.blocker == MiningTetherBlocker::None,
        "a same-depth disabled Mining Rig should remain an EVA tow target");
    const double distanceBeforeTow = std::hypot(
        disabledRig.operatorX - disabledRig.droneX,
        disabledRig.operatorY - disabledRig.droneY);
    toggleMiningTether(towedWreck);
    require(disabledRig.operatorRigTethered,
        "T should attach EVA to a nearby disabled Mining Rig");
    updateMiningRun(towedWreck, catalog, 0.40);
    const double distanceAfterTow = std::hypot(
        disabledRig.operatorX - disabledRig.droneX,
        disabledRig.operatorY - disabledRig.droneY);
    require(distanceAfterTow < distanceBeforeTow,
        "an attached disabled Mining Rig should move toward the EVA operator");
    disabledRig.operatorX = disabledRig.returnZoneX;
    disabledRig.operatorY = disabledRig.returnZoneY;
    disabledRig.droneX = disabledRig.returnZoneX + tuning::mining::returnZoneRadiusCells + 3.0;
    disabledRig.droneY = disabledRig.returnZoneY;
    require(!finishMiningRun(towedWreck, catalog, false).applied,
        "an attached disabled rig must reach the shuttle with its EVA operator");
    disabledRig.droneX = disabledRig.returnZoneX;
    disabledRig.droneY = disabledRig.returnZoneY;
    disabledRig.oxygenSeconds = 0.0;
    disabledRig.stowedMaterials.common = 3;
    disabledRig.stowedCargo = disabledRig.stowedMaterials.common;
    updateMiningRun(towedWreck, catalog, 0.01);
    require(
        towedWreck.screen == Screen::Mining &&
            disabledRig.active &&
            disabledRig.rigDisabled &&
            disabledRig.oxygenSeconds > 0.0 &&
            !disabledRig.operatorRigTethered,
        "towing a disabled rig to the shuttle should dock for service, restore life support, and keep the run active");
    const MiningRunPresentation disabledRigService =
        miningRunPresentation(towedWreck, catalog);
    const auto disabledRigRepair = std::find_if(
        disabledRigService.actions.begin(),
        disabledRigService.actions.end(),
        [](const PanelButtonPresentation& action) {
            return action.actionId == ui::actions::miningRepairDrone;
        });
    require(
        disabledRigRepair != disabledRigService.actions.end() &&
            disabledRigRepair->enabled &&
            disabledRigRepair->label.find("Shuttle patch") != std::string::npos &&
            disabledRigRepair->label.find("35%") != std::string::npos,
        "ship service should explain the external 35% shuttle patch instead of presenting a hidden ore cost");
    const int shipCommonBeforeRecovery = disabledRig.stowedMaterials.common;
    require(repairMiningDrone(towedWreck),
        "ship service should repair a disabled rig after it is towed home");
    require(
        disabledRig.active &&
            !disabledRig.rigDisabled &&
            nearlyEqual(
                disabledRig.droneHealth,
                tuning::mining::emergencyRigRecoveryIntegrity) &&
            disabledRig.stowedMaterials.common == shipCommonBeforeRecovery,
        "external recovery should patch a disabled rig without spending ship ore or ending the run");
    const PreparedLaunch emergencyRepairPanelLaunch {};
    const std::string evaRepairPanel = buildGamePanelHtml({
        towedWreck,
        catalog,
        emergencyRepairPanelLaunch,
        emergencyRepairPanelLaunch});
    require(
        evaRepairPanel.find("id=\"rr-hud-mining-actor-integrity-label\">SUIT INTEGRITY") != std::string::npos &&
            evaRepairPanel.find("id=\"rr-hud-mining-actor-integrity\">100%") != std::string::npos,
        "the active-actor readout should continue reporting suit integrity while the operator remains in EVA");
    require(toggleMiningOperator(towedWreck),
        "the EVA operator should be able to re-enter the repaired rig and keep mining");
    require(
        disabledRig.operatorMode == MiningOperatorMode::Rig &&
            disabledRig.active,
        "re-entering the repaired rig should retain the active expedition");
    const std::string repairedRigPanel = buildGamePanelHtml({
        towedWreck,
        catalog,
        emergencyRepairPanelLaunch,
        emergencyRepairPanelLaunch});
    require(
        repairedRigPanel.find("id=\"rr-hud-mining-actor-integrity-label\">RIG INTEGRITY") != std::string::npos &&
            repairedRigPanel.find("id=\"rr-hud-mining-actor-integrity\">35%") != std::string::npos,
        "re-entering after roadside assistance should report the rig's actual 35% integrity instead of the full EVA suit value");

    GameState failed = activeMiningStateForEvaTest(catalog, 0xE7A106);
    MiningRunState& mining = failed.run.mining;
    require(toggleMiningOperator(failed), "suit-failure test should enter EVA");
    mining.artifact.present = true;
    mining.artifact.state = MiningArtifactState::Loose;
    mining.artifact.tethered = true;
    mining.miniDrones.push_back({});
    mining.miniDrones.back().velocityX = 2.0;
    mining.miniDrones.back().velocityY = -1.0;
    mining.operatorIntegrity = 0.0;
    updateMiningRun(failed, catalog, 0.01);
    require(
        mining.failurePending &&
            !mining.artifact.tethered &&
            !mining.firing &&
            !mining.drilling,
        "zero suit integrity should release the tether and freeze active operator actions");
    require(
        std::all_of(mining.miniDrones.begin(), mining.miniDrones.end(), [](const MiningMiniDroneAgent& agent) {
            return std::abs(agent.velocityX) < 0.000001 &&
                std::abs(agent.velocityY) < 0.000001;
        }),
        "suit-integrity failure should freeze the entire mini-drone swarm");
}

void miningEvaAuditRegressionGuardsHold()
{
    const ContentCatalog catalog = createDefaultContent();
    const auto startWithDrones = [&](std::uint64_t seed,
                                     const std::vector<std::string>& droneIds,
                                     bool hostile) {
        GameState state = createNewGame(catalog, seed);
        state.meta.unlockKeys.push_back(content::unlock::droneBay);
        state.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
        state.meta.unlockKeys.push_back(content::unlock::perimeterDrones);
        if (std::find(droneIds.begin(), droneIds.end(), content::drone::hazardDrone) != droneIds.end()) {
            state.meta.unlockKeys.push_back(content::unlock::ioHazardDrone);
        }
        if (hostile) {
            state.meta.campaignMilestone =
                CampaignMilestone::HostileSystemStranded;
            state.meta.ark.condition = ArkCondition::DamagedStranded;
            state.meta.ark.fuelReserve =
                tuning::ark::hostileSystemFuelReserve;
            state.meta.unlockKeys.push_back(content::unlock::deepSpace);
        }
        ensureDroneBayState(state, catalog);
        state.meta.droneBaySlots = static_cast<int>(droneIds.size());
        state.meta.equippedDroneIds = droneIds;
        state.run.destinationIndex = hostile ? 4 : 2;
        startSurfaceExpedition(state, catalog);
        prepareMiningSiteForTest(state);
        require(
            startMiningRun(
                state,
                catalog,
                {MiningAct::ActOne, 4, seed},
                false)
                .applied,
            "audit regression fixture should start a mining run");
        clearMiningTerrainForEvaTest(state.run.mining);
        return state;
    };

    GameState isolatedCargo = startWithDrones(
        0xE7A107,
        {content::drone::resourceDrone},
        false);
    MiningRunState& isolatedMining = isolatedCargo.run.mining;
    isolatedMining.droneX = 18.0;
    isolatedMining.droneY = 18.0;
    isolatedMining.rigDepthZone = isolatedMining.depthZone;
    isolatedMining.temporaryMaterials.common = 2;
    isolatedMining.cargo = 2 * tuning::mining::commonCargo;
    require(
        toggleMiningOperator(isolatedCargo),
        "Resource isolation fixture should enter EVA");
    isolatedMining.operatorX = 34.0;
    isolatedMining.operatorY = 18.0;
    MiningMiniDroneAgent& isolatedResource =
        isolatedMining.miniDrones.front();
    MiniDroneCoordinationPoint isolatedHome =
        miniDroneOrbitPoint(isolatedMining, isolatedResource);
    isolatedResource.x = isolatedHome.x;
    isolatedResource.y = isolatedHome.y;
    isolatedResource.velocityX = 0.0;
    isolatedResource.velocityY = 0.0;
    isolatedResource.behavior = MiningMiniDroneBehavior::Working;
    isolatedResource.actionCooldownSeconds = 0.0;
    updateMiningRun(isolatedCargo, catalog, 0.08);
    require(
        isolatedResource.haulMaterials.common == 0 &&
            isolatedMining.temporaryMaterials.common == 2 &&
            isolatedMining.cargo == 2 * tuning::mining::commonCargo,
        "a Resource drone following a distant operator must not pull cargo from the parked rig");

    isolatedMining.operatorX = isolatedMining.droneX + 0.25;
    isolatedMining.operatorY = isolatedMining.droneY;
    isolatedMining.rigDisabled = true;
    isolatedResource.x = isolatedMining.droneX + 0.30;
    isolatedResource.y = isolatedMining.droneY;
    isolatedResource.velocityX = 0.0;
    isolatedResource.velocityY = 0.0;
    isolatedResource.behavior = MiningMiniDroneBehavior::Working;
    isolatedResource.actionCooldownSeconds = 0.0;
    updateMiningRun(isolatedCargo, catalog, 0.08);
    require(
        isolatedResource.haulMaterials.common == 0 &&
            isolatedMining.temporaryMaterials.common == 2,
        "a Resource drone must not extract cargo from a disabled rig even while nearby");

    GameState artifactImmunity =
        activeMiningStateForEvaTest(catalog, 0xE7A108);
    MiningRunState& artifactMining = artifactImmunity.run.mining;
    require(
        toggleMiningOperator(artifactImmunity),
        "artifact-immunity fixture should enter EVA");
    artifactMining.operatorX = 10.30;
    artifactMining.operatorY = 10.50;
    artifactMining.operatorAimDirX = 1.0;
    artifactMining.operatorAimDirY = 0.0;
    MiningCell* artifactCell =
        miningCellAt(artifactMining.terrain, 11, 10);
    require(artifactCell != nullptr,
        "artifact-immunity fixture should have a target cell");
    *artifactCell = {
        MiningCellMaterial::ArtifactCache,
        4.0,
        4.0,
        true,
        false
    };
    artifactMining.artifact.present = true;
    artifactMining.artifact.state = MiningArtifactState::Loose;
    artifactMining.artifact.x = 11.5;
    artifactMining.artifact.y = 10.5;
    artifactMining.artifact.health = 0.73;
    artifactMining.artifact.maxHealth = 1.0;
    setMiningFire(artifactImmunity, true);
    updateMiningRun(artifactImmunity, catalog, 0.01);
    setMiningFire(artifactImmunity, false);
    require(
        std::abs(artifactCell->remainingToughness - 4.0) < 0.000001 &&
            std::abs(artifactMining.artifact.health - 0.73) < 0.000001 &&
            !artifactMining.combatProjectiles.empty(),
        "the EVA sidearm should visibly stop at an artifact cache without damaging its cell or artifact");

    GameState hardRecall = startWithDrones(
        0xE7A109,
        {content::drone::attackDrone, content::drone::hazardDrone},
        true);
    MiningRunState& recallMining = hardRecall.run.mining;
    const int hazardX =
        std::clamp(
            static_cast<int>(std::floor(recallMining.droneX)) + 1,
            1,
            recallMining.terrain.width - 2);
    const int hazardY =
        std::clamp(
            static_cast<int>(std::floor(recallMining.droneY)) + 1,
            1,
            recallMining.terrain.height - 2);
    *miningCellAt(recallMining.terrain, hazardX, hazardY) = {
        MiningCellMaterial::HazardPocket,
        2.0,
        2.0,
        true,
        false
    };
    MiningEnemy recallThreat = createMiningEnemy(
        MiningEnemyType::Flying,
        MiningCellFeature::EncounterZone,
        recallMining.droneX + 2.0,
        recallMining.droneY);
    recallThreat.speed = 0.0;
    recallThreat.damagePerSecond = 0.0;
    recallMining.enemies = {recallThreat};
    auto attack = std::find_if(
        recallMining.miniDrones.begin(),
        recallMining.miniDrones.end(),
        [](const MiningMiniDroneAgent& agent) {
            return agent.role == MiniDroneRole::Attack;
        });
    auto hazard = std::find_if(
        recallMining.miniDrones.begin(),
        recallMining.miniDrones.end(),
        [](const MiningMiniDroneAgent& agent) {
            return agent.role == MiniDroneRole::Hazard;
        });
    require(
        attack != recallMining.miniDrones.end() &&
            hazard != recallMining.miniDrones.end(),
        "hard-recall fixture should create Attack and Hazard agents");
    const double remoteX =
        recallMining.droneX +
        tuning::mining::miningDroneLeashRadiusCells + 2.0;
    for (MiningMiniDroneAgent* agent : {&*attack, &*hazard}) {
        agent->x = remoteX;
        agent->y = recallMining.droneY;
        agent->velocityX = 0.0;
        agent->velocityY = 0.0;
        agent->actionCooldownSeconds = 0.0;
    }
    attack->targetEnemyIndex = 0;
    attack->behavior = MiningMiniDroneBehavior::Engaging;
    hazard->targetCellX = hazardX;
    hazard->targetCellY = hazardY;
    hazard->behavior = MiningMiniDroneBehavior::Working;
    updateMiningRun(hardRecall, catalog, 0.01);
    require(
        attack->targetEnemyIndex < 0 &&
            hazard->targetCellX < 0 &&
            hazard->targetCellY < 0 &&
            attack->behavior == MiningMiniDroneBehavior::Returning &&
            hazard->behavior == MiningMiniDroneBehavior::Returning,
        "hard-leash recall should clear Attack and Hazard tasks without same-tick reacquisition");

    GameState miningPickup = startWithDrones(
        0xE7A10A,
        {content::drone::miningDrone},
        false);
    MiningRunState& pickupMining = miningPickup.run.mining;
    MiningMiniDroneAgent& miningAgent =
        pickupMining.miniDrones.front();
    const MiniDroneCoordinationPoint miningHome =
        miniDroneOrbitPoint(pickupMining, miningAgent);
    miningAgent.x = miningHome.x;
    miningAgent.y = miningHome.y;
    miningAgent.velocityX = 0.0;
    miningAgent.velocityY = 0.0;
    miningAgent.behavior = MiningMiniDroneBehavior::Following;
    miningAgent.actionCooldownSeconds = 0.0;
    pickupMining.looseChunks.push_back({
        MiningCellMaterial::RareOre,
        miningHome.x,
        miningHome.y,
        0.0,
        0.0,
        tuning::mining::rareCargo,
        true
    });
    updateMiningRun(miningPickup, catalog, 0.01);
    require(
        miningAgent.haulMaterials.rare == 1 &&
            pickupMining.looseChunks.empty() &&
            pickupMining.temporaryMaterials.rare == 0,
        "a Mining drone should spatially pick up a nearby loose chunk into its own haul");

    GameState explicitAnchors = startWithDrones(
        0xE7A10B,
        {content::drone::resourceDrone, content::drone::resourceDrone},
        false);
    MiningRunState& anchorMining = explicitAnchors.run.mining;
    anchorMining.droneX = 12.0;
    anchorMining.droneY = 16.0;
    anchorMining.rigDepthZone = anchorMining.depthZone;
    require(
        toggleMiningOperator(explicitAnchors),
        "explicit-anchor fixture should enter EVA");
    anchorMining.operatorX = 36.0;
    anchorMining.operatorY = 20.0;
    anchorMining.operatorVelocityX = 0.0;
    anchorMining.operatorVelocityY = 0.0;
    MiningMiniDroneAgent& rigBound = anchorMining.miniDrones[0];
    MiningMiniDroneAgent& operatorBound = anchorMining.miniDrones[1];
    rigBound.anchorTarget = MiningAnchorTarget::Rig;
    operatorBound.anchorTarget = MiningAnchorTarget::Operator;
    rigBound.x = operatorBound.x = 24.0;
    rigBound.y = operatorBound.y = 18.0;
    rigBound.velocityX = operatorBound.velocityX = 0.0;
    rigBound.velocityY = operatorBound.velocityY = 0.0;
    rigBound.behavior = operatorBound.behavior =
        MiningMiniDroneBehavior::Returning;
    for (int step = 0; step < 180; ++step) {
        updateMiningRun(explicitAnchors, catalog, 0.05);
    }
    const MiniDroneAnchorFrame rigFrame =
        resolveMiniDroneAnchor(anchorMining, MiningAnchorTarget::Rig);
    const MiniDroneAnchorFrame operatorFrame =
        resolveMiniDroneAnchor(anchorMining, MiningAnchorTarget::Operator);
    require(
        rigFrame.valid &&
            rigFrame.actor == MiningActorIdentity::Rig &&
            operatorFrame.valid &&
            operatorFrame.actor == MiningActorIdentity::Operator,
        "explicit Rig and Operator anchors should remain independently valid during EVA");
    const MiniDroneCoordinationPoint rigOrbit =
        miniDroneOrbitPoint(anchorMining, rigBound);
    const MiniDroneCoordinationPoint operatorOrbit =
        miniDroneOrbitPoint(anchorMining, operatorBound);
    require(
        std::hypot(
            rigBound.x - rigOrbit.x,
            rigBound.y - rigOrbit.y) < 0.75 &&
            std::hypot(
                operatorBound.x - operatorOrbit.x,
                operatorBound.y - operatorOrbit.y) < 0.75 &&
            std::hypot(
                rigBound.x - operatorFrame.x,
                rigBound.y - operatorFrame.y) > 10.0 &&
            std::hypot(
                operatorBound.x - rigFrame.x,
                operatorBound.y - rigFrame.y) > 10.0,
        "runtime drone motion should follow each explicit anchor instead of the currently controlled actor");
}


void roughSurfaceExtractionReportsLostPayload()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 8181);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    state.run.surfaceExpedition.supply = 0;
    state.run.surfaceExpedition.cargo = 30;
    state.run.surfaceExpedition.hazard = 1.0;
    state.run.surfaceExpedition.temporaryMaterials = {.common = 5, .rare = 3, .exotic = 1};
    state.run.surfaceExpedition.temporaryArtifacts.push_back({"mars_artifact_loss", content::destination::mars, false});

    const SurfaceActionOutcome outcome = extractSurfacePayload(state);
    require(outcome.applied && outcome.cargoRecovered, "normal return should always resolve as recovered");
    require(outcome.materialDelta.common == 5 && outcome.materialDelta.rare == 3 && outcome.materialDelta.exotic == 1,
        "normal return should retain every Ship material");
    require(outcome.materialLost.common == 0 && outcome.artifactsLost == 0,
        "normal return should not lose cargo or artifacts");
    require(state.meta.artifacts.size() == 1, "normal return should retain artifacts");
}

void roughMiningOreCreditsTheSurvivingContractPayload()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 8182);
    state.run.destinationIndex = 1;
    state.meta.furthestTier = 1;
    require(acknowledgeCampaignObjectiveBriefing(state, CampaignObjectiveId::LunarProspector),
        "the lunar contract must be active before testing a rough delivery");
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied,
        "the lunar contract should start a normal Mining Rig loop");
    state.run.mining.stowedMaterials.common = 9;
    state.run.mining.droneX = state.run.mining.returnZoneX;
    state.run.mining.droneY = state.run.mining.returnZoneY;
    require(finishMiningRun(state, catalog, false).applied,
        "returned mining ore should transfer through the normal surface-extraction handoff");
    require(state.run.surfaceExpedition.bankedMiningArenaValid &&
            state.run.surfaceExpedition.bankedMiningProgressionEligible &&
            state.run.surfaceExpedition.bankedMiningMaterials.common == 9,
        "the normal return handoff should retain mining-payload provenance for contract credit");
    state.run.surfaceExpedition.supply = 0;
    state.run.surfaceExpedition.cargo = 30;
    state.run.surfaceExpedition.hazard = 1.0;

    const SurfaceActionOutcome outcome = extractSurfacePayload(state, catalog);
    require(outcome.materialReturned.common == 9,
        "deterministic return should report the full Ship manifest");
    require(state.meta.prospectorCommonOreRecovered == 9,
        "the Lunar contract must reserve all delivered Mining Rig ore");
    require(state.meta.materials.common == 0,
        "contract ore should stay reserved rather than entering general materials");
}




void saveRoundTripPreservesProgress()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 55);
    state.run.credits = 222.0;
    state.run.destinationIndex = 2;
    state.run.frontierReadiness = 3;
    state.run.refitEntitled = true;
    state.meta.launchLessons.stage = LaunchTrainingStage::FlightControlsCalibration;
    state.run.offerModuleIds = {content::module::fuelTanks1, "", ""};
    state.run.shipDamage = 17;
    state.run.offerRerollsThisExpedition = 2;
    state.run.repairOpsThisExpedition = 1;
    state.run.trainingOpsThisExpedition = 2;
    state.run.restOpsThisExpedition = 3;
    state.meta.unlockKeys.push_back(content::unlock::thermal);
    state.meta.blueprintProgress = 5;
    state.meta.materials = {.common = 3, .rare = 2, .exotic = 1};
    state.meta.prospectorCommonOreRecovered = 2;
    state.meta.ownedModuleIds.push_back(content::module::cryoLoop);
    state.meta.defaultEquippedModuleIds.push_back(content::module::cryoLoop);
    state.meta.artifacts.push_back({"mars_signal_1", content::destination::mars, true});
    state.meta.shipsLost = 1;
    state.meta.closestSurvivalMargin = 0.04;
    state.meta.closestSurvivalBurn = 2.78;
    state.meta.closestSurvivalFailurePoint = 2.82;
    state.meta.maxBurnDepth = 3.48;
    state.meta.maxPeakWarning = 1.0;
    state.meta.maxPeakAbortRisk = 0.94;
    state.meta.bestCreditDelta = 524.0;
    state.meta.worstCreditDelta = -30.0;
    state.meta.destinationAttempts = {2, 1, 0};
    state.meta.destinationSuccesses = {1, 0, 0};
    state.meta.acknowledgedActivityBriefingIds = {
        std::string(ui::briefings::launch),
        std::string(ui::briefings::flyby),
        std::string(ui::briefings::landing),
        std::string(ui::briefings::mining)
    };
    state.meta.memorials.push_back("Test Pilot lost during Mars");
    state.run.crewUpgradeIds = {
        content::crewUpgrade::analogSimBay,
        content::crewUpgrade::medicalRecoveryWard
    };
    state.run.crew.front().training = 7;
    state.run.crew.front().stress = 42;
    state.run.crew.front().status = CrewStatus::Injured;

    const std::string text = serializeSaveData(captureSaveData(state));
    const auto save = deserializeSaveData(text);
    require(save.has_value(), "serialized save should parse");

    GameState restored = createNewGame(catalog, 1);
    restoreSaveData(restored, catalog, *save);

    require(std::abs(restored.run.credits - 222.0) < 0.001, "credits should round trip");
    require(restored.run.destinationIndex == 2, "destination index should round trip");
    require(restored.run.frontierReadiness == 3, "frontier readiness should round trip");
    require(restored.run.refitEntitled, "saved refit entitlement should round trip");
    require(restored.run.offerModuleIds[0] == content::module::fuelTanks1 &&
            restored.run.offerModuleIds[1].empty() && restored.run.offerModuleIds[2].empty(),
        "saved one-card launch lesson offer should round trip");
    require(restored.run.shipDamage == 17, "ship damage should round trip");
    require(restored.run.offerRerollsThisExpedition == 2, "refit reroll count should round trip");
    require(restored.run.repairOpsThisExpedition == 1, "repair escalation should round trip");
    require(restored.run.trainingOpsThisExpedition == 2, "training escalation should round trip");
    require(restored.run.restOpsThisExpedition == 3, "rest escalation should round trip");
    require(hasUnlock(restored.meta, content::unlock::thermal), "unlock keys should round trip");
    require(restored.meta.materials.common == 3 && restored.meta.materials.rare == 2 && restored.meta.materials.exotic == 1, "materials should round trip");
    require(restored.meta.prospectorCommonOreRecovered == 2, "Prospector contract progress should round trip");
    require(std::find(restored.meta.ownedModuleIds.begin(), restored.meta.ownedModuleIds.end(), content::module::cryoLoop) != restored.meta.ownedModuleIds.end(), "permanent shipyard modules should round trip");
    require(std::find(restored.meta.defaultEquippedModuleIds.begin(), restored.meta.defaultEquippedModuleIds.end(), content::module::cryoLoop) != restored.meta.defaultEquippedModuleIds.end(), "default shipyard loadout should round trip");
    require(restored.meta.artifacts.size() == 1 && restored.meta.artifacts[0].identified, "artifacts should round trip");
    require(std::abs(restored.meta.closestSurvivalMargin - 0.04) < 0.001, "closest survival margin should round trip");
    require(std::abs(restored.meta.closestSurvivalBurn - 2.78) < 0.001, "closest survival burn should round trip");
    require(std::abs(restored.meta.closestSurvivalFailurePoint - 2.82) < 0.001, "closest survival failure point should round trip");
    require(std::abs(restored.meta.maxBurnDepth - 3.48) < 0.001, "max burn depth should round trip");
    require(std::abs(restored.meta.maxPeakWarning - 1.0) < 0.001, "max peak warning should round trip");
    require(std::abs(restored.meta.maxPeakAbortRisk - 0.94) < 0.001, "max peak abort should round trip");
    require(std::abs(restored.meta.bestCreditDelta - 524.0) < 0.001, "best credit delta should round trip");
    require(std::abs(restored.meta.worstCreditDelta + 30.0) < 0.001, "worst credit delta should round trip");
    require(restored.meta.destinationAttempts.size() >= 3 && restored.meta.destinationAttempts[0] == 2, "destination attempts should round trip");
    require(restored.meta.destinationSuccesses.size() >= 3 && restored.meta.destinationSuccesses[0] == 1, "destination successes should round trip");
    require(ui::briefings::acknowledged(restored.meta.acknowledgedActivityBriefingIds, ui::briefings::launch)
            && ui::briefings::acknowledged(restored.meta.acknowledgedActivityBriefingIds, ui::briefings::flyby)
            && ui::briefings::acknowledged(restored.meta.acknowledgedActivityBriefingIds, ui::briefings::landing)
            && ui::briefings::acknowledged(restored.meta.acknowledgedActivityBriefingIds, ui::briefings::mining),
        "activity introduction acknowledgments should round trip");
    require(restored.meta.memorials.size() == 1, "memorials should round trip");
    require(restored.run.crewUpgradeIds.size() == 2 && restored.run.crewUpgradeIds[0] == content::crewUpgrade::analogSimBay, "crew upgrades should round trip");
    require(restored.run.crew.front().training == 7, "crew training should round trip");
    require(restored.run.crew.front().stress == 42, "crew stress should round trip");
    require(restored.run.crew.front().status == CrewStatus::Injured, "crew status should round trip");
}

void progressedSavesSkipTheFirstLaunchIntroduction()
{
    const ContentCatalog catalog = createDefaultContent();
    const GameState freshState = createNewGame(catalog, 0xB12EF);
    SaveData freshSave = captureSaveData(freshState);

    GameState freshRestored = createNewGame(catalog, 1);
    restoreSaveData(freshRestored, catalog, freshSave);
    require(!ui::briefings::acknowledged(freshRestored.meta.acknowledgedActivityBriefingIds, ui::briefings::launch),
        "a campaign with no launch history should retain the first-flight introduction");

    freshSave.destinationAttempts = {1};
    GameState progressedRestored = createNewGame(catalog, 2);
    restoreSaveData(progressedRestored, catalog, freshSave);
    require(ui::briefings::acknowledged(progressedRestored.meta.acknowledgedActivityBriefingIds, ui::briefings::launch),
        "a campaign with recorded launch history should migrate past the first-flight introduction");
}


void saveSchemaConstantsMatchSerializedFields()
{
    const ContentCatalog catalog = createDefaultContent();
    require(save_schema::currentVersion == 14, "the current save schema should be version fourteen");
    GameState state = createNewGame(catalog, 12);
    state.run.credits = 123.0;
    state.run.inventoryModuleIds = {content::module::sparrowEngine, content::module::cryoLoop};
    state.meta.memorials = {"Ada burned late", "Ben returned home"};

    const SaveData captured = captureSaveData(state);
    const std::string text = serializeSaveData(captured);
    require(text.find(std::string(save_schema::header) + "\n") == 0, "save should start with shared schema header");
    require(text.find(std::string(save_schema::field::credits) + save_schema::keyValueDelimiter) != std::string::npos, "credits key should use shared schema name");
    require(text.find(std::string(save_schema::field::inventory) + save_schema::keyValueDelimiter) != std::string::npos, "inventory key should use shared schema name");
    require(text.find(std::string(save_schema::field::ownedModules) + save_schema::keyValueDelimiter) != std::string::npos, "owned modules key should use shared schema name");
    require(text.find(std::string(save_schema::field::defaultEquippedModules) + save_schema::keyValueDelimiter) != std::string::npos, "default equipped modules key should use shared schema name");
    require(text.find(std::string(save_schema::field::refitEntitled) + save_schema::keyValueDelimiter) != std::string::npos, "refit entitlement key should use shared schema name");
    require(text.find(std::string(save_schema::field::acknowledgedActivityBriefings) + save_schema::keyValueDelimiter) != std::string::npos, "activity briefing acknowledgments should use a shared schema name");
    require(text.find(std::string(save_schema::field::offerModules) + save_schema::keyValueDelimiter) != std::string::npos, "refit offers key should use shared schema name");
    require(text.find(std::string(save_schema::field::screen) + save_schema::keyValueDelimiter) != std::string::npos, "screen key should use shared schema name");
    require(text.find(std::string(save_schema::field::pendingTransferAssistExitCourseOffset) + save_schema::keyValueDelimiter) != std::string::npos,
        "transfer-assist exit course offset should use a shared schema name");
    require(text.find(std::string(save_schema::field::chapter) + save_schema::keyValueDelimiter) != std::string::npos, "chapter key should use shared schema name");
    require(text.find(std::string(save_schema::field::materials) + save_schema::keyValueDelimiter) != std::string::npos, "materials key should use shared schema name");
    require(text.find(std::string(save_schema::field::surfaceSite) + save_schema::keyValueDelimiter) != std::string::npos, "surface site key should use shared schema name");
    require(text.find(std::string(save_schema::field::surfaceLog) + save_schema::keyValueDelimiter) != std::string::npos, "surface log key should use shared schema name");
    require(text.find(std::string(save_schema::field::expeditionLevel) + save_schema::keyValueDelimiter) != std::string::npos, "expedition level should use a shared schema name");
    require(text.find(std::string(save_schema::field::expeditionExperience) + save_schema::keyValueDelimiter) != std::string::npos, "expedition experience should use a shared schema name");
    require(text.find(std::string(save_schema::field::pendingRunUpgradeChoices) + save_schema::keyValueDelimiter) != std::string::npos, "pending run choices should use a shared schema name");
    require(text.find(std::string(save_schema::field::runUpgradeOffers) + save_schema::keyValueDelimiter) != std::string::npos, "run upgrade offers should use a shared schema name");
    require(text.find(std::string(save_schema::field::runUpgradeOfferCount) + save_schema::keyValueDelimiter) != std::string::npos, "run offer count should use a shared schema name");
    require(text.find(std::string(save_schema::field::runUpgradeOfferPending) + save_schema::keyValueDelimiter) != std::string::npos, "run offer pending state should use a shared schema name");
    require(text.find(std::string(save_schema::field::runUpgradeReturnScreen) + save_schema::keyValueDelimiter) != std::string::npos, "run offer return screen should use a shared schema name");
    require(text.find(std::string(save_schema::field::runRigUpgradeRanks) + save_schema::keyValueDelimiter) != std::string::npos, "run rig ranks should use a shared schema name");
    require(text.find(std::string(save_schema::field::runDroneRanks) + save_schema::keyValueDelimiter) != std::string::npos, "run drone ranks should use a shared schema name");
    require(text.find(std::string(save_schema::field::selectedSynergyIds) + save_schema::keyValueDelimiter) != std::string::npos, "selected synergies should use a shared schema name");
    require(text.find(std::string(save_schema::field::droneModuleAssignments) + save_schema::keyValueDelimiter) != std::string::npos, "temporary drone grafts should use a shared schema name");
    require(text.find(std::string(save_schema::field::miningRigState) + save_schema::keyValueDelimiter) != std::string::npos, "mining rig state key should use shared schema name");
    require(text.find(std::string(save_schema::field::miningOperatorState) + save_schema::keyValueDelimiter) != std::string::npos, "mining operator state key should use shared schema name");
    require(text.find(std::string(save_schema::field::miningGravity) + save_schema::keyValueDelimiter) != std::string::npos, "mining gravity key should use shared schema name");
    require(text.find(std::string(save_schema::field::miningLooseChunks) + save_schema::keyValueDelimiter) != std::string::npos, "mining loose-chunk key should use shared schema name");
    require(text.find(std::string(save_schema::field::droneBaySlots) + save_schema::keyValueDelimiter) != std::string::npos, "drone bay slots key should use shared schema name");
    require(text.find(std::string(save_schema::field::ownedDrones) + save_schema::keyValueDelimiter) != std::string::npos, "owned drones key should use shared schema name");
    require(text.find(std::string(save_schema::field::equippedDrones) + save_schema::keyValueDelimiter) != std::string::npos, "equipped drones key should use shared schema name");
    require(text.find(std::string(save_schema::field::prospectorCommonOreRecovered) + save_schema::keyValueDelimiter) != std::string::npos, "Prospector contract progress should use a shared schema name");
    require(text.find(std::string(save_schema::field::marsCommonOreRecovered) + save_schema::keyValueDelimiter) != std::string::npos, "Mars contract progress should use a shared schema name");
    require(text.find(std::string(save_schema::field::ioArtifactRecovered) + save_schema::keyValueDelimiter) != std::string::npos, "Io artifact state should use a shared schema name");
    require(text.find(std::string(save_schema::field::saturnRouteUnlocked) + save_schema::keyValueDelimiter) != std::string::npos, "Saturn route state should use a shared schema name");
    require(text.find("surfaceUpgrades=") == std::string::npos &&
            text.find("surfaceUpgradeOffers=") == std::string::npos &&
            text.find("surfaceUpgradeOfferAvailable=") == std::string::npos &&
            text.find("surfaceUpgradeOffersSeen=") == std::string::npos &&
            text.find("surfaceModuleOffers=") == std::string::npos &&
            text.find("pendingDroneModuleId=") == std::string::npos &&
            text.find("pendingDroneModuleOfferIndex=") == std::string::npos &&
            text.find("pendingDroneModuleFrame=") == std::string::npos &&
            text.find("pendingDroneModuleReplacementConfirmation=") == std::string::npos,
        "version-fourteen saves must not persist the retired surface draft subflows");
    require(text.find("fieldInsight=") == std::string::npos &&
            text.find("fieldInsightAwardKeys=") == std::string::npos &&
            text.find("miningDraftsEarned=") == std::string::npos &&
            text.find("pendingFieldDraftThreshold=") == std::string::npos &&
            text.find("fieldDraftReturnScreen=") == std::string::npos,
        "version-fourteen saves must not persist retired Field Insight progression");
    require(text.find("droneUpgrades=") == std::string::npos &&
            text.find("droneUpgradeCredits=") == std::string::npos,
        "version-fourteen saves must not persist retired permanent drone progression");
    require(text.find("miningFuelBurn=") == std::string::npos,
        "version-fourteen saves must not persist the retired seconds-based fuel field");
    require(text.find(std::string(1, save_schema::textListDelimiter)) != std::string::npos, "text list delimiter should be shared");

    const std::string minimalSave = std::string(save_schema::header) + "\n" +
        std::string(save_schema::field::version) + save_schema::keyValueDelimiter +
            std::to_string(save_schema::currentVersion) + "\n" +
        std::string(save_schema::field::credits) + save_schema::keyValueDelimiter + "321\n";
    const auto parsed = deserializeSaveData(minimalSave);
    require(parsed.has_value(), "minimal save with shared header should parse");
    require(std::abs(parsed->credits - 321.0) < 0.001, "shared credits key should parse");
    require(!deserializeSaveData("RR_SAVE_V0\ncredits=1\n").has_value(), "unknown save header should not parse");
    require(!deserializeSaveData(std::string(save_schema::header) + "\ncredits=1\n").has_value(),
        "save payloads without an explicit schema version must not parse");
    const std::string duplicateVersionSave = std::string(save_schema::header) +
        "\nversion=" + std::to_string(save_schema::currentVersion) +
        "\nversion=" + std::to_string(save_schema::currentVersion) + "\ncredits=1\n";
    require(!deserializeSaveData(duplicateVersionSave).has_value(),
        "save payloads with duplicate version declarations must not parse");
    require(!deserializeSaveData(
                std::string(save_schema::header) + "\nversion=invalid\ncredits=1\n")
                .has_value(),
        "save payloads with malformed version declarations must not parse");
    require(!deserializeSaveData(
                std::string(save_schema::header) + "\nversion=14trailing-data\ncredits=1\n")
                .has_value(),
        "save payloads must declare the exact current version value");

    SaveData incompatible = captureSaveData(state);
    incompatible.version = save_schema::currentVersion - 1;
    require(!deserializeSaveData(serializeSaveData(incompatible)).has_value(),
        "version-thirteen saves must be rejected instead of partially migrated");
    incompatible.version = save_schema::currentVersion + 1;
    require(!deserializeSaveData(serializeSaveData(incompatible)).has_value(),
        "future save versions must also be rejected instead of partially restored");

    GameState unchanged = createNewGame(catalog, 13);
    unchanged.run.credits = 77.0;
    incompatible.version = save_schema::currentVersion - 1;
    restoreSaveData(unchanged, catalog, incompatible);
    require(std::abs(unchanged.run.credits - 77.0) < 0.001,
        "restore must reject an incompatible schema before mutating game state");
}

void legacyRecordsTrackAchievementStats()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 909);
    state.meta.launchLessons.stage = LaunchTrainingStage::Complete;
    syncLaunchConfig(state, catalog);

    LaunchOutcome first;
    first.type = LaunchResultType::MissionComplete;
    first.recoveryMethod = RecoveryMethod::ReturnHome;
    first.destinationId = content::destination::earthOrbit;
    first.ejectMultiplier = 2.78;
    first.crashMultiplier = 2.82;
    first.payout = 600.0;
    first.recoveryCost = 76.0;
    first.peakWarning = 1.0;
    first.peakAbortRisk = 0.99;
    applyLaunchOutcome(state, catalog, first);

    require(std::abs(state.meta.closestSurvivalMargin - 0.04) < 0.001, "closest survival margin should track close successful recoveries");
    require(std::abs(state.meta.closestSurvivalBurn - 2.78) < 0.001, "closest survival burn should track the recovered burn depth");
    require(std::abs(state.meta.closestSurvivalFailurePoint - 2.82) < 0.001, "closest survival failure point should track the hidden failure");
    require(std::abs(state.meta.maxBurnDepth - 2.78) < 0.001, "max burn should track launch depth");
    require(std::abs(state.meta.maxPeakWarning - 1.0) < 0.001, "max warning should track peak telemetry");
    require(std::abs(state.meta.maxPeakAbortRisk - 0.99) < 0.001, "max abort should track peak abort");
    require(std::abs(state.meta.bestCreditDelta - 584.0) < 0.001, "best credit delta should include close-call bonus rewards");
    require(std::abs(state.lastOutcome.payout - 660.0) < 0.001, "skin-of-your-teeth outcomes should add a ten percent mission credit bonus");

    LaunchOutcome later = first;
    later.ejectMultiplier = 3.20;
    later.crashMultiplier = 3.90;
    later.payout = 0.0;
    later.recoveryCost = 15.0;
    later.peakWarning = 0.50;
    later.peakAbortRisk = 0.45;
    applyLaunchOutcome(state, catalog, later);

    require(std::abs(state.meta.closestSurvivalMargin - 0.04) < 0.001, "wider recoveries should not replace the closest survival");
    require(std::abs(state.meta.maxBurnDepth - 3.20) < 0.001, "max burn should continue to update independently");
    require(std::abs(state.meta.worstCreditDelta + 15.0) < 0.001, "worst credit delta should track expensive recoveries");
}








void flightProgressHelpersShareTravelAndReturnMath()
{
    const ContentCatalog catalog = createDefaultContent();
    const Destination& earthOrbit = catalog.destinations[0];

    const double midpointBurn = 1.0 + (earthOrbit.targetMultiplier - 1.0) * 0.50;
    require(std::abs(flight_progress::travelProgressForBurn(midpointBurn, earthOrbit) - 0.50) < 0.000001, "travel progress helper should map burn depth to destination progress");
    require(flight_progress::travelProgressForBurn(0.80, earthOrbit) == 0.0, "travel progress helper should clamp low burn depth");
    require(flight_progress::travelProgressForBurn(earthOrbit.targetMultiplier + 5.0, earthOrbit) == tuning::session::maxTravelProgress, "travel progress helper should clamp high burn depth");

    const double returnDuration = 2.4;
    require(std::abs(flight_progress::returnCompletion(1.2, returnDuration) - math::smoothStep(0.5)) < 0.000001, "return completion should use shared smooth step");
    require(std::abs(flight_progress::returnTravelProgress(0.80, 1.2, returnDuration) - 0.40) < 0.000001, "return travel helper should move the visual ship back home");

    const double startTravel = 0.35;
    const double baseDuration = tuning::session::returnBaseDuration + startTravel * tuning::session::returnDurationPerProgress;
    require(std::abs(flight_progress::returnDuration(startTravel, false) - baseDuration) < 0.000001, "return duration helper should use tuned base duration");
    require(std::abs(flight_progress::returnDuration(startTravel, true) - baseDuration * tuning::session::returnDriftDurationMultiplier) < 0.000001, "return duration helper should apply drift multiplier");
}






void arkDiscoveryAndScriptedJumpProgression()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 62001);

    LaunchOutcome neptuneArrival;
    neptuneArrival.type = LaunchResultType::MissionComplete;
    neptuneArrival.frontierTransfer = true;
    neptuneArrival.destinationId = content::destination::neptune;
    neptuneArrival.ejectMultiplier = 4.20;
    neptuneArrival.crashMultiplier = 6.35;
    applyLaunchOutcome(state, catalog, neptuneArrival);

    require(!arkDiscovered(state), "Neptune arrival must not discover the Ark before the story takeover is acknowledged");
    require(state.storyBriefing.pending == StoryBriefingId::StraylightDiscovery, "successful Neptune arrival should persist the Straylight takeover");
    require(acknowledgeStoryBriefing(state, catalog), "the Straylight takeover should acknowledge once");
    require(arkDiscovered(state), "acknowledging the Neptune discovery should reveal the operable derelict Ark");
    require(state.meta.campaignMilestone == CampaignMilestone::ArkDiscovered, "Ark discovery should advance the campaign milestone");
    require(state.meta.chapter == GameChapter::Breakthrough, "Ark discovery should enter Breakthrough chapter");
    require(state.meta.ark.condition == ArkCondition::DerelictOperable, "discovered Ark should be derelict but operable");
    require(!navigationAvailable(state), "navigation should not become the main loop before the gravity-well disaster");

    require(performArkJump(state, catalog), "first Ark jump should resolve");
    require(state.meta.ark.firstJumpComplete, "first Ark jump should be recorded");
    require(state.meta.campaignMilestone == CampaignMilestone::FirstArkJumpComplete, "first Ark jump should have its own milestone");
    require(state.meta.chapter == GameChapter::Straylight, "first Ark jump should enter Straylight");
    require(state.meta.ark.condition == ArkCondition::DerelictOperable, "first Ark jump should not strand the Ark");

    require(performArkJump(state, catalog), "second Ark jump should resolve into the scripted disaster");
    require(hostileSystemActive(state), "second Ark jump should activate the hostile system loop");
    require(state.meta.chapter == GameChapter::Arkfall, "gravity-well disaster should enter Arkfall");
    require(navigationAvailable(state), "navigation should become available after the disaster");
    require(state.meta.ark.gravityWellDisaster, "gravity-well disaster should be recorded");
    require(state.meta.ark.condition == ArkCondition::DamagedStranded, "Ark should be damaged and stranded after the scripted disaster");
    require(hasUnlock(state.meta, content::unlock::deepSpace), "hostile system should unlock deep-space destinations");
    require(hasUnlock(state.meta, content::unlock::droneBay), "Arkfall should provision the Drone Bay even when its research was skipped");
    require(hasUnlock(state.meta, content::unlock::perimeterDrones), "hostile system should unlock combat-drone tech timing");
    require(!hasUnlock(state.meta, content::unlock::perimeterCoordination), "Arkfall should not skip the advanced combat coordination research step");
    require(state.meta.droneBaySlots >= 3, "Arkfall should raise an undersized Drone Bay to three slots");
    require(std::find(state.meta.ownedDroneIds.begin(), state.meta.ownedDroneIds.end(), content::drone::attackDrone) != state.meta.ownedDroneIds.end(),
        "Arkfall should grant an Attack drone");
    require(std::find(state.meta.ownedDroneIds.begin(), state.meta.ownedDroneIds.end(), content::drone::defenseDrone) != state.meta.ownedDroneIds.end(),
        "Arkfall should grant a Defense drone");
    require(expeditionDroneRank(state, content::drone::attackDrone) == 1 &&
            expeditionDroneRank(state, content::drone::defenseDrone) == 1 &&
            state.run.surfaceExpedition.runDroneRanks.empty(),
        "Arkfall combat drones should enter service at baseline Mk I without free run upgrades");
    require(state.screen == Screen::Navigation, "gravity-well disaster should land the player on Navigation");

    GameState upgraded = createNewGame(catalog, 62002);
    upgraded.meta.ark.condition = ArkCondition::DerelictOperable;
    upgraded.meta.ark.firstJumpComplete = true;
    upgraded.meta.campaignMilestone = CampaignMilestone::FirstArkJumpComplete;
    upgraded.meta.droneBaySlots = 5;
    upgraded.meta.ownedDroneIds = {content::drone::attackDrone};
    require(performArkJump(upgraded, catalog), "pre-upgraded Ark should still resolve the scripted disaster");
    require(upgraded.meta.droneBaySlots == 5, "Arkfall should never shrink an already expanded Drone Bay");
    require(upgraded.run.surfaceExpedition.runDroneRanks.empty(),
        "Arkfall should grant ownership without silently granting temporary Drone ranks");
}

void numberedChaptersAdvanceMonotonically()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 62005);
    require(state.meta.chapter == GameChapter::ProvingGround, "new game should start at Chapter 1");
    state.meta.launchLessons.stage = LaunchTrainingStage::Complete;

    auto completeTransfer = [&](std::string_view destinationId, double multiplier) {
        LaunchOutcome outcome;
        outcome.type = LaunchResultType::MissionComplete;
        outcome.recoveryMethod = RecoveryMethod::TransferArrival;
        outcome.frontierTransfer = true;
        outcome.destinationId = std::string(destinationId);
        outcome.ejectMultiplier = multiplier;
        outcome.crashMultiplier = multiplier + 0.65;
        applyLaunchOutcome(state, catalog, outcome);
    };

    completeTransfer(content::destination::moon, 1.95);
    require(state.meta.chapter == GameChapter::LunarProgram, "Moon advancement should enter Chapter 2");

    completeTransfer(content::destination::mars, 2.65);
    require(state.meta.chapter == GameChapter::RedFrontier, "Mars advancement should enter Chapter 3");

    completeTransfer(content::destination::jupiter, 3.15);
    completeTransfer(content::destination::saturn, 3.45);
    completeTransfer(content::destination::uranus, 3.80);
    completeTransfer(content::destination::neptune, 4.20);
    require(state.meta.chapter == GameChapter::Breakthrough, "outer-planet progression should remain in Chapter 4 through Neptune");
    require(!arkDiscovered(state), "Neptune completion should wait for the discovery acknowledgment");
    require(acknowledgeStoryBriefing(state, catalog), "Neptune discovery should acknowledge before the first Ark jump");
    require(arkDiscovered(state), "Chapter 4 should discover the Ark only after the Neptune takeover");

    require(performArkJump(state, catalog), "first Ark jump should enter Straylight");
    require(state.meta.chapter == GameChapter::Straylight, "first Ark jump should enter Chapter 5");
    require(state.meta.navigation.currentSystemId == "relay_system", "Straylight should use the peaceful relay system");
    require(!hostileSystemActive(state), "Straylight should remain non-hostile");

    require(performArkJump(state, catalog), "second Ark jump should trigger Arkfall");
    require(state.meta.chapter == GameChapter::Arkfall, "gravity-well disaster should enter Chapter 6");
    require(hostileSystemActive(state), "Arkfall should activate the hostile-system loop");

    GameState legacyDisaster = createNewGame(catalog, 62007);
    legacyDisaster.meta.campaignMilestone = CampaignMilestone::GravityWellDisaster;
    syncChapterProgress(legacyDisaster, catalog);
    require(legacyDisaster.meta.chapter == GameChapter::Arkfall, "legacy gravity-well milestone should derive Arkfall");
    require(navigationAvailable(legacyDisaster), "legacy gravity-well milestone should make Navigation available");

    completeTransfer(content::destination::nearbyStar, 5.10);
    require(state.meta.chapter == GameChapter::LastCampfire, "first hostile-system sortie success should enter Chapter 7");

    completeTransfer(content::destination::nearbyGalaxy, 7.00);
    require(state.meta.chapter == GameChapter::VoidCompass, "Rift Belt success should enter Chapter 8");

    state.meta.campaignMilestone = CampaignMilestone::ArkRepairing;
    syncChapterProgress(state, catalog);
    require(state.meta.chapter == GameChapter::Ouroboros, "Ark repair milestone should enter Chapter 9");

    state.meta.chapter = GameChapter::Ascent;
    state.run.destinationIndex = 0;
    state.meta.campaignMilestone = CampaignMilestone::SolarTutorial;
    state.meta.ark = {};
    state.meta.navigation = {};
    syncChapterProgress(state, catalog);
    require(state.meta.chapter == GameChapter::Ascent, "chapter sync should never roll a later chapter backward");
}

void hostileNavigationSelectsShuttleSortie()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 62002);
    state.meta.launchLessons.stage = LaunchTrainingStage::Complete;
    discoverArk(state, catalog);
    performArkJump(state, catalog);
    performArkJump(state, catalog);

    const std::vector<const Destination*> destinations = navigationDestinations(state, catalog);
    require(destinations.size() >= 2, "hostile navigation should expose multiple mapped destinations");
    require(destinations.front()->id == content::destination::nearbyStar, "Nearby Star should be the first hostile-system sortie target");

    const int fuelBefore = state.meta.ark.fuelReserve;
    const int expectedFuelCost = 6;
    require(selectNavigationDestination(state, catalog, 0), "selecting a navigation destination should succeed");
    require(state.screen == Screen::Hangar, "selecting a destination should open shuttle prep in the Hangar");
    require(state.launchConfig.destinationId == content::destination::nearbyStar, "navigation should sync launch destination");
    require(state.launchConfig.frontierTransfer, "hostile navigation sorties should use transfer burn tuning");
    require(state.meta.ark.fuelReserve == fuelBefore - expectedFuelCost, "navigation sorties should spend Ark fuel");

    state.screen = Screen::Navigation;
    state.meta.ark.fuelReserve = 0;
    require(!selectNavigationDestination(state, catalog, 0), "navigation should reject destinations the Ark fuel reserve cannot afford");

    startSurfaceExpedition(state, catalog);
    require(state.run.surfaceExpedition.enemyEncountersEnabled, "hostile-system surface expeditions should enable enemy contact");
}

void legacyDeepSpaceFrontierMigratesToArkFlow()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 62004);
    auto destinationIndex = [&](std::string_view id) {
        for (int index = 0; index < static_cast<int>(catalog.destinations.size()); ++index) {
            if (catalog.destinations[static_cast<std::size_t>(index)].id == id) {
                return index;
            }
        }
        return -1;
    };
    const int neptuneIndex = destinationIndex(content::destination::neptune);
    const int legacyStarIndex = destinationIndex(content::destination::nearbyStar);
    require(neptuneIndex >= 0, "Neptune should resolve");
    require(legacyStarIndex > neptuneIndex, "legacy deep-space destination should still exist for save compatibility");

    state.run.destinationIndex = legacyStarIndex;
    state.meta.furthestTier = catalog.destinations[static_cast<std::size_t>(legacyStarIndex)].tier;
    state.launchConfig.frontierTransfer = true;
    state.launchConfig.destinationId = content::destination::nearbyStar;
    state.screen = Screen::Launch;

    require(migrateLegacyDeepSpaceFrontier(state, catalog), "legacy direct star launch should migrate");
    require(arkDiscovered(state), "migration should discover the Ark instead of preserving the retired ladder");
    require(!hostileSystemActive(state), "migration should not skip the scripted Ark jumps");
    require(state.screen == Screen::Hangar, "migration should return stale launch saves to the Hangar");
    require(state.run.destinationIndex == neptuneIndex, "migration should put the solar frontier at Neptune");
    require(nextDestination(state, catalog) == nullptr, "Solar System ladder should stop at Neptune once Ark flow begins");
}

void arkCampaignStateRoundTripsThroughSave()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 62003);
    discoverArk(state, catalog);
    performArkJump(state, catalog);
    performArkJump(state, catalog);
    selectNavigationDestination(state, catalog, 1);

    const SaveData save = captureSaveData(state);
    const std::string serialized = serializeSaveData(save);
    const std::optional<SaveData> parsed = deserializeSaveData(serialized);
    require(parsed.has_value(), "Ark campaign save should deserialize");

    GameState restored = createNewGame(catalog, 1);
    restoreSaveData(restored, catalog, *parsed);
    require(restored.meta.campaignMilestone == CampaignMilestone::HostileSystemStranded, "campaign milestone should round trip");
    require(restored.meta.chapter == GameChapter::Arkfall, "chapter should round trip and stay at Arkfall before hostile sortie success");
    require(restored.meta.ark.condition == ArkCondition::DamagedStranded, "Ark condition should round trip");
    require(restored.meta.ark.gravityWellDisaster, "gravity-well flag should round trip");
    require(restored.meta.navigation.currentSystemId == "hostile_system", "navigation system id should round trip");
    require(restored.meta.navigation.selectedDestinationId == content::destination::nearbyGalaxy, "selected navigation target should round trip");

}

void uiActionsUseStableSchemaIds()
{
    require(ui::actions::prepareLaunch == "prepare_launch", "prepare launch action should use a stable schema id");
    require(ui::actions::startLaunch == "start_launch", "start launch action should use a stable schema id");
    require(ui::actions::returnHome == "return_home", "return action should use a stable schema id");
    require(ui::actions::arrivalOps == "arrival_ops", "arrival ops action should use a stable schema id");
    require(ui::actions::skipArrivalFanfare == "skip_arrival_fanfare", "arrival fanfare skip action should use a stable schema id");
    require(ui::actions::acknowledgeApproachIntroduction == "acknowledge_approach_introduction",
        "approach introduction acknowledgment should use a stable schema id");
    require(ui::actions::openNavigation == "open_navigation", "navigation action should use a stable schema id");
    require(ui::actions::arkJump == "ark_jump", "Ark jump action should use a stable schema id");
    require(ui::actions::selectNavigationDestination(2) == "select_navigation:2", "indexed navigation actions should share one action family");
    require(ui::actions::researchProject(2) == "research_project:2", "indexed research actions should share one action family");
    require(ui::actions::surfaceUpgrade(2) == "surface_upgrade:2", "indexed surface upgrade actions should share one action family");
    require(ui::actions::droneOps == "drone_ops", "Drone Ops action should use a stable schema id");
    require(ui::actions::equipDrone(2) == "equip_drone:2", "indexed drone equipment actions should share one action family");
    require(ui::actions::upgradeDroneSlot == "upgrade_drone_slot", "drone slot upgrade action should use a stable schema id");
    require(ui::actions::recruitCandidate(2) == "recruit_candidate:2", "indexed recruit actions should share one action family");
    require(ui::actions::extractSurface == "extract_surface", "surface extraction action should use a stable schema id");
    require(ui::actions::surfaceScanPulse == "surface_scan_pulse", "surface scan pulse action should use a stable schema id");
    require(ui::actions::surfaceScanBank == "surface_scan_bank", "surface scan bank action should use a stable schema id");
    require(ui::actions::surfacePushStep == "surface_push_step", "surface push step action should use a stable schema id");
    require(ui::actions::surfacePushBank == "surface_push_bank", "surface push bank action should use a stable schema id");
    require(ui::actions::miningTether == "mining_tether", "mining tether action should use a stable schema id");
    require(ui::actions::miningRepairDrill == "mining_repair_drill", "mining drill repair action should use a stable schema id");
    require(ui::actions::miningRepairDrone == "mining_repair_drone", "mining drone repair action should use a stable schema id");
    require(ui::actions::resetSave == "reset_save", "settings actions should use stable schema ids");
    require(ui::actions::newGame == "new_game", "New Game should use a stable title action id");
    require(ui::actions::continueGame == "continue_game", "Continue should use a stable title action id");
    require(ui::modals::launchBlocked == "launch_blocked", "modal ids should stay shared and data-like");
    require(ui::modals::map == "map", "solar map modal id should stay shared and data-like");

    const std::string buyOffer = ui::actions::buyOffer(2);
    require(buyOffer == "buy_offer:2", "indexed offer actions should encode the offer index in one reusable action family");
    require(buyOffer.find("rr.") == std::string::npos, "panel action ids should not embed JavaScript snippets");
}

void panelLayoutModeIsPortablePresentationData()
{
    require(panelLayoutMode(Screen::Launch) == PanelLayoutMode::ControlPanel, "launch should remain a compact action control panel");
    require(panelLayoutMode(Screen::ArrivalFanfare) == PanelLayoutMode::ControlPanel, "arrival fanfare should keep the scene open for the celebration overlay");
    require(panelLayoutMode(Screen::Orbit) == PanelLayoutMode::ControlPanel, "orbit should keep the scene open for the orbital minigame");
    require(panelLayoutMode(Screen::Hangar) == PanelLayoutMode::Fullscreen, "hangar should use the full-screen management workspace");
    require(panelLayoutMode(Screen::Results) == PanelLayoutMode::Fullscreen, "results should use the fullscreen scene with content-sized acknowledgement treatment");
    require(!usesPhaseBoard(Screen::Results), "results should not be classified as a persistent phase-board rail");
    require(panelLayoutMode(Screen::Research) == PanelLayoutMode::Fullscreen, "research should use the full-screen management workspace");
    require(panelLayoutMode(Screen::SurfaceExpedition) == PanelLayoutMode::Fullscreen, "surface expedition should use the full-screen decision workspace");
    require(panelLayoutMode(Screen::SurfaceUpgrade) == PanelLayoutMode::Fullscreen, "field upgrade draft should use the full-screen selection workspace");
    require(panelLayoutMode(Screen::SurfaceScan) == PanelLayoutMode::PhaseBoard, "surface scan should use the phase board layout");
    require(panelLayoutMode(Screen::SurfacePush) == PanelLayoutMode::PhaseBoard, "Push Deeper should use the phase board layout");
    require(panelLayoutMode(Screen::DroneOps) == PanelLayoutMode::Fullscreen, "drone ops should use its non-gameplay full-screen workspace");
    require(!usesPhaseBoard(Screen::DroneOps), "drone ops should not reserve the persistent gameplay rail");
    require(panelLayoutMode(Screen::Navigation) == PanelLayoutMode::Fullscreen, "navigation should use the full-screen management workspace");
    require(panelLayoutMode(Screen::Upgrade) == PanelLayoutMode::Fullscreen, "refit should use the full-screen selection workspace");
    require(!usesPhaseBoard(Screen::Research), "non-gameplay management screens should not reserve the gameplay rail");
    require(usesPhaseBoard(Screen::SurfaceScan), "active surface minigames should retain the protected phase-board geometry");
}

void structuredPanelPresentationSelectsFirstWaveTemplates()
{
    const ContentCatalog catalog = createDefaultContent();
    const auto presentationFor = [&catalog](GameState& state, std::uint64_t seed) {
        Random rng(seed);
        const PreparedLaunch launch = prepareLaunch(state, catalog, rng);
        PanelRenderContext context {state, catalog, launch, launch};
        context.firstTimeIntroductionsEnabled = false;
        return buildGamePanelPresentation(context);
    };

    GameState hangar = createNewGame(catalog, 0xA110);
    hangar.screen = Screen::Hangar;
    hangar.meta.launchLessons.stage = LaunchTrainingStage::Complete;
    const PanelDocumentPresentation hangarPresentation = presentationFor(hangar, 0xA110);
    require(
        hangarPresentation.templateKind == PanelTemplateKind::Workspace
            && hangarPresentation.metadata.screen == Screen::Hangar,
        "Hangar should select the shared Workspace template");
    require(
        hangarPresentation.contentMarkup.find("rr-screen-header") != std::string::npos
            && hangarPresentation.contentMarkup.find("rr-card-grid") != std::string::npos
            && hangarPresentation.contentMarkup.find("rr-fixed-lane-card") != std::string::npos
            && hangarPresentation.contentMarkup.find("rr-action-footer") != std::string::npos,
        "Hangar should consume the shared typed header, card-grid, card, and action-footer primitives");
    require(
        hangarPresentation.contentMarkup.find("hangar_details") != std::string::npos
            && hangarPresentation.contentMarkup.find("hangar-detail-actions") == std::string::npos
            && hangarPresentation.contentMarkup.find("hangar-launch-prep") != std::string::npos,
        "Hangar details should live behind the top-bar Details modal and launch prep should own its compact control class");

    GameState flyby = createNewGame(catalog, 0xA111);
    LaunchOutcome moonArrival;
    moonArrival.type = LaunchResultType::MissionComplete;
    moonArrival.frontierTransfer = true;
    moonArrival.destinationId = content::destination::moon;
    introduceArrivalFlybyForTest(flyby);
    startArrivalOps(flyby, moonArrival);
    startArrivalFlybyRun(flyby, catalog);
    require(flyby.screen == Screen::Flyby && !flyby.run.flyby.completed,
        "first-wave template test should create an active Flyby");
    const PanelDocumentPresentation flybyPresentation = presentationFor(flyby, 0xA111);
    require(
        flybyPresentation.templateKind == PanelTemplateKind::ControlPanel
            && flybyPresentation.metadata.interaction == PanelInteractionMode::Realtime,
        "an active Flyby should select the shared Control Panel template");
    require(
        flybyPresentation.contentMarkup.find("flight-status-list") != std::string::npos
            && flybyPresentation.contentMarkup.find("rr-metric-strip") == std::string::npos
            && flybyPresentation.contentMarkup.find("rr-action-footer") != std::string::npos,
        "active Flyby should consume compact status and shared action-footer primitives");

    GameState scan = createNewGame(catalog, 0xA112);
    scan.run.destinationIndex = 2;
    startSurfaceExpedition(scan, catalog);
    Random scanRng(0xA112);
    require(startSurfaceScanRun(scan, scanRng).applied,
        "first-wave template test should create an active Surface Scan");
    const PanelDocumentPresentation scanPresentation = presentationFor(scan, 0xA112);
    require(
        scanPresentation.templateKind == PanelTemplateKind::SurfaceMinigame
            && scanPresentation.metadata.surface == PanelSurfaceKind::SurfaceScan
            && scanPresentation.metadata.interaction == PanelInteractionMode::Realtime,
        "Surface Scan should select the shared Surface Minigame template");
    require(
        scanPresentation.contentMarkup.find("rr-screen-header") != std::string::npos
            && scanPresentation.contentMarkup.find("rr-metric-strip") != std::string::npos
            && scanPresentation.contentMarkup.find("rr-action-footer") != std::string::npos,
        "Surface Scan should consume shared header, metric-strip, and action-footer primitives");
    scan.run.surfaceScan.completed = true;
    require(
        presentationFor(scan, 0xA112).metadata.interaction == PanelInteractionMode::Standard,
        "a completed Surface Scan should return Space and Enter to its explicit result actions");

    GameState push = createNewGame(catalog, 0xA119);
    push.run.destinationIndex = 2;
    startSurfaceExpedition(push, catalog);
    push.run.surfaceExpedition.depthProspects.push_back({1, 1});
    Random pushRng(0xA119);
    require(startSurfacePushRun(push, pushRng).applied,
        "first-wave template test should create an active Surface Push");
    const PanelDocumentPresentation pushPresentation = presentationFor(push, 0xA119);
    require(
        pushPresentation.templateKind == PanelTemplateKind::SurfaceMinigame
            && pushPresentation.metadata.surface == PanelSurfaceKind::SurfacePush
            && pushPresentation.metadata.interaction == PanelInteractionMode::Realtime,
        "Surface Push should select the shared Surface Minigame template and own its realtime step shortcut");
    push.run.surfacePush.busted = true;
    require(
        presentationFor(push, 0xA119).metadata.interaction == PanelInteractionMode::Standard,
        "a busted Surface Push should return Space and Enter to its explicit result actions");

    GameState surface = createNewGame(catalog, 0xA118);
    surface.run.destinationIndex = 2;
    startSurfaceExpedition(surface, catalog);
    surface.screen = Screen::SurfaceExpedition;
    const PanelDocumentPresentation surfacePresentation = presentationFor(surface, 0xA118);
    require(
        surfacePresentation.contentMarkup.find("rr-fixed-action-stack") != std::string::npos
            && surfacePresentation.contentMarkup.find("rr-fixed-action-context") != std::string::npos
            && surfacePresentation.contentMarkup.find("rr-fixed-action-lane") != std::string::npos
            && surfacePresentation.contentMarkup.find("rr-card-grid") != std::string::npos,
        "Surface Ops should consume the reusable fixed-action stack instead of screen-local positioning");

    GameState mining = createNewGame(catalog, 0xA113);
    mining.run.destinationIndex = 2;
    startSurfaceExpedition(mining, catalog);
    mining.run.surfaceExpedition.miningSitePrepared = true;
    require(startMiningRun(mining, catalog, {MiningAct::ActOne, 4, 0xA113}, false).applied,
        "first-wave template test should create an active Mining run");
    mining.run.surfaceExpedition.rigFuel = 16.0;
    mining.run.surfaceExpedition.rigFuelCapacity = 18.0;
    const MiningHudPresentation miningHud = miningHudPresentation(mining, catalog);
    require(miningHud.vitals[1].value == "16/18",
        "compact Mining HUD fuel should use whole units without overflowing its tile");
    const PanelDocumentPresentation miningPresentation = presentationFor(mining, 0xA113);
    require(
        miningPresentation.templateKind == PanelTemplateKind::Mining
            && miningPresentation.metadata.surface == PanelSurfaceKind::Mining
            && miningPresentation.metadata.overlay == PanelOverlayKind::MiningExperience,
        "Mining should select the shared Mining template");
    require(
        miningPresentation.contentMarkup.find("rr-screen-header") != std::string::npos
            && miningPresentation.contentMarkup.find("rr-metric-strip") != std::string::npos
            && miningPresentation.contentMarkup.find("rr-hud-mining-xp") == std::string::npos,
        "Mining should consume shared header and metric-strip primitives");
    require(
        miningPresentation.contentMarkup.find("mining-utility-button") != std::string::npos
            && miningPresentation.contentMarkup.find("\">DETAILS</button>") != std::string::npos
            && miningPresentation.contentMarkup.find("\">INV</button>") != std::string::npos
            && miningPresentation.contentMarkup.find("\">MENU</button>") != std::string::npos,
        "Mining utility controls should keep direct labels that RmlUi can render inside their button shells");
    require(
        miningPresentation.runtime.expeditionExperienceRequired > 0
            && miningPresentation.runtime.expeditionExperienceFilledSegments >= 0
            && miningPresentation.runtime.expeditionExperienceFilledSegments <= 12,
        "Mining XP should expose stable values for the scene overlay");

    Random miningTitleRng(0xA113);
    const PreparedLaunch miningTitleLaunch = prepareLaunch(mining, catalog, miningTitleRng);
    PanelRenderContext miningTitleContext {
        mining,
        catalog,
        miningTitleLaunch,
        miningTitleLaunch,
    };
    miningTitleContext.titleScreenActive = true;
    miningTitleContext.hasSavedGame = true;
    miningTitleContext.firstTimeIntroductionsEnabled = false;
    const PanelDocumentPresentation miningTitlePresentation =
        buildGamePanelPresentation(miningTitleContext);
    require(
        miningTitlePresentation.metadata.overlay == PanelOverlayKind::None,
        "Title screen should suppress a stale Mining XP overlay when the saved game is in Mining");

    GameState briefing = createNewGame(catalog, 0xA114);
    briefing.screen = Screen::StoryBriefing;
    briefing.storyBriefing.pending = StoryBriefingId::CampaignIntroduction;
    const PanelDocumentPresentation briefingPresentation = presentationFor(briefing, 0xA114);
    require(
        briefingPresentation.templateKind == PanelTemplateKind::Takeover
            && briefingPresentation.metadata.interaction == PanelInteractionMode::Takeover,
        "Story Briefing should select the shared Takeover template");

    GameState results = createNewGame(catalog, 0xA115);
    results.screen = Screen::Results;
    results.lastOutcome.type = LaunchResultType::SafeEject;
    results.lastOutcome.recoveryMethod = RecoveryMethod::ReturnHome;
    results.lastOutcome.ejectMultiplier = 1.1;
    results.lastOutcome.crashMultiplier = 1.5;
    const PanelDocumentPresentation resultsPresentation = presentationFor(results, 0xA115);
    require(
        resultsPresentation.templateKind == PanelTemplateKind::Results
            && resultsPresentation.metadata.interaction == PanelInteractionMode::Takeover,
        "Results should select the shared Results template");
    require(
        hangarPresentation.metadata.legacyContentOwnsLaneGeometry
            && flybyPresentation.metadata.legacyContentOwnsLaneGeometry
            && scanPresentation.metadata.legacyContentOwnsLaneGeometry
            && pushPresentation.metadata.legacyContentOwnsLaneGeometry
            && miningPresentation.metadata.legacyContentOwnsLaneGeometry
            && briefingPresentation.metadata.legacyContentOwnsLaneGeometry
            && resultsPresentation.metadata.legacyContentOwnsLaneGeometry,
        "first-wave templates should explicitly preserve their migrated content-owned lane geometry");
    require(
        !PanelDocumentPresentation {}.metadata.legacyContentOwnsLaneGeometry,
        "future template presentations should inherit shell lane geometry unless they explicitly opt into migration compatibility");
    const auto resultsOutcome = std::find_if(
        resultsPresentation.modals.begin(),
        resultsPresentation.modals.end(),
        [](const ModalPresentation& modal) {
            return modal.id == ui::modals::launchOutcome;
        });
    require(
        resultsOutcome != resultsPresentation.modals.end()
            && resultsOutcome->bodyMarkup.find("rr-action-footer") != std::string::npos,
        "Results should consume the shared action-footer primitive inside its typed acknowledgement modal");
}

void structuredPanelPresentationCarriesTypedModalPolicy()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState lunar = createNewGame(catalog, 0xA116);
    lunar.run.destinationIndex = 1;
    startSurfaceExpedition(lunar, catalog);
    lunar.screen = Screen::SurfaceExpedition;
    Random lunarRng(0xA116);
    const PreparedLaunch lunarLaunch = prepareLaunch(lunar, catalog, lunarRng);
    PanelRenderContext lunarContext {lunar, catalog, lunarLaunch, lunarLaunch};
    lunarContext.firstTimeIntroductionsEnabled = false;
    const PanelDocumentPresentation lunarPresentation =
        buildGamePanelPresentation(lunarContext);

    const auto lunarModal = std::find_if(
        lunarPresentation.modals.begin(),
        lunarPresentation.modals.end(),
        [](const ModalPresentation& modal) {
            return modal.id == "scenario_" + std::string(content::scenario::lunarProspector) + "_briefing";
        });
    require(lunarModal != lunarPresentation.modals.end(),
        "the mandatory Lunar briefing should be emitted as typed modal data");
    require(
        lunarModal->autoOpen && !lunarModal->dismissible
            && lunarModal->closeAction.empty()
            && lunarModal->bodyMarkup.find(ui::actions::scenarioAction(
                content::scenario::lunarProspector,
                "briefing",
                static_cast<int>(ScenarioActionKind::AcknowledgeBriefing))) != std::string::npos,
        "the Lunar briefing should auto-open, reject generic dismissal, and require its named action");
    require(
        lunarPresentation.contentMarkup.find("<template") == std::string::npos
            && lunarPresentation.contentMarkup.find("data-modal=") == std::string::npos,
        "panel content should not carry the retired template-based modal transport");

    const auto settingsModal = std::find_if(
        lunarPresentation.modals.begin(),
        lunarPresentation.modals.end(),
        [](const ModalPresentation& modal) {
            return modal.id == ui::modals::settings;
        });
    require(
        settingsModal != lunarPresentation.modals.end()
            && !settingsModal->autoOpen
            && settingsModal->dismissible
            && settingsModal->showClose
            && settingsModal->tone == ModalTone::Neutral
            && modalToneCssClass(settingsModal->tone).empty(),
        "ordinary utility modals should remain explicitly dismissible in typed metadata");

    GameState results = createNewGame(catalog, 0xA117);
    results.screen = Screen::Results;
    results.lastOutcome.type = LaunchResultType::SafeEject;
    results.lastOutcome.recoveryMethod = RecoveryMethod::ReturnHome;
    results.lastOutcome.ejectMultiplier = 1.1;
    results.lastOutcome.crashMultiplier = 1.5;
    Random resultsRng(0xA117);
    const PreparedLaunch resultsLaunch = prepareLaunch(results, catalog, resultsRng);
    const PanelDocumentPresentation resultsPresentation =
        buildGamePanelPresentation({results, catalog, resultsLaunch, resultsLaunch});
    const auto outcomeModal = std::find_if(
        resultsPresentation.modals.begin(),
        resultsPresentation.modals.end(),
        [](const ModalPresentation& modal) {
            return modal.id == ui::modals::launchOutcome;
        });
    require(
        outcomeModal != resultsPresentation.modals.end()
            && outcomeModal->autoOpen
            && !outcomeModal->dismissible
            && outcomeModal->tone == ModalTone::Negative
            && modalToneCssClass(outcomeModal->tone) == "modal-tone-negative",
        "an incomplete return should preserve mandatory acknowledgement and use the negative outcome tone");

    results.lastOutcome.type = LaunchResultType::MissionComplete;
    results.lastOutcome.recoveryMethod = RecoveryMethod::ReturnHome;
    const PanelDocumentPresentation successfulResultsPresentation =
        buildGamePanelPresentation({results, catalog, resultsLaunch, resultsLaunch});
    const auto successfulOutcomeModal = std::find_if(
        successfulResultsPresentation.modals.begin(),
        successfulResultsPresentation.modals.end(),
        [](const ModalPresentation& modal) {
            return modal.id == ui::modals::launchOutcome;
        });
    require(
        successfulOutcomeModal != successfulResultsPresentation.modals.end()
            && successfulOutcomeModal->tone == ModalTone::Positive
            && modalToneCssClass(successfulOutcomeModal->tone) == "modal-tone-positive",
        "a completed return should use the positive outcome tone");

    results.lastOutcome.fuelSurveyReturnTiming = FuelSurveyReturnTiming::Late;
    const PanelDocumentPresentation lateResultsPresentation =
        buildGamePanelPresentation({results, catalog, resultsLaunch, resultsLaunch});
    const auto lateOutcomeModal = std::find_if(
        lateResultsPresentation.modals.begin(),
        lateResultsPresentation.modals.end(),
        [](const ModalPresentation& modal) {
            return modal.id == ui::modals::launchOutcome;
        });
    require(
        lateOutcomeModal != lateResultsPresentation.modals.end()
            && lateOutcomeModal->tone == ModalTone::Warning
            && modalToneCssClass(lateOutcomeModal->tone) == "modal-tone-warning",
        "a late qualified return should use the amber modal");
}

void contentIdsResolveAgainstDefaultCatalog()
{
    const ContentCatalog catalog = createDefaultContent();

    require(catalog.findModule(content::module::sparrowEngine) != nullptr, "starter module id should resolve");
    require(catalog.findModule(content::module::radiatorVanes) != nullptr, "cooling module id should resolve");
    require(catalog.findCrewUpgrade(content::crewUpgrade::analogSimBay) != nullptr, "crew upgrade id should resolve");
    require(catalog.findFrame(content::frame::pathfinder) != nullptr, "ship frame id should resolve");
    const Astronaut* startingCrew = catalog.findAstronaut(content::astronaut::ava);
    require(startingCrew != nullptr, "astronaut id should resolve");
    require(catalog.findDestination(content::destination::moon) != nullptr, "destination id should resolve");
    require(catalog.findResearchProject(content::research::blueprintSurvey) != nullptr, "research project id should resolve");
    require(catalog.findResearchProject(content::research::fieldProbeNetwork) != nullptr, "field probe research id should resolve");
    require(catalog.findResearchProject(content::research::regolithDrillRig) != nullptr, "drill research id should resolve");
    require(catalog.findResearchProject(content::research::cargoReturnRig) != nullptr, "cargo research id should resolve");
    require(catalog.findResearchProject(content::research::droneBayProgram) != nullptr, "drone bay research id should resolve");
    require(catalog.findResearchProject(content::research::perimeterDroneNetwork) != nullptr, "perimeter drone research id should resolve");
    const ResearchProject* arkProject = catalog.findResearchProject(content::research::arkScaffoldProgram);
    require(arkProject != nullptr, "ark scaffold research id should resolve");
    require(arkProject->requiredDestinationTier == 3, "ark scaffold should start at the outer-planets phase");
    require(arkProject->rewardUnlockKey == content::unlock::arkScaffold, "ark scaffold should unlock the future home-base hook");
    require(catalog.findSurfaceUpgrade(content::surfaceUpgrade::thermalDrillJackets) != nullptr, "surface upgrade ids should resolve");
    require(catalog.findSurfaceUpgrade(content::surfaceUpgrade::widebandPulse) != nullptr, "scanner surface upgrade id should resolve");
    require(catalog.findMiniDrone(content::drone::miningDrone) != nullptr, "mining drone id should resolve");
    require(catalog.findMiniDrone(content::drone::resourceDrone) != nullptr, "resource drone id should resolve");
    require(catalog.findMiniDrone(content::drone::surveyDrone) != nullptr, "survey drone id should resolve");
    require(catalog.findMiniDrone(content::drone::hazardDrone) != nullptr, "hazard drone id should resolve");

    MetaProgress meta;
    require(hasUnlock(meta, content::unlock::starter), "starter unlock should stay implicit");
    meta.unlockKeys.push_back(content::unlock::thermal);
    require(hasUnlock(meta, content::unlock::thermal), "named unlock key should resolve through shared ids");
    meta.unlockKeys.push_back(content::unlock::surfaceProbes);
    require(hasUnlock(meta, content::unlock::surfaceProbes), "surface unlock key should resolve through shared ids");
    meta.unlockKeys.push_back(content::unlock::analysisLab);
    require(hasUnlock(meta, content::unlock::analysisLab), "research facility unlock key should resolve through shared ids");
    meta.unlockKeys.push_back(content::unlock::perimeterDrones);
    require(hasUnlock(meta, content::unlock::perimeterDrones), "passive defense unlock key should resolve through shared ids");
    meta.unlockKeys.push_back(content::unlock::perimeterCoordination);
    require(hasUnlock(meta, content::unlock::perimeterCoordination), "advanced combat coordination should resolve through shared ids");
}


void outerPlanetCampaignSequenceIsExplicitAndUnskippable()
{
    const ContentCatalog catalog = createDefaultContent();
    struct ExpectedDestination {
        std::string_view id;
        int tier;
        double target;
        double maxCrash;
        double reward;
        double hazard;
    };
    const std::array<ExpectedDestination, 4> expected {{
        {content::destination::jupiter, 3, 3.15, 5.00, 44.0, 1.55},
        {content::destination::saturn, 4, 3.45, 5.40, 52.0, 1.70},
        {content::destination::uranus, 5, 3.80, 5.85, 62.0, 1.85},
        {content::destination::neptune, 6, 4.20, 6.35, 76.0, 2.05},
    }};
    for (const ExpectedDestination& item : expected) {
        const Destination* destination = catalog.findDestination(item.id);
        require(destination != nullptr, "every outer planet should have a stable destination id");
        require(destination->tier == item.tier && std::abs(destination->targetMultiplier - item.target) < 0.000001,
            "outer-planet tier and target values should match the campaign contract");
        require(std::abs(destination->maxCrashMultiplier - item.maxCrash) < 0.000001
                && std::abs(destination->baseReward - item.reward) < 0.000001
                && std::abs(destination->hazard - item.hazard) < 0.000001,
            "outer-planet crash, reward, and hazard values should match the campaign contract");
    }
    require(catalog.findDestination(content::destination::outerPlanets) == nullptr,
        "the grouped outer_planets id must remain migration-only, not playable content");
    require(catalog.findDestination(content::destination::nearbyStar)->tier == 7
            && catalog.findDestination(content::destination::nearbyGalaxy)->tier == 8,
        "Khepri Prime and Rift Belt should follow Neptune at tiers 7 and 8");

    GameState state = createNewGame(catalog, 0x5511);
    state.run.destinationIndex = 2;
    state.meta.launchLessons.stage = LaunchTrainingStage::Complete;
    syncLaunchConfig(state, catalog);
    LaunchOutcome skipped;
    skipped.type = LaunchResultType::MissionComplete;
    skipped.recoveryMethod = RecoveryMethod::TransferArrival;
    skipped.frontierTransfer = true;
    skipped.destinationId = content::destination::saturn;
    skipped.ejectMultiplier = 3.45;
    skipped.crashMultiplier = 5.40;
    applyLaunchOutcome(state, catalog, skipped);
    require(state.run.destinationIndex == 2 && destinationHistoryValue(state.meta.destinationSuccesses, catalog, content::destination::saturn) == 0,
        "Saturn cannot be completed or unlocked while Jupiter is current");

    for (std::size_t index = 0; index < expected.size(); ++index) {
        const ExpectedDestination& item = expected[index];
        LaunchOutcome arrival;
        arrival.type = LaunchResultType::MissionComplete;
        arrival.recoveryMethod = RecoveryMethod::TransferArrival;
        arrival.frontierTransfer = true;
        arrival.destinationId = std::string(item.id);
        arrival.ejectMultiplier = item.target;
        arrival.crashMultiplier = item.maxCrash;
        applyLaunchOutcome(state, catalog, arrival);
        require(state.run.destinationIndex == item.tier, "each successful arrival should unlock exactly the next outer planet");
        require(destinationHistoryValue(state.meta.destinationSuccesses, catalog, item.id) == 1,
            "each outer planet should retain its own completion history");
        if (item.id != content::destination::neptune) {
            require(!arkDiscovered(state) && state.storyBriefing.pending == StoryBriefingId::None,
                "Jupiter through Uranus must not reveal or hint at the Straylight");
        }
    }
    require(!arkDiscovered(state) && state.storyBriefing.pending == StoryBriefingId::StraylightDiscovery,
        "only successful Neptune arrival should queue the saved Straylight reveal");
    require(nextDestination(state, catalog) == nullptr, "the solar transfer ladder should stop at Neptune until the story beat is acknowledged");
}

void storyBriefingsTakeOverAndPersist()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 0x5522);
    LaunchOutcome neptune;
    neptune.type = LaunchResultType::MissionComplete;
    neptune.recoveryMethod = RecoveryMethod::TransferArrival;
    neptune.frontierTransfer = true;
    neptune.destinationId = content::destination::neptune;
    neptune.ejectMultiplier = 4.20;
    neptune.crashMultiplier = 6.35;
    state.run.destinationIndex = 5;
    applyLaunchOutcome(state, catalog, neptune);
    startArrivalOps(state, neptune);
    state.screen = Screen::StoryBriefing;

    Random rng(0x5522);
    const PreparedLaunch launch = prepareLaunch(state, catalog, rng);
    PanelRenderContext context {state, catalog, launch, launch};
    const std::string html = buildGamePanelHtml(context);
    require(html.find("data-panel-mode=\"story-briefing\"") != std::string::npos,
        "Straylight discovery should use the dedicated full-screen story panel");
    require(countOccurrences(html, "data-rr-action=") == 1
            && html.find("data-rr-action=\"acknowledge_story_briefing\"") != std::string::npos,
        "the takeover should expose only Approach the Straylight as an action");
    require(html.find("data-ui-close-modal") == std::string::npos
            && html.find("data-ui-modal=\"settings\"") == std::string::npos
            && html.find("data-ui-modal=\"map\"") == std::string::npos,
        "story takeover markup must not expose close, navigation, HUD, or settings controls");

    const std::optional<SaveData> parsed = deserializeSaveData(serializeSaveData(captureSaveData(state)));
    require(parsed.has_value(), "pending story takeover should serialize");
    GameState restored = createNewGame(catalog, 1);
    restoreSaveData(restored, catalog, *parsed);
    require(restored.screen == Screen::StoryBriefing && restored.storyBriefing.pending == StoryBriefingId::StraylightDiscovery,
        "reload during the reveal must return to the takeover");
    require(acknowledgeStoryBriefing(restored, catalog), "the saved takeover should acknowledge once");
    require(arkDiscovered(restored) && restored.meta.straylightDiscoveryAcknowledged && restored.screen == Screen::ArrivalOps,
        "approach should atomically discover the Ark and resume Neptune Arrival Ops");
}

void miningThermalCutoffAndGuidanceAreExplicit()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 0x5544);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    require(startMiningRun(state, catalog, {MiningAct::ActOne, 9, 0x5544}, false).applied,
        "thermal cutoff test mining run should start with scanner, tether, and heat rules enabled");
    MiningRunState& mining = state.run.mining;
    mining.drillHeat = 1.0;
    mining.drilling = true;
    MiningEnemy thermal;
    thermal.active = true;
    thermal.type = MiningEnemyType::Elemental;
    thermal.affinity = MiningElementalAffinity::Thermal;
    thermal.x = mining.droneX;
    thermal.y = mining.droneY;
    thermal.health = thermal.maxHealth = 100.0;
    thermal.effectRadius = 4.0;
    mining.enemies.push_back(thermal);
    updateMiningRun(state, catalog, 0.01);
    require(mining.drillThermalLock && !mining.drilling, "100% heat should cut off active drilling");
    setMiningDrilling(state, true);
    require(!mining.drilling, "thermal lock should reject restart above 60% heat");
    mining.enemies.clear();
    mining.drillHeat = 0.60;
    updateMiningRun(state, catalog, 0.01);
    require(!mining.drillThermalLock, "drilling should become available again at or below 60% heat");

    pulseMiningScanner(state, catalog);
    mining.artifact = {};
    MiningRunPresentation presentation = miningRunPresentation(state, catalog);
    auto tether = std::find_if(presentation.actions.begin(), presentation.actions.end(), [](const PanelButtonPresentation& action) {
        return action.actionId == ui::actions::miningTether;
    });
    require(tether != presentation.actions.end() && !tether->enabled &&
            tether->label == "No tether target",
        "the Mining Rig should not offer a retired ship tether without an artifact");

    mining.artifact.present = true;
    mining.artifact.state = MiningArtifactState::Embedded;
    presentation = miningRunPresentation(state, catalog);
    tether = std::find_if(presentation.actions.begin(), presentation.actions.end(), [](const PanelButtonPresentation& action) {
        return action.actionId == ui::actions::miningTether;
    });
    require(tether != presentation.actions.end() && !tether->enabled &&
            tether->label == "Scan or expose artifact",
        "an embedded artifact should explain that it must be exposed before tethering");

    mining.miniDrones.emplace_back();
    presentation = miningRunPresentation(state, catalog);
    require(!presentation.commandHints.empty(),
        "equipped helper drones should expose command guidance");
}

void marsMiningPressureFitsOxygenWindow()
{
    const ContentCatalog catalog = createDefaultContent();
    const MiningArenaRules moonRules = resolveMiningArenaRules({MiningAct::ActOne, 3, 0x5545});
    const MiningArenaRules marsRules = resolveMiningArenaRules({MiningAct::ActOne, 4, 0x5545});
    require(moonRules.mechanics.oxygenAndFuel && !moonRules.mechanics.drillHeat && !moonRules.mechanics.drillIntegrity,
        "the Moon tutorial should teach the oxygen return cycle before Mars adds heat and integrity pressure");
    require(marsRules.mechanics.oxygenAndFuel && marsRules.mechanics.drillHeat
            && marsRules.mechanics.drillIntegrity && marsRules.mechanics.fieldRepairs,
        "Mars should introduce oxygen, heat, integrity, and field-repair pressure together");

    const double secondsToIntegrityWear =
        tuning::mining::heatDamageThreshold / tuning::mining::heatRisePerSecond;
    const double secondsToThermalLock =
        tuning::mining::drillHeatFlashThreshold / tuning::mining::heatRisePerSecond;
    require(secondsToIntegrityWear < tuning::mining::oxygenSeconds
            && secondsToThermalLock < tuning::mining::oxygenSeconds,
        "continuous Mars drilling should enter integrity wear and thermal lock before the 30-second oxygen cycle expires");
}


void secondaryMiningStateRoundTrips()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 1);
    auto& expedition = state.run.surfaceExpedition;
    expedition.active = true;
    state.screen = Screen::SurfaceUpgrade;
    expedition.expeditionLevel = 4;
    expedition.expeditionExperience = 37.5;
    expedition.pendingRunUpgradeChoices = 2;
    expedition.runUpgradeOffers = {{
        {RunUpgradeKind::Rig, content::surfaceUpgrade::thermalDrillJackets, 2, -1},
        {RunUpgradeKind::DroneRank, content::drone::surveyDrone, 3, -1},
        {RunUpgradeKind::DroneGraft, "pulse_strike", 0, 1}}};
    expedition.runUpgradeOfferCount = 3;
    expedition.runUpgradeOfferPending = true;
    expedition.runUpgradeReturnScreen = Screen::Mining;
    expedition.runRigUpgradeRanks = {{content::surfaceUpgrade::thermalDrillJackets, 2}};
    expedition.runDroneRanks = {{content::drone::surveyDrone, 3}};
    expedition.selectedSynergyIds = {"relic_pathfinder", "full_spectrum_swarm"};
    expedition.scannerCooldownSeconds = 2.5;
    expedition.treasureMarks.push_back({4, 5, 2});
    expedition.droneModuleAssignments.push_back({1, content::drone::surveyDrone, DroneModuleKind::PulseStrike});
    expedition.droneModuleRuntime.push_back({1, 0.4, 1.2, {}});
    MiningMiniDroneAgent agent;
    agent.haulMaterials = {4, 2, 1};
    agent.uncreditedHaulMaterials = {3, 1, 1};
    state.run.mining.miniDrones.push_back(agent);
    const auto parsed = deserializeSaveData(serializeSaveData(captureSaveData(state)));
    require(parsed.has_value(), "secondary mining state should serialize");
    GameState restored = createNewGame(catalog, 1);
    restoreSaveData(restored, catalog, *parsed);
    const auto& restoredExpedition = restored.run.surfaceExpedition;
    require(restoredExpedition.expeditionLevel == 4 && std::abs(restoredExpedition.expeditionExperience - 37.5) < 0.001 &&
            restoredExpedition.pendingRunUpgradeChoices == 2,
        "expedition level, experience, and queued run choices should round trip");
    require(restoredExpedition.runUpgradeOfferPending && restoredExpedition.runUpgradeOfferCount == 3 &&
            restoredExpedition.runUpgradeOffers[0].kind == RunUpgradeKind::Rig &&
            restoredExpedition.runUpgradeOffers[0].definitionId == content::surfaceUpgrade::thermalDrillJackets &&
            restoredExpedition.runUpgradeOffers[1].kind == RunUpgradeKind::DroneRank &&
            restoredExpedition.runUpgradeOffers[1].targetRank == 3 &&
            restoredExpedition.runUpgradeOffers[2].kind == RunUpgradeKind::DroneGraft &&
            restoredExpedition.runUpgradeOffers[2].slotIndex == 1 &&
            restoredExpedition.runUpgradeReturnScreen == Screen::Mining &&
            restored.screen == Screen::SurfaceUpgrade,
        "the pending level-up draft and return screen should round trip");
    require(restoredExpedition.runRigUpgradeRanks.size() == 1 &&
            restoredExpedition.runRigUpgradeRanks.front().rank == 2 &&
            restoredExpedition.runDroneRanks.size() == 1 &&
            restoredExpedition.runDroneRanks.front().rank == 3 &&
            restoredExpedition.selectedSynergyIds.size() == 2,
        "temporary rig ranks, drone ranks, and selected synergies should round trip");
    require(restored.run.surfaceExpedition.scannerCooldownSeconds > 2.4 && restored.run.surfaceExpedition.treasureMarks.size() == 1,
        "scanner cooldown and treasure marks should round trip");
    require(restored.run.surfaceExpedition.droneModuleAssignments.size() == 1 && restored.run.surfaceExpedition.droneModuleRuntime.size() == 1,
        "module assignments and runtime should round trip");
    require(restored.run.mining.miniDrones.size() == 1 &&
            restored.run.mining.miniDrones.front().uncreditedHaulMaterials.common == 3 &&
            restored.run.mining.miniDrones.front().uncreditedHaulMaterials.rare == 1 &&
            restored.run.mining.miniDrones.front().uncreditedHaulMaterials.exotic == 1,
        "uncredited drone haul provenance should round trip without creating duplicate XP");
}

void secondaryPulseUsesUnifiedCooldownAndStrongestHit()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 1234);
    auto& mining = state.run.mining;
    mining.arenaMetadata.act = MiningAct::ActTwo;
    mining.arenaMetadata.difficulty = 2;
    state.meta.equippedDroneIds = {content::drone::surveyDrone};
    state.meta.droneBaySlots = 1;
    state.meta.unlockKeys = {content::unlock::droneBay, content::unlock::droneSupportSuite};
    state.run.surfaceExpedition.runDroneRanks.push_back({content::drone::surveyDrone, 3});
    mining.active = true;
    mining.terrain.width = 12; mining.terrain.height = 12; mining.terrain.cells.resize(144);
    mining.droneX = mining.operatorX = 6.0; mining.droneY = mining.operatorY = 6.0;
    MiningEnemy enemy = createMiningEnemy(MiningEnemyType::Mammal, MiningCellFeature::EncounterZone, 6.0, 6.0);
    enemy.health = enemy.maxHealth = 20.0; mining.enemies.push_back(enemy);
    MiningMiniDroneAgent survey; survey.role = MiniDroneRole::Survey; survey.roleIndex = 0; survey.equippedFrame = 0; survey.upgradeLevel = 3; survey.x = 6.0; survey.y = 6.0; mining.miniDrones.push_back(survey);
    state.run.surfaceExpedition.droneModuleAssignments.push_back({0, content::drone::surveyDrone, DroneModuleKind::PulseStrike});
    state.run.surfaceExpedition.runRigUpgradeRanks.push_back({content::surfaceUpgrade::resonantDischarge, 3});
    pulseMiningScanner(state, catalog);
    const double healthAfterPulse = mining.enemies.front().health;
    require(healthAfterPulse == 17.0, "pulse should leave enemy state valid after activation");
    pulseMiningScanner(state, catalog);
    require(mining.enemies.front().health == healthAfterPulse, "manual scanner should respect unified cooldown");
}

void treasurePingMarksRareFirstAndSkipsExcludedMaterials()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 4567);
    auto& mining = state.run.mining;
    mining.arenaMetadata.act = MiningAct::ActTwo;
    mining.arenaMetadata.difficulty = 2;
    mining.active = true; mining.terrain.width = 10; mining.terrain.height = 10; mining.terrain.cells.resize(100);
    state.meta.equippedDroneIds = {content::drone::resourceDrone};
    state.meta.droneBaySlots = 1;
    state.run.surfaceExpedition.runDroneRanks.push_back({content::drone::resourceDrone, 2});
    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    state.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
    mining.droneX = mining.operatorX = 5.0; mining.droneY = mining.operatorY = 5.0;
    for (int y = 2; y < 8; ++y) for (int x = 2; x < 8; ++x) {
        MiningCell& cell = mining.terrain.cells[static_cast<std::size_t>(y * 10 + x)];
        cell.material = (x == 3 ? MiningCellMaterial::RareOre : MiningCellMaterial::CommonOre);
        cell.revealed = true; cell.maxToughness = cell.remainingToughness = 1.0;
    }
    mining.terrain.cells[22].material = MiningCellMaterial::ExoticVein;
    mining.terrain.cells[23].material = MiningCellMaterial::ArtifactCache;
    MiningMiniDroneAgent resource; resource.role = MiniDroneRole::Resource; resource.roleIndex = 0; resource.equippedFrame = 0; resource.upgradeLevel = 2; mining.miniDrones.push_back(resource);
    state.run.surfaceExpedition.droneModuleAssignments.push_back({0, content::drone::resourceDrone, DroneModuleKind::TreasurePing});
    pulseMiningScanner(state, catalog);
    require(state.run.surfaceExpedition.treasureMarks.size() == 2, "Mk II Treasure Ping should mark two tiles");
    require(state.run.surfaceExpedition.treasureMarks.front().x == 3, "Treasure Ping should prioritize Rare ore");
    require(std::none_of(state.run.surfaceExpedition.treasureMarks.begin(), state.run.surfaceExpedition.treasureMarks.end(), [](const TreasureMark& mark) { return mark.x == 2 && (mark.y == 2 || mark.y == 3); }), "Treasure Ping should exclude non-normal materials");
    const auto first = state.run.surfaceExpedition.treasureMarks;
    state.run.surfaceExpedition.scannerCooldownSeconds = 0.0;
    pulseMiningScanner(state, catalog);
    require(state.run.surfaceExpedition.scannerCooldownSeconds > 3.9, "manual pulse should start the unified recharge");
    require(std::abs(mining.scannerPulseSeconds - tuning::mining::scannerPulseSeconds) < 1e-9,
        "manual pulse should use the shared 0.64-second presentation duration");
    require(tuning::mining::scannerRechargePresentationProgress(4.0) == 0.0
            && tuning::mining::scannerRechargePresentationProgress(3.36) < 1e-9
            && tuning::mining::scannerRechargePresentationProgress(0.0) == 1.0,
        "visible scanner recharge should begin after the pulse and finish with the shared cooldown");
    require(state.run.surfaceExpedition.treasureMarks.size() >= first.size(), "repeated Treasure Ping should preserve existing marks and select new tiles");
    resource.upgradeLevel = 3;
    mining.miniDrones.front().upgradeLevel = 3;
    state.run.surfaceExpedition.runDroneRanks.front().rank = 3;
    state.run.surfaceExpedition.treasureMarks.clear();
    state.run.surfaceExpedition.scannerCooldownSeconds = 0.0;
    pulseMiningScanner(state, catalog);
    require(state.run.surfaceExpedition.treasureMarks.size() == 3, "Mk III Treasure Ping should mark three tiles");
    state.run.surfaceExpedition.runDroneRanks.front().rank = 1;
    mining.miniDrones.front().upgradeLevel = 1;
    state.run.surfaceExpedition.treasureMarks.clear();
    state.run.surfaceExpedition.scannerCooldownSeconds = 0.0;
    pulseMiningScanner(state, catalog);
    require(state.run.surfaceExpedition.treasureMarks.size() == 1, "Mk I Treasure Ping should mark one tile");
    require(applyMiningTreasureMultiplier({1, 0, 0}, MiningCellMaterial::CommonOre, 2).common == 2 &&
            applyMiningTreasureMultiplier({0, 1, 0}, MiningCellMaterial::RareOre, 2).rare == 2 &&
            applyMiningTreasureMultiplier({0, 0, 1}, MiningCellMaterial::ExoticVein, 2).exotic == 1,
        "Treasure multiplier should double only normal Common and Rare payouts");
}

void secondaryHybridTuningCoversAllRanksAndCaps()
{
    const auto near = [](double a, double b) { return std::abs(a - b) < 1e-9; };
    const std::array<DroneModuleKind, 10> modules = {
        DroneModuleKind::CombatDrill, DroneModuleKind::DrillGuard, DroneModuleKind::SpectrumFilter,
        DroneModuleKind::OreRelay, DroneModuleKind::ContainmentShell, DroneModuleKind::ReclamationLoop,
        DroneModuleKind::TargetedAssault, DroneModuleKind::PenetratingImpact,
        DroneModuleKind::RetributionArc, DroneModuleKind::HazardScreen};
    for (const DroneModuleKind module : modules) {
        require(secondaryModuleValue(module, 1) > 0.0 && secondaryModuleValue(module, 2) >= secondaryModuleValue(module, 1)
                && secondaryModuleValue(module, 3) >= secondaryModuleValue(module, 2),
            "every secondary hybrid should scale monotonically through Mk III");
    }
    require(near(secondaryModuleValue(DroneModuleKind::CombatDrill, 1), 1.0)
            && near(secondaryModuleValue(DroneModuleKind::CombatDrill, 2), 2.0)
            && near(secondaryModuleValue(DroneModuleKind::CombatDrill, 3), 3.0), "Combat Drill damage should scale 1/2/3");
    require(near(secondaryModuleValue(DroneModuleKind::DrillGuard, 1), .08)
            && near(secondaryModuleValue(DroneModuleKind::DrillGuard, 2), .12)
            && near(secondaryModuleValue(DroneModuleKind::DrillGuard, 3), .16), "Drill Guard relief should scale 8/12/16 percent");
    require(near(secondaryModuleValue(DroneModuleKind::SpectrumFilter, 1), .10)
            && near(secondaryModuleValue(DroneModuleKind::SpectrumFilter, 2), .18)
            && near(secondaryModuleValue(DroneModuleKind::SpectrumFilter, 3), .25), "Spectrum Filter relief should scale 10/18/25 percent");
    require(near(secondaryModuleValue(DroneModuleKind::OreRelay, 3), 3.0), "Ore Relay should add three chunks at Mk III");
    require(near(secondaryModuleValue(DroneModuleKind::ContainmentShell, 3), .16), "Containment Shell should reach 16 percent");
    require(near(secondaryModuleValue(DroneModuleKind::ReclamationLoop, 1), .5)
            && near(secondaryModuleValue(DroneModuleKind::ReclamationLoop, 3), 1.5), "Reclamation Loop should recover .5/1/1.5 fuel per tile");
    require(near(secondaryModuleValue(DroneModuleKind::TargetedAssault, 3), 16.0), "Targeted Assault should add 16 crit points at Mk III");
    require(near(secondaryModuleValue(DroneModuleKind::PenetratingImpact, 1), .10)
            && secondaryModuleSecondaryHits(DroneModuleKind::PenetratingImpact, 1) == 0
            && secondaryModuleSecondaryHits(DroneModuleKind::PenetratingImpact, 2) == 1
            && secondaryModuleSecondaryHits(DroneModuleKind::PenetratingImpact, 3) == 2, "Penetrating Impact should scale armor and aligned targets");
    require(near(secondaryModuleValue(DroneModuleKind::RetributionArc, 3), 3.0), "Retribution Arc should reach three counter damage");
    require(near(secondaryModuleValue(DroneModuleKind::HazardScreen, 3), .25), "Hazard Screen should reach 25 percent");
    require(secondaryModuleSecondaryHits(DroneModuleKind::CombatDrill, 3) == 0, "non-penetrating hybrids should not gain secondary hits");
    require(std::min(.24, .16 + .16 + .16) == .24, "Drill Guard duplicate relief should cap at 24 percent");
    require(std::max(.10, .25) == .25, "Hazard Screen duplicates should use highest protection");
}

void postSolarBodiesAndGeologiesAreDeterministicAndPersistent()
{
    const auto geologies = postSolarGeologyCatalog();
    require(geologies.size() == 32U, "post-solar geology catalog should contain 32 families");
    std::vector<bool> rows(32U, false);
    std::vector<std::string> ids;
    for (const PostSolarGeologyProfile& geology : geologies) {
        require(geology.atlasRow >= 0 && geology.atlasRow < 32, "geology atlas row should be in range");
        require(!rows[static_cast<std::size_t>(geology.atlasRow)], "geology atlas rows should be unique");
        rows[static_cast<std::size_t>(geology.atlasRow)] = true;
        require(std::find(ids.begin(), ids.end(), geology.id) == ids.end(), "geology ids should be unique");
        ids.emplace_back(geology.id);
    }

    const auto requireSameRoster = [](const PostSolarSystemRoster& left, const PostSolarSystemRoster& right) {
        require(left.systemId == right.systemId, "system ids should be deterministic");
        require(left.seed == right.seed, "system seed should be deterministic");
        require(left.primaryBodyId == right.primaryBodyId, "primary body should be deterministic");
        require(left.bodies.size() == right.bodies.size(), "body count should be deterministic");
        for (std::size_t index = 0; index < left.bodies.size(); ++index) {
            const PostSolarBodyProfile& a = left.bodies[index];
            const PostSolarBodyProfile& b = right.bodies[index];
            require(a.id == b.id && a.name == b.name && a.parentId == b.parentId,
                "body identity should be deterministic");
            require(a.kind == b.kind && a.visualArchetype == b.visualArchetype,
                "body archetype should be deterministic");
            require(a.surfaceGeologyId == b.surfaceGeologyId && a.deepGeologyId == b.deepGeologyId,
                "body geology should be deterministic");
            require(a.seed == b.seed && a.mineable == b.mineable,
                "body mining state should be deterministic");
        }
    };

    constexpr std::uint64_t seed = 0xA4A2C0DEULL;
    const PostSolarSystemRoster aaru = generatePostSolarSystemRoster(content::postSolarSystem::aaruVale, seed);
    requireSameRoster(aaru, generatePostSolarSystemRoster(content::postSolarSystem::aaruVale, seed));
    const auto primaryCount = [](const PostSolarSystemRoster& roster) {
        return std::count_if(roster.bodies.begin(), roster.bodies.end(), [](const auto& body) {
            return body.parentId.empty();
        });
    };
    const auto moonCount = [](const PostSolarSystemRoster& roster) {
        return std::count_if(roster.bodies.begin(), roster.bodies.end(), [](const auto& body) {
            return body.kind == PostSolarBodyKind::Moon;
        });
    };
    require(primaryCount(aaru) >= 4 && primaryCount(aaru) <= 6, "Aaru should generate 4-6 primary bodies");
    require(moonCount(aaru) >= 3 && moonCount(aaru) <= 7, "Aaru should generate 3-7 moons");
    require(primaryPostSolarBody(aaru) != nullptr, "Aaru should expose a mineable primary body");

    const PostSolarSystemRoster khepri = generatePostSolarSystemRoster(content::postSolarSystem::khepriPrime, seed);
    requireSameRoster(khepri, generatePostSolarSystemRoster(content::postSolarSystem::khepriPrime, seed));
    require(primaryCount(khepri) >= 3 && primaryCount(khepri) <= 5, "Khepri should generate 3-5 primary bodies");
    require(moonCount(khepri) >= 2 && moonCount(khepri) <= 8, "Khepri should generate 2-8 moons");

    const PostSolarSystemRoster rift = generatePostSolarSystemRoster(content::postSolarSystem::riftBelt, seed);
    require(rift.bodies.size() >= 4U && rift.bodies.size() <= 8U, "Rift should generate 4-8 fragments");
    require(std::all_of(rift.bodies.begin(), rift.bodies.end(), [](const auto& body) {
        return body.kind == PostSolarBodyKind::MinorBody && body.mineable;
    }), "Rift fragments should be mineable minor bodies");

    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, seed);
    state.meta.postSolarSystemRosters.push_back(khepri);
    state.run.surfaceExpedition.postSolarSystemId = khepri.systemId;
    state.run.surfaceExpedition.bodyId = khepri.primaryBodyId;
    state.run.mining.postSolarSystemId = khepri.systemId;
    state.run.mining.bodyId = khepri.primaryBodyId;
    state.run.mining.surfaceGeologyId = khepri.bodies.front().surfaceGeologyId;
    state.run.mining.deepGeologyId = khepri.bodies.front().deepGeologyId;
    state.run.mining.geologySeed = khepri.bodies.front().seed;
    const std::optional<SaveData> restored = deserializeSaveData(serializeSaveData(captureSaveData(state)));
    require(restored.has_value(), "post-solar save should deserialize");
    require(restored->postSolarSystemRosters.size() == 1U, "post-solar roster should persist");
    require(restored->surfaceExpedition.bodyId == khepri.primaryBodyId, "selected body should persist");
    require(restored->mining.surfaceGeologyId == state.run.mining.surfaceGeologyId,
        "active geology should persist");
    require(restored->mining.geologySeed == state.run.mining.geologySeed,
        "geology seed should persist");
}

} // namespace

int main()
{
    proceduralScenarioTemplatesStayDormantUntilInstanced();
    launchControlsAreSeededCorrectableAndImproveByRank();
    launchThermalManagementIsPlayerDriven();
    launchCurriculumResolutionHasNoHiddenDamageOrBlueprintLeaks();
    launchAsteroidsAreDeterministicFairAndHullScaled();
    launchCurriculumEconomyGatesAndRoundTrips();
    launchCompetentPoliciesSurviveFiveThousandSeeds();
    launchSkillFailuresRemainVisibleAndNonRandom();
    launchFailureSummariesMatchTheActualLesson();
    sharedFlightInstrumentPresentationMatchesEachMode();
    activeFlightPanelsUseTheClusterAndCompactStatusRows();
    launchCurriculumFuelMathAndRange();
    emergencyRecruitmentPreventsDeadRosterSoftLock();
    emergencyRecruitmentOffersAnimalCandidateChoice();
    moduleOffersAreOneChoiceRefits();
    openingRefitTracksAreCuratedAndEntitled();
    fuelRefitsTeachMarsAndFundJupiter();
    refitRerollsSpendAndEscalate();
    specialShipComponentsRequireRecoveredMaterials();
    preMiningRefitOffersAvoidMaterialCosts();
    shipModuleProgressSurvivesDestroyedVehicles();
    deadCrewLosesTraining();
    crewUpgradeOffersInstallAndModifyCrewOps();
    hangarOpsStartCheapAndEscalate();
    medicalRestEscalationResetsAfterSurvivedMission();
    hangarOperationPreviewMatchesCoreMath();
    totaledShipCanAlwaysReachSalvageRepair();
    lowCreditRefitWindowIncludesAffordableOffer();
    researchPhasesUnlockOnlyAfterMarsArrival();
    arrivalOperationsUseMutuallyExclusiveCommitments();
    arrivalPresentationExplainsCommitmentAndResearchProgress();
    solarRouteLegsKeepCampaignProgressSeparateFromShipPosition();
    arrivalFlybyMinigameRewardsProgressionAndSlingshot();
    shipUpgradesAssistFlybyAndOrbitMinigames();
    orbitControlsFollowClockwiseProgradeDirection();
    orbitStartsCircularAndIsSolvableAcrossDestinationTiers();
    launchUpgradeRanksProvideExplicitOrbitAssists();
    activeFlybySaveResumesAtApproach();
    arrivalOrbitMinigameRewardsProgressionOnlyResearch();
    activeOrbitSaveResumesAtApproach();
    researchProjectsGenerateAndCompleteFromSharedRules();
    materialResearchUnlocksModuleFamilies();
    artifactInsightImprovesFutureResearch();
    researchFacilitiesImproveFutureResearch();
    artifactResearchIdentifiesRecoveredArtifacts();
    surfaceToolResearchImprovesExpeditions();
    animalCrewClassesModifySurfaceExpeditions();
    expeditionExperienceQueuesDistinctSelectableOffers();
    exhaustedRunUpgradePoolConsumesQueuedChoices();
    droneGraftOffersAreDistinctPerCompatibleSlot();
    postExtractionLevelUpDraftRestoresWithoutSurfaceRuntime();
    selectedSurfaceUpgradesModifyMiningAndSurfaceStats();
    surfaceUpgradesAndDronesModifyScanMiniGame();
    surfaceUpgradesAndDronesModifyPushDeeperMiniGame();
    surfaceDepthRatingsReplaceTemporaryEnvelopeBonuses();
    permanentSurfaceDepthRefitsUnlockAndPersist();
    surfaceDepthTutorialAndSafetyGatesAreHard();
    surfaceScanTimingWindowsTightenByMappedDepth();
    runUpgradesSurviveEmergencyRecall();
    runUpgradeLifetimeFollowsTheTransport();
    miningDepletionAtShipGracefullyEndsRun();
    droneBayUnlocksSlotsLoadoutsAndMiningEffects();
    scenarioUiActionsDoNotAwardExpeditionExperience();
    firstMiningContractBuildsAndCelebratesProspector();
    explicitSolarCampaignObjectivesGateRewardsAndRoutes();
    campaignStateRoundTripsAtCurrentVersion();
    scenarioAndCocoonStateRoundTrips();
    surfaceSiteProfilesChangeExpeditionRules();
    surfaceHazardsCreateEnvironmentalSetbacks();
    surfaceEventsCreateSmallRunVariation();
    enemyContactStartsBeyondSolarSystemAndCanBeMitigated();
    surfaceExpeditionBanksMaterialsAndDefersEnemies();
    surfaceExpeditionRoundTripsThroughSave();
    transferFuelPersistsAndBecomesRigFuel();
    surfaceMiningUsesRigFuelAndRunsOnce();
    physicalMiningArtifactsAreSingleAndDeliveryGated();
    miningArtifactTetherAndDestructionRules();
    miningArtifactRewardsResolveOnExtraction();
    miningArtifactSaveRoundTrips();
    surfaceScanMiniGameBanksSurveyPayload();
    surfacePushMiniGameBanksDepthRoute();
    surfacePushLaterCollapseLosesUncommittedRoute();
    scannedArtifactBecomesARecoverableMiningTarget();
    poiGuidancePrioritizesSafetyAndTracksRecoverableArtifacts();
    surfaceScanForecastsPushDepthLayers();
    thermalSurfacePushStillMapsResourceLayers();
    surfaceScanBustAndAbortDiscardForecasts();
    surfaceMissionLogIsBounded();
    miningTerrainIsDeterministicAndDepthScales();
    hostileMiningTerrainGeneratesPreDugEnemyStructures();
    actBasedMiningEnemyProgressionIsEnforced();
    hostileMiningRunSpawnsEnemiesAndPassiveDefenses();
    miningEnemySpawnersAreGenericCappedAndDestructible();
    rangedMiningEnemiesShootAndCombatVisualsExpire();
    attackDroneCombatCanCritAndEnemyCooldownPersists();
    miningMiniDronesFollowIndependentRolePositions();
    hazardDroneTreatsAffinityLadderAndBatches();
    hazardDronesCrossSolidTerrainButNeverTargetHiddenCells();
    hazardDroneFinishesCommittedTreatmentBeforeFollowingMovedPlayer();
    duplicateHazardDronesCoordinatePriorityAndExactAssistance();
    hazardDroneAssignmentsNormalizeAcrossSaveRoundTrips();
    miningHazardAffinitiesApplyOnlyOnDrillContact();
    miningAndSurveyDroneAgentsPerformWorldActions();
    prospectorSafeRecallRecoversFromBlockedReturnPath();
    surveyDroneRunsAnchoredPriorityScanCycles();
    surveyDronesMaintainCoordinatedSearchLanes();
    resourceDroneRunsTimedMaterialShuttles();
    resourceDronesCollectInMovingFormation();
    miningDroneRunsTimedCapacityShuttles();
    defenseDronesCoordinateChargedShieldArcs();
    attackAndDefenseDroneAgentsOwnCombatBehavior();
    elementalMiningCombatAppliesAffinityAndAreaDefenses();
    themedAffinityMechanicsStayRestrictedToElementalsAndTrueElites();
    mammalBossChambersGrantAdvancedRewards();
    enemyMovementTypesHaveDistinctBehavior();
    miningDrillBreaksCellsAndMarksChunks();
    miningUsesRigFuelReserve();
    rigFuelLoopRanksControlOperatingCadence();
    miningDrillFootprintCapsWearToWorstContact();
    miningMovementGrindsSoftTerrainAndRecoilsFromHardTerrain();
    miningDrillTargetsFirstSolidCellOnRay();
    miningCompletionFeedsSurfacePayload();
    miningBrokenDrillBitDisablesDrillingOnly();
    miningShipRepairsUseBankedMaterialsProportionally();
    miningShipBankingLeaveAndEmergencyRecallRules();
    miningSwarmNestPreviewAndPersistence();
    miningOxygenDrainsRigHealthBeforeEmergencyEjection();
    miningLoadBurdenAndUpgradeRelief();
    miningRefitModulesImproveDrillProfileIncrementally();
    miningEvaFixedDrillProfileIgnoresRigUpgrades();
    activeMiningRoundTripsThroughSave();
    operatorRigTetherRoundTripsThroughSave();
    miningEvaAndSwarmStateRoundTrips();
    miningDepthLayersAreBidirectionalAndPersistent();
    miningDeploysDeepAndGeneratesTheRouteBackToSurface();
    miningDestinationGravityAndEvaMotionUsePhysicalProfiles();
    miningEvaTogglePassagesAndExtractionRulesAreSafe();
    miningEvaLooseChunksResourceRecoveryAndSidearmAreDeterministic();
    miningSwarmAnchorTransfersPreserveRuntimeState();
    miningEmergencyEvaFailureAndRecoveryRulesHold();
    miningEvaAuditRegressionGuardsHold();
    roughSurfaceExtractionReportsLostPayload();
    roughMiningOreCreditsTheSurvivingContractPayload();
    saveRoundTripPreservesProgress();
    progressedSavesSkipTheFirstLaunchIntroduction();
    saveSchemaConstantsMatchSerializedFields();
    legacyRecordsTrackAchievementStats();
    flightProgressHelpersShareTravelAndReturnMath();
    arkDiscoveryAndScriptedJumpProgression();
    numberedChaptersAdvanceMonotonically();
    hostileNavigationSelectsShuttleSortie();
    legacyDeepSpaceFrontierMigratesToArkFlow();
    arkCampaignStateRoundTripsThroughSave();
    uiActionsUseStableSchemaIds();
    panelLayoutModeIsPortablePresentationData();
    structuredPanelPresentationSelectsFirstWaveTemplates();
    structuredPanelPresentationCarriesTypedModalPolicy();
    contentIdsResolveAgainstDefaultCatalog();
    outerPlanetCampaignSequenceIsExplicitAndUnskippable();
    storyBriefingsTakeOverAndPersist();
    miningThermalCutoffAndGuidanceAreExplicit();
    marsMiningPressureFitsOxygenWindow();
    secondaryMiningStateRoundTrips();
    secondaryPulseUsesUnifiedCooldownAndStrongestHit();
    treasurePingMarksRareFirstAndSkipsExcludedMaterials();
    secondaryHybridTuningCoversAllRanksAndCaps();
    postSolarBodiesAndGeologiesAreDeterministicAndPersistent();

    std::cout << "rocket_core_tests passed\n";
    return 0;
}
