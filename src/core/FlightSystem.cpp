#include "core/FlightSystem.h"
#include "core/MiningSystem.h"

#include <algorithm>
#include <cmath>

namespace rocket {

const std::array<PlanetLandingZone, 6>& planetLandingZones()
{
    static const auto zones = [] {
        std::array<PlanetLandingZone, 6> result;
        constexpr double sector = 1.0471975511965976;
        const double inbound = std::atan2(flight_geometry::startY, flight_geometry::startX);
        for (int i = 0; i < 6; ++i) {
            result[i] = {"zone_" + std::to_string(i + 1), i,
                std::remainder(inbound + sector * i, 6.283185307179586),
                sector * 0.5, i == 0, i == 0 ? "inbound_site" : ""};
        }
        return result;
    }();
    return zones;
}

const PlanetLandingZone* planetLandingZone(std::string_view id)
{
    for (const auto& zone : planetLandingZones()) if (zone.id == id) return &zone;
    return nullptr;
}

bool landingZoneContains(const PlanetLandingZone& zone, double bearing)
{
    const double delta = flightWrappedAngleDelta(zone.centerBearing, bearing);
    // Half-open intervals: a shared edge belongs to exactly one sector.
    return delta >= -zone.halfAngle && delta < zone.halfAngle;
}

const PlanetLandingZone* enabledLandingZoneAt(double bearing)
{
    for (const auto& zone : planetLandingZones())
        if (zone.enabled && landingZoneContains(zone, bearing)) return &zone;
    return nullptr;
}

double landingZoneSiteBearing(const PlanetLandingZone& zone, double gridX,
    double padX, double siteWidth)
{
    const double span = std::max(1.0, std::max(padX, siteWidth - padX));
    // Local screen-right is clockwise around the pad's outward surface normal.
    return zone.centerBearing - std::clamp((gridX - padX) / span, -1.0, 1.0)
        * zone.halfAngle * 0.5;
}

CoastPredictionPose stepCoastPrediction(CoastPredictionPose p, double dt)
{
    const auto acceleration = [](double x, double y) {
        const double radius = std::max(0.0001, std::hypot(x, y));
        const double gravity = flightGravityAcceleration(radius, 0.0);
        return std::pair<double, double>{-x / radius * gravity, -y / radius * gravity};
    };
    const auto [ax, ay] = acceleration(p.x, p.y);
    const auto [mx, my] = acceleration(p.x + p.vx * dt * 0.5, p.y + p.vy * dt * 0.5);
    return {p.x + (p.vx + ax * dt * 0.5) * dt,
        p.y + (p.vy + ay * dt * 0.5) * dt, p.vx + mx * dt, p.vy + my * dt};
}

OrbitLoopAssessment assessOrbitLoop(const FlightRunState& flight)
{
    constexpr double tau = 6.28318530717958647692;
    CoastPredictionPose pose{flight.positionX, flight.positionY, flight.velocityX, flight.velocityY};
    const double startRadius = std::hypot(pose.x, pose.y);
    const double momentum = pose.x * pose.vy - pose.y * pose.vx;
    if (!std::isfinite(startRadius) || !std::isfinite(momentum) || std::abs(momentum) < 1e-8 ||
        startRadius <= flight_geometry::bodyRadius || startRadius >= flight_geometry::influenceRadius) return {};
    const double direction = momentum > 0.0 ? 1.0 : -1.0;
    double travel = 0.0;
    bool perfect = true;
    for (int i = 0; i < 300; ++i) {
        auto next = stepCoastPrediction(pose, 0.2);
        const double delta = direction * flightWrappedAngleDelta(std::atan2(pose.y, pose.x), std::atan2(next.y, next.x));
        if (!std::isfinite(delta) || delta <= 0.0) return {};
        bool closes = travel + delta >= tau;
        if (closes) {
            double low = 0.0, high = 0.2;
            for (int n = 0; n < 16; ++n) {
                const double mid = (low + high) * 0.5;
                const auto p = stepCoastPrediction(pose, mid);
                const double arc = direction * flightWrappedAngleDelta(std::atan2(pose.y, pose.x), std::atan2(p.y, p.x));
                if (travel + arc < tau) low = mid; else high = mid;
            }
            next = stepCoastPrediction(pose, high);
        }
        const double dx = next.x - pose.x, dy = next.y - pose.y;
        const double t = std::clamp(-(pose.x * dx + pose.y * dy) / std::max(1e-15, dx * dx + dy * dy), 0.0, 1.0);
        if (std::hypot(pose.x + t * dx, pose.y + t * dy) <= flight_geometry::bodyRadius ||
            std::hypot(next.x, next.y) >= flight_geometry::influenceRadius) return {};
        for (const auto& p : {pose, next}) {
            const auto k = flightKinematics(p.x, p.y, p.vx, p.vy);
            perfect = perfect && flightOrbitStable(k, flight.orbit.targetRadius, flight.orbit.perfectBand);
        }
        if (closes) return {std::hypot(next.x - flight.positionX, next.y - flight.positionY) <=
            std::max(0.03, startRadius * 0.15), perfect};
        travel += delta;
        pose = next;
    }
    return {};
}

double orbitConfirmationProgress(const FlightRunState& flight)
{
    return flight.orbit.captured ? 1.0 : std::clamp(flight.orbit.confirmationSeconds / 2.0, 0.0, 1.0);
}

double flightWrappedAngleDelta(double from, double to)
{
    constexpr double tau = 6.28318530717958647692;
    constexpr double pi = 3.14159265358979323846;
    double delta = std::fmod(to - from, tau);
    if (delta > pi) {
        delta -= tau;
    } else if (delta < -pi) {
        delta += tau;
    }
    return delta;
}

FlightKinematics flightKinematics(
    double positionX,
    double positionY,
    double velocityX,
    double velocityY)
{
    FlightKinematics result;
    result.radius = std::max(0.0001, std::hypot(positionX, positionY));
    result.radialVelocity =
        (positionX * velocityX + positionY * velocityY) / result.radius;
    result.tangentialVelocity =
        (positionX * velocityY - positionY * velocityX) / result.radius;
    result.angle = std::atan2(positionY, positionX);
    return result;
}

FlightScaleProfile flightScaleProfile(
    const FlightKinematics& kinematics,
    double travelProgress,
    double orbitTargetRadius,
    double orbitGoodBand,
    bool enteredInfluence,
    bool landingAuthorized,
    bool landingFrameActive)
{
    const double outerOrbitRadius = std::max(
        0.0001,
        orbitTargetRadius + orbitGoodBand);
    const double worldLength = std::hypot(
        flight_geometry::startX,
        flight_geometry::startY);
    const double approachStartRadius = std::max(
        outerOrbitRadius + 0.0001,
        worldLength * flight_scale::approachStartDistanceShare);
    const double proximityProgress =
        (approachStartRadius - kinematics.radius) /
        std::max(0.0001, approachStartRadius - outerOrbitRadius);
    const double routeProgress =
        (travelProgress - flight_scale::approachStartProgress) /
        (flight_scale::approachEndProgress - flight_scale::approachStartProgress);

    const auto smootherstep = [](double value) {
        const double t = std::clamp(value, 0.0, 1.0);
        return t * t * t * (t * (t * 6.0 - 15.0) + 10.0);
    };

    double approachLinear = std::max(proximityProgress, routeProgress);
    if (enteredInfluence) {
        // Ease the outward-only buffer in with radial speed. A ship that
        // reverses before reaching full zoom therefore cannot create a
        // one-frame camera or handling step as its radial velocity crosses 0.
        const double outwardBlend = smootherstep(
            kinematics.radialVelocity / flight_scale::outwardBufferVelocity);
        approachLinear += flight_scale::zoomOutBuffer * outwardBlend;
    }

    FlightScaleProfile result;
    result.approachLinear = std::clamp(approachLinear, 0.0, 1.0);
    result.approachBlend = smootherstep(result.approachLinear);
    result.timeScale = 1.0 -
        (1.0 - flight_scale::closeRangeTimeScale) * result.approachBlend;
    result.controlScale = 1.0 -
        (1.0 - flight_scale::closeRangeControlScale) * result.approachBlend;
    // Synthetic presentation callers may provide an explicit local mode.
    // Radius no longer selects Landing or its physics.
    result.landingFrameActive = landingFrameActive;
    result.landingBlend = landingFrameActive ? 1.0 : 0.0;
    result.landingLinear = result.landingBlend;
    (void)landingAuthorized;
    return result;
}

FlightScaleProfile flightScaleProfile(const FlightRunState& flight)
{
    FlightScaleProfile result;
    const auto ease = [](double v) {
        const double t = std::clamp(v, 0.0, 1.0);
        return t*t*t*(t*(t*6.0-15.0)+10.0);
    };
    const double handoff = ease(flight.handoff.elapsed / flight_landing::handoffSeconds);
    if (flight.mode == FlightMode::Travel) {
        result.approachBlend = flight.handoff.from == FlightMode::Orbit ? 1.0-handoff : 0.0;
    } else {
        result.approachBlend = 1.0;
        if (flight.mode == FlightMode::Orbit && flight.handoff.from == FlightMode::Travel) {
            result.approachBlend = ease(flight.orbitZoomProgress);
        }
    }
    result.approachLinear = result.approachBlend;
    result.timeScale = std::lerp(1.0, 0.4, result.approachBlend);
    result.controlScale = 1.0;
    result.landingFrameActive = flight.mode == FlightMode::Landing;
    if (result.landingFrameActive) {
        result.timeScale = result.controlScale = 1.0;
        result.landingBlend = flight.handoff.from == FlightMode::Orbit ? handoff : 1.0;
    } else if (flight.handoff.from == FlightMode::Landing) {
        result.landingBlend = 1.0-handoff;
    }
    result.landingLinear = result.landingBlend;
    return result;
}

void enterLocalLanding(FlightRunState& flight)
{
    flight.orbit.confirmationSeconds = 0.0;
    flight.orbit.loopQualifies = false;
    auto& land = flight.landing;
    land.basisAngle = std::atan2(flight.positionY, flight.positionX);
    const double nx = std::cos(land.basisAngle), ny = std::sin(land.basisAngle);
    // Local right is clockwise around the surface normal; right/up is a
    // positively oriented basis, so A/D never need another sign inversion.
    land.horizontalPosition = 0.0;
    land.altitude = flight_landing::entryAltitude;
    land.lateralVelocity = (flight.velocityX*ny-flight.velocityY*nx)*flight_landing::velocityConversion;
    land.verticalVelocity = (flight.velocityX*nx+flight.velocityY*ny)*flight_landing::velocityConversion;
    land.heading = flight.heading-land.basisAngle+1.5707963267948966;
    land.surfaceAngle = std::abs(flightWrappedAngleDelta(1.5707963267948966, land.heading));
    land.gateArmed = false;
    flight.handoff = {FlightMode::Orbit, FlightMode::Landing, 0.0,
        flight.positionX, flight.positionY, flight.heading};
    flight.mode = FlightMode::Landing;
    flight.phase = FlightPhase::Landing;
}

void leaveLocalLanding(FlightRunState& flight)
{
    const auto& land = flight.landing;
    const double nx = std::cos(land.basisAngle), ny = std::sin(land.basisAngle);
    const double radial = flight_geometry::bodyRadius+land.altitude/flight_landing::metersPerOrbitUnit;
    flight.positionX = nx*radial+ny*land.horizontalPosition/flight_landing::metersPerOrbitUnit;
    flight.positionY = ny*radial-nx*land.horizontalPosition/flight_landing::metersPerOrbitUnit;
    flight.velocityX = (nx*land.verticalVelocity+ny*land.lateralVelocity)/flight_landing::velocityConversion;
    flight.velocityY = (ny*land.verticalVelocity-nx*land.lateralVelocity)/flight_landing::velocityConversion;
    flight.heading = land.heading+land.basisAngle-1.5707963267948966;
    flight.handoff = {FlightMode::Landing, FlightMode::Orbit, 0.0,
        flight.positionX, flight.positionY, flight.heading};
    flight.mode = FlightMode::Orbit;
    flight.phase = flight.orbit.captured ? FlightPhase::Orbiting : FlightPhase::TargetApproach;
    flight.orbit.previousAngle = std::atan2(flight.positionY, flight.positionX);
    flight.landing.departureActive = false;
}

void bindLandingSite(FlightRunState& flight, const MiningRunState& mining)
{
    if (flight.landing.siteBound) return;
    flight.landing.siteBound = true;
    flight.landing.siteKey = mining.geologySeed;
    flight.landing.padGridX = mining.surfaceOriginBound ? mining.surfacePadX : mining.returnZoneX;
    flight.landing.padGridY = mining.surfaceOriginBound ? mining.surfacePadY : mining.returnZoneY;
}

std::vector<FlightSurfaceContact> localLandingContacts(const LandingState& land, const MiningRunState& mining,
    double hullMarginMeters)
{
    std::vector<FlightSurfaceContact> contacts;
    const double unit = flight_landing::metersPerCell;
    const double gridX = land.padGridX+land.horizontalPosition/unit;
    const double centerX=gridX+0.5;
    const double centerY = land.padGridY-(land.altitude+flight_landing::hullHalfHeight)/unit;
    const double c = std::cos(land.heading), s = std::sin(land.heading);
    const double margin = std::max(0.0, hullMarginMeters);
    const double halfW = (flight_landing::hullHalfWidth+margin)/unit;
    const double halfH = (flight_landing::hullHalfHeight+margin)/unit;
    const double extentX = std::abs(s)*halfW+std::abs(c)*halfH;
    const double extentY = std::abs(c)*halfW+std::abs(s)*halfH;
    const auto solid = [&](int x, int y) {
        if (y < 0 || x < 0 || x >= mining.terrain.width || y >= mining.terrain.height) return false;
        const auto i = static_cast<std::size_t>(y*mining.terrain.width+x);
        return i < mining.terrain.cells.size() &&
            mining.terrain.cells[i].material != MiningCellMaterial::Empty &&
            mining.terrain.cells[i].remainingToughness > 0.0;
    };
    // OBB versus solid terrain squares. Minimum translation axes provide real
    // face normals, including side/ceiling contact rather than a fake floor.
    for (int y=std::max(0,static_cast<int>(std::floor(centerY-extentY))); y<=std::min(mining.terrain.height-1,static_cast<int>(std::floor(centerY+extentY))); ++y) {
        for (int x=std::max(0,static_cast<int>(std::floor(centerX-extentX))); x<=std::min(mining.terrain.width-1,static_cast<int>(std::floor(centerX+extentX))); ++x) {
            if (!solid(x,y)) continue;
            const double dx=centerX-(x+0.5),dy=(y+0.5)-centerY;
            double depth=1e9,nx=0.0,ny=1.0;
            bool separated=false;
            const double axes[4][2]={{1,0},{0,1},{c,s},{s,-c}};
            for (const auto& axis:axes) {
                const double ax=axis[0],ay=axis[1];
                const double projection=dx*ax+dy*ay;
                const double overlap=halfH*std::abs(c*ax+s*ay)+halfW*std::abs(s*ax-c*ay)+
                    0.5*(std::abs(ax)+std::abs(ay))-std::abs(projection);
                if (overlap<0.0) {separated=true;break;}
                if (overlap<depth) {
                    depth=overlap;
                    const double sign=projection<0.0 ? -1.0 : 1.0;
                    nx=ax*sign;ny=ay*sign;
                }
            }
            if (separated) continue;
            // Adjacent solid tiles are one surface, not independent walls at
            // every grid seam. Ignore faces buried inside that solid union.
            if ((nx>0.999 && solid(x+1,y)) || (nx<-0.999 && solid(x-1,y)) ||
                (ny>0.999 && solid(x,y-1)) || (ny<-0.999 && solid(x,y+1))) continue;
            const auto supportSign=[](double v) {return std::abs(v)<1e-8 ? 0.0 : std::copysign(1.0,v);};
            const double along=supportSign(c*nx+s*ny),across=supportSign(s*nx-c*ny);
            const double px=std::clamp(centerX-c*halfH*along-s*halfW*across,static_cast<double>(x),x+1.0);
            const double py=std::clamp(centerY+s*halfH*along-c*halfW*across,static_cast<double>(y),y+1.0);
            FlightSurfaceContact contact;
            contact.normalX=nx;contact.normalY=ny;contact.penetration=depth*unit;
            contact.pointX=(px-land.padGridX-0.5)*unit;
            contact.pointY=(land.padGridY-py)*unit;
            contact.gridX=gridX;contact.gridY=y;
            contact.suitable=margin==0.0 && ny>=std::cos(flight_landing::stickTiltRadians)-1e-9 && std::abs(centerY+extentY-y)<0.45 &&
                solid(static_cast<int>(std::floor(centerX-halfW*0.85)),y) &&
                solid(static_cast<int>(std::floor(centerX+halfW*0.85)),y);
            if (contact.suitable) {
                double rigX=0.0,rigY=0.0;
                contact.suitable=surfaceLandingStaging(mining,gridX,y,rigX,rigY);
            }
            contacts.push_back(contact);
        }
    }
    return contacts;
}

bool localLandingContact(const LandingState& land,const MiningRunState& mining,
    bool& suitable,double& gridX,double& gridY)
{
    const auto contacts=localLandingContacts(land,mining);
    suitable=false;
    for (const auto& contact:contacts) {
        gridX=contact.gridX;gridY=contact.gridY;
        if (contact.suitable) {suitable=true;break;}
    }
    return !contacts.empty();
}

double flightImpactDamage(double speed)
{
    return std::max(0.0,speed-8.0)*5.0;
}

double flightContactSpeed(double vx,double vy,double omega,double armX,double armY,double nx,double ny)
{
    return std::max(0.0,-((vx-omega*armY)*nx+(vy+omega*armX)*ny));
}

void applyFlightImpact(FlightRunState& flight,double speed,double x,double y)
{
    const double damage=flightImpactDamage(speed),before=flight.hullRemaining;
    flight.hullRemaining=std::max(0.0,before-damage);
    // Quiet settling must not erase the meaningful impact the player is reading.
    if (speed>1.0 && (damage>0.0 || !flight.impact.valid ||
        (flight.impact.damage==0.0 && flight.impactDisplaySeconds<=0.0))) {
        flight.impact={true,speed,damage,before,flight.hullRemaining,x,y,flight.destinationId};
        flight.impactDisplaySeconds=3.0;
    }
}

void reboundFlightContact(double& vx,double& vy,double& omega,double nx,double ny,double armX,double armY)
{
    const double closing=flightContactSpeed(vx,vy,omega,armX,armY,nx,ny);
    if (closing<=0.0) return;
    const double normal=vx*nx+vy*ny;
    const double tangentX=vx-normal*nx,tangentY=vy-normal*ny;
    omega*=0.5;
    // Cancel residual inward corner rotation as well as centre-of-mass motion.
    const double rotationalNormal=(-omega*armY)*nx+(omega*armX)*ny;
    const double outward=std::max(0.0,closing*0.15-rotationalNormal);
    vx=tangentX*0.85+nx*outward;
    vy=tangentY*0.85+ny*outward;
}

int physicalFlightCampaignDamage(const FlightRunState& flight,int existingDamage)
{
    const double lost=100.0*(1.0-flight.hullRemaining/std::max(1.0,flight.hullMaximum));
    const int total=std::clamp(static_cast<int>(std::round(lost)),0,flight.hullRemaining>0.0 ? 99 : 100);
    return std::max(0,total-existingDamage);
}

double flightGravityAcceleration(double radius, double landingBlend)
{
    const double clampedRadius = std::max(0.0001, radius);
    const double baseGravity = clampedRadius < flight_geometry::influenceRadius
        ? std::min(0.52, 0.095 / (clampedRadius * clampedRadius))
        : 0.0025;
    // Used only by space flight. Local Landing owns constant downward gravity.
    (void)landingBlend;
    return baseGravity;
}

double flightThrottleForInput(double previousThrottle, double requestedThrottle,
    double realDeltaSeconds, bool analogThrottle)
{
    const double requested = std::clamp(requestedThrottle, -1.0, 1.0);
    if (analogThrottle || std::abs(requested) <= 0.001) {
        return requested; // Analog is proportional; releasing cuts immediately.
    }
    // Keyboard taps make small burns; holding reaches full power in 0.4 s.
    // Reverse starts a new ramp, never a frame of thrust in the old direction.
    const double previous = previousThrottle * requested > 0.0
        ? std::clamp(std::abs(previousThrottle), 0.0, 1.0) : 0.0;
    const double power = std::min(std::abs(requested),
        previous + std::max(0.0, realDeltaSeconds) / flight_controls::keyboardThrottleRiseSeconds);
    return std::copysign(power, requested);
}

bool flightOrbitStable(
    const FlightKinematics& kinematics,
    double targetRadius,
    double goodBand)
{
    return std::abs(kinematics.radius - targetRadius) <= goodBand
        && std::abs(kinematics.radialVelocity) <= 0.075
        && std::abs(kinematics.tangentialVelocity) >= 0.34
        && std::abs(kinematics.tangentialVelocity) <= 0.58;
}

FlightTouchdownOutcome classifyFlightTouchdown(
    bool landingAuthorized,
    double downwardVelocityMetersPerSecond,
    double lateralVelocityMetersPerSecond,
    double tiltDegrees)
{
    if (landingAuthorized
        && downwardVelocityMetersPerSecond <= flight_landing::safeVerticalSpeed
        && lateralVelocityMetersPerSecond <= flight_landing::safeLateralSpeed
        && tiltDegrees <= flight_landing::safeTiltDegrees) {
        return FlightTouchdownOutcome::Safe;
    }
    if (landingAuthorized
        && downwardVelocityMetersPerSecond <= flight_landing::hardVerticalSpeed
        && lateralVelocityMetersPerSecond <= flight_landing::hardLateralSpeed
        && tiltDegrees <= flight_landing::hardTiltDegrees) {
        return FlightTouchdownOutcome::Hard;
    }
    return FlightTouchdownOutcome::Impact;
}

} // namespace rocket
