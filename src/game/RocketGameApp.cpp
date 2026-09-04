#include "game/RocketGameApp.h"

#include "core/FlightProgress.h"
#include "core/FlightInstrumentPresentation.h"
#include "core/SurfaceBayTiming.h"
#include "core/FlightSystem.h"
#include "core/ContentIds.h"
#include "core/GameUi.h"
#include "core/GameText.h"
#include "core/GameMath.h"
#include "core/LaunchPresentation.h"
#include "core/MiniDroneCoordination.h"
#include "core/MiningSystem.h"
#include "core/PostSolarSystem.h"
#include "core/ResearchSystem.h"
#include "core/ScenarioSystem.h"
#include "core/RefitPresentation.h"
#include "core/Tuning.h"
#include "core/SaveData.h"
#include "game/GamePanel.h"
#include "input/MiningInputTransform.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace rocket {
namespace {

constexpr double asteroidImpactFeedbackDuration = 0.32;
constexpr double kTouchdownCelebrationSeconds = surface_bay_timing::touchdownSeconds;
constexpr double kSurfaceDeploymentSeconds = surface_bay_timing::deploymentSeconds;
constexpr double kSurfaceUndeployedTakeoffSeconds = surface_bay_timing::shipOnlyDepartureSeconds;
constexpr std::uint32_t kArrivalAudioRigEjection = 1U << 0U;
constexpr std::uint32_t kArrivalAudioArrestingBurst = 1U << 1U;
constexpr std::uint32_t kArrivalAudioRigImpact = 1U << 2U;
constexpr std::uint32_t kArrivalAudioBayClose = 1U << 3U;
constexpr std::uint32_t kArrivalAudioSurfaceReady = 1U << 4U;
// The renderer displays at most this many scan/push markers. Keep snapshot
// construction bounded too, so a malformed saved prospect cannot allocate a
// large transient vector before the renderer reaches its own visual limit.
constexpr std::size_t kMaxSurfaceProspectMarkers = 14U;
constexpr std::size_t kMaxSurfacePushRewardMarkers = 10U;
constexpr double kLevelUpFanfareSeconds = 0.70;
constexpr double kLevelUpActivationFenceSeconds = 0.35;
constexpr double kLevelUpRefreshFenceSeconds = 0.22;
constexpr double kLevelUpSelectionResolveSeconds = 0.10;
constexpr double kTitleLaunchIgnitionDelaySeconds = 0.50;
constexpr double kTitleLaunchDepartureSeconds = 0.65 / 0.75;
constexpr double kTitleLaunchSequenceSeconds =
    kTitleLaunchIgnitionDelaySeconds + kTitleLaunchDepartureSeconds;
constexpr double kSceneFadeToBlackSeconds = 1.0;
constexpr double kSceneFadeFromBlackSeconds = 0.25;
constexpr double kMiningEvaDeathImpactSeconds = 1.15;
constexpr double kMiningEvaDeathFadeToBlackSeconds = 0.62;
constexpr double kMiningEvaDeathFadeFromBlackSeconds = 0.32;

int destinationIndexForId(const ContentCatalog& catalog, std::string_view destinationId);

std::uint64_t surfaceSiteSeed(
    std::uint64_t campaignSeed,
    std::string_view destinationId,
    int landingOrdinal)
{
    std::uint64_t hash = campaignSeed ^ 0x9E3779B97F4A7C15ULL;
    for (const unsigned char byte : destinationId) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= 1099511628211ULL;
    }
    hash ^= static_cast<std::uint64_t>(std::max(0, landingOrdinal));
    hash *= 0xBF58476D1CE4E5B9ULL;
    return hash == 0 ? 1 : hash;
}

void awardUnifiedOrbit(
    GameState& state,
    const ContentCatalog& catalog,
    const Destination& destination,
    OrbitGrade grade)
{
    if (grade != OrbitGrade::Good && grade != OrbitGrade::Perfect) {
        return;
    }
    const int destinationIndex = destinationIndexForId(catalog, destination.id);
    if (destinationIndex >= 0) {
        if (state.meta.destinationOrbits.size() < catalog.destinations.size()) {
            state.meta.destinationOrbits.resize(catalog.destinations.size(), 0);
        }
        ++state.meta.destinationOrbits[static_cast<std::size_t>(destinationIndex)];
    }
    state.run.credits += orbitCreditReward(destination, grade);
    state.meta.blueprintProgress += orbitResearchDataReward(destination, grade);
    unlockFromBlueprints(state);
}

struct DebugActOneCheckpoint {
    std::string_view label;
    std::string_view destinationId;
};

constexpr std::array<DebugActOneCheckpoint, 7> kDebugActOneCheckpoints {{
    {"Moon / Fuel Training", content::destination::moon},
    {"Moon", content::destination::moon},
    {"Mars", content::destination::mars},
    {"Jupiter", content::destination::jupiter},
    {"Saturn", content::destination::saturn},
    {"Uranus", content::destination::uranus},
    {"Neptune", content::destination::neptune}
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
    outcome.transferFuelRemaining = 5.0;
    outcome.transferFuelCapacity = 20.0;
    outcome.blueprintGain = 3;
    outcome.peakWarning = 0.38;
    outcome.peakAbortRisk = 0.0;
    outcome.telemetry = {
        {1.0, 0.12, 0.10, 0.05, 0.0, 0.86, 0.0, 0.0, 0.10, 0.12, "Debug launch nominal."},
        {1.7, 0.32, 0.24, 0.20, 0.0, 0.70, 0.0, 0.0, 0.18, 0.28, "Transfer burn committed."},
        {2.4, 0.46, 0.36, 0.30, 0.0, 0.62, 0.0, 0.0, 0.26, 0.38, "Arrival corridor reached."}
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
    addDebugUnlock(state, content::unlock::droneSupportSuite);
    addDebugUnlock(state, content::unlock::perimeterDrones);
    state.meta.materials.common = std::max(state.meta.materials.common, 18);
    state.meta.materials.rare = std::max(state.meta.materials.rare, 8);
    state.meta.materials.exotic = std::max(state.meta.materials.exotic, 2);
    state.meta.blueprintProgress = std::max(state.meta.blueprintProgress, 7);
}

void seedDebugDroneBay(GameState& state, const ContentCatalog& catalog)
{
    addDebugUnlock(state, content::unlock::droneBay);
    addDebugUnlock(state, content::unlock::droneSupportSuite);
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
    state.run.approach = {true, std::string(destinationId)};
    startSurfaceExpedition(state, catalog, &rng);
    PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
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
    const FlightRunState& finalFlight,
    bool returningHome)
{
    constexpr int sampleCapacity = tuning::launch::telemetrySampleCount;
    std::vector<TelemetryEvent> telemetry;
    telemetry.reserve(sampleCapacity);
    for (int i = 0; i < sampleCapacity; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(sampleCapacity - 1);
        FlightRunState sample = finalFlight;
        const double routeShape = returningHome ? std::sin(t * math::pi) : t;
        sample.currentMultiplier = 1.0 + (finalFlight.peakMultiplier - 1.0) * routeShape;
        sample.heat = tuning::launch::pilotingHeatInitial +
            (finalFlight.heat - tuning::launch::pilotingHeatInitial) * t;
        sample.courseOffset = finalFlight.courseOffset * t;
        sample.fuelRemaining = sample.fuelCapacity -
            (sample.fuelCapacity - finalFlight.fuelRemaining) * t;
        sample.heatFailureSeconds = finalFlight.heatFailureSeconds * t;
        sample.courseFailureSeconds = finalFlight.courseFailureSeconds * t;
        sample.fuelFailureSeconds = finalFlight.fuelFailureSeconds * t;
        telemetry.push_back(launchTelemetryAt(launch, sample));
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

struct ScenarioActionAddress {
    std::string scenarioId;
    std::string stepId;
    ScenarioActionKind action = ScenarioActionKind::None;
};

bool parseScenarioAction(std::string_view encoded, ScenarioActionAddress& result)
{
    if (!encoded.starts_with(ui::actions::scenarioActionPrefix)) {
        return false;
    }
    const std::string_view payload = encoded.substr(ui::actions::scenarioActionPrefix.size());
    const std::size_t first = payload.find('|');
    const std::size_t second = first == std::string_view::npos ? std::string_view::npos : payload.find('|', first + 1);
    if (first == std::string_view::npos || second == std::string_view::npos ||
        first == 0 || second == first + 1 || second + 1 >= payload.size()) {
        return false;
    }
    const std::string_view rawAction = payload.substr(second + 1);
    int actionValue = 0;
    for (const char c : rawAction) {
        if (c < '0' || c > '9') {
            return false;
        }
        actionValue = actionValue * 10 + (c - '0');
    }
    if (actionValue <= static_cast<int>(ScenarioActionKind::None) ||
        actionValue > static_cast<int>(ScenarioActionKind::AcknowledgeFailure)) {
        return false;
    }
    result.scenarioId = std::string(payload.substr(0, first));
    result.stepId = std::string(payload.substr(first + 1, second - first - 1));
    result.action = static_cast<ScenarioActionKind>(actionValue);
    return true;
}

std::string encodeScenarioAction(
    std::string_view scenarioId,
    std::string_view stepId,
    ScenarioActionKind action)
{
    return std::string(ui::actions::scenarioActionPrefix) + std::string(scenarioId) + "|" +
        std::string(stepId) + "|" + std::to_string(static_cast<int>(action));
}

void recordActiveMiningScenarioAbort(GameState& state, const ContentCatalog& catalog)
{
    const MiningRunState& mining = state.run.mining;
    if (mining.scenarioId.empty() || mining.scenarioStepId.empty()) {
        return;
    }
    (void)recordScenarioEvent(
        state,
        catalog,
        {ScenarioEventKind::ActivityAborted,
         mining.scenarioId,
         mining.scenarioStepId,
         mining.miningSiteDefinitionId,
         {},
         0,
         0});
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
        for (int i = 0; i < std::max(0, count)
            && markers.size() < kMaxSurfaceProspectMarkers; ++i) {
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

RocketGameApp::RocketGameApp(AppServices& services)
    : services_(services), session_(state_.run.flight)
{
}

PreparedLaunch RocketGameApp::currentFlightModel() const
{
    return session_.preparedLaunch;
}

void RocketGameApp::recordTelemetryPeak(const TelemetryEvent& event)
{
    session_.peakWarning = std::max(session_.peakWarning, event.warning);
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
    if (state_.storyBriefing.pending == StoryBriefingId::StraylightDiscovery) {
        session_.arrivalFanfare = {};
        state_.screen = Screen::StoryBriefing;
        state_.statusLine = "An impossible contact is resolving beyond Neptune.";
    } else {
        // This transient phase owns only presentation. It adds no choice,
        // confirmation, reward authority, or serialized state; after two
        // seconds it yields directly to the already-prepared Approach.
        session_.arrivalFanfare = {true, 0.0};
        state_.screen = Screen::ArrivalFanfare;
        state_.statusLine = "Arrival confirmed.";
        queueControllerHapticCue(ControllerHapticCue::Arrival);
    }
}

void RocketGameApp::finishArrivalFanfare()
{
    if (!session_.arrivalFanfare.active) {
        return;
    }
    session_.arrivalFanfare = {};
    if (state_.screen == Screen::ArrivalFanfare) {
        // Touchdown already was the landing decision. The celebration is
        // ceremonial only and yields directly to the continuous surface (or
        // the next eligible activity when a destination has no surface).
        beginSurfaceExpeditionOrRefit();
    }
    if (!firstTimeIntroductionsEnabled_
        && state_.run.approach.destinationId == content::destination::moon) {
        ui::briefings::acknowledge(state_.meta.acknowledgedActivityBriefingIds, ui::briefings::approach);
        save();
    }
    panelDirty_ = true;
}

bool RocketGameApp::levelUpActivationLocked() const
{
    return levelUp_.resolving || levelUp_.activationFenceSeconds > 0.0;
}

void RocketGameApp::maybeOpenLevelUpDraft()
{
    PlanetaryExpeditionState& expedition = state_.run.planetaryExpedition;
    if (titleScreenActive_ || services_.ui.modalOpen() || levelUp_.resolving) {
        return;
    }

    if (state_.screen == Screen::SurfaceUpgrade) {
        if (expedition.runUpgradeOfferPending) {
            // Revalidate drafts restored from before an encounter gate existed.
            // The generator is a no-op unless it finds a combat offer that the
            // player has not yet been introduced to.
            const auto offersBefore = expedition.runUpgradeOffers;
            const int offerCountBefore = expedition.runUpgradeOfferCount;
            (void)generateRunUpgradeOffers(state_, catalog_, rng_);
            const bool offersChanged = offerCountBefore != expedition.runUpgradeOfferCount ||
                std::any_of(
                    offersBefore.begin(),
                    offersBefore.end(),
                    [&](const RunUpgradeOffer& before) {
                        const std::size_t index = static_cast<std::size_t>(&before - offersBefore.data());
                        const RunUpgradeOffer& after = expedition.runUpgradeOffers[index];
                        return before.kind != after.kind || before.definitionId != after.definitionId ||
                            before.targetRank != after.targetRank || before.slotIndex != after.slotIndex;
                    });
            if (offersChanged) {
                save();
                panelDirty_ = true;
            }
            return;
        }
        if (expedition.pendingRunUpgradeChoices <= 0) {
            const Screen returnScreen = expedition.runUpgradeReturnScreen == Screen::SurfaceUpgrade
                ? Screen::Mining
                : expedition.runUpgradeReturnScreen;
            state_.screen = returnScreen;
            state_.statusLine = "Expedition upgrade installed.";
            levelUp_ = {};
            save();
            panelDirty_ = true;
            return;
        }
    } else {
        if (expedition.pendingRunUpgradeChoices <= 0) {
            return;
        }
        const bool priorityTransition = state_.screen == Screen::StoryBriefing
            || state_.screen == Screen::Results
            || state_.screen == Screen::ArrivalFanfare
            || (state_.screen == Screen::Flight &&
                state_.launchConfig.missionKind == LaunchMissionKind::StraylightApproach)
            || (state_.screen == Screen::Flyby && state_.run.approach.flyby.completed)
            || (state_.screen == Screen::Orbit && state_.run.approach.orbit.completed)
            || (state_.screen == Screen::Mining && state_.run.mining.failurePending)
            || surfaceBaySequence_.active();
        if (priorityTransition) {
            return;
        }
        expedition.runUpgradeReturnScreen = state_.screen;
    }

    // Exhausted pools consume their pending choice in the core generator.
    // Bound the loop defensively so malformed state can never stall a frame.
    for (int guard = 0;
         guard < 64 && expedition.pendingRunUpgradeChoices > 0 && !expedition.runUpgradeOfferPending;
         ++guard) {
        const int choicesBefore = expedition.pendingRunUpgradeChoices;
        if (generateRunUpgradeOffers(state_, catalog_, rng_)) {
            break;
        }
        if (expedition.pendingRunUpgradeChoices >= choicesBefore) {
            break;
        }
    }
    if (!expedition.runUpgradeOfferPending) {
        if (expedition.pendingRunUpgradeChoices <= 0) {
            if (state_.screen == Screen::SurfaceUpgrade) {
                state_.screen = expedition.runUpgradeReturnScreen;
            }
            state_.statusLine = "ALL ELIGIBLE UPGRADES INSTALLED";
            levelUp_ = {};
            save();
            panelDirty_ = true;
        }
        return;
    }

    const bool openingBatch = state_.screen != Screen::SurfaceUpgrade;
    if (openingBatch) {
        releaseRealtimeInputs(true);
        levelUp_ = {};
        levelUp_.fanfareActive = true;
        levelUp_.activationFenceSeconds = kLevelUpActivationFenceSeconds;
        levelUp_.batchChoices = expedition.pendingRunUpgradeChoices;
        state_.screen = Screen::SurfaceUpgrade;
        state_.statusLine = "LEVEL UP \xE2\x80\x94 choose one expedition upgrade.";
        queueControllerHapticCue(ControllerHapticCue::LevelUp);
    }
    save();
    panelDirty_ = true;
}

void RocketGameApp::finishLevelUpSelection()
{
    if (!levelUp_.resolving) {
        return;
    }
    levelUp_.resolving = false;
    levelUp_.resolveElapsed = 0.0;
    levelUp_.selectedOfferIndex = -1;

    PlanetaryExpeditionState& expedition = state_.run.planetaryExpedition;
    for (int guard = 0;
         guard < 64 && expedition.pendingRunUpgradeChoices > 0 && !expedition.runUpgradeOfferPending;
         ++guard) {
        const int choicesBefore = expedition.pendingRunUpgradeChoices;
        if (generateRunUpgradeOffers(state_, catalog_, rng_)) {
            break;
        }
        if (expedition.pendingRunUpgradeChoices >= choicesBefore) {
            break;
        }
    }

    if (expedition.runUpgradeOfferPending) {
        levelUp_.activationFenceSeconds = kLevelUpRefreshFenceSeconds;
        state_.statusLine = std::to_string(expedition.pendingRunUpgradeChoices) + " PICKS REMAIN";
    } else {
        const Screen returnScreen = expedition.runUpgradeReturnScreen == Screen::SurfaceUpgrade
            ? Screen::Mining
            : expedition.runUpgradeReturnScreen;
        state_.screen = returnScreen;
        state_.statusLine = expedition.pendingRunUpgradeChoices <= 0
            ? "Expedition upgrade installed."
            : "ALL ELIGIBLE UPGRADES INSTALLED";
        levelUp_ = {};
    }
    save();
    panelDirty_ = true;
}

void RocketGameApp::observeExpeditionExperience()
{
    const PlanetaryExpeditionState& expedition = state_.run.planetaryExpedition;
    if (!expeditionXpObservationInitialized_) {
        observedExpeditionLevel_ = expedition.expeditionLevel;
        observedExpeditionExperience_ = expedition.expeditionExperience;
        expeditionXpObservationInitialized_ = true;
        return;
    }
    const bool gained = expedition.expeditionLevel > observedExpeditionLevel_
        || (expedition.expeditionLevel == observedExpeditionLevel_
            && expedition.expeditionExperience > observedExpeditionExperience_ + 0.0001);
    if (gained) {
        expeditionXpPulseSeconds_ = expedition.expeditionLevel > observedExpeditionLevel_
            ? kLevelUpFanfareSeconds
            : 0.36;
        realtimeHudDirty_ = true;
    }
    observedExpeditionLevel_ = expedition.expeditionLevel;
    observedExpeditionExperience_ = expedition.expeditionExperience;
}

void RocketGameApp::beginSurfaceExpeditionOrRefit()
{
    startSurfaceExpedition(state_, catalog_, &rng_);
    if (state_.run.planetaryExpedition.active) {
        const SurfaceActionOutcome mining = startMiningRun(state_, catalog_);
        state_.statusLine = mining.applied
            ? std::string(text::status::miningStarted)
            : surfaceActionSummary(mining);
    } else if (openRefitIfAvailable()) {
        state_.statusLine = std::string(text::status::refitWindowOpened);
    } else {
        state_.screen = navigationAvailable(state_) ? Screen::Navigation : Screen::Hangar;
        state_.statusLine = std::string(text::status::refitWindowClosed);
    }
}

void RocketGameApp::prepareSurfaceArrivalIfNeeded(const Destination& destination)
{
    if (!destinationSupportsSurface(destination)) {
        return;
    }
    if (surfaceArrival_.prepared.has_value() &&
        (!surfaceArrival_.prepared->valid ||
         preparedSurfaceLandingCurrent(state_, catalog_, *surfaceArrival_.prepared))) {
        return;
    }

    const int destinationIndex = destinationIndexForId(catalog_, destination.id);
    const int completedLandings = destinationIndex >= 0 &&
            destinationIndex < static_cast<int>(state_.meta.destinationLandings.size())
        ? state_.meta.destinationLandings[static_cast<std::size_t>(destinationIndex)]
        : 0;
    SurfaceLandingBuildRequest request;
    request.destinationId = destination.id;
    request.landingOrdinal = completedLandings + 1;
    request.siteSeed = surfaceSiteSeed(state_.seed, destination.id, request.landingOrdinal);
    PreparedSurfaceLanding prepared = prepareSurfaceLanding(state_, catalog_, request);
    if (!prepared.valid) {
        services_.host.log(
            PlatformLogLevel::Error,
            "Surface landing preparation failed: " + prepared.error);
    }
    surfaceArrival_.prepared = std::move(prepared);
    surfaceArrival_.phase = SurfaceArrivalPhase::SurfaceReveal;
}

bool RocketGameApp::commitSurfaceTouchdown(
    const Destination& destination,
    bool hardTouchdown)
{
    // A failed background preparation gets one final attempt at contact;
    // never spin rebuilding it every frame or strand the Take Off command.
    if (surfaceArrival_.prepared.has_value() && !surfaceArrival_.prepared->valid) {
        surfaceArrival_.prepared.reset();
    }
    prepareSurfaceArrivalIfNeeded(destination);
    if (!surfaceArrival_.prepared.has_value() || !surfaceArrival_.prepared->valid) {
        state_.statusLine = "SURFACE SITE UNAVAILABLE - TAKE OFF REMAINS AVAILABLE";
        surfaceArrival_.phase = SurfaceArrivalPhase::AwaitingCommand;
        surfaceArrival_.elapsed = 0.0;
        surfaceArrival_.landingCommitted = false;
        panelDirty_ = true;
        return false;
    }

    const GameState stateBefore = state_;
    const PreparedLaunch flightModel = currentFlightModel();
    LaunchOutcome outcome = resolveLaunch(
        flightModel,
        catalog_,
        state_,
        destination.targetMultiplier,
        RecoveryMethod::TransferArrival,
        rng_,
        {true,
            LaunchFailureCause::None,
            session_.flight.minimumSafetyMargin,
            session_.flight.hullDamageTaken,
            session_.flight.fuelSurveyReturnTiming});
    outcome.peakWarning = std::max(outcome.peakWarning, session_.peakWarning);
    outcome.transferFuelRemaining = std::max(0.0, session_.flight.fuelRemaining);
    outcome.transferFuelCapacity = std::max(0.0, session_.flight.fuelCapacity);
    outcome.telemetry = chartTelemetryForOutcome(flightModel, session_.flight, false);
    outcome.impact=session_.flight.impact;
    outcome.shipDamage=physicalFlightCampaignDamage(session_.flight,flightModel.existingShipDamage);
    if (!outcome.telemetry.empty()) {
        outcome.telemetry.back() = launchTelemetryAt(flightModel, session_.flight);
    }
    applyLaunchOutcome(state_, catalog_, outcome);
    if (!preparedSurfaceLandingCurrent(state_, catalog_, *surfaceArrival_.prepared)) {
        const SurfaceLandingBuildRequest request = surfaceArrival_.prepared->request;
        surfaceArrival_.prepared = prepareSurfaceLanding(state_, catalog_, request);
    }
    if (!commitPreparedSurfaceLanding(
            state_,
            std::move(*surfaceArrival_.prepared),
            outcome.transferFuelRemaining)) {
        state_ = stateBefore;
        surfaceArrival_.prepared.reset();
        surfaceArrival_.phase = SurfaceArrivalPhase::AwaitingCommand;
        surfaceArrival_.landingCommitted = false;
        state_.statusLine = "SURFACE SITE UNAVAILABLE - LANDING NOT COMMITTED";
        panelDirty_ = true;
        return false;
    }

    if (!positionSurfaceLandingTeam(state_.run.mining,
            session_.flight.landing.touchdownGridX,session_.flight.landing.touchdownGridY)) {
        state_=stateBefore;
        surfaceArrival_.reset();
        surfaceArrival_.phase=SurfaceArrivalPhase::AwaitingCommand;
        state_.statusLine="SURFACE CLEARANCE UNAVAILABLE - TAKE OFF REMAINS AVAILABLE";
        panelDirty_=true;
        return false;
    }

    surfaceArrival_.prepared.reset();
    surfaceArrival_.phase = SurfaceArrivalPhase::Touchdown;
    surfaceArrival_.elapsed = 0.0;
    surfaceArrival_.deployQueued = false;
    surfaceArrival_.landingCommitted = true;
    surfaceArrival_.rigImpactFeedbackPlayed = false;
    surfaceArrival_.surfaceReadyFeedbackPlayed = false;
    surfaceArrival_.audioCueMask = 0;
    surfaceArrival_.droneAudioCueCount = 0;
    session_.currentMultiplier = outcome.ejectMultiplier;
    session_.peakWarning = 0.0;
    releaseRealtimeInputs(true);
    session_.controls.actions.cutEnginesActive = false;
    state_.statusLine = hardTouchdown
        ? "HARD LANDING - HULL DAMAGED"
        : "TOUCHDOWN";
    queueAudioCue(hardTouchdown ? GameAudioCue::HardTouchdown : GameAudioCue::SafeTouchdown);
    if (!validateProgressionStateOrRestore(stateBefore, "surface touchdown")) {
        surfaceArrival_.reset();
        return false;
    }
    panelDirty_ = true;
    realtimeHudDirty_ = true;
    return true;
}

void RocketGameApp::deploySurfaceTeam()
{
    if (surfaceArrival_.phase == SurfaceArrivalPhase::Touchdown) {
        surfaceArrival_.deployQueued = true;
        state_.statusLine = "DEPLOYMENT QUEUED";
        panelDirty_ = true;
        return;
    }
    if (surfaceArrival_.phase != SurfaceArrivalPhase::AwaitingCommand ||
        !surfaceArrival_.landingCommitted) {
        return;
    }
    beginSurfaceDeploymentSequence();
}

void RocketGameApp::beginSurfaceDeploymentSequence()
{
    surfaceArrival_.phase = SurfaceArrivalPhase::Deploying;
    surfaceArrival_.elapsed = 0.0;
    surfaceArrival_.rigImpactFeedbackPlayed = false;
    surfaceArrival_.surfaceReadyFeedbackPlayed = false;
    surfaceArrival_.audioCueMask = 0;
    surfaceArrival_.droneAudioCueCount = 0;
    surfaceBaySequence_ = {SurfaceBaySequenceKind::Deploy, false, 0.0};
    state_.statusLine = "DEPLOYING - RIG / DRONES";
    queueAudioCue(GameAudioCue::BayOpen);
    releaseRealtimeInputs(true);
    panelDirty_ = true;
}

void RocketGameApp::departSurfaceUndeployed()
{
    if (surfaceArrival_.phase != SurfaceArrivalPhase::AwaitingCommand) {
        return;
    }
    surfaceArrival_.phase = SurfaceArrivalPhase::UndeployedTakeoff;
    surfaceArrival_.elapsed = 0.0;
    surfaceBaySequence_ = {SurfaceBaySequenceKind::ShipOnlyDepart, false, 0.0};
    state_.statusLine = "TAKING OFF";
    queueAudioCue(GameAudioCue::TakeoffIgnition);
    releaseRealtimeInputs(true);
    panelDirty_ = true;
}

void RocketGameApp::completeSurfaceDeployment()
{
    if (!surfaceArrival_.landingCommitted || !state_.run.mining.active) {
        state_.statusLine = "SURFACE TEAM COULD NOT DEPLOY";
        surfaceArrival_.phase = SurfaceArrivalPhase::AwaitingCommand;
        surfaceArrival_.elapsed = 0.0;
        panelDirty_ = true;
        return;
    }
    state_.screen = Screen::Mining;
    state_.statusLine = "SURFACE TEAM READY";
    session_.flightArmed = false;
    clearFlightControls();
    surfaceBaySequence_.reset();
    surfaceArrival_.reset();
    save();
    panelDirty_ = true;
    realtimeHudDirty_ = true;
}

void RocketGameApp::completeUndeployedTakeoff()
{
    state_.run.mining = {};
    state_.run.planetaryExpedition = {};
    session_.flightArmed = false;
    clearFlightControls();
    surfaceBaySequence_.reset();
    surfaceArrival_.reset();
    finishArrivalVisit("SURFACE DEPARTURE COMPLETE");
    save();
    panelDirty_ = true;
}

void RocketGameApp::advanceSurfaceArrival(double deltaSeconds)
{
    const double step = std::clamp(
        deltaSeconds,
        0.0,
        tuning::launch::maxFrameStepSeconds);
    switch (surfaceArrival_.phase) {
    case SurfaceArrivalPhase::Touchdown:
        surfaceArrival_.elapsed = std::min(
            kTouchdownCelebrationSeconds,
            surfaceArrival_.elapsed + step);
        if (surfaceArrival_.elapsed >= kTouchdownCelebrationSeconds) {
            if (surfaceArrival_.deployQueued && surfaceArrival_.landingCommitted) {
                beginSurfaceDeploymentSequence();
            } else {
                surfaceArrival_.phase = SurfaceArrivalPhase::AwaitingCommand;
                surfaceArrival_.elapsed = 0.0;
                state_.statusLine = surfaceArrival_.landingCommitted
                    ? "LANDED"
                    : "LANDED - SURFACE SITE UNAVAILABLE";
            }
            panelDirty_ = true;
        }
        realtimeHudDirty_ = true;
        break;
    case SurfaceArrivalPhase::Deploying: {
        surfaceBaySequence_.elapsed = std::min(
            kSurfaceDeploymentSeconds,
            surfaceBaySequence_.elapsed + step);
        surfaceArrival_.elapsed = surfaceBaySequence_.elapsed;
        if ((surfaceArrival_.audioCueMask & kArrivalAudioRigEjection) == 0U &&
            surfaceArrival_.elapsed >= 0.25) {
            surfaceArrival_.audioCueMask |= kArrivalAudioRigEjection;
            queueAudioCue(GameAudioCue::RigEjection);
        }
        if ((surfaceArrival_.audioCueMask & kArrivalAudioArrestingBurst) == 0U &&
            surfaceArrival_.elapsed >= 0.85) {
            surfaceArrival_.audioCueMask |= kArrivalAudioArrestingBurst;
            queueAudioCue(GameAudioCue::ArrestingBurst);
        }
        const std::size_t droneCount = state_.run.mining.miniDrones.size();
        while (surfaceArrival_.droneAudioCueCount < static_cast<int>(droneCount) &&
            surfaceArrival_.elapsed >= surface_bay_timing::droneLaunchSeconds(
                static_cast<std::size_t>(surfaceArrival_.droneAudioCueCount), droneCount)) {
            const int index = surfaceArrival_.droneAudioCueCount++;
            queueAudioCue(GameAudioCue::DroneLaunch, 0.94 + 0.035 * static_cast<double>(index % 4));
        }
        if (!surfaceArrival_.rigImpactFeedbackPlayed && surfaceArrival_.elapsed >= 1.20) {
            surfaceArrival_.rigImpactFeedbackPlayed = true;
            queueControllerHapticCue(ControllerHapticCue::MiningHardContact);
            surfaceArrival_.audioCueMask |= kArrivalAudioRigImpact;
            queueAudioCue(GameAudioCue::RigImpact);
        }
        if ((surfaceArrival_.audioCueMask & kArrivalAudioBayClose) == 0U &&
            surfaceArrival_.elapsed >= 1.75) {
            surfaceArrival_.audioCueMask |= kArrivalAudioBayClose;
            queueAudioCue(GameAudioCue::BayClose);
        }
        if (!surfaceArrival_.surfaceReadyFeedbackPlayed && surfaceArrival_.elapsed >= 2.72) {
            surfaceArrival_.surfaceReadyFeedbackPlayed = true;
            queueControllerHapticCue(ControllerHapticCue::Arrival);
            surfaceArrival_.audioCueMask |= kArrivalAudioSurfaceReady;
            queueAudioCue(GameAudioCue::SurfaceReady);
        }
        if (surfaceArrival_.elapsed >= kSurfaceDeploymentSeconds) {
            completeSurfaceDeployment();
        } else {
            realtimeHudDirty_ = true;
        }
        break;
    }
    case SurfaceArrivalPhase::UndeployedTakeoff:
        surfaceBaySequence_.elapsed = std::min(
            kSurfaceUndeployedTakeoffSeconds,
            surfaceBaySequence_.elapsed + step);
        surfaceArrival_.elapsed = surfaceBaySequence_.elapsed;
        if (surfaceArrival_.elapsed >= kSurfaceUndeployedTakeoffSeconds) {
            completeUndeployedTakeoff();
        } else {
            realtimeHudDirty_ = true;
        }
        break;
    case SurfaceArrivalPhase::None:
    case SurfaceArrivalPhase::SurfaceReveal:
    case SurfaceArrivalPhase::AwaitingCommand:
    case SurfaceArrivalPhase::Complete:
        break;
    }
}

void RocketGameApp::finishArrivalVisit(std::string statusLine)
{
    state_.run.approach = {};
    if (!openRefitIfAvailable()) {
        state_.screen = navigationAvailable(state_) ? Screen::Navigation : Screen::Hangar;
    }
    state_.statusLine = std::move(statusLine);
}

bool RocketGameApp::openRefitIfAvailable(bool regenerateOffers)
{
    if (!state_.run.refitEntitled) {
        return false;
    }

    if (regenerateOffers || refitWindowPresentation(state_, catalog_).offers.empty()) {
        if (regenerateOffers) {
            beginRefitVisit(state_);
        }
        generateModuleOffers(state_, catalog_, rng_);
    }
    if (refitWindowPresentation(state_, catalog_).offers.empty()) {
        state_.run.refitEntitled = false;
        state_.run.offerCrewUpgradeIds = {};
        return false;
    }

    selectedRefitOfferIndex_ = 0;
    state_.screen = Screen::Upgrade;
    return true;
}

void RocketGameApp::beginLaunchSession(PreparedLaunch preparedLaunch)
{
    surfaceArrival_.reset();
    surfaceBaySequence_.reset();
    session_.preparedLaunch = preparedLaunch;
    const Destination* destination = catalog_.findDestination(
        preparedLaunch.routeProfileDestinationId.empty()
            ? preparedLaunch.config.destinationId
            : preparedLaunch.routeProfileDestinationId);
    session_.flight = beginLaunchFlight(
        preparedLaunch,
        destination == nullptr ? currentDestination(state_, catalog_) : *destination);
    session_.flight.active = false;
    session_.flightArmed = false;
    session_.launchQueued = false;
    session_.preflightElapsed = miningDroneTransferEnabled(state_)
        ? 0.0
        : tuning::session::preflightBoardingSeconds;
    session_.elapsed = 0.0;
    session_.autosaveElapsed = 0.0;
    session_.currentMultiplier = 1.0;
    session_.peakWarning = 0.0;
    session_.steerInput = 0.0;
    session_.throttleInput = 0.0;
    session_.asteroidImpactFeedbackSeconds = 0.0;
    session_.destruction = {};
    clearFlightControls();
    clearResultView();
    session_.arrivalFanfare = {};
}

void RocketGameApp::consumeNextLaunchBoost()
{
    state_.run.nextLaunchFuelBoost = 0.0;
    state_.run.nextLaunchSpeedBoost = 0.0;
    state_.run.nextLaunchInstabilityPenalty = 0.0;
    if (!session_.preparedLaunch.transferAssistId.empty() &&
        state_.run.pendingTransferAssist.definitionId == session_.preparedLaunch.transferAssistId) {
        state_.run.pendingTransferAssist = {};
    }
}

double RocketGameApp::liveBurnMultiplier() const
{
    return session_.flight.currentMultiplier;
}

void RocketGameApp::loadSavedGameOrDefault(bool showTitleScreen)
{
    surfaceArrival_.reset();
    debugActOneCheckpoint_ = -1;
    state_ = createNewGame(catalog_, 0x524F434B45544ULL);
    rng_ = Random(state_.seed);
    session_.reset();
    surfaceBaySequence_.reset();
    miningEvaDeathPresentation_ = {};
    miningSceneHandoff_ = MiningSceneHandoff::None;
    miningSceneHandoffCommitted_ = false;
    levelUp_ = {};
    expeditionXpPulseSeconds_ = 0.0;
    expeditionXpObservationInitialized_ = false;
    keyboardRealtimeInput_ = {};
    controllerRealtimeInput_ = {};
    hasSavedGame_ = false;
    checkpointRecoveryAvailable_ = false;
    titleNotice_.clear();
    panelDirty_ = true;

    const std::string storedSave = services_.saves.load();
    if (const auto saveData = deserializeSaveData(storedSave)) {
        restoreSaveData(state_, catalog_, *saveData);
        const CampaignProgressionAuditResult audit = auditCampaignProgression(state_, catalog_);
        if (!audit.valid) {
            state_ = createNewGame(catalog_, 0x524F434B45544ULL);
            const std::string checkpoint = services_.saves.loadCheckpoint();
            if (const auto checkpointSave = deserializeSaveData(checkpoint)) {
                GameState checkpointState = createNewGame(catalog_, checkpointSave->seed);
                restoreSaveData(checkpointState, catalog_, *checkpointSave);
                checkpointRecoveryAvailable_ = auditCampaignProgression(checkpointState, catalog_).valid;
            }
            titleNotice_ = checkpointRecoveryAvailable_
                ? "ROUTE CONTROL // RECOVERY REQUIRED. Restore the last validated checkpoint or begin a new campaign."
                : "ROUTE CONTROL // RECOVERY REQUIRED. No valid checkpoint is available; begin a new campaign.";
        } else {
            rng_ = Random(saveData->seed + 0xA51CE5ULL + static_cast<std::uint64_t>(saveData->blueprintProgress));
            if (state_.screen == Screen::Flight) {
                session_.preparedLaunch = rocket::prepareLaunch(state_, catalog_, rng_);
                session_.flightArmed = state_.run.flight.active;
                session_.preflightElapsed = session_.flightArmed
                    ? tuning::session::preflightBoardingSeconds
                    : 0.0;
            }
            if (state_.screen == Screen::Upgrade && !openRefitIfAvailable(false)) {
                state_.screen = navigationAvailable(state_) ? Screen::Navigation : Screen::Hangar;
            }
            state_.statusLine = std::string(text::status::saveRestored);
            hasSavedGame_ = true;
        }
    } else if (!storedSave.empty()) {
        // v19 is an intentional fresh-campaign boundary. Preserve the old
        // payload byte-for-byte until the player explicitly starts a new
        // campaign; incompatible active and checkpoint saves are never
        // offered as recovery paths.
        checkpointRecoveryAvailable_ = false;
        titleNotice_ = "CAMPAIGN UPDATE \xC2\xB7 A new campaign is required.";
        hasSavedGame_ = false;
    }

    ensureDroneBayState(state_, catalog_);
    syncLaunchConfig(state_, catalog_);
    titleScreenActive_ = showTitleScreen;
}

void RocketGameApp::beginDebugSandbox(const std::string& statusLine)
{
    surfaceArrival_.reset();
    surfaceBaySequence_.reset();
    titleScreenActive_ = false;
    titleNotice_.clear();
    debugSessionActive_ = true;
    debugActOneCheckpoint_ = -1;
    state_ = createNewGame(catalog_, 0xD36B6D3BU);
    rng_ = Random(state_.seed ^ 0x51A7E5ULL);
    session_.reset();
    levelUp_ = {};
    expeditionXpPulseSeconds_ = 0.0;
    expeditionXpObservationInitialized_ = false;
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
    debugDroneLoadout_.droneRanks = state_.run.planetaryExpedition.runDroneRanks;
}

void RocketGameApp::applyDebugDroneLoadout()
{
    seedDebugDroneLoadout();
    if (!debugDroneLoadout_.configured) {
        return;
    }
    state_.meta.equippedDroneIds = debugDroneLoadout_.equippedDroneIds;
    state_.run.planetaryExpedition.runDroneRanks = debugDroneLoadout_.droneRanks;
    ensureDroneBayState(state_, catalog_);
}

bool RocketGameApp::initialize()
{
    catalog_ = createDefaultContent();
    loadSavedGameOrDefault(true);

    if (!services_.renderer.initialize()) {
        state_.statusLine = "Graphics renderer initialization failed.";
        services_.host.log(PlatformLogLevel::Error, state_.statusLine);
        return false;
    }
    if (!services_.ui.initialize([this](const std::string& action) {
        runUiAction(action);
    })) {
        state_.statusLine = "RmlUi initialization failed.";
        services_.host.log(PlatformLogLevel::Error, state_.statusLine);
        services_.renderer.shutdown();
        return false;
    }

    refreshPanel();
    return true;
}

void RocketGameApp::shutdown()
{
    releaseRealtimeInputs(true);
    services_.ui.shutdown();
    services_.renderer.shutdown();
}

int RocketGameApp::currentScreen() const
{
    return static_cast<int>(state_.screen);
}

std::uint64_t RocketGameApp::deterministicStateHash() const
{
    // Hash the same canonical payload used by the versioned save path. This
    // keeps benchmark determinism checks aligned with authoritative gameplay
    // state without adding renderer-only fields or changing the save format.
    const std::string payload = serializeSaveData(captureSaveData(state_));
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char byte : payload) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

void RocketGameApp::setControllerPreferences(const ControllerPreferences& preferences)
{
    ControllerPreferences normalized = preferences;
    normalized.stickDeadzone = std::clamp(
        normalized.stickDeadzone,
        controller_tuning::minimumStickDeadzone,
        controller_tuning::maximumStickDeadzone);
    if (controllerPreferences_.promptFamily == normalized.promptFamily
        && controllerPreferences_.stickDeadzone == normalized.stickDeadzone
        && controllerPreferences_.invertFlightY == normalized.invertFlightY
        && controllerPreferences_.swapConfirmCancel == normalized.swapConfirmCancel
        && controllerPreferences_.vibrationEnabled == normalized.vibrationEnabled) {
        return;
    }
    controllerPreferences_ = normalized;
}

const ControllerPreferences& RocketGameApp::controllerPreferences() const
{
    return controllerPreferences_;
}

void RocketGameApp::setMiningDrillMode(MiningDrillMode mode)
{
    if (miningDrillMode_ == mode) {
        return;
    }
    releaseRealtimeInputs(true);
    miningDrillMode_ = mode;
}

void RocketGameApp::setFirstTimeIntroductionsEnabled(bool enabled)
{
    if (firstTimeIntroductionsEnabled_ == enabled) {
        return;
    }
    firstTimeIntroductionsEnabled_ = enabled;
    if (!enabled
        && state_.screen == Screen::ArrivalOps
        && state_.run.approach.destinationId == content::destination::moon
        && !ui::briefings::acknowledged(state_.meta.acknowledgedActivityBriefingIds, ui::briefings::approach)) {
        ui::briefings::acknowledge(state_.meta.acknowledgedActivityBriefingIds, ui::briefings::approach);
        save();
    }
    panelDirty_ = true;
}

void RocketGameApp::setActiveInputSource(InputSource source)
{
    if (activeInputSource_ != source && activeInputSource_ != InputSource::None) {
        // A held action belongs to the source that began it. Releasing both
        // realtime lanes here prevents mouse/controller handoff from carrying
        // fire, drill, movement, or the EVA hold into the new source.
        releaseRealtimeInputs(true);
    }
    activeInputSource_ = source;
    if (source != InputSource::Controller) {
        controllerClaimedInput_ = false;
    }
}

InputContext RocketGameApp::inputContext() const
{
    return resolvedControllerInputContext(gameplayInputContext(), pauseReason_, services_.ui.modalOpen());
}

InputContext RocketGameApp::gameplayInputContext() const
{
    if (titleScreenActive_) {
        return InputContext::Ui;
    }
    switch (state_.screen) {
    case Screen::Flight:
        return surfaceArrival_.active()
            ? InputContext::SurfaceArrival
            : session_.destruction.active
            ? InputContext::Stamp
            : (session_.flightArmed ? InputContext::Launch : InputContext::Preflight);
    case Screen::Results:
    case Screen::ArrivalFanfare:
    case Screen::StoryBriefing:
        return InputContext::Stamp;
    case Screen::Flyby:
    case Screen::Orbit:
    case Screen::SurfaceScan:
    case Screen::SurfacePush:
        return InputContext::Ui;
    case Screen::Mining:
        if (state_.run.mining.failurePending) {
            return InputContext::MiningFailure;
        }
        return miningAtReturnZone(state_.run.mining)
            ? InputContext::MiningService
            : InputContext::MiningActive;
    case Screen::Hangar:
    case Screen::ArrivalOps:
    case Screen::Research:
    case Screen::SurfaceExpedition:
    case Screen::SurfaceUpgrade:
    case Screen::Upgrade:
    case Screen::Legacy:
    case Screen::DroneOps:
    case Screen::Navigation:
        return InputContext::Ui;
    }
    return InputContext::Ui;
}

namespace {

std::string_view controllerContextName(InputContext context)
{
    switch (context) {
    case InputContext::Ui: return "ui";
    case InputContext::Preflight: return "preflight";
    case InputContext::Launch: return "launch";
    case InputContext::SurfaceArrival: return "surface_arrival";
    case InputContext::MiningActive: return "mining_active";
    case InputContext::MiningService: return "mining_service";
    case InputContext::MiningFailure: return "mining_failure";
    case InputContext::Stamp: return "stamp";
    case InputContext::Paused: return "paused";
    }
    return "ui";
}

std::string_view controllerPauseName(PauseReason reason)
{
    switch (reason) {
    case PauseReason::None: return "none";
    case PauseReason::SystemMenu: return "system_menu";
    case PauseReason::BlockingModal: return "blocking_modal";
    case PauseReason::ControllerDisconnected: return "controller_disconnected";
    case PauseReason::PageHidden: return "page_hidden";
    case PauseReason::ControllerUiFocus: return "controller_ui_focus";
    }
    return "none";
}

std::string_view controllerSourceName(InputSource source)
{
    switch (source) {
    case InputSource::None: return "none";
    case InputSource::KeyboardPointer: return "keyboard_pointer";
    case InputSource::Controller: return "controller";
    }
    return "none";
}

std::string_view controllerActionName(GameInputAction action)
{
    switch (action) {
    case GameInputAction::ActivateFocused: return "activate_focused";
    case GameInputAction::CancelFocused: return "cancel_focused";
    case GameInputAction::OpenSystemMenu: return "open_system_menu";
    case GameInputAction::OpenMap: return "open_map";
    case GameInputAction::OpenInventory: return "open_inventory";
    case GameInputAction::StartOrContinue: return "start_or_continue";
    case GameInputAction::ReturnHome: return "return_home";
    case GameInputAction::ToggleEngines: return "toggle_engines";
    case GameInputAction::DeploySurfaceTeam: return "deploy_surface_team";
    case GameInputAction::DepartSurfaceUndeployed: return "depart_surface_undeployed";
    case GameInputAction::Abort: return "abort";
    case GameInputAction::MiningScan: return "mining_scan";
    case GameInputAction::MiningTether: return "mining_tether";
    case GameInputAction::MiningStow: return "mining_stow";
    case GameInputAction::MiningOperatorToggle: return "mining_operator_toggle";
    case GameInputAction::MiningRepairDrill: return "mining_repair_drill";
    case GameInputAction::MiningRepairRig: return "mining_repair_rig";
    case GameInputAction::MiningFailureAcknowledge: return "mining_failure_acknowledge";
    case GameInputAction::EnterUiFocus: return "enter_ui_focus";
    case GameInputAction::Count: break;
    }
    return "none";
}

std::string controllerJsonEscape(std::string_view value)
{
    std::string result;
    result.reserve(value.size());
    for (const char character : value) {
        if (character == '\\' || character == '"') {
            result.push_back('\\');
        }
        result.push_back(character);
    }
    return result;
}

} // namespace

std::string RocketGameApp::controllerDebugStatusJson() const
{
    std::ostringstream out;
    out << "{\"context\":\"" << controllerContextName(inputContext())
        << "\",\"focusId\":\"" << controllerJsonEscape(services_.ui.focusedId())
        << "\",\"lastAction\":\"" << controllerJsonEscape(lastControllerAction_)
        << "\",\"pauseReason\":\"" << controllerPauseName(pauseReason_)
        << "\",\"activeSource\":\"" << controllerSourceName(activeInputSource_)
        << "\",\"connected\":" << (controllerConnected_ ? "true" : "false")
        << ",\"resumeNeutralRequired\":" << (controllerResumeNeutralRequired_ ? "true" : "false")
        << ",\"lastInputSeconds\":" << lastControllerInputSeconds_
        << '}';
    return out.str();
}

ControllerHapticCue RocketGameApp::consumePendingControllerHapticCue()
{
    const ControllerHapticCue cue = controllerPreferences_.vibrationEnabled
        ? pendingHapticCue_
        : ControllerHapticCue::None;
    pendingHapticCue_ = ControllerHapticCue::None;
    return cue;
}

std::vector<GameAudioEvent> RocketGameApp::consumePendingAudioEvents()
{
    std::vector<GameAudioEvent> events = std::move(pendingAudioEvents_);
    pendingAudioEvents_.clear();
    return events;
}

void RocketGameApp::queueAudioCue(GameAudioCue cue, double pitch)
{
    if (pendingAudioEvents_.size() >= 16U) {
        return;
    }
    pendingAudioEvents_.push_back({cue, std::clamp(pitch, 0.75, 1.25)});
}

void RocketGameApp::queueControllerHapticCue(ControllerHapticCue cue)
{
    if (!controllerPreferences_.vibrationEnabled
        || !controllerConnected_
        || activeInputSource_ != InputSource::Controller) {
        return;
    }
    if (static_cast<int>(cue) > static_cast<int>(pendingHapticCue_)) {
        pendingHapticCue_ = cue;
    }
}

void RocketGameApp::updateControllerHapticState()
{
    if (state_.screen != Screen::Mining) {
        lastMiningContactIntensity_ = 0.0;
        lastMiningDroneHealth_ = 1.0;
        lastMiningFailurePending_ = false;
        return;
    }

    const MiningRunState& mining = state_.run.mining;
    if (mining.failurePending && !lastMiningFailurePending_) {
        queueControllerHapticCue(ControllerHapticCue::Failure);
    } else if (mining.droneHealth + 0.0001 < lastMiningDroneHealth_) {
        queueControllerHapticCue(ControllerHapticCue::Damage);
    } else if (mining.drilling
        && mining.contactIntensity >= 0.55
        && lastMiningContactIntensity_ < 0.55) {
        queueControllerHapticCue(ControllerHapticCue::MiningHardContact);
    }
    lastMiningContactIntensity_ = mining.contactIntensity;
    lastMiningDroneHealth_ = mining.droneHealth;
    lastMiningFailurePending_ = mining.failurePending;
}

bool RocketGameApp::realtimeControllerContext(InputContext context) const
{
    return context == InputContext::Preflight
        || context == InputContext::Launch
        || context == InputContext::SurfaceArrival
        || context == InputContext::MiningActive
        || context == InputContext::MiningService
        || context == InputContext::MiningFailure;
}

void RocketGameApp::releaseRealtimeInputs(bool releaseKeyboard)
{
    controllerRealtimeInput_ = {};
    if (releaseKeyboard) {
        keyboardRealtimeInput_ = {};
        keyboardDrillPressed_ = false;
    }
    state_.run.approach.flyby.inputX = 0.0;
    state_.run.approach.flyby.inputY = 0.0;
    state_.run.approach.orbit.inputX = 0.0;
    state_.run.approach.orbit.inputY = 0.0;
    session_.steerInput = 0.0;
    session_.throttleInput = 0.0;
    state_.run.mining.moveX = 0.0;
    state_.run.mining.moveY = 0.0;
    state_.run.mining.drilling = false;
    setMiningFire(state_, false);
    setMiningOperatorToggleProgress(state_, 0.0);
    miningOperatorToggleConfirmationSeconds_ = 0.0;
}

void RocketGameApp::applyRealtimeInputs()
{
    const bool useController = activeInputSource_ == InputSource::Controller;
    const double moveX = useController ? controllerRealtimeInput_.moveX : keyboardRealtimeInput_.moveX;
    const double moveY = useController ? controllerRealtimeInput_.moveY : keyboardRealtimeInput_.moveY;

    switch (state_.screen) {
    case Screen::Flight:
        session_.steerInput = moveX;
        session_.throttleInput = moveY;
        break;
    case Screen::Flyby:
        setFlybyMove(state_, moveX, moveY);
        break;
    case Screen::Orbit:
        setOrbitMove(state_, moveX, moveY);
        break;
    case Screen::Mining:
        setMiningMove(state_, moveX, moveY);
        {
            const RealtimeInputState& miningInput = useController
                ? controllerRealtimeInput_
                : keyboardRealtimeInput_;
            const bool operatorActive =
                state_.run.mining.operatorMode == MiningOperatorMode::Jetpack;
            if (std::hypot(miningInput.aimX, miningInput.aimY) > 0.01) {
                setMiningAim(state_, miningInput.aimX, miningInput.aimY);
            }
            // The primary control remains the rig drill, but becomes suit
            // fire after EVA. The secondary control is the suit hand drill.
            setMiningFire(state_, operatorActive && miningInput.firing);
            setMiningDrilling(
                state_,
                operatorActive ? miningInput.drilling : (miningInput.firing || miningInput.drilling));
        }
        break;
    default:
        break;
    }
}

void RocketGameApp::openControllerSystemMenu(PauseReason reason)
{
    releaseRealtimeInputs(true);
    if (pauseReason_ != reason) {
        controllerResumeNeutralRequired_ = reason == PauseReason::ControllerDisconnected
            || (reason == PauseReason::PageHidden
                && controllerClaimedInput_
                && activeInputSource_ == InputSource::Controller);
    }
    pauseReason_ = reason;
    services_.ui.setControllerResumeBlocked(
        controllerResumeBlocked(reason, controllerConnected_, controllerResumeNeutralRequired_),
        controllerConnected_);
    services_.ui.openModal("system_menu");
}

void RocketGameApp::clearControllerPause()
{
    pauseReason_ = PauseReason::None;
    controllerResumeNeutralRequired_ = false;
    services_.ui.setControllerResumeBlocked(false, controllerConnected_);
    releaseRealtimeInputs(true);
}

void RocketGameApp::dispatchControllerAction(InputContext context, GameInputAction action)
{
    // Once a modal is visible, only its focused Accept/Cancel contract may
    // receive controller actions. Nested modal buttons are opened through
    // ActivateFocused, so blocking direct shortcuts here cannot make them
    // unreachable.
    if (services_.ui.modalOpen()
        && action != GameInputAction::ActivateFocused
        && action != GameInputAction::CancelFocused
        && action != GameInputAction::Count) {
        lastControllerAction_ = "modal_blocked_" + std::string(controllerActionName(action));
        return;
    }

    lastControllerAction_ = std::string(controllerActionName(action));
    if (action != GameInputAction::EnterUiFocus && action != GameInputAction::Count) {
        queueControllerHapticCue(ControllerHapticCue::Confirmation);
    }

    switch (action) {
    case GameInputAction::ActivateFocused:
        (void)services_.ui.activateFocused();
        break;
    case GameInputAction::CancelFocused:
        if (pauseReason_ == PauseReason::ControllerDisconnected || pauseReason_ == PauseReason::PageHidden) {
            // Safety pauses require the explicit Resume control; East cannot
            // dismiss their root menu and accidentally restart simulation.
            break;
        }
        if (pauseReason_ == PauseReason::ControllerUiFocus && !services_.ui.modalOpen()) {
            clearControllerPause();
        } else if (pauseReason_ == PauseReason::None && !services_.ui.modalOpen() && state_.screen == Screen::DroneOps) {
            backToSurfaceOps();
        } else {
            services_.ui.cancel();
        }
        break;
    case GameInputAction::OpenSystemMenu:
        if (titleScreenActive_) {
            services_.ui.openModal(std::string(ui::modals::settings));
            break;
        }
        if (!services_.ui.modalOpen() || pauseReason_ == PauseReason::ControllerUiFocus) {
            openControllerSystemMenu(PauseReason::SystemMenu);
        }
        break;
    case GameInputAction::OpenMap:
        if (!titleScreenActive_) {
            services_.ui.openModal(std::string(ui::modals::map));
        }
        break;
    case GameInputAction::OpenInventory:
        if (!titleScreenActive_) {
            services_.ui.openModal(std::string(ui::modals::inventory));
        }
        break;
    case GameInputAction::StartOrContinue:
        if (context == InputContext::Preflight) {
            startLaunch();
        } else if (state_.screen == Screen::Results) {
            next();
        } else if (state_.screen == Screen::StoryBriefing) {
            acknowledgeStoryBriefing();
        }
        break;
    case GameInputAction::ReturnHome:
        returnHome();
        break;
    case GameInputAction::ToggleEngines:
        cutEngines();
        break;
    case GameInputAction::DeploySurfaceTeam:
        deploySurfaceTeam();
        break;
    case GameInputAction::DepartSurfaceUndeployed:
        departSurfaceUndeployed();
        break;
    case GameInputAction::Abort:
        if (context == InputContext::MiningActive || context == InputContext::MiningService) {
            miningAbort();
        }
        break;
    case GameInputAction::MiningScan:
        miningScanner();
        break;
    case GameInputAction::MiningTether:
        miningTether();
        break;
    case GameInputAction::MiningStow:
        miningStow();
        break;
    case GameInputAction::MiningOperatorToggle:
        miningOperatorToggle();
        break;
    case GameInputAction::MiningRepairDrill:
        miningRepairDrill();
        break;
    case GameInputAction::MiningRepairRig:
        miningRepairDrone();
        break;
    case GameInputAction::MiningFailureAcknowledge:
        miningFailureAck();
        break;
    case GameInputAction::EnterUiFocus:
        releaseRealtimeInputs(true);
        pauseReason_ = PauseReason::ControllerUiFocus;
        break;
    case GameInputAction::Count:
        break;
    }
}

void RocketGameApp::dispatchControllerInput(InputContext context, const RoutedGameInput& input)
{
    if (miningSceneHandoff_ != MiningSceneHandoff::None) {
        releaseRealtimeInputs(true);
        return;
    }

    if (context == InputContext::Launch) {
        // Launch steering is lateral: negative moves toward the left side of
        // the rendered corridor, matching the raw left-stick X convention.
        controllerRealtimeInput_.moveX = input.moveX;
        controllerRealtimeInput_.moveY = input.moveY;
    } else if (context == InputContext::MiningActive
        || context == InputContext::MiningService) {
        controllerRealtimeInput_.moveX = input.moveX;
        controllerRealtimeInput_.moveY = input.moveY;
    } else {
        controllerRealtimeInput_.moveX = 0.0;
        controllerRealtimeInput_.moveY = 0.0;
    }
    if (context == InputContext::MiningActive || context == InputContext::MiningService) {
        controllerRealtimeInput_.aimX = input.aimX;
        controllerRealtimeInput_.aimY = input.aimY;
        controllerRealtimeInput_.firing = input.firing;
        setMiningOperatorToggleProgress(state_, input.operatorToggleProgress);
    } else {
        controllerRealtimeInput_.aimX = 0.0;
        controllerRealtimeInput_.aimY = 0.0;
        controllerRealtimeInput_.firing = false;
        setMiningOperatorToggleProgress(state_, 0.0);
    }
    controllerRealtimeInput_.drilling = (context == InputContext::MiningActive || context == InputContext::MiningService)
        && input.drilling;

    // Pause transitions outrank every gameplay action. This prevents a Menu
    // press on the same frame as a completed eject/abort hold from performing
    // the dangerous action behind the newly opened overlay.
    if (input.has(GameInputAction::OpenSystemMenu)) {
        dispatchControllerAction(context, GameInputAction::OpenSystemMenu);
        return;
    }
    if (input.has(GameInputAction::EnterUiFocus)) {
        dispatchControllerAction(context, GameInputAction::EnterUiFocus);
        if (input.navigation) {
            uiNavigate(*input.navigation);
        }
        return;
    }
    if (input.navigation) {
        uiNavigate(*input.navigation);
    }
    if (std::abs(input.scroll) > 0.10) {
        services_.ui.scroll(static_cast<float>(input.scroll * 48.0));
    }

    for (std::size_t index = 0; index < gameInputActionCount; ++index) {
        const GameInputAction action = static_cast<GameInputAction>(index);
        if (action != GameInputAction::EnterUiFocus
            && action != GameInputAction::OpenSystemMenu
            && input.has(action)) {
            dispatchControllerAction(context, action);
        }
    }
    if (pauseReason_ == PauseReason::None) {
        applyRealtimeInputs();
    }
}

void RocketGameApp::previewSyntheticControllerInput(const ControllerFrame& frame, double realTimeSeconds)
{
    // Controller Lab input is intentionally preview-only. It may move focus
    // and exercise hold/repeat routing, but it never dispatches a game action,
    // changes realtime controls, pauses play, or reaches save-producing code.
    services_.ui.setControllerFocusVisible(frame.connected);
    services_.ui.setControllerPresentation(frame.connected, frame.family);
    if (!frame.connected) {
        syntheticInputRouter_.reset();
        lastControllerAction_ = "synthetic_disconnect";
        return;
    }

    if (frame.meaningfulInput) {
        lastControllerInputSeconds_ = realTimeSeconds;
    }
    const InputContext context = inputContext();
    const double focusedActivationHoldSeconds = services_.ui.focusedId() == "action:reset_save" ? 0.75 : 0.0;
    const RoutedGameInput routed = syntheticInputRouter_.route(
        context,
        frame,
        controllerPreferences_,
        focusedActivationHoldSeconds);

    if (routed.navigation) {
        services_.ui.navigate(*routed.navigation);
    }
    if (std::abs(routed.scroll) > 0.10) {
        services_.ui.scroll(static_cast<float>(routed.scroll * 48.0));
    }

    bool actionFound = false;
    for (std::size_t index = 0; index < gameInputActionCount; ++index) {
        const GameInputAction action = static_cast<GameInputAction>(index);
        if (!routed.has(action)) {
            continue;
        }
        lastControllerAction_ = "synthetic_" + std::string(controllerActionName(action));
        actionFound = true;
    }
    if (!actionFound && frame.meaningfulInput) {
        lastControllerAction_ = "synthetic_input";
    }
}

void RocketGameApp::inputFrame(const ControllerFrame& frame, double realTimeSeconds)
{
    if (frame.synthetic) {
        if (!frame.pageVisible || !frame.browserFocused) {
            const InputContext gameplayContext = gameplayInputContext();
            if (realtimeControllerContext(gameplayContext)) {
                openControllerSystemMenu(PauseReason::PageHidden);
            } else {
                releaseRealtimeInputs(true);
            }
            syntheticInputRouter_.reset();
            return;
        }
        previewSyntheticControllerInput(frame, realTimeSeconds);
        return;
    }

    controllerConnected_ = frame.connected;
    const InputContext presentationContext = gameplayInputContext();
    const bool directGameplayControls = presentationContext == InputContext::Preflight
        || presentationContext == InputContext::Launch
        || presentationContext == InputContext::SurfaceArrival
        || presentationContext == InputContext::MiningActive
        || presentationContext == InputContext::MiningService;
    const bool openingScreen = titleScreenActive_
        || (state_.screen == Screen::StoryBriefing
            && state_.storyBriefing.pending == StoryBriefingId::CampaignIntroduction);
    const bool controllerPresentation = frame.connected
        && (activeInputSource_ == InputSource::Controller || openingScreen);
    const bool controllerFocusVisible = controllerPresentation
        && (!directGameplayControls || pauseReason_ != PauseReason::None || services_.ui.modalOpen());
    services_.ui.setControllerFocusVisible(controllerFocusVisible);
    services_.ui.setControllerPresentation(controllerPresentation, frame.family);

    if (state_.screen != lastInputScreen_) {
        releaseRealtimeInputs(true);
        if (pauseReason_ == PauseReason::ControllerUiFocus) {
            pauseReason_ = PauseReason::None;
        }
        lastInputScreen_ = state_.screen;
    }

    const InputContext gameplayContext = gameplayInputContext();
    const bool realtime = realtimeControllerContext(gameplayContext);

    if (!frame.pageVisible || !frame.browserFocused) {
        if (realtime) {
            openControllerSystemMenu(PauseReason::PageHidden);
        } else {
            releaseRealtimeInputs(true);
        }
        controllerWasConnected_ = frame.connected;
        return;
    }

    updateControllerHapticState();

    const bool controllerLost = frame.justDisconnected || (controllerWasConnected_ && !frame.connected);
    if (controllerLost) {
        const bool activeControllerLost = controllerClaimedInput_ && activeInputSource_ == InputSource::Controller;
        releaseRealtimeInputs(activeControllerLost);
        if (!activeControllerLost && pauseReason_ == PauseReason::None) {
            applyRealtimeInputs();
        }
        if (realtime && activeControllerLost) {
            openControllerSystemMenu(PauseReason::ControllerDisconnected);
        }
        controllerWasConnected_ = false;
        controllerClaimedInput_ = false;
        return;
    }

    controllerWasConnected_ = frame.connected;
    if (frame.meaningfulInput) {
        controllerClaimedInput_ = true;
        lastControllerInputSeconds_ = realTimeSeconds;
    }

    const bool safetyResumePause = pauseReason_ == PauseReason::ControllerDisconnected
        || pauseReason_ == PauseReason::PageHidden;
    services_.ui.setControllerResumeBlocked(
        controllerResumeBlocked(pauseReason_, frame.connected, controllerResumeNeutralRequired_),
        frame.connected);
    if (safetyResumePause && controllerResumeNeutralRequired_) {
        if (!services_.ui.modalOpen()) {
            services_.ui.openModal("system_menu");
        }
        const bool neutralController = frame.connected
            && frame.down.none()
            && std::abs(frame.leftX) <= 0.01
            && std::abs(frame.leftY) <= 0.01
            && std::abs(frame.rightX) <= 0.01
            && std::abs(frame.rightY) <= 0.01
            && !frame.navigation;
        if (!neutralController) {
            controllerRealtimeInput_ = {};
            return;
        }
        // Route this neutral frame so the router observes every button as
        // released. Resume then requires a fresh confirm edge on a later frame.
        controllerResumeNeutralRequired_ = false;
        services_.ui.setControllerResumeBlocked(false, frame.connected);
    }

    if (pauseReason_ != PauseReason::None && pauseReason_ != PauseReason::ControllerUiFocus && !services_.ui.modalOpen()) {
        if (controllerResumeBlocked(pauseReason_, frame.connected, controllerResumeNeutralRequired_)) {
            services_.ui.openModal("system_menu");
            controllerRealtimeInput_ = {};
            return;
        }
        clearControllerPause();
    }

    const bool modalOpen = services_.ui.modalOpen();
    if (modalOpen) {
        // Modal opening is also a realtime-input fence. Clear both input
        // sources before routing this frame so movement, steering, or drilling
        // cannot continue beneath an overlay.
        releaseRealtimeInputs(true);
    }
    if (pauseReason_ == PauseReason::None && modalOpen && realtimeControllerContext(gameplayContext)) {
        pauseReason_ = PauseReason::BlockingModal;
    }

    const InputContext context = inputContext();
    const double focusedActivationHoldSeconds = services_.ui.focusedId() == "action:reset_save" ? 0.75 : 0.0;
    const RoutedGameInput routed = inputRouter_.route(
        context,
        frame,
        controllerPreferences_,
        focusedActivationHoldSeconds);
    const Screen screenBeforeDispatch = state_.screen;
    dispatchControllerInput(context, routed);
    if (state_.screen != screenBeforeDispatch) {
        releaseRealtimeInputs(true);
        if (pauseReason_ == PauseReason::ControllerUiFocus) {
            clearControllerPause();
        }
        lastInputScreen_ = state_.screen;
    }
}

void RocketGameApp::tick(double deltaSeconds)
{
    if (titleScreenActive_) {
        const double transitionDeltaSeconds = std::clamp(deltaSeconds, 0.0, 0.25);
        if (titleLaunchActive_) {
            titleLaunchElapsedSeconds_ = std::min(
                titleLaunchElapsedSeconds_ + transitionDeltaSeconds,
                kTitleLaunchSequenceSeconds);
            if (titleLaunchElapsedSeconds_ >= kTitleLaunchSequenceSeconds) {
                completeTitleLaunch();
            }
        } else {
            if (sceneTransition_.advance(transitionDeltaSeconds)) {
                finishTitleLaunch();
            } else if (sceneTransition_.active()) {
                // The UI is an independent layer above the scene renderer.
                // Refresh its lightweight title document while fading so both
                // layers share the same blackout envelope.
                refreshPanel();
            }
        }
        return;
    }
    const bool evaDeathTransitionOwned =
        advanceMiningEvaDeathPresentation(deltaSeconds);
    if (advanceMiningSceneHandoff(deltaSeconds)) {
        return;
    }
    if (sceneTransition_.active() && !evaDeathTransitionOwned) {
        sceneTransition_.advance(std::clamp(deltaSeconds, 0.0, 0.25));
        // The fade is a shared scene/UI overlay, so remove it from both
        // renderers on the frame it completes.
        refreshPanel();
    }
    visualTimeSeconds_ += std::clamp(deltaSeconds, 0.0, 0.25);
    if (expeditionXpPulseSeconds_ > 0.0) {
        expeditionXpPulseSeconds_ = std::max(
            0.0,
            expeditionXpPulseSeconds_ - std::clamp(deltaSeconds, 0.0, 0.25));
        realtimeHudDirty_ = true;
    }
    if (miningOperatorToggleConfirmationSeconds_ > 0.0) {
        miningOperatorToggleConfirmationSeconds_ = std::max(
            0.0,
            miningOperatorToggleConfirmationSeconds_ -
                std::clamp(deltaSeconds, 0.0, 0.25));
        if (miningOperatorToggleConfirmationSeconds_ <= 0.0) {
            setMiningOperatorToggleProgress(state_, 0.0);
            realtimeHudDirty_ = true;
        }
    }

    if (controllerPauseStopsSimulation(pauseReason_, gameplayInputContext(), services_.ui.modalOpen())) {
        return;
    }

    if (state_.screen == Screen::SurfaceUpgrade) {
        const double clampedDelta = std::clamp(deltaSeconds, 0.0, 0.25);
        const bool activationWasLocked = levelUp_.activationFenceSeconds > 0.0;
        levelUp_.activationFenceSeconds = std::max(
            0.0,
            levelUp_.activationFenceSeconds - clampedDelta);
        if (activationWasLocked) {
            realtimeHudDirty_ = true;
        }
        if (levelUp_.fanfareActive) {
            levelUp_.elapsed = std::min(
                levelUp_.elapsed + clampedDelta,
                kLevelUpFanfareSeconds);
            if (levelUp_.elapsed >= kLevelUpFanfareSeconds) {
                levelUp_.fanfareActive = false;
            }
            realtimeHudDirty_ = true;
        }
        if (levelUp_.resolving) {
            levelUp_.resolveElapsed += clampedDelta;
            if (levelUp_.resolveElapsed >= kLevelUpSelectionResolveSeconds) {
                finishLevelUpSelection();
            }
        }
        return;
    }

    if (state_.screen == Screen::Flight) {
        if (session_.destruction.active) {
            const double clampedDelta = std::clamp(
                deltaSeconds,
                0.0,
                tuning::launch::maxFrameStepSeconds);
            session_.destruction.elapsed = std::min(
                session_.destruction.elapsed + clampedDelta,
                tuning::session::flightDestructionSequenceSeconds);
            if (session_.destruction.elapsed >= tuning::session::flightDestructionSequenceSeconds) {
                const double burnMultiplier = session_.destruction.burnMultiplier;
                const LaunchFailureCause failureCause = session_.destruction.failureCause;
                session_.destruction.active = false;
                completeLaunch(
                    burnMultiplier,
                    RecoveryMethod::None,
                    failureCause);
            } else {
                realtimeHudDirty_ = true;
            }
            return;
        }
        if (surfaceArrival_.active()) {
            advanceSurfaceArrival(deltaSeconds);
            return;
        }
        if (!session_.flightArmed) {
            const bool wasReady = session_.preflightElapsed >= tuning::session::preflightBoardingSeconds;
            session_.preflightElapsed = std::min(
                session_.preflightElapsed + std::clamp(deltaSeconds, 0.0, tuning::launch::maxFrameStepSeconds),
                tuning::session::preflightBoardingSeconds);
            if (!wasReady && session_.preflightElapsed >= tuning::session::preflightBoardingSeconds) {
                if (session_.launchQueued) {
                    startLaunch();
                } else {
                    state_.statusLine = std::string(text::status::preflightReady);
                    // Reassert the scene control when boarding completes so a
                    // pointer or header-navigation detour cannot leave confirm
                    // on Menu at the moment Launch becomes immediate.
                    services_.ui.requestFocus("action:start_launch");
                    refreshPanel();
                }
            }
            return;
        }
        const PreparedLaunch flightModel = currentFlightModel();
        const Destination* activeDestination = catalog_.findDestination(
            session_.preparedLaunch.routeProfileDestinationId.empty()
                ? session_.preparedLaunch.config.destinationId
                : session_.preparedLaunch.routeProfileDestinationId);
        const Destination& destination = activeDestination == nullptr
            ? currentDestination(state_, catalog_)
            : *activeDestination;
        const double clampedDelta = std::clamp(deltaSeconds, 0.0, tuning::launch::maxFrameStepSeconds);
        // A resumed local descent needs its exact terrain before the first
        // movement step, not one frame after contact could already occur.
        if (session_.flight.mode == FlightMode::Landing) {
            prepareSurfaceArrivalIfNeeded(destination);
        }
        session_.asteroidImpactFeedbackSeconds = std::max(
            0.0,
            session_.asteroidImpactFeedbackSeconds - clampedDelta);
        const LaunchFlightStep step = updateLaunchFlight(
            session_.flight,
            session_.preparedLaunch,
            destination,
            {
                session_.steerInput,
                session_.throttleInput,
                session_.controls.actions.cutEnginesActive,
                activeInputSource_ == InputSource::Controller,
            },
            clampedDelta,
            surfaceArrival_.prepared.has_value() && surfaceArrival_.prepared->valid
                ? &surfaceArrival_.prepared->miningTemplate : nullptr);
        session_.elapsed += clampedDelta;
        session_.autosaveElapsed += clampedDelta;
        session_.currentMultiplier = session_.flight.currentMultiplier;
        const TelemetryEvent event = launchTelemetryAt(flightModel, session_.flight);
        recordTelemetryPeak(event);
        if (step.asteroidHit || (step.surfaceImpact && session_.flight.impact.damage>0.0)) {
            session_.asteroidImpactFeedbackSeconds = asteroidImpactFeedbackDuration;
            if (step.surfaceImpact && !step.failed) queueControllerHapticCue(ControllerHapticCue::Damage);
        }
        if (session_.flight.active && !step.reachedDestination && session_.autosaveElapsed >= 2.0) {
            session_.autosaveElapsed = 0.0;
            save();
        }

        const bool surfacePreparationReady = session_.preparedLaunch.orbitRequired
            ? session_.flight.orbit.captured
            : (session_.flight.travelProgress >= 0.50 ||
               session_.flight.mode == FlightMode::Landing);
        if (session_.preparedLaunch.config.frontierTransfer && surfacePreparationReady) {
            prepareSurfaceArrivalIfNeeded(destination);
        }

        if (step.orbitCaptured) {
            if (!session_.flight.orbit.rewardAwarded) {
                awardUnifiedOrbit(
                    state_,
                    catalog_,
                    destination,
                    session_.flight.orbit.grade);
                session_.flight.orbit.rewardAwarded = true;
            }
            state_.statusLine = "ORBIT CAPTURED — brake against your path to deorbit.";
            queueControllerHapticCue(ControllerHapticCue::Arrival);
        }
        if (step.failed &&
            (step.failureCause == LaunchFailureCause::LunarImpact ||
             step.failureCause == LaunchFailureCause::ThermalRunaway)) {
            beginFlightDestructionCinematic(step.failureCause);
        } else if (step.failed) {
            completeLaunch(session_.flight.peakMultiplier, RecoveryMethod::None, step.failureCause);
        } else if (step.reachedHome) {
            completeLaunch(session_.flight.peakMultiplier, RecoveryMethod::ReturnHome);
        } else if (step.reachedDestination && session_.preparedLaunch.config.frontierTransfer) {
            if (commitSurfaceTouchdown(destination, step.hardTouchdown)) {
                queueControllerHapticCue(
                    step.hardTouchdown ? ControllerHapticCue::Damage : ControllerHapticCue::Arrival);
            }
        } else if (step.flyby) {
            surfaceArrival_.reset();
            completeLaunch(destination.targetMultiplier, RecoveryMethod::TransferArrival);
            finishArrivalVisit("FLYBY RECORDED — trajectory left the destination influence.");
            save();
        } else if (step.asteroidHit) {
            state_.statusLine = "ASTEROID IMPACT \xE2\x80\x94 HULL " +
                display::fixed(std::max(0.0, session_.flight.hullRemaining), 0) + " / " +
                display::fixed(session_.flight.hullMaximum, 0) + " HP";
        } else if (!step.orbitCaptured) {
            state_.statusLine = launchStatusMessage(
                session_.preparedLaunch,
                session_.flight,
                session_.controls.actions);
        }

        if (state_.screen == Screen::Flight) {
            realtimeHudDirty_ = true;
        }
    } else if (state_.screen == Screen::Mining) {
        if (surfaceBaySequence_.kind == SurfaceBaySequenceKind::Extract) {
            surfaceBaySequence_.elapsed = std::min(
                surfaceBaySequence_.elapsed + std::clamp(deltaSeconds, 0.0, tuning::launch::maxFrameStepSeconds),
                tuning::mining::miningExtractionSequenceSeconds);
            if (surfaceBaySequence_.elapsed >= tuning::mining::miningExtractionSequenceSeconds &&
                !surfaceBaySequence_.handoffQueued) {
                queueMiningSceneHandoff(MiningSceneHandoff::DepartPlanet);
                surfaceBaySequence_.handoffQueued =
                    miningSceneHandoff_ == MiningSceneHandoff::DepartPlanet;
            }
        } else {
            const bool wasActive = state_.run.mining.active;
            const bool failureWasPending = state_.run.mining.failurePending;
            const bool thermalLockWasActive = state_.run.mining.drillThermalLock;
            updateMiningRun(state_, catalog_, deltaSeconds);
            const MiningRunState& mining = state_.run.mining;
            if (!failureWasPending &&
                mining.failurePending &&
                mining.operatorMode == MiningOperatorMode::Jetpack &&
                mining.operatorPresent &&
                mining.operatorIntegrity <= 0.0) {
                beginMiningEvaDeathPresentation();
            }
            if (!thermalLockWasActive && state_.run.mining.drillThermalLock) {
                keyboardRealtimeInput_.drilling = false;
                controllerRealtimeInput_.drilling = false;
                keyboardDrillPressed_ = false;
            }
            if (wasActive && !state_.run.mining.active) {
                state_.statusLine = std::string(text::status::miningAborted);
                save();
            }
        }
        if (state_.screen == Screen::Mining) {
            realtimeHudDirty_ = true;
        } else {
            panelDirty_ = true;
        }
    } else if (state_.screen == Screen::Flyby) {
        const bool wasCompleted = state_.run.approach.flyby.completed;
        updateFlybyRun(state_, deltaSeconds);
        if (!wasCompleted && state_.run.approach.flyby.completed) {
            const TransferAssistDefinition* transferAssist = catalog_.findTransferAssist(
                state_.run.approach.flyby.transferAssistId);
            const bool transferAssistRun = transferAssist != nullptr;
            const Destination* assistSource = transferAssist == nullptr
                ? nullptr
                : catalog_.findDestination(transferAssist->sourceDestinationId);
            const Destination* assistTarget = transferAssist == nullptr
                ? nullptr
                : catalog_.findDestination(transferAssist->targetDestinationId);
            const std::string assistSourceName = assistSource == nullptr ? "the source body" : assistSource->name;
            const std::string assistTargetName = assistTarget == nullptr ? "the target" : assistTarget->name;
            if (transferAssist != nullptr &&
                static_cast<int>(state_.run.approach.flyby.result) >= static_cast<int>(transferAssist->minimumGrade)) {
                (void)armTransferAssist(state_, catalog_);
            }
            const bool scenarioChallenge = state_.run.approach.flyby.purpose == FlybyPurpose::ScenarioChallenge &&
                !state_.run.approach.flyby.scenarioId.empty() && !state_.run.approach.flyby.scenarioStepId.empty();
            const ScenarioDefinition* scenario = scenarioChallenge
                ? findScenarioDefinition(catalog_, state_.run.approach.flyby.scenarioId)
                : nullptr;
            const ScenarioStepDefinition* challenge = scenario == nullptr
                ? nullptr
                : findScenarioStepDefinition(*scenario, state_.run.approach.flyby.scenarioStepId);
            if (scenarioChallenge) {
                // Realtime Flyby motion is intentionally transient, but the
                // finished grade is campaign progress. Record it before the
                // result modal is shown so a refresh cannot erase a Perfect
                // pass and strand the player back at an Active objective.
                (void)recordScenarioEvent(
                    state_,
                    catalog_,
                    {ScenarioEventKind::FlybyFinished,
                     state_.run.approach.flyby.scenarioId,
                     state_.run.approach.flyby.scenarioStepId,
                     state_.run.approach.flyby.scenarioId,
                     {},
                     1,
                     static_cast<int>(state_.run.approach.flyby.result)});
            }
            switch (state_.run.approach.flyby.result) {
            case FlybyGrade::Perfect:
                state_.statusLine = transferAssistRun
                    ? assistSourceName + " assist active. The ship is already moving toward " + assistTargetName + "."
                    : (scenarioChallenge
                          ? "Perfect corridor held. Claim the scenario reward."
                          : "Perfect slingshot. The next launch saves powered fuel and carries more velocity.");
                break;
            case FlybyGrade::Good:
                state_.statusLine = transferAssistRun
                    ? assistSourceName + " assist active. The Good pass reaches " + assistTargetName + " with a wilder flight."
                    : (scenarioChallenge
                          ? (challenge != nullptr && !challenge->failureExplanation.empty()
                                ? challenge->failureExplanation
                                : "Clean flyby, but the scenario requirement was not met.")
                          : "Clean flyby. Recon data secured.");
                break;
            case FlybyGrade::Miss:
            default:
                state_.statusLine = transferAssistRun
                    ? assistSourceName + " assist lost. The " + assistTargetName + " option remains open."
                    : (scenarioChallenge
                          ? (challenge != nullptr && !challenge->failureExplanation.empty()
                                ? challenge->failureExplanation
                                : "Scenario corridor lost.")
                          : "Missed flyby window. Approach options remain open.");
                break;
            }
            save();
            panelDirty_ = true;
        } else {
            realtimeHudDirty_ = true;
        }
    } else if (state_.screen == Screen::Orbit) {
        const bool wasCompleted = state_.run.approach.orbit.completed;
        updateOrbitRun(state_, deltaSeconds);
        if (!wasCompleted && state_.run.approach.orbit.completed) {
            switch (state_.run.approach.orbit.result) {
            case OrbitGrade::Perfect:
                state_.statusLine = "Perfect orbit plotted. Research run ready to stamp.";
                break;
            case OrbitGrade::Good:
                state_.statusLine = "Stable orbit completed. Research data ready to stamp.";
                break;
            case OrbitGrade::Miss:
            default:
                state_.statusLine = "Orbit window missed. Fuel and time spent, no telemetry validated.";
                break;
            }
            save();
            panelDirty_ = true;
        } else {
            realtimeHudDirty_ = true;
        }
    } else if (state_.screen == Screen::SurfaceScan) {
        const double clampedDelta = std::clamp(
            deltaSeconds,
            0.0,
            tuning::launch::maxFrameStepSeconds);
        state_.run.surfaceScan.elapsedSeconds += clampedDelta;
        state_.run.surfaceScan.successFanfareSeconds = std::max(
            0.0,
            state_.run.surfaceScan.successFanfareSeconds - clampedDelta);
        state_.run.surfaceScan.missFanfareSeconds = std::max(
            0.0,
            state_.run.surfaceScan.missFanfareSeconds - clampedDelta);
        realtimeHudDirty_ = true;
    } else if (state_.screen == Screen::Results) {
        session_.result.elapsed += std::clamp(deltaSeconds, 0.0, tuning::launch::maxFrameStepSeconds);
    }
    if (session_.arrivalFanfare.active) {
        session_.arrivalFanfare.elapsed = std::min(
            session_.arrivalFanfare.elapsed + std::clamp(deltaSeconds, 0.0, tuning::launch::maxFrameStepSeconds),
            tuning::session::arrivalFanfareSeconds);
        if (session_.arrivalFanfare.elapsed >= tuning::session::arrivalFanfareSeconds) {
            finishArrivalFanfare();
        }
    }
}

void RocketGameApp::renderScene()
{
    services_.renderer.render(snapshot());
}

void RocketGameApp::renderUi()
{
    // XP sources mutate only authoritative progression state. Presentation
    // opens the persisted draft here, after the current simulation update and
    // after failure/story transitions have had first priority.
    observeExpeditionExperience();
    maybeOpenLevelUpDraft();
    // Simulation can run several fixed steps before one presentation. Collapse
    // all resulting panel changes into a single UI synchronization so RmlUi is
    // never rebuilt once per simulation substep.
    if (panelDirty_) {
        refreshPanel();
    } else if (realtimeHudDirty_) {
        refreshRealtimeHud();
    }
    services_.ui.render();
}

void RocketGameApp::prepareForLaunch()
{
    if (titleScreenActive_ || state_.screen != Screen::Hangar) {
        return;
    }

    if (transferAssistCanContinue(state_, catalog_)) {
        const Destination* target = nextDestination(state_, catalog_);
        state_.statusLine = "Transfer momentum is already active. Continue to " +
            std::string(target == nullptr ? "the target" : target->name) + " instead of starting another sortie.";
        refreshPanel();
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

    syncLaunchConfig(state_, catalog_);
    const bool queuedRouteTransit = state_.run.routeTransit.active() &&
        routeLinkForTransit(catalog_, state_.run.routeTransit) != nullptr;
    const Destination& currentFrontier = currentDestination(state_, catalog_);
    const bool hiddenStarterOrigin = currentFrontier.hiddenFromProgression;
    if (!currentDestinationLaunchReady(state_, catalog_)) {
        state_.statusLine = "Install the required launch upgrade before this mission.";
        refreshPanel();
        return;
    }

    if (!ui::briefings::acknowledged(state_.meta.acknowledgedActivityBriefingIds, ui::briefings::launch)) {
        ui::briefings::acknowledge(state_.meta.acknowledgedActivityBriefingIds, ui::briefings::launch);
        // Persist the one-time briefing acknowledgement before Flight begins.
        save();
    }
    if (state_.meta.launchLessons.stage == LaunchTrainingStage::FlightControlsCalibration) {
        ui::briefings::acknowledge(
            state_.meta.acknowledgedActivityBriefingIds,
            ui::briefings::flightControlsCalibration);
        // Store the acknowledgement while there is still no active flight to
        // restore. This also covers keyboard/controller activation.
        save();
    }

    state_.run.active = true;
    if (!hiddenStarterOrigin && !queuedRouteTransit) {
        state_.launchConfig.frontierTransfer =
            hostileSystemActive(state_) || launchStageUsesArrival(state_.meta.launchLessons.stage);
        state_.launchConfig.missionKind = LaunchMissionKind::Standard;
        state_.launchConfig.destinationId = currentFrontier.id;
        state_.launchConfig.burnGoalMultiplier = state_.launchConfig.frontierTransfer
            ? currentFrontier.targetMultiplier
            : defaultProvingTarget(currentFrontier);
    }
    beginLaunchSession(rocket::prepareLaunch(state_, catalog_, rng_));
    consumeNextLaunchBoost();
    state_.screen = Screen::Flight;
    state_.statusLine = miningDroneTransferEnabled(state_)
        ? std::string(text::status::droneStowing)
        : std::string(text::status::preflightReadyWithoutDrone);
    // Persist one-attempt momentum consumption and the preflight state
    // immediately. Once controls go live, Flight autosaves its physical
    // trajectory without recreating a retired approach board.
    save();
    // Queue semantic focus before rebuilding the persistent hosts. The target
    // does not exist on the Hangar tree, so GameRmlUi retains the request and
    // applies it as part of mounting the Flight scene overlay.
    services_.ui.requestFocus("action:start_launch");
    refreshPanel();
}

void RocketGameApp::startLaunch()
{
    if (state_.screen == Screen::Hangar) {
        prepareForLaunch();
        return;
    }

    if (state_.screen != Screen::Flight || session_.flightArmed) {
        return;
    }

    if (session_.preflightElapsed < tuning::session::preflightBoardingSeconds) {
        session_.launchQueued = true;
        state_.statusLine = std::string(text::status::launchQueued);
        refreshPanel();
        return;
    }

    session_.launchQueued = false;
    session_.flightArmed = true;
    session_.flight.active = true;
    state_.statusLine = session_.preparedLaunch.config.frontierTransfer
        ? std::string(text::status::transferBurnStarted)
        : std::string(text::status::provingBurnStarted);
    refreshPanel();
}

void RocketGameApp::launchMove(double steerAxis, double throttleAxis)
{
    if (state_.screen != Screen::Flight || !session_.flightArmed || session_.destruction.active) {
        return;
    }
    keyboardRealtimeInput_.moveX = std::clamp(steerAxis, -1.0, 1.0);
    keyboardRealtimeInput_.moveY = std::clamp(throttleAxis, -1.0, 1.0);
    applyRealtimeInputs();
}

void RocketGameApp::returnHome()
{
    if (state_.screen != Screen::Flight || !session_.flightArmed ||
        session_.destruction.active || session_.controls.actions.returningHome ||
        session_.preparedLaunch.config.missionKind == LaunchMissionKind::StraylightApproach) {
        return;
    }

    const PreparedLaunch flightModel = currentFlightModel();
    const TelemetryEvent event = launchTelemetryAt(flightModel, session_.flight);
    recordTelemetryPeak(event);
    beginLaunchReturn(session_.flight);
    session_.controls.actions.returningHome = true;
    state_.statusLine = std::string(text::status::returnBurnRotating);
    panelDirty_ = true;
}

void RocketGameApp::arrivalOps()
{
    if (state_.screen != Screen::Flight || !session_.flightArmed || session_.controls.actions.returningHome || session_.preparedLaunch.config.frontierTransfer) {
        return;
    }

    const Destination& destination = currentDestination(state_, catalog_);
    if (destination.tier < 1 || session_.currentMultiplier < destination.targetMultiplier) {
        return;
    }

    recordTelemetryPeak(telemetryAt(currentFlightModel(), session_.currentMultiplier));
    completeLaunch(session_.currentMultiplier, RecoveryMethod::TransferArrival);
}

void RocketGameApp::acknowledgeStoryBriefing()
{
    if (state_.screen != Screen::StoryBriefing) {
        return;
    }
    if (state_.storyBriefing.pending == StoryBriefingId::StraylightApproach) {
        beginStraylightApproach();
        return;
    }
    const StoryBriefingId acknowledged = state_.storyBriefing.pending;
    if (!rocket::acknowledgeStoryBriefing(state_, catalog_)) {
        return;
    }
    if (acknowledged == StoryBriefingId::StraylightDiscovery) {
        beginStraylightApproach();
        return;
    }
    save();
    refreshPanel();
}

void RocketGameApp::beginStraylightApproach()
{
    if (state_.storyBriefing.pending != StoryBriefingId::StraylightApproach ||
        currentDestination(state_, catalog_).id != content::destination::neptune) {
        return;
    }

    // Realtime flights are intentionally not restored mid-flight. Persist the
    // explicit approach beat first so a reload returns to a safe, player-driven
    // launch point instead of replaying the discovery or skipping the transfer.
    state_.statusLine = "UNKNOWN CONTACT LOCKED — ceremonial approach ready.";
    save();

    state_.run.active = true;
    state_.run.routeTransit = {};
    state_.launchConfig.routeTransit = {};
    state_.launchConfig.destinationId = content::destination::neptune;
    state_.launchConfig.frontierTransfer = true;
    state_.launchConfig.missionKind = LaunchMissionKind::StraylightApproach;
    state_.launchConfig.burnGoalMultiplier = currentDestination(state_, catalog_).targetMultiplier;
    beginLaunchSession(rocket::prepareLaunch(state_, catalog_, rng_));
    state_.screen = Screen::Flight;
    refreshPanel();
}

void RocketGameApp::cutEngines()
{
    if (state_.screen != Screen::Flight || !session_.flightArmed ||
        session_.destruction.active || !session_.preparedLaunch.heatEnabled) {
        return;
    }

    session_.controls.actions.cutEnginesActive = !session_.controls.actions.cutEnginesActive;
    state_.statusLine = session_.controls.actions.cutEnginesActive
        ? std::string(text::status::engineCutConfirmed)
        : std::string(text::status::thrustRestored);
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
            if (state_.storyBriefing.pending == StoryBriefingId::StraylightDiscovery) {
                state_.screen = Screen::StoryBriefing;
                state_.statusLine = "An impossible contact is resolving beyond Neptune.";
            } else {
                beginSurfaceExpeditionOrRefit();
            }
            syncLaunchConfig(state_, catalog_);
            save();
            panelDirty_ = true;
            return;
        }
        if (openRefitIfAvailable()) {
            state_.statusLine = std::string(text::status::refitWindowOpened);
        } else {
            state_.screen = navigationAvailable(state_) ? Screen::Navigation : Screen::Hangar;
            state_.statusLine = std::string(text::status::refitWindowClosed);
        }
        syncLaunchConfig(state_, catalog_);
        save();
    } else if (state_.screen == Screen::SurfaceUpgrade) {
        state_.statusLine = "Choose an eligible expedition upgrade to continue.";
        panelDirty_ = true;
        return;
    } else if (state_.screen == Screen::Upgrade) {
        if (curatedProvingRefitsActive(state_) &&
            !refitWindowPresentation(state_, catalog_).showSkip) {
            state_.statusLine = "Install the required launch upgrade to continue.";
            panelDirty_ = true;
            return;
        }
        state_.run.offerCrewUpgradeIds = {};
        state_.run.refitEntitled = false;
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

    const ScenarioObjectivePresentation objective = scenarioDepartureChallengeForDestination(
        state_,
        catalog_,
        state_.run.approach.destinationId);
    if (objective.available) {
        state_.statusLine = "Jupiter departure requires the Perfect Slingshot challenge. Use its route card.";
        panelDirty_ = true;
        return;
    }

    ui::briefings::acknowledge(state_.meta.acknowledgedActivityBriefingIds, ui::briefings::flyby);
    startArrivalFlybyRun(state_, catalog_);
    state_.statusLine = "Manual flyby started. Stay in the approach corridor.";
    syncLaunchConfig(state_, catalog_);
    save();
    panelDirty_ = true;
}

void RocketGameApp::acknowledgeApproachIntroduction()
{
    if (state_.screen != Screen::ArrivalOps
        || state_.run.approach.destinationId != content::destination::moon) {
        return;
    }

    ui::briefings::acknowledge(state_.meta.acknowledgedActivityBriefingIds, ui::briefings::approach);
    save();
    panelDirty_ = true;
}

void RocketGameApp::acknowledgeJupiterWindow()
{
    if (!jupiterWindowReviewed(state_, catalog_)) {
        (void)performScenarioAction(
            state_,
            catalog_,
            content::scenario::marsBayExpansion,
            "funding",
            ScenarioActionKind::AcknowledgeBriefing);
    }
    state_.screen = Screen::Hangar;
    state_.statusLine =
        "Jupiter options reviewed. Build permanent margin, take the Mars slingshot, or stack both.";
    syncLaunchConfig(state_, catalog_);
    save();
    panelDirty_ = true;
}

void RocketGameApp::openJupiterRefit()
{
    acknowledgeJupiterWindow();
    if (!openRefitIfAvailable(true)) {
        state_.statusLine =
            "Refit is closed. Successful Mars operations earn credits and reopen it; the slingshot remains available.";
        save();
        panelDirty_ = true;
    }
}

void RocketGameApp::beginJupiterSlingshot()
{
    acknowledgeJupiterWindow();
    beginTransferAssist(content::transferAssist::marsJupiter);
}

void RocketGameApp::beginTransferAssist(std::string_view definitionId)
{
    const TransferAssistDefinition* definition = catalog_.findTransferAssist(definitionId);
    if (definition != nullptr &&
        !scenarioStepBriefingAcknowledged(state_, definition->availabilityScenarioId, definition->availabilityStepId)) {
        (void)performScenarioAction(
            state_, catalog_, definition->availabilityScenarioId, definition->availabilityStepId,
            ScenarioActionKind::AcknowledgeBriefing);
    }
    if (!startTransferAssistRun(state_, catalog_, definitionId)) {
        panelDirty_ = true;
        return;
    }
    save();
    panelDirty_ = true;
}

void RocketGameApp::continueJupiterSlingshot()
{
    continueTransferAssist();
}

void RocketGameApp::continueTransferAssist()
{
    if (state_.screen == Screen::Flyby && state_.run.approach.flyby.active &&
        state_.run.approach.flyby.completed &&
        !state_.run.approach.flyby.transferAssistId.empty()) {
        const TransferAssistDefinition* definition = catalog_.findTransferAssist(state_.run.approach.flyby.transferAssistId);
        if (definition == nullptr || static_cast<int>(state_.run.approach.flyby.result) < static_cast<int>(definition->minimumGrade)) {
            completeFlybyRun(state_, catalog_);
            save();
            panelDirty_ = true;
            return;
        }
        (void)armTransferAssist(state_, catalog_);
        completeFlybyRun(state_, catalog_);
    }
    if (!transferAssistCanContinue(state_, catalog_)) {
        state_.statusLine = "Reach the required transfer-assist grade before continuing.";
        save();
        panelDirty_ = true;
        return;
    }
    state_.screen = Screen::Hangar;
    attemptFrontierTransfer();
}

void RocketGameApp::flybyMove(double xAxis, double yAxis)
{
    if (state_.screen != Screen::Flyby) {
        return;
    }
    keyboardRealtimeInput_.moveX = std::clamp(xAxis, -1.0, 1.0);
    keyboardRealtimeInput_.moveY = std::clamp(yAxis, -1.0, 1.0);
    applyRealtimeInputs();
}

void RocketGameApp::flybyAbort()
{
    if (state_.screen != Screen::Flyby || state_.run.approach.flyby.completed) {
        return;
    }
    const bool jupiterSlingshot = !state_.run.approach.flyby.transferAssistId.empty();
    const bool scenarioChallenge = state_.run.approach.flyby.purpose == FlybyPurpose::ScenarioChallenge &&
        !state_.run.approach.flyby.scenarioId.empty() && !state_.run.approach.flyby.scenarioStepId.empty();
    abortFlybyRun(state_, catalog_);
    if (!scenarioChallenge && !jupiterSlingshot) {
        state_.statusLine = "Flyby aborted. No recon reward earned.";
    }
    save();
    panelDirty_ = true;
}

void RocketGameApp::flybyContinue()
{
    if (state_.screen != Screen::Flyby || !state_.run.approach.flyby.completed) {
        return;
    }

    const FlybyGrade grade = state_.run.approach.flyby.result;
    if (!state_.run.approach.flyby.transferAssistId.empty()) {
        const TransferAssistDefinition* definition = catalog_.findTransferAssist(state_.run.approach.flyby.transferAssistId);
        if (definition != nullptr && static_cast<int>(grade) >= static_cast<int>(definition->minimumGrade)) {
            continueTransferAssist();
        } else {
            completeFlybyRun(state_, catalog_);
            syncLaunchConfig(state_, catalog_);
            save();
            panelDirty_ = true;
        }
        return;
    }
    const bool scenarioChallenge = state_.run.approach.flyby.purpose == FlybyPurpose::ScenarioChallenge &&
        !state_.run.approach.flyby.scenarioId.empty() && !state_.run.approach.flyby.scenarioStepId.empty();
    if (scenarioChallenge) {
        // Completion records a typed scenario event. A Perfect leaves an
        // explicit claim instead of advancing a route implicitly; failures
        // retain their one-time explanation through scenario progress.
        completeFlybyRun(state_, catalog_);
        syncLaunchConfig(state_, catalog_);
        save();
        panelDirty_ = true;
        return;
    }
    const bool genericRouteCleared =
        (grade == FlybyGrade::Good || grade == FlybyGrade::Perfect) &&
        bankFlybyRouteClearance(state_, catalog_);
    completeFlybyRun(state_, catalog_);
    switch (grade) {
    case FlybyGrade::Perfect:
        if (genericRouteCleared) {
            finishArrivalVisit("Planet skipped. Powered-fuel savings and extra velocity stored for the next launch.");
        } else if (queueBlockedArrivalFlybyRecovery(state_, catalog_)) {
            state_.statusLine = "RECOVERY ROUTE — fly back to the prior staging body before reapproaching this objective.";
            syncLaunchConfig(state_, catalog_);
            attemptFrontierTransfer();
        } else {
            finishArrivalVisit("Planet skipped. The active capture objective and its next route remain blocked.");
        }
        break;
    case FlybyGrade::Good:
        if (genericRouteCleared) {
            finishArrivalVisit("Planet skipped. Pass Through complete.");
        } else if (queueBlockedArrivalFlybyRecovery(state_, catalog_)) {
            state_.statusLine = "RECOVERY ROUTE — fly back to the prior staging body before reapproaching this objective.";
            syncLaunchConfig(state_, catalog_);
            attemptFrontierTransfer();
        } else {
            finishArrivalVisit("Planet skipped. The active capture objective and its next route remain blocked.");
        }
        break;
    case FlybyGrade::Miss:
    default:
        state_.statusLine = "Missed window. Approach remains uncommitted.";
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

    ui::briefings::acknowledge(state_.meta.acknowledgedActivityBriefingIds, ui::briefings::orbit);
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
    keyboardRealtimeInput_.moveX = std::clamp(xAxis, -1.0, 1.0);
    keyboardRealtimeInput_.moveY = std::clamp(yAxis, -1.0, 1.0);
    applyRealtimeInputs();
}

void RocketGameApp::orbitAbort()
{
    if (state_.screen != Screen::Orbit || state_.run.approach.orbit.completed) {
        return;
    }
    abortOrbitRun(state_);
    state_.statusLine = "Orbit insertion aborted. No research reward earned.";
    save();
    panelDirty_ = true;
}

void RocketGameApp::orbitContinue()
{
    if (state_.screen != Screen::Orbit || !state_.run.approach.orbit.completed) {
        return;
    }

    const OrbitGrade grade = state_.run.approach.orbit.result;
    completeOrbitRun(state_, catalog_);
    switch (grade) {
    case OrbitGrade::Perfect:
        (void)captureArrivalOrbit(state_);
        state_.statusLine = "Orbit captured. Choose mapped landing or depart with science.";
        break;
    case OrbitGrade::Good:
        (void)captureArrivalOrbit(state_);
        state_.statusLine = "Orbit captured. Choose mapped landing or depart with science.";
        break;
    case OrbitGrade::Miss:
    default:
        state_.statusLine = "Missed orbit. Approach remains uncommitted.";
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

    ui::briefings::acknowledge(state_.meta.acknowledgedActivityBriefingIds, ui::briefings::landing);
    const bool mappedDescent = state_.run.approach.rewards.orbitAwarded;
    state_.run.approach.phase = ApproachPhase::Descent;
    state_.run.approach.descent.active = true;
    state_.run.approach.descent.directDescent = !mappedDescent;
    state_.run.approach.descent.corridorWidth = mappedDescent ? 1.0 : 0.65;
    state_.run.approach.descent.turbulence = mappedDescent ? 0.0 : 0.35;
    bankArrivalLandingFlightData(state_, catalog_);
    state_.run.approach.rewards.landingRecorded = true;
    beginSurfaceExpeditionOrRefit();
    state_.statusLine = mappedDescent
        ? "Mapped descent committed. Orbital survey removed the +20 descent hazard."
        : "Unmapped descent committed. Surface hazard +20.";
    syncLaunchConfig(state_, catalog_);
    save();
    panelDirty_ = true;
}

void RocketGameApp::departCapturedOrbit()
{
    if (state_.screen != Screen::ArrivalOps || !canDepartCapturedArrivalOrbit(state_, catalog_)) {
        state_.statusLine = arrivalOperationBlockReason(state_, catalog_, "depart");
        panelDirty_ = true;
        return;
    }

    finishArrivalVisit("Departed with orbital science. No route clearance or surface Flight Data earned.");
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
    if (outcome.applied) {
        ui::briefings::acknowledge(
            state_.meta.acknowledgedActivityBriefingIds,
            ui::briefings::surfaceSurveyIntroduction);
    }
    state_.statusLine = surfaceActionSummary(outcome);
    save();
    panelDirty_ = true;
}

void RocketGameApp::mineSurface()
{
    if (state_.screen != Screen::SurfaceExpedition) {
        return;
    }

    if (!surfaceOpsTutorialMiningUnlocked(state_)) {
        state_.statusLine = "Set a start depth before deploying the Mining Rig.";
        panelDirty_ = true;
        return;
    }
    const ScenarioObjectivePresentation objective = scenarioObjectiveForDestination(
        state_,
        catalog_,
        state_.run.planetaryExpedition.destinationId);
    if (objective.available && objective.mandatoryBriefing && !objective.briefingAcknowledged) {
        state_.statusLine = "Acknowledge " + objective.title + " before deploying the Mining Rig.";
        panelDirty_ = true;
        return;
    }

    queueMiningSceneHandoff(MiningSceneHandoff::EnterMining);
}

void RocketGameApp::startMiningRunAfterFade()
{
    if (state_.screen != Screen::SurfaceExpedition) {
        return;
    }
    if (!surfaceOpsTutorialMiningUnlocked(state_)) {
        state_.statusLine = "Set a start depth before deploying the Mining Rig.";
        panelDirty_ = true;
        return;
    }
    PlanetaryExpeditionState& expedition = state_.run.planetaryExpedition;
    const ScenarioObjectivePresentation objective = scenarioObjectiveForDestination(
        state_,
        catalog_,
        expedition.destinationId);
    if (objective.available && objective.mandatoryBriefing && !objective.briefingAcknowledged) {
        state_.statusLine = "Acknowledge " + objective.title + " before deploying the Mining Rig.";
        panelDirty_ = true;
        return;
    }

    const ScenarioDefinition* definition = objective.available
        ? scenarioDefinitionForRuntimeId(state_, catalog_, objective.scenarioId)
        : nullptr;
    const ScenarioInstance* instance = objective.available
        ? findScenarioInstance(state_.meta, objective.scenarioId)
        : nullptr;
    const ScenarioDefinition resolved = definition != nullptr && instance != nullptr
        ? resolveScenarioDefinition(*definition, *instance)
        : ScenarioDefinition {};
    const ScenarioStepDefinition* step = definition == nullptr
        ? nullptr
        : (instance == nullptr
               ? findScenarioStepDefinition(*definition, objective.stepId)
               : findScenarioStepDefinition(resolved, objective.stepId));
    const bool pendingSite = !expedition.pendingMiningSiteDefinitionId.empty();
    if (!pendingSite && step != nullptr &&
        step->completionEvent == ScenarioEventKind::ProtectedObjectiveExtracted &&
        !step->miningSiteDefinitionId.empty()) {
        // Mine is the stable player-facing verb for every excavation. When a
        // campaign recovery is active, direct Mine into its authored site
        // rather than hiding the action or asking the player to discover a
        // separate story-labelled substitute.
        expedition.pendingScenarioId = objective.scenarioId;
        expedition.pendingScenarioStepId = objective.stepId;
        expedition.pendingMiningSiteDefinitionId = step->miningSiteDefinitionId;
    }

    const MiningSiteDefinition* site = !expedition.pendingMiningSiteDefinitionId.empty()
        ? findMiningSiteDefinition(catalog_, expedition.pendingMiningSiteDefinitionId)
        : nullptr;
    const bool siteRequiresHazard = site != nullptr && std::any_of(
        site->cocoon.layers.begin(),
        site->cocoon.layers.end(),
        [](const MiningCocoonLayerDefinition& layer) { return layer.requiredHazardMark > 0; });
    if (siteRequiresHazard) {
        const bool hazardEquipped = std::any_of(
            state_.meta.equippedDroneIds.begin(),
            state_.meta.equippedDroneIds.end(),
            [&](const std::string& equippedId) {
                const auto found = std::find_if(
                    catalog_.miniDrones.begin(),
                    catalog_.miniDrones.end(),
                    [&](const MiniDrone& drone) { return drone.id == equippedId; });
                return found != catalog_.miniDrones.end() && found->role == MiniDroneRole::Hazard;
            });
        if (!hazardEquipped) {
            state_.screen = Screen::DroneOps;
            state_.statusLine = "Equip a Hazard Drone before beginning this recovery site.";
            panelDirty_ = true;
            return;
        }
    }

    const SurfaceActionOutcome outcome = startMiningRun(state_, catalog_);
    if (outcome.applied) {
        ui::briefings::acknowledge(state_.meta.acknowledgedActivityBriefingIds, ui::briefings::mining);
    }
    state_.statusLine = outcome.applied ? std::string(text::status::miningStarted) : surfaceActionSummary(outcome);
    save();
    panelDirty_ = true;
}

void RocketGameApp::pushSurface()
{
    if (state_.screen != Screen::SurfaceExpedition) {
        return;
    }
    if (!surfaceOpsTutorialDigUnlocked(state_)) {
        state_.statusLine = "Log a Survey before digging a tunnel.";
        panelDirty_ = true;
        return;
    }

    const SurfaceActionOutcome outcome = startSurfacePushRun(state_, rng_);
    if (outcome.applied) {
        ui::briefings::acknowledge(
            state_.meta.acknowledgedActivityBriefingIds,
            ui::briefings::surfaceDigIntroduction);
    }
    state_.statusLine = surfaceActionSummary(outcome);
    save();
    panelDirty_ = true;
}

void RocketGameApp::extractSurface()
{
    if (state_.screen != Screen::SurfaceExpedition) {
        return;
    }

    const SurfaceActionOutcome outcome = extractSurfacePayload(state_, catalog_);
    if (!outcome.applied) {
        panelDirty_ = true;
        return;
    }

    state_.statusLine = surfaceActionSummary(outcome);
    if (!openRefitIfAvailable()) {
        state_.screen = navigationAvailable(state_) ? Screen::Navigation : Screen::Hangar;
    }
    save();
    panelDirty_ = true;
}

void RocketGameApp::selectSurfaceUpgrade(int index)
{
    if (state_.screen != Screen::SurfaceUpgrade || levelUpActivationLocked()) {
        return;
    }

    if (!chooseRunUpgrade(state_, catalog_, index)) {
        state_.statusLine = "That expedition upgrade is no longer eligible.";
        panelDirty_ = true;
        return;
    }
    // Gameplay state and the persisted offer are committed immediately. The
    // old DOM remains visible for a short focused-card resolve beat before the
    // next queued offer (or the captured gameplay screen) replaces it.
    levelUp_.resolving = true;
    levelUp_.fanfareActive = false;
    levelUp_.elapsed = kLevelUpFanfareSeconds;
    levelUp_.resolveElapsed = 0.0;
    levelUp_.selectedOfferIndex = index;
    state_.statusLine = "Expedition upgrade installed.";
    save();
    realtimeHudDirty_ = true;
}

void RocketGameApp::openDroneOps()
{
    const bool miningService = state_.screen == Screen::Mining
        && state_.run.mining.active
        && miningAtReturnZone(state_.run.mining);
    const bool legacySurface = state_.screen == Screen::SurfaceExpedition;
    if ((!miningService && !legacySurface)
        || !state_.run.planetaryExpedition.active
        || !droneBayUnlocked(state_)) {
        state_.statusLine = "Complete the Prospector contract before assigning Support Drones.";
        panelDirty_ = true;
        return;
    }

    ui::briefings::acknowledge(state_.meta.acknowledgedActivityBriefingIds, ui::briefings::miniDrones);
    ensureDroneBayState(state_, catalog_);
    state_.screen = Screen::DroneOps;
    state_.statusLine = "Choose Support Drones for the next mining run.";
    save();
    panelDirty_ = true;
}

void RocketGameApp::backToSurfaceOps()
{
    if (state_.screen != Screen::DroneOps) {
        return;
    }

    if (state_.run.planetaryExpedition.active && state_.run.mining.active) {
        state_.screen = Screen::Mining;
        state_.statusLine = "Drone loadout updated. Surface control restored.";
    } else if (openRefitIfAvailable(true)) {
        state_.statusLine = "Mars requires 20 transfer fuel. Use the Moon mission credits to install Fuel Tanks II.";
    } else {
        state_.screen = Screen::Hangar;
    }
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
    if (state_.screen != Screen::Mining || surfaceBaySequence_.active()) {
        return;
    }
    keyboardRealtimeInput_.moveX = std::clamp(xAxis, -1.0, 1.0);
    keyboardRealtimeInput_.moveY = std::clamp(yAxis, -1.0, 1.0);
    applyRealtimeInputs();
}

void RocketGameApp::miningAim(double normalizedX, double normalizedY)
{
    if (state_.screen != Screen::Mining || surfaceBaySequence_.active()) {
        return;
    }
    keyboardRealtimeInput_.aimX = normalizedX;
    keyboardRealtimeInput_.aimY = normalizedY;
    setMiningAim(state_, normalizedX, normalizedY);
}

void RocketGameApp::miningPointerAim(double viewportX, double viewportY)
{
    const ViewportMetrics viewport = services_.host.viewportMetrics();
    const MiningPointerAim aim = miningPointerAimFromViewport(
        viewportX,
        viewportY,
        viewport.logicalWidth,
        viewport.logicalHeight);
    if (aim.valid) {
        miningAim(aim.x, aim.y);
    }
}

void RocketGameApp::miningFire(bool active)
{
    if (state_.screen != Screen::Mining || surfaceBaySequence_.active()) {
        keyboardRealtimeInput_.firing = false;
        return;
    }
    keyboardRealtimeInput_.firing = active;
    applyRealtimeInputs();
}

void RocketGameApp::miningDrill(bool active)
{
    if (state_.screen != Screen::Mining || surfaceBaySequence_.active()) {
        return;
    }
    keyboardRealtimeInput_.drilling = active;
    applyRealtimeInputs();
}

void RocketGameApp::miningOperatorToggle()
{
    if (state_.screen != Screen::Mining || surfaceBaySequence_.active()) {
        return;
    }
    const bool toggled = toggleMiningOperator(state_);
    keyboardRealtimeInput_.firing = false;
    keyboardRealtimeInput_.drilling = false;
    controllerRealtimeInput_.firing = false;
    controllerRealtimeInput_.drilling = false;
    setMiningFire(state_, false);
    setMiningDrilling(state_, false);
    // A successful immediate F toggle gets the same one-frame confirmation
    // pulse as the completed controller hold ring, then clears independently
    // of a keyboard key-up event.
    setMiningOperatorToggleProgress(state_, toggled ? 1.0 : 0.0);
    miningOperatorToggleConfirmationSeconds_ = toggled ? 0.18 : 0.0;
    panelDirty_ = true;
    realtimeHudDirty_ = true;
}

void RocketGameApp::miningOperatorToggleProgress(double progress)
{
    if (state_.screen != Screen::Mining || surfaceBaySequence_.active()) {
        return;
    }
    setMiningOperatorToggleProgress(state_, std::clamp(progress, 0.0, 1.0));
    realtimeHudDirty_ = true;
}

void RocketGameApp::miningKeyboardDrill(bool active)
{
    if (state_.screen != Screen::Mining || surfaceBaySequence_.active()) {
        keyboardDrillPressed_ = false;
        return;
    }
    if (active == keyboardDrillPressed_) {
        return;
    }
    keyboardDrillPressed_ = active;
    if (miningDrillMode_ == MiningDrillMode::Toggle) {
        if (active) {
            keyboardRealtimeInput_.drilling = !keyboardRealtimeInput_.drilling;
        }
    } else {
        keyboardRealtimeInput_.drilling = active;
    }
    applyRealtimeInputs();
}

void RocketGameApp::miningScanner()
{
    if (state_.screen != Screen::Mining || surfaceBaySequence_.active()) {
        return;
    }

    pulseMiningScanner(state_, catalog_);
    panelDirty_ = true;
}

void RocketGameApp::miningTether()
{
    if (state_.screen != Screen::Mining || surfaceBaySequence_.active()) {
        return;
    }

    const MiningArtifactObject before = state_.run.mining.artifact;
    const bool beforeOperatorRigTethered = state_.run.mining.operatorRigTethered;
    toggleMiningTether(state_);
    const MiningRunState& mining = state_.run.mining;
    const MiningArtifactObject& artifact = state_.run.mining.artifact;
    if (artifact.tethered && !before.tethered) {
        state_.statusLine = "Artifact tether locked. Pull it free and bring it to the ship bay.";
    } else if (!artifact.tethered && before.tethered) {
        state_.statusLine = "Artifact tether released.";
    } else if (mining.operatorRigTethered && !beforeOperatorRigTethered) {
        state_.statusLine = "Jetpack tether locked to the Mining Rig. Tow it back to the ship.";
    } else if (!mining.operatorRigTethered && beforeOperatorRigTethered) {
        state_.statusLine = "Jetpack tether released.";
    }
    panelDirty_ = true;
}

void RocketGameApp::miningRepairDrill()
{
    if (state_.screen != Screen::Mining || surfaceBaySequence_.active()) {
        return;
    }
    MiningRunState& mining = state_.run.mining;
    const int cost = miningDrillRepairCost(mining);
    if (!miningAtReturnZone(mining)) {
        state_.statusLine = "Return to the ship to repair the drill bit.";
    } else if (cost <= 0) {
        state_.statusLine = "Drill bit integrity is already full.";
    } else if (mining.stowedMaterials.common < cost) {
        state_.statusLine = "Need " + std::to_string(cost) + " stowed common materials to repair the drill bit.";
    } else if (repairMiningDrill(state_)) {
        state_.statusLine = "Drill bit repaired for " + std::to_string(cost) + " common materials.";
        save();
    }
    panelDirty_ = true;
}

void RocketGameApp::miningRepairDrone()
{
    if (state_.screen != Screen::Mining || surfaceBaySequence_.active()) {
        return;
    }
    MiningRunState& mining = state_.run.mining;
    const bool evaActive =
        mining.operatorMode == MiningOperatorMode::Jetpack &&
        mining.operatorPresent;
    const bool repairingDisabledRig =
        mining.rigDisabled && miningRigAtReturnZone(mining);
    const bool repairingOperator = evaActive && !repairingDisabledRig;
    const int cost = repairingOperator
        ? (mining.operatorIntegrity < 1.0
                ? static_cast<int>(tuning::mining::operatorIntegrityRepairCommonCost)
                : 0)
        : miningDroneRepairCost(mining);
    if (!miningAtReturnZone(mining)) {
        state_.statusLine = repairingOperator
            ? "Return to the shuttle to repair the EVA suit."
            : "Return to the ship to repair the Mining Rig.";
    } else if (repairingDisabledRig) {
        if (repairMiningDrone(state_)) {
            state_.statusLine =
                "Shuttle umbilical patch complete: Rig restored to 35% integrity. Move beside it and re-enter to keep mining.";
            save();
        }
    } else if (cost <= 0) {
        state_.statusLine = repairingOperator
            ? "EVA suit integrity is already full."
            : "Mining Rig integrity is already full.";
    } else if (mining.stowedMaterials.common < cost) {
        state_.statusLine =
            "Need " + std::to_string(cost) +
            " stowed common materials to repair the " +
            (repairingOperator ? "EVA suit." : "Mining Rig.");
    } else if (repairingOperator
                   ? repairMiningOperator(state_)
                   : repairMiningDrone(state_)) {
        state_.statusLine =
            std::string(repairingOperator ? "EVA suit" : "Mining Rig") +
            " repaired for " + std::to_string(cost) +
            " common materials." +
            (repairingDisabledRig ? " Move beside it and re-enter to keep mining." : "");
        save();
    }
    panelDirty_ = true;
}

void RocketGameApp::miningStow()
{
    if (state_.screen != Screen::Mining || surfaceBaySequence_.active()) {
        return;
    }
    if (!miningAtReturnZone(state_.run.mining)) {
        state_.statusLine = std::string(text::status::miningReturnToShip);
        panelDirty_ = true;
        return;
    }

    const bool banked = bankMiningPayloadAtShip(state_, catalog_);
    state_.statusLine = banked
        ? "Payload banked. Surface control remains active."
        : "No rig payload can transfer. Drone cargo must physically return before unloading.";
    save();
    panelDirty_ = true;
}

void RocketGameApp::miningWaitForDrones()
{
    if (state_.screen != Screen::Mining || surfaceBaySequence_.active()) {
        return;
    }
    if (!miningAtReturnZone(state_.run.mining)) {
        state_.statusLine = "Return to the shuttle before recalling Support Drones.";
        panelDirty_ = true;
        return;
    }

    if (requestMiningDroneRecall(state_)) {
        state_.statusLine = "Support Drones recalled. Their payload counts only after they reach the shuttle.";
        save();
    } else {
        state_.statusLine = "All Support Drone payload is already aboard.";
    }
    panelDirty_ = true;
}

void RocketGameApp::miningDepart()
{
    if (state_.screen != Screen::Mining || surfaceBaySequence_.active() ||
        miningSceneHandoff_ != MiningSceneHandoff::None) {
        return;
    }

    // Settle the physical visit once, then retain only a presentation copy of
    // the landed scene while the bay closes and the ship lifts off. The save
    // remains at the pre-departure state until the cinematic handoff commits.
    const GameState preDepartureState = state_;
    MiningRunState extractionVisual = state_.run.mining;
    const SurfaceActionOutcome miningOutcome = finishMiningRun(state_, catalog_, false);
    if (!miningOutcome.applied) {
        state_.statusLine = surfaceActionSummary(miningOutcome);
        panelDirty_ = true;
        return;
    }
    const SurfaceActionOutcome surfaceOutcome = extractSurfacePayload(state_, catalog_);
    if (!surfaceOutcome.applied) {
        state_ = preDepartureState;
        state_.statusLine = "Departure settlement could not complete. Payload and surface state were preserved.";
        panelDirty_ = true;
        return;
    }

    extractionVisual.active = false;
    extractionVisual.failurePending = false;
    extractionVisual.drilling = false;
    extractionVisual.moveX = 0.0;
    extractionVisual.moveY = 0.0;
    extractionVisual.cargo = 0;
    extractionVisual.temporaryMaterials = {};
    extractionVisual.temporaryArtifacts.clear();
    extractionVisual.stowedCargo = 0;
    extractionVisual.stowedMaterials = {};
    extractionVisual.stowedArtifacts.clear();
    extractionVisual.combatProjectiles.clear();
    extractionVisual.damageNumbers.clear();
    state_.run.mining = std::move(extractionVisual);
    state_.screen = Screen::Mining;
    state_.statusLine = "DEPARTING — Bay secured. Ignition sequence.";
    surfaceBaySequence_ = {SurfaceBaySequenceKind::Extract, false, 0.0};
    releaseRealtimeInputs(true);
    panelDirty_ = true;
    realtimeHudDirty_ = true;
}

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
    state_.statusLine = surfaceActionSummary(outcome);
    save();
    panelDirty_ = true;
}

void RocketGameApp::miningAbort()
{
    if (state_.screen != Screen::Mining || surfaceBaySequence_.active() ||
        miningSceneHandoff_ != MiningSceneHandoff::None) {
        return;
    }
    if (miningAtReturnZone(state_.run.mining) && !state_.run.mining.failurePending) {
        state_.statusLine = "Leave is available inside the ship zone.";
        panelDirty_ = true;
        return;
    }

    queueMiningSceneHandoff(MiningSceneHandoff::AbortMining);
}

void RocketGameApp::miningFailureAck()
{
    if (state_.screen != Screen::Mining ||
        !state_.run.mining.failurePending ||
        !miningEvaDeathModalReady()) {
        return;
    }

    // The shared modal focus scope activates this recovery action for every
    // input source. DOM fallback dispatches before closing its modal, while
    // native RmlUi closes before dispatch, so tolerate either lifecycle order.
    if (services_.ui.modalOpen()) {
        services_.ui.closeModal();
    }
    if (pauseReason_ == PauseReason::BlockingModal) {
        clearControllerPause();
    }

    queueMiningSceneHandoff(MiningSceneHandoff::AbortMining);
}

void RocketGameApp::debugStartMining()
{
    debugStartMiningArena(1, 7, 0xA17E5701ULL, 0);
}

void RocketGameApp::debugStartCombatMining()
{
    debugStartMiningArena(2, 7, 0xC0BA7701ULL, 0);
}

void RocketGameApp::debugStartSwarmArena()
{
    constexpr int act = 2;
    constexpr int difficulty = 5;
    constexpr std::uint64_t maximumSeedAttempts = 4096;
    for (std::uint64_t seed = 1; seed <= maximumSeedAttempts; ++seed) {
        debugStartMiningArena(act, difficulty, seed, 0);
        if (!state_.run.mining.swarm.enabled) {
            continue;
        }
        if (enterMiningSwarmArenaForDebug(state_, catalog_)) {
            state_.statusLine = "Swarm Arena Lab: wave 1 active. Sandbox rewards are not saved.";
        } else {
            state_.statusLine = "Swarm Arena Lab could not enter the generated nest layer.";
        }
        panelDirty_ = true;
        return;
    }
    state_.statusLine = "Swarm Arena Lab could not find an eligible deterministic seed.";
    panelDirty_ = true;
}

void RocketGameApp::debugStartMiningArena(
    int act,
    int difficulty,
    std::uint64_t seed,
    int loadoutMode,
    int gateOverride,
    int destinationTierOverride,
    int postSolarSystemOverride,
    int bodyIndex)
{
    const MiningAct miningAct = act <= 1
        ? MiningAct::ActOne
        : (act == 2 ? MiningAct::ActTwo : MiningAct::ActThree);
    const MiningArenaRequest request {
        miningAct,
        std::clamp(difficulty, 1, 10),
        std::max<std::uint64_t>(1, seed),
        gateOverride >= 0,
        static_cast<MiningGateType>(std::clamp(gateOverride, 0, static_cast<int>(MiningGateType::CompoundVault)))
    };
    const MiningArenaRules rules = resolveMiningArenaRules(request);

    beginDebugSandbox("Mining Arena Lab sandbox. No payload, materials, or save data will be written.");
    state_.seed = request.seed;

    std::string_view destinationId = content::destination::moon;
    if (miningAct == MiningAct::ActOne) {
        if (request.difficulty >= 7) {
            destinationId = content::destination::neptune;
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

    if (destinationTierOverride >= 1 && destinationTierOverride <= 8) {
        constexpr std::array<std::string_view, 8> destinationsByTier {{
            content::destination::moon,
            content::destination::mars,
            content::destination::jupiter,
            content::destination::saturn,
            content::destination::uranus,
            content::destination::neptune,
            content::destination::nearbyStar,
            content::destination::nearbyGalaxy,
        }};
        destinationId = destinationsByTier[static_cast<std::size_t>(destinationTierOverride - 1)];
    }

    std::string_view debugPostSolarSystemId;
    if (postSolarSystemOverride == 1) {
        debugPostSolarSystemId = content::postSolarSystem::aaruVale;
        destinationId = content::destination::neptune;
    } else if (postSolarSystemOverride == 2) {
        debugPostSolarSystemId = content::postSolarSystem::khepriPrime;
        destinationId = content::destination::nearbyStar;
    } else if (postSolarSystemOverride == 3) {
        debugPostSolarSystemId = content::postSolarSystem::riftBelt;
        destinationId = content::destination::nearbyGalaxy;
    }

    state_.run.destinationIndex = destinationIndexForId(catalog_, destinationId);
    PlanetaryExpeditionState& expedition = state_.run.planetaryExpedition;
    expedition = {};
    expedition.active = true;
    expedition.destinationId = std::string(destinationId);
    if (!debugPostSolarSystemId.empty()) {
        expedition.postSolarSystemId = debugPostSolarSystemId;
        PostSolarSystemRoster& roster = ensurePostSolarSystemRoster(
            state_.meta, debugPostSolarSystemId, request.seed);
        std::vector<const PostSolarBodyProfile*> mineableBodies;
        for (const PostSolarBodyProfile& body : roster.bodies) {
            if (body.mineable) mineableBodies.push_back(&body);
        }
        if (!mineableBodies.empty()) {
            const std::size_t selected = static_cast<std::size_t>(std::clamp(
                bodyIndex, 0, static_cast<int>(mineableBodies.size()) - 1));
            expedition.bodyId = mineableBodies[selected]->id;
        }
    }
    expedition.siteProfile = rules.band == MiningProgressionBand::Learn
        ? SurfaceSiteProfile::SurveyBasin
        : (rules.band == MiningProgressionBand::Combine ? SurfaceSiteProfile::OreShelf : SurfaceSiteProfile::FractureField);
    expedition.supply = tuning::research::baseSupply + act;
    expedition.expeditionPackFuel = tuning::research::expeditionRigPackFuel;
    expedition.transferFuelRecovered = 5.0;
    expedition.rigFuelCapacity = expedition.expeditionPackFuel + expedition.transferFuelRecovered;
    expedition.rigFuel = expedition.rigFuelCapacity;
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
            const auto rank = std::find_if(
                expedition.runDroneRanks.begin(),
                expedition.runDroneRanks.end(),
                [&](const RunDroneRank& record) { return record.droneId == drone->id; });
            if (rank != expedition.runDroneRanks.end()) {
                rank->rank = std::max(1, rules.referenceDrones.maximumMark);
            } else if (rules.referenceDrones.maximumMark > 1) {
                expedition.runDroneRanks.push_back({drone->id, rules.referenceDrones.maximumMark});
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

std::string RocketGameApp::debugPostSolarBodyPreview(
    int postSolarSystemOverride,
    int bodyIndex,
    std::uint64_t seed) const
{
    std::string_view systemId;
    if (postSolarSystemOverride == 1) systemId = content::postSolarSystem::aaruVale;
    if (postSolarSystemOverride == 2) systemId = content::postSolarSystem::khepriPrime;
    if (postSolarSystemOverride == 3) systemId = content::postSolarSystem::riftBelt;
    if (systemId.empty()) return {};
    const PostSolarSystemRoster roster = generatePostSolarSystemRoster(
        systemId, std::max<std::uint64_t>(1, seed));
    std::vector<const PostSolarBodyProfile*> mineableBodies;
    for (const PostSolarBodyProfile& body : roster.bodies) {
        if (body.mineable) mineableBodies.push_back(&body);
    }
    if (mineableBodies.empty()) return "No mineable bodies generated.";
    const int selected = std::clamp(bodyIndex, 0, static_cast<int>(mineableBodies.size()) - 1);
    const PostSolarBodyProfile& body = *mineableBodies[static_cast<std::size_t>(selected)];
    const PostSolarGeologyProfile* surface = findPostSolarGeology(body.surfaceGeologyId);
    const PostSolarGeologyProfile* deep = findPostSolarGeology(body.deepGeologyId);
    return body.name + "  •  body " + std::to_string(selected + 1) + "/"
        + std::to_string(mineableBodies.size()) + "  •  portrait "
        + std::to_string(body.visualArchetype) + "\nSurface: "
        + (surface != nullptr ? std::string(surface->name) : body.surfaceGeologyId)
        + "  •  Deep: "
        + (deep != nullptr ? std::string(deep->name) : body.deepGeologyId);
}

std::string RocketGameApp::debugMiningArenaPreview(int act, int difficulty, int gateOverride) const
{
    const MiningAct miningAct = act <= 1
        ? MiningAct::ActOne
        : (act == 2 ? MiningAct::ActTwo : MiningAct::ActThree);
    const MiningArenaRules rules = resolveMiningArenaRules({
        miningAct,
        std::clamp(difficulty, 1, 10),
        1,
        gateOverride >= 0,
        static_cast<MiningGateType>(std::clamp(gateOverride, 0, static_cast<int>(MiningGateType::CompoundVault)))
    });
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
    const MiningGateType gateType = selectMiningGateType(rules);
    const MiningGateDefinition gate = resolveMiningGateDefinition(rules, gateType, false);
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
            << "\nGate: " << gate.name
            << "\nRequired: " << gate.requiredCapability
            << "\nAlternatives: " << gate.alternatives
            << "\nRich cap: " << rules.rewardBudget.rareCap << " rare • "
            << rules.rewardBudget.exoticCap << " exotic"
            << "\nCounter: " << rules.recommendedCounters;
    return preview.str();
}

void RocketGameApp::debugShowTitle()
{
    debugSessionActive_ = false;
    debugDroneLoadout_ = {};
    loadSavedGameOrDefault(true);
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

void RocketGameApp::debugShowJupiterOptions(int mode)
{
    beginDebugSandbox("Debug Jupiter options. No save data will be written.");
    const int combination = std::clamp(mode, 0, 5);
    const bool tanksInstalled = combination == 1 || combination == 3 || combination == 5;
    const bool slingshotActive = combination >= 2;
    const bool goodSlingshot = combination == 2 || combination == 3;
    state_.run.destinationIndex = destinationIndexForId(catalog_, content::destination::mars);
    state_.meta.furthestTier = 2;
    state_.meta.launchLessons.stage = LaunchTrainingStage::HullIntegrity;
    state_.meta.launchUpgrades.fuelTanks = tanksInstalled ? 3 : 2;
    addDebugUnlock(state_, content::unlock::routeMars);
    addDebugUnlock(state_, content::unlock::routeJupiter);
    if (ScenarioInstance* marsScenario = findScenarioInstance(
            state_.meta,
            content::scenario::marsBayExpansion)) {
        for (std::string_view completedStep : {std::string_view("briefing"), std::string_view("delivery")}) {
            if (ScenarioStepProgress* progress = findScenarioStepProgress(*marsScenario, completedStep)) {
                progress->briefingAcknowledged = true;
                progress->completed = true;
                progress->claimed = true;
            }
        }
        if (ScenarioStepProgress* funding = findScenarioStepProgress(*marsScenario, "funding")) {
            funding->briefingAcknowledged = slingshotActive;
            funding->completed = slingshotActive;
        }
    }
    state_.meta.marsMiningBriefingAcknowledged = true;
    state_.meta.marsBayExpansionClaimed = true;
    state_.run.pendingTransferAssist = slingshotActive
        ? PendingTransferAssist {
            content::transferAssist::marsJupiter,
            content::destination::mars,
            content::destination::jupiter,
            goodSlingshot ? FlybyGrade::Good : FlybyGrade::Perfect,
            tuning::flyby::jupiterSlingshotFuelSavings,
            tuning::flyby::slingshotSpeedBoost *
                (goodSlingshot ? 1.0 : tuning::flyby::slingshotMaxSpeedScale),
            goodSlingshot ? tuning::flyby::jupiterSlingshotGoodInstabilityPenalty : 0.0,
            goodSlingshot
                ? 0.50
                : tuning::launch::pilotingCourseSafe * 0.50 }
        : PendingTransferAssist {};
    state_.run.nextLaunchFuelBoost = 0.0;
    state_.run.nextLaunchSpeedBoost = 0.0;
    state_.run.nextLaunchInstabilityPenalty = 0.0;
    state_.run.refitEntitled = true;
    state_.run.credits = 92.0;
    state_.screen = Screen::Hangar;
    static constexpr std::array<std::string_view, 6> labels {
        "Neither path",
        "Fuel Tanks III",
        "Good Mars slingshot",
        "Fuel Tanks III plus Good Mars slingshot",
        "Perfect Mars slingshot",
        "Fuel Tanks III plus Perfect Mars slingshot"
    };
    state_.statusLine = "Debug Jupiter readiness: " +
        std::string(labels[static_cast<std::size_t>(combination)]) +
        ". Real save remains untouched.";
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

void RocketGameApp::debugShowArrivalCelebration()
{
    beginDebugSandbox("Debug automatic arrival celebration. No save data will be written.");
    state_.run.destinationIndex = destinationIndexForId(catalog_, content::destination::mars);
    state_.lastOutcome = debugTransferOutcome(content::destination::mars);
    startArrivalOps(state_, state_.lastOutcome);
    session_.flight.physicalFlight = true;
    session_.flight.phase = FlightPhase::Landed;
    beginArrivalFanfare();
    syncLaunchConfig(state_, catalog_);
    panelDirty_ = true;
}

void RocketGameApp::debugShowResearch()
{
    beginDebugSandbox("Debug Research board. No save data will be written.");
    state_.run.destinationIndex = destinationIndexForId(catalog_, content::destination::mars);
    state_.run.approach = {true, content::destination::mars};
    seedDebugResearchAccess(state_);
    generateResearchProjects(state_, catalog_, rng_);
    state_.screen = Screen::Research;
    state_.statusLine = "Debug Research board. Inspect project cards without touching your save.";
    syncLaunchConfig(state_, catalog_);
    panelDirty_ = true;
}

void RocketGameApp::debugShowRefit()
{
    beginDebugSandbox("Debug Refit board. No save data will be written.");
    state_.meta.launchLessons.stage = LaunchTrainingStage::Complete;
    seedDebugResearchAccess(state_);
    state_.run.refitEntitled = true;
    state_.run.credits = std::max(state_.run.credits, 100.0);
    beginRefitVisit(state_);
    generateModuleOffers(state_, catalog_, rng_);
    selectedRefitOfferIndex_ = 0;
    state_.screen = Screen::Upgrade;
    state_.statusLine = "Debug Refit board. Inspect and navigate permanent offers without touching your save.";
    syncLaunchConfig(state_, catalog_);
    panelDirty_ = true;
}

void RocketGameApp::debugShowSurfaceUpgrade()
{
    beginDebugSandbox("Debug Surface Upgrade board. No save data will be written.");
    seedDebugResearchAccess(state_);
    seedDebugSurfaceExpedition(state_, catalog_, rng_, content::destination::mars);
    (void)awardExpeditionExperience(state_, 10.0, Screen::Mining);
    generateRunUpgradeOffers(state_, catalog_, rng_);
    state_.screen = Screen::SurfaceUpgrade;
    state_.run.planetaryExpedition.runUpgradeReturnScreen = Screen::Mining;
    state_.statusLine = "Debug Level Up board. Inspect draft cards without touching your save.";
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
    state_.statusLine = "Debug Drone Ops. All 6 slots and Support Drone types are available; this loadout carries into Mining and Combat Mining.";
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

void RocketGameApp::debugStartLaunchLesson(int lessonIndex)
{
    if (lessonIndex < 0 || lessonIndex > 3) {
        return;
    }

    static constexpr std::array<std::string_view, 4> lessonLabels {
        "Fuel Survey",
        "Flight Controls Calibration",
        "Thermal Qualification",
        "Asteroid Belt Survey"};

    beginDebugSandbox("Debug launch lesson. No progress, rewards, or save data will be written.");
    state_.meta.campaignIntroductionAcknowledged = true;
    state_.meta.launchUpgrades = {};

    std::string_view currentDestinationId = content::destination::earthOrbit;
    switch (lessonIndex) {
    case 0:
        state_.meta.launchLessons.stage = LaunchTrainingStage::FuelCalibration;
        state_.meta.chapter = GameChapter::ProvingGround;
        state_.meta.furthestTier = 0;
        break;
    case 1:
        state_.meta.launchLessons.stage = LaunchTrainingStage::FlightControlsCalibration;
        state_.meta.launchUpgrades.fuelTanks = 1;
        state_.meta.chapter = GameChapter::ProvingGround;
        state_.meta.furthestTier = 0;
        break;
    case 2:
        state_.meta.launchLessons.stage = LaunchTrainingStage::ThermalManagement;
        state_.meta.launchUpgrades.fuelTanks = 2;
        state_.meta.launchUpgrades.flightControls = 1;
        state_.meta.chapter = GameChapter::LunarProgram;
        state_.meta.furthestTier = 1;
        currentDestinationId = content::destination::moon;
        addDebugUnlock(state_, content::unlock::routeMars);
        break;
    case 3:
        state_.meta.launchLessons.stage = LaunchTrainingStage::HullIntegrity;
        state_.meta.launchUpgrades.fuelTanks = 3;
        state_.meta.launchUpgrades.flightControls = 1;
        state_.meta.chapter = GameChapter::RedFrontier;
        state_.meta.furthestTier = 2;
        currentDestinationId = content::destination::mars;
        addDebugUnlock(state_, content::unlock::routeMars);
        addDebugUnlock(state_, content::unlock::routeJupiter);
        break;
    }

    state_.run.destinationIndex = destinationIndexForId(catalog_, currentDestinationId);
    syncLaunchConfig(state_, catalog_);
    beginLaunchSession(rocket::prepareLaunch(state_, catalog_, rng_));
    session_.flightArmed = true;
    session_.flight.active = true;
    state_.screen = Screen::Flight;
    state_.statusLine = "Debug launch lesson " + std::to_string(lessonIndex + 1) +
        "/4: " + std::string(lessonLabels[static_cast<std::size_t>(lessonIndex)]) +
        ". Real save remains untouched.";
    panelDirty_ = true;
    realtimeHudDirty_ = true;
}

void RocketGameApp::debugStartSurfaceArrival(int destinationIndex, int phaseIndex)
{
    const bool mars = destinationIndex == 1;
    if ((destinationIndex != 0 && !mars) || phaseIndex < 0 || phaseIndex > 20) {
        return;
    }

    debugStartLaunchLesson(mars ? 3 : 2);
    // Arrival choreography needs an equipped team to make the deterministic
    // drone fan independently inspectable. This is debug-sandbox state only;
    // real landings continue to deploy exactly the player's equipped drones.
    seedDebugDroneLoadout();
    state_.meta.droneBaySlots = 3;
    state_.meta.equippedDroneIds = {
        content::drone::miningDrone,
        content::drone::resourceDrone,
        content::drone::surveyDrone
    };
    ensureDroneBayState(state_, catalog_);
    state_.launchConfig.destinationId = mars ? content::destination::mars : content::destination::moon;
    state_.launchConfig.routeTransit = {};
    state_.launchConfig.frontierTransfer = true;
    state_.launchConfig.missionKind = LaunchMissionKind::Standard;
    beginLaunchSession(rocket::prepareLaunch(state_, catalog_, rng_));
    session_.flightArmed = true;
    FlightRunState& flight = session_.flight;
    flight.active = true;
    flight.physicalFlight = true;
    flight.phase = FlightPhase::Landing;
    flight.positionX = 0.0;
    flight.positionY = 0.235;
    flight.velocityX = 0.0;
    flight.velocityY = -0.055;
    flight.heading = 1.5707963267948966;
    flight.angularVelocity = 0.0;
    flight.travelProgress = 1.0;
    flight.previousTravelProgress = 1.0;
    flight.orbit.enteredInfluence = true;
    flight.orbit.captured = true;
    flight.orbit.grade = OrbitGrade::Good;
    enterLocalLanding(flight);
    flight.handoff.elapsed = flight_landing::handoffSeconds;
    flight.landing.altitude = 7.5;
    flight.landing.verticalVelocity = -1.43;
    flight.landing.lateralVelocity = 0.0;
    flight.landing.surfaceAngle = 0.0;
    if (phaseIndex == 9) {
        // A manual feel preset just after an overcorrection: already in the
        // landing frame, drifting sideways and climbing above its entry.
        flight.landing.altitude = 35.0;
        flight.landing.verticalVelocity = 2.0;
        flight.landing.lateralVelocity = 3.0;
    }
    if (phaseIndex == 0 || phaseIndex == 10 || phaseIndex == 11) {
        flight.landing.altitude=60.0;
        flight.landing.verticalVelocity=phaseIndex==11 ? -24.0 : -4.0;
        flight.landing.heading=phaseIndex==10 ? -1.5707963267948966 : 1.5707963267948966;
    }
    if (phaseIndex==12) {flight.landing.altitude=2.0;flight.landing.verticalVelocity=-2.0;}
    if (phaseIndex==13) {flight.landing.altitude=116.0;flight.landing.verticalVelocity=8.0;}
    if (phaseIndex>=16) {
        flight.hullRemaining=phaseIndex==17 ? 40.0 : 85.0;
        flight.landing.altitude=0.001;
        flight.landing.verticalVelocity=-std::sqrt(18.0*18.0-2.0*flight_landing::gravityAcceleration*0.001);
        if (phaseIndex==18) {
            flight.landing.heading=1.10;
            flight.landing.altitude=1.5;
            flight.landing.verticalVelocity=-2.0;
            flight.landing.lateralVelocity=3.0;
        }
        if (phaseIndex==19) {flight.landing.verticalVelocity=-0.1;}
        if (phaseIndex==20) {
            flight.mode=FlightMode::Orbit;flight.phase=FlightPhase::TargetApproach;
            flight.orbit.captured=false;flight.orbitZoomProgress=1.0;
            flight.handoff={FlightMode::Orbit,FlightMode::Orbit,flight_landing::handoffSeconds,0.0,0.0,0.0};
            flight.positionX=flight_geometry::bodyRadius+0.001;flight.positionY=0.0;
            flight.velocityX=-18.0/flight_geometry::velocityToMetersPerSecond;flight.velocityY=0.0;
            flight.heading=0.0;
        }
    }
    if (phaseIndex==14 || phaseIndex==15) {
        // Exercise the actual swept gate and basis conversion rather than
        // starting inside the local phase. The fast case is never clamped.
        const double angle=std::atan2(flight_geometry::startY,flight_geometry::startX);
        const double nx=std::cos(angle),ny=std::sin(angle);
        const double inward=(phaseIndex==15 ? -30.0 : -5.0)/flight_landing::velocityConversion;
        const double sideways=4.0/flight_landing::velocityConversion;
        flight.mode=FlightMode::Orbit;
        flight.phase=FlightPhase::Orbiting;
        flight.orbitZoomProgress=1.0;
        flight.handoff={FlightMode::Orbit,FlightMode::Orbit,flight_landing::handoffSeconds,0.0,0.0,angle};
        flight.positionX=nx*(flight_geometry::landingBoundary+0.015);
        flight.positionY=ny*(flight_geometry::landingBoundary+0.015);
        flight.velocityX=nx*inward+ny*sideways;
        flight.velocityY=ny*inward-nx*sideways;
        flight.heading=angle;
        flight.orbit.previousAngle=angle;
        flight.landing.gateArmed=true;
    }
    prepareSurfaceArrivalIfNeeded(currentDestination(state_, catalog_));

    if (surfaceArrival_.prepared && surfaceArrival_.prepared->valid) {
        bindLandingSite(flight,surfaceArrival_.prepared->miningTemplate);
        flight.landing.touchdownGridX=flight.landing.padGridX;
        flight.landing.touchdownGridY=flight.landing.padGridY;
    }

    if (phaseIndex == 0 || phaseIndex >= 9) {
        state_.statusLine = phaseIndex == 9
            ? "Landing correction trial. Rising with sideways drift; real save untouched."
            : mars
            ? "Debug Mars final approach. Surface site is prepared but uncommitted."
            : "Debug Moon final approach. Surface site is prepared but uncommitted.";
        panelDirty_ = true;
        realtimeHudDirty_ = true;
        return;
    }

    flight.active = false;
    flight.phase = FlightPhase::Landed;
    flight.positionY = flight_geometry::bodyRadius;
    flight.landing.altitude = 0.0;
    flight.velocityX = 0.0;
    flight.velocityY = 0.0;
    const bool hardTouchdown = phaseIndex == 2;
    flight.landing.hardLanding = hardTouchdown;
    if (!commitSurfaceTouchdown(currentDestination(state_, catalog_), hardTouchdown)) {
        return;
    }
    if (phaseIndex <= 2) {
        surfaceArrival_.elapsed = 0.18;
    } else if (phaseIndex == 3) {
        surfaceArrival_.phase = SurfaceArrivalPhase::AwaitingCommand;
        surfaceArrival_.elapsed = 0.0;
        state_.statusLine = "LANDED";
    } else if (phaseIndex == 8) {
        surfaceArrival_.phase = SurfaceArrivalPhase::AwaitingCommand;
        surfaceArrival_.elapsed = 0.0;
        departSurfaceUndeployed();
    } else {
        surfaceArrival_.phase = SurfaceArrivalPhase::AwaitingCommand;
        beginSurfaceDeploymentSequence();
        static constexpr std::array<double, 4> deploymentTimes {0.10, 0.68, 1.32, 2.18};
        const double elapsed = deploymentTimes[static_cast<std::size_t>(phaseIndex - 4)];
        surfaceArrival_.elapsed = elapsed;
        surfaceBaySequence_.elapsed = elapsed;
    }
    panelDirty_ = true;
    realtimeHudDirty_ = true;
}

void RocketGameApp::applyDebugActOneCheckpoint()
{
    debugActOneCheckpoint_ = std::clamp(
        debugActOneCheckpoint_,
        0,
        static_cast<int>(kDebugActOneCheckpoints.size()) - 1);
    const DebugActOneCheckpoint& checkpoint = kDebugActOneCheckpoints[static_cast<std::size_t>(debugActOneCheckpoint_)];

    session_.reset();
    levelUp_ = {};
    expeditionXpPulseSeconds_ = 0.0;
    expeditionXpObservationInitialized_ = false;
    clearResearchAndExpeditionState(state_);
    state_.lastOutcome = debugActOneCheckpoint_ == 0
        ? LaunchOutcome{}
        : debugTransferOutcome(std::string(checkpoint.destinationId));
    state_.run.destinationIndex = destinationIndexForId(catalog_, checkpoint.destinationId);
    state_.meta.furthestTier = currentDestination(state_, catalog_).tier;
    state_.meta.ark = {};
    state_.meta.campaignMilestone = CampaignMilestone::SolarTutorial;
    state_.meta.campaignIntroductionAcknowledged = true;
    state_.meta.straylightDiscoveryAcknowledged = false;
    state_.storyBriefing = {};
    state_.meta.chapter = debugActOneCheckpoint_ == 0
        ? GameChapter::ProvingGround
        : (debugActOneCheckpoint_ == 1
            ? GameChapter::LunarProgram
            : (debugActOneCheckpoint_ == 2 ? GameChapter::RedFrontier : GameChapter::Breakthrough));

    if (debugActOneCheckpoint_ == 0) {
        state_.launchConfig.frontierTransfer = false;
        state_.launchConfig.destinationId = std::string(checkpoint.destinationId);
        state_.launchConfig.burnGoalMultiplier = defaultProvingTarget(currentDestination(state_, catalog_));
        syncLaunchConfig(state_, catalog_);
        beginLaunchSession(rocket::prepareLaunch(state_, catalog_, rng_));
        session_.flightArmed = true;
        session_.flight.active = true;
        state_.screen = Screen::Flight;
    } else {
        setDestinationHistory(state_.meta.destinationFlybys, catalog_, checkpoint.destinationId, 1);
        setDestinationHistory(state_.meta.destinationOrbits, catalog_, checkpoint.destinationId, 1);
        startArrivalOps(state_, state_.lastOutcome);
        if (checkpoint.destinationId == std::string_view(content::destination::neptune)) {
            scheduleStoryBriefing(state_, StoryBriefingId::StraylightDiscovery, Screen::Hangar);
            state_.screen = Screen::StoryBriefing;
        } else {
            beginSurfaceExpeditionOrRefit();
        }
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
    loadSavedGameOrDefault(false);
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

    syncLaunchConfig(state_, catalog_);
    const bool queuedRouteTransit = state_.run.routeTransit.active() &&
        routeLinkForTransit(catalog_, state_.run.routeTransit) != nullptr;
    if (!launchMissionReady(state_, catalog_)) {
        const Destination* next = nextDestination(state_, catalog_);
        state_.statusLine = next != nullptr && next->id == content::destination::jupiter
            ? "Create 5 fuel of Jupiter margin with Fuel Tanks III, a Good-or-better Mars slingshot, or both."
            : "Install the required launch upgrade before this route attempt.";
        refreshPanel();
        return;
    }

    const bool qualificationFlight =
        state_.meta.launchLessons.stage != LaunchTrainingStage::Complete &&
        !state_.launchConfig.frontierTransfer;
    if (!queuedRouteTransit && !qualificationFlight && !canCommitToNextFrontier(state_, catalog_)) {
        const Destination* next = nextDestination(state_, catalog_);
        state_.statusLine = next == nullptr ? std::string(text::status::noFartherFrontier) : std::string(text::status::moreProvingDataBeforeTransfer);
        refreshPanel();
        return;
    }

    const Destination* next = queuedRouteTransit
        ? catalog_.findDestination(state_.run.routeTransit.targetDestinationId)
        : nextDestination(state_, catalog_);
    if (next == nullptr) {
        state_.statusLine = std::string(text::status::noFartherFrontier);
        refreshPanel();
        return;
    }

    // Every genuine interplanetary attempt carries its physical origin. This
    // includes the guided Moon/Mars/Jupiter curriculum; campaign frontier is
    // progress only and must never be used as a substitute for ship position.
    if (!queuedRouteTransit &&
        (state_.launchConfig.frontierTransfer ||
         state_.meta.launchLessons.stage == LaunchTrainingStage::Complete)) {
        state_.run.routeTransit = makeRouteTransit(
            catalog_,
            currentDestination(state_, catalog_).id,
            next->id,
            RouteTransitIntent::Outbound);
        syncLaunchConfig(state_, catalog_);
    }

    if (state_.meta.launchLessons.stage == LaunchTrainingStage::Complete && !queuedRouteTransit) {
        state_.launchConfig.frontierTransfer = true;
        state_.launchConfig.destinationId = next->id;
        state_.launchConfig.burnGoalMultiplier = next->targetMultiplier;
    }
    beginLaunchSession(rocket::prepareLaunch(state_, catalog_, rng_));
    consumeNextLaunchBoost();
    state_.screen = Screen::Flight;
    state_.statusLine = miningDroneTransferEnabled(state_)
        ? std::string(text::status::droneStowing)
        : std::string(text::status::preflightReadyWithoutDrone);
    save();
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

void RocketGameApp::selectRefitOffer(int index)
{
    if (state_.screen == Screen::Upgrade && index >= 0) {
        selectedRefitOfferIndex_ = index;
        const RefitWindowPresentation refitWindow = refitWindowPresentation(state_, catalog_);
        if (static_cast<std::size_t>(index) < refitWindow.offers.size() &&
            refitWindow.offers[static_cast<std::size_t>(index)].affordable) {
            services_.ui.requestFocus(
                "action:" + refitWindow.offers[static_cast<std::size_t>(index)].action.actionId);
        }
        panelDirty_ = true;
    }
}

void RocketGameApp::buyOffer(int index)
{
    if (state_.screen != Screen::Upgrade) {
        return;
    }
    const ShipModule* selectedModule =
        index >= 0 && index < static_cast<int>(state_.run.offerModuleIds.size())
        ? catalog_.findModule(
              state_.run.offerModuleIds[static_cast<std::size_t>(index)])
        : nullptr;
    const bool permanentSurfacePurchase = selectedModule != nullptr &&
        (selectedModule->surfaceDepthUpgradeKind != SurfaceDepthUpgradeKind::None ||
         selectedModule->rigFuelLoopRank > 0);
    if (rocket::buyOffer(state_, catalog_, index)) {
        selectedRefitOfferIndex_ = 0;
        if (permanentSurfacePurchase &&
            !refitWindowPresentation(state_, catalog_).offers.empty()) {
            state_.screen = Screen::Upgrade;
        } else {
            state_.run.refitEntitled = false;
            state_.screen = navigationAvailable(state_) ? Screen::Navigation : Screen::Hangar;
        }
        save();
    }
    panelDirty_ = true;
}

void RocketGameApp::rerollOffers()
{
    if (state_.screen == Screen::Upgrade) {
        if (rocket::rerollOffers(state_, catalog_, rng_)) {
            selectedRefitOfferIndex_ = 0;
            save();
        }
        panelDirty_ = true;
        return;
    }

    // Expedition Level Up choices are mandatory and persisted. Their pool has
    // no paid reroll path and does not share Refit's economy counter.
    panelDirty_ = true;
}

void RocketGameApp::repairShip()
{
    if (rocket::repairShip(state_)) {
        save();
    }
    panelDirty_ = true;
}

void RocketGameApp::resetSave()
{
    debugSessionActive_ = false;
    if (!services_.saves.clear()) {
        services_.host.log(PlatformLogLevel::Error, services_.saves.lastError());
        if (titleScreenActive_) {
            titleNotice_ = "Unable to clear the local save. Existing progress was preserved.";
        } else {
            state_.statusLine = "Save reset failed; the previous native save was preserved.";
        }
        panelDirty_ = true;
        return;
    }
    if (!services_.saves.clearCheckpoint()) {
        services_.host.log(PlatformLogLevel::Error, services_.saves.lastError());
    }
    hasSavedGame_ = false;
    checkpointRecoveryAvailable_ = false;
    titleNotice_ = titleScreenActive_ ? "Local campaign save cleared." : std::string();
    state_ = createNewGame(catalog_, 0x524F434B45544ULL);
    rng_ = Random(state_.seed);
    session_.reset();
    levelUp_ = {};
    expeditionXpPulseSeconds_ = 0.0;
    expeditionXpObservationInitialized_ = false;
    session_.returnTrip.duration = tuning::session::returnDefaultDuration;
    disableDebugToolsForFreshCampaign();
    refreshPanel();
}

void RocketGameApp::newGame()
{
    if (!titleScreenActive_ || titleLaunchActive_ || sceneTransition_.active()) {
        return;
    }

    beginTitleLaunch(true);
}

void RocketGameApp::startNewGame()
{
    // A confirmed new campaign must never retain a recovery path into the
    // campaign it deliberately replaced.
    if (!services_.saves.clearCheckpoint()) {
        services_.host.log(PlatformLogLevel::Error, services_.saves.lastError());
    }
    GameState freshState = createNewGame(catalog_, 0x524F434B45544ULL);
    ensureDroneBayState(freshState, catalog_);
    syncLaunchConfig(freshState, catalog_);
    scheduleStoryBriefing(freshState, StoryBriefingId::CampaignIntroduction, Screen::Hangar);
    freshState.screen = Screen::StoryBriefing;
    const std::string initialSave = serializeSaveData(captureSaveData(freshState));
    const bool stored = services_.saves.storeAtomic(initialSave);
    if (!stored) {
        services_.host.log(PlatformLogLevel::Error, services_.saves.lastError());
        if (hasSavedGame_) {
            titleNotice_ = "New campaign could not replace the existing save. Your progress is still intact.";
            refreshPanel();
            return;
        }
        freshState.statusLine = "New campaign started, but local save initialization failed.";
    }

    debugSessionActive_ = false;
    debugActOneCheckpoint_ = -1;
    state_ = std::move(freshState);
    rng_ = Random(state_.seed);
    session_.reset();
    session_.returnTrip.duration = tuning::session::returnDefaultDuration;
    surfaceBaySequence_.reset();
    miningEvaDeathPresentation_ = {};
    levelUp_ = {};
    expeditionXpPulseSeconds_ = 0.0;
    expeditionXpObservationInitialized_ = false;
    releaseRealtimeInputs(true);
    disableDebugToolsForFreshCampaign();
    hasSavedGame_ = stored;
    checkpointRecoveryAvailable_ = false;
    titleScreenActive_ = false;
    titleNotice_.clear();
    refreshPanel();
}

void RocketGameApp::disableDebugToolsForFreshCampaign()
{
    AppPreferences preferences = services_.preferences.load();
    if (!preferences.debugToolsEnabled) {
        return;
    }

    preferences.debugToolsEnabled = false;
    if (!services_.preferences.store(preferences)) {
        services_.host.log(PlatformLogLevel::Error, services_.preferences.lastError());
        return;
    }

    // The preference controls the web overlay as well as native UI
    // presentation, so update both immediately rather than waiting a frame.
    services_.renderer.setPreferences(preferences);
    services_.uiBridge.preferencesChanged(preferences);
}

void RocketGameApp::continueGame()
{
    if (!titleScreenActive_ || !hasSavedGame_ || titleLaunchActive_ || sceneTransition_.active()) {
        return;
    }
    beginTitleLaunch(false);
}

bool RocketGameApp::restoreCheckpoint()
{
    if (!titleScreenActive_ || !checkpointRecoveryAvailable_ ||
        titleLaunchActive_ || sceneTransition_.active()) {
        return false;
    }
    const auto checkpoint = deserializeSaveData(services_.saves.loadCheckpoint());
    if (!checkpoint) {
        checkpointRecoveryAvailable_ = false;
        titleNotice_ = "ROUTE CONTROL // CHECKPOINT UNREADABLE. Begin a new campaign.";
        panelDirty_ = true;
        return false;
    }
    GameState restored = createNewGame(catalog_, checkpoint->seed);
    restoreSaveData(restored, catalog_, *checkpoint);
    const CampaignProgressionAuditResult audit = auditCampaignProgression(restored, catalog_);
    if (!audit.valid) {
        checkpointRecoveryAvailable_ = false;
        titleNotice_ = "ROUTE CONTROL // CHECKPOINT INVALID. Begin a new campaign.";
        panelDirty_ = true;
        return false;
    }
    state_ = std::move(restored);
    rng_ = Random(checkpoint->seed + 0xA51CE5ULL + static_cast<std::uint64_t>(checkpoint->blueprintProgress));
    session_.reset();
    surfaceBaySequence_.reset();
    miningEvaDeathPresentation_ = {};
    levelUp_ = {};
    hasSavedGame_ = true;
    checkpointRecoveryAvailable_ = false;
    titleScreenActive_ = false;
    titleNotice_.clear();
    state_.statusLine = "ROUTE CONTROL // CHECKPOINT RESTORED.";
    if (!services_.saves.storeAtomic(serializeSaveData(captureSaveData(state_)))) {
        services_.host.log(PlatformLogLevel::Error, services_.saves.lastError());
        state_.statusLine = "Checkpoint restored in memory, but active save storage failed.";
    }
    panelDirty_ = true;
    return true;
}

bool RocketGameApp::stateCanBecomeCheckpoint() const
{
    switch (state_.screen) {
    case Screen::Hangar:
    case Screen::ArrivalOps:
    case Screen::DroneOps:
    case Screen::Research:
    case Screen::Navigation:
        return !state_.run.routeTransit.active() && state_.storyBriefing.pending == StoryBriefingId::None;
    case Screen::Flight:
    case Screen::Results:
    case Screen::ArrivalFanfare:
    case Screen::Flyby:
    case Screen::Orbit:
    case Screen::SurfaceExpedition:
    case Screen::SurfaceUpgrade:
    case Screen::SurfaceScan:
    case Screen::SurfacePush:
    case Screen::Mining:
    case Screen::Upgrade:
    case Screen::Legacy:
    case Screen::StoryBriefing:
        return false;
    }
    return false;
}

bool RocketGameApp::validateProgressionStateOrRestore(const GameState& previous, std::string_view action)
{
    const CampaignProgressionAuditResult audit = auditCampaignProgression(state_, catalog_);
    if (audit.valid) {
        return true;
    }
    state_ = previous;
    state_.statusLine = "ROUTE CONTROL // ACTION REJECTED. " + audit.detail;
    services_.host.log(PlatformLogLevel::Error,
        "Progression action rejected (" + std::string(action) + "): " + audit.detail);
    panelDirty_ = true;
    return false;
}

void RocketGameApp::beginTitleLaunch(bool newCampaign)
{
    titleLaunchActive_ = true;
    titleLaunchStartsNewCampaign_ = newCampaign;
    titleLaunchElapsedSeconds_ = 0.0;
    sceneTransition_.clear();
    releaseRealtimeInputs(true);
    refreshPanel();
}

void RocketGameApp::completeTitleLaunch()
{
    titleLaunchActive_ = false;
    beginSceneFadeToBlack(kSceneFadeToBlackSeconds);
    refreshPanel();
}

void RocketGameApp::finishTitleLaunch()
{
    const bool startsNewCampaign = titleLaunchStartsNewCampaign_;
    titleLaunchStartsNewCampaign_ = false;
    titleLaunchElapsedSeconds_ = 0.0;
    if (startsNewCampaign) {
        startNewGame();
        if (!titleScreenActive_) {
            beginSceneFadeFromBlack(kSceneFadeFromBlackSeconds);
            refreshPanel();
        }
        return;
    }
    titleScreenActive_ = false;
    titleNotice_.clear();
    releaseRealtimeInputs(true);
    beginSceneFadeFromBlack(kSceneFadeFromBlackSeconds);
    refreshPanel();
}

void RocketGameApp::beginSceneFadeToBlack(double durationSeconds)
{
    sceneTransition_.beginFadeToBlack(durationSeconds);
}

void RocketGameApp::beginSceneFadeFromBlack(double durationSeconds)
{
    sceneTransition_.beginFadeFromBlack(durationSeconds);
}

void RocketGameApp::beginMiningEvaDeathPresentation()
{
    if (miningEvaDeathPresentation_.phase !=
        MiningEvaDeathPresentationState::Phase::None) {
        return;
    }
    miningEvaDeathPresentation_.phase =
        MiningEvaDeathPresentationState::Phase::Impact;
    miningEvaDeathPresentation_.elapsed = 0.0;
    releaseRealtimeInputs(true);
    panelDirty_ = true;
}

bool RocketGameApp::advanceMiningEvaDeathPresentation(double deltaSeconds)
{
    using Phase = MiningEvaDeathPresentationState::Phase;
    const double dt = std::clamp(deltaSeconds, 0.0, 0.25);
    switch (miningEvaDeathPresentation_.phase) {
    case Phase::None:
    case Phase::Complete:
        return false;
    case Phase::Impact:
        miningEvaDeathPresentation_.elapsed = std::min(
            kMiningEvaDeathImpactSeconds,
            miningEvaDeathPresentation_.elapsed + dt);
        if (miningEvaDeathPresentation_.elapsed >=
            kMiningEvaDeathImpactSeconds) {
            miningEvaDeathPresentation_.phase = Phase::FadingOut;
            beginSceneFadeToBlack(kMiningEvaDeathFadeToBlackSeconds);
            refreshPanel();
            return true;
        }
        return false;
    case Phase::FadingOut:
        if (sceneTransition_.advance(dt)) {
            miningEvaDeathPresentation_.phase = Phase::FadingIn;
            beginSceneFadeFromBlack(kMiningEvaDeathFadeFromBlackSeconds);
        }
        refreshPanel();
        return true;
    case Phase::FadingIn:
        sceneTransition_.advance(dt);
        if (!sceneTransition_.active()) {
            miningEvaDeathPresentation_.phase = Phase::Complete;
        }
        refreshPanel();
        return true;
    }
    return false;
}

bool RocketGameApp::miningEvaDeathModalReady() const noexcept
{
    using Phase = MiningEvaDeathPresentationState::Phase;
    return miningEvaDeathPresentation_.phase == Phase::None ||
        miningEvaDeathPresentation_.phase == Phase::Complete;
}

void RocketGameApp::queueMiningSceneHandoff(MiningSceneHandoff handoff)
{
    if (handoff == MiningSceneHandoff::None ||
        miningSceneHandoff_ != MiningSceneHandoff::None ||
        sceneTransition_.active()) {
        return;
    }

    miningSceneHandoff_ = handoff;
    miningSceneHandoffCommitted_ = false;
    releaseRealtimeInputs(true);
    beginSceneFadeToBlack(kSceneFadeToBlackSeconds);
    refreshPanel();
}

bool RocketGameApp::advanceMiningSceneHandoff(double deltaSeconds)
{
    if (miningSceneHandoff_ == MiningSceneHandoff::None) {
        return false;
    }

    const bool blackoutReady = sceneTransition_.advance(std::clamp(deltaSeconds, 0.0, 0.25));
    if (!miningSceneHandoffCommitted_ && blackoutReady) {
        completeMiningSceneHandoff();
        miningSceneHandoffCommitted_ = true;
        beginSceneFadeFromBlack(kSceneFadeFromBlackSeconds);
    } else if (miningSceneHandoffCommitted_ && !sceneTransition_.active()) {
        miningSceneHandoff_ = MiningSceneHandoff::None;
        miningSceneHandoffCommitted_ = false;
    }

    refreshPanel();
    // This frame belonged to the handoff even when the fade-in just finished.
    // Do not immediately apply a full simulation step to the newly revealed
    // mining scene; doing so can pull the untouched rig out of the ship zone
    // before the player receives control.
    return true;
}

void RocketGameApp::completeMiningSceneHandoff()
{
    miningEvaDeathPresentation_ = {};
    switch (miningSceneHandoff_) {
    case MiningSceneHandoff::EnterMining:
        startMiningRunAfterFade();
        break;
    case MiningSceneHandoff::DepartPlanet:
        surfaceBaySequence_.reset();
        state_.run.mining = {};
        if (!openRefitIfAvailable()) {
            state_.screen = navigationAvailable(state_) ? Screen::Navigation : Screen::Hangar;
        }
        state_.statusLine = "Planetary departure complete.";
        save();
        panelDirty_ = true;
        break;
    case MiningSceneHandoff::AbortMining: {
        recordActiveMiningScenarioAbort(state_, catalog_);
        const SurfaceActionOutcome outcome = finishMiningRun(state_, catalog_, true);
        state_.statusLine = outcome.applied
            ? surfaceActionSummary(outcome)
            : std::string(text::status::miningAborted);
        save();
        panelDirty_ = true;
        break;
    }
    case MiningSceneHandoff::None:
        break;
    }
}

bool RocketGameApp::uiMouseMove(int x, int y)
{
    return services_.ui.mouseMove(x, y);
}

bool RocketGameApp::uiMouseDown(int x, int y, int button)
{
    return services_.ui.mouseDown(x, y, button);
}

bool RocketGameApp::uiMouseUp(int x, int y, int button)
{
    return services_.ui.mouseUp(x, y, button);
}

bool RocketGameApp::uiMouseWheel(int x, int y, double deltaY)
{
    return services_.ui.mouseWheel(x, y, deltaY);
}

bool RocketGameApp::uiHitTest(int x, int y) const
{
    return services_.ui.hitTest(x, y);
}

bool RocketGameApp::uiNavigate(UiDirection direction)
{
    const bool navigated = services_.ui.navigate(direction);
    if (!navigated || state_.screen != Screen::Upgrade) {
        return navigated;
    }

    int offerIndex = 0;
    const RefitWindowPresentation refitWindow = refitWindowPresentation(state_, catalog_);
    if (consumeIndexedAction(services_.ui.focusedId(), "refit-offer:", offerIndex) &&
        offerIndex >= 0 &&
        static_cast<std::size_t>(offerIndex) < refitWindow.offers.size() &&
        offerIndex != selectedRefitOfferIndex_) {
        selectedRefitOfferIndex_ = offerIndex;
        panelDirty_ = true;
    }
    return true;
}

bool RocketGameApp::uiActivateFocused()
{
    return services_.ui.activateFocused();
}

bool RocketGameApp::uiCancel()
{
    return services_.ui.cancel();
}

void RocketGameApp::beginFlightDestructionCinematic(LaunchFailureCause failureCause)
{
    if (session_.destruction.active) {
        return;
    }

    session_.destruction = {
        true,
        0.0,
        session_.flight.peakMultiplier,
        failureCause,
    };
    // Keep an already-revealed site underneath an authorized surface crash.
    // It remains uncommitted and is discarded when the loss handoff finishes.
    surfaceArrival_.phase = surfaceArrival_.prepared.has_value()
        ? SurfaceArrivalPhase::SurfaceReveal : SurfaceArrivalPhase::None;
    surfaceArrival_.elapsed = 0.0;
    surfaceArrival_.deployQueued = false;
    surfaceArrival_.landingCommitted = false;
    releaseRealtimeInputs(true);
    session_.controls.actions.cutEnginesActive = false;
    state_.statusLine = failureCause == LaunchFailureCause::ThermalRunaway
        ? "THERMAL RUNAWAY \xE2\x80\x94 ENGINE FAILURE"
        : "HULL LOST";
    queueControllerHapticCue(ControllerHapticCue::Failure);
    // Destruction changes live values and copy, not panel structure. Keep the
    // existing Flight HUD mounted through the explosion and patch its status
    // in place instead of rebuilding the entire sidebar mid-cinematic.
    realtimeHudDirty_ = true;
}

void RocketGameApp::completeLaunch(
    double burnMultiplier,
    RecoveryMethod method,
    LaunchFailureCause failureCause)
{
    surfaceArrival_.reset();
    const GameState stateBefore = state_;
    const PreparedLaunch flightModel = currentFlightModel();
    const bool wasReturningHome = session_.controls.actions.returningHome;
    const double frozenTravelProgress = session_.flight.travelProgress;

    LaunchOutcome outcome = resolveLaunch(
        flightModel,
        catalog_,
        state_,
        burnMultiplier,
        method,
        rng_,
        {true,
            failureCause,
            session_.flight.minimumSafetyMargin,
            session_.flight.hullDamageTaken,
            session_.flight.fuelSurveyReturnTiming});
    outcome.peakWarning = std::max(outcome.peakWarning, session_.peakWarning);
    outcome.transferFuelRemaining = std::max(0.0, session_.flight.fuelRemaining);
    outcome.transferFuelCapacity = std::max(0.0, session_.flight.fuelCapacity);
    outcome.telemetry = chartTelemetryForOutcome(flightModel, session_.flight, wasReturningHome);
    if (outcome.failureCause==LaunchFailureCause::None || outcome.failureCause==LaunchFailureCause::LunarImpact) {
        outcome.impact=session_.flight.impact;
    }
    if (session_.flight.physicalFlight && outcome.type!=LaunchResultType::Destroyed &&
        outcome.failureCause==LaunchFailureCause::None) {
        outcome.shipDamage=physicalFlightCampaignDamage(session_.flight,flightModel.existingShipDamage);
    }
    if (!outcome.telemetry.empty()) {
        outcome.telemetry.back() = launchTelemetryAt(flightModel, session_.flight);
    }
    applyLaunchOutcome(state_, catalog_, outcome);
    session_.destruction.active = false;
    const bool completedStraylightApproach =
        flightModel.config.missionKind == LaunchMissionKind::StraylightApproach &&
        outcome.type == LaunchResultType::MissionComplete &&
        outcome.failureCause == LaunchFailureCause::None;
    if (completedStraylightApproach) {
        state_.storyBriefing = {};
        scheduleStoryBriefing(state_, StoryBriefingId::ActOneComplete, Screen::Hangar);
        state_.screen = Screen::StoryBriefing;
        state_.statusLine = "Straylight has opened a docking corridor.";
    } else if (shouldOpenArrivalOps(outcome, catalog_)) {
        if (state_.storyBriefing.pending == StoryBriefingId::StraylightDiscovery) {
            // Neptune is the one arrival whose ordinary fanfare gives way to a
            // saved, input-blocking story beat after the player reviews results.
            state_.screen = Screen::Results;
        } else if (session_.flight.physicalFlight &&
                   session_.flight.phase == FlightPhase::Landed) {
            // Physical Flight already owns approach, orbit, descent, and the
            // touchdown ceremony. Enter the continuous landed world without
            // constructing the retired Arrival Ops aggregate even briefly.
            beginSurfaceExpeditionOrRefit();
        } else {
            startArrivalOps(state_, outcome);
            beginArrivalFanfare();
        }
    } else {
        state_.screen = Screen::Results;
    }
    session_.currentMultiplier = outcome.ejectMultiplier;
    session_.peakWarning = 0.0;
    session_.result.usesTravelProgress = wasReturningHome;
    session_.result.travelProgress = frozenTravelProgress;
    session_.result.elapsed = 0.0;
    clearFlightControls();
    if (!validateProgressionStateOrRestore(stateBefore, "launch outcome")) {
        return;
    }
    save();
    panelDirty_ = true;
}

void RocketGameApp::save()
{
    if (debugSessionActive_ || surfaceArrival_.active() || session_.destruction.active) {
        return;
    }
    const CampaignProgressionAuditResult audit = auditCampaignProgression(state_, catalog_);
    if (!audit.valid) {
        services_.host.log(PlatformLogLevel::Error, "Progression audit blocked save: " + audit.detail);
        state_.statusLine = "ROUTE CONTROL // RECOVERY REQUIRED.";
        panelDirty_ = true;
        return;
    }
    const std::string payload = serializeSaveData(captureSaveData(state_));
    if (!services_.saves.storeAtomic(payload)) {
        services_.host.log(PlatformLogLevel::Error, services_.saves.lastError());
        state_.statusLine = "Save failed; the previous save was preserved.";
        panelDirty_ = true;
    } else if (stateCanBecomeCheckpoint() && !services_.saves.storeCheckpointAtomic(payload)) {
        services_.host.log(PlatformLogLevel::Error, services_.saves.lastError());
    }
}

PanelRenderContext RocketGameApp::panelRenderContext(const PreparedLaunch& flightModel) const
{
    return {
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
        session_.launchQueued,
        session_.preflightElapsed >= tuning::session::preflightBoardingSeconds,
        miningDroneTransferEnabled(state_),
        debugActOneCheckpoint_,
        services_.saves.description(),
        services_.renderer.description(),
        titleScreenActive_,
        titleLaunchActive_ || sceneTransition_.active(),
        sceneTransition_.blackoutOpacity(),
        hasSavedGame_,
        checkpointRecoveryAvailable_,
        titleNotice_,
        firstTimeIntroductionsEnabled_,
        selectedRefitOfferIndex_,
        &session_.flight,
        levelUp_.elapsed,
        levelUp_.batchChoices,
        levelUpActivationLocked(),
        levelUp_.resolving ? levelUp_.selectedOfferIndex : -1,
        expeditionXpPulseSeconds_ > 0.0,
        miningEvaDeathModalReady(),
        surfaceBaySequence_.kind == SurfaceBaySequenceKind::Extract,
        surfaceArrival_.active(),
        static_cast<int>(surfaceArrival_.phase),
        surfaceArrival_.deployQueued,
        surfaceArrival_.landingCommitted,
        controllerConnected_ && activeInputSource_ == InputSource::Controller,
        controllerPreferences_.invertFlightY,
    };
}

void RocketGameApp::refreshPanel()
{
    const PreparedLaunch flightModel = currentFlightModel();
    const PanelRenderContext context = panelRenderContext(flightModel);
    const PanelDocumentPresentation presentation = buildGamePanelPresentation(context);
    services_.uiBridge.setUiHostContext({
        presentation.metadata.screen,
        uiSurfaceKindForScreen(presentation.metadata.screen),
        presentation.runtime.titleScreen,
        presentation.metadata.interaction == PanelInteractionMode::Realtime,
    });
    services_.ui.setPanelPresentation(presentation);
    panelStructureKey_ = realtimePanelStructureKey(context);
    panelDirty_ = false;
    realtimeHudDirty_ = false;
}

void RocketGameApp::refreshRealtimeHud()
{
    const PreparedLaunch flightModel = currentFlightModel();
    const PanelRenderContext context = panelRenderContext(flightModel);
    const std::uint64_t structureKey = realtimePanelStructureKey(context);
    if (structureKey != panelStructureKey_) {
        refreshPanel();
        return;
    }

    buildRealtimeHudState(context, realtimeHudState_);
    services_.ui.setRealtimeHudState(realtimeHudState_);
    realtimeHudDirty_ = false;
}

bool RocketGameApp::runScenarioUiAction(std::string_view action)
{
    ScenarioActionAddress address;
    if (!parseScenarioAction(action, address)) {
        return false;
    }

    const ScenarioDefinition* definition = scenarioDefinitionForRuntimeId(
        state_,
        catalog_,
        address.scenarioId);
    const ScenarioInstance* instance = findScenarioInstance(state_.meta, address.scenarioId);
    const ScenarioDefinition resolved = definition != nullptr && instance != nullptr
        ? resolveScenarioDefinition(*definition, *instance)
        : ScenarioDefinition {};
    const ScenarioStepDefinition* step = definition == nullptr
        ? nullptr
        : (instance == nullptr
               ? findScenarioStepDefinition(*definition, address.stepId)
               : findScenarioStepDefinition(resolved, address.stepId));
    if (step == nullptr) {
        return true;
    }

    // The completed Flyby result screen presents the reward claim directly.
    // Its run has finished visually, but its completion event is deliberately
    // committed only by this explicit player action.  Resolve that event
    // before asking the scenario system to claim the reward; otherwise the
    // button is labelled from the still-active step and the claim is rejected.
    const FlybyRunState& flyby = state_.run.approach.flyby;
    const bool claimingCompletedScenarioFlyby =
        address.action == ScenarioActionKind::ClaimReward &&
        state_.screen == Screen::Flyby &&
        flyby.active && flyby.completed &&
        flyby.purpose == FlybyPurpose::ScenarioChallenge &&
        flyby.scenarioId == address.scenarioId &&
        flyby.scenarioStepId == address.stepId;
    if (claimingCompletedScenarioFlyby) {
        if (flyby.result != FlybyGrade::Perfect) {
            return true;
        }
        completeFlybyRun(state_, catalog_);
    }

    const bool miningSiteAction =
        (address.action == ScenarioActionKind::BeginActivity ||
         address.action == ScenarioActionKind::RetryActivity) &&
        !step->miningSiteDefinitionId.empty();
    if (miningSiteAction && state_.run.planetaryExpedition.active &&
        state_.run.planetaryExpedition.miningRunUsed) {
        state_.statusLine =
            "This surface loop's Mining Rig deployment is spent. Return to Earth, then land again to retry this recovery.";
        panelDirty_ = true;
        return true;
    }

    const std::size_t equippedDroneCountBefore = state_.meta.equippedDroneIds.size();
    const bool grantsAutoAssignedSupportDrone = std::any_of(
        step->rewards.begin(),
        step->rewards.end(),
        [](const ScenarioReward& reward) {
            return reward.kind == ScenarioRewardKind::SupportDrone &&
                reward.equipIfSlotAvailable;
        });

    // A flyby challenge owns its run initialization. It dispatches the
    // scenario action exactly once, then records the grade/abort through the
    // same generic event stream used by any future challenge.
    if ((address.action == ScenarioActionKind::BeginActivity ||
         address.action == ScenarioActionKind::RetryActivity) &&
        step->completionEvent == ScenarioEventKind::FlybyFinished) {
        const GameState stateBefore = state_;
        if (!startScenarioFlybyRun(
                state_,
                catalog_,
                address.scenarioId,
                address.stepId,
                address.action)) {
            state_.statusLine = "The scenario challenge is not ready to launch.";
            panelDirty_ = true;
            return true;
        }
        if (!validateProgressionStateOrRestore(stateBefore, action)) {
            return true;
        }
        save();
        panelDirty_ = true;
        return true;
    }

    const GameState stateBefore = state_;
    const ScenarioActionOutcome outcome = performScenarioAction(
        state_, catalog_, address.scenarioId, address.stepId, address.action);
    if (!outcome.applied) {
        return true;
    }

    const Destination* claimedRoute = address.action == ScenarioActionKind::ClaimReward
        ? scenarioRouteRewardDestination(catalog_, *step)
        : nullptr;
    const bool supportDroneNeedsAssignment = grantsAutoAssignedSupportDrone &&
        state_.meta.equippedDroneIds.size() <= equippedDroneCountBefore;
    if (outcome.beginsActivity && !outcome.miningSiteDefinitionId.empty()) {
        PlanetaryExpeditionState& expedition = state_.run.planetaryExpedition;
        if (!expedition.active) {
            state_.statusLine = "Open Surface Ops before beginning this recovery site.";
        } else if (expedition.miningRunUsed) {
            // A scenario mining site shares the Surface Ops loop's single
            // Mining Rig deployment. Keep its retry state explicit instead
            // of sending the action through a generic no-op start attempt.
            state_.statusLine =
                "This surface loop's Mining Rig deployment is spent. Return to Earth, then land again to retry this recovery.";
        } else {
            expedition.pendingScenarioId = address.scenarioId;
            expedition.pendingScenarioStepId = address.stepId;
            expedition.pendingMiningSiteDefinitionId = outcome.miningSiteDefinitionId;
            mineSurface();
        }
    } else if (outcome.transition.kind == ScenarioTransitionKind::QueueRewardedRoute) {
        if (claimedRoute != nullptr && commitClaimedScenarioRoute(
                state_, catalog_, address.scenarioId, address.stepId)) {
            state_.statusLine = claimedRoute->name + " course locked. Launch when ready.";
        } else {
            state_.statusLine = "Route reward is secured, but its physical launch leg could not be queued.";
        }
    } else if (outcome.transition.kind == ScenarioTransitionKind::OpenScreen) {
        state_.screen = outcome.transition.screen;
        state_.statusLine = outcome.message;
    } else if (outcome.transition.kind == ScenarioTransitionKind::PresentStoryTakeover) {
        scheduleStoryBriefing(state_, outcome.transition.storyBriefing, outcome.transition.screen);
        state_.statusLine = outcome.message;
    } else if (supportDroneNeedsAssignment) {
        // The reward is owned, but no capacity was available for its
        // content-authored automatic assignment. Drone Ops provides the
        // explicit, reusable swap decision instead of silently replacing a
        // loadout entry.
        state_.screen = Screen::DroneOps;
        state_.statusLine = "New Support Drone ready. Free a bay slot or swap an active Support Drone, then assign it.";
    } else if (!outcome.message.empty()) {
        state_.statusLine = outcome.message;
    }

    if (!validateProgressionStateOrRestore(stateBefore, action)) {
        return true;
    }

    syncLaunchConfig(state_, catalog_);
    save();
    panelDirty_ = true;
    return true;
}

void RocketGameApp::runUiAction(const std::string& action)
{
    if (miningSceneHandoff_ != MiningSceneHandoff::None) {
        return;
    }

    int index = 0;
    if (runScenarioUiAction(action)) {
        return;
    } else if (action.starts_with(ui::actions::acknowledgeResearchBreakthroughPrefix)) {
        const std::string key = action.substr(ui::actions::acknowledgeResearchBreakthroughPrefix.size());
        for (const tuning::unlocks::BlueprintUnlock& milestone : tuning::unlocks::blueprintUnlocks) {
            if (milestone.key == key && state_.meta.blueprintProgress >= milestone.threshold && hasUnlock(state_.meta, key)) {
                ui::briefings::acknowledge(state_.meta.acknowledgedActivityBriefingIds, action);
                state_.statusLine = unlockDisplayName(key) + " added to future Refit offers.";
                save();
                panelDirty_ = true;
                break;
            }
        }
    } else if (action == ui::actions::newGame) {
        newGame();
    } else if (action == ui::actions::continueGame) {
        continueGame();
    } else if (action == ui::actions::restoreCheckpoint) {
        (void)restoreCheckpoint();
    } else if (consumeIndexedAction(action, ui::actions::selectRefitOfferPrefix, index)) {
        selectRefitOffer(index);
    } else if (consumeIndexedAction(action, ui::actions::buyOfferPrefix, index)) {
        buyOffer(index);
    } else if (consumeIndexedAction(action, ui::actions::researchProjectPrefix, index)) {
        selectResearchProject(index);
    } else if (consumeIndexedAction(action, ui::actions::surfaceUpgradePrefix, index)) {
        selectSurfaceUpgrade(index);
    } else if (consumeIndexedAction(action, ui::actions::equipDronePrefix, index)) {
        equipDrone(index);
    } else if (consumeIndexedAction(action, ui::actions::unequipDroneSlotPrefix, index)) {
        unequipDroneSlot(index);
    } else if (consumeIndexedAction(action, ui::actions::selectNavigationDestinationPrefix, index)) {
        selectNavigationDestination(index);
    } else if (action == ui::actions::acceptCrewReplacement) {
        if (rocket::acceptCrewReplacement(state_, catalog_)) {
            save();
        }
        panelDirty_ = true;
    } else if (action == ui::actions::prepareLaunch) {
        prepareForLaunch();
    } else if (action == ui::actions::startLaunch) {
        startLaunch();
    } else if (action == ui::actions::returnHome) {
        returnHome();
    } else if (action == ui::actions::arrivalOps) {
        arrivalOps();
    } else if (action == ui::actions::acknowledgeStoryBriefing) {
        acknowledgeStoryBriefing();
    } else if (action == ui::actions::cutEngines) {
        cutEngines();
    } else if (action == ui::actions::deploySurfaceTeam) {
        deploySurfaceTeam();
    } else if (action == ui::actions::departSurfaceUndeployed) {
        departSurfaceUndeployed();
    } else if (action == ui::actions::next) {
        next();
    } else if (action == ui::actions::attemptFrontier) {
        attemptFrontierTransfer();
    } else if (action == ui::actions::acknowledgeJupiterWindow) {
        acknowledgeJupiterWindow();
    } else if (action == ui::actions::openJupiterRefit) {
        openJupiterRefit();
    } else if (action.starts_with(ui::actions::beginTransferAssistPrefix)) {
        beginTransferAssist(action.substr(ui::actions::beginTransferAssistPrefix.size()));
    } else if (action == ui::actions::continueTransferAssist) {
        continueTransferAssist();
    } else if (action == ui::actions::beginJupiterSlingshot) {
        beginJupiterSlingshot();
    } else if (action == ui::actions::continueJupiterSlingshot) {
        continueJupiterSlingshot();
    } else if (action == ui::actions::openNavigation) {
        openNavigation();
    } else if (action == ui::actions::arkJump) {
        arkJump();
    } else if (action == ui::actions::rerollOffers) {
        rerollOffers();
    } else if (action == ui::actions::acknowledgeApproachIntroduction) {
        acknowledgeApproachIntroduction();
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
    } else if (action == ui::actions::arrivalOrbitDepart) {
        departCapturedOrbit();
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
    } else if (action == ui::actions::miningWaitForDrones) {
        miningWaitForDrones();
    } else if (action == ui::actions::miningDepart) {
        miningDepart();
    } else if (action == ui::actions::miningAbort) {
        miningAbort();
    } else if (action == ui::actions::miningFailureAck) {
        miningFailureAck();
    } else if (action == ui::actions::repairShip) {
        repairShip();
    } else if (action == ui::actions::resetSave) {
        resetSave();
    }
}

RenderSnapshot RocketGameApp::snapshot() const
{
    RenderSnapshot result;
    if (titleScreenActive_) {
        result.screen = Screen::Hangar;
        result.titleScreen = true;
        if (titleLaunchActive_ || sceneTransition_.active()) {
            result.titleLaunchRumble = std::clamp(
                1.0 - titleLaunchElapsedSeconds_ / kTitleLaunchIgnitionDelaySeconds,
                0.0,
                1.0);
            result.titleLaunchProgress = std::clamp(
                (titleLaunchElapsedSeconds_ - kTitleLaunchIgnitionDelaySeconds) / kTitleLaunchDepartureSeconds,
                0.0,
                1.0);
        }
        result.sceneFadeToBlack = sceneTransition_.blackoutOpacity();
        result.animationTime = services_.host.monotonicSeconds();
        return result;
    }
    const PreparedLaunch flightModel = currentFlightModel();
    result.screen = state_.screen;
    result.sceneFadeToBlack = sceneTransition_.blackoutOpacity();
    result.lastResult = state_.screen == Screen::Results ? state_.lastOutcome.type : LaunchResultType::None;
    result.lastLaunchFailureCause = state_.screen == Screen::Results
        ? state_.lastOutcome.failureCause
        : LaunchFailureCause::None;
    result.currentMultiplier = session_.currentMultiplier;
    result.animationTime = session_.result.elapsed;
    if (state_.screen == Screen::Flight) {
        result.animationTime = session_.destruction.active
            ? session_.destruction.elapsed
            : (surfaceArrival_.active()
                ? surfaceArrival_.elapsed
                : (session_.flightArmed ? session_.elapsed : session_.preflightElapsed));
    } else if (state_.screen == Screen::Mining) {
        result.animationTime = surfaceBaySequence_.kind == SurfaceBaySequenceKind::Extract
            ? surfaceBaySequence_.elapsed
            : state_.run.mining.elapsedSeconds;
    } else if (state_.screen == Screen::Flyby) {
        result.animationTime = state_.run.approach.flyby.elapsedSeconds;
    } else if (state_.screen == Screen::Orbit) {
        result.animationTime = state_.run.approach.orbit.elapsedSeconds;
    } else if (state_.screen == Screen::SurfaceScan) {
        result.animationTime = state_.run.surfaceScan.elapsedSeconds;
    } else if (state_.screen == Screen::SurfacePush) {
        result.animationTime = visualTimeSeconds_;
    } else if (state_.screen == Screen::SurfaceUpgrade) {
        result.animationTime = visualTimeSeconds_;
        result.levelUpFanfare = levelUp_.fanfareActive
            ? 1.0 - std::clamp(levelUp_.elapsed / kLevelUpFanfareSeconds, 0.0, 1.0)
            : 0.0;
    } else if (state_.screen == Screen::ArrivalFanfare) {
        result.animationTime = session_.arrivalFanfare.elapsed;
    }
    const Destination& currentFrontier = currentDestination(state_, catalog_);
    const Destination* visualDestination = &currentFrontier;
    if (state_.screen == Screen::Flight) {
        if (const Destination* activeDestination = catalog_.findDestination(session_.preparedLaunch.config.destinationId)) {
            visualDestination = activeDestination;
        }
        result.frontierTransfer = session_.preparedLaunch.config.frontierTransfer;
    } else if (state_.screen == Screen::StoryBriefing || state_.screen == Screen::Results || state_.screen == Screen::ArrivalFanfare || state_.screen == Screen::ArrivalOps || state_.screen == Screen::Flyby || state_.screen == Screen::Orbit || state_.screen == Screen::SurfaceScan || state_.screen == Screen::SurfacePush) {
        if (const Destination* resultDestination = catalog_.findDestination(state_.lastOutcome.destinationId)) {
            visualDestination = resultDestination;
        }
        if (state_.screen == Screen::Flyby && !state_.run.approach.flyby.destinationId.empty()) {
            if (const Destination* flybyDestination = catalog_.findDestination(state_.run.approach.flyby.destinationId)) {
                visualDestination = flybyDestination;
            }
        }
        if (state_.screen == Screen::Orbit && !state_.run.approach.orbit.destinationId.empty()) {
            if (const Destination* orbitDestination = catalog_.findDestination(state_.run.approach.orbit.destinationId)) {
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
    if (state_.screen == Screen::Flight) {
        result.launchMissionTargetProgress = std::clamp(
            (flightModel.config.burnGoalMultiplier - 1.0) /
                std::max(0.01, visualDestination->targetMultiplier - 1.0),
            0.0,
            1.0);
        result.travelProgress = session_.flightArmed ? session_.flight.travelProgress : 0.0;
        const double targetMultiplier = 1.0 +
            (visualDestination->targetMultiplier - 1.0) * result.launchMissionTargetProgress;
        result.launchMissionTargetReached = session_.flightArmed &&
            session_.flight.peakMultiplier + 0.000001 >= targetMultiplier;
        result.returningHome = session_.flight.returningHome;
        result.returnTurnProgress = result.returningHome ? 1.0 : 0.0;
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
    if (state_.screen == Screen::Flight) {
        if (flightModel.config.routeTransit.active()) {
            if (const Destination* source = catalog_.findDestination(flightModel.config.routeTransit.originDestinationId)) {
                result.launchOriginTier = source->tier;
            }
        } else if (!flightModel.transferAssistId.empty()) {
            if (const TransferAssistDefinition* assist = catalog_.findTransferAssist(flightModel.transferAssistId)) {
                if (const Destination* source = catalog_.findDestination(assist->sourceDestinationId)) {
                    result.launchOriginTier = source->tier;
                }
            }
        }
    }
    result.debugActOneCheckpoint = debugActOneCheckpoint_;
    result.arkCondition = state_.meta.ark.condition;
    result.straylightStoryReveal = state_.screen == Screen::StoryBriefing &&
        (state_.storyBriefing.pending == StoryBriefingId::StraylightDiscovery ||
         state_.storyBriefing.pending == StoryBriefingId::StraylightApproach ||
         state_.storyBriefing.pending == StoryBriefingId::ActOneComplete);
    result.straylightApproach = state_.screen == Screen::Flight &&
        flightModel.config.missionKind == LaunchMissionKind::StraylightApproach;
    result.campaignStoryIntroduction = state_.screen == Screen::StoryBriefing
        && state_.storyBriefing.pending == StoryBriefingId::CampaignIntroduction;
    if (result.straylightStoryReveal) {
        result.arkCondition = ArkCondition::DerelictOperable;
    }
    result.preflightActive = state_.screen == Screen::Flight && !session_.flightArmed && miningDroneTransferEnabled(state_);
    result.preflightProgress = result.preflightActive
        ? std::clamp(session_.preflightElapsed / tuning::session::preflightBoardingSeconds, 0.0, 1.0)
        : 1.0;

    const MiningRunState* visualMining = nullptr;
    const PlanetaryExpeditionState* visualExpedition = nullptr;
    if (state_.screen == Screen::Mining &&
        (state_.run.mining.active || surfaceBaySequence_.kind == SurfaceBaySequenceKind::Extract)) {
        visualMining = &state_.run.mining;
        visualExpedition = &state_.run.planetaryExpedition;
    } else if (state_.screen == Screen::Flight) {
        if (surfaceArrival_.landingCommitted && state_.run.mining.active) {
            visualMining = &state_.run.mining;
            visualExpedition = &state_.run.planetaryExpedition;
        } else if (surfaceArrival_.prepared.has_value() && surfaceArrival_.prepared->valid) {
            visualMining = &surfaceArrival_.prepared->miningTemplate;
            visualExpedition = &surfaceArrival_.prepared->expeditionTemplate;
        }
    }
    result.surfaceArrivalPrepared = visualMining != nullptr && state_.screen == Screen::Flight;
    result.surfaceArrivalActive = surfaceArrival_.active();
    result.surfaceArrivalLandingCommitted = surfaceArrival_.landingCommitted;
    result.surfaceArrivalHardLanding = session_.flight.landing.hardLanding;
    result.surfaceArrivalDeployQueued = surfaceArrival_.deployQueued;
    result.surfaceArrivalPhase = static_cast<int>(surfaceArrival_.phase);
    result.surfaceArrivalProgress = surfaceArrival_.phase == SurfaceArrivalPhase::Deploying
        ? std::clamp(surfaceArrival_.elapsed / kSurfaceDeploymentSeconds, 0.0, 1.0)
        : (surfaceArrival_.phase == SurfaceArrivalPhase::UndeployedTakeoff
            ? std::clamp(surfaceArrival_.elapsed / kSurfaceUndeployedTakeoffSeconds, 0.0, 1.0)
            : (surfaceArrival_.phase == SurfaceArrivalPhase::Touchdown
                ? std::clamp(surfaceArrival_.elapsed / kTouchdownCelebrationSeconds, 0.0, 1.0)
                : 0.0));

    if (visualMining != nullptr && visualExpedition != nullptr) {
        const MiningRunState& mining = *visualMining;
        const PlanetaryExpeditionState& expedition = *visualExpedition;
        result.miningExtractionActive = surfaceBaySequence_.kind == SurfaceBaySequenceKind::Extract;
        result.miningExtractionProgress = result.miningExtractionActive
            ? std::clamp(surfaceBaySequence_.elapsed / tuning::mining::miningExtractionSequenceSeconds, 0.0, 1.0)
            : 0.0;
        result.miningWidth = mining.terrain.width;
        result.miningHeight = mining.terrain.height;
        const std::string& activeGeology = mining.depthZone > mining.entryDepthZone
            ? mining.deepGeologyId
            : mining.surfaceGeologyId;
        result.miningPostSolarGeologyRow = postSolarGeologyRow(activeGeology);
        result.miningGeologySeed = mining.geologySeed;
        result.miningDroneX = mining.droneX;
        result.miningDroneY = mining.droneY;
        result.miningTargetX = mining.targetTipX;
        result.miningTargetY = mining.targetTipY;
        result.miningHeat = mining.drillHeat;
        result.miningDrillIntegrity = mining.drillIntegrity;
        result.miningDroneHealth = mining.droneHealth;
        result.miningReturnZoneX = mining.returnZoneX;
        result.miningReturnZoneY = mining.returnZoneY;
        result.miningShipPresent = mining.depthZone == mining.entryDepthZone;
        result.miningAtReturnZone = miningAtReturnZone(mining);
        const MiningLoadStats loadStats = surfaceArrival_.prepared.has_value()
            ? MiningLoadStats {}
            : miningLoadStats(state_, catalog_);
        result.miningLoad = loadStats.currentLoad;
        result.miningLoadSpeedMultiplier = loadStats.speedMultiplier;
        result.miningContactIntensity = mining.contactIntensity;
        result.miningContactIndicatorSeconds = mining.contactIndicatorSeconds;
        result.miningContactIndicatorDirX = mining.contactIndicatorDirX;
        result.miningContactIndicatorDirY = mining.contactIndicatorDirY;
        result.miningScannerPulse = mining.scannerPulseSeconds;
        result.miningArtifactSecuredCelebration =
            std::clamp(mining.artifactSecuredCelebrationSeconds / 2.0, 0.0, 1.0);
        result.miningScannerRechargeProgress = tuning::mining::scannerRechargePresentationProgress(
            expedition.scannerCooldownSeconds);
        const MiningDrillStats miningStats = miningDrillStats(state_, catalog_);
        if (!surfaceBaySequence_.active() && state_.screen == Screen::Mining) {
            result.miningPoiGuidance = miningPoiGuidanceTarget(
                mining,
                miningActiveOxygenSeconds(mining),
                miningActiveOxygenCapacity(state_, catalog_),
                tuning::launch::warningCautionThreshold,
                result.miningAtReturnZone);
        }
        result.miningScannerRadius =
            mining.operatorMode == MiningOperatorMode::Jetpack
            ? tuning::mining::scannerRevealRadius
            : miningStats.scannerRadius;
        result.miningBounceRelief = miningStats.hardRockBounceRelief;
        result.miningFailurePulse = mining.failurePending ? std::max(0.25, std::clamp(mining.failureSeconds / 1.5, 0.0, 1.0)) : 0.0;
        const auto evaDeathPhase = miningEvaDeathPresentation_.phase;
        result.miningEvaDeathActive =
            evaDeathPhase != MiningEvaDeathPresentationState::Phase::None &&
            evaDeathPhase != MiningEvaDeathPresentationState::Phase::Complete;
        result.miningEvaDeathProgress =
            evaDeathPhase == MiningEvaDeathPresentationState::Phase::Impact
            ? std::clamp(
                  miningEvaDeathPresentation_.elapsed /
                      kMiningEvaDeathImpactSeconds,
                  0.0,
                  1.0)
            : (result.miningEvaDeathActive ? 1.0 : 0.0);
        result.miningRecoilX = mining.recoilX;
        result.miningRecoilY = mining.recoilY;
        result.miningMoveX = mining.moveX;
        result.miningMoveY = mining.moveY;
        result.miningHullDirX = mining.hullDirX;
        result.miningHullDirY = mining.hullDirY;
        result.miningOperatorPresent = mining.operatorPresent;
        result.miningOperatorActive =
            mining.operatorMode == MiningOperatorMode::Jetpack &&
            mining.operatorPresent;
        result.miningOperatorX = mining.operatorX;
        result.miningOperatorY = mining.operatorY;
        result.miningOperatorVelocityX = mining.operatorVelocityX;
        result.miningOperatorVelocityY = mining.operatorVelocityY;
        result.miningOperatorAimX = mining.operatorAimDirX;
        result.miningOperatorAimY = mining.operatorAimDirY;
        result.miningOperatorThrustX = result.miningOperatorActive ? mining.moveX : 0.0;
        result.miningOperatorThrustY = result.miningOperatorActive ? mining.moveY : 0.0;
        result.miningOperatorIntegrity = mining.operatorIntegrity;
        result.miningOperatorToggleProgress = mining.operatorToggleProgress;
        result.miningOperatorFirePulse =
            std::clamp(mining.operatorFirePulseSeconds / 0.12, 0.0, 1.0);
        result.miningRigPresent = mining.rigDepthZone == mining.depthZone;
        result.miningRigDisabled = mining.rigDisabled;
        // Disabled rigs remain valid EVA recovery targets. Do not hide the
        // tow line merely because the object being recovered is a wreck.
        result.miningOperatorRigTethered = mining.operatorRigTethered &&
            mining.operatorMode == MiningOperatorMode::Jetpack && mining.operatorPresent;
        const MiniDroneAnchorFrame anchor = resolveMiniDroneAnchor(mining);
        result.miningAnchorValid = anchor.valid;
        result.miningAnchorX = anchor.x;
        result.miningAnchorY = anchor.y;
        const MiningCell* target = miningCellAt(mining.terrain, mining.targetCellX, mining.targetCellY);
        const bool targetDrillable = target != nullptr && miningMaterialSolid(target->material) && target->material != MiningCellMaterial::Bedrock;
        result.miningBounce = mining.contactBounce;
        result.miningTargetDrillable = targetDrillable;
        result.miningDrilling = mining.drilling && targetDrillable;
        result.miningCargo = mining.cargo;
        result.miningStowedCargo = mining.stowedCargo;
        result.miningMaterials = mining.temporaryMaterials;
        result.miningStowedMaterials = mining.stowedMaterials;
        result.miningSwarmActive = mining.swarm.enabled && mining.depthZone == mining.swarm.depthZone;
        result.miningSwarmAlert = result.miningSwarmActive && mining.swarm.alerted;
        result.miningSwarmWave = result.miningSwarmActive ? mining.swarm.wave : 0;
        result.miningSwarmDepth = mining.swarm.depthZone;
        result.miningSwarmAlertProgress = result.miningSwarmAlert
            ? std::clamp(1.0 - mining.swarm.alertSeconds / 2.5, 0.0, 1.0)
            : 0.0;
        result.miningSwarmCacheExposed = result.miningSwarmActive && mining.swarm.cacheExposed;
        result.miningSwarmCacheClaimed = mining.swarm.cacheClaimed;
        result.miningSwarmArtifact = mining.swarm.bonusArtifactRolled;
        result.miningSwarmCacheX = mining.swarm.cacheX;
        result.miningSwarmCacheY = mining.swarm.cacheY;
        result.miningEnemyTheme = mining.enemyTheme;
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
                mining.artifact.revealed,
                static_cast<int>(mining.gate.type),
                static_cast<int>(mining.gate.state),
                mining.artifactTetherDeniedFlashSeconds
            };
        }
        result.bindMiningFrameViews(mining);
        result.miningDroneModuleAssignments = expedition.droneModuleAssignments;
        result.miningTreasureMarks = expedition.treasureMarks;
    }

    if (state_.screen == Screen::Flyby && state_.run.approach.flyby.active) {
        const FlybyRunState& flyby = state_.run.approach.flyby;
        result.flybyCompleted = flyby.completed;
        result.flybyZone = flyby.currentZone;
        result.flybyResult = static_cast<int>(flyby.result);
        result.flybyShipX = flyby.shipX;
        result.flybyShipY = flyby.shipY;
        result.flybyVelocityX = flyby.velocityX;
        result.flybyVelocityY = flyby.velocityY;
        result.flybyInputX = flyby.inputX;
        result.flybyInputY = flyby.inputY;
        result.flybyDestinationX = tuning::flyby::destinationX;
        result.flybyDestinationY = tuning::flyby::destinationY;
        result.flybyGoodBand = tuning::flyby::goodBand;
        result.flybyPerfectBand = tuning::flyby::perfectBand;
        result.flybyTrailPoints = flyby.trailPoints;
    }

    if (state_.screen == Screen::Orbit && state_.run.approach.orbit.active) {
        const OrbitRunState& orbit = state_.run.approach.orbit;
        result.orbitCompleted = orbit.completed;
        result.orbitZone = orbit.currentZone;
        result.orbitResult = static_cast<int>(orbit.result);
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
        result.orbitTrailPoints = orbit.trailPoints;
    }

    if (state_.screen == Screen::SurfaceScan && (state_.run.surfaceScan.active || state_.run.surfaceScan.completed)) {
        const SurfaceScanRunState& scan = state_.run.surfaceScan;
        result.surfaceScanBusted = scan.busted;
        result.surfaceScanPulses = scan.pulses;
        result.surfaceScanMaxPulses = std::max(1, scan.maxPulses);
        result.surfaceScanCurrentDepthOffset = static_cast<int>(scan.depthProspects.size());
        result.surfaceScanSignal = scan.signal;
        result.surfaceScanInterference = scan.interference;
        result.surfaceScanBustRisk = scan.bustRisk;
        const double fanfareDuration = scan.lastPulseGrade == SurfaceScanPulseGrade::Perfect
            ? tuning::research::scanPerfectSuccessFanfareSeconds
            : tuning::research::scanGoodSuccessFanfareSeconds;
        result.surfaceScanSuccessFanfare = std::clamp(
            scan.successFanfareSeconds / fanfareDuration,
            0.0,
            1.0);
        result.surfaceScanMissFanfare = std::clamp(
            scan.missFanfareSeconds / tuning::research::scanMissFanfareSeconds,
            0.0,
            1.0);
        result.surfaceScanLastPulseGrade = scan.lastPulseGrade;
        result.surfaceScanMaterials = scan.temporaryMaterials;
        result.surfaceScanArtifacts = static_cast<int>(scan.temporaryArtifacts.size());
        for (const SurfaceDepthProspect& prospect : scan.depthProspects) {
            if (result.surfaceScanPreviewMarkers.size() >= kMaxSurfaceProspectMarkers) {
                break;
            }
            appendProspectMarkers(result.surfaceScanPreviewMarkers, result.surfaceScanPreviewDepthOffsets, prospect);
        }
    }

    if (state_.screen == Screen::SurfacePush && (state_.run.surfacePush.active || state_.run.surfacePush.completed)) {
        const SurfacePushRunState& push = state_.run.surfacePush;
        result.surfacePushBusted = push.busted;
        result.surfacePushStartDepth = std::max(0, state_.run.planetaryExpedition.depth);
        result.surfacePushSteps = push.steps;
        result.surfacePushMaxSteps = std::max(1, push.maxSteps);
        result.surfacePushPressure = push.pressure;
        result.surfacePushCollapseRisk = push.collapseRisk;
        result.surfacePushMaterials = push.temporaryMaterials;
        result.surfacePushArtifacts = static_cast<int>(push.temporaryArtifacts.size());
        const std::size_t visibleRewardCount = std::min(
            push.rewardMarkers.size(),
            kMaxSurfacePushRewardMarkers);
        result.surfacePushRewardMarkers.assign(
            push.rewardMarkers.begin(),
            push.rewardMarkers.begin() + static_cast<std::ptrdiff_t>(visibleRewardCount));
        const std::size_t visibleRewardOffsetCount = std::min(
            push.rewardMarkerDepthOffsets.size(),
            visibleRewardCount);
        result.surfacePushRewardDepthOffsets.assign(
            push.rewardMarkerDepthOffsets.begin(),
            push.rewardMarkerDepthOffsets.begin() + static_cast<std::ptrdiff_t>(visibleRewardOffsetCount));
        for (const SurfaceDepthProspect& prospect : state_.run.planetaryExpedition.depthProspects) {
            if (result.surfacePushForecastMarkers.size() >= kMaxSurfaceProspectMarkers) {
                break;
            }
            appendProspectMarkers(result.surfacePushForecastMarkers, result.surfacePushForecastDepthOffsets, prospect);
        }
    }

    if (state_.screen == Screen::Flight) {
        result.launchDestructionActive = session_.destruction.active;
        result.launchDestructionElapsed = session_.destruction.elapsed;
        result.launchDestructionCause = session_.destruction.failureCause;
        result.launchManualControlsEnabled = flightModel.manualControlsEnabled;
        result.launchHeatEnabled = flightModel.heatEnabled;
        result.launchAsteroidsEnabled = flightModel.asteroidsEnabled;
        result.launchFuelCapacity = flightModel.fuelCapacity;
        result.launchFuelRemaining = session_.flight.fuelRemaining;
        result.launchProjectedFuelReserve = session_.flight.projectedFuelReserve;
        result.launchInsertionReserve = flightModel.arrivalReserveFuel;
        result.launchCourseLimit = launchCourseLimit(flightModel);
        // The slingshot handoff is already physically in this lane during
        // preflight; expose it before arming so the launch scene never flashes
        // at center and then jumps sideways on ignition.
        result.launchCourseOffset = session_.flight.courseOffset;
        result.launchCourseVelocity = session_.flight.courseVelocity;
        result.launchPhysicalFlight = session_.flight.physicalFlight;
        result.launchFlightPhase = static_cast<int>(session_.flight.phase);
        result.launchPositionX = session_.flight.positionX;
        result.launchPositionY = session_.flight.positionY;
        result.launchVelocityX = session_.flight.velocityX;
        result.launchVelocityY = session_.flight.velocityY;
        result.launchHeading = session_.flight.heading;
        result.launchLandingAuthorized = session_.flight.orbit.captured || !flightModel.orbitRequired;
        const FlightScaleProfile scaleProfile = flightScaleProfile(session_.flight);
        result.launchApproachBlend = scaleProfile.approachBlend;
        result.launchLandingBlend = scaleProfile.landingBlend;
        result.launchOrbitTargetRadius = session_.flight.orbit.targetRadius;
        result.launchOrbitGoodBand = session_.flight.orbit.goodBand;
        result.launchOrbitProgress = std::clamp(
            session_.flight.orbit.stableAngularProgress / 6.28318530717958647692,
            0.0,
            1.0);
        result.launchOrbitCaptured = session_.flight.orbit.captured;
        result.launchLandingAltitude = session_.flight.landing.altitude;
        result.launchLandingVerticalVelocity = session_.flight.landing.verticalVelocity;
        result.launchLandingLateralVelocity = session_.flight.landing.lateralVelocity;
        result.launchLandingLocalFrame = session_.flight.mode == FlightMode::Landing;
        result.launchFlightMode = static_cast<int>(session_.flight.mode);
        result.launchHandoffFrom = static_cast<int>(session_.flight.handoff.from);
        result.launchHandoffProgress = std::clamp(session_.flight.handoff.elapsed/flight_landing::handoffSeconds,0.0,1.0);
        result.launchHandoffX=session_.flight.handoff.sourceX;
        result.launchHandoffY=session_.flight.handoff.sourceY;
        result.launchLandingBasisAngle=session_.flight.landing.basisAngle;
        result.launchLandingHorizontalPosition=session_.flight.landing.horizontalPosition;
        result.launchLandingPadX=session_.flight.landing.padGridX;
        result.launchLandingPadY=session_.flight.landing.padGridY;
        result.launchDescentGateArmed=session_.flight.landing.gateArmed;
        if (result.launchLandingLocalFrame) {
            const auto& land=session_.flight.landing;
            const double nx=std::cos(land.basisAngle),ny=std::sin(land.basisAngle);
            const double r=flight_geometry::bodyRadius+land.altitude/flight_landing::metersPerOrbitUnit;
            result.launchPositionX=nx*r+ny*land.horizontalPosition/flight_landing::metersPerOrbitUnit;
            result.launchPositionY=ny*r-nx*land.horizontalPosition/flight_landing::metersPerOrbitUnit;
            result.launchVelocityX=(nx*land.verticalVelocity+ny*land.lateralVelocity)/flight_landing::metersPerOrbitUnit;
            result.launchVelocityY=(ny*land.verticalVelocity-nx*land.lateralVelocity)/flight_landing::metersPerOrbitUnit;
            result.launchHeading=land.heading+land.basisAngle-1.5707963267948966;
        }
        result.launchTouchdownCelebration =
            surfaceArrival_.phase == SurfaceArrivalPhase::Touchdown;
        result.launchTouchdownCelebrationProgress = std::clamp(
            (surfaceArrival_.phase == SurfaceArrivalPhase::Touchdown
                ? surfaceArrival_.elapsed
                : 0.0) / kTouchdownCelebrationSeconds,
            0.0,
            1.0);
        result.launchPredictedTrajectory.clear();
        result.launchPredictedTrajectory.reserve(session_.flight.predictedTrajectory.size());
        for (const FlybyTrailPoint& point : session_.flight.predictedTrajectory) {
            result.launchPredictedTrajectory.push_back({point.x, point.y});
        }
        result.launchAsteroidCount = std::clamp(
            flightModel.asteroidCount,
            0,
            static_cast<int>(result.launchAsteroids.size()));
        for (int index = 0; index < result.launchAsteroidCount; ++index) {
            const LaunchAsteroid& asteroid = flightModel.asteroids[static_cast<std::size_t>(index)];
            LaunchAsteroidSnapshot& snapshot = result.launchAsteroids[static_cast<std::size_t>(index)];
            snapshot.routeProgress = asteroid.routeProgress;
            snapshot.courseOffset = asteroid.courseOffset;
            snapshot.radius = asteroid.radius;
            snapshot.scale = asteroid.scale;
            snapshot.rotation = asteroid.rotation;
            snapshot.spin = asteroid.spin;
            snapshot.hit = session_.flight.asteroidHit[static_cast<std::size_t>(index)];
        }
        if (!session_.flightArmed) {
            result.telemetryCount = 0;
            result.poweredFlight = false;
            result.launchShake = 0.0;
            return result;
        }

        const TelemetryEvent event = launchTelemetryAt(flightModel, session_.flight);
        result.heat = flightModel.heatEnabled ? session_.flight.heat : 0.0;
        result.warning = event.warning;
        result.launchSteerInput = session_.steerInput;
        result.launchThrottle = session_.controls.actions.cutEnginesActive ? 0.0 : session_.flight.selectedThrottle;
        result.launchFuel = session_.flight.fuelRemaining / std::max(0.01, session_.flight.fuelCapacity);
        result.launchHullRemaining = session_.flight.hullRemaining;
        result.launchHullMaximum = session_.flight.hullMaximum;
        result.launchHeatFailureProgress = std::clamp(
            session_.flight.heatFailureSeconds / tuning::launch::pilotingHeatFailureSeconds,
            0.0,
            1.0);
        result.launchCourseFailureProgress = std::clamp(
            session_.flight.courseFailureSeconds / tuning::launch::pilotingCourseFailureSeconds,
            0.0,
            1.0);
        result.launchFuelFailureProgress = std::clamp(
            session_.flight.fuelFailureSeconds / tuning::launch::pilotingFuelFailureSeconds,
            0.0,
            1.0);
        result.telemetryCount = 0;
        result.poweredFlight = session_.flightArmed &&
            !session_.destruction.active &&
            !session_.controls.actions.cutEnginesActive &&
            (!session_.flight.physicalFlight || std::abs(session_.flight.selectedThrottle) > 0.01);
        result.launchImpactFlash = std::clamp(
            session_.asteroidImpactFeedbackSeconds / asteroidImpactFeedbackDuration,
            0.0,
            1.0);
        const double launchStartShake = session_.flightArmed
            ? std::clamp(1.0 - session_.elapsed / tuning::session::launchShakeSeconds, 0.0, 1.0)
            : 0.0;
        result.launchShake = session_.destruction.active
            ? 0.0
            : std::max(launchStartShake, result.launchImpactFlash);
    } else if (!state_.lastOutcome.telemetry.empty()) {
        const int count = std::min(static_cast<int>(result.telemetry.size()), static_cast<int>(state_.lastOutcome.telemetry.size()));
        for (int i = 0; i < count; ++i) {
            const TelemetryEvent& sample = state_.lastOutcome.telemetry[static_cast<std::size_t>(i)];
            result.telemetry[static_cast<std::size_t>(i)] = sample.warning;
            result.heatTelemetry[static_cast<std::size_t>(i)] = std::clamp(sample.heat, 0.0, 1.0);
        }
        result.telemetryCount = count;
    }

    FlightInstrumentPresentation instruments;
    if (state_.screen == Screen::Flight && session_.flightArmed &&
        !session_.destruction.active && !surfaceArrival_.active() &&
        session_.flight.mode != FlightMode::Landing) {
        instruments = launchFlightInstruments(flightModel, session_.flight);
    } else if (state_.screen == Screen::Flyby) {
        instruments = flybyFlightInstruments(state_.run.approach.flyby);
    } else if (state_.screen == Screen::Orbit) {
        instruments = orbitFlightInstruments(state_.run.approach.orbit);
    }
    result.flightInstrumentsVisible = instruments.visible;
    result.instrumentSpeed = instruments.speed;
    result.instrumentTemperature = instruments.temperature;
    result.instrumentFuel = instruments.fuel;
    result.instrumentThrottle = instruments.throttle;
    result.instrumentOffCourse = instruments.offCourse;
    result.instrumentCourseCritical = instruments.courseCritical;

    return result;
}

} // namespace rocket
