#include "core/ExpeditionSystem.h"
#include "core/ContentIds.h"
#include "core/ScenarioSystem.h"
#include "core/ResearchSystem.h"
#include "core/SolarProgression.h"
#include <algorithm>
#include <cmath>

namespace rocket {
FlightGuidance expeditionGuidance(const GameState& state, bool surveyed, bool laserComplete) {
    const auto& e = state.run.expedition;
    const auto& f = state.run.flight;
    const auto& system = solarSystemDefinition();
    FlightGuidance g;
    const auto* frame = e.location.frame == CoordinateFrame::Body ? systemBody(system,e.location.bodyId) : nullptr;
    const auto* target = systemBody(system,e.course.targetBodyId);
    const SystemVector offset = frame ? frame->position : SystemVector{};
    if (target) {
        const SystemVector destination = systemNavigationPosition(*target);
        g.targetId = target->id;
        g.targetName = target->dock ? target->name + " Dock" : target->name;
        g.targetPosition = {destination.x-offset.x,destination.y-offset.y};
        const double dx=g.targetPosition.x-f.positionX,dy=g.targetPosition.y-f.positionY;
        g.targetDistance=std::hypot(dx,dy); g.targetBearing=std::atan2(dy,dx);
    }
    const auto* orbit = frame && !frame->dock ? frame : target;
    if (orbit && !orbit->siteId.empty() && !orbit->dock) {
        g.orbitBodyId = orbit->id;
        g.orbitPosition = {orbit->position.x-offset.x,orbit->position.y-offset.y};
    }
    g.predictedImpact = f.predictedImpact;
    if (earthLaunchReady(e)) g.nextAction = "Launch from Earth / " + g.targetName + " ahead";
    else if (e.undockReady) g.nextAction = "Thrust to undock";
    else if (f.courseNoticeSeconds > 0) g.nextAction = e.cruise.active ? "Waypoint set / CRUISE ACTIVE" : "Waypoint set / manual flight";
    else if (f.mode == FlightMode::Landing) g.nextAction = f.landing.departureActive ? "Climb clear of the surface" : "Control descent and touch down";
    else if (frame && frame->id == "earth" && f.positionX*f.velocityX+f.positionY*f.velocityY > 0)
        g.nextAction = "Climb away from Earth / follow your " + g.targetName + " marker";
    else if (f.predictedImpact) g.nextAction = "Brake or turn: predicted impact";
    else if (frame && f.orbit.captured && g.orbitBodyId == frame->id) g.nextAction = laserComplete ? "Align with the landing gate, then Land" : surveyed ? "Use the orbital laser to prepare your landing" : "Use Pulse Survey to inspect a landing site";
    else if (frame && g.orbitBodyId == frame->id) g.nextAction = "Shape your trajectory into the orbit bands";
    else if (target && target->dock) g.nextAction = "Approach the dock marker and slow to dock";
    else g.nextAction = target ? "Approach " + target->name + " / establish orbit" : "Plot a destination or fly manually";
    return g;
}
namespace {
bool prepareEarthOpening(GameState& state, const ContentCatalog& catalog) {
    auto& e = state.run.expedition;
    e.location = {"solar", "earth", CoordinateFrame::Body, earthLaunchPosition(), {}, 0, "earth.launch"};
    const auto model = expeditionFlightModel(state,catalog);
    auto flight = beginLaunchFlight(model,expeditionEnvironment(state,catalog));
    restoreSystemLocation(e.location,flight);
    flight.active = false;
    flight.heat = 0;
    flight.mode = FlightMode::Orbit;
    flight.predictedTrajectory.clear();
    flight.predictedImpact = false;
    state.run.flight = std::move(flight);
    e.active = false;
    e.openingInitialized = e.departureHistoryKnown = true;
    e.departureCount = 0;
    e.course.targetBodyId = "moon";
    e.cruise.active = false;
    state.screen = Screen::Flight;
    state.statusLine = "Ready at Earth. Select Launch to begin your Moon flight.";
    // Keep the stable occurrence ID so existing acknowledgements remain valid.
    enqueueIncomingMessage(state.incomingMessages,catalog,{"campaign.lunar_approach","lunar_approach","default"});
    return true;
}
}
bool beginEarthOpening(GameState& state, const ContentCatalog& catalog) {
    auto& e = state.run.expedition;
    if (!e.travelInitialized || e.openingInitialized || e.undockReady || state.screen != Screen::Hangar ||
        !operationalHomeDocked(e) || e.location.bodyId != "earth" || e.departureCount ||
        !e.sites.empty() || !e.wrecks.empty() || e.nextWreckId != 1 || state.run.launchesThisExpedition ||
        state.run.planetaryExpedition.active || state.run.mining.active || state.run.credits != 0 ||
        state.meta.shipsLost || state.meta.furthestTier ||
        state.meta.prospectorCommonOreRecovered || state.meta.lunarProspectorClaimed ||
        state.meta.droneBaySlots || !state.meta.artifacts.empty() || !state.meta.miningSites.empty()) return false;
    if (e.cargo.credits || e.cargo.materials.common || e.cargo.materials.rare || e.cargo.materials.exotic || state.meta.blueprintProgress ||
        std::any_of(state.meta.unlockKeys.begin(),state.meta.unlockKeys.end(),[](const auto& key){return key != content::unlock::starter;})) return false;
    for (auto kind : {LaunchUpgradeKind::FuelTanks,LaunchUpgradeKind::FlightControls,LaunchUpgradeKind::Cooling,LaunchUpgradeKind::Hull})
        if (launchUpgradeRank(state,kind)>0) return false;
    for (auto kind : {SurfaceDepthUpgradeKind::SurveyArray,SurfaceDepthUpgradeKind::BoreSystem})
        if (surfaceDepthUpgradeRank(state,kind)>0) return false;
    for (const auto* counts : {&state.meta.destinationAttempts,&state.meta.destinationSuccesses,&state.meta.destinationFlybys,&state.meta.destinationOrbits,&state.meta.destinationLandings})
        if (std::any_of(counts->begin(),counts->end(),[](int count){return count != 0;})) return false;
    if (std::any_of(e.discoveredBodies.begin(),e.discoveredBodies.end(),[](const auto& id){ return id != "earth"; })) return false;
    for (const auto& scenario : state.meta.scenarios) {
        if (scenario.completed || !scenario.awardedRewardIds.empty()) return false;
        for (const auto& step : scenario.steps)
            if (step.progress || step.completed || step.claimed || step.activityStarted || step.failureSeen) return false;
    }
    for (const auto& b : e.batteries) if (b.discovered || b.researchEarned || b.owner != BatteryOwner::Site) return false;
    const auto& existingFlight = state.run.flight;
    if (existingFlight.active || existingFlight.elapsedSeconds != 0 || existingFlight.hullDamageTaken ||
        std::abs(existingFlight.fuelRemaining-launchFuelCapacity(state)) > 1e-8 ||
        std::abs(existingFlight.hullRemaining-existingFlight.hullMaximum) > 1e-8 ||
        std::hypot(e.location.velocity.x,e.location.velocity.y) > 1e-8) return false;
    return prepareEarthOpening(state,catalog);
}
bool openingMissionRetryEligible(const GameState& state) {
    const auto& e=state.run.expedition;
    const auto empty=[](const ExpeditionCargo& c) {
        return c.materials.common==0 && c.materials.rare==0 && c.materials.exotic==0 &&
            c.credits==0 && c.shipPropellant==0 && c.shipRepair==0;
    };
    return e.travelInitialized && e.openingInitialized && e.departureCount==1 && e.homeBodyId=="earth" &&
        e.sites.empty() && !state.run.mining.active && !state.meta.lunarProspectorClaimed &&
        state.meta.prospectorCommonOreRecovered==0 && empty(e.cargo) &&
        std::all_of(state.meta.destinationLandings.begin(),state.meta.destinationLandings.end(),[](int n){return n==0;}) &&
        std::all_of(e.wrecks.begin(),e.wrecks.end(),[&](const auto& w){return empty(w.cargo) && !w.buildRecoverable;}) &&
        std::all_of(e.batteries.begin(),e.batteries.end(),[](const auto& b){return b.owner==BatteryOwner::Site && !b.researchEarned;});
}
static std::string_view openingRetrySequenceVariant(const GameState& state) {
    const auto& acknowledged=state.run.expedition.decision.acknowledgedIds;
    const auto seen=[&](std::string_view id) {
        return std::find(acknowledged.begin(),acknowledged.end(),id)!=acknowledged.end();
    };
    if (seen("opening_retry:crater")) return "tips";
    if (seen("opening_retry:tips")) return "crater";
    return seen("opening_retry:default") ? "tips" : "default";
}
std::string_view openingRetryMessageVariant(const GameState& state) {
    return state.run.flight.failureCause == LaunchFailureCause::ThermalRunaway
        ? "heat_tips" : openingRetrySequenceVariant(state);
}
ExpeditionResult retryOpeningMission(GameState& state, const ContentCatalog& catalog) {
    auto& e=state.run.expedition;
    if (!openingMissionRetryEligible(state) || state.screen != Screen::Flight ||
        e.active || state.run.flight.active || e.decision.pendingId != "opening_retry")
        return ExpeditionResult::InvalidState;
    const std::string acknowledgement="opening_retry:"+std::string(openingRetrySequenceVariant(state));
    if (std::find(e.decision.acknowledgedIds.begin(),e.decision.acknowledgedIds.end(),acknowledgement)==e.decision.acknowledgedIds.end())
        e.decision.acknowledgedIds.push_back(acknowledgement);
    e.progression={};
    e.decision.pendingId.clear();
    e.decision.awaitingAscent=false;
    e.undockReady=false;
    state.run.shipDamage=0;
    state.run.planetaryExpedition={};
    prepareEarthOpening(state,catalog);
    return launchEarthOpening(state,catalog);
}
bool earthLaunchReady(const PersistentExpeditionState& e) {
    return e.travelInitialized && e.openingInitialized && !e.active && e.departureCount == 0 &&
        e.location.frame == CoordinateFrame::Body && e.location.bodyId == "earth" && e.location.siteId == "earth.launch";
}
ExpeditionResult launchEarthOpening(GameState& state, const ContentCatalog& catalog) {
    auto& e = state.run.expedition;
    auto& f = state.run.flight;
    if (!earthLaunchReady(e) || f.active || f.fuelRemaining <= 0 || f.hullRemaining <= 0)
        return ExpeditionResult::InvalidState;
    e.active = e.departureHistoryKnown = true;
    e.departureCount = 1;
    e.location.siteId.clear();
    f.active = true;
    // The opening launch supplies the authored departure impulse once. Later
    // dock departures continue to release only under ordinary player thrust.
    f.velocityX = earthLaunchSpeed;
    f.velocityY = 0;
    captureSystemLocation(e.location,f);
    refreshExpeditionTrajectory(e,f,expeditionFlightModel(state,catalog),expeditionEnvironment(state,catalog),solarSystemDefinition());
    state.statusLine = "Earth departure. Thrust toward the Moon, then shape your orbit.";
    return ExpeditionResult::Applied;
}
bool operationalHomeDocked(const PersistentExpeditionState& e) {
    return !e.active && e.location.siteId == e.location.bodyId + ".dock" &&
        (e.location.bodyId == "earth" || (e.location.bodyId == "straylight" && e.arkActivated));
}
const Destination& expeditionEnvironment(const GameState& state, const ContentCatalog& catalog) {
    const auto* body = systemBody(solarSystemDefinition(), state.run.expedition.location.bodyId);
    if (body) if (const auto* environment = catalog.findDestination(body->environmentId)) return *environment;
    // An environment profile supplies tuning only; it never places the ship.
    return *catalog.findDestination(content::destination::moon);
}
PreparedLaunch expeditionFlightModel(const GameState& state, const ContentCatalog& catalog) {
    PreparedLaunch model;
    model.config = state.launchConfig;
    model.config.frontierTransfer = true;
    model.config.missionKind = LaunchMissionKind::Standard;
    model.config.destinationId = expeditionEnvironment(state, catalog).id;
    model.routeProfileDestinationId = model.config.destinationId;
    model.fuelCapacity = launchFuelCapacity(state);
    model.flightControlRank = launchUpgradeRank(state, LaunchUpgradeKind::FlightControls);
    model.coolingRank = launchUpgradeRank(state, LaunchUpgradeKind::Cooling);
    model.hullRank = launchUpgradeRank(state, LaunchUpgradeKind::Hull);
    model.heatEnabled = true;
    model.heatGraceMultiplier = (earthLaunchReady(state.run.expedition) || openingMissionRetryEligible(state)) ? 4.0 : 1.0;
    model.orbitRequired = true;
    model.manualControlsEnabled = true;
    return model;
}
bool initializeLiveExpedition(GameState& state, const ContentCatalog& catalog) {
    auto& e = state.run.expedition;
    if (e.travelInitialized) return e.location.systemId == "solar" &&
        (e.location.frame == CoordinateFrame::System || systemBody(solarSystemDefinition(), e.location.bodyId));
    if (hostileSystemActive(state) || state.meta.ark.firstJumpComplete) return false;
    auto& f = state.run.flight;
    const bool surface = f.landing.siteCommitted;
    if (surface || (state.screen == Screen::Flight && f.physicalFlight)) {
        const std::string id = surface ? state.run.mining.destinationId : f.destinationId;
        const auto* body = bodyForEnvironment(solarSystemDefinition(), id);
        if (!body) return false;
        e.location = {"solar", body->id, CoordinateFrame::Body, {}, {}, f.heading,
            surface ? body->siteId + ":zone_1" : ""};
        captureSystemLocation(e.location, f);
        e.active = true;
        e.rigFuel = state.run.mining.rigFuel;
        if (surface) storeVisitedSite(state, e.location.siteId);
    } else if (state.screen == Screen::Hangar && !state.run.planetaryExpedition.active && currentDestination(state, catalog).hiddenFromProgression) {
        const auto* earth = systemBody(solarSystemDefinition(), "earth");
        e.location = {"solar", "earth", CoordinateFrame::Body, {systemDockPosition(*earth).x-earth->position.x, systemDockPosition(*earth).y-earth->position.y}, {}, 0, "earth.dock"};
        // Only an existing home state creates/services a ship. Mid-mission state never comes here.
        f = beginLaunchFlight(expeditionFlightModel(state, catalog), expeditionEnvironment(state, catalog));
        restoreSystemLocation(e.location, f);
        f.active = false;
        e.active = false;
        e.rigFuel.capacity = e.rigFuel.current = tuning::research::expeditionRigPackFuel + launchFuelCapacity(state);
    } else return false;
    e.travelInitialized = true;
    e.course.targetBodyId = "moon";
    e.discoveredBodies = {"earth", e.location.bodyId};
    return true;
}
void recordExpeditionArrival(GameState& state, const ContentCatalog& catalog, const LaunchOutcome& outcome) {
    auto& e = state.run.expedition;
    if (!e.travelInitialized || state.run.flight.phase != FlightPhase::Landed || outcome.type != LaunchResultType::MissionComplete) return;
    const auto* body = systemBody(solarSystemDefinition(), e.location.bodyId);
    if (!body || !body->authoredObjectives || body->dock || body->siteId.empty()) return;
    if (std::any_of(e.sites.begin(),e.sites.end(),[&](const auto& site) { return site.systemId==e.location.systemId && site.siteId==body->siteId; })) return;
    e.cargo.credits += std::max(0.0, outcome.payout-outcome.recoveryCost);
    recordScenarioEvent(state,catalog,{ScenarioEventKind::DestinationReached,{},{},{},body->environmentId,1,0});
}
ExpeditionResult departHome(GameState& state, const ContentCatalog&) {
    auto& e = state.run.expedition;
    auto& f = state.run.flight;
    if (!e.travelInitialized || !operationalHomeDocked(e) || f.active)
        return ExpeditionResult::NotDocked;
    if (e.undockReady) return ExpeditionResult::AlreadyApplied;
    if (f.fuelRemaining <= 0 || f.hullRemaining <= 0)
        return ExpeditionResult::InvalidState;
    e.undockReady = true;
    f.physicalFlight = true;
    f.mode = FlightMode::Orbit;
    f.phase = FlightPhase::Transfer;
    f.failureCause = LaunchFailureCause::None;
    f.landing = {};
    f.orbit = {};
    f.selectedThrottle = f.angularVelocity = f.burnRatePerSecond = 0;
    state.screen = Screen::Flight;
    return ExpeditionResult::Applied;
}
ExpeditionResult recoverExpedition(GameState& state, const SystemDefinition& system) {
    auto& e = state.run.expedition;
    if (!e.active) return ExpeditionResult::AlreadyApplied;
    if (openingMissionRetryEligible(state)) {
        e.active=false;
        e.cruise.active=false;
        e.undockReady=false;
        state.run.flight.active=false;
        state.run.flight.selectedThrottle=state.run.flight.angularVelocity=state.run.flight.burnRatePerSecond=0;
        e.decision.pendingId="opening_retry";
        e.decision.awaitingAscent=false;
        state.screen=Screen::Flight;
        state.statusLine="Launch attempt ended. Try the Moon approach again.";
        return ExpeditionResult::Applied;
    }
    if (!e.location.siteId.empty() && !e.location.siteId.ends_with(".dock")) storeVisitedSite(state, e.location.siteId);
    const auto result = loseExpedition(e, state.run.flight, system);
    if (result != ExpeditionResult::Applied) return result;
    ++state.meta.shipsLost;
    state.run.flight.landing = {};
    state.run.flight.orbit = {};
    state.run.mining = {};
    state.run.planetaryExpedition = {};
    state.run.approach = {};
    state.run.routeTransit = {};
    state.run.pendingTransferAssist = {};
    state.run.nextLaunchFuelBoost = state.run.nextLaunchSpeedBoost = state.run.nextLaunchInstabilityPenalty = 0;
    state.run.shipDamage = 0;
    e.decision.pendingId.clear();
    e.decision.awaitingAscent = false;
    state.screen = Screen::Hangar;
    state.statusLine = "Replacement ready at Earth. Upgrades survive docking. Recover your wreck to reclaim lost upgrades.";
    return result;
}
std::string recommendedExpeditionLead(const GameState& state, const ContentCatalog& catalog) {
    if (const auto* mission = nextSolarMission(state, catalog)) return mission->bodyId;
    return arkDiscovered(state) ? "straylight" : "moon";
}
void queueExpeditionDecision(GameState& state, const ContentCatalog& catalog) {
    auto& e = state.run.expedition;
    const auto& m = state.run.mining;
    if (!e.travelInitialized || !e.active) return;
    if (solarMissionForBody(catalog, e.location.bodyId) != nullptr) return;
    const bool artifact = m.artifact.present && m.artifact.state == MiningArtifactState::Delivered;
    const auto* body = systemBody(solarSystemDefinition(), e.location.bodyId);
    if (!body || !body->authoredObjectives) return;
    bool contract = false;
    for (const auto& instance : state.meta.scenarios) {
        const auto* definition = scenarioDefinitionForRuntimeId(state, catalog, instance.id);
        if (!definition || definition->destinationId != body->environmentId) continue;
        for (const auto& step : definition->steps) {
            const auto* progress = findScenarioStepProgress(instance, step.id);
            if (step.completionEvent == ScenarioEventKind::SafeMaterialDelivered && progress && progress->completed) contract = true;
        }
    }
    if (!contract && !artifact) return;
    const auto id = e.location.siteId + (artifact ? ".recovery" : ".contract");
    if (std::find(e.decision.acknowledgedIds.begin(), e.decision.acknowledgedIds.end(), id) != e.decision.acknowledgedIds.end()) return;
    if (e.decision.pendingId.empty()) e.decision.pendingId = id;
    e.decision.awaitingAscent = true;
}
bool acknowledgeExpeditionDecision(GameState& state, std::string_view occurrence) {
    auto& d = state.run.expedition.decision;
    if (occurrence.empty() || d.pendingId != occurrence || d.awaitingAscent) return false;
    d.acknowledgedIds.push_back(d.pendingId);
    d.pendingId.clear();
    return true;
}
} // namespace rocket
