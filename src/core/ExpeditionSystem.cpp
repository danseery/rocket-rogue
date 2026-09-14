#include "core/ExpeditionSystem.h"
#include "core/ContentIds.h"
#include "core/ResearchSystem.h"
#include "core/PayloadTransfer.h"
#include "core/SolarProgression.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rocket
{
namespace
{
constexpr double dockRange = .65;
constexpr double rendezvousSpeed = expeditionDockSpeed;
double distance(SystemVector a, SystemVector b) { return std::hypot(a.x - b.x, a.y - b.y); }
SystemLocation absolute(const SystemLocation &p, const SystemDefinition &s)
{
    return convertSystemFrame(p, CoordinateFrame::System, "", s);
}
bool atDock(const PersistentExpeditionState &e, std::string_view body)
{
    return e.location.bodyId == body && e.location.siteId == std::string(body) + ".dock";
}

void mergeRecoveredBuild(ExpeditionProgressionState& active, ExpeditionProgressionState recovered)
{
    if (recovered.expeditionLevel > active.expeditionLevel ||
        (recovered.expeditionLevel == active.expeditionLevel &&
         recovered.expeditionExperience > active.expeditionExperience)) {
        active.expeditionLevel = recovered.expeditionLevel;
        active.expeditionExperience = recovered.expeditionExperience;
    }
    active.pendingRunUpgradeChoices += std::max(0, recovered.pendingRunUpgradeChoices);
    active.runUpgradeDraftCount = std::max(active.runUpgradeDraftCount, recovered.runUpgradeDraftCount);
    active.wideDrillHeadOffered = active.wideDrillHeadOffered || recovered.wideDrillHeadOffered;
    active.sideCuttersOffered = active.sideCuttersOffered || recovered.sideCuttersOffered;
    for (const auto& recoveredRank : recovered.runRigUpgradeRanks) {
        auto found = std::find_if(active.runRigUpgradeRanks.begin(), active.runRigUpgradeRanks.end(),
            [&](const auto& rank) { return rank.upgradeId == recoveredRank.upgradeId; });
        if (found == active.runRigUpgradeRanks.end()) active.runRigUpgradeRanks.push_back(recoveredRank);
        else found->rank = std::clamp(std::max(found->rank, recoveredRank.rank), 0, 3);
    }
    for (const auto& recoveredRank : recovered.runDroneRanks) {
        auto found = std::find_if(active.runDroneRanks.begin(), active.runDroneRanks.end(),
            [&](const auto& rank) { return rank.droneId == recoveredRank.droneId; });
        if (found == active.runDroneRanks.end()) active.runDroneRanks.push_back(recoveredRank);
        else found->rank = std::clamp(std::max(found->rank, recoveredRank.rank), 1, 3);
    }
    for (const std::string& synergy : recovered.selectedSynergyIds)
        if (std::find(active.selectedSynergyIds.begin(), active.selectedSynergyIds.end(), synergy) == active.selectedSynergyIds.end())
            active.selectedSynergyIds.push_back(synergy);
    const auto restoreGraft = [&](const auto& recoveredGraft) {
        auto current = std::find_if(active.droneModuleAssignments.begin(), active.droneModuleAssignments.end(),
            [&](const auto& graft) { return graft.equippedFrame == recoveredGraft.equippedFrame; });
        if (current == active.droneModuleAssignments.end()) {
            active.droneModuleAssignments.push_back(recoveredGraft);
        } else if (current->module != recoveredGraft.module ||
                   current->primaryDroneId != recoveredGraft.primaryDroneId) {
            const bool alreadyPending = std::any_of(active.pendingGraftConflicts.begin(), active.pendingGraftConflicts.end(),
                [&](const auto& conflict) {
                    return conflict.equippedFrame == recoveredGraft.equippedFrame &&
                        conflict.recovered.module == recoveredGraft.module &&
                        conflict.recovered.primaryDroneId == recoveredGraft.primaryDroneId;
                });
            if (!alreadyPending)
                active.pendingGraftConflicts.push_back({recoveredGraft.equippedFrame, *current, recoveredGraft});
        }
    };
    for (const auto& graft : recovered.droneModuleAssignments) restoreGraft(graft);
    for (const auto& conflict : recovered.pendingGraftConflicts) restoreGraft(conflict.recovered);
    active.droneModuleRuntime.clear();
    active.runUpgradeOffers = {};
    active.runUpgradeOfferCount = 0;
    active.runUpgradeOfferPending = false;
}

bool validRecoveredBuild(const ExpeditionProgressionState& build)
{
    if (build.expeditionLevel < 1 || !std::isfinite(build.expeditionExperience) ||
        build.expeditionExperience < 0.0 || build.pendingRunUpgradeChoices < 0 ||
        build.runUpgradeDraftCount < 0) return false;
    for (std::size_t i = 0; i < build.runRigUpgradeRanks.size(); ++i) {
        const auto& rank = build.runRigUpgradeRanks[i];
        if (rank.upgradeId.empty() || rank.rank < 1 || rank.rank > 3) return false;
        if (std::any_of(build.runRigUpgradeRanks.begin(), build.runRigUpgradeRanks.begin() + static_cast<std::ptrdiff_t>(i),
            [&](const auto& prior) { return prior.upgradeId == rank.upgradeId; })) return false;
    }
    for (std::size_t i = 0; i < build.runDroneRanks.size(); ++i) {
        const auto& rank = build.runDroneRanks[i];
        if (rank.droneId.empty() || rank.rank < 1 || rank.rank > 3) return false;
        if (std::any_of(build.runDroneRanks.begin(), build.runDroneRanks.begin() + static_cast<std::ptrdiff_t>(i),
            [&](const auto& prior) { return prior.droneId == rank.droneId; })) return false;
    }
    for (std::size_t i = 0; i < build.selectedSynergyIds.size(); ++i) {
        if (build.selectedSynergyIds[i].empty() ||
            std::find(build.selectedSynergyIds.begin(), build.selectedSynergyIds.begin() + static_cast<std::ptrdiff_t>(i),
                build.selectedSynergyIds[i]) != build.selectedSynergyIds.begin() + static_cast<std::ptrdiff_t>(i)) return false;
    }
    return std::all_of(build.droneModuleAssignments.begin(), build.droneModuleAssignments.end(), [](const auto& graft) {
        return graft.equippedFrame >= 0 && !graft.primaryDroneId.empty() &&
            static_cast<int>(graft.module) >= 0 && static_cast<int>(graft.module) <= static_cast<int>(DroneModuleKind::HazardScreen);
    });
}

bool routeRevealed(const MetaProgress &meta, std::string_view bodyId)
{
    if (bodyId == "mercury" || bodyId == "venus") return hasUnlock(meta, content::unlock::routeMars);
    if (bodyId == "mars") return hasUnlock(meta, content::unlock::routeMars);
    if (bodyId == "jupiter" || bodyId == "io")
        return hasUnlock(meta, content::unlock::routeJupiter);
    if (bodyId == "saturn" || bodyId == "titan")
        return hasUnlock(meta, content::unlock::routeSaturn);
    if (bodyId == "uranus" || bodyId == "titania")
        return hasUnlock(meta, content::unlock::routeUranus);
    if (bodyId == "neptune" || bodyId == "triton")
        return hasUnlock(meta, content::unlock::routeNeptune);
    return false;
}
BeaconBatteryState *battery(PersistentExpeditionState &e, std::string_view id)
{
    const auto it =
        std::find_if(e.batteries.begin(), e.batteries.end(), [&](const auto &b) { return b.id == id; });
    return it == e.batteries.end() ? nullptr : &*it;
}
double segmentDistance(SystemVector a, SystemVector b, SystemVector point)
{
    const double dx = b.x - a.x, dy = b.y - a.y;
    const double t = std::clamp(
        ((point.x - a.x) * dx + (point.y - a.y) * dy) / std::max(1e-15, dx * dx + dy * dy), 0.0, 1.0);
    return distance({a.x + t * dx, a.y + t * dy}, point);
}
void addCargo(ExpeditionCargo &to, const ExpeditionCargo &from)
{
    to.materials.common += from.materials.common;
    to.materials.rare += from.materials.rare;
    to.materials.exotic += from.materials.exotic;
    to.shipPropellant += from.shipPropellant;
    to.shipRepair += from.shipRepair;
    to.credits += from.credits;
}
} // namespace
bool expeditionMapBodyRevealed(const GameState &state, const SystemBodyDefinition &body)
{
    const auto &expedition = state.run.expedition;
    if (body.id == "sun" || body.id == "earth" || body.id == "moon") return true;
    if (body.id == "straylight") return arkDiscovered(state);
    if (routeRevealed(state.meta, body.id)) return true;
    return std::find(expedition.discoveredBodies.begin(), expedition.discoveredBodies.end(), body.id) !=
        expedition.discoveredBodies.end();
}
SystemLocation convertSystemFrame(const SystemLocation &source, CoordinateFrame frame,
                                  std::string_view bodyId, const SystemDefinition &system)
{
    SystemLocation p = source;
    if (p.systemId != system.id)
        throw std::invalid_argument("Location belongs to another system");
    if (p.frame == CoordinateFrame::Body)
    {
        const auto *body = systemBody(system, p.bodyId);
        if (!body)
            throw std::invalid_argument("Unknown source frame");
        p.position.x += body->position.x;
        p.position.y += body->position.y;
        p.velocity.x += body->velocity.x;
        p.velocity.y += body->velocity.y;
    }
    if (frame == CoordinateFrame::Body)
    {
        const auto *body = systemBody(system, bodyId);
        if (!body)
            throw std::invalid_argument("Unknown destination frame");
        p.position.x -= body->position.x;
        p.position.y -= body->position.y;
        p.velocity.x -= body->velocity.x;
        p.velocity.y -= body->velocity.y;
    }
    p.frame = frame;
    p.bodyId = std::string(bodyId);
    return p;
}
void captureSystemLocation(SystemLocation &p, const FlightRunState &f)
{
    p.position = {f.positionX, f.positionY};
    p.velocity = {f.velocityX, f.velocityY};
    p.heading = f.heading;
}
void restoreSystemLocation(const SystemLocation &p, FlightRunState &f)
{
    f.positionX = p.position.x;
    f.positionY = p.position.y;
    f.velocityX = p.velocity.x;
    f.velocityY = p.velocity.y;
    f.heading = p.heading;
}
const SystemBodyDefinition *encounteredBody(const SystemLocation &location, const SystemDefinition &system)
{
    const auto p = absolute(location, system);
    // A wider exit boundary than entry prevents chatter near a frame boundary.
    if (location.frame == CoordinateFrame::Body)
    {
        const auto *current = systemBody(system, location.bodyId);
        if (current && distance(p.position, current->position) < current->influenceRadius * 1.1)
            return current;
    }
    const SystemBodyDefinition *result = nullptr;
    double nearest = 1.0;
    for (const auto &body : system.bodies)
    {
        const double normalized = distance(p.position, body.position) / body.influenceRadius;
        if (normalized < nearest)
        {
            nearest = normalized;
            result = &body;
        }
    }
    return result;
}
CoastPredictionPose integrateSystemCoast(CoastPredictionPose p, double dt, const SystemDefinition &system)
{
    const auto acceleration = [&](double x, double y)
    {
        SystemVector a;
        for (const auto &b : system.bodies)
        {
            const double dx = x - b.position.x, dy = y - b.position.y,
                         r = std::max(.0001, std::hypot(dx, dy));
            const double g = systemBodyGravityAcceleration(b, r);
            a.x -= dx / r * g;
            a.y -= dy / r * g;
        }
        return a;
    };
    const auto a = acceleration(p.x, p.y);
    const auto m = acceleration(p.x + p.vx * dt * .5, p.y + p.vy * dt * .5);
    return {p.x + (p.vx + a.x * dt * .5) * dt, p.y + (p.vy + a.y * dt * .5) * dt, p.vx + m.x * dt,
            p.vy + m.y * dt};
}
CoursePlan previewSystemCourse(const SystemLocation &location, const FlightRunState &flight,
                               const SystemDefinition &system, std::string_view targetId,
                               std::string_view homeId, const PreparedLaunch* suppliedModel)
{
    CoursePlan plan;
    const auto *target = systemBody(system, targetId);
    const auto *home = systemBody(system, homeId);
    if (!target || !home)
        return plan;
    const auto p = absolute(location, system);
    plan.targetBodyId = targetId;
    FlightRunState predicted = flight;
    predicted.active = predicted.physicalFlight = true;
    predicted.failureCause = LaunchFailureCause::None;
    predicted.phase = FlightPhase::Transfer;
    predicted.mode = location.frame == CoordinateFrame::System ? FlightMode::Travel : FlightMode::Orbit;
    predicted.landing = {};
    PersistentExpeditionState predictedExpedition;
    predictedExpedition.location = location;
    predictedExpedition.course.targetBodyId = targetId;
    predictedExpedition.cruise.active = true;
    PreparedLaunch model = suppliedModel ? *suppliedModel : PreparedLaunch{};
    model.trajectoryPreview = true;
    const Destination environment;
    const auto fuelEstimate = [&](SystemLocation start, FlightRunState ship, const SystemBodyDefinition& goal) {
        PersistentExpeditionState estimate;
        estimate.location = start;
        ship.active = ship.physicalFlight = true;
        ship.failureCause = LaunchFailureCause::None;
        ship.phase = FlightPhase::Transfer;
        ship.landing = {};
        ship.mode = start.frame == CoordinateFrame::System ? FlightMode::Travel : FlightMode::Orbit;
        // Hypothetical capacity measures required fuel; it never modifies the live ship.
        const double initialFuel = ship.fuelRemaining = 10000.0;
        const SystemVector goalPosition = systemNavigationPosition(goal);
        const double arrivalRadius = goal.dock ? expeditionDockRadius : goal.influenceRadius;
        for (int i = 0; i < 3600; ++i) {
            captureSystemLocation(estimate.location, ship);
            const auto current = absolute(estimate.location, system);
            if (distance(current.position, goalPosition) <= arrivalRadius) return initialFuel-ship.fuelRemaining;
            const double desired = std::atan2(goalPosition.y-current.position.y, goalPosition.x-current.position.x);
            const double error = flightWrappedAngleDelta(ship.heading, desired);
            FlightInput input{std::clamp(-error*2.0,-1.0,1.0), std::abs(error)<.35 && ship.heat<.45 ? .6 : 0.0, false, true};
            if (advanceExpeditionFlight(estimate,ship,model,environment,system,input,.05).failed) return -1.0;
        }
        return -1.0;
    };
    const double approach = fuelEstimate(location, flight, *target);
    auto returnStart = p;
    const double homeDistance = std::max(.0001, distance(target->position,home->position));
    returnStart.position = {target->position.x+(home->position.x-target->position.x)/homeDistance*target->influenceRadius,
        target->position.y+(home->position.y-target->position.y)/homeDistance*target->influenceRadius};
    returnStart.velocity = {};
    FlightRunState returnShip = flight;
    returnShip.heat = returnShip.heatFailureSeconds = 0;
    restoreSystemLocation(returnStart, returnShip);
    const double returning = target->id == home->id ? 0 : fuelEstimate(returnStart,returnShip,*home);
    plan.estimateValid = approach >= 0 && returning >= 0;
    plan.approachFuel = std::max(0.0, approach) + (target->dock ? 0.0 : plan.manualCaptureAllowance);
    plan.returnMargin = flight.fuelRemaining - plan.approachFuel - std::max(0.0, returning) -
        (home->dock ? 0.0 : plan.manualCaptureAllowance);
    plan.trajectory.push_back(p.position);
    for (int i = 0; i < 1800; ++i)
    {
        const auto step = advanceExpeditionFlight(predictedExpedition, predicted, model, environment, system, {}, .05);
        const auto point = absolute(predictedExpedition.location, system).position;
        if (i % 5 == 0 || step.failed) plan.trajectory.push_back(point);
        if (step.failed || predicted.fuelRemaining <= 0) break;
    }
    for (const auto &b : system.bodies)
    {
        if (b.hazard.empty())
            continue;
        bool intersects = false;
        for (std::size_t i = 1; i < plan.trajectory.size(); ++i)
            intersects = intersects || segmentDistance(plan.trajectory[i-1], plan.trajectory[i], b.position) < b.radius + .1;
        if (intersects)
            plan.intersectedHazards.push_back(b.id);
    }
    return plan;
}
ExpeditionResult plotSystemCourse(PersistentExpeditionState &e, const FlightRunState &f,
                                  const SystemDefinition &s, std::string_view target, const PreparedLaunch* model)
{
    if (const auto* wreck = courseWreck(e, target)) {
        auto position = e.location;
        captureSystemLocation(position, f);
        e.course = {};
        e.course.targetBodyId = std::string(target);
        e.course.estimateValid = false;
        e.course.trajectory = {absolute(position,s).position, absolute(wreck->location,s).position};
        return ExpeditionResult::Applied;
    }
    if (!systemBody(s, target))
        return ExpeditionResult::InvalidTarget;
    auto position = e.location;
    captureSystemLocation(position, f);
    e.course = previewSystemCourse(position, f, s, target, e.homeBodyId, model);
    return ExpeditionResult::Applied;
}
ExpeditionResult toggleCruise(PersistentExpeditionState &e)
{
    if (e.course.targetBodyId.empty())
        return ExpeditionResult::InvalidTarget;
    e.cruise.active = !e.cruise.active;
    e.cruise.cooling = false;
    return ExpeditionResult::Applied;
}
FlightInput cruiseInput(PersistentExpeditionState &e, const FlightRunState &flight, const SystemDefinition &s,
                        FlightInput manual, bool heatEnabled)
{
    if (std::abs(manual.steer) > .01 || std::abs(manual.throttle) > .01 || manual.enginesCut)
    {
        e.cruise = {};
        return manual;
    }
    if (!e.cruise.active) {
        e.cruise.cooling = false;
        return manual;
    }
    const auto target = courseTargetLocation(e, s, e.course.targetBodyId);
    if (!target)
    {
        e.cruise = {};
        return manual;
    }
    auto p = e.location;
    captureSystemLocation(p, flight);
    p = absolute(p, s);
    const SystemVector destination = target->position;
    if (courseWreck(e, e.course.targetBodyId) && distance(p.position, destination) <= expeditionSalvageRadius * 3.0) {
        e.cruise = {};
        return manual; // Final rendezvous remains manual.
    }
    const double desired = std::atan2(destination.y - p.position.y, destination.x - p.position.x);
    // Hysteresis keeps engines fully off until there is room for another
    // useful burn. Steering and normal coast physics remain responsive.
    if (!heatEnabled || flight.heat <= tuning::launch::cruiseCoolingResume)
        e.cruise.cooling = false;
    else if (flight.heat >= tuning::launch::cruiseCoolingStart)
        e.cruise.cooling = true;
    return {std::clamp(-flightWrappedAngleDelta(flight.heading, desired) * 2.0, -1.0, 1.0),
        e.cruise.cooling ? 0.0 : 1.0, e.cruise.cooling, true};
}
int batteryResearchRank(const PersistentExpeditionState &e)
{
    const auto n =
        std::count_if(e.batteries.begin(), e.batteries.end(), [](const auto &b) { return b.researchEarned; });
    return n >= 4 ? 3 : n >= 2 ? 2 : 1;
}
LaunchFlightStep advanceExpeditionFlight(PersistentExpeditionState &e, FlightRunState &flight,
                                         const PreparedLaunch &launch, const Destination &destination,
                                         const SystemDefinition &system, FlightInput input, double dt,
                                         const MiningRunState *site)
{
    if (!flight.active && !e.undockReady) return {};
    if (e.undockReady) {
        e.cruise.active = false;
        if (input.throttle <= 0.001) {
            advanceFlightHeading(flight, input.steer, std::max(0.0, dt));
            e.location.heading = flight.heading;
            return {};
        }
        if (departDock(e,flight) != ExpeditionResult::Applied) return {};
        e.undockReady = false;
        const auto *body = encounteredBody(e.location, system);
        if (body && body->id == "straylight" && !e.straylightRevealed) body = nullptr;
        e.location = convertSystemFrame(e.location, body ? CoordinateFrame::Body : CoordinateFrame::System,
                                        body ? body->id : "", system);
        restoreSystemLocation(e.location, flight);
        flight.mode = body ? FlightMode::Orbit : FlightMode::Travel;
    }
    input = cruiseInput(e, flight, system, input, launch.heatEnabled);
    auto result = updateLaunchFlight(flight, launch, destination, input, dt, site, &system, &e.location);
    if (flight.mode == FlightMode::Landing)
        return result;
    captureSystemLocation(e.location, flight);
    if (result.failed)
        return result;
    const auto *encounter = encounteredBody(e.location, system);
    if (encounter && encounter->id == "straylight" && !e.straylightRevealed) encounter = nullptr;
    const auto frame = encounter ? CoordinateFrame::Body : CoordinateFrame::System;
    const std::string bodyId = encounter ? encounter->id : "";
    if (e.location.frame != frame || e.location.bodyId != bodyId)
    {
        e.location = convertSystemFrame(e.location, frame, bodyId, system);
        e.location.siteId.clear();
        restoreSystemLocation(e.location, flight);
        flight.mode = encounter ? FlightMode::Orbit : FlightMode::Travel;
        flight.orbit = {};
        flight.landing = {};
        flight.orbitZoomProgress = 0;
        flight.destinationId = encounter ? encounter->environmentId : "";
        if (encounter && std::find(e.discoveredBodies.begin(), e.discoveredBodies.end(), bodyId) == e.discoveredBodies.end())
            e.discoveredBodies.push_back(bodyId);
        flight.predictedTrajectory.clear();
    }
    if (!launch.trajectoryPreview) {
        flight.predictionAge += dt;
        if (flight.predictionAge >= .10 || flight.predictedTrajectory.empty()) refreshExpeditionTrajectory(e,flight,launch,destination,system);
    }
    return result;
}
void refreshExpeditionTrajectory(PersistentExpeditionState& e, FlightRunState& flight,
    const PreparedLaunch& launch, const Destination& destination, const SystemDefinition& system) {
    if (flight.mode == FlightMode::Landing) return;
    auto predicted = flight;
    predicted.predictedTrajectory.clear();
    PersistentExpeditionState forecast;
    forecast.location = e.location;
    forecast.travelInitialized = true;
    auto model = launch;
    model.trajectoryPreview = true;
    flight.predictedTrajectory = {{flight.positionX,flight.positionY}};
    flight.predictedImpact = false;
    flight.predictionAge = 0;
    if (!flight.active) return;
    const int steps = e.location.frame == CoordinateFrame::Body ? 1000 : 400;
    for (int i=0;i<steps;++i) {
        const auto result = advanceExpeditionFlight(forecast,predicted,model,destination,system,{},.05);
        auto p = forecast.location;
        captureSystemLocation(p,predicted);
        p = convertSystemFrame(p,e.location.frame,e.location.bodyId,system);
        if (i%4==3 || result.failed || result.asteroidHit) flight.predictedTrajectory.push_back({p.position.x,p.position.y});
        if (result.failed || result.asteroidHit) { flight.predictedImpact = true; break; }
        if (predicted.mode == FlightMode::Landing) break;
    }
}
bool validBatteryOwnership(const PersistentExpeditionState &e)
{
    for (std::size_t i = 0; i < e.batteries.size(); ++i)
    {
        const auto &b = e.batteries[i];
        if (b.id.empty() || b.sourceSiteId.empty())
            return false;
        if (static_cast<int>(b.owner) < 0 || static_cast<int>(b.owner) > 4)
            return false;
        for (std::size_t j = 0; j < i; ++j)
            if (e.batteries[j].id == b.id)
                return false;
        if (b.owner == BatteryOwner::Wreck)
        {
            if (b.wreckId == 0 || std::count_if(e.wrecks.begin(), e.wrecks.end(),
                                                [&](const auto &w) { return w.id == b.wreckId; }) != 1)
                return false;
        }
        else if (b.wreckId != 0)
            return false;
    }
    for (std::size_t i = 0; i < e.wrecks.size(); ++i)
    {
        if (!e.wrecks[i].id || e.wrecks[i].id >= e.nextWreckId)
            return false;
        if (!validRecoveredBuild(e.wrecks[i].build))
            return false;
        for (std::size_t j = 0; j < i; ++j)
            if (e.wrecks[i].id == e.wrecks[j].id)
                return false;
    }
    return !e.arkActivated || std::all_of(e.batteries.begin(), e.batteries.end(),
                                          [](const auto &b) { return b.owner == BatteryOwner::ArkSlot; });
}
ExpeditionResult recoverSiteBattery(PersistentExpeditionState &e, std::string_view id)
{
    auto *b = battery(e, id);
    if (!b)
        return ExpeditionResult::InvalidTarget;
    if (b->owner != BatteryOwner::Site)
        return ExpeditionResult::AlreadyApplied;
    if (e.location.bodyId != id && e.location.siteId != b->sourceSiteId &&
        !e.location.siteId.starts_with(b->sourceSiteId + ":"))
        return ExpeditionResult::NotAtSite;
    b->owner = BatteryOwner::Ship;
    b->discovered = true;
    return ExpeditionResult::Applied;
}
ExpeditionResult dockExpedition(PersistentExpeditionState &e, FlightRunState &f, const SystemDefinition &s)
{
    if (e.travelInitialized && (f.hullRemaining <= 0 || f.mode == FlightMode::Landing))
        return ExpeditionResult::InvalidState;
    if (e.travelInitialized && !f.active && !atDock(e, "earth") && !atDock(e, "straylight"))
        return ExpeditionResult::InvalidState;
    auto p = e.location;
    captureSystemLocation(p, f);
    p = absolute(p, s);
    const SystemBodyDefinition *dock = nullptr;
    for (const auto &b : s.bodies)
        if (b.dock && (!e.travelInitialized || b.id != "straylight" || e.straylightRevealed) &&
            distance(p.position, e.travelInitialized ? systemDockPosition(b) : b.position) <= (e.travelInitialized ? expeditionDockRadius : dockRange) &&
            distance(p.velocity, b.velocity) <= rendezvousSpeed)
        {
            dock = &b;
            break;
        }
    if (!dock)
        return ExpeditionResult::OutOfRange;
    if (atDock(e, dock->id) && !f.active) { e.undockReady=false; return ExpeditionResult::AlreadyApplied; }
    e.location = convertSystemFrame(p, CoordinateFrame::Body, dock->id, s);
    e.location.siteId = dock->siteId;
    if (e.travelInitialized) {
        const auto marker = systemDockPosition(*dock);
        e.location.position = {marker.x - dock->position.x, marker.y - dock->position.y};
        e.location.velocity = {};
        e.location.heading = 0;
    }
    restoreSystemLocation(e.location, f);
    e.cruise.active = false;
    e.undockReady = false;
    f.active = false;
    if (dock->id == "earth")
        for (auto &b : e.batteries)
            if (b.owner == BatteryOwner::Ship)
            {
                b.owner = BatteryOwner::EarthStorage;
                b.researchEarned = true;
            }
    if (dock->id == "straylight" && !e.arkActivated)
        return ExpeditionResult::Applied;
    e.active = false;
    e.rigFuel.current = e.rigFuel.capacity;
    f.fuelRemaining = f.fuelCapacity;
    f.hullRemaining = f.hullMaximum;
    f.heat = 0;
    f.selectedThrottle = f.angularVelocity = f.heatFailureSeconds = 0;
    f.failureCause = LaunchFailureCause::None;
    return ExpeditionResult::Applied;
}
ExpeditionResult dockExpedition(GameState &state, const SystemDefinition &system)
{
    auto &e = state.run.expedition;
    const auto result = dockExpedition(e, state.run.flight, system);
    if (result != ExpeditionResult::Applied || e.active || (atDock(e, "straylight") && !e.arkActivated))
        return result;
    state.meta.materials.common += e.cargo.materials.common;
    state.meta.materials.rare += e.cargo.materials.rare;
    state.meta.materials.exotic += e.cargo.materials.exotic;
    state.run.credits += e.cargo.credits;
    e.cargo.credits = 0;
    e.cargo.materials = {};
    return result;
}
bool canDockExpedition(const PersistentExpeditionState& e, const FlightRunState& f, const SystemDefinition& s) {
    if (e.travelInitialized && f.hullRemaining <= 0) return false;
    if (!expeditionDockInRange(e, f, s)) return false;
    auto p = e.location;
    captureSystemLocation(p, f);
    p = absolute(p, s);
    for (const auto& b : s.bodies)
        if (b.dock && (!e.travelInitialized || b.id != "straylight" || e.straylightRevealed) &&
            distance(p.position, systemDockPosition(b)) <= expeditionDockRadius &&
            distance(p.velocity, b.velocity) <= rendezvousSpeed) return true;
    return false;
}
bool expeditionDockInRange(const PersistentExpeditionState& e, const FlightRunState& f, const SystemDefinition& s, std::string_view dockBodyId) {
    if (!f.active || f.mode == FlightMode::Landing) return false;
    auto p = e.location;
    captureSystemLocation(p, f);
    p = absolute(p, s);
    for (const auto& b : s.bodies)
        if (b.dock && (!e.travelInitialized || b.id != "straylight" || e.straylightRevealed) &&
            (dockBodyId.empty() || b.id == dockBodyId) &&
            distance(p.position, systemDockPosition(b)) <= expeditionDockRadius) return true;
    return false;
}
bool canSalvageWreck(const PersistentExpeditionState& e, const FlightRunState& f, const SystemDefinition& s, std::uint64_t id, bool requireMatchedSpeed) {
    if (!f.active || f.mode == FlightMode::Landing) return false;
    auto p = e.location;
    captureSystemLocation(p, f);
    p = absolute(p, s);
    for (const auto& w : e.wrecks) if (w.id == id) {
        const auto point = absolute(w.location, s);
        return distance(p.position, point.position) <= expeditionSalvageRadius &&
            (!requireMatchedSpeed || distance(p.velocity, point.velocity) <= expeditionSalvageSpeed);
    }
    return false;
}
ExpeditionResult departDock(PersistentExpeditionState &e, FlightRunState &f)
{
    if (!atDock(e, "earth") && !atDock(e, "straylight"))
        return ExpeditionResult::NotDocked;
    if (f.active || (e.active && !(atDock(e, "straylight") && !e.arkActivated)))
        return ExpeditionResult::InvalidState;
    if (e.travelInitialized && atDock(e, "straylight") && !e.straylightRevealed)
        return ExpeditionResult::InvalidTarget;
    if (f.fuelRemaining <= 0 || f.hullRemaining <= 0)
        return ExpeditionResult::InvalidState;
    e.active = true;
    e.departureHistoryKnown = true;
    ++e.departureCount;
    e.undockReady = false;
    e.location.siteId.clear();
    f.active = true;
    return ExpeditionResult::Applied;
}
ExpeditionResult loadEarthBattery(PersistentExpeditionState &e, std::string_view id)
{
    if (!atDock(e, "earth"))
        return ExpeditionResult::NotDocked;
    auto *b = battery(e, id);
    if (!b)
        return ExpeditionResult::InvalidTarget;
    if (b->owner == BatteryOwner::Ship)
        return ExpeditionResult::AlreadyApplied;
    if (b->owner != BatteryOwner::EarthStorage)
        return ExpeditionResult::NotOwner;
    b->owner = BatteryOwner::Ship;
    return ExpeditionResult::Applied;
}
ExpeditionResult installArkBattery(PersistentExpeditionState &e, std::string_view id)
{
    if (!atDock(e, "straylight"))
        return ExpeditionResult::NotDocked;
    auto *b = battery(e, id);
    if (!b)
        return ExpeditionResult::InvalidTarget;
    if (b->owner == BatteryOwner::ArkSlot)
        return ExpeditionResult::AlreadyApplied;
    if (b->owner != BatteryOwner::Ship)
        return ExpeditionResult::NotOwner;
    b->owner = BatteryOwner::ArkSlot;
    b->researchEarned = true;
    return ExpeditionResult::Applied;
}
ExpeditionResult activateStraylight(PersistentExpeditionState &e)
{
    if (e.arkActivated)
        return ExpeditionResult::AlreadyApplied;
    if (!atDock(e, "straylight"))
        return ExpeditionResult::NotDocked;
    if (!std::all_of(e.batteries.begin(), e.batteries.end(),
                     [](const auto &b) { return b.owner == BatteryOwner::ArkSlot; }))
        return ExpeditionResult::MissingBatteries;
    e.arkActivated = true;
    e.homeBodyId = "straylight";
    return ExpeditionResult::Applied;
}
ExpeditionResult salvageWreck(PersistentExpeditionState &e, std::uint64_t id, const SystemDefinition &s, int holdCapacity)
{
    auto it = std::find_if(e.wrecks.begin(), e.wrecks.end(), [&](const auto &w) { return w.id == id; });
    if (it == e.wrecks.end())
        return ExpeditionResult::AlreadyApplied;
    const auto p = absolute(e.location, s), w = absolute(it->location, s);
    if (distance(p.position, w.position) > expeditionSalvageRadius || distance(p.velocity, w.velocity) > expeditionSalvageSpeed)
        return ExpeditionResult::OutOfRange;
    for (auto &b : e.batteries)
        if (b.owner == BatteryOwner::Wreck && b.wreckId == id)
        {
            b.owner = BatteryOwner::Ship;
            b.wreckId = 0;
        }
    const auto transfer = planPayloadTransfer(it->cargo.materials, {}, e.cargo.materials, holdCapacity);
    ExpeditionCargo recovered = it->cargo;
    recovered.materials = transfer.toShipHold;
    addCargo(e.cargo, recovered);
    it->cargo = {};
    it->cargo.materials = transfer.remainingAtSource;
    if (it->buildRecoverable) {
        mergeRecoveredBuild(e.progression, std::move(it->build));
        it->build = {};
        it->buildRecoverable = false;
    }
    if (materialCargoMass(it->cargo.materials) == 0 && !it->buildRecoverable) e.wrecks.erase(it);
    return ExpeditionResult::Applied;
}

ExpeditionResult resolveRecoveredGraftConflict(PersistentExpeditionState& e, int conflictIndex, bool useRecovered)
{
    auto& conflicts = e.progression.pendingGraftConflicts;
    if (conflictIndex < 0 || conflictIndex >= static_cast<int>(conflicts.size())) return ExpeditionResult::InvalidTarget;
    const auto conflict = conflicts[static_cast<std::size_t>(conflictIndex)];
    auto current = std::find_if(e.progression.droneModuleAssignments.begin(), e.progression.droneModuleAssignments.end(),
        [&](const auto& graft) { return graft.equippedFrame == conflict.equippedFrame; });
    if (useRecovered) {
        if (current == e.progression.droneModuleAssignments.end()) e.progression.droneModuleAssignments.push_back(conflict.recovered);
        else *current = conflict.recovered;
    }
    conflicts.erase(conflicts.begin() + conflictIndex);
    e.progression.droneModuleRuntime.clear();
    return ExpeditionResult::Applied;
}
ExpeditionResult loseExpedition(PersistentExpeditionState &e, FlightRunState &f, const SystemDefinition &s)
{
    if (!e.active)
        return ExpeditionResult::AlreadyApplied;
    const auto *home = systemBody(s, e.homeBodyId);
    if (!home || !home->dock || (home->id != "earth" && !e.arkActivated))
        return ExpeditionResult::InvalidState;
    auto p = e.location;
    captureSystemLocation(p, f);
    p = absolute(p, s);
    const double speed = std::hypot(p.velocity.x, p.velocity.y);
    SystemVector incoming =
        speed > 1e-8 ? SystemVector{-p.velocity.x / speed, -p.velocity.y / speed} : SystemVector{1, 0};
    for (const auto &body : s.bodies)
        if (distance(p.position, body.position) < body.radius + dockRange + 0.25)
        {
            p.position = {body.position.x + incoming.x * (body.radius + dockRange + .5),
                          body.position.y + incoming.y * (body.radius + dockRange + .5)};
        }
    p.velocity = {};
    p.siteId.clear();
    const auto id = e.nextWreckId++;
    WreckState wreck;
    wreck.id = id;
    wreck.location = p;
    wreck.cargo = e.cargo;
    wreck.build = e.progression;
    wreck.build.droneModuleRuntime.clear();
    wreck.build.runUpgradeOffers = {};
    wreck.build.runUpgradeOfferCount = 0;
    wreck.build.runUpgradeOfferPending = false;
    // The wreck payload already stores graft alternatives as assignments. Keep
    // unresolved choices there so a second loss and reload cannot discard them;
    // merging the recovered same-slot assignments recreates the explicit choice.
    for (const auto& conflict : wreck.build.pendingGraftConflicts) {
        const auto& graft = conflict.recovered;
        const bool stored = std::any_of(wreck.build.droneModuleAssignments.begin(), wreck.build.droneModuleAssignments.end(),
            [&](const auto& candidate) {
                return candidate.equippedFrame == graft.equippedFrame && candidate.module == graft.module &&
                    candidate.primaryDroneId == graft.primaryDroneId;
            });
        if (!stored) wreck.build.droneModuleAssignments.push_back(graft);
    }
    wreck.build.pendingGraftConflicts.clear();
    wreck.buildRecoverable = true;
    e.wrecks.push_back(std::move(wreck));
    e.cargo = {};
    for (auto &b : e.batteries)
        if (b.owner == BatteryOwner::Ship)
        {
            b.owner = BatteryOwner::Wreck;
            b.wreckId = id;
        }
    e.active = false;
    e.cruise.active = false;
    e.undockReady = false;
    e.progression = {};
    e.coursePlayerSelected = false;
    e.selectedOrbitBody.clear();
    e.selectedOrbitZone = "zone_1";
    e.course.trajectory.clear();
    e.course.intersectedHazards.clear();
    e.course.estimateValid = false;
    e.course.approachFuel = e.course.returnMargin = 0;
    e.rigFuel.current = e.rigFuel.capacity;
    e.location = {s.id, home->id, CoordinateFrame::Body, {e.travelInitialized ? systemDockPosition(*home).x-home->position.x : dockRange * .8, e.travelInitialized ? systemDockPosition(*home).y-home->position.y : 0}, {}, 0, home->siteId};
    const double fuelCapacity = std::max(10.0, f.fuelCapacity);
    const double hullMaximum = std::max(100.0, f.hullMaximum);
    f = {};
    restoreSystemLocation(e.location, f);
    f.active = false;
    f.physicalFlight = e.travelInitialized;
    f.phase = FlightPhase::Transfer;
    f.mode = FlightMode::Orbit;
    f.selectedThrottle = 0;
    f.fuelCapacity = fuelCapacity;
    f.hullMaximum = hullMaximum;
    f.fuelRemaining = f.fuelCapacity;
    f.hullRemaining = f.hullMaximum;
    f.heat = 0;
    return ExpeditionResult::Applied;
}
ExpeditionResult useShipSupplies(PersistentExpeditionState &e, FlightRunState &f)
{
    if (e.location.siteId.empty() || f.active)
        return ExpeditionResult::NotAtSite;
    const double fuel = std::min(e.cargo.shipPropellant, std::max(0.0, f.fuelCapacity - f.fuelRemaining));
    const double repair = std::min(e.cargo.shipRepair, std::max(0.0, f.hullMaximum - f.hullRemaining));
    e.cargo.shipPropellant -= fuel;
    f.fuelRemaining += fuel;
    e.cargo.shipRepair -= repair;
    f.hullRemaining += repair;
    return fuel + repair > 0 ? ExpeditionResult::Applied : ExpeditionResult::AlreadyApplied;
}
void storeVisitedSite(GameState &state, std::string_view id)
{
    auto &e = state.run.expedition;
    const auto match = [&](const auto &site)
    { return site.systemId == e.location.systemId && site.bodyId == e.location.bodyId && site.siteId == id; };
    auto found = std::find_if(e.sites.begin(), e.sites.end(), match);
    PersistentSiteState site{e.location.systemId, e.location.bodyId, std::string(id),
                             state.run.planetaryExpedition, state.run.mining, {}};
    // The build remains on the expedition; a dormant site must not restore an
    // earlier set of XP choices on revisit.
    if (found == e.sites.end())
        e.sites.push_back(std::move(site));
    else {
        site.orbital = found->orbital;
        *found = std::move(site);
    }
}
bool restoreVisitedSite(GameState &state, std::string_view id)
{
    const auto &e = state.run.expedition;
    const auto found = std::find_if(e.sites.begin(), e.sites.end(), [&](const auto &site)
                                    { return site.systemId == e.location.systemId && site.bodyId == e.location.bodyId && site.siteId == id; });
    if (found == e.sites.end())
        return false;
    auto surface = found->surface;
    state.run.planetaryExpedition = std::move(surface);
    state.run.mining = found->mining;
    return true;
}
} // namespace rocket
