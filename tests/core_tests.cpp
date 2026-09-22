#include "core/RigGeometry.h"
#include "core/Content.h"
#include "core/ContentIds.h"
#include "core/CrewPresentation.h"
#include "core/FlightProgress.h"
#include "core/FlightSystem.h"
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
#include "core/RigFuelSystem.h"
#include "core/PostSolarSystem.h"
#include "core/OutcomePresentation.h"
#include "core/PanelChromePresentation.h"
#include "core/ProgramPresentation.h"
#include "core/RefitPresentation.h"
#include "core/ResearchPresentation.h"
#include "core/ResearchSystem.h"
#include "core/ScenarioSystem.h"
#include "core/SolarProgression.h"
#include "core/SystemContent.h"
#include "core/SaveData.h"
#include "core/SaveSchema.h"
#include "core/ShipPresentation.h"
#include "core/Tuning.h"
#include "core/GameUi.h"

#ifdef _MSC_VER
#include <crtdbg.h>
#endif
#include "game/GamePanel.h"
#include "render/RenderSnapshot.h"
#include "render/MiningFogPresentation.h"

#include <cassert>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace rocket;

void beltWarningFollowsTravelDirection()
{
    // Reproduce both inbound and outbound crossings, including the old buffer
    // that incorrectly brought "belt ahead" back after leaving the ring.
    for (double angle : {0.0, 0.7, 2.4, 4.1}) {
        const auto point = [=](double radius) {
            return SystemVector{radius * std::cos(angle), radius * std::sin(angle)};
        };
        for (double speed : {.01, 1.0, 5.0}) {
            assert(approachingSolarAsteroidBelt(point(solarBeltInnerRadius-1), point(speed)));
            assert(!approachingSolarAsteroidBelt(point(solarBeltInnerRadius-1), point(-speed)));
            assert(approachingSolarAsteroidBelt(point(solarBeltOuterRadius+1), point(-speed)));
            assert(!approachingSolarAsteroidBelt(point(solarBeltOuterRadius+1), point(speed)));
            assert(approachingSolarAsteroidBelt(point(25), point(speed)));
            assert(approachingSolarAsteroidBelt(point(25), point(-speed)));
        }
    }
    assert(approachingSolarAsteroidBelt({19,0},{1,0}));
    assert(approachingSolarAsteroidBelt({33,0},{-1,0}));
    assert(approachingSolarAsteroidBelt({25,0},{0,0}));
    assert(!approachingSolarAsteroidBelt({22,0},{0,0}));
    assert(!approachingSolarAsteroidBelt({29,0},{0,0}));
    assert(!approachingSolarAsteroidBelt({29,0},{0,1}));
    assert(!approachingSolarAsteroidBelt({29,0},{-.01,1}));
}

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
    state.run.planetaryExpedition.miningSitePrepared = true;
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
    mining.rigOxygen.current = 1000.0;
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
        state.run.planetaryExpedition.active,
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
    FlightRunState& flight,
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

        FlightInput input;
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
    FlightRunState cooling = beginLaunchFlight(launch, mars);
    cooling.heat = 0.90;
    const double before = cooling.heat;
    updateLaunchFlight(cooling, launch, mars, {0.0, 0.0, true}, 0.08);
    require(cooling.heat < before &&
            nearlyEqual(before - cooling.heat, 0.10 * 0.08, 0.000001),
        "base engines-off cooling must be deterministic and never overpowered");

    FlightRunState coast = beginLaunchFlight(launch, mars);
    const double fuelBeforeCoast = coast.fuelRemaining;
    const double positionBeforeCoast = coast.positionX;
    for (int index = 0; index < 50; ++index) {
        updateLaunchFlight(coast, launch, mars, {0.0, 0.0, true}, 0.04);
    }
    require(nearlyEqual(coast.fuelRemaining, fuelBeforeCoast),
        "physical coasting must consume no fuel");
    require(!nearlyEqual(coast.positionX, positionBeforeCoast),
        "physical coasting must preserve momentum instead of stopping on a rail");

    FlightRunState reckless = beginLaunchFlight(launch, mars);
    reckless.heat = 1.0;
    LaunchFlightStep recklessResult;
    for (int index = 0; index < 300 && reckless.active; ++index) {
        recklessResult = updateLaunchFlight(reckless, launch, mars, {0.0, 1.0, false}, 0.04);
    }
    require(recklessResult.failed &&
            recklessResult.failureCause == LaunchFailureCause::ThermalRunaway,
        "sustained full thrust through temperature warnings must retain an explicit Thermal Runaway outcome");
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
    FlightRunState struck = beginLaunchFlight(collision, jupiter);
    struck.positionX = -1.76;
    struck.positionY = 0.0;
    struck.velocityX = 1.50;
    struck.velocityY = 0.0;
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
    FlightRunState breached = beginLaunchFlight(beltQualification, jupiter);
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
        FlightRunState integrity = beginLaunchFlight(ranked, jupiter);
        require(nearlyEqual(
                    integrity.hullMaximum,
                    100.0 + static_cast<double>(rank) * 25.0),
            "Hull ranks must expose 100, 125, 150, and 175 HP");
    }

    FlightRunState noHit = beginLaunchFlight(first, jupiter);
    for (int index = 0; index < 20 && noHit.active; ++index) {
        updateLaunchFlight(noHit, first, jupiter, {0.0, 0.0, true}, 0.04);
    }
    require(noHit.hullRemaining == noHit.hullMaximum,
        "physical flight must never apply hidden hull damage when no asteroid is contacted");
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
    state.meta.crewLossPending = true;
    state.meta.pendingReplacementArchetypeId = "fox_navigator";
    require(acceptCrewReplacement(state, catalog), "free deterministic replacement should prevent a dead-roster soft lock");
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

    state.meta.crewLossPending = true;
    state.meta.pendingReplacementArchetypeId = "beaver_engineer";
    require(acceptCrewReplacement(state, catalog), "authored replacement should be accepted for free");
    const Astronaut* recruited = activeAstronaut(state);
    require(recruited != nullptr && recruited->archetypeId == "beaver_engineer",
        "replacement should preserve the authored race and class archetype");
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
    // A fully owned free catalog may now produce no offer: retired crew chore
    // upgrades are deliberately not used as filler. Any offer that remains
    // must still respect the pre-mining no-material rule above.
    (void)sawOffer;
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
        state.meta.launchLessons.stage = LaunchTrainingStage::Complete;

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
    const SurfaceExpeditionPresentation presentation = planetaryExpeditionPresentation(capybara);
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
    require(award.levelsGained == 3 && state.run.expedition.progression.expeditionLevel == 4,
        "75 expedition XP should cross the 10, 16, and 25 thresholds");
    require(std::abs(state.run.expedition.progression.expeditionExperience - 24.0) < 0.001 &&
            state.run.expedition.progression.pendingRunUpgradeChoices == 3,
        "75 expedition XP should leave 24 XP and queue three mandatory choices");
    Random rng(643);
    require(generateRunUpgradeOffers(state, catalog, rng), "a queued level should generate a persisted offer");
    const PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    require(state.run.expedition.progression.runUpgradeOfferPending && state.run.expedition.progression.runUpgradeOfferCount == 3,
        "a populated run-upgrade pool should expose three cards");
    require(state.run.expedition.progression.runUpgradeOffers[0].definitionId == content::surfaceUpgrade::highTorqueMotor &&
            state.run.expedition.progression.runUpgradeOffers[1].definitionId == content::surfaceUpgrade::wideDrillHead,
        "the first draft must include cutting power and a width upgrade");
    for (int left = 0; left < state.run.expedition.progression.runUpgradeOfferCount; ++left) {
        for (int right = left + 1; right < state.run.expedition.progression.runUpgradeOfferCount; ++right) {
            const RunUpgradeOffer& a = state.run.expedition.progression.runUpgradeOffers[static_cast<std::size_t>(left)];
            const RunUpgradeOffer& b = state.run.expedition.progression.runUpgradeOffers[static_cast<std::size_t>(right)];
            require(a.kind != b.kind || a.definitionId != b.definitionId || a.slotIndex != b.slotIndex,
                "one level-up board must not repeat an identical offer target");
        }
    }
    const Screen originalScreen = state.screen;
    require(chooseRunUpgrade(state, catalog, 0), "selecting a valid persisted offer should apply it");
    require(state.run.expedition.progression.pendingRunUpgradeChoices == 2 &&
            !state.run.expedition.progression.runUpgradeOfferPending,
        "selection should consume exactly one choice and clear only the current board");
    require(state.screen == originalScreen,
        "core offer selection must not mutate screens or auto-open the next board");
    require(generateRunUpgradeOffers(state, catalog, rng) &&
            std::any_of(state.run.expedition.progression.runUpgradeOffers.begin(),
                state.run.expedition.progression.runUpgradeOffers.begin() + state.run.expedition.progression.runUpgradeOfferCount,
                [](const RunUpgradeOffer& offer) { return offer.definitionId == content::surfaceUpgrade::sideCutters; }),
        "the other width upgrade must appear by the third draft");
}

void sharedFlightInstrumentPresentationMatchesEachMode()
{
    for (double velocity : {-0.199, -0.1, -0.049, -0.0, 0.0, 0.049, 0.1, 0.199})
        require(landingVerticalSpeedText(velocity) == "0.0 m/s",
            "vertical telemetry must suppress near-zero flutter and negative zero");
    require(landingVerticalSpeedText(-0.2) == "-0.2 m/s" &&
            landingVerticalSpeedText(0.2) == "0.2 m/s" &&
            landingVerticalSpeedText(-12.4) == "-12.4 m/s",
        "vertical telemetry must retain meaningful speed outside the display dead zone");
    PreparedLaunch launch;
    launch.config.burnGoalMultiplier = 2.0;
    launch.manualControlsEnabled = true;
    launch.heatEnabled = true;
    FlightRunState flight;
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


}

void exhaustedRunUpgradePoolConsumesQueuedChoices()
{
    ContentCatalog catalog = createDefaultContent();
    catalog.surfaceUpgrades.clear();
    catalog.miniDrones.clear();
    catalog.droneModules.clear();
    catalog.droneSynergies.clear();

    GameState state = createNewGame(catalog, 6421);
    state.run.expedition.progression.pendingRunUpgradeChoices = 2;
    Random rng(6422);
    require(!generateRunUpgradeOffers(state, catalog, rng) &&
            state.run.expedition.progression.pendingRunUpgradeChoices == 1 &&
            !state.run.expedition.progression.runUpgradeOfferPending,
        "an exhausted finite pool should consume exactly one mandatory choice without opening an empty board");
    require(!generateRunUpgradeOffers(state, catalog, rng) &&
            state.run.expedition.progression.pendingRunUpgradeChoices == 0 &&
            state.run.expedition.progression.runUpgradeOfferCount == 0,
        "queued choices should drain deterministically when every eligible upgrade is installed");
}

void combatRunUpgradesWaitForFirstEnemyEncounter()
{
    ContentCatalog catalog = createDefaultContent();
    catalog.surfaceUpgrades = {
        *catalog.findSurfaceUpgrade(content::surfaceUpgrade::resonantDischarge),
        *catalog.findSurfaceUpgrade(content::surfaceUpgrade::coolantMist)};
    catalog.miniDrones.clear();
    catalog.droneModules.clear();
    catalog.droneSynergies.clear();

    GameState state = createNewGame(catalog, 6423);
    state.run.expedition.progression.pendingRunUpgradeChoices = 1;
    Random rng(6424);
    require(generateRunUpgradeOffers(state, catalog, rng), "non-combat upgrades should remain available before enemy contact");
    require(state.run.expedition.progression.runUpgradeOfferCount == 1 &&
            state.run.expedition.progression.runUpgradeOffers[0].definitionId == content::surfaceUpgrade::coolantMist,
        "Resonant Discharge should stay out of the early upgrade pool before an enemy is encountered");

    state.run.expedition.progression.runUpgradeOffers = {{
        {RunUpgradeKind::Rig, content::surfaceUpgrade::resonantDischarge, 1, -1}}};
    state.run.expedition.progression.runUpgradeOfferCount = 1;
    state.run.expedition.progression.runUpgradeOfferPending = true;
    require(generateRunUpgradeOffers(state, catalog, rng) &&
            state.run.expedition.progression.runUpgradeOffers[0].definitionId == content::surfaceUpgrade::coolantMist,
        "a saved pre-contact combat draft should be rerolled without consuming its earned pick");

    state.meta.hasEncounteredEnemy = true;
    state.run.expedition.progression.runUpgradeOffers = {};
    state.run.expedition.progression.runUpgradeOfferCount = 0;
    state.run.expedition.progression.runUpgradeOfferPending = false;
    require(generateRunUpgradeOffers(state, catalog, rng) &&
            state.run.expedition.progression.runUpgradeOfferCount == 2,
        "the combat upgrade should enter the pool after the first hostile encounter");
    require(std::any_of(
                state.run.expedition.progression.runUpgradeOffers.begin(),
                state.run.expedition.progression.runUpgradeOffers.begin() + state.run.expedition.progression.runUpgradeOfferCount,
                [](const RunUpgradeOffer& offer) {
                    return offer.definitionId == content::surfaceUpgrade::resonantDischarge;
                }),
        "Resonant Discharge should become eligible immediately after the first hostile encounter");
}

void droneGraftOffersAreDistinctPerCompatibleSlot()
{
    ContentCatalog catalog = createDefaultContent();
    catalog.surfaceUpgrades.clear();
    catalog.droneSynergies.clear();
    GameState state = createNewGame(catalog, 6431);
    state.screen = Screen::SurfaceUpgrade;
    state.meta.hasEncounteredEnemy = true;
    state.meta.unlockKeys = {content::unlock::droneBay, content::unlock::perimeterDrones};
    state.meta.droneBaySlots = 2;
    state.meta.ownedDroneIds = {content::drone::miningDrone, content::drone::miningDrone};
    state.meta.equippedDroneIds = state.meta.ownedDroneIds;
    state.run.expedition.progression.runDroneRanks = {{content::drone::miningDrone, 3}};
    state.run.expedition.progression.pendingRunUpgradeChoices = 1;
    Random rng(6432);
    require(generateRunUpgradeOffers(state, catalog, rng), "compatible empty drone slots should create graft candidates");
    const PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    require(state.run.expedition.progression.runUpgradeOfferCount == 3,
        "four slot-specific Mining graft candidates should produce a three-card board");
    bool sameGraftDifferentSlots = false;
    for (int left = 0; left < state.run.expedition.progression.runUpgradeOfferCount; ++left) {
        const RunUpgradeOffer& a = state.run.expedition.progression.runUpgradeOffers[static_cast<std::size_t>(left)];
        require(a.kind == RunUpgradeKind::DroneGraft && (a.slotIndex == 0 || a.slotIndex == 1),
            "the exhausted filtered pool should contain only pre-bound compatible grafts");
        for (int right = left + 1; right < state.run.expedition.progression.runUpgradeOfferCount; ++right) {
            const RunUpgradeOffer& b = state.run.expedition.progression.runUpgradeOffers[static_cast<std::size_t>(right)];
            sameGraftDifferentSlots = sameGraftDifferentSlots ||
                (a.definitionId == b.definitionId && a.slotIndex != b.slotIndex);
        }
    }
    require(sameGraftDifferentSlots,
        "duplicate Drone types must allow the same graft definition to target separate slots");
    const RunUpgradeOffer chosen = state.run.expedition.progression.runUpgradeOffers[0];
    require(chooseRunUpgrade(state, catalog, 0) &&
            state.run.expedition.progression.droneModuleAssignments.size() == 1 &&
            state.run.expedition.progression.droneModuleAssignments.front().equippedFrame == chosen.slotIndex,
        "choosing a graft should install it directly on its offered slot without assignment UI");
}

void postExtractionLevelUpDraftRestoresWithoutSurfaceRuntime()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 6433);
    PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    expedition.active = false;
    state.run.expedition.progression.expeditionLevel = 4;
    state.run.expedition.progression.expeditionExperience = 24.0;
    state.run.expedition.progression.pendingRunUpgradeChoices = 1;
    state.run.expedition.progression.runUpgradeOffers[0] = {
        RunUpgradeKind::Rig,
        content::surfaceUpgrade::coolantMist,
        1,
        -1};
    state.run.expedition.progression.runUpgradeOfferCount = 1;
    state.run.expedition.progression.runUpgradeOfferPending = true;
    state.run.expedition.progression.runUpgradeReturnScreen = Screen::Hangar;
    state.screen = Screen::SurfaceUpgrade;

    const std::optional<SaveData> save = deserializeSaveData(
        serializeSaveData(captureSaveData(state)));
    require(save.has_value(), "a post-extraction Level Up draft should serialize as a valid v14 save");

    GameState restored = createNewGame(catalog, 1);
    restoreSaveData(restored, catalog, *save);
    require(restored.screen == Screen::SurfaceUpgrade,
        "an open post-extraction Level Up draft should restore even after Surface runtime ends");
    require(!restored.run.planetaryExpedition.active &&
            restored.run.expedition.progression.runUpgradeOfferPending &&
            restored.run.expedition.progression.runUpgradeOfferCount == 1 &&
            restored.run.expedition.progression.runUpgradeReturnScreen == Screen::Hangar,
        "post-extraction draft offers and their eventual return screen should round trip intact");
}

void selectedSurfaceUpgradesModifyMiningAndSurfaceStats()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState baseline = createNewGame(catalog, 644);
    baseline.run.destinationIndex = 2;
    startSurfaceExpedition(baseline, catalog);

    GameState upgraded = baseline;
    upgraded.run.expedition.progression.runRigUpgradeRanks = {
        {content::surfaceUpgrade::coolantMist, 1},
        {content::surfaceUpgrade::highTorqueMotor, 1},
        {content::surfaceUpgrade::wideDrillHead, 1},
        {content::surfaceUpgrade::sideCutters, 1},
        {content::surfaceUpgrade::hardRockTeeth, 1},
        {content::surfaceUpgrade::widebandPulse, 1},
        {content::surfaceUpgrade::cargoSkids, 1},
        {content::surfaceUpgrade::shockMounts, 1},
        {content::surfaceUpgrade::oreScentArray, 1}
    };

    const MiningDrillStats baselineStats = miningDrillStats(baseline, catalog);
    const MiningDrillStats upgradedStats = miningDrillStats(upgraded, catalog);
    require(nearlyEqual(tuning::mining::baseDrillPower, 5.04) &&
            nearlyEqual(miningOperatorDrillStats().power,
                tuning::mining::operatorBaseDrillPower * tuning::mining::operatorDrillPowerScale),
        "the Rig starter boost must leave the EVA drill at its original cutting power");
    require(upgradedStats.heatRiseScale < baselineStats.heatRiseScale, "thermal field upgrades should reduce mining heat rise");
    require(nearlyEqual(baselineStats.power, tuning::mining::baseDrillPower) &&
            nearlyEqual(upgradedStats.power - baselineStats.power, tuning::mining::baseDrillPower * 0.25),
        "starter and High-Torque cutting power must use the authored additive values");
    require(nearlyEqual(upgradedStats.headWidthScale, 1.25) && nearlyEqual(upgradedStats.sideCutterReach, 0.5) &&
            nearlyEqual(upgradedStats.hardRockPower, 0.25),
        "width, side reach, and hard-rock upgrades must resolve independently");
    require(upgradedStats.scannerRadius > baselineStats.scannerRadius, "scanner field upgrades should widen pulse reveal radius");
    require(upgradedStats.integrityRelief > baselineStats.integrityRelief, "shock mounts should improve mining durability");
    require(upgradedStats.hardRockBounceRelief > baselineStats.hardRockBounceRelief, "shock mounts should reduce hard-rock recoil");
    require(upgradedStats.oreYieldChance > baselineStats.oreYieldChance, "ore field upgrades should improve yield odds");
    require(miningRigCargoCapacityMass(upgraded, catalog) == 26,
        "Cargo Skids I must increase real Rig capacity from 24 to 26");
    require(surfaceToolEffects(upgraded.meta).hazardRelief >= surfaceToolEffects(baseline.meta).hazardRelief, "cargo field upgrades should reduce Push Deeper hazard risk");

    const SurfaceExpeditionPresentation presentation = planetaryExpeditionPresentation(upgraded, catalog);
    require(!presentation.selectedUpgradeNames.empty(), "surface presentation should expose selected field upgrades");
    require(!presentation.details.empty(), "surface details should list field upgrades");
}

void runUpgradesSurviveEmergencyRecall()
{
    const ContentCatalog catalog = createDefaultContent();

    GameState brokenBit = createNewGame(catalog, 648);
    brokenBit.run.destinationIndex = 2;
    startSurfaceExpedition(brokenBit, catalog);
    prepareMiningSiteForTest(brokenBit);
    brokenBit.run.expedition.progression.runRigUpgradeRanks = {{content::surfaceUpgrade::shockMounts, 1}};
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
    recalled.run.expedition.progression.runRigUpgradeRanks = {
        {content::surfaceUpgrade::shockMounts, 1},
        {content::surfaceUpgrade::oreHopper, 1}
    };
    recalled.run.expedition.progression.expeditionLevel = 3;
    recalled.run.expedition.progression.expeditionExperience = 7.0;
    recalled.run.expedition.progression.runDroneRanks = {{content::drone::miningDrone, 2}};
    recalled.run.expedition.progression.droneModuleAssignments = {
        {0, content::drone::miningDrone, DroneModuleKind::CombatDrill}};
    recalled.run.expedition.progression.selectedSynergyIds = {"long_haul_rig"};
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
    require(recalled.run.expedition.progression.expeditionLevel == 3 &&
            std::abs(recalled.run.expedition.progression.expeditionExperience - 7.0) < 0.001 &&
            expeditionDroneRank(recalled, content::drone::miningDrone) == 2 &&
            recalled.run.expedition.progression.droneModuleAssignments.size() == 1 &&
            recalled.run.expedition.progression.selectedSynergyIds == std::vector<std::string>{"long_haul_rig"},
        "emergency recall should preserve XP, temporary Drone ranks, grafts, and selected synergies together");
}

void runUpgradeLifetimeFollowsTheTransport()
{
    const ContentCatalog catalog = createDefaultContent();
    auto seedBuild = [](GameState& state) {
        PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
        state.run.expedition.progression.expeditionLevel = 4;
        state.run.expedition.progression.expeditionExperience = 11.0;
        state.run.expedition.progression.pendingRunUpgradeChoices = 1;
        state.run.expedition.progression.runRigUpgradeRanks = {{content::surfaceUpgrade::widebandPulse, 2}};
        state.run.expedition.progression.runDroneRanks = {{content::drone::surveyDrone, 3}};
        state.run.expedition.progression.droneModuleAssignments = {
            {0, content::drone::surveyDrone, DroneModuleKind::PulseStrike}};
        state.run.expedition.progression.selectedSynergyIds = {"pathfinder_loop"};
    };
    auto requireBuild = [](const GameState& state, std::string_view transition) {
        const PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
        require(state.run.expedition.progression.expeditionLevel == 4 &&
                std::abs(state.run.expedition.progression.expeditionExperience - 11.0) < 0.001 &&
                state.run.expedition.progression.pendingRunUpgradeChoices == 1 &&
                runRigUpgradeRank(state, content::surfaceUpgrade::widebandPulse) == 2 &&
                expeditionDroneRank(state, content::drone::surveyDrone) == 3 &&
                state.run.expedition.progression.droneModuleAssignments.size() == 1 &&
                state.run.expedition.progression.selectedSynergyIds == std::vector<std::string>{"pathfinder_loop"},
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
    require(state.run.expedition.progression.expeditionLevel == 1 &&
            state.run.expedition.progression.expeditionExperience == 0.0 &&
            state.run.expedition.progression.pendingRunUpgradeChoices == 0 &&
            state.run.expedition.progression.runRigUpgradeRanks.empty() &&
            state.run.expedition.progression.runDroneRanks.empty() &&
            state.run.expedition.progression.droneModuleAssignments.empty() &&
            state.run.expedition.progression.selectedSynergyIds.empty(),
        "New Expedition should reset XP and every temporary upgrade family atomically");

    GameState destroyed = createNewGame(catalog, 6492);
    seedBuild(destroyed);
    LaunchOutcome loss;
    loss.type = LaunchResultType::Destroyed;
    loss.destinationId = content::destination::earthOrbit;
    applyLaunchOutcome(destroyed, catalog, loss);
    require(!destroyed.run.active &&
            destroyed.run.expedition.progression.expeditionLevel == 1 &&
            destroyed.run.expedition.progression.runRigUpgradeRanks.empty() &&
            destroyed.run.expedition.progression.runDroneRanks.empty() &&
            destroyed.run.expedition.progression.droneModuleAssignments.empty() &&
            destroyed.run.expedition.progression.selectedSynergyIds.empty(),
        "Transport destruction should clear XP progression and every temporary upgrade family");
}

void miningShipServiceRestoresOxygenWithoutEndingRun()
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
    oxygen.run.mining.rigOxygen.current = 0.01;
    updateMiningRun(oxygen, catalog, 0.08);
    require(oxygen.screen == Screen::Mining && oxygen.run.mining.active,
        "ship oxygen service should keep an empty rig's mining run active");
    require(std::abs(oxygen.run.mining.rigOxygen.current - miningActiveOxygenCapacity(oxygen, catalog)) < 0.000001,
        "ship oxygen service should instantly restore the full rig reserve without cargo");
    require(!oxygen.run.mining.failurePending,
        "ship oxygen service should not start failure recall");

    GameState fuel;
    startParkedAtShip(fuel, 651);
    fuel.run.mining.rigFuel.current = 0.0;
    fuel.run.mining.rigOxygen.current = 10.0;
    updateMiningRun(fuel, catalog, 0.08);
    require(fuel.screen == Screen::Mining && fuel.run.mining.active,
        "zero fuel should leave the continuous surface session active");
    require(!fuel.run.mining.failurePending,
        "zero fuel should not fail the astronaut or force a screen transition");
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
    require(state.run.expedition.progression.expeditionLevel == 2 &&
            std::abs(state.run.expedition.progression.expeditionExperience) < 0.001 &&
            state.run.expedition.progression.pendingRunUpgradeChoices == 1,
        "completing an authored material-delivery objective should award exactly 10 expedition XP");
    require(recordScenarioEvent(
                state, catalog,
                {ScenarioEventKind::ArtifactRecovered, {}, {}, "mars",
                 content::protectedObjective::marsSignalArtifact, 1, 0}),
        "the Mars slot fixture should recover the mission artifact before claiming its reward");
    require(performScenarioAction(
                state, catalog, content::scenario::marsBayExpansion, "artifact",
                ScenarioActionKind::ClaimReward).applied,
        "the completed Mars mission should explicitly fabricate Slot 2");
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
    state.run.expedition.progression.runDroneRanks = {{content::drone::miningDrone, 2}};
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
    restored.run.expedition.progression.selectedSynergyIds = {"killbox_screen"};
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

void saturnArtifactQueuesPhysicalUranusRoute()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 0x51A7);
    state.meta.launchLessons.stage = LaunchTrainingStage::Complete;
    state.meta.unlockKeys.push_back(content::unlock::routeSaturn);
    state.run.destinationIndex = 4;
    state.meta.furthestTier = 4;
    state.screen = Screen::Hangar;
    syncLaunchConfig(state, catalog);

    ScenarioObjectivePresentation objective = scenarioObjectiveForDestination(
        state,
        catalog,
        content::destination::saturn);
    require(objective.available && objective.scenarioId == content::scenario::saturnDeparture &&
            objective.current == 0 && objective.required == 1 &&
            objective.state == ScenarioStepState::Active,
        "Saturn should expose a visible 0/1 artifact-departure objective");

    state.run.planetaryExpedition.active = true;
    state.run.planetaryExpedition.destinationId = content::destination::saturn;
    state.run.planetaryExpedition.temporaryArtifacts.push_back(
        {"saturn_route_artifact", content::destination::saturn, false});
    objective = scenarioObjectiveForDestination(state, catalog, content::destination::saturn);
    require(objective.current == 0 && objective.state == ScenarioStepState::Active,
        "a temporary Saturn artifact must not count before safe extraction");

    const SurfaceActionOutcome extracted = extractSurfacePayload(state, catalog);
    require(extracted.applied && extracted.artifactFound && state.meta.artifacts.size() == 1,
        "safe Saturn extraction should move the artifact into permanent inventory");
    state.screen = Screen::Hangar;
    objective = scenarioObjectiveForDestination(state, catalog, content::destination::saturn);
    require(objective.current == 1 && objective.required == 1 &&
            objective.state == ScenarioStepState::ReadyToClaim &&
            objective.location == "SATURN DEPARTURE" &&
            objective.title == "Artifact Secured" &&
            objective.actionLabel == "Complete Mission",
        "the returned Saturn artifact should expose the explicit mission hand-in");

    Random claimPanelRng(0x51A8);
    const PreparedLaunch claimPanelLaunch = prepareLaunch(state, catalog, claimPanelRng);
    const std::string claimPanel = buildGamePanelHtml({
        state,
        catalog,
        claimPanelLaunch,
        claimPanelLaunch});
    require(claimPanel.find("SATURN DEPARTURE") != std::string::npos &&
            claimPanel.find("1/1") != std::string::npos &&
            claimPanel.find("Complete Mission") != std::string::npos &&
            claimPanel.find("Launch: Saturn") == std::string::npos,
        "the Saturn Hangar should prioritize the concise Uranus claim over another Saturn sortie");

    require(performScenarioAction(
                state,
                catalog,
                content::scenario::saturnDeparture,
                "artifact",
                ScenarioActionKind::ClaimReward).applied &&
            commitClaimedScenarioRoute(
                state,
                catalog,
                content::scenario::saturnDeparture,
                "artifact"),
        "claiming the Saturn artifact objective should queue its authored route");
    require(hasUnlock(state.meta, content::unlock::routeUranus) &&
            currentDestination(state, catalog).id == content::destination::saturn &&
            state.run.routeTransit.intent == RouteTransitIntent::Outbound &&
            state.run.routeTransit.originDestinationId == content::destination::saturn &&
            state.run.routeTransit.targetDestinationId == content::destination::uranus,
        "locking Uranus must leave the ship at Saturn and queue Saturn-to-Uranus");

    Random routeRng(0x51A9);
    const PreparedLaunch uranusLaunch = prepareLaunch(state, catalog, routeRng);
    const LaunchPanelPresentation launchPresentation = launchPanelPresentation(
        state,
        catalog,
        uranusLaunch,
        1.0,
        0.0,
        0.0,
        0.0,
        FlightActionState {});
    require(uranusLaunch.config.destinationId == content::destination::uranus &&
            uranusLaunch.config.routeTransit.originDestinationId == content::destination::saturn &&
            launchPresentation.sectionTitle.find("Saturn \xE2\x86\x92 Uranus") != std::string::npos &&
            launchPresentation.objectiveTitle == "REACH Uranus",
        "the queued flight should visibly depart Saturn for Uranus");

    GameState retry = state;
    LaunchOutcome returned;
    returned.type = LaunchResultType::SafeEject;
    returned.recoveryMethod = RecoveryMethod::ReturnHome;
    returned.frontierTransfer = true;
    returned.destinationId = content::destination::uranus;
    returned.routeTransit = retry.run.routeTransit;
    applyLaunchOutcome(retry, catalog, returned);
    require(currentDestination(retry, catalog).id == content::destination::saturn &&
            retry.run.routeTransit.targetDestinationId == content::destination::uranus,
        "an incomplete Uranus leg should remain retryable from Saturn");

    LaunchOutcome arrived;
    arrived.type = LaunchResultType::MissionComplete;
    arrived.recoveryMethod = RecoveryMethod::TransferArrival;
    arrived.frontierTransfer = true;
    arrived.destinationId = content::destination::uranus;
    arrived.routeTransit = state.run.routeTransit;
    applyLaunchOutcome(state, catalog, arrived);
    require(currentDestination(state, catalog).id == content::destination::uranus &&
            !state.run.routeTransit.active(),
        "only a successful Saturn-to-Uranus arrival should advance the frontier and clear the leg");

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
    require(state.run.expedition.progression.expeditionLevel == 1 &&
            state.run.expedition.progression.expeditionExperience == 0.0 &&
            state.run.expedition.progression.pendingRunUpgradeChoices == 0,
        "briefings, manual actions, equipment assignment, and other UI actions must not grant expedition XP");

}

void solarMissionAcceptanceUsesAuthoredActionsAndPreservesLiveLoadouts()
{
    const ContentCatalog catalog = createDefaultContent();
    std::string error;
    require(validateSolarMissionCatalog(catalog, &error),
        "every solar mission should have a valid authored acceptance binding");
    const SolarMissionDefinition* io = solarMissionForBody(catalog, "io");
    require(io != nullptr && io->acceptanceStepId == "commission" &&
            io->acceptanceAction == ScenarioActionKind::BeginActivity,
        "Io must bind its commissioning action, not a nonexistent briefing acknowledgement");

    for (int invalidBinding = 0; invalidBinding < 4; ++invalidBinding) {
        ContentCatalog invalid = catalog;
        auto& mission = *std::find_if(invalid.solarMissions.begin(), invalid.solarMissions.end(),
            [](const auto& item) { return item.bodyId == "io"; });
        if (invalidBinding == 0) mission.acceptanceStepId = "briefing";
        if (invalidBinding == 1) mission.acceptanceAction = ScenarioActionKind::AcknowledgeBriefing;
        if (invalidBinding == 2) mission.acceptanceStepId = "recovery";
        if (invalidBinding == 3) mission.acceptanceAction = ScenarioActionKind::None;
        require(!validateSolarMissionCatalog(invalid, &error),
            "catalog validation must reject missing, incompatible, activity-starting, or empty acceptance bindings");
        GameState unchanged = createNewGame(catalog, 0x10AC);
        unchanged.run.expedition.travelInitialized = true;
        unchanged.run.expedition.location.bodyId = "io";
        unchanged.meta.unlockKeys.push_back(content::unlock::routeJupiter);
        const std::string before = serializeSaveData(captureSaveData(unchanged));
        const auto result = acceptSolarMission(unchanged, invalid, mission);
        require(!result.accepted && !result.applied &&
                serializeSaveData(captureSaveData(unchanged)) == before,
            "an invalid runtime binding must fail without consuming or changing campaign state");
    }

    const auto atIo = [&]() {
        GameState state = createNewGame(catalog, 0x10AD);
        state.run.expedition.travelInitialized = true;
        state.run.expedition.location.bodyId = "io";
        state.meta.unlockKeys.push_back(content::unlock::routeJupiter);
        ensureScenarioInstances(state, catalog);
        return state;
    };
    GameState state = atIo();
    state.run.expedition.location.bodyId = "jupiter";
    require(!acceptSolarMission(state, catalog, *io).accepted &&
            !solarMissionAcceptanceForBody(state, catalog, "io").available,
        "entering Jupiter space or selecting Io as a target must not accept Io's mission");
    state.run.expedition.location.bodyId = "io";
    state.meta.unlockKeys.erase(std::remove(state.meta.unlockKeys.begin(), state.meta.unlockKeys.end(),
        content::unlock::routeJupiter), state.meta.unlockKeys.end());
    require(!acceptSolarMission(state, catalog, *io).accepted &&
            !solarMissionAcceptanceForBody(state, catalog, "io").available,
        "physical Io presence must not bypass its explicit route prerequisite");
    state.meta.unlockKeys.push_back(content::unlock::routeJupiter);
    const auto acceptance = solarMissionAcceptanceForBody(state, catalog, "io");
    require(acceptance.available && acceptance.stepId == "commission" &&
            acceptance.action == ScenarioActionKind::BeginActivity &&
            acceptance.actionLabel == "Commission Hazard Drone",
        "the persistent acceptance control should expose the authored commissioning label and action");
    const auto accepted = acceptSolarMission(state, catalog, *io);
    require(accepted.accepted && accepted.applied && solarMissionAccepted(state, catalog, *io) &&
            !solarMissionAcceptanceForBody(state, catalog, "io").available &&
            hasUnlock(state.meta, content::unlock::ioHazardDrone) &&
            ownedMiniDroneCount(state, content::drone::hazardDrone) == 1 &&
            equippedMiniDroneCount(state, content::drone::hazardDrone) == 1 &&
            scenarioStepState(state, catalog, io->scenarioId, "recovery") == ScenarioStepState::Active,
        "explicit acceptance should commission one assigned Hazard frame and activate recovery");
    const auto* instance = findScenarioInstance(state.meta, io->scenarioId);
    require(instance != nullptr && instance->awardedRewardIds.size() == 4,
        "commissioning should record each authored reward exactly once");
    const std::string acceptedSave = serializeSaveData(captureSaveData(state));
    const auto duplicate = acceptSolarMission(state, catalog, *io);
    require(duplicate.accepted && !duplicate.applied &&
            serializeSaveData(captureSaveData(state)) == acceptedSave,
        "repeated acceptance should succeed idempotently without duplicate rewards or save mutations");

    GameState full = atIo();
    full.meta.unlockKeys.push_back(content::unlock::droneBay);
    full.meta.droneBaySlots = 2;
    full.meta.ownedDroneIds = {content::drone::miningDrone, content::drone::miningDrone};
    full.meta.equippedDroneIds = full.meta.ownedDroneIds;
    const auto occupiedSlots = full.meta.equippedDroneIds;
    require(acceptSolarMission(full, catalog, *io).accepted && full.meta.droneBaySlots == 2 &&
            full.meta.equippedDroneIds == occupiedSlots &&
            ownedMiniDroneCount(full, content::drone::hazardDrone) == 1 &&
            equippedMiniDroneCount(full, content::drone::hazardDrone) == 0,
        "a full bay should retain every assigned frame and own the commissioned Hazard for an explicit swap");
    const auto reload = deserializeSaveData(serializeSaveData(captureSaveData(full)));
    require(reload.has_value() && reload->version == 23,
        "successful commissioning must round-trip through the unchanged v23 schema");
    GameState restored = createNewGame(catalog, 0x10AF);
    restoreSaveData(restored, catalog, *reload);
    require(solarMissionAccepted(restored, catalog, *io) &&
            hasUnlock(restored.meta, content::unlock::ioHazardDrone) &&
            ownedMiniDroneCount(restored, content::drone::hazardDrone) == 1 &&
            restored.meta.equippedDroneIds == occupiedSlots &&
            equippedMiniDroneCount(restored, content::drone::hazardDrone) == 0 &&
            !solarMissionAcceptanceForBody(restored, catalog, "io").available,
        "reload must preserve accepted commissioning and the owned-but-unassigned Hazard without replaying its prompt");
    const std::string restoredSave = serializeSaveData(captureSaveData(restored));
    const auto replay = acceptSolarMission(restored, catalog, *io);
    require(replay.accepted && !replay.applied &&
            serializeSaveData(captureSaveData(restored)) == restoredSave,
        "replaying acceptance after reload must leave reward ownership and the save unchanged");

    GameState mining = atIo();
    mining.screen = Screen::Mining;
    mining.run.mining.active = true;
    mining.run.planetaryExpedition.active = true;
    mining.run.mining.droneX = 14.5;
    mining.run.mining.droneY = 8.25;
    mining.run.mining.rigFuel.current = 4.75;
    mining.meta.unlockKeys.push_back(content::unlock::droneBay);
    mining.meta.droneBaySlots = 2;
    mining.meta.ownedDroneIds = {content::drone::miningDrone};
    mining.meta.equippedDroneIds = mining.meta.ownedDroneIds;
    MiningMiniDroneAgent hauling;
    hauling.role = MiniDroneRole::Mining;
    hauling.haulMaterials.common = 3;
    hauling.carriedLooseObjectId = 17;
    hauling.x = 8.5;
    mining.run.mining.miniDrones.push_back(hauling);
    mining.incomingMessages.acknowledgedMessages.push_back(io->briefingMessageId);
    mining.incomingMessages.acknowledgedOccurrences.push_back("campaign.solar.io.briefing");
    require(solarMissionAcceptanceForBody(mining, catalog, "io").available,
        "an acknowledged incoming message must not hide unresolved commissioning during active mining");
    const auto originalLoadout = mining.meta.equippedDroneIds;
    const auto miningAccepted = acceptSolarMission(mining, catalog, *io);
    require(miningAccepted.accepted && miningAccepted.applied &&
            ownedMiniDroneCount(mining, content::drone::hazardDrone) == 1 &&
            mining.meta.equippedDroneIds.size() == originalLoadout.size() + 1 &&
            equippedMiniDroneCount(mining, content::drone::hazardDrone) == 1 &&
            mining.run.mining.miniDrones.size() == 2 &&
            mining.run.mining.miniDrones.front().haulMaterials.common == 3 &&
            mining.run.mining.miniDrones.front().carriedLooseObjectId == 17 &&
            mining.run.mining.miniDrones.front().x == 8.5 &&
            mining.screen == Screen::Mining && mining.run.mining.active &&
            mining.run.mining.droneX == 14.5 && mining.run.mining.droneY == 8.25 &&
            mining.run.mining.rigFuel.current == 4.75,
        "commissioning must immediately append the Hazard without rebuilding, moving, or resetting the hauling team");
    require(mining.incomingMessages.acknowledgedMessages ==
                std::vector<std::string>{io->briefingMessageId} &&
            mining.incomingMessages.acknowledgedOccurrences ==
                std::vector<std::string>{"campaign.solar.io.briefing"},
        "same-schema recovery must not rewrite acknowledged message history");

    GameState normalReward = atIo();
    normalReward.run.mining.active = true;
    require(performScenarioAction(normalReward, catalog, io->scenarioId, io->acceptanceStepId,
                io->acceptanceAction).applied &&
            equippedMiniDroneCount(normalReward, content::drone::hazardDrone) == 1,
        "the default scenario reward policy must preserve unrelated existing automatic assignment behavior");

    for (const SolarMissionDefinition& mission : catalog.solarMissions) {
        if (mission.bodyId == "io") continue;
        GameState other = createNewGame(catalog, 0x10AE);
        other.run.expedition.travelInitialized = true;
        other.run.expedition.location.bodyId = mission.bodyId;
        if (!mission.prerequisiteUnlockKey.empty()) other.meta.unlockKeys.push_back(mission.prerequisiteUnlockKey);
        ensureScenarioInstances(other, catalog);
        require(mission.acceptanceStepId == "briefing" &&
                mission.acceptanceAction == ScenarioActionKind::AcknowledgeBriefing &&
                acceptSolarMission(other, catalog, mission).accepted &&
                solarMissionAccepted(other, catalog, mission),
            "ordinary solar missions should keep their explicit authored briefing acknowledgement");
    }
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

    state.run.planetaryExpedition.active = true;
    state.run.planetaryExpedition.destinationId =
        content::destination::jupiter;
    state.run.planetaryExpedition.pendingScenarioId =
        content::scenario::volcanicDescent;
    state.run.planetaryExpedition.pendingScenarioStepId = "recovery";
    state.run.planetaryExpedition.pendingMiningSiteDefinitionId =
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
        restored.run.planetaryExpedition.pendingScenarioId ==
                content::scenario::volcanicDescent &&
            restored.run.planetaryExpedition.pendingScenarioStepId == "recovery" &&
            restored.run.planetaryExpedition.pendingMiningSiteDefinitionId ==
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

void activeFlightRoundTripsThroughSave()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 9090);
    state.screen = Screen::Flight;
    state.run.flight.active = true;
    state.run.flight.physicalFlight = true;
    state.run.flight.originId = content::destination::earthOrbit;
    state.run.flight.destinationId = content::destination::moon;
    state.run.flight.phase = FlightPhase::Orbiting;
    state.run.flight.positionX = 0.31;
    state.run.flight.positionY = -0.27;
    state.run.flight.velocityX = 0.17;
    state.run.flight.velocityY = 0.22;
    state.run.flight.heading = 1.25;
    state.run.flight.fuelCapacity = 14.0;
    state.run.flight.fuelRemaining = 6.75;
    state.run.flight.hullMaximum = 120.0;
    state.run.flight.hullRemaining = 91.0;
    state.run.flight.orbit.stableAngularProgress = 2.4;
    state.run.flight.orbit.enteredInfluence = true;

    const std::string text = serializeSaveData(captureSaveData(state));
    const auto save = deserializeSaveData(text);
    require(save.has_value(), "active v18 Flight save should parse");
    require(text.find("surfaceSupply=") == std::string::npos &&
            text.find("surfaceRigFuel=") == std::string::npos &&
            text.find("surfaceCargo=") == std::string::npos &&
            text.find("surfaceDepthProspects=") == std::string::npos,
        "v18 output must not serialize retired Surface Ops payload fields");

    GameState restored = createNewGame(catalog, 1);
    restoreSaveData(restored, catalog, *save);
    require(restored.screen == Screen::Flight && restored.run.flight.active,
        "v18 should resume the authoritative active Flight state");
    require(restored.run.flight.phase == FlightPhase::Orbiting &&
            nearlyEqual(restored.run.flight.positionX, 0.31) &&
            nearlyEqual(restored.run.flight.velocityY, 0.22) &&
            nearlyEqual(restored.run.flight.fuelRemaining, 6.75) &&
            nearlyEqual(restored.run.flight.hullRemaining, 91.0) &&
            nearlyEqual(restored.run.flight.orbit.stableAngularProgress, 2.4),
        "physical trajectory, finite resources, hull, and orbit progress must round trip together");
}

void surfaceMiningUsesRigFuelAndRunsOnce()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 92928);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    const SurfaceActionOutcome started = startMiningRun(state, catalog);
    require(started.applied, "landing should start the physical surface session");
    require(nearlyEqual(state.run.mining.rigFuel.current, state.run.mining.rigFuel.capacity),
        "the entire expedition allotment should be visible in the rig tank");
    const double fuelBeforeIdle = state.run.mining.rigFuel.current;
    state.run.mining.enemies.clear();
    state.run.mining.swarm = {};
    setMiningMove(state, 0.0, 0.0);
    setMiningDrilling(state, false);
    updateMiningRun(state, catalog, 3.0);
    require(nearlyEqual(state.run.mining.rigFuel.current, fuelBeforeIdle),
        "idling should not consume rig fuel");
}

void physicalMiningArtifactsAreSingleAndDeliveryGated()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 92929);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    state.run.planetaryExpedition.prospectArtifacts = 3;

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
    state.run.mining.artifact.x = state.run.mining.returnZoneX +
        tuning::mining::returnZoneCenterOffsetX + tuning::mining::returnZoneRadiusCells - 0.05;
    state.run.mining.artifact.y = state.run.mining.returnZoneY - tuning::mining::returnZoneCenterHeightCells;
    state.run.mining.shipDepthZone = state.run.mining.depthZone;
    // The artifact can reach the ship bay before the rig reaches the pad.
    // Its ownership must still become Ship manifest immediately.
    state.run.mining.droneX = state.run.mining.returnZoneX + tuning::mining::returnZoneRadiusCells + 1.0;
    state.run.mining.droneY = state.run.mining.artifact.y;
    updateMiningRun(state, catalog, 0.08);
    require(state.run.mining.artifact.state == MiningArtifactState::Delivered,
        "a tethered artifact should deliver anywhere inside the visible ship service zone");
    require(state.run.mining.temporaryArtifacts.empty(), "delivered artifact should not remain in the rig ledger");
    require(state.run.mining.stowedArtifacts.size() == 1, "delivered artifact should enter the Ship manifest even before the rig docks");
    require(state.run.mining.stowedCargo >= tuning::mining::artifactCargo, "delivered artifact should add banked cargo weight");
}

void miningArtifactTetherAndDestructionRules()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 92930);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    state.run.planetaryExpedition.prospectArtifacts = 1;
    require(
        startMiningRun(state, catalog, {MiningAct::ActOne, 8, 92930, true, MiningGateType::None}, true).applied,
        "artifact tether test should start mining at an artifact-enabled tier");

    MiningArtifactObject& artifact = state.run.mining.artifact;
    artifact.present = true;
    artifact.id = content::protectedObjective::lunarSignalArtifact;
    artifact.state = MiningArtifactState::Loose;
    artifact.x = state.run.mining.droneX;
    artifact.y = state.run.mining.droneY + 1.0;
    artifact.health = artifact.maxHealth = 10.0;
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

    // A tethered artifact must be able to scrape tunnel terrain while being
    // dragged. Only a high-speed slam should cost condition.
    MiningCell* bumpWall = miningCellAt(state.run.mining.terrain, 21, 10);
    require(bumpWall != nullptr, "artifact bump-threshold fixture requires a wall cell");
    *bumpWall = {MiningCellMaterial::HardRock, 1.0, 1.0, true, false};
    state.run.mining.gravityStrength = 0.0;
    state.run.mining.artifact.tethered = false;
    state.run.mining.artifact.state = MiningArtifactState::Loose;
    state.run.mining.artifact.maxHealth = 1.0;
    state.run.mining.artifact.health = 1.0;
    state.run.mining.artifact.x = 20.95;
    state.run.mining.artifact.y = 10.5;
    state.run.mining.artifact.velocityX = 5.0;
    state.run.mining.artifact.velocityY = 0.0;
    updateMiningRun(state, catalog, 0.08);
    require(std::abs(state.run.mining.artifact.health - 1.0) < 0.000001,
        "ordinary artifact bumps against tunnel terrain must not cause damage");

    state.run.mining.artifact.x = 20.95;
    state.run.mining.artifact.y = 10.5;
    state.run.mining.artifact.velocityX = 8.0;
    state.run.mining.artifact.velocityY = 0.0;
    updateMiningRun(state, catalog, 0.08);
    require(state.run.mining.artifact.health < 1.0,
        "a high-speed artifact slam should still cause condition damage");

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
    story.run.planetaryExpedition.hazard = 0.0;
    story.run.planetaryExpedition.cargo = 0;
    story.run.planetaryExpedition.supply = 10;
    story.run.planetaryExpedition.rigFuel = story.run.planetaryExpedition.rigFuelCapacity;
    story.run.planetaryExpedition.temporaryArtifacts.push_back({"story_artifact", content::destination::nearbyStar, false, ArtifactKind::Story, ArtifactRewardType::None, 1.0, false});
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
    boost.run.planetaryExpedition.hazard = 0.0;
    boost.run.planetaryExpedition.cargo = 0;
    boost.run.planetaryExpedition.supply = 10;
    boost.run.planetaryExpedition.rigFuel = boost.run.planetaryExpedition.rigFuelCapacity;
    boost.run.planetaryExpedition.temporaryArtifacts.push_back({"boost_artifact", content::destination::mars, false, ArtifactKind::Boost, ArtifactRewardType::Credits, 1.0, false});
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
    state.run.planetaryExpedition.prospectArtifacts = 1;
    require(
        startMiningRun(state, catalog, {MiningAct::ActOne, 8, 92933, true, MiningGateType::None}, true).applied,
        "artifact save test should start mining at an artifact-enabled tier");
    state.run.mining.artifact.present = true;
    state.run.mining.artifact.id = content::protectedObjective::lunarSignalArtifact;
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

void poiGuidancePrioritizesSafetyAndTracksRecoverableArtifacts()
{
    MiningRunState mining;
    mining.active = true;
    mining.depthZone = 2;
    mining.entryDepthZone = 0;
    mining.returnZoneX = 8.0;
    mining.returnZoneY = 3.0;
    mining.rigOxygen.current = 100.0;
    mining.droneHealth = 1.0;
    mining.drillIntegrity = 1.0;
    mining.artifact.present = true;
    mining.artifact.revealed = true;
    mining.artifact.state = MiningArtifactState::Embedded;
    mining.artifact.x = 12.5;
    mining.artifact.y = 18.5;

    PoiGuidanceTarget guidance = miningPoiGuidanceTarget(
        mining, miningActiveOxygenSeconds(mining), 100.0, tuning::launch::warningCautionThreshold, false);
    require(guidance.active && guidance.kind == PoiGuidanceKind::Artifact &&
            guidance.direction == PoiGuidanceDirection::WorldTarget,
        "a revealed recoverable artifact on the active layer should receive dynamic guidance");

    mining.rigOxygen.current = 37.0;
    guidance = miningPoiGuidanceTarget(
        mining, miningActiveOxygenSeconds(mining), 100.0, tuning::launch::warningCautionThreshold, false);
    require(guidance.active && guidance.kind == PoiGuidanceKind::Ship &&
            guidance.direction == PoiGuidanceDirection::Ascend,
        "oxygen caution should override artifact guidance and point upward below surface");

    mining.rigOxygen.current = 100.0;
    mining.droneHealth = 0.37;
    guidance = miningPoiGuidanceTarget(
        mining, miningActiveOxygenSeconds(mining), 100.0, tuning::launch::warningCautionThreshold, false);
    require(guidance.active && guidance.kind == PoiGuidanceKind::Ship,
        "mining rig integrity caution should override artifact guidance");

    mining.droneHealth = 1.0;
    mining.operatorMode = MiningOperatorMode::Jetpack;
    mining.operatorPresent = true;
    mining.operatorIntegrity = 0.37;
    guidance = miningPoiGuidanceTarget(
        mining, miningActiveOxygenSeconds(mining), tuning::mining::operatorOxygenSeconds, tuning::launch::warningCautionThreshold, false);
    require(guidance.active && guidance.kind == PoiGuidanceKind::Ship,
        "active operator suit integrity caution should override artifact guidance");

    mining.operatorMode = MiningOperatorMode::Rig;
    mining.operatorPresent = false;
    mining.operatorIntegrity = 1.0;
    mining.drillIntegrity = 0.37;
    guidance = miningPoiGuidanceTarget(
        mining, miningActiveOxygenSeconds(mining), 100.0, tuning::launch::warningCautionThreshold, false);
    require(guidance.active && guidance.kind == PoiGuidanceKind::Ship,
        "drill integrity caution should override artifact guidance");

    mining.drillIntegrity = 1.0;
    mining.rigOxygen.current = 37.0;
    mining.depthZone = 0;
    guidance = miningPoiGuidanceTarget(
        mining, miningActiveOxygenSeconds(mining), 100.0, tuning::launch::warningCautionThreshold, false);
    require(guidance.active && guidance.direction == PoiGuidanceDirection::WorldTarget &&
            guidance.x == mining.returnZoneX && guidance.y == mining.returnZoneY,
        "surface safety guidance should point directly to the ship");
    require(!miningPoiGuidanceTarget(
                 mining, miningActiveOxygenSeconds(mining), 100.0, tuning::launch::warningCautionThreshold, true).active,
        "surface ship guidance should disappear inside the return zone");

    mining.rigOxygen.current = 100.0;
    mining.depthZone = 2;
    mining.artifact.state = MiningArtifactState::Delivered;
    guidance = miningPoiGuidanceTarget(
        mining, miningActiveOxygenSeconds(mining), 100.0, tuning::launch::warningCautionThreshold, false);
    require(!guidance.active, "delivered artifacts should not retain POI guidance");
    mining.artifact.state = MiningArtifactState::Destroyed;
    guidance = miningPoiGuidanceTarget(
        mining, miningActiveOxygenSeconds(mining), 100.0, tuning::launch::warningCautionThreshold, false);
    require(!guidance.active, "destroyed artifacts should not retain POI guidance");

    MiningDepthLayerState upperLayer;
    upperLayer.depthZone = 1;
    upperLayer.artifact.present = true;
    upperLayer.artifact.revealed = true;
    upperLayer.artifact.state = MiningArtifactState::Loose;
    mining.depthLayers = {upperLayer};
    guidance = miningPoiGuidanceTarget(
        mining, miningActiveOxygenSeconds(mining), 100.0, tuning::launch::warningCautionThreshold, false);
    require(guidance.active && guidance.direction == PoiGuidanceDirection::Ascend,
        "an artifact on an upper cached layer should lead to the ascent boundary");
    mining.depthLayers.front().depthZone = 3;
    guidance = miningPoiGuidanceTarget(
        mining, miningActiveOxygenSeconds(mining), 100.0, tuning::launch::warningCautionThreshold, false);
    require(guidance.active && guidance.direction == PoiGuidanceDirection::Descend,
        "an artifact on a lower cached layer should lead to the descent boundary");

    PoiGuidanceTarget futureTarget;
    futureTarget.active = true;
    futureTarget.kind = PoiGuidanceKind::Boss;
    futureTarget.label = "BOSS";
    require(futureTarget.kind == PoiGuidanceKind::Boss,
        "future story and boss guidance should use the same dynamic-label interface");
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
    for (int x = 1; x < a.width - 1; ++x) {
        require(miningCellAt(a, x, a.height - 1)->material != MiningCellMaterial::Bedrock,
            "intermediate depth floors must remain excavatable");
    }
    for (int y = 0; y < a.height; ++y) {
        require(miningCellAt(a, 0, y)->material == MiningCellMaterial::Bedrock &&
                miningCellAt(a, a.width - 1, y)->material == MiningCellMaterial::Bedrock,
            "bedrock must continue to frame the sides of every depth");
    }
    const MiningTerrain finalDepth = generateMiningTerrain(
        state,
        outerPlanets,
        SurfaceSiteProfile::OreShelf,
        tuning::surfaceDepthProgression::maximumDepthRating);
    for (int x = 1; x < finalDepth.width - 1; ++x) {
        require(miningCellAt(finalDepth, x, finalDepth.height - 1)->material == MiningCellMaterial::Bedrock,
            "the final supported depth must retain its bedrock floor");
    }
    MiningRunState legacyBoundaries;
    legacyBoundaries.terrain = a;
    for (int x = 1; x < legacyBoundaries.terrain.width - 1; ++x) {
        miningCellAt(legacyBoundaries.terrain, x, legacyBoundaries.terrain.height - 1)->material =
            MiningCellMaterial::Bedrock;
    }
    MiningDepthLayerState legacyFinal;
    legacyFinal.depthZone = tuning::surfaceDepthProgression::maximumDepthRating;
    legacyFinal.terrain = finalDepth;
    legacyBoundaries.depthLayers.push_back(legacyFinal);
    repairLegacyDepthBoundaries(legacyBoundaries);
    for (int x = 1; x < legacyBoundaries.terrain.width - 1; ++x) {
        require(miningCellAt(legacyBoundaries.terrain, x, legacyBoundaries.terrain.height - 1)->material !=
                MiningCellMaterial::Bedrock,
            "save repair must remove obsolete intermediate bedrock floors");
        require(miningCellAt(legacyBoundaries.depthLayers.front().terrain, x,
                legacyBoundaries.depthLayers.front().terrain.height - 1)->material == MiningCellMaterial::Bedrock,
            "save repair must preserve the final-depth bedrock floor");
    }
    for (int x = 1; x < legacyBoundaries.terrain.width - 1; ++x) {
        miningCellAt(legacyBoundaries.terrain, x, legacyBoundaries.terrain.height - 1)->material =
            MiningCellMaterial::Bedrock;
    }
    legacyBoundaries.active = true;
    legacyBoundaries.destinationId = outerPlanets.id;
    GameState legacySaveState = state;
    legacySaveState.screen = Screen::Mining;
    legacySaveState.run.planetaryExpedition.active = true;
    legacySaveState.run.mining = legacyBoundaries;
    const auto legacySave = deserializeSaveData(serializeSaveData(captureSaveData(legacySaveState)));
    require(legacySave.has_value(), "legacy intermediate bedrock fixture must serialize");
    GameState repairedSave = createNewGame(catalog, 91919);
    restoreSaveData(repairedSave, catalog, *legacySave);
    require(repairedSave.run.mining.active,
        "depth-boundary repair must preserve the active mining save");
    for (int x = 1; x < repairedSave.run.mining.terrain.width - 1; ++x) {
        require(miningCellAt(repairedSave.run.mining.terrain, x,
                repairedSave.run.mining.terrain.height - 1)->material != MiningCellMaterial::Bedrock,
            "loading a save must repair its obsolete intermediate bedrock floor");
    }
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
        state.run.planetaryExpedition.rigFuel = std::max(1.0, state.run.planetaryExpedition.rigFuel);
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
    spawnerArena.run.mining.rigOxygen.current = 1000.0;
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
    require(std::any_of(
                defended.run.mining.looseObjects.begin(),
                defended.run.mining.looseObjects.end(),
                [](const MiningLooseObject& object) {
                    return object.active && object.kind == MiningLooseObjectKind::Material &&
                        (object.material == MiningCellMaterial::RareOre ||
                         object.material == MiningCellMaterial::ExoticVein);
                }),
        "miniboss defeats should drop physical upgrade-grade rewards into the world");
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
        state.run.expedition.progression.runDroneRanks = {{content::drone::hazardDrone, level}};
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
    state.run.expedition.progression.runDroneRanks = {{content::drone::hazardDrone, 1}};
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

    GameState hiddenCocoon = contactState(MiningElementalAffinity::Thermal);
    hiddenCocoon.run.mining.drillHeat = 0.0;
    hiddenCocoon.run.mining.droneX = 32.5;
    hiddenCocoon.run.mining.droneY = 8.5;
    MiningCell* concealedCocoonHazard = miningCellAt(hiddenCocoon.run.mining.terrain, 33, 8);
    require(concealedCocoonHazard != nullptr, "hidden cocoon hazard test cell should exist");
    concealedCocoonHazard->revealed = true;
    concealedCocoonHazard->cocoonLayer = 0;
    hiddenCocoon.run.mining.gate.cocoonLayers.push_back({"hidden", "HIDDEN", 1, 1, 1, false});
    const double healthBeforeHiddenCocoon = hiddenCocoon.run.mining.droneHealth;
    setMiningDrilling(hiddenCocoon, false);
    updateMiningRun(hiddenCocoon, catalog, 0.20);
    require(hiddenCocoon.run.mining.drillHeat == 0.0 &&
            hiddenCocoon.run.mining.droneHealth == healthBeforeHiddenCocoon,
        "concealed cocoon layers must remain inert until their reveal rule is satisfied");
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
    state.run.mining.rigOxygen.current = 100.0;

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
    mining.rigOxygen.current = 100.0;
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
        "safe-recall behavior, timer, and cargo should survive an active current save");

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
    state.run.mining.rigOxygen.current = 100.0;

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
    state.run.mining.rigOxygen.current = 100.0;

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
        state.run.expedition.progression.runDroneRanks = {{content::drone::resourceDrone, upgradeLevel}};
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
    // Leave twelve units free in the expanded starter hold.
    state.meta.materials.common = shipHoldCapacity(state, catalog) - 12;
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
        state.run.mining.stowedMaterials.exotic == stowedBeforeUnload + 7,
        "Resource drone should unload only the material that fits the hard-capped Ship hold");
    require(resource->haulMaterials.common == 0 && resource->haulMaterials.rare == 0 &&
        resource->haulMaterials.exotic == 1 &&
        resource->behavior == MiningMiniDroneBehavior::DeliveringToShip,
        "Ship overflow should remain on its physical Resource drone at service");
    require(state.run.mining.stowedMaterials.common == 4 &&
        state.run.mining.stowedMaterials.rare == 2 &&
        state.run.mining.stowedMaterials.exotic == 1,
        "Resource drone drop-off should fill exactly twelve units of Ship hold mass");

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
    state.run.mining.rigOxygen.current = 100.0;

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
    state.run.expedition.progression.runDroneRanks = {{content::drone::miningDrone, 1}};
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog).applied, "mining shuttle run should start");
    state.run.mining.enemies.clear();
    state.run.mining.rigOxygen.current = 100.0;

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
    const MaterialInventory richRewardsBefore = state.run.mining.richRewardsAwarded;
    state.run.mining.enemies = {
        {MiningEnemyType::Mammal, MiningCellFeature::BossChamber, state.run.mining.droneX + 2.0, state.run.mining.droneY, 0.0, 0.0, 0.5, 35.0, 0.20, 0.0, 0.0, 0.0, true}
    };
    for (int tick = 0; tick < 10 && state.run.mining.enemiesDefeated == 0; ++tick) {
        updateMiningRun(state, catalog, 0.08);
    }
    require(state.run.mining.enemiesDefeated == 1, "passive defenses should defeat weakened mammal boss test enemies");
    const int rareRewards = state.run.mining.richRewardsAwarded.rare - richRewardsBefore.rare;
    const int exoticRewards = state.run.mining.richRewardsAwarded.exotic - richRewardsBefore.exotic;
    require(rareRewards > 0 &&
            state.run.mining.richRewardsAwarded.rare <= state.run.mining.rewardBudget.rareCap,
        "mammal boss chambers should drop physical rare materials within the shared arena cap");
    require(exoticRewards > 0 &&
            state.run.mining.richRewardsAwarded.exotic <= state.run.mining.rewardBudget.exoticCap,
        "mammal boss chambers should drop physical exotic materials within the shared arena cap");
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
    const SurfaceActionOutcome started = startMiningRun(state, catalog);
    require(started.applied, "mining should start when the site is prepared");
    require(state.screen == Screen::Mining, "starting mining should move to the mining screen");

    MiningRunState& mining = state.run.mining;
    setMiningAim(state, 1.0, mining.droneY / static_cast<double>(mining.terrain.height - 1));
    setMiningMove(state, -1.0, 0.0);
    require(mining.hullDirY > 0.99, "movement requests must not rotate collision geometry before simulation");
    require(mining.aimDirX < -0.99 && std::abs(mining.aimDirY) < 0.01, "drill direction should remain fixed to the hull heading");
    setMiningMove(state, 0.0, 0.0);
    require(mining.hullDirY > 0.99, "accepted heading stays unchanged until the next simulation step");
    require(mining.aimDirX < -0.99, "drill direction should persist with the stopped hull heading");

    require(std::abs(mining.rigOxygen.current - tuning::mining::oxygenSeconds) < 0.000001, "starter mining run should begin with configured oxygen");
    require(nearlyEqual(mining.rigFuel.current, mining.rigFuel.capacity),
        "deployment should not charge a hidden fuel fee");
    MiningCell* ore = miningCellAt(mining.terrain, 33, 4);
    require(ore != nullptr, "test ore cell should exist");
    *ore = {MiningCellMaterial::CommonOre, 0.25, 0.25, true, false};
    MiningCell* farOre = miningCellAt(mining.terrain, 33, 3);
    require(farOre != nullptr, "test far ore cell should exist");
    *farOre = {MiningCellMaterial::CommonOre, 0.45, 0.45, true, false};
    std::fill(mining.terrain.dirtyChunks.begin(), mining.terrain.dirtyChunks.end(), 0);
    mining.droneX = 33.0 - rig_geometry::drillTip + .01;
        mining.hullDirX=1.0;mining.hullDirY=0.0;
        mining.rigGeometryValidated=true;
    mining.droneY = 4.0;
    setMiningMove(state, 1.0, 0.0);
    setMiningMove(state, 0.0, 0.0);
    setMiningDrilling(state, true);
    for (int i = 0; i < 8; ++i) {
        updateMiningRun(state, catalog, 0.08);
    }

    require(ore->material == MiningCellMaterial::Empty, "drilling should break depleted terrain cells");
    require(farOre->material == MiningCellMaterial::Empty, "drilling should damage every solid tile under the drill footprint instead of one tile at a time");
    require(std::any_of(
                mining.looseObjects.begin(),
                mining.looseObjects.end(),
                [](const MiningLooseObject& object) {
                    return object.active && object.kind == MiningLooseObjectKind::Material &&
                        object.material == MiningCellMaterial::CommonOre;
                }) || state.run.mining.temporaryMaterials.common > 0,
        "breaking common ore should create physical common material before intake");
    require(
        std::any_of(mining.terrain.dirtyChunks.begin(), mining.terrain.dirtyChunks.end(), [](std::uint8_t value) { return value != 0; }),
        "drilling should mark the changed chunk dirty");
}

void miningUsesRigFuelReserve()
{
    ResourceTankState tank {3.0, 3.0};
    const RigFuelEvent idle = consumeRigFuel(tank, {}, 30.0);
    require(nearlyEqual(idle.consumed, 0.0) && nearlyEqual(tank.current, 3.0),
        "idle and coasting should consume no fuel");
    const RigFuelEvent thrust = consumeRigFuel(tank, {true, false, 1.0, 0}, 30.0);
    require(nearlyEqual(thrust.consumed, 1.0) && nearlyEqual(tank.current, 2.0),
        "full unloaded thrust should consume one fuel per thirty seconds");
    const RigFuelEvent stacked = consumeRigFuel(tank, {true, true, 1.0, 0}, 30.0);
    require(nearlyEqual(stacked.consumed, 2.0) && nearlyEqual(tank.current, 0.0),
        "simultaneous thrust and drilling should stack their demand");
    const RigFuelEvent recovered = transferFuelCell(tank, 1.0);
    require(nearlyEqual(recovered.transferred, 1.0) && recovered.restarted && nearlyEqual(tank.current, 1.0),
        "physical fuel-cell contact should restart a zero-fuel rig");
}

void supportDronesPrioritizeArtifactWork()
{
    for (const auto role : {MiniDroneRole::Mining, MiniDroneRole::Hazard}) {
        MiningRunState mining;
        mining.active = true;
        mining.terrain.width = 30;
        mining.terrain.height = 20;
        mining.terrain.cells.resize(600);
        mining.droneX = mining.operatorX = 10.5;
        mining.droneY = mining.operatorY = 10.5;
        mining.artifact.present = mining.artifact.revealed = true;
        mining.artifact.state = MiningArtifactState::Embedded;
        mining.artifact.x = 16.5;
        mining.artifact.y = 10.5;
        mining.gate.active = true;
        for (const int x : {9, 15, 16}) {
            auto& cell = mining.terrain.cells[10 * 30 + x];
            cell.revealed = true;
            cell.material = role == MiniDroneRole::Hazard ? MiningCellMaterial::HazardPocket : MiningCellMaterial::CommonOre;
            cell.hazard = role == MiniDroneRole::Hazard;
            cell.hazardAffinity = MiningElementalAffinity::Thermal;
            cell.gateAssociated = x == 16;
            cell.maxToughness = cell.remainingToughness = 5;
        }
        MiningMiniDroneAgent agent;
        agent.role = role;
        agent.upgradeLevel = 3;
        agent.x = 10.5;
        agent.y = 10.5;
        MiningDroneCoordinator prospector(mining);
        HazardDroneCoordinator hazard(mining);
        MiniDroneTaskCoordinator& coordinator = role == MiniDroneRole::Mining
            ? static_cast<MiniDroneTaskCoordinator&>(prospector)
            : static_cast<MiniDroneTaskCoordinator&>(hazard);
        require(coordinator.acquireAssignment(agent) && agent.targetCellX == 16,
            "support drone must choose the mission gate before closer ordinary work");
        mining.terrain.cells[10 * 30 + 16] = {};
        require(coordinator.acquireAssignment(agent) && agent.targetCellX == 15,
            "support drone must clear non-gate artifact surroundings next");
        mining.artifact.revealed = false;
        require(coordinator.acquireAssignment(agent) &&
                agent.targetCellX == (role == MiniDroneRole::Hazard ? 15 : 9),
            "Hazard Drone clears the artifact approach before scanning; Prospector behavior stays unchanged");
        require(!mining.artifact.revealed,
            "Hazard priority must not reveal the unscanned artifact");
        mining.artifact.revealed = true;
        mining.artifact.state = MiningArtifactState::Delivered;
        require(coordinator.acquireAssignment(agent) && agent.targetCellX == 9,
            "delivered artifacts must not retain special work priority");
        mining.artifact.state = MiningArtifactState::Embedded;
        mining.terrain.cells[10 * 30 + 15] = {};
        require(coordinator.acquireAssignment(agent) && agent.targetCellX == 9,
            "ordinary work resumes when artifact surroundings are cleared");
    }
}

void drillPowerUpgradesProduceMeasuredCuttingGains()
{
    const ContentCatalog catalog = createDefaultContent();
    const auto oneTickDamage = [&](MiningCellMaterial material, const std::vector<RunRigUpgradeRank>& ranks) {
        GameState state = createNewGame(catalog, 92939);
        state.run.destinationIndex = 2;
        startSurfaceExpedition(state, catalog);
        prepareMiningSiteForTest(state);
        require(startMiningRun(state, catalog, {MiningAct::ActOne, 4, 92939}, false).applied,
            "measured drill-power fixture should start");
        state.run.expedition.progression.runRigUpgradeRanks = ranks;
        MiningRunState& mining = state.run.mining;
        for (MiningCell& cell : mining.terrain.cells) cell = {MiningCellMaterial::Empty, 0.0, 0.0, true, false};
        MiningCell* target = miningCellAt(mining.terrain, 33, 4);
        require(target != nullptr, "measured drill-power target should exist");
        *target = {material, 100.0, 100.0, true, false};
        mining.droneX = 33.0 - rig_geometry::drillTip + .01;
        mining.droneY = 4.0;
        mining.hullDirX = 1.0;
        mining.hullDirY = 0.0;
        mining.rigGeometryValidated = true;
        setMiningDrilling(state, true);
        updateMiningRun(state, catalog, 0.08);
        return 100.0 - target->remainingToughness;
    };

    const double starterCommon = oneTickDamage(MiningCellMaterial::CommonOre, {});
    const double torqueCommon = oneTickDamage(MiningCellMaterial::CommonOre,
        {{content::surfaceUpgrade::highTorqueMotor, 1}});
    require(starterCommon > 0.0 && nearlyEqual(torqueCommon / starterCommon, 1.25, 0.001),
        "High-Torque Motor I must cut ordinary terrain 25 percent faster than the improved starter Rig");

    const double starterHard = oneTickDamage(MiningCellMaterial::HardRock, {});
    const double teethHard = oneTickDamage(MiningCellMaterial::HardRock,
        {{content::surfaceUpgrade::hardRockTeeth, 1}});
    require(starterHard > 0.0 && nearlyEqual(teethHard / starterHard, 1.25, 0.001),
        "Hard-Rock Teeth I must multiply final Hard Rock cutting power by 25 percent");
}

void rigFuelLoopRanksControlOperatingCadence()
{
    const std::array<double, 4> expectedEfficiency {0.0, 0.1, 0.2, 0.3};
    for (int rank = 0; rank <= 3; ++rank) {
        require(nearlyEqual(rigFuelEfficiency(rank), expectedEfficiency[rank]),
            "Fuel Loop ranks should provide ten, twenty, and thirty percent efficiency");
        ResourceTankState tank {10.0, 10.0};
        consumeRigFuel(tank, {true, false, 1.0, rank}, 30.0);
        require(nearlyEqual(tank.current, 10.0 - (1.0 - expectedEfficiency[rank])),
            "Fuel Loop efficiency should lower powered consumption without changing tank capacity");
    }
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
            MiningCell* extraRock = miningCellAt(mining.terrain, 33, 3);
            require(extraRock != nullptr, "secondary rock should exist");
            *extraRock = {MiningCellMaterial::HardRock, 40.0, 40.0, true, false};
        }

        mining.droneX = 33.0 - rig_geometry::drillTip + .01;
        mining.hullDirX=1.0;mining.hullDirY=0.0;
        mining.rigGeometryValidated=true;
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
    const MiningCell* second = miningCellAt(doubleContact.run.mining.terrain, 33, 3);
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
        33.0 - rig_geometry::drillTip - 0.12;
    mining.droneX = softContactStartX;
    mining.droneY = 10.0;
    mining.hullDirX = 1.0;
    mining.hullDirY = 0.0;
    MiningCell* soft = miningCellAt(mining.terrain, 33, 10);
    require(soft != nullptr, "soft contact cell should exist");
    *soft = {MiningCellMaterial::Regolith, 3.0, 3.0, false, false};
    setMiningMove(state, 1.0, 0.0);
    setMiningDrilling(state, true);
    updateMiningRun(state, catalog, 0.08);
    updateMiningRun(state, catalog, 0.08); // Arrive at contact, then cut on the next tick.
    updateMiningRun(state, catalog, 0.08);
    require(mining.droneX > softContactStartX, "drilling into regolith should let the drone grind forward slowly");
    require(mining.droneX + rig_geometry::drillTip <= 33.001,
        "the full oriented hull should not overlap unbroken regolith before the drill clears it");
    require(mining.contactIntensity > 0.0, "soft contact should set mining feedback intensity");
    require(soft->remainingToughness < soft->maxToughness, "pushing into regolith while drilling should do terrain work");

    for (int i = 0; i < 64 && soft->material != MiningCellMaterial::Empty; ++i) {
        updateMiningRun(state, catalog, 0.08);
    }

    require(soft->material == MiningCellMaterial::Empty, "continued drilling should visibly clear soft terrain before the drone passes through");

    const double hardContactStartX =
        33.0 - rig_geometry::drillTip - 0.02;
    mining.droneX = hardContactStartX;
    mining.droneY = 12.0;
    MiningCell* hard = miningCellAt(mining.terrain, 33, 12);
    require(hard != nullptr, "hard contact cell should exist");
    *hard = {MiningCellMaterial::HardRock, 8.0, 8.0, false, false};
    mining.contactIndicatorLatched = false;
    mining.contactIndicatorSerial = 0;
    mining.contactIndicatorSeconds = 0.0;
    mining.contactIntensity = 0.0;
    mining.contactBounce = 0.0;
    mining.contactBounceVelocity = 0.0;
    mining.contactBounceCooldown = 0.0;
    mining.contactSpeedRecovery = 1.0;
    setMiningMove(state, 1.0, 0.0);
    setMiningDrilling(state, true);
    GameState dampedState = state;
    dampedState.run.expedition.progression.runRigUpgradeRanks = {{content::surfaceUpgrade::shockMounts, 1}};
    updateMiningRun(state, catalog, 0.08);
    updateMiningRun(dampedState, catalog, 0.08);

    const double hardContactBoundary = 33.0 - rig_geometry::drillTip;
    require(
        mining.droneX > hardContactStartX && mining.droneX < hardContactBoundary + 0.001,
        "a hard-rock collision should sweep the rig to the physical boundary instead of leaving a full movement-step gap");
    require(mining.recoilX < 0.0, "hard contact should push feedback opposite travel");
    require(mining.contactIntensity > 0.5, "hard contact should produce stronger mining feedback");
    require(
        mining.contactIndicatorSeconds > 0.0 && mining.contactIndicatorSerial > 0 && mining.rigContactX == 33 &&
            std::hypot(mining.contactIndicatorDirX,mining.contactIndicatorDirY) > .99,
        "a player-driven rig collision should retain a short-lived indicator on the contacted edge");
    {
        GameState resting = state;
        auto& contact = resting.run.mining;
        const auto serial = contact.contactIndicatorSerial;
        const double x = contact.droneX, y = contact.droneY;
        setMiningDrilling(resting, false);
        for (int i = 0; i < 12; ++i) {
            contact.droneX = x;
            contact.droneY = y;
            updateMiningRun(resting, catalog, .08);
        }
        require(contact.contactIndicatorSerial == serial && contact.contactIndicatorSeconds == 0.0,
            "holding against terrain must not refresh the first-impact flash or sound event");
        contact.droneX = x - 4.0;
        setMiningMove(resting, 0.0, 0.0);
        updateMiningRun(resting, catalog, .08);
        require(!contact.contactIndicatorLatched, "clear separation must re-arm terrain impact feedback");
        contact.droneX = x;
        contact.droneY = y;
        setMiningMove(resting, 1.0, 0.0);
        updateMiningRun(resting, catalog, .08);
        require(contact.contactIndicatorSerial > serial, "a new collision after separation must sound again");
    }
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
    mining.droneX = hardContactStartX;
    mining.droneY = 12.0;
    setMiningMove(state, 1.0, 0.0);
    setMiningDrilling(state, true);
    for (int i = 0; i < 4; ++i) {
        updateMiningRun(state, catalog, 0.08);
    }
    require(hard->material == MiningCellMaterial::HardRock, "hard rock should require several hard contacts before breaking");
    for (int i = 0; i < 96 && hard->material != MiningCellMaterial::Empty; ++i) {
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
    clearMiningTerrainForEvaTest(mining);
    mining.droneX = 33.0 - rig_geometry::drillTip - 0.01;
    mining.droneY = 10.0;
    MiningCell* nearOre = miningCellAt(mining.terrain, 33, 10);
    MiningCell* farOre = miningCellAt(mining.terrain, 34, 10);
    require(nearOre != nullptr && farOre != nullptr, "targeting test cells should exist");
    *nearOre = {MiningCellMaterial::CommonOre, 1.0, 1.0, true, false};
    *farOre = {MiningCellMaterial::RareOre, 1.0, 1.0, true, false};

    mining.hullDirX=1; mining.hullDirY=0;
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
    const double hazardBeforeBanking = state.run.planetaryExpedition.hazard;

    const SurfaceActionOutcome finished = finishMiningRun(state, catalog, false);
    require(finished.applied, "finishing mining should produce a surface action outcome");
    require(state.screen == Screen::SurfaceExpedition, "finishing mining should return to surface expedition");
    require(!state.run.mining.active, "finishing mining should clear the active mining run");
    require(state.meta.materials.common == 2 && state.meta.materials.rare == 1,
        "physically banked ore should enter the capped Ship hold when no contract is active");
    require(state.run.planetaryExpedition.cargo == 4,
        "the expedition audit should retain the mass that physically reached the Ship");
    require(nearlyEqual(state.run.planetaryExpedition.hazard, hazardBeforeBanking),
        "successful physical banking should not add an abstract post-run hazard penalty");
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

void miningShipBankingLeaveAndEmergencyRecallRules()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 95959);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    state.run.expedition.progression.runRigUpgradeRanks = {
        {content::surfaceUpgrade::cargoSkids, 1},
        {content::surfaceUpgrade::emergencyWinch, 1}
    };
    prepareMiningSiteForTest(state);
    require(
        startMiningRun(state, catalog, {MiningAct::ActOne, 2, 95959}, false).applied,
        "mining should start for ship banking test with oxygen enabled");

    state.run.mining.temporaryMaterials.common = 2;
    state.run.mining.cargo = 2;
    state.run.mining.rigOxygen.current = 3.0;
    state.run.mining.oxygenDepletedNotified = true;
    const double oxygenCapacity = state.run.mining.rigOxygen.capacity;
    state.run.mining.droneX = state.run.mining.returnZoneX;
    state.run.mining.droneY = state.run.mining.returnZoneY;
    updateMiningRun(state, catalog, 0.08);
    require(state.run.mining.temporaryMaterials.common == 0, "ship zone should clear carried materials after banking");
    require(state.run.mining.cargo == 0, "ship zone should clear carried cargo after banking");
    require(state.run.mining.stowedMaterials.common == 2, "ship zone should stow carried materials");
    require(state.run.mining.stowedCargo == 2, "ship zone should stow carried cargo");
    require(
        std::abs(state.run.mining.rigOxygen.current - oxygenCapacity) < 0.000001,
        "ship service should refill the current oxygen capacity while banking payload");
    require(!state.run.mining.oxygenDepletedNotified, "oxygen refill should reset the depletion warning latch");

    state.run.mining.rigOxygen.current = 3.0;
    updateMiningRun(state, catalog, 0.08);
    require(
        std::abs(state.run.mining.rigOxygen.current - oxygenCapacity) < 0.000001,
        "entering the ship zone without carried payload should grant free rig oxygen service");

    MiningRunPresentation atShip = miningRunPresentation(state, catalog);
    require(std::any_of(atShip.actions.begin(), atShip.actions.end(), [](const PanelButtonPresentation& action) {
        return action.actionId == ui::actions::miningStow;
    }), "Leave should appear inside the ship radius");
    require(std::none_of(atShip.actions.begin(), atShip.actions.end(), [](const PanelButtonPresentation& action) {
        return action.actionId == ui::actions::miningAbort;
    }), "Emergency recall should not appear inside the ship radius");

    state.run.mining.droneX = state.run.mining.returnZoneX + tuning::mining::returnZoneCenterOffsetX + tuning::mining::returnZoneRadiusCells * 0.95;
    state.run.mining.droneY = state.run.mining.returnZoneY - tuning::mining::returnZoneCenterHeightCells;
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
    require(state.meta.materials.common == 2, "emergency recall should preserve material already banked in the Ship hold");
    require(state.meta.materials.rare == 0, "emergency recall should lose material still carried by the rig");
    require(state.run.planetaryExpedition.cargo == 2, "emergency recall should preserve only banked cargo");
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
    lucky.run.expedition.progression.runRigUpgradeRanks = {{content::surfaceUpgrade::widebandPulse, 1}};
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

    GameState debugState = state;
    require(enterMiningSwarmArenaForDebug(debugState, catalog),
        "the Swarm debug arena should enter the authored chamber");
    require(debugState.run.mining.enemies.empty() &&
            debugState.run.mining.swarm.spawnedInWave == 0 &&
            debugState.run.mining.swarm.spawnCooldownSeconds >=
                tuning::mining::swarmWaveInitialDelayMinimumSeconds &&
            debugState.run.mining.swarm.spawnCooldownSeconds <=
                tuning::mining::swarmWaveInitialDelayMaximumSeconds,
        "the debug arena should use the real staggered ingress instead of materializing a full horde");
    updateMiningRun(debugState, catalog, 0.08);
    require(debugState.run.mining.swarm.spawnedInWave == 0,
        "the debug arena's seeded initial delay should remain visible for at least one frame");

    mining.droneX = static_cast<double>(mining.swarm.triggerX);
    mining.droneY = static_cast<double>(mining.swarm.chamberY);
    mining.droneHealth = 100000.0;
    mining.rigOxygen.current = 1000.0;
    mining.miniDrones.clear();
    GameState sameSeedState = state;
    GameState differentSeedState = state;
    differentSeedState.run.mining.swarm.seed ^= 0x9E3779B97F4A7C15ULL;
    const auto advanceToFirstSpawn = [&](GameState& target) {
        for (int step = 0;
             step < 80 && target.run.mining.swarm.spawnedInWave == 0;
             ++step) {
            updateMiningRun(target, catalog, 0.08);
        }
    };
    advanceToFirstSpawn(state);
    advanceToFirstSpawn(sameSeedState);
    advanceToFirstSpawn(differentSeedState);
    const auto firstSwarmEnemy = std::find_if(
        mining.enemies.begin(),
        mining.enemies.end(),
        [](const MiningEnemy& enemy) { return enemy.active && enemy.swarmAssociated; });
    require(firstSwarmEnemy != mining.enemies.end(), "Swarm wave should begin after its warning");
    require(
        firstSwarmEnemy->x < 0.0 || firstSwarmEnemy->x > static_cast<double>(mining.terrain.width) ||
            firstSwarmEnemy->y < 0.0 || firstSwarmEnemy->y > static_cast<double>(mining.terrain.height),
        "Swarm enemies should begin beyond the visible mine bounds instead of appearing beside the player");
    require(sameSeedState.run.mining.enemies.size() == mining.enemies.size() &&
            nearlyEqual(sameSeedState.run.mining.enemies.front().x, firstSwarmEnemy->x) &&
            nearlyEqual(sameSeedState.run.mining.enemies.front().y, firstSwarmEnemy->y) &&
            nearlyEqual(sameSeedState.run.mining.swarm.spawnCooldownSeconds,
                mining.swarm.spawnCooldownSeconds),
        "identical Swarm seeds should reproduce entrance position and timing");
    require(!differentSeedState.run.mining.enemies.empty() &&
            (!nearlyEqual(differentSeedState.run.mining.enemies.front().x, firstSwarmEnemy->x) ||
                !nearlyEqual(differentSeedState.run.mining.enemies.front().y, firstSwarmEnemy->y)),
        "different Swarm seeds should vary the first entrance point");

    std::vector<double> observedSpawnIntervals {mining.swarm.spawnCooldownSeconds};
    std::vector<std::pair<double, double>> observedSpawnPoints {{
        firstSwarmEnemy->x - firstSwarmEnemy->velocityX * 0.08,
        firstSwarmEnemy->y - firstSwarmEnemy->velocityY * 0.08}};
    int previousSpawnCount = mining.swarm.spawnedInWave;
    for (int step = 0; step < 80 && mining.swarm.spawnedInWave < 8; ++step) {
        updateMiningRun(state, catalog, 0.08);
        if (mining.swarm.spawnedInWave > previousSpawnCount) {
            observedSpawnIntervals.push_back(mining.swarm.spawnCooldownSeconds);
            const MiningEnemy& newest = mining.enemies.back();
            observedSpawnPoints.push_back({
                newest.x - newest.velocityX * 0.08,
                newest.y - newest.velocityY * 0.08});
            previousSpawnCount = mining.swarm.spawnedInWave;
        }
    }
    require(observedSpawnIntervals.size() >= 6,
        "the opening Swarm should expose several staggered spawn intervals");
    require(std::all_of(observedSpawnIntervals.begin(), observedSpawnIntervals.end(), [](double interval) {
            return interval >= tuning::mining::swarmSpawnIntervalMinimumSeconds &&
                interval <= tuning::mining::swarmSpawnIntervalMaximumSeconds;
        }),
        "every seeded Swarm spawn interval should remain inside its authored range");
    require(std::any_of(
                observedSpawnIntervals.begin() + 1,
                observedSpawnIntervals.end(),
                [&](double interval) {
                    return !nearlyEqual(interval, observedSpawnIntervals.front());
                }),
        "Swarm arrivals should not use a visibly fixed cadence");
    require(std::all_of(observedSpawnPoints.begin(), observedSpawnPoints.end(), [&](const auto& point) {
            return point.first <= -tuning::mining::swarmOffscreenSpawnMarginCells ||
                point.first >= static_cast<double>(mining.terrain.width) +
                    tuning::mining::swarmOffscreenSpawnMarginCells ||
                point.second <= -tuning::mining::swarmOffscreenSpawnMarginCells ||
                point.second >= static_cast<double>(mining.terrain.height) +
                    tuning::mining::swarmOffscreenSpawnMarginCells;
        }),
        "jittered Swarm entrance points should remain beyond the visible mine bounds");
    for (std::size_t first = 0; first < observedSpawnPoints.size(); ++first) {
        for (std::size_t second = first + 1; second < observedSpawnPoints.size(); ++second) {
            require(std::hypot(
                        observedSpawnPoints[first].first - observedSpawnPoints[second].first,
                        observedSpawnPoints[first].second - observedSpawnPoints[second].second) + 0.001 >=
                    tuning::mining::swarmSpawnMinimumSpacingCells,
                "seeded Swarm entrance points should not stack into one geometric origin");
        }
    }

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

    GameState tokenState = state;
    MiningRunState& tokenMining = tokenState.run.mining;
    tokenMining.droneX = tokenMining.swarm.cacheX;
    tokenMining.droneY = tokenMining.swarm.cacheY;
    tokenMining.gravityStrength = 0.0;
    tokenMining.rigVelocityX = 0.0;
    tokenMining.rigVelocityY = 0.0;
    int configuredMelee = 0;
    for (MiningEnemy& enemy : tokenMining.enemies) {
        if (!enemy.active || !enemy.swarmAssociated || configuredMelee >= 6) {
            enemy.active = false;
            continue;
        }
        const double angle = static_cast<double>(configuredMelee) * 6.28318530718 / 6.0;
        enemy.type = MiningEnemyType::Ant;
        enemy.x = tokenMining.droneX +
            std::cos(angle) * tuning::mining::swarmMeleeHoldingRadiusCells;
        enemy.y = tokenMining.droneY +
            std::sin(angle) * tuning::mining::swarmMeleeHoldingRadiusCells;
        enemy.maxHealth = 1000.0;
        enemy.health = enemy.maxHealth;
        enemy.attackCooldownSeconds = 0.0;
        enemy.swarmAttackCommitSeconds = 0.0;
        enemy.swarmAttackRequeueSeconds = 0.0;
        ++configuredMelee;
    }
    require(configuredMelee == 6, "the Swarm token fixture requires six melee enemies");
    updateMiningRun(tokenState, catalog, 0.08);
    const auto committedCount = [](const MiningRunState& run) {
        return static_cast<int>(std::count_if(
            run.enemies.begin(), run.enemies.end(), [](const MiningEnemy& enemy) {
                return enemy.active && enemy.swarmAssociated &&
                    enemy.swarmAttackCommitSeconds > 0.0;
            }));
    };
    require(committedCount(tokenMining) == tuning::mining::swarmMeleeAttackTokenCount,
        "a Swarm should grant exactly three simultaneous melee attack tokens when enough enemies are ready");
    std::vector<std::size_t> initiallyCommitted;
    for (std::size_t index = 0; index < tokenMining.enemies.size(); ++index) {
        if (tokenMining.enemies[index].swarmAttackCommitSeconds > 0.0) {
            initiallyCommitted.push_back(index);
        }
    }
    tokenMining.enemies[initiallyCommitted.front()].attackCooldownSeconds =
        tuning::mining::swarmMeleeAttackIntervalSeconds;
    tokenMining.enemies[initiallyCommitted.front()].swarmAttackCommitSeconds = 0.0;
    updateMiningRun(tokenState, catalog, 0.08);
    require(committedCount(tokenMining) == tuning::mining::swarmMeleeAttackTokenCount &&
            std::any_of(tokenMining.enemies.begin(), tokenMining.enemies.end(), [&](const MiningEnemy& enemy) {
                const std::size_t index = static_cast<std::size_t>(&enemy - tokenMining.enemies.data());
                return enemy.swarmAttackCommitSeconds > 0.0 &&
                    std::find(initiallyCommitted.begin(), initiallyCommitted.end(), index) ==
                        initiallyCommitted.end();
            }),
        "a vacated melee attack token should rotate to another ready enemy");

    const std::optional<SaveData> tokenSave = deserializeSaveData(
        serializeSaveData(captureSaveData(tokenState)));
    require(tokenSave.has_value(), "active Swarm attack-token state should serialize safely");
    GameState tokenRestored = createNewGame(catalog, 0x5A13);
    restoreSaveData(tokenRestored, catalog, *tokenSave);
    require(std::none_of(
                tokenRestored.run.mining.enemies.begin(),
                tokenRestored.run.mining.enemies.end(),
                [](const MiningEnemy& enemy) {
                    return enemy.swarmAttackCommitSeconds > 0.0 ||
                        enemy.swarmAttackRequeueSeconds > 0.0;
                }),
        "transient Swarm attack tokens should not enter the current save contract");
    updateMiningRun(tokenRestored, catalog, 0.08);
    require(committedCount(tokenRestored.run.mining) ==
            tuning::mining::swarmMeleeAttackTokenCount,
        "a loaded active Swarm should deterministically reacquire its melee tokens");

    GameState separationState = state;
    MiningRunState& separationMining = separationState.run.mining;
    int separatedEnemies = 0;
    for (MiningEnemy& enemy : separationMining.enemies) {
        if (!enemy.active || !enemy.swarmAssociated || separatedEnemies >= 3) {
            enemy.active = false;
            continue;
        }
        enemy.type = separatedEnemies == 0 ? MiningEnemyType::Ant : MiningEnemyType::Beetle;
        enemy.elite = separatedEnemies == 2;
        enemy.x = separationMining.swarm.cacheX;
        enemy.y = separationMining.swarm.cacheY;
        enemy.maxHealth = 1000.0;
        enemy.health = enemy.maxHealth;
        enemy.attackCooldownSeconds = tuning::mining::swarmMeleeAttackIntervalSeconds;
        ++separatedEnemies;
    }
    for (int step = 0; step < 8; ++step) {
        updateMiningRun(separationState, catalog, 0.08);
    }
    std::vector<const MiningEnemy*> separated;
    for (const MiningEnemy& enemy : separationMining.enemies) {
        if (enemy.active && enemy.swarmAssociated) {
            separated.push_back(&enemy);
        }
    }
    const auto separationRadius = [](const MiningEnemy& enemy) {
        if (enemy.elite) return tuning::mining::swarmEliteSeparationRadiusCells;
        if (enemy.type == MiningEnemyType::Beetle || enemy.type == MiningEnemyType::Mammal) {
            return tuning::mining::swarmLargeSeparationRadiusCells;
        }
        return tuning::mining::swarmSmallSeparationRadiusCells;
    };
    require(separated.size() == 3, "the mixed Swarm spacing fixture should retain three enemies");
    for (std::size_t first = 0; first < separated.size(); ++first) {
        for (std::size_t second = first + 1; second < separated.size(); ++second) {
            const double distance = std::hypot(
                separated[first]->x - separated[second]->x,
                separated[first]->y - separated[second]->y);
            require(std::isfinite(distance) &&
                    distance + 0.04 >= separationRadius(*separated[first]) +
                        separationRadius(*separated[second]),
                "mixed Swarm enemies should resolve overlap to their type-aware minimum distance");
        }
    }

    mining.gravityStrength = 0.0;
    mining.rigVelocityX = 0.0;
    mining.rigVelocityY = 0.0;
    const auto verifySwarmRetreat = [&](MiningEnemyType type, double startRadius, double cooldown) {
        GameState retreatState = state;
        MiningRunState& retreatMining = retreatState.run.mining;
        // Retreat applies inside the nest; outside it enemies are still ingressing.
        retreatMining.droneX = retreatMining.swarm.cacheX;
        retreatMining.droneY = retreatMining.swarm.cacheY;
        const auto found = std::find_if(
            retreatMining.enemies.begin(),
            retreatMining.enemies.end(),
            [](const MiningEnemy& enemy) { return enemy.active && enemy.swarmAssociated; });
        require(found != retreatMining.enemies.end(), "Swarm retreat fixture requires an active enemy");
        const std::size_t enemyIndex = static_cast<std::size_t>(std::distance(retreatMining.enemies.begin(), found));
        // Isolate steering from the crowded horde's separation response.
        for (auto& other : retreatMining.enemies) other.active = &other == &*found;
        MiningEnemy& enemy = *found;
        enemy.type = type;
        enemy.maxHealth = 1000.0;
        enemy.health = enemy.maxHealth;
        enemy.attackCooldownSeconds = cooldown;
        constexpr double goldenAngle = 2.39996322973;
        const double orbitDirection = enemyIndex % 2 == 0 ? 1.0 : -1.0;
        const double slotAngle = std::fmod(
            static_cast<double>(enemyIndex + 1) * goldenAngle +
                static_cast<double>(retreatMining.swarm.wave) * 0.61 +
                retreatMining.elapsedSeconds * tuning::mining::swarmOrbitRadiansPerSecond * orbitDirection,
            6.28318530718);
        enemy.x = retreatMining.droneX + std::cos(slotAngle) * startRadius;
        enemy.y = retreatMining.droneY +
            std::sin(slotAngle) * startRadius * tuning::mining::swarmVerticalRingScale;
        const double before = std::hypot(enemy.x - retreatMining.droneX, enemy.y - retreatMining.droneY);
        updateMiningRun(retreatState, catalog, 0.08);
        const double after = std::hypot(retreatMining.enemies[enemyIndex].x - retreatMining.droneX, retreatMining.enemies[enemyIndex].y - retreatMining.droneY);
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
    state.run.mining.swarm.spawnCooldownSeconds = 0.19;
    state.run.mining.swarm.cacheExposed = true;
    const auto saved = deserializeSaveData(serializeSaveData(captureSaveData(state)));
    require(saved.has_value(), "Swarm mining save should deserialize");
    GameState restored = createNewGame(catalog, 0x5A12);
    restoreSaveData(restored, catalog, *saved);
    require(restored.run.mining.swarm.enabled && restored.run.mining.swarm.alerted &&
            restored.run.mining.swarm.wave == 2 && restored.run.mining.swarm.cacheExposed &&
            restored.run.mining.swarm.seed == state.run.mining.swarm.seed &&
            nearlyEqual(restored.run.mining.swarm.spawnCooldownSeconds, 0.19),
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

    clearMiningTerrainForEvaTest(state.run.mining);
    // Stay outside service horizontally, even when the unsupported ship falls.
    state.run.mining.droneX += tuning::mining::returnZoneRadiusCells + 2.0;
    state.run.mining.rigOxygen.current = 0.0;
    state.run.mining.suitOxygen.current = 5.0;
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
    const double suitBeforeEvaTick = state.run.mining.suitOxygen.current;
    const double rigOxygenAfterEjection = state.run.mining.rigOxygen.current;
    const double suitIntegrityBeforeEvaTick = state.run.mining.operatorIntegrity;
    updateMiningRun(state, catalog, 0.08);
    require(state.run.mining.suitOxygen.current < suitBeforeEvaTick &&
            std::abs(state.run.mining.rigOxygen.current - rigOxygenAfterEjection) < 0.000001 &&
            std::abs(state.run.mining.operatorIntegrity - suitIntegrityBeforeEvaTick) < 0.000001,
        "emergency EVA should use its remaining suit reserve before draining suit integrity");
}

void miningOxygenReservesDrainIndependentlyAndShipServicesBoth()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = activeMiningStateForEvaTest(catalog, 0x0A2E);
    MiningRunState& mining = state.run.mining;
    mining.gravityStrength = 0.0;
    mining.droneX = mining.returnZoneX + tuning::mining::returnZoneRadiusCells + 2.0;
    mining.droneY = mining.returnZoneY;
    mining.rigOxygen.current = 12.0;
    mining.suitOxygen.current = 9.0;

    updateMiningRun(state, catalog, 0.08);
    require(mining.rigOxygen.current < 12.0 && std::abs(mining.suitOxygen.current - 9.0) < 0.000001,
        "rig mode should drain only the rig reserve outside ship service");
    const double rigAfterRigTick = mining.rigOxygen.current;
    require(toggleMiningOperator(state), "independent oxygen test should be able to leave the rig for EVA");
    updateMiningRun(state, catalog, 0.08);
    require(mining.suitOxygen.current < 9.0 &&
            std::abs(mining.rigOxygen.current - rigAfterRigTick) < 0.000001,
        "EVA should drain only the suit reserve while the rig oxygen pauses");
    mining.suitOxygen.current = 0.0;
    const double rigIntegrityBeforeSuitDepletion = mining.droneHealth;
    const double suitIntegrityBeforeSuitDepletion = mining.operatorIntegrity;
    updateMiningRun(state, catalog, 0.08);
    require(mining.operatorIntegrity < suitIntegrityBeforeSuitDepletion &&
            std::abs(mining.droneHealth - rigIntegrityBeforeSuitDepletion) < 0.000001,
        "empty suit oxygen should damage only suit integrity while the rig remains safe");
    mining.suitOxygen.current = 8.0;
    const double suitAfterEvaTick = mining.suitOxygen.current;
    require(toggleMiningOperator(state), "independent oxygen test should re-enter the nearby rig");
    require(std::abs(mining.suitOxygen.current - suitAfterEvaTick) < 0.000001,
        "re-entering the rig away from the ship must not refill the EVA suit");

    mining.droneX = mining.returnZoneX;
    mining.droneY = mining.returnZoneY;
    mining.rigOxygen.current = 1.0;
    mining.suitOxygen.current = 1.0;
    updateMiningRun(state, catalog, 0.08);
    require(std::abs(mining.rigOxygen.current - mining.rigOxygen.capacity) < 0.000001 &&
            std::abs(mining.suitOxygen.current - mining.suitOxygen.capacity) < 0.000001,
        "an occupied rig at the ship should refill both independent reserves");
    require(miningHudPresentation(state, catalog).vitals[0].label == "RIG O2",
        "the compact HUD should identify the active rig oxygen reserve");
    Random rigPanelRng(0x0A2E);
    const PreparedLaunch rigPanelLaunch = prepareLaunch(state, catalog, rigPanelRng);
    const std::string rigPanelHtml = buildGamePanelHtml(
        {state, catalog, rigPanelLaunch, rigPanelLaunch});
    require(rigPanelHtml.find("id=\"rr-hud-mining-oxygen-label\">RIG O2") != std::string::npos &&
            rigPanelHtml.find("Rig O2") != std::string::npos &&
            rigPanelHtml.find("Suit O2") != std::string::npos,
        "mining details should expose both reserves while the compact vital names the active rig reserve");

    require(toggleMiningOperator(state), "ship-service test should be able to exit into EVA");
    mining.suitOxygen.current = 1.0;
    updateMiningRun(state, catalog, 0.08);
    require(std::abs(mining.suitOxygen.current - mining.suitOxygen.capacity) < 0.000001 &&
            miningHudPresentation(state, catalog).vitals[0].label == "SUIT O2",
        "EVA at the ship should refill only the suit and label the active suit reserve");
    Random suitPanelRng(0x0A2F);
    const PreparedLaunch suitPanelLaunch = prepareLaunch(state, catalog, suitPanelRng);
    const std::string suitPanelHtml = buildGamePanelHtml(
        {state, catalog, suitPanelLaunch, suitPanelLaunch});
    require(suitPanelHtml.find("id=\"rr-hud-mining-oxygen-label\">SUIT O2") != std::string::npos,
        "native and web panel markup should switch the live oxygen label to the suit during EVA");
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
    upgraded.run.expedition.progression.runRigUpgradeRanks = {
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
    require(upgradedStats.cargoCapacityBonus > baseStats.cargoCapacityBonus, "cargo refits should increase Rig capacity");
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
    upgraded.run.expedition.progression.runRigUpgradeRanks = {
        {content::surfaceUpgrade::coolantMist, 1},
        {content::surfaceUpgrade::shockMounts, 1},
        {content::surfaceUpgrade::oreScentArray, 1},
        {content::surfaceUpgrade::oreHopper, 1}
    };
    upgraded.meta.unlockKeys.push_back(content::unlock::droneBay);
    upgraded.meta.unlockKeys.push_back(content::unlock::perimeterDrones);
    upgraded.meta.droneBaySlots = 1;
    upgraded.meta.ownedDroneIds = {content::drone::defenseDrone};
    upgraded.meta.equippedDroneIds = {content::drone::defenseDrone};
    upgraded.run.expedition.progression.runDroneRanks = {
        {content::drone::defenseDrone, 3}
    };

    const MiningDrillStats operatorStats =
        miningOperatorDrillStats();
    const MiningDrillStats upgradedRigStats =
        miningDrillStats(upgraded, catalog);
    require(
        std::abs(
            operatorStats.power -
            tuning::mining::operatorBaseDrillPower *
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
        mining.looseObjects.clear();
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
        upgradedDrillYield.run.mining.looseObjects.size() == 1,
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
        upgradedSidearmYield.run.mining.looseObjects.size() == 1,
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
    state.run.mining.rigFuel = {1.25, 4.0};
    state.run.mining.rigOxygen = {12.5, 30.0};
    state.run.mining.suitOxygen = {6.25, 15.0};
    state.run.mining.cargo = 25;
    state.run.expedition.progression.runRigUpgradeRanks = {{content::surfaceUpgrade::cargoSkids, 1}};
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
    require(serialized.find("miningRigFuelTank=") != std::string::npos, "active mining saves should store the rig fuel tank");
    require(serialized.find("miningFuelCycle=") == std::string::npos, "current saves should not write the retired timed fuel cycle");
    const auto save = deserializeSaveData(serialized);
    require(save.has_value(), "active mining save should parse");

    GameState restored = createNewGame(catalog, 1);
    restoreSaveData(restored, catalog, *save);
    require(restored.screen == Screen::Mining, "active mining screen should round trip");
    require(restored.run.planetaryExpedition.miningSitePrepared && restored.run.planetaryExpedition.miningRunUsed, "active mining restore should preserve the one-run surface state");
    require(restored.run.mining.active, "active mining state should round trip");
    require(std::abs(restored.run.mining.droneX - 21.5) < 0.000001, "mining drone x should round trip");
    require(std::abs(restored.run.mining.hullDirX + 0.6) < 0.000001, "mining hull heading x should round trip");
    require(std::abs(restored.run.mining.hullDirY - 0.8) < 0.000001, "mining hull heading y should round trip");
    require(std::abs(restored.run.mining.returnZoneX - 31.5) < 0.000001, "mining return zone x should round trip");
    require(std::abs(restored.run.mining.returnZoneY - 4.25) < 0.000001, "mining return zone y should round trip");
    require(!restored.run.mining.rigTethered,
        "canonical saves should write the retired ship-to-rig tether as inactive");
    require(std::abs(restored.run.mining.droneHealth - 0.72) < 0.000001, "mining drone health should round trip");
    require(nearlyEqual(restored.run.mining.rigFuel.current, 1.25) && nearlyEqual(restored.run.mining.rigFuel.capacity, 4.0),
        "mining rig fuel tank should round trip");
    require(nearlyEqual(restored.run.mining.rigOxygen.current, 12.5) && nearlyEqual(restored.run.mining.suitOxygen.current, 6.25),
        "independent rig and suit oxygen tanks should round trip");
    require(restored.run.mining.temporaryMaterials.exotic == 1, "mining temporary materials should round trip");
    require(restored.run.mining.stowedMaterials.common == 2, "mining stowed materials should round trip");
    require(restored.run.mining.stowedCargo == 3, "mining stowed cargo should round trip");
    require(restored.run.mining.stowedArtifacts.size() == 1, "mining stowed artifacts should round trip");
    require(restored.run.mining.cargo == 25 && miningRigCargoCapacityMass(restored, catalog) == 26,
        "the upgraded 26-unit Rig hold and cargo beyond the original limit should round trip");
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
    mining.suitOxygen.current = 6.25;
    mining.operatorFireCooldownSeconds = 0.11;
    mining.operatorFirePulseSeconds = 0.07;
    mining.gravityDirectionX = 0.8;
    mining.gravityDirectionY = 0.6;
    mining.gravityStrength = 3.75;

    MiningLooseObject activeChunk;
    activeChunk.material = MiningCellMaterial::RareOre;
    activeChunk.x = 14.5;
    activeChunk.y = 16.25;
    activeChunk.velocityX = -0.3;
    activeChunk.velocityY = 0.9;
    activeChunk.cargoValue = 3;
    mining.looseObjects = {activeChunk};

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
    MiningLooseObject cachedChunk;
    cachedChunk.material = MiningCellMaterial::ExoticVein;
    cachedChunk.x = 8.5;
    cachedChunk.y = 20.5;
    cachedChunk.velocityX = 0.2;
    cachedChunk.velocityY = -0.4;
    cachedChunk.cargoValue = 5;
    cachedLayer.looseObjects = {cachedChunk};
    mining.depthLayers = {cachedLayer};
    mining.deepestDepthZone = cachedLayer.depthZone;

    const SaveData captured = captureSaveData(state);
    require(captured.version == save_schema::currentVersion, "new saves should use the current schema version");
    const std::string serialized = serializeSaveData(captured);
    require(serialized.find("miningRigState=") != std::string::npos, "version-six saves should write rig state");
    require(serialized.find("miningOperatorState=") != std::string::npos, "version-six saves should write operator state");
    require(serialized.find("miningGravity=") != std::string::npos, "version-six saves should write vector gravity");
    require(serialized.find("miningLooseObjects=") != std::string::npos, "version-six saves should write loose chunks");
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
    require(std::abs(result.suitOxygen.current - 6.25) < 0.000001,
        "independent EVA oxygen should round trip in the mining operator record");
    require(std::abs(result.gravityDirectionX - 0.8) < 0.000001 &&
            std::abs(result.gravityDirectionY - 0.6) < 0.000001 &&
            std::abs(result.gravityStrength - 3.75) < 0.000001,
        "gravity direction and strength should round trip");
    require(result.looseObjects.size() == 1 &&
            result.looseObjects.front().material == MiningCellMaterial::RareOre &&
            result.looseObjects.front().cargoValue == 3 &&
            std::abs(result.looseObjects.front().velocityY - 0.9) < 0.000001,
        "active-layer loose chunks should round trip");
    const MiningCell* restoredActiveSuitPassage =
        miningCellAt(result.terrain, 6, 6);
    require(
        restoredActiveSuitPassage != nullptr &&
            restoredActiveSuitPassage->suitOnlyPassage,
        "version-six saves should preserve active-layer suit-only passages");
    require(result.depthLayers.size() == 1 &&
            result.depthLayers.front().looseObjects.size() == 1 &&
            result.depthLayers.front().looseObjects.front().material == MiningCellMaterial::ExoticVein &&
            result.depthLayers.front().looseObjects.front().cargoValue == 5,
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
    for (int y = entry.terrain.height - 8; y < entry.terrain.height; ++y) {
        for (int x = 0; x < entry.terrain.width; ++x) {
            if (MiningCell* cell = miningCellAt(entry.terrain, x, y)) {
                *cell = {MiningCellMaterial::Empty, 0.0, 0.0, true, false};
            }
        }
    }
    entry.droneX = static_cast<double>(entry.terrain.width) * 0.5;
    entry.droneY = static_cast<double>(entry.terrain.height) - 7.0;
    entry.hullDirX = entry.aimDirX = 0.0;
    entry.hullDirY = entry.aimDirY = 1.0;
    entry.moveX = 0.0;
    entry.moveY = 1.0;
    const double hazardBeforeDescent = entry.hazardDelta;
    for (int tick = 0; tick < 180 && state.run.mining.depthZone == entryDepth; ++tick) {
        updateMiningRun(state, catalog, 0.08);
    }
    state.run.mining.moveY = 0.0;

    MiningRunState& deep = state.run.mining;
    require(deep.depthZone == entryDepth + 1,
        "driving through a physically open lower seam should descend exactly one depth");
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
    state.run.planetaryExpedition.depth = 2;
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
        mining.droneX < 20.45,
        "an explicit suit-only passage must reject the full oriented rig hull");

    setMiningMove(state, 0.0, 0.0);
    mining.droneX = 20.30;
    mining.droneY = 20.50;
    mining.rigVelocityX = 0.0;
    mining.rigVelocityY = 0.0;
    require(toggleMiningOperator(state), "the same marked passage should permit an adjacent EVA exit");
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
        finishMiningRun(state, catalog, false).applied,
        "an EVA operator who reaches the mothership must always be allowed to depart without teleporting the rig");
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
            !mining.looseObjects.empty(),
        "the EVA hand drill should turn ore into a spatial loose chunk");
    require(
        mining.temporaryMaterials.common == materialsBefore.common &&
            mining.cargo == cargoBefore,
        "the zero-cargo suit should not place hand-drilled ore directly into carried payload");

    mining.droneX = mining.looseObjects.front().x;
    mining.droneY = mining.looseObjects.front().y;
    updateMiningRun(state, catalog, 0.01);
    require(
        mining.looseObjects.empty() &&
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
    MiningLooseObject resourceChunk;
    resourceChunk.persistentId = resourceState.run.mining.nextLooseObjectId++;
    resourceChunk.kind = MiningLooseObjectKind::Material;
    resourceChunk.material = MiningCellMaterial::CommonOre;
    resourceChunk.x = home.x;
    resourceChunk.y = home.y;
    resourceChunk.mass = 1.0;
    resourceChunk.cargoValue = 1;
    resourceState.run.mining.looseObjects.push_back(resourceChunk);
    updateMiningRun(resourceState, catalog, 0.08);
    require(
        resource.haulMaterials.common == 1 &&
            resourceState.run.mining.looseObjects.empty(),
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
    // The EVA operator could depart here; this recovery-path test deliberately
    // keeps playing to tow and patch the wreck instead.
    disabledRig.droneX = disabledRig.returnZoneX;
    disabledRig.droneY = disabledRig.returnZoneY;
    disabledRig.rigOxygen.current = 0.0;
    disabledRig.stowedMaterials.common = 3;
    disabledRig.stowedCargo = disabledRig.stowedMaterials.common;
    updateMiningRun(towedWreck, catalog, 0.01);
    require(
        towedWreck.screen == Screen::Mining &&
            disabledRig.active &&
            disabledRig.rigDisabled &&
            disabledRig.rigOxygen.current > 0.0 &&
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
        evaRepairPanel.find(">SUIT INTEGRITY</span>") != std::string::npos &&
            evaRepairPanel.find("id=\"rr-hud-mining-drill-bit-value\">100%") != std::string::npos,
        "the compact vital should report suit integrity while the operator remains in EVA");
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
        repairedRigPanel.find(">RIG INTEGRITY</span>") != std::string::npos &&
            repairedRigPanel.find("id=\"rr-hud-mining-drill-bit-value\">35%") != std::string::npos,
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
    MiningLooseObject miningChunk;
    miningChunk.persistentId = pickupMining.nextLooseObjectId++;
    miningChunk.kind = MiningLooseObjectKind::Material;
    miningChunk.material = MiningCellMaterial::RareOre;
    miningChunk.x = miningHome.x;
    miningChunk.y = miningHome.y;
    miningChunk.mass = 1.0;
    miningChunk.cargoValue = tuning::mining::rareCargo;
    pickupMining.looseObjects.push_back(miningChunk);
    updateMiningRun(miningPickup, catalog, 0.01);
    require(
        miningAgent.haulMaterials.rare == 1 &&
            pickupMining.looseObjects.empty() &&
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
    state.run.planetaryExpedition.supply = 0;
    state.run.planetaryExpedition.cargo = 30;
    state.run.planetaryExpedition.hazard = 1.0;
    state.run.planetaryExpedition.temporaryMaterials = {.common = 5, .rare = 3, .exotic = 1};
    state.run.planetaryExpedition.temporaryArtifacts.push_back({"mars_artifact_loss", content::destination::mars, false});

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
    state.run.mining.temporaryMaterials.common = 24;
    state.run.mining.cargo = 24;
    state.run.mining.droneX = state.run.mining.returnZoneX;
    state.run.mining.droneY = state.run.mining.returnZoneY;
    require(finishMiningRun(state, catalog, false).applied,
        "returned mining ore should transfer through the normal surface-extraction handoff");
    require(state.run.planetaryExpedition.bankedMiningArenaValid &&
            state.run.planetaryExpedition.bankedMiningProgressionEligible &&
            state.run.planetaryExpedition.bankedMiningMaterials.common == 24,
        "the normal return handoff should retain mining-payload provenance for contract credit");
    require(state.meta.materials.common == 4,
        "service banking should reserve the twenty-unit Lunar contract and place only the remainder in the Ship hold");
    state.run.planetaryExpedition.supply = 0;
    state.run.planetaryExpedition.cargo = 30;
    state.run.planetaryExpedition.hazard = 1.0;

    const SurfaceActionOutcome outcome = extractSurfacePayload(state, catalog);
    require(outcome.materialReturned.common == 0,
        "departure must not report the audit ledger as a second payload transfer");
    require(state.meta.prospectorCommonOreRecovered == 20 && state.meta.materials.common == 4,
        "departure must not duplicate contract allocation or Ship inventory");
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
    state.meta.shipHoldRank = 2;
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
    state.run.crew.front().archetypeId = "capybara_endurance";
    state.run.crew.front().status = CrewStatus::Injured;
    state.run.expedition.progression.runUpgradeDraftCount = 3;
    state.run.expedition.progression.wideDrillHeadOffered = true;
    state.run.expedition.progression.sideCuttersOffered = true;
    state.run.expedition.progression.pendingGraftConflicts = {{
        0,
        {0, "active_drone", DroneModuleKind::SpectrumFilter},
        {0, "wreck_drone", DroneModuleKind::CombatDrill}}};

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
    require(restored.meta.shipHoldRank == 2, "ship hold rank should round trip");
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
    require(restored.run.crew.front().archetypeId == "capybara_endurance", "crew archetype should round trip");
    require(restored.run.crew.front().status == CrewStatus::Injured, "crew status should round trip");
    require(restored.run.expedition.progression.runUpgradeDraftCount == 3 &&
            restored.run.expedition.progression.wideDrillHeadOffered &&
            restored.run.expedition.progression.sideCuttersOffered &&
            restored.run.expedition.progression.pendingGraftConflicts.size() == 1 &&
            restored.run.expedition.progression.pendingGraftConflicts.front().recovered.primaryDroneId == "wreck_drone",
        "v23 draft guarantees and unresolved wreck graft choices should round trip");
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
        "a current campaign with recorded launch history should skip the first-flight introduction");
}


void saveSchemaConstantsMatchSerializedFields()
{
    const ContentCatalog catalog = createDefaultContent();
    require(save_schema::currentVersion == 23, "the current save schema should be version twenty-three");
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
    require(text.find(std::string(save_schema::field::runUpgradeDraftCount) + save_schema::keyValueDelimiter) != std::string::npos, "drill draft count should use a shared schema name");
    require(text.find(std::string(save_schema::field::wideDrillHeadOffered) + save_schema::keyValueDelimiter) != std::string::npos, "wide-head guarantee state should use a shared schema name");
    require(text.find(std::string(save_schema::field::sideCuttersOffered) + save_schema::keyValueDelimiter) != std::string::npos, "side-cutter guarantee state should use a shared schema name");
    require(text.find(std::string(save_schema::field::pendingGraftConflicts) + save_schema::keyValueDelimiter) != std::string::npos, "wreck graft choices should use a shared schema name");
    require(text.find(std::string(save_schema::field::droneModuleAssignments) + save_schema::keyValueDelimiter) != std::string::npos, "temporary drone grafts should use a shared schema name");
    require(text.find(std::string(save_schema::field::miningRigState) + save_schema::keyValueDelimiter) != std::string::npos, "mining rig state key should use shared schema name");
    require(text.find(std::string(save_schema::field::miningOperatorState) + save_schema::keyValueDelimiter) != std::string::npos, "mining operator state key should use shared schema name");
    require(text.find(std::string(save_schema::field::miningGravity) + save_schema::keyValueDelimiter) != std::string::npos, "mining gravity key should use shared schema name");
    require(text.find(std::string(save_schema::field::miningLooseObjects) + save_schema::keyValueDelimiter) != std::string::npos, "mining loose-chunk key should use shared schema name");
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
        "current saves must not persist the retired surface draft subflows");
    require(text.find("fieldInsight=") == std::string::npos &&
            text.find("fieldInsightAwardKeys=") == std::string::npos &&
            text.find("miningDraftsEarned=") == std::string::npos &&
            text.find("pendingFieldDraftThreshold=") == std::string::npos &&
            text.find("fieldDraftReturnScreen=") == std::string::npos,
        "current saves must not persist retired Field Insight progression");
    require(text.find("droneUpgrades=") == std::string::npos &&
            text.find("droneUpgradeCredits=") == std::string::npos,
        "version-fifteen saves must not persist retired permanent drone progression");
    require(text.find("miningFuelBurn=") == std::string::npos,
        "version-fifteen saves must not persist the retired seconds-based fuel field");
    require(text.find("approachFlybyRun=") == std::string::npos
            && text.find("approachOrbitRun=") == std::string::npos
            && text.find("approachDescentRun=") == std::string::npos
            && text.find("arrivalActive=") == std::string::npos
            && text.find("arrivalDestination=") == std::string::npos
            && text.find("arrivalTransferFuelRemaining=") == std::string::npos
            && text.find("arrivalTransferFuelCapacity=") == std::string::npos
            && text.find("approachPhase=") == std::string::npos
            && text.find("approachRewards=") == std::string::npos,
        "v18 saves must not persist retired standalone flight simulations");
    require(text.find("surfaceSharedFuel=") == std::string::npos
            && text.find("surfaceSharedFuelCapacity=") == std::string::npos,
        "v18 saves must not persist the retired shared surface reserve");
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
                std::string(save_schema::header) + "\nversion=15trailing-data\ncredits=1\n")
                .has_value(),
        "save payloads must declare the exact current version value");

    SaveData incompatible = captureSaveData(state);
    incompatible.version = save_schema::currentVersion - 1;
    require(!deserializeSaveData(serializeSaveData(incompatible)).has_value(),
        "prior-version saves must be rejected instead of partially migrated");
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


void unifiedPhysicalFlightCapturesOrbitAndResolvesTouchdown()
{
    const ContentCatalog catalog = createDefaultContent();
    const Destination* moon = catalog.findDestination(content::destination::moon);
    require(moon != nullptr, "physical flight test requires the Moon");

    PreparedLaunch launch;
    launch.config.destinationId = moon->id;
    launch.config.frontierTransfer = true;
    launch.config.missionKind = LaunchMissionKind::Standard;
    launch.fuelCapacity = 10.0;
    launch.manualControlsEnabled = true;
    launch.orbitRequired = true;
    FlightRunState orbit = beginLaunchFlight(launch, *moon);
    orbit.mode = FlightMode::Orbit;
    orbit.positionX = orbit.orbit.targetRadius;
    orbit.positionY = 0.0;
    orbit.velocityX = 0.0;
    orbit.velocityY = std::sqrt(0.095 / orbit.orbit.targetRadius);
    orbit.heading = 1.5707963267948966;
    orbit.orbit.previousAngle = 0.0;
    const double coastingFuel = orbit.fuelRemaining;
    {
        auto correcting = orbit;
        correcting.orbit.confirmationSeconds = 1.5;
        correcting.selectedThrottle = 0.2;
        FlightInput correction;
        correction.throttle = .2;
        correction.analogThrottle = true;
        updateLaunchFlight(correcting, launch, *moon, correction, 0.01);
        require(correcting.orbit.confirmationSeconds > 1.48 && correcting.orbit.confirmationSeconds < 1.5,
            "small powered corrections should decay, not erase, capture progress");
        require(!correcting.orbit.captured, "powered corrections cannot capture orbit");
        correcting.selectedThrottle = 0.0;
        correcting.velocityX = 0.0;
        correcting.velocityY = std::sqrt(0.095 / orbit.orbit.targetRadius);
        correcting.positionX = orbit.orbit.targetRadius;
        correcting.positionY = 0.0;
        const double before = correcting.orbit.confirmationSeconds;
        updateLaunchFlight(correcting, launch, *moon, {}, 0.01);
        require(correcting.orbit.confirmationSeconds > before, "safe coasting resumes confirmation");
        correcting.mode = FlightMode::Travel;
        correcting.positionX = 3.0;
        updateLaunchFlight(correcting, launch, *moon, {}, 0.01);
        require(correcting.orbit.confirmationSeconds == 0.0, "leaving Orbit clears pending capture");
    }
    LaunchFlightStep orbitStep;
    for (int index = 0; index < 2000 && !orbit.orbit.captured; ++index) {
        orbitStep = updateLaunchFlight(orbit, launch, *moon, {}, 0.01);
    }
    require(orbit.orbit.captured && orbitStep.orbitCaptured,
        "one stable physical revolution should capture orbit");
    require(nearlyEqual(orbit.fuelRemaining, coastingFuel, 0.000001),
        "coasting through orbit capture must consume no fuel");

    FlightRunState teachingApproach = beginLaunchFlight(launch, *moon);
    LaunchFlightStep teachingStep;
    double teachingSeconds = 0.0;
    bool enteredOuterCaptureCorridor = false;
    while (teachingApproach.active && teachingSeconds < 70.0) {
        teachingStep = updateLaunchFlight(
            teachingApproach,
            launch,
            *moon,
            {},
            0.01);
        teachingSeconds += 0.01;
        const double orbitError = std::abs(
            std::hypot(teachingApproach.positionX, teachingApproach.positionY) -
            teachingApproach.orbit.targetRadius);
        enteredOuterCaptureCorridor = enteredOuterCaptureCorridor ||
            orbitError <= teachingApproach.orbit.goodBand;
        if (teachingSeconds >= 25.0 && teachingSeconds < 25.01) {
            require(teachingApproach.active,
                "the opening Moon approach must leave a readable control window");
        }
    }
    require(enteredOuterCaptureCorridor,
        "an untouched opening trajectory should visibly enter the outer capture corridor");
    require(teachingStep.flyby && !teachingStep.failed,
        "an untouched opening trajectory should make a close flyby instead of auto-crashing or auto-orbiting");
    require(teachingSeconds >= 25.0,
        "the opening transfer should give a new player time to read guidance and choose a capture burn");

    FlightRunState powered = beginLaunchFlight(launch, *moon);
    const double poweredFuel = powered.fuelRemaining;
    (void)updateLaunchFlight(powered, launch, *moon, {0.0, 1.0, false}, 0.25);
    require(powered.fuelRemaining < poweredFuel,
        "main thrust should consume physical flight fuel");

    FlightRunState approachTelemetry = beginLaunchFlight(launch, *moon);
    approachTelemetry.orbit.captured = true;
    approachTelemetry.positionX = 0.50;
    approachTelemetry.positionY = 0.0;
    approachTelemetry.velocityX = -0.10;
    approachTelemetry.velocityY = 0.0;
    approachTelemetry.heading = 0.0;
    (void)updateLaunchFlight(approachTelemetry, launch, *moon, {}, 0.01);
    require(approachTelemetry.mode != FlightMode::Landing &&
            approachTelemetry.landing.altitude > 0.0 &&
            approachTelemetry.landing.verticalVelocity < 0.0,
        "physical approach telemetry must update before the local landing boundary so the camera can anticipate descent");

    FlightRunState leftTurn = beginLaunchFlight(launch, *moon);
    (void)updateLaunchFlight(leftTurn, launch, *moon, {-1.0, 0.0, false}, 0.25);
    require(leftTurn.heading > 0.0,
        "Negative steering must rotate the physical ship visibly left");
    FlightRunState rightTurn = beginLaunchFlight(launch, *moon);
    (void)updateLaunchFlight(rightTurn, launch, *moon, {1.0, 0.0, false}, 0.25);
    require(rightTurn.heading < 0.0,
        "Positive steering must rotate the physical ship visibly right");

    for (double heading : {0.0,1.5707963267948966,3.141592653589793,-1.5707963267948966}) for (double side : {-1.0,1.0}) {
        auto coast = beginLaunchFlight(launch,*moon);
        coast.mode = FlightMode::Travel;
        coast.positionX = coast.positionY = 8;
        coast.heading = heading;
        auto strafe = coast;
        updateLaunchFlight(coast,launch,*moon,{},.01);
        updateLaunchFlight(strafe,launch,*moon,{0,0,false,true,side},.01);
        const double dx=strafe.velocityX-coast.velocityX, dy=strafe.velocityY-coast.velocityY;
        require((dx*std::sin(heading)-dy*std::cos(heading))*side>0.0 &&
            std::abs(dx*std::cos(heading)+dy*std::sin(heading))<.00001 && strafe.heading==coast.heading,
            "Strafe must accelerate along ship-relative right/left without changing heading or forward thrust");
        require(strafe.fuelRemaining<coast.fuelRemaining && strafe.selectedThrottle==0.0,
            "Side thrusters consume fuel independently of the main engine");
        for (bool cut : {false,true}) {
            auto idle = beginLaunchFlight(launch,*moon);
            idle.mode=FlightMode::Travel; idle.positionX=idle.positionY=8;
            if (!cut) idle.fuelRemaining=0;
            auto blocked=idle;
            updateLaunchFlight(idle,launch,*moon,{},.01);
            updateLaunchFlight(blocked,launch,*moon,{0,0,cut,true,side},.01);
            require(blocked.velocityX==idle.velocityX && blocked.velocityY==idle.velocityY,
                "Empty fuel or engine cut must suppress lateral thrust");
        }
    }

    GameState surfaceState = createNewGame(catalog, 1931);
    const auto prepared = prepareSurfaceLanding(surfaceState, catalog, {moon->id});
    require(prepared.valid, "landing checks require the generated surface site");
    {
        auto coast=beginLaunchFlight(launch,*moon);
        coast.mode=FlightMode::Landing;
        coast.landing.heading=1.5707963267948966;
        coast.landing.altitude=30;
        auto strafe=coast;
        updateLaunchFlight(coast,launch,*moon,{},.01);
        updateLaunchFlight(strafe,launch,*moon,{0,0,false,true,1},.01);
        require(strafe.landing.lateralVelocity>coast.landing.lateralVelocity &&
            strafe.landing.heading==coast.landing.heading &&
            std::abs(strafe.landing.verticalVelocity-coast.landing.verticalVelocity)<.00001,
            "Landing strafe changes lateral velocity independently of rotation and vertical thrust");
    }
    auto touchdown = [&](double verticalVelocity) {
        FlightRunState landing = beginLaunchFlight(launch, *moon);
        landing.orbit.captured = true;
        landing.mode = FlightMode::Landing;
        landing.phase = FlightPhase::Landing;
        landing.landing.heading = 1.5707963267948966;
        landing.landing.altitude = 0.001;
        landing.landing.verticalVelocity = verticalVelocity;
        LaunchFlightStep step;
        for (int i = 0; i < 1000 && landing.active; ++i) {
            step = updateLaunchFlight(landing, launch, *moon, {}, 0.01, &prepared.miningTemplate);
        }
        return step;
    };
    require(touchdown(-0.10).safeTouchdown,
        "settled local terrain contact should land safely");
    {
        FlightRunState landing = beginLaunchFlight(launch, *moon);
        landing.orbit.captured = true;
        landing.mode = FlightMode::Landing;
        landing.phase = FlightPhase::Landing;
        landing.landing.heading = 1.5707963267948966;
        landing.landing.altitude = -0.01;
        landing.landing.verticalVelocity = 0.50;
        const auto contact = updateLaunchFlight(
            landing, launch, *moon, {}, 0.01, &prepared.miningTemplate);
        require(contact.safeTouchdown && !landing.active,
            "upright supported contact should land immediately despite slight upward drift");
    }
    {
        FlightRunState landing = beginLaunchFlight(launch, *moon);
        landing.orbit.captured = true;
        landing.mode = FlightMode::Landing;
        landing.phase = FlightPhase::Landing;
        landing.landing.heading = 1.5707963267948966 + 0.6108652381980153;
        landing.landing.altitude = 0.001;
        landing.landing.verticalVelocity = -0.10;
        const auto contact = updateLaunchFlight(
            landing, launch, *moon, {}, 0.01, &prepared.miningTemplate);
        require(landing.contactEpisode && landing.active &&
                !contact.safeTouchdown && !contact.hardTouchdown,
            "supported contact beyond the thirty-degree posture limit should rebound instead of landing");
    }
    require(touchdown(-18.0).hardTouchdown,
        "a survivable local impact should rebound and settle with visible hull damage");
    require(touchdown(-35.0).failed,
        "a local impact exceeding current hull should destroy the ship");

    FlightRunState unauthorizedLanding = beginLaunchFlight(launch, *moon);
    unauthorizedLanding.phase = FlightPhase::Landing;
    unauthorizedLanding.positionX = 0.1601;
    unauthorizedLanding.positionY = 0.0;
    unauthorizedLanding.velocityX = -0.10;
    unauthorizedLanding.velocityY = 0.0;
    unauthorizedLanding.heading = 0.0;
    const LaunchFlightStep firstVisitDirect = updateLaunchFlight(
        unauthorizedLanding,
        launch,
        *moon,
        {},
        0.01);
    require(firstVisitDirect.failed && !firstVisitDirect.safeTouchdown &&
            unauthorizedLanding.mode != FlightMode::Landing,
        "the first Moon landing must reject a physically gentle descent without warping into the landing camera before impact");

    PreparedLaunch repeatLaunch = launch;
    repeatLaunch.orbitRequired = false;
    FlightRunState repeatLanding = beginLaunchFlight(repeatLaunch, *moon);
    repeatLanding.mode = FlightMode::Orbit;
    const double gateAngle = std::atan2(flight_geometry::startY, flight_geometry::startX);
    repeatLanding.positionX = std::cos(gateAngle) * (flight_geometry::landingBoundary + 0.0001);
    repeatLanding.positionY = std::sin(gateAngle) * (flight_geometry::landingBoundary + 0.0001);
    repeatLanding.velocityX = -0.10 * std::cos(gateAngle);
    repeatLanding.velocityY = -0.10 * std::sin(gateAngle);
    (void)updateLaunchFlight(repeatLanding, repeatLaunch, *moon, {}, 0.01);
    require(repeatLanding.mode == FlightMode::Landing && repeatLanding.active,
        "repeat Moon visits should enter manual Landing through the authorized descent gate");
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
    state.run.destinationIndex = 5;
    state.meta.unlockKeys.push_back(content::unlock::routeNeptune);
    state.meta.launchLessons.stage = LaunchTrainingStage::Complete;
    state.run.routeTransit = makeRouteTransit(
        catalog,
        content::destination::uranus,
        content::destination::neptune,
        RouteTransitIntent::Outbound);
    neptuneArrival.routeTransit = state.run.routeTransit;
    applyLaunchOutcome(state, catalog, neptuneArrival);

    require(!arkDiscovered(state), "Neptune arrival must not discover the Ark before the story takeover is acknowledged");
    const ScenarioActionOutcome discovery = performScenarioAction(
        state, catalog, content::scenario::neptuneDiscovery, "arrival", ScenarioActionKind::ClaimReward);
    require(discovery.applied && discovery.transition.kind == ScenarioTransitionKind::PresentStoryTakeover,
        "successful Neptune arrival should provide an explicit Straylight story claim");
    scheduleStoryBriefing(state, discovery.transition.storyBriefing, discovery.transition.screen);
    require(state.storyBriefing.pending == StoryBriefingId::StraylightDiscovery,
        "claiming the discovery should persist the Straylight takeover");
    require(acknowledgeStoryBriefing(state, catalog), "the Straylight takeover should acknowledge once");
    require(!arkDiscovered(state) && state.storyBriefing.pending == StoryBriefingId::StraylightApproach,
        "acknowledging the contact should queue the physical Straylight rendezvous before revealing the Ark");

    state.launchConfig.destinationId = content::destination::neptune;
    state.launchConfig.frontierTransfer = true;
    state.launchConfig.missionKind = LaunchMissionKind::StraylightApproach;
    state.launchConfig.burnGoalMultiplier = catalog.findDestination(content::destination::neptune)->targetMultiplier;
    Random approachRng(62001);
    const PreparedLaunch approach = prepareLaunch(state, catalog, approachRng);
    require(!approach.manualControlsEnabled && !approach.heatEnabled && !approach.asteroidsEnabled,
        "the Straylight rendezvous should use automatic guidance with no failure mechanics");
    require(std::abs(approach.cruiseFuelCost) < 0.000001,
        "the ceremonial rendezvous should not consume transfer fuel");
    FlightRunState flight = beginLaunchFlight(
        approach, *catalog.findDestination(content::destination::neptune));
    const double startingFuel = flight.fuelRemaining;
    LaunchFlightStep step;
    for (int frame = 0; frame < 10000 && !step.reachedDestination; ++frame) {
        step = updateLaunchFlight(
            flight,
            approach,
            *catalog.findDestination(content::destination::neptune),
            {},
            0.05);
        require(!step.failed, "the ceremonial Straylight rendezvous must have no failure path");
    }
    require(step.reachedDestination, "automatic guidance should carry the ship to Straylight");
    require(std::abs(flight.fuelRemaining - startingFuel) < 0.000001,
        "the Straylight rendezvous should leave transfer fuel unchanged");
    LaunchOutcome rendezvous = resolveLaunch(
        approach,
        catalog,
        state,
        catalog.findDestination(content::destination::neptune)->targetMultiplier,
        RecoveryMethod::TransferArrival,
        approachRng,
        {true, LaunchFailureCause::None, 1.0, 0});
    applyLaunchOutcome(state, catalog, rendezvous);
    require(rendezvous.type == LaunchResultType::MissionComplete && rendezvous.payout == 0.0 &&
            rendezvous.blueprintGain == 0 && rendezvous.shipDamage == 0,
        "the no-fail story transfer should not grant mechanical rewards or damage the ship");
    scheduleStoryBriefing(state, StoryBriefingId::ActOneComplete, Screen::Hangar);
    require(acknowledgeStoryBriefing(state, catalog), "Act I completion should acknowledge explicitly");
    require(arkDiscovered(state), "boarding after the rendezvous should reveal the operable derelict Ark");
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
            state.run.expedition.progression.runDroneRanks.empty(),
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
    require(upgraded.run.expedition.progression.runDroneRanks.empty(),
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
    state.meta.unlockKeys.push_back(content::unlock::routeNeptune);
    require(recordScenarioEvent(
                state,
                catalog,
                {ScenarioEventKind::DestinationReached, {}, {}, {}, content::destination::neptune, 1, 0}),
        "the simulated Neptune arrival should emit the typed discovery event");
    const ScenarioActionOutcome discovery = performScenarioAction(
        state, catalog, content::scenario::neptuneDiscovery, "arrival", ScenarioActionKind::ClaimReward);
    require(discovery.applied && discovery.transition.kind == ScenarioTransitionKind::PresentStoryTakeover,
        "the authored Neptune discovery should claim through its typed transition");
    scheduleStoryBriefing(state, discovery.transition.storyBriefing, discovery.transition.screen);
    require(acknowledgeStoryBriefing(state, catalog), "Neptune discovery should acknowledge before the first Ark jump");
    require(!arkDiscovered(state) && state.storyBriefing.pending == StoryBriefingId::StraylightApproach,
        "Chapter 4 should remain active until the Straylight rendezvous finishes");
    scheduleStoryBriefing(state, StoryBriefingId::ActOneComplete, Screen::Hangar);
    require(acknowledgeStoryBriefing(state, catalog), "Act I completion should be acknowledged before the first Ark jump");
    require(arkDiscovered(state), "Chapter 4 should discover the Ark only after the completed rendezvous");

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
    require(state.run.planetaryExpedition.enemyEncountersEnabled, "hostile-system surface expeditions should enable enemy contact");
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


void controllerPanelDefaultsAndOrbitalActions()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 0xC017);
    Random rng(0xC017);
    const PreparedLaunch launch = prepareLaunch(state, catalog, rng);
    PanelRenderContext context{state, catalog, launch, launch};
    context.firstTimeIntroductionsEnabled = false;
    context.incomingMessageDeliveryAllowed = false;
    const auto defaultIs = [](std::string_view markup, std::string_view id) {
        require(countOccurrences(markup, "data-ui-default-focus=\"1\"") == 1,
            "an action scope must declare exactly one controller default");
        const std::string marker = "data-ui-focus-id=\"" + std::string(id) + "\" data-ui-default-focus=\"1\"";
        require(markup.find(marker) != std::string_view::npos,
            "the controller default must match the intended primary action: " + std::string(id));
    };
    const auto inspectScope = [](std::string_view markup) {
        require(countOccurrences(markup, "data-ui-default-focus=\"1\"") <= 1,
            "each rendered screen or modal must have at most one explicit default");
        std::vector<std::string> ids;
        std::size_t position = 0;
        while (position < markup.size()) {
            const auto begin = std::min(markup.find("<button", position), markup.find("<select", position));
            if (begin == std::string_view::npos) break;
            const auto end = markup.find('>', begin);
            require(end != std::string_view::npos, "controller controls must have complete opening tags");
            const auto tag = markup.substr(begin, end - begin);
            position = end + 1;
            if (tag.find(" disabled") != std::string_view::npos ||
                tag.find("data-ui-focus-skip=\"1\"") != std::string_view::npos) continue;
            constexpr std::string_view idAttribute = "data-ui-focus-id=\"";
            const auto idStart = tag.find(idAttribute);
            require(idStart != std::string_view::npos, "every enabled panel control needs a stable controller identity");
            const auto valueStart = idStart + idAttribute.size();
            const std::string id(tag.substr(valueStart, tag.find('"', valueStart) - valueStart));
            require(std::find(ids.begin(), ids.end(), id) == ids.end(),
                "controller identities must be unique within each screen or modal: " + id);
            ids.push_back(id);
        }
    };
    const auto inspectPanel = [&](const PanelDocumentPresentation& panel) {
        inspectScope(panel.contentMarkup);
        for (const auto& modal : panel.modals) inspectScope(modal.bodyMarkup);
    };

    state.screen = Screen::Flight;
    FlightRunState flight;
    flight.active = flight.physicalFlight = true;
    flight.destinationId = content::destination::moon;
    flight.mode = FlightMode::Orbit;
    flight.selectedThrottle = 0.0;
    flight.orbit.captured = true;
    flight.orbit.loopQualifies = false;
    OrbitalWorkState work;
    context.launchFlight = &flight;
    context.orbitalWork = &work;
    context.orbitalInsideZone = true;
    auto panel = buildGamePanelPresentation(context);
    defaultIs(panel.contentMarkup, "action:orbital_scan");
    require(panel.contentMarkup.find("data-ui-activation=\"continuous\"") == std::string::npos,
        "Scan must be a discrete press, not a continuous action");
    flight.selectedThrottle = 0.60;
    panel = buildGamePanelPresentation(context);
    require(panel.contentMarkup.find("action:resume_orbital_flight") == std::string::npos,
        "ordinary piloting must not offer Resume Flight when orbital work is inactive");
    require(panel.contentMarkup.find("COAST TO SCAN") != std::string::npos &&
        panel.contentMarkup.find("action:orbital_scan") == std::string::npos,
        "powered flight must remain status rather than offer Scan");
    flight.selectedThrottle = 0.0;

    work.phase = OrbitalWorkPhase::Surveying;
    panel = buildGamePanelPresentation(context);
    defaultIs(panel.contentMarkup, "action:resume_orbital_flight");
    require(panel.contentMarkup.find("SCANNING...") != std::string::npos &&
        panel.contentMarkup.find("data-rr-action=\"orbital_work\"") == std::string::npos,
        "Scanning must render as status while Resume Flight remains reachable");

    work.surveyComplete = true;
    work.phase = OrbitalWorkPhase::LaserReady;
    context.orbitalLandingEligible = true;
    context.orbitalArtifactDepth = 2;
    panel = buildGamePanelPresentation(context);
    defaultIs(panel.contentMarkup, "action:orbital_drill");
    require(panel.contentMarkup.find("Artifact at Depth +2") != std::string::npos,
        "a completed scan must state the artifact depth in the action panel");
    require(!flight.orbit.loopQualifies,
        "restored captured orbit must expose Drill and Land without transient loop requalification");
    require(panel.contentMarkup.find("data-ui-activation=\"continuous\"") != std::string::npos,
        "focused Drill must advertise continuous hold activation");
    const auto drillPosition = panel.contentMarkup.find("action:orbital_drill");
    const auto landPosition = panel.contentMarkup.find("action:land_from_orbit");
    const auto resumePosition = panel.contentMarkup.find("action:resume_orbital_flight");
    require(drillPosition < landPosition && landPosition < resumePosition,
        "orbital controller actions must follow Drill, Land, Resume Flight visual order");
    for (const bool blocked : {false, true}) {
        context.orbitalLaserComplete = !blocked;
        context.orbitalLaserBlocked = blocked;
        panel = buildGamePanelPresentation(context);
        defaultIs(panel.contentMarkup, "action:land_from_orbit");
        require(panel.contentMarkup.find(blocked ? "SURFACE TOOLS REQUIRED" : "BORE REACH EXCAVATED") != std::string::npos &&
            panel.contentMarkup.find("data-rr-action=\"orbital_work\"") == std::string::npos,
            "a completed or blocked laser must be status, never a false Drill default");
    }
    context.orbitalInsideZone = false;
    context.orbitalLandingEligible = false;
    panel = buildGamePanelPresentation(context);
    defaultIs(panel.contentMarkup, "action:resume_orbital_flight");
    require(panel.contentMarkup.find("action:land_from_orbit") == std::string::npos,
        "Land must be skipped outside its eligible wedge");
    inspectPanel(panel);
    for (const auto phase : {OrbitalWorkPhase::Inactive, OrbitalWorkPhase::Surveying,
             OrbitalWorkPhase::LaserReady, OrbitalWorkPhase::LandingAlignment}) {
        work.phase = phase;
        panel = buildGamePanelPresentation(context);
        RealtimeHudState hud;
        buildRealtimeHudState(context, hud);
        for (const auto& patch : hud.patches) {
            if (patch.elementId != "rr-hud-launch-status" && patch.elementId != "rr-orbital-status") continue;
            require(panel.contentMarkup.find("id=\"" + patch.elementId + "\"") != std::string::npos,
                "orbital HUD updates must target the active panel, including descent alignment");
        }
    }

    context.launchFlight = nullptr;
    context.orbitalWork = nullptr;
    context.titleScreenActive = true;
    context.hasSavedGame = true;
    panel = buildGamePanelPresentation(context);
    defaultIs(panel.contentMarkup, "action:continue_game");
    inspectPanel(panel);
    for (const auto& modal : panel.modals) {
        if (modal.id == "new_game_confirm") defaultIs(modal.bodyMarkup, "new-game:cancel");
        if (modal.id == "reset_save_confirm") {
            defaultIs(modal.bodyMarkup, "reset:cancel");
            require(modal.bodyMarkup.find("data-ui-activation=\"hold\" data-ui-hold-seconds=\"0.75\"") != std::string::npos,
                "destructive reset must declare its hold threshold explicitly");
        }
        if (modal.id == "settings") defaultIs(modal.bodyMarkup, "setting:resolution");
    }
    context.titleScreenActive = false;
    for (const auto screen : {Screen::Hangar, Screen::Research, Screen::DroneOps, Screen::Upgrade,
             Screen::Results, Screen::ArrivalOps, Screen::StoryBriefing}) {
        state.screen = screen;
        inspectPanel(buildGamePanelPresentation(context));
    }
    state.run.destinationIndex = 1;
    startSurfaceExpedition(state, catalog);
    inspectPanel(buildGamePanelPresentation(context));
    require(startMiningRun(state, catalog, {MiningAct::ActOne, 9, 0xC017}, false).applied,
        "controller panel audit must enter the mining screen");
    inspectPanel(buildGamePanelPresentation(context));

    state = createNewGame(catalog, 0xC018);
    state.screen = Screen::Hangar;
    state.run.expedition.travelInitialized = true;
    state.run.expedition.location.systemId = "solar";
    state.run.expedition.location.bodyId = "earth";
    state.run.expedition.location.siteId = "earth.dock";
    state.run.expedition.course.targetBodyId = "moon";
    panel = buildGamePanelPresentation(context);
    defaultIs(panel.contentMarkup, "action:expedition:depart");
    inspectPanel(panel);
    require(std::any_of(panel.modals.begin(), panel.modals.end(), [](const auto& modal) { return modal.id == "system_menu"; }),
        "the orbital dock must retain the controller pause menu");
    for (const auto& modal : panel.modals) {
        if (modal.id != "map") continue;
        defaultIs(modal.bodyMarkup, "action:expedition:preview:moon");
        require(modal.bodyMarkup.find("data-ui-focus-skip=\"1\" tabindex=\"-1\"") != std::string::npos,
            "planet image aliases must not create duplicate controller stops beside the name buttons");
    }
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
    require(catalog.findSurfaceUpgrade(content::surfaceUpgrade::coolantMist) != nullptr, "surface upgrade ids should resolve");
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
    state.meta.unlockKeys.push_back(content::unlock::routeNeptune);
    require(recordScenarioEvent(
                state,
                catalog,
                {ScenarioEventKind::DestinationReached, {}, {}, {}, content::destination::neptune, 1, 0}),
        "the outer-route fixture should emit the authored Neptune arrival event");
    const ScenarioActionOutcome discovery = performScenarioAction(
        state, catalog, content::scenario::neptuneDiscovery, "arrival", ScenarioActionKind::ClaimReward);
    require(discovery.applied && discovery.transition.kind == ScenarioTransitionKind::PresentStoryTakeover,
        "only successful Neptune arrival should make the saved Straylight reveal claimable");
    scheduleStoryBriefing(state, discovery.transition.storyBriefing, discovery.transition.screen);
    require(!arkDiscovered(state) && state.storyBriefing.pending == StoryBriefingId::StraylightDiscovery,
        "only the explicit Neptune discovery claim should queue the saved Straylight reveal");
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
    state.meta.unlockKeys.push_back(content::unlock::routeNeptune);
    require(recordScenarioEvent(
                state,
                catalog,
                {ScenarioEventKind::DestinationReached, {}, {}, {}, content::destination::neptune, 1, 0}),
        "the story fixture should emit the authored Neptune arrival event");
    const ScenarioActionOutcome discovery = performScenarioAction(
        state, catalog, content::scenario::neptuneDiscovery, "arrival", ScenarioActionKind::ClaimReward);
    require(discovery.applied && discovery.transition.kind == ScenarioTransitionKind::PresentStoryTakeover,
        "Neptune arrival should make the authored discovery claim available");
    scheduleStoryBriefing(state, discovery.transition.storyBriefing, discovery.transition.screen);
    startArrivalOps(state, neptune);
    state.screen = Screen::StoryBriefing;

    Random rng(0x5522);
    const PreparedLaunch launch = prepareLaunch(state, catalog, rng);
    PanelRenderContext context {state, catalog, launch, launch};
    const std::string html = buildGamePanelHtml(context);
    require(chapterGate(GameChapter::Breakthrough).find("Ark") == std::string_view::npos &&
            toString(LaunchMissionKind::StraylightApproach).find("Straylight") == std::string_view::npos,
        "persistent Chapter 4 and approach labels must not classify the unknown contact early");
    require(html.find("data-panel-mode=\"story-briefing\"") != std::string::npos,
        "Straylight discovery should use the dedicated full-screen story panel");
    require(html.find("SOMETHING JUST BLOCKED THE STARS") != std::string::npos &&
            html.find("The Ark") == std::string::npos &&
            html.find("<h2>Straylight</h2>") == std::string::npos,
        "the pre-boarding reveal should preserve the contact's unknown identity");
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
    require(!arkDiscovered(restored) && restored.meta.straylightDiscoveryAcknowledged &&
            restored.storyBriefing.pending == StoryBriefingId::StraylightApproach,
        "approach should persist the rendezvous phase without prematurely discovering the Ark");
    const std::optional<SaveData> approachSave = deserializeSaveData(
        serializeSaveData(captureSaveData(restored)));
    require(approachSave.has_value(), "pending Straylight rendezvous should serialize");
    GameState resumedApproach = createNewGame(catalog, 2);
    restoreSaveData(resumedApproach, catalog, *approachSave);
    require(resumedApproach.screen == Screen::StoryBriefing &&
            resumedApproach.storyBriefing.pending == StoryBriefingId::StraylightApproach,
        "reload before or during the ceremonial transfer should offer the safe approach again");
    const std::string resumedApproachHtml = buildGamePanelHtml(
        {resumedApproach, catalog, launch, launch});
    require(resumedApproachHtml.find("THE LIGHT IS STILL ON") != std::string::npos &&
            resumedApproachHtml.find("The Ark") == std::string::npos,
        "the resumable approach should retain the grand unknown-contact framing");
    scheduleStoryBriefing(resumedApproach, StoryBriefingId::ActOneComplete, Screen::Hangar);
    const std::string finaleHtml = buildGamePanelHtml(
        {resumedApproach, catalog, launch, launch});
    require(finaleHtml.find("ACT I COMPLETE") != std::string::npos &&
            finaleHtml.find("The name on the hull is STRAYLIGHT") != std::string::npos &&
            finaleHtml.find(">Board<") != std::string::npos,
        "successful rendezvous should end on an explicit Act I completion beat");
    require(acknowledgeStoryBriefing(resumedApproach, catalog) && arkDiscovered(resumedApproach) &&
            resumedApproach.screen == Screen::Hangar,
        "boarding Straylight should commit Ark discovery and enter the post-Act-I hub");
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
    auto& expedition = state.run.planetaryExpedition;
    expedition.active = true;
    state.screen = Screen::SurfaceUpgrade;
    state.run.expedition.progression.expeditionLevel = 4;
    state.run.expedition.progression.expeditionExperience = 37.5;
    state.run.expedition.progression.pendingRunUpgradeChoices = 2;
    state.run.expedition.progression.runUpgradeOffers = {{
        {RunUpgradeKind::Rig, content::surfaceUpgrade::coolantMist, 2, -1},
        {RunUpgradeKind::DroneRank, content::drone::surveyDrone, 3, -1},
        {RunUpgradeKind::DroneGraft, "pulse_strike", 0, 1}}};
    state.run.expedition.progression.runUpgradeOfferCount = 3;
    state.run.expedition.progression.runUpgradeOfferPending = true;
    state.run.expedition.progression.runUpgradeReturnScreen = Screen::Mining;
    state.run.expedition.progression.runRigUpgradeRanks = {{content::surfaceUpgrade::coolantMist, 2}};
    state.run.expedition.progression.runDroneRanks = {{content::drone::surveyDrone, 3}};
    state.run.expedition.progression.selectedSynergyIds = {"relic_pathfinder", "full_spectrum_swarm"};
    state.meta.hasEncounteredEnemy = true;
    expedition.scannerCooldownSeconds = 2.5;
    expedition.treasureMarks.push_back({4, 5, 2});
    state.run.expedition.progression.droneModuleAssignments.push_back({1, content::drone::surveyDrone, DroneModuleKind::PulseStrike});
    state.run.expedition.progression.droneModuleRuntime.push_back({1, 0.4, 1.2, {}});
    MiningMiniDroneAgent agent;
    agent.haulMaterials = {4, 2, 1};
    agent.uncreditedHaulMaterials = {3, 1, 1};
    state.run.mining.miniDrones.push_back(agent);
    const auto parsed = deserializeSaveData(serializeSaveData(captureSaveData(state)));
    require(parsed.has_value(), "secondary mining state should serialize");
    GameState restored = createNewGame(catalog, 1);
    restoreSaveData(restored, catalog, *parsed);
    const auto& restoredExpedition = restored.run.planetaryExpedition;
    require(restored.run.expedition.progression.expeditionLevel == 4 && std::abs(restored.run.expedition.progression.expeditionExperience - 37.5) < 0.001 &&
            restored.run.expedition.progression.pendingRunUpgradeChoices == 2,
        "expedition level, experience, and queued run choices should round trip");
    require(restored.run.expedition.progression.runUpgradeOfferPending && restored.run.expedition.progression.runUpgradeOfferCount == 3 &&
            restored.run.expedition.progression.runUpgradeOffers[0].kind == RunUpgradeKind::Rig &&
            restored.run.expedition.progression.runUpgradeOffers[0].definitionId == content::surfaceUpgrade::coolantMist &&
            restored.run.expedition.progression.runUpgradeOffers[1].kind == RunUpgradeKind::DroneRank &&
            restored.run.expedition.progression.runUpgradeOffers[1].targetRank == 3 &&
            restored.run.expedition.progression.runUpgradeOffers[2].kind == RunUpgradeKind::DroneGraft &&
            restored.run.expedition.progression.runUpgradeOffers[2].slotIndex == 1 &&
            restored.run.expedition.progression.runUpgradeReturnScreen == Screen::Mining &&
            restored.screen == Screen::SurfaceUpgrade,
        "the pending level-up draft and return screen should round trip");
    require(restored.run.expedition.progression.runRigUpgradeRanks.size() == 1 &&
            restored.run.expedition.progression.runRigUpgradeRanks.front().rank == 2 &&
            restored.run.expedition.progression.runDroneRanks.size() == 1 &&
            restored.run.expedition.progression.runDroneRanks.front().rank == 3 &&
            restored.run.expedition.progression.selectedSynergyIds.size() == 2,
        "temporary rig ranks, drone ranks, and selected synergies should round trip");
    require(restored.meta.hasEncounteredEnemy,
        "enemy encounter knowledge should round trip with the campaign save");
    require(restored.run.planetaryExpedition.scannerCooldownSeconds > 2.4 && restored.run.planetaryExpedition.treasureMarks.size() == 1,
        "scanner cooldown and treasure marks should round trip");
    require(restored.run.expedition.progression.droneModuleAssignments.size() == 1 && restored.run.expedition.progression.droneModuleRuntime.size() == 1,
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
    state.run.expedition.progression.runDroneRanks.push_back({content::drone::surveyDrone, 3});
    mining.active = true;
    mining.terrain.width = 12; mining.terrain.height = 12; mining.terrain.cells.resize(144);
    mining.droneX = mining.operatorX = 6.0; mining.droneY = mining.operatorY = 6.0;
    MiningEnemy enemy = createMiningEnemy(MiningEnemyType::Mammal, MiningCellFeature::EncounterZone, 6.0, 6.0);
    enemy.health = enemy.maxHealth = 20.0; mining.enemies.push_back(enemy);
    MiningMiniDroneAgent survey; survey.role = MiniDroneRole::Survey; survey.roleIndex = 0; survey.equippedFrame = 0; survey.upgradeLevel = 3; survey.x = 6.0; survey.y = 6.0; mining.miniDrones.push_back(survey);
    state.run.expedition.progression.droneModuleAssignments.push_back({0, content::drone::surveyDrone, DroneModuleKind::PulseStrike});
    state.run.expedition.progression.runRigUpgradeRanks.push_back({content::surfaceUpgrade::resonantDischarge, 3});
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
    state.run.expedition.progression.runDroneRanks.push_back({content::drone::resourceDrone, 2});
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
    state.run.expedition.progression.droneModuleAssignments.push_back({0, content::drone::resourceDrone, DroneModuleKind::TreasurePing});
    pulseMiningScanner(state, catalog);
    require(state.run.planetaryExpedition.treasureMarks.size() == 2, "Mk II Treasure Ping should mark two tiles");
    require(state.run.planetaryExpedition.treasureMarks.front().x == 3, "Treasure Ping should prioritize Rare ore");
    require(std::none_of(state.run.planetaryExpedition.treasureMarks.begin(), state.run.planetaryExpedition.treasureMarks.end(), [](const TreasureMark& mark) { return mark.x == 2 && (mark.y == 2 || mark.y == 3); }), "Treasure Ping should exclude non-normal materials");
    const auto first = state.run.planetaryExpedition.treasureMarks;
    state.run.planetaryExpedition.scannerCooldownSeconds = 0.0;
    pulseMiningScanner(state, catalog);
    require(state.run.planetaryExpedition.scannerCooldownSeconds > 3.9, "manual pulse should start the unified recharge");
    require(std::abs(mining.scannerPulseSeconds - tuning::mining::scannerPulseSeconds) < 1e-9,
        "manual pulse should use the shared 0.64-second presentation duration");
    require(tuning::mining::scannerRechargePresentationProgress(4.0) == 0.0
            && tuning::mining::scannerRechargePresentationProgress(3.36) < 1e-9
            && tuning::mining::scannerRechargePresentationProgress(0.0) == 1.0,
        "visible scanner recharge should begin after the pulse and finish with the shared cooldown");
    require(state.run.planetaryExpedition.treasureMarks.size() >= first.size(), "repeated Treasure Ping should preserve existing marks and select new tiles");
    resource.upgradeLevel = 3;
    mining.miniDrones.front().upgradeLevel = 3;
    state.run.expedition.progression.runDroneRanks.front().rank = 3;
    state.run.planetaryExpedition.treasureMarks.clear();
    state.run.planetaryExpedition.scannerCooldownSeconds = 0.0;
    pulseMiningScanner(state, catalog);
    require(state.run.planetaryExpedition.treasureMarks.size() == 3, "Mk III Treasure Ping should mark three tiles");
    state.run.expedition.progression.runDroneRanks.front().rank = 1;
    mining.miniDrones.front().upgradeLevel = 1;
    state.run.planetaryExpedition.treasureMarks.clear();
    state.run.planetaryExpedition.scannerCooldownSeconds = 0.0;
    pulseMiningScanner(state, catalog);
    require(state.run.planetaryExpedition.treasureMarks.size() == 1, "Mk I Treasure Ping should mark one tile");
    require(applyMiningTreasureMultiplier({1, 0, 0}, MiningCellMaterial::CommonOre, 2).common == 2 &&
            applyMiningTreasureMultiplier({0, 1, 0}, MiningCellMaterial::RareOre, 2).rare == 2 &&
            applyMiningTreasureMultiplier({0, 0, 1}, MiningCellMaterial::ExoticVein, 2).exotic == 1,
        "Treasure multiplier should double only normal Common and Rare payouts");
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
    state.run.planetaryExpedition.postSolarSystemId = khepri.systemId;
    state.run.planetaryExpedition.bodyId = khepri.primaryBodyId;
    state.run.mining.postSolarSystemId = khepri.systemId;
    state.run.mining.bodyId = khepri.primaryBodyId;
    state.run.mining.surfaceGeologyId = khepri.bodies.front().surfaceGeologyId;
    state.run.mining.deepGeologyId = khepri.bodies.front().deepGeologyId;
    state.run.mining.geologySeed = khepri.bodies.front().seed;
    const std::optional<SaveData> restored = deserializeSaveData(serializeSaveData(captureSaveData(state)));
    require(restored.has_value(), "post-solar save should deserialize");
    require(restored->postSolarSystemRosters.size() == 1U, "post-solar roster should persist");
    require(restored->planetaryExpedition.bodyId == khepri.primaryBodyId, "selected body should persist");
    require(restored->mining.surfaceGeologyId == state.run.mining.surfaceGeologyId,
        "active geology should persist");
    require(restored->mining.geologySeed == state.run.mining.geologySeed,
        "geology seed should persist");
}

} // namespace

void persistentExpeditionTests();
void incomingMessageTests();

void rigCompoundCollisionSweepsAndRecovery()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 92932);
    state.run.destinationIndex = 2;
    startSurfaceExpedition(state, catalog);
    prepareMiningSiteForTest(state);
    require(startMiningRun(state, catalog, {MiningAct::ActOne, 4, 92932}, false).applied,
        "surface headroom regression should start mining");
    auto& mining = state.run.mining;
    clearMiningTerrainForEvaTest(mining);
    mining.depthZone = mining.rigDepthZone = 0;
    mining.droneX = 20.0;
    mining.droneY = 4.0;
    mining.hullDirX = mining.aimDirX = 0.0;
    mining.hullDirY = mining.aimDirY = -1.0;
    const auto airspace = rig_geometry::surfaceProfile(mining, 1.0, 0.0);
    const double padY = mining.surfaceOriginBound ? mining.surfacePadY : mining.returnZoneY;
    require(nearlyEqual(airspace.minimumY,
            padY - tuning::mining::returnZoneCenterHeightCells - tuning::mining::returnZoneRadiusCells),
        "surface ceiling must align with the top of the ship drop-off radius");
    setMiningMove(state, 0.0, -1.0);
    for (int tick = 0; tick < 180; ++tick) updateMiningRun(state, catalog, 1.0 / 60.0);
    require(mining.droneY < 0.0, "actual rig movement must pass through the former row-zero ceiling");
    require(mining.droneY - rig_geometry::drillTip >= airspace.minimumY - .001,
        "upward movement must keep the drill tip inside the raised ceiling");
    const auto ascent = rig_geometry::sweep(mining.terrain, 20, 4, -1.5707963267948966,
        20, airspace.minimumY - 5, -1.5707963267948966, airspace);
    require(ascent.fraction < 1 && nearlyEqual(
        std::lerp(4.0, airspace.minimumY - 5, ascent.fraction) - rig_geometry::drillTip,
        airspace.minimumY, .003), "raised ceiling must stop the rig exactly at the ring top");
    require(rig_geometry::sweep(mining.terrain, 20, -1, 0, 20, -1, -1.5707963267948966,
        airspace).fraction == 1, "rig must turn freely above the old ceiling");
    require(rig_geometry::sweep(mining.terrain, 20, -1, 0, -5, -1, 0,
        airspace).fraction < 1, "surface airspace must retain side boundaries");
    mining.operatorMode = MiningOperatorMode::Jetpack;
    mining.operatorPresent = true;
    mining.operatorX = 20.0;
    mining.operatorY = 4.0;
    mining.rigDisabled = true;
    mining.operatorRigTethered = false;
    setMiningMove(state, 0.0, -1.0);
    for (int tick = 0; tick < 360; ++tick) updateMiningRun(state, catalog, 1.0 / 60.0);
    require(nearlyEqual(mining.operatorY - tuning::mining::operatorColliderRadiusCells,
            airspace.minimumY, .003), "EVA movement must reach the same raised ceiling as the rig");
    const double evaX = mining.operatorX;
    setMiningMove(state, 1.0, 0.0);
    for (int tick = 0; tick < 60; ++tick) updateMiningRun(state, catalog, 1.0 / 60.0);
    require(mining.operatorX > evaX + 1.0 && mining.operatorY < 0.0,
        "EVA must move horizontally above the old ceiling without snapping back");
    mining.depthZone = 1;
    require(rig_geometry::surfaceProfile(mining, 1.0, 0.0).minimumY == 0.0,
        "underground layers must retain their existing ceiling");
    MiningTerrain terrain;terrain.width=20;terrain.height=20;terrain.cells.resize(400);
    for(auto& cell:terrain.cells) cell.material=MiningCellMaterial::Empty;
    for(int y=0;y<20;++y) terrain.cells[y*20+10].material=MiningCellMaterial::HardRock;
    const auto wall=rig_geometry::sweep(terrain,6,8,0,14,8,0);
    require(wall.fraction<1 && nearlyEqual(6+8*wall.fraction+rig_geometry::drillTip,10,.003),
        "solid drill must stop at the wall and cannot tunnel through it");
    const auto turn=rig_geometry::sweep(terrain,8.5,8,-1.5707963267948966,8.5,8,0);
    require(turn.fraction<1 && turn.fraction>0,"turning must stop before the drill enters a wall");
    const auto away=rig_geometry::sweep(terrain,8.5,8,0,6,8,0);
    require(away.fraction>0,"an overlapping Rig must be able to reduce penetration");
    double x=8.5,y=8;rig_geometry::recoverOverlap(terrain,x,y,0);
    require(std::hypot(x-8.5,y-8)<=rig_geometry::bodyRadius*2+.001,"recovery must stay within one body diameter");
    require(rig_geometry::contacts(terrain,x,y,1,0).empty(),"nearby recoverable overlap must resolve to a clear pose");
    terrain.cells[7*20+7].material=MiningCellMaterial::HardRock;
    require(rig_geometry::contacts(terrain,6,6,1,0).empty(),"empty rectangle corners must not block the circular body");
    terrain.cells[7*20+7].material=MiningCellMaterial::Empty;
    terrain.cells[8*20+6].material=MiningCellMaterial::HardRock;
    require(rig_geometry::contacts(terrain,6,6,1,0).empty() &&
            !rig_geometry::contacts(terrain,6,6,1,0,{1.75,1.5}).empty(),
        "upgraded head and side-cutter dimensions must participate in Rig clearance");
    terrain.cells[8*20+6].material=MiningCellMaterial::Empty;
    terrain.cells[6*20+8].suitOnlyPassage=true;
    require(!rig_geometry::contacts(terrain,6,6,1,0).empty(),"the solid drill must respect EVA-only passage cells");
}

void parkedShipLosesExcavatedSupport()
{
    const auto catalog = createDefaultContent();
    auto state = std::make_unique<GameState>(createNewGame(catalog, 1977));
    auto& mining = state->run.mining;
    mining.active = true;
    mining.returnZoneX = 10; mining.returnZoneY = 6;
    mining.terrain.width = mining.terrain.height = 20;
    mining.terrain.cells.resize(400);
    const auto floorAt = [](MiningTerrain& terrain, int row) {
        for (int x = 0; x < terrain.width; ++x) {
            auto& cell = terrain.cells[row * terrain.width + x];
            cell.material = MiningCellMaterial::HardRock;
            cell.remainingToughness = cell.maxToughness = 1;
        }
    };
    for (auto& cell : mining.terrain.cells) cell.material = MiningCellMaterial::Empty;
    floorAt(mining.terrain, 6); floorAt(mining.terrain, 12);
    for (int i = 0; i < 30; ++i) updateMiningShipSupport(mining, 1.0 / 60);
    require(mining.returnZoneY == 6 && mining.shipFallVelocity == 0, "solid support holds the parked ship steady");
    mining.terrain.cells[6 * 20 + 10].material = MiningCellMaterial::Empty;
    updateMiningShipSupport(mining, 1.0 / 60);
    require(mining.returnZoneY == 6, "a narrow excavation must not let the full-width hull pass through a ledge");
    mining.terrain.cells[6 * 20 + 9].material = MiningCellMaterial::Empty;
    mining.terrain.cells[6 * 20 + 11].material = MiningCellMaterial::Empty;
    updateMiningShipSupport(mining, 1.0 / 60);
    require(mining.returnZoneY > 6 && mining.returnZoneY < 6.01 && mining.shipFallVelocity > 0,
        "removing all support starts a gradual gravitational fall");
    const auto save = deserializeSaveData(serializeSaveData(captureSaveData(*state)));
    require(save.has_value() && save->mining.shipFallVelocity == mining.shipFallVelocity &&
        nearlyEqual(save->mining.returnZoneY, mining.returnZoneY, 0.00001), "mid-fall position and velocity must round trip");
    auto resumed = std::make_unique<MiningRunState>(save->mining);
    for (int i = 0; i < 360; ++i) {
        updateMiningShipSupport(mining, 1.0 / 60);
        updateMiningShipSupport(*resumed, 1.0 / 60);
    }
    require(mining.returnZoneY == 12 && mining.shipFallVelocity == 0 && resumed->returnZoneY == 12,
        "the falling ship and reloaded ship settle exactly on the next floor");
    require(mining.surfacePadY == 6, "falling must preserve the original flight surface reference");
    mining.returnZoneY = 6; mining.shipFallVelocity = 500;
    updateMiningShipSupport(mining, 0.08);
    require(mining.returnZoneY == 12 && mining.shipFallVelocity == 0, "a fast fall cannot tunnel through a one-cell floor");
    for (auto& cell : mining.terrain.cells) cell.material = MiningCellMaterial::Empty;
    MiningDepthLayerState lower;
    lower.depthZone = 1; lower.terrain = mining.terrain;
    floorAt(lower.terrain, 5);
    mining.depthLayers.push_back(lower);
    mining.returnZoneY = 19; mining.shipFallVelocity = 0;
    for (int i = 0; i < 360; ++i) updateMiningShipSupport(mining, 1.0 / 60);
    require(mining.shipDepthZone == 1 && mining.returnZoneY == 5 && mining.shipFallVelocity == 0,
        "falling across a cached layer seam moves the real ship and service location");
    mining.droneX = mining.returnZoneX + tuning::mining::returnZoneCenterOffsetX;
    mining.droneY = mining.returnZoneY - tuning::mining::returnZoneCenterHeightCells;
    mining.rigDepthZone = mining.depthZone = 1;
    require(miningAtReturnZone(mining), "ship services must follow the fallen ship");
}

int main(int argc, char** argv)
{
    {
        std::vector<MiningCell> above(5 * 4);
        for (auto& cell : above) cell.material = MiningCellMaterial::HardRock;
        for (int row=0;row<4;++row) {
            auto& cell=above[row*5+2];
            cell.material=MiningCellMaterial::Empty; cell.revealed=true;
        }
        int edges=0;
        const auto edge=[&](int x0,int y0,int x1,int y1) {
            ++edges;
            require(x0==x1 && (x0==2 || x0==3) && y0<0 && y1<=0,
                "Shaft contour follows both walls above baseline without false seam caps");
        };
        forEachMiningUpperPassageEdge(2,above,5,4,edge);
        require(edges==8,"Known shaft walls remain readable over upper fog");
        edges=0;
        forEachMiningUpperPassageEdge(0,above,5,4,edge);
        require(edges==0,"Surface sky needs no upper shaft contour");
        for(auto& cell:above) cell.revealed=false;
        forEachMiningUpperPassageEdge(2,above,5,4,edge);
        require(edges==0,"Unknown open caves must not leak through fog");
        for(auto& cell:above) {cell.revealed=true;cell.material=MiningCellMaterial::CommonOre;}
        forEachMiningUpperPassageEdge(2,above,5,4,edge);
        require(edges==0,"Scanned resources must not produce through-fog outlines");
    }
    for (int depth : {1, 2, 4}) {
        require(miningUpperBoundaryFog(depth, -100.0F) == 1.0F &&
            miningUpperBoundaryFog(depth, 0.0F) == 1.0F,
            "Underground space above the baseline remains opaque regardless of scanning");
        require(miningUpperBoundaryFog(depth, 1.0F) == .5F &&
            miningUpperBoundaryFog(depth, 2.0F) == 0.0F,
            "Permanent upper fog feathers into the first two terrain rows only");
    }
    require(miningUpperBoundaryFog(0, -100.0F) == 0.0F,
        "Surface sky must not inherit the underground upper fog cap");
    parkedShipLosesExcavatedSupport();
    if (argc > 1 && std::string_view(argv[1]) == "--ship-support-only") return 0;
    if (argc > 1 && std::string_view(argv[1]) == "--flight-controls-only") {
        unifiedPhysicalFlightCapturesOrbitAndResolvesTouchdown();
        return 0;
    }
    rigCompoundCollisionSweepsAndRecovery();
    if (argc > 1 && std::string_view(argv[1]) == "--rig-geometry-only") return 0;
    try { (void)createDefaultContent(); }
    catch (const std::exception& error) { std::cerr << "Content: " << error.what() << '\n'; return 1; }
#ifdef _MSC_VER
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    {
        const auto catalog = createDefaultContent();
        const auto* cross = catalog.findMiningSite(content::miningSite::thermalLayeredRecovery);
        const auto* reinforced = catalog.findMiningSite(content::miningSite::reinforcedThermalRecovery);
        require(cross && reinforced, "both cocoon options must be authored");
        require(cross->cocoon.layers.front().offsets.size() == 4,
            "introductory cocoon retains four drill targets");
        require(cross->cocoon.version==3,"adjacent cross has a new saved definition version");
        for (const auto& offset : cross->cocoon.layers.front().offsets)
            require(std::abs(offset.x)+std::abs(offset.y)==1,"cross tiles share an artifact edge");
        const auto& shell = reinforced->cocoon.layers.front();
        require(shell.offsets.size() == 8 && shell.completionRule == cross->cocoon.layers.front().completionRule &&
            shell.requiredHazardMark == cross->cocoon.layers.front().requiredHazardMark,
            "reinforced cocoon adds locks without changing treatment rules");
        for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
            const auto count = std::count_if(shell.offsets.begin(), shell.offsets.end(),
                [=](const auto& offset) { return offset.x == x && offset.y == y; });
            require(count == ((x == 0 && y == 0) ? 0 : 1),
                "reinforced shell covers all eight neighbors exactly once, leaving the artifact intact");
        }
        for (const bool full : {false, true}) {
            auto state = createNewGame(catalog, 0x7A170);
            state.meta.unlockKeys.push_back(content::unlock::routeNeptune);
            state.meta.unlockKeys.push_back(content::unlock::droneBay);
            state.meta.droneBaySlots = 1;
            if (full) {
                state.meta.ownedDroneIds = {content::drone::miningDrone};
                state.meta.equippedDroneIds = state.meta.ownedDroneIds;
            }
            (void)recordScenarioEvent(state, catalog,
                {ScenarioEventKind::DestinationReached, {}, {}, {}, "neptune", 1, 0});
            require(ownedMiniDroneCount(state, content::drone::attackDrone) == 0,
                "orbital arrival must not grant the stowed drone");
            require(recordScenarioEvent(state, catalog,
                {ScenarioEventKind::SurfaceLanded, {}, {}, {}, "triton", 1, 0}),
                "Triton landing must grant the stowed drone");
            require(ownedMiniDroneCount(state, content::drone::attackDrone) == 1 &&
                equippedMiniDroneCount(state, content::drone::attackDrone) == (full ? 0 : 1),
                "landing grants one attack frame and preserves occupied bays");
            require(!hasUnlock(state.meta, content::unlock::perimeterDrones),
                "introductory attack reward must not unlock the later combat suite");
            const auto saved = deserializeSaveData(serializeSaveData(captureSaveData(state)));
            require(saved.has_value(), "Triton reward saves successfully");
            auto restored = createNewGame(catalog, 17);
            restoreSaveData(restored, catalog, *saved);
            require(!recordScenarioEvent(restored, catalog,
                {ScenarioEventKind::SurfaceLanded, {}, {}, {}, "triton", 1, 0}) &&
                ownedMiniDroneCount(restored, content::drone::attackDrone) == 1,
                "repeated landings and reload must not duplicate or remove the drone");
        }
        for (int sector = 1; sector <= 6; ++sector) {
            auto state = createNewGame(catalog, 0x7710 + sector);
            state.meta.unlockKeys.push_back(content::unlock::routeNeptune);
            state.run.expedition.travelInitialized = true;
            state.run.expedition.location.bodyId = "triton";
            const auto* mission = solarMissionForBody(catalog, "triton");
            require(mission && acceptSolarMission(state, catalog, *mission).accepted, "accept Triton mission");
            SurfaceLandingBuildRequest request;
            request.destinationId = "neptune";
            request.bodyId = "triton";
            request.zoneId = "zone_" + std::to_string(sector);
            auto prepared = prepareSurfaceLanding(state, catalog, request);
            require(prepared.valid, "prepare Triton patrol site");
            int guards = 0;
            const auto check = [&](const auto& layer) {
                for (const auto& enemy : layer.enemies) {
                    require(enemy.type == MiningEnemyType::Flying && enemy.maxHealth == 4.0 && !enemy.elite,
                        "Triton uses easy flying drones, not later organic enemies");
                    require(layer.artifact.present, "security patrol stays with the artifact depth");
                    ++guards;
                }
            };
            check(prepared.miningTemplate);
            for (const auto& layer : prepared.miningTemplate.depthLayers) check(layer);
            require(guards == 3, "each Triton artifact site has exactly three defenders");
        }
    }
#ifdef _MSC_VER
    // A failed guard must fail CI visibly, not wait on a native assertion dialog.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
#endif
    if (argc > 1 && std::string_view(argv[1]) == "--expedition-only") {
        persistentExpeditionTests();
        std::cout << "Expedition checks passed\n";
        return 0;
    }
    try { persistentExpeditionTests(); }
    catch (const std::exception& error) { std::cerr << "Expedition: " << error.what() << '\n'; return 1; }
    try { incomingMessageTests(); }
    catch (const std::exception& error) { std::cerr << "Incoming messages: " << error.what() << '\n'; return 1; }
    launchThermalManagementIsPlayerDriven();
    launchAsteroidsAreDeterministicFairAndHullScaled();
    sharedFlightInstrumentPresentationMatchesEachMode();
    emergencyRecruitmentPreventsDeadRosterSoftLock();
    emergencyRecruitmentOffersAnimalCandidateChoice();
    moduleOffersAreOneChoiceRefits();
    refitRerollsSpendAndEscalate();
    specialShipComponentsRequireRecoveredMaterials();
    preMiningRefitOffersAvoidMaterialCosts();
    shipModuleProgressSurvivesDestroyedVehicles();
    totaledShipCanAlwaysReachSalvageRepair();
    lowCreditRefitWindowIncludesAffordableOffer();
    researchPhasesUnlockOnlyAfterMarsArrival();
    researchProjectsGenerateAndCompleteFromSharedRules();
    materialResearchUnlocksModuleFamilies();
    artifactInsightImprovesFutureResearch();
    researchFacilitiesImproveFutureResearch();
    artifactResearchIdentifiesRecoveredArtifacts();
    animalCrewClassesModifySurfaceExpeditions();
    expeditionExperienceQueuesDistinctSelectableOffers();
    exhaustedRunUpgradePoolConsumesQueuedChoices();
    combatRunUpgradesWaitForFirstEnemyEncounter();
    droneGraftOffersAreDistinctPerCompatibleSlot();
    postExtractionLevelUpDraftRestoresWithoutSurfaceRuntime();
    selectedSurfaceUpgradesModifyMiningAndSurfaceStats();
    runUpgradesSurviveEmergencyRecall();
    runUpgradeLifetimeFollowsTheTransport();
    miningShipServiceRestoresOxygenWithoutEndingRun();
    droneBayUnlocksSlotsLoadoutsAndMiningEffects();
    scenarioUiActionsDoNotAwardExpeditionExperience();
    solarMissionAcceptanceUsesAuthoredActionsAndPreservesLiveLoadouts();
    scenarioAndCocoonStateRoundTrips();
    activeFlightRoundTripsThroughSave();
    surfaceMiningUsesRigFuelAndRunsOnce();
    miningArtifactTetherAndDestructionRules();
    miningArtifactRewardsResolveOnExtraction();
    miningArtifactSaveRoundTrips();
    poiGuidancePrioritizesSafetyAndTracksRecoverableArtifacts();
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
    supportDronesPrioritizeArtifactWork();
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
    drillPowerUpgradesProduceMeasuredCuttingGains();
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
    miningOxygenReservesDrainIndependentlyAndShipServicesBoth();
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
    unifiedPhysicalFlightCapturesOrbitAndResolvesTouchdown();
    flightProgressHelpersShareTravelAndReturnMath();
    hostileNavigationSelectsShuttleSortie();
    arkCampaignStateRoundTripsThroughSave();
    controllerPanelDefaultsAndOrbitalActions();
    structuredPanelPresentationCarriesTypedModalPolicy();
    contentIdsResolveAgainstDefaultCatalog();
    miningThermalCutoffAndGuidanceAreExplicit();
    marsMiningPressureFitsOxygenWindow();
    secondaryMiningStateRoundTrips();
    secondaryPulseUsesUnifiedCooldownAndStrongestHit();
    treasurePingMarksRareFirstAndSkipsExcludedMaterials();
    postSolarBodiesAndGeologiesAreDeterministicAndPersistent();

    beltWarningFollowsTravelDirection();
    std::cout << "rocket_core_tests passed\n";
    return 0;
}
