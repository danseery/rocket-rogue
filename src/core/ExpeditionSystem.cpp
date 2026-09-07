#include "core/ExpeditionSystem.h"
#include "core/ResearchSystem.h"
#include "core/PayloadTransfer.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace rocket
{
namespace
{
constexpr double dockRange = .65;
constexpr double rendezvousSpeed = .20;
double distance(SystemVector a, SystemVector b) { return std::hypot(a.x - b.x, a.y - b.y); }
SystemLocation absolute(const SystemLocation &p, const SystemDefinition &s)
{
    return convertSystemFrame(p, CoordinateFrame::System, "", s);
}
bool atDock(const PersistentExpeditionState &e, std::string_view body)
{
    return e.location.bodyId == body && e.location.siteId == std::string(body) + ".dock";
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
        for (int i = 0; i < 3600; ++i) {
            captureSystemLocation(estimate.location, ship);
            const auto current = absolute(estimate.location, system);
            if (distance(current.position, goal.position) <= goal.influenceRadius) return initialFuel-ship.fuelRemaining;
            const double desired = std::atan2(goal.position.y-current.position.y, goal.position.x-current.position.x);
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
    plan.approachFuel = std::max(0.0, approach) + plan.manualCaptureAllowance;
    plan.returnMargin = flight.fuelRemaining - plan.approachFuel - std::max(0.0, returning) - plan.manualCaptureAllowance;
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
    return ExpeditionResult::Applied;
}
FlightInput cruiseInput(PersistentExpeditionState &e, const FlightRunState &flight, const SystemDefinition &s,
                        FlightInput manual)
{
    if (std::abs(manual.steer) > .01 || std::abs(manual.throttle) > .01 || manual.enginesCut)
    {
        e.cruise.active = false;
        return manual;
    }
    if (!e.cruise.active)
        return manual;
    const auto *target = systemBody(s, e.course.targetBodyId);
    if (!target)
    {
        e.cruise.active = false;
        return manual;
    }
    auto p = e.location;
    captureSystemLocation(p, flight);
    p = absolute(p, s);
    const double desired = std::atan2(target->position.y - p.position.y, target->position.x - p.position.x);
    return {std::clamp(-flightWrappedAngleDelta(flight.heading, desired) * 2.0, -1.0, 1.0), 1.0, false, true};
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
    if (e.undockReady) {
        e.cruise.active = false;
        if (input.throttle <= 0.001) return {};
        if (departDock(e,flight) != ExpeditionResult::Applied) return {};
        e.undockReady = false;
        const auto *body = encounteredBody(e.location, system);
        e.location = convertSystemFrame(e.location, body ? CoordinateFrame::Body : CoordinateFrame::System,
                                        body ? body->id : "", system);
        restoreSystemLocation(e.location, flight);
        flight.mode = body ? FlightMode::Orbit : FlightMode::Travel;
    }
    input = cruiseInput(e, flight, system, input);
    auto result = updateLaunchFlight(flight, launch, destination, input, dt, site, &system, &e.location);
    if (flight.mode == FlightMode::Landing)
        return result;
    captureSystemLocation(e.location, flight);
    if (result.failed)
        return result;
    const auto *encounter = encounteredBody(e.location, system);
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
        if (i%4==3 || result.failed) flight.predictedTrajectory.push_back({p.position.x,p.position.y});
        if (result.failed) { flight.predictedImpact = true; break; }
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
    if (e.location.siteId != b->sourceSiteId && e.location.siteId != b->sourceSiteId + ":zone_1")
        return ExpeditionResult::NotAtSite;
    b->owner = BatteryOwner::Ship;
    b->discovered = true;
    return ExpeditionResult::Applied;
}
ExpeditionResult dockExpedition(PersistentExpeditionState &e, FlightRunState &f, const SystemDefinition &s)
{
    auto p = e.location;
    captureSystemLocation(p, f);
    p = absolute(p, s);
    const SystemBodyDefinition *dock = nullptr;
    for (const auto &b : s.bodies)
        if (b.dock && distance(p.position, e.travelInitialized ? systemDockPosition(b) : b.position) <= (e.travelInitialized ? .16 : dockRange) &&
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
    e.progression = {};
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
    if (!f.active || f.mode == FlightMode::Landing) return false;
    auto p = e.location;
    captureSystemLocation(p, f);
    p = absolute(p, s);
    for (const auto& b : s.bodies)
        if (b.dock && distance(p.position, systemDockPosition(b)) <= .16 && distance(p.velocity, b.velocity) <= rendezvousSpeed) return true;
    return false;
}
bool canSalvageWreck(const PersistentExpeditionState& e, const FlightRunState& f, const SystemDefinition& s, std::uint64_t id, bool requireMatchedSpeed) {
    if (!f.active || f.mode == FlightMode::Landing) return false;
    auto p = e.location;
    captureSystemLocation(p, f);
    p = absolute(p, s);
    for (const auto& w : e.wrecks) if (w.id == id) {
        const auto point = absolute(w.location, s);
        return distance(p.position, point.position) <= dockRange &&
            (!requireMatchedSpeed || distance(p.velocity, point.velocity) <= rendezvousSpeed);
    }
    return false;
}
ExpeditionResult departDock(PersistentExpeditionState &e, FlightRunState &f)
{
    if (!atDock(e, "earth") && !atDock(e, "straylight"))
        return ExpeditionResult::NotDocked;
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
    if (distance(p.position, w.position) > dockRange || distance(p.velocity, w.velocity) > rendezvousSpeed)
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
    if (materialCargoMass(it->cargo.materials) == 0) e.wrecks.erase(it);
    return ExpeditionResult::Applied;
}
ExpeditionResult loseExpedition(PersistentExpeditionState &e, FlightRunState &f, const SystemDefinition &s)
{
    if (!e.active)
        return ExpeditionResult::AlreadyApplied;
    const auto *home = systemBody(s, e.homeBodyId);
    if (!home || !home->dock)
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
    e.wrecks.push_back({id, p, e.cargo});
    e.cargo = {};
    for (auto &b : e.batteries)
        if (b.owner == BatteryOwner::Ship)
        {
            b.owner = BatteryOwner::Wreck;
            b.wreckId = id;
        }
    e.active = false;
    e.cruise.active = false;
    e.progression = {};
    e.rigFuel.current = e.rigFuel.capacity;
    e.location = {s.id, home->id, CoordinateFrame::Body, {e.travelInitialized ? systemDockPosition(*home).x-home->position.x : dockRange * .8, e.travelInitialized ? systemDockPosition(*home).y-home->position.y : 0}, {}, 0, home->siteId};
    restoreSystemLocation(e.location, f);
    f.active = false;
    f.fuelCapacity = std::max(10.0, f.fuelCapacity);
    f.hullMaximum = std::max(100.0, f.hullMaximum);
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
