#pragma once
#include "core/FlightSystem.h"
#include "core/GameTypes.h"
#include "core/LaunchSimulation.h"

namespace rocket
{
inline constexpr double expeditionDockRadius = 2.0;
inline constexpr double expeditionDockSpeed = 1.0;
namespace service_dock {
inline constexpr double approachRadius = expeditionDockRadius;
inline constexpr double exitRadius = expeditionDockRadius * 1.25;
inline constexpr double mouthY = 0.58;
inline constexpr double rearY = -0.58;
inline constexpr double channelHalfWidth = 0.34;
inline constexpr double outerHalfWidth = 0.86;
inline constexpr double hullRadius = 0.13;
inline constexpr double rotationRateRadians = 0.5235987755982988;
inline constexpr double captureHeadingRadians = 0.2617993877991494;
inline constexpr double captureForwardSpeed = 2.0;
inline constexpr double captureLateralSpeed = 1.0;
inline constexpr double captureSeconds = 0.5;
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
