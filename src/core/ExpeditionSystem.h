#pragma once
#include "core/FlightSystem.h"
#include "core/GameTypes.h"
#include "core/LaunchSimulation.h"

namespace rocket
{
inline constexpr double expeditionDockRadius = 2.0;
inline constexpr double expeditionDockSpeed = 1.0;
namespace service_dock {
// Enter the close-up only when the dock is visually relevant. The wider
// exit boundary prevents an abort/re-entry loop while the player backs away.
inline constexpr double approachRadius = 1.0;
inline constexpr double exitRadius = 1.25;
inline constexpr double entrySpeedScale = 0.35;
inline constexpr double entryMaxSpeed = 0.45; // Local units/s, including lateral motion.
// Convert system distances to the expanded close-up frame. Entry velocity is
// converted on the same scale, then reduced once for precision maneuvering.
inline constexpr double localUnitsPerSystemUnit = 4.0;
// The service-dock art is a U-shaped cradle which opens along +Y. Keep all
// physical and presentation measurements in this shared dock-local frame so
// the visible tongs, guidance, and collision/capture volumes cannot drift.
inline constexpr double artWorldSize = 1.75;
inline constexpr double handoffIconWorldSize = 0.32;
inline constexpr double closeCameraScale = 0.82;
inline constexpr double shipRenderSize = 0.55;
inline constexpr double mouthY = 0.78;
inline constexpr double backstopY = 0.08;
inline constexpr double channelHalfWidth = 0.34;
inline constexpr double outerHalfWidth = 0.86;
inline constexpr double hullRadius = 0.10;
inline constexpr double hullHalfLength = 0.14;
inline constexpr double shipLength = 2.0 * (hullHalfLength + hullRadius);
inline constexpr double captureCenterY = 0.42;
inline constexpr double captureHalfDepth = 0.05;
inline constexpr double captureHalfWidth = 0.12;
inline constexpr double guideHalfDepth = 0.12;
inline constexpr double captureHeadingRadians = 0.2617993877991494;
inline constexpr double captureForwardSpeed = 2.0;
inline constexpr double captureLateralSpeed = 1.0;
inline constexpr double captureSeconds = 0.5;
inline constexpr double settleSeconds = 0.35;
inline constexpr double clampLockSeconds = 0.85;
inline constexpr double arrivalFadeSeconds = 1.75;
inline constexpr double securingSeconds = 2.0;
inline constexpr double contactRearmSeconds = 0.15;
inline constexpr double bumpFeedbackSeconds = 0.32;
}
inline constexpr double expeditionSalvageRadius = 2.0;
inline constexpr double expeditionSalvageSpeed = 1.0;
enum class CampaignObjectiveKind { Mission, SecureArtifact, RecoverArtifact, RecoveryUnavailable, Complete };
struct CampaignObjective {
    CampaignObjectiveKind kind = CampaignObjectiveKind::Complete;
    std::string targetId, artifactId, title, detail;
    std::uint64_t wreckId = 0;
};
// Reserved wreck:<id> targets reuse the existing saved course field.
const WreckState* courseWreck(const PersistentExpeditionState&, std::string_view target);
std::optional<SystemLocation> courseTargetLocation(const PersistentExpeditionState&, const SystemDefinition&, std::string_view target);
std::string courseTargetName(const PersistentExpeditionState&, const SystemDefinition&, std::string_view target);
bool wreckCarriesArtifact(const PersistentExpeditionState&, std::uint64_t wreckId);
std::string wreckDisplayName(const PersistentExpeditionState&, std::uint64_t wreckId);
CampaignObjective recommendedCampaignObjective(const GameState&, const ContentCatalog&);
bool reconcileCampaignGuidance(GameState&, const ContentCatalog&, bool followNow = false);

enum class ExpeditionResult
{
    Applied,
    AlreadyApplied,
    InvalidTarget,
    NotAtSite,
    NotDocked,
    NotOperational,
    NotOwner,
    MissingBatteries,
    OutOfRange,
    InvalidState
};
SystemLocation convertSystemFrame(const SystemLocation &, CoordinateFrame, std::string_view bodyId,
                                  const SystemDefinition &);
// Flight is authoritative while running; capture a persistent boundary snapshot.
void captureSystemLocation(SystemLocation &, const FlightRunState &);
void restoreSystemLocation(const SystemLocation &, FlightRunState &);
const SystemBodyDefinition *encounteredBody(const SystemLocation &, const SystemDefinition &);
CoastPredictionPose integrateSystemCoast(CoastPredictionPose, double dt, const SystemDefinition &);
CoursePlan previewSystemCourse(const SystemLocation &, const FlightRunState &, const SystemDefinition &,
                               std::string_view target, std::string_view home, const PreparedLaunch* model = nullptr);
ExpeditionResult plotSystemCourse(PersistentExpeditionState &, const FlightRunState &,
                                  const SystemDefinition &, std::string_view target, const PreparedLaunch* model = nullptr);
ExpeditionResult toggleCruise(PersistentExpeditionState &);
FlightInput cruiseInput(PersistentExpeditionState &, const FlightRunState &, const SystemDefinition &,
                        FlightInput manual, bool heatEnabled = true);
LaunchFlightStep advanceExpeditionFlight(PersistentExpeditionState &, FlightRunState &,
                                         const PreparedLaunch &, const Destination &,
                                         const SystemDefinition &, FlightInput, double deltaSeconds,
                                         const MiningRunState *landingSite = nullptr);
int batteryResearchRank(const PersistentExpeditionState &);
bool validBatteryOwnership(const PersistentExpeditionState &);
ExpeditionResult recoverSiteBattery(PersistentExpeditionState &, std::string_view batteryId);
ExpeditionResult dockExpedition(PersistentExpeditionState &, FlightRunState &, const SystemDefinition &);
ExpeditionResult dockExpedition(GameState &, const SystemDefinition &);
ExpeditionResult departDock(PersistentExpeditionState &, FlightRunState &);
ExpeditionResult loadEarthBattery(PersistentExpeditionState &, std::string_view batteryId);
ExpeditionResult installArkBattery(PersistentExpeditionState &, std::string_view batteryId);
ExpeditionResult activateStraylight(PersistentExpeditionState &);
ExpeditionResult salvageWreck(PersistentExpeditionState &, std::uint64_t wreckId, const SystemDefinition &, int holdCapacity = 2147483647);
ExpeditionResult resolveRecoveredGraftConflict(PersistentExpeditionState&, int conflictIndex, bool useRecovered);
ExpeditionResult loseExpedition(PersistentExpeditionState &, FlightRunState &, const SystemDefinition &);
ExpeditionResult useShipSupplies(PersistentExpeditionState &, FlightRunState &);
void storeVisitedSite(GameState &, std::string_view siteId);
bool restoreVisitedSite(GameState &, std::string_view siteId);
bool initializeLiveExpedition(GameState &, const ContentCatalog &);
bool beginEarthOpening(GameState &, const ContentCatalog &);
bool openingMissionRetryEligible(const GameState &);
std::string_view openingRetryMessageVariant(const GameState &);
ExpeditionResult retryOpeningMission(GameState &, const ContentCatalog &);
bool earthLaunchReady(const PersistentExpeditionState &);
ExpeditionResult launchEarthOpening(GameState &, const ContentCatalog &);
FlightGuidance expeditionGuidance(const GameState &, bool surveyed = false, bool laserComplete = false);
void refreshExpeditionTrajectory(PersistentExpeditionState &, FlightRunState &, const PreparedLaunch &, const Destination &, const SystemDefinition &);
bool operationalHomeDocked(const PersistentExpeditionState &);
bool expeditionMapBodyRevealed(const GameState &, const SystemBodyDefinition &);
void recordExpeditionArrival(GameState &, const ContentCatalog &, const LaunchOutcome &);
bool expeditionDockInRange(const PersistentExpeditionState &, const FlightRunState &, const SystemDefinition &, std::string_view dockBodyId = {});
bool earthDockingActive(const FlightRunState&);
std::string earthDockingGuidance(const FlightRunState&);
bool canDockExpedition(const PersistentExpeditionState &, const FlightRunState &, const SystemDefinition &);
bool canSalvageWreck(const PersistentExpeditionState &, const FlightRunState &, const SystemDefinition &, std::uint64_t id, bool requireMatchedSpeed = true);
const Destination &expeditionEnvironment(const GameState &, const ContentCatalog &);
PreparedLaunch expeditionFlightModel(const GameState &, const ContentCatalog &);
ExpeditionResult departHome(GameState &, const ContentCatalog &);
ExpeditionResult recoverExpedition(GameState &, const SystemDefinition &);
std::string recommendedExpeditionLead(const GameState &, const ContentCatalog &);
void queueExpeditionDecision(GameState &, const ContentCatalog &);
bool acknowledgeExpeditionDecision(GameState &, std::string_view occurrence);
} // namespace rocket
