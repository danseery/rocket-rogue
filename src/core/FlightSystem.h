#pragma once

#include "core/GameTypes.h"

namespace rocket {

const std::array<PlanetLandingZone, 6>& planetLandingZones();
const PlanetLandingZone* planetLandingZone(std::string_view id);
bool landingZoneContains(const PlanetLandingZone& zone, double bearing);
const PlanetLandingZone* enabledLandingZoneAt(double bearing);
double landingZoneSiteBearing(const PlanetLandingZone& zone, double gridX,
    double padX, double siteWidth);

namespace flight_geometry {
inline constexpr double startX = -3.40;
inline constexpr double startY = 1.10;
inline constexpr double bodyRadius = 0.16;
inline constexpr double influenceRadius = 1.42;
inline constexpr double landingBoundary = 0.40;
inline constexpr double velocityToMetersPerSecond = 26.0;
} // namespace flight_geometry

namespace flight_scale {
inline constexpr double approachStartProgress = 0.60;
inline constexpr double approachEndProgress = 0.95;
inline constexpr double approachStartDistanceShare = 0.25;
inline constexpr double zoomOutBuffer = 0.10;
inline constexpr double outwardBufferVelocity = 0.04;
inline constexpr double closeRangeTimeScale = 0.40;
inline constexpr double closeRangeControlScale = 1.00;
inline constexpr double landingTimeScale = 0.20;
inline constexpr double landingControlScale = 0.85;
} // namespace flight_scale

namespace flight_controls {
inline constexpr double turnAcceleration = 5.50; // 25% above the previous 4.40.
inline constexpr double keyboardThrottleRiseSeconds = 0.40;
} // namespace flight_controls

namespace flight_landing {
inline constexpr double entryAltitude = 60.0;
inline constexpr double departureAltitude = 60.0;
inline constexpr double departureSpeed = 2.0;
inline constexpr double velocityConversion = 12.0;
inline constexpr double metersPerOrbitUnit = 250.0;
inline constexpr double metersPerCell = 4.0;
inline constexpr double gravityAcceleration = 3.0;
inline constexpr double forwardAcceleration = 6.0;
inline constexpr double reverseAcceleration = 3.0;
inline constexpr double turnRate = 1.3089969389957472;
inline constexpr double turnResponseSeconds = 0.15;
inline constexpr double stickTiltRadians = 0.5235987755982988; // 30 degrees from upright.
inline constexpr double touchdownSettleSeconds = 0.60;
inline constexpr double takeoffClearanceMeters = 0.50;
inline constexpr double takeoffClearSeconds = 0.20;
inline constexpr double handoffSeconds = 1.25;
inline constexpr double hullHalfWidth = 5.2;
inline constexpr double hullHalfHeight = 15.47;
inline constexpr double gateHalfAngle = 0.5235987755982988;
inline constexpr double gateRearmRadius = 0.55;
inline constexpr double safeVerticalSpeed = 8.0;
inline constexpr double safeLateralSpeed = 5.0;
inline constexpr double safeTiltDegrees = 25.0;
inline constexpr double hardVerticalSpeed = 16.0;
inline constexpr double hardLateralSpeed = 10.0;
inline constexpr double hardTiltDegrees = 60.0;
} // namespace flight_landing

struct FlightKinematics {
    double radius = 0.0;
    double radialVelocity = 0.0;
    double tangentialVelocity = 0.0;
    double angle = 0.0;
};

struct CoastPredictionPose { double x, y, vx, vy; };
struct OrbitLoopAssessment { bool qualifies = false; bool perfect = false; };
CoastPredictionPose stepCoastPrediction(CoastPredictionPose pose, double dt);
OrbitLoopAssessment assessOrbitLoop(const FlightRunState& flight);
double orbitConfirmationProgress(const FlightRunState& flight);

struct FlightScaleProfile {
    double approachLinear = 0.0;
    double approachBlend = 0.0;
    double landingLinear = 0.0;
    double landingBlend = 0.0;
    double timeScale = 1.0;
    double controlScale = 1.0;
    bool landingFrameActive = false;
};

enum class FlightTouchdownOutcome {
    Safe,
    Hard,
    Impact
};

double flightWrappedAngleDelta(double from, double to);
FlightKinematics flightKinematics(
    double positionX,
    double positionY,
    double velocityX,
    double velocityY);
FlightScaleProfile flightScaleProfile(
    const FlightKinematics& kinematics,
    double travelProgress,
    double orbitTargetRadius,
    double orbitGoodBand,
    bool enteredInfluence,
    bool landingAuthorized,
    bool landingFrameActive);
FlightScaleProfile flightScaleProfile(const FlightRunState& flight);
void enterLocalLanding(FlightRunState& flight);
void leaveLocalLanding(FlightRunState& flight);
void bindLandingSite(FlightRunState& flight, const MiningRunState& mining);
// Contact returns false in clear air; suitable is true only with real support
// under both feet and room for the deployed rig beside the shuttle.
bool localLandingContact(const LandingState& landing, const MiningRunState& mining,
    bool& suitable, double& gridX, double& gridY);

struct FlightSurfaceContact {
    double normalX = 0.0;
    double normalY = 1.0;
    double penetration = 0.0;
    // Local metres, relative to the fixed pad (same frame as landing pose).
    double pointX = 0.0;
    double pointY = 0.0;
    double gridX = 0.0;
    double gridY = 0.0;
    bool suitable = false;
};
std::vector<FlightSurfaceContact> localLandingContacts(const LandingState&, const MiningRunState&,
    double hullMarginMeters = 0.0);
double flightImpactDamage(double impactSpeed);
double flightContactSpeed(double velocityX, double velocityY, double angularVelocity,
    double armX, double armY, double normalX, double normalY);
void applyFlightImpact(FlightRunState&, double speed, double contactX, double contactY);
void reboundFlightContact(double& velocityX, double& velocityY, double& angularVelocity,
    double normalX, double normalY, double armX, double armY);
int physicalFlightCampaignDamage(const FlightRunState&, int existingDamage);
double flightGravityAcceleration(double radius, double landingBlend);
double flightThrottleForInput(double previousThrottle, double requestedThrottle,
    double realDeltaSeconds, bool analogThrottle);
bool flightOrbitStable(
    const FlightKinematics& kinematics,
    double targetRadius,
    double goodBand);
FlightTouchdownOutcome classifyFlightTouchdown(
    bool landingAuthorized,
    double downwardVelocityMetersPerSecond,
    double lateralVelocityMetersPerSecond,
    double tiltDegrees);

} // namespace rocket
