#include "game/GameRunner.h"
#include "game/GamePanel.h"
#include "game/GameRmlUi.h"
#include "game/IRmlRenderHost.h"
#include "core/ContentIds.h"
#include "core/GameState.h"
#include "core/ExpeditionSystem.h"
#include "core/GameUi.h"
#include "core/MiningSystem.h"
#include "core/PayloadTransfer.h"
#include "core/ResearchSystem.h"
#include "core/SaveData.h"
#include "core/ScenarioSystem.h"
#include "core/SolarProgression.h"
#include "core/Tuning.h"
#include "platform/AppServices.h"

#include <RmlUi/Core/RenderInterface.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace {

class FakeSaveStore final : public rocket::ISaveStore {
public:
    std::string load() override
    {
        ++loadCount;
        return value;
    }
    bool storeAtomic(std::string_view data) override
    {
        ++storeCount;
        if (failStore) return false;
        value = data;
        return true;
    }
    bool clear() override
    {
        ++clearCount;
        if (failClear) return false;
        value.clear();
        return true;
    }
    std::string loadCheckpoint() override { return checkpoint; }
    bool storeCheckpointAtomic(std::string_view data) override
    {
        ++checkpointStoreCount;
        checkpoint.assign(data);
        return true;
    }
    bool clearCheckpoint() override
    {
        checkpoint.clear();
        return true;
    }
    std::string lastError() const override { return failStore || failClear ? "Injected save failure." : ""; }
    std::string value;
    std::string checkpoint;
    int loadCount = 0;
    int storeCount = 0;
    int clearCount = 0;
    int checkpointStoreCount = 0;
    bool failStore = false;
    bool failClear = false;
};

class FakePreferenceStore final : public rocket::IPreferenceStore {
public:
    rocket::AppPreferences load() override { ++loadCount; return value; }
    bool store(const rocket::AppPreferences& next) override
    {
        value = next;
        ++revisionValue;
        return true;
    }
    std::uint64_t revision() const override { return revisionValue; }
    std::string lastError() const override { return {}; }
    rocket::AppPreferences value;
    int loadCount = 0;
    std::uint64_t revisionValue = 0;
};

class FakeHost final : public rocket::IPlatformHost {
public:
    double monotonicSeconds() const override { return now; }
    rocket::ViewportMetrics viewportMetrics() override { return metrics; }
    bool focused() const override { return true; }
    bool visible() const override { return true; }
    bool fullscreenAvailable() const override { return true; }
    bool fullscreen() const override { return fullscreenValue; }
    bool setFullscreen(bool enabled) override { fullscreenValue = enabled; return true; }
    void log(rocket::PlatformLogLevel level, std::string_view message) override
    {
        logLevels.push_back(level);
        logMessages.emplace_back(message);
    }
    bool haptic(double, double, double) override { ++hapticCount; return true; }
    mutable double now = 1.0;
    rocket::ViewportMetrics metrics {1280, 800, 2560, 1600, 2.0F};
    bool fullscreenValue = false;
    int hapticCount = 0;
    std::vector<rocket::PlatformLogLevel> logLevels;
    std::vector<std::string> logMessages;
};

class FakeController final : public rocket::IControllerSource {
public:
    rocket::ControllerFrame sampleFrame(double) override
    {
        rocket::ControllerFrame result = frame;
        frame.pressed.reset();
        return result;
    }
    void setPreferences(const rocket::ControllerPreferences& next) override
    {
        preferences = next;
        ++preferenceUpdateCount;
    }
    rocket::InputSource activeSource() const override { return rocket::InputSource::Controller; }
    void reset() override { resetCalled = true; }
    rocket::ControllerFrame frame;
    rocket::ControllerPreferences preferences;
    int preferenceUpdateCount = 0;
    bool resetCalled = false;
};

class FakeTextureSource final : public rocket::ITextureSource {
public:
    void request(std::string_view, std::string_view) override {}
    rocket::TextureStatus status(std::string_view) const override { return rocket::TextureStatus::Ready; }
    std::string lastError() const override { return {}; }
};

class FakeRenderer final : public rocket::IGameRenderer {
public:
    bool initialize() override { initialized = true; return true; }
    void render(const rocket::RenderSnapshot& snapshot) override
    {
        ++renderCount;
        animationTime = snapshot.animationTime;
        screen = snapshot.screen;
        titleScreen = snapshot.titleScreen;
        sceneFadeToBlack = snapshot.sceneFadeToBlack;
        shipDamage = snapshot.shipDamage;
        miningHeat = snapshot.miningHeat;
        miningOperatorRigTethered = snapshot.miningOperatorRigTethered;
        miningEvaDeathActive = snapshot.miningEvaDeathActive;
        miningEvaDeathProgress = snapshot.miningEvaDeathProgress;
        miningExtractionActive = snapshot.miningExtractionActive;
        miningExtractionProgress = snapshot.miningExtractionProgress;
        launchCourseOffset = snapshot.launchCourseOffset;
        launchCourseVelocity = snapshot.launchCourseVelocity;
        launchVelocityX = snapshot.launchVelocityX;
        launchVelocityY = snapshot.launchVelocityY;
        launchManualControlsEnabled = snapshot.launchManualControlsEnabled;
        launchHeatEnabled = snapshot.launchHeatEnabled;
        launchAsteroidsEnabled = snapshot.launchAsteroidsEnabled;
        launchFrontierTransfer = snapshot.frontierTransfer;
        launchFuelCapacity = snapshot.launchFuelCapacity;
        launchAsteroidCount = snapshot.launchAsteroidCount;
        launchDestinationTier = snapshot.destinationTier;
        launchOriginTier = snapshot.launchOriginTier;
        straylightApproach = snapshot.straylightApproach;
        launchTravelProgress = snapshot.travelProgress;
        launchReturningHome = snapshot.returningHome;
        launchDestructionActive = snapshot.launchDestructionActive;
        launchDestructionElapsed = snapshot.launchDestructionElapsed;
        launchDestructionCause = snapshot.launchDestructionCause;
        launchLandingAuthorized = snapshot.launchLandingAuthorized;
        launchLandingLocalFrame = snapshot.launchLandingLocalFrame;
        lastLaunchFailureCause = snapshot.lastLaunchFailureCause;
        miningSwarmActive = snapshot.miningSwarmActive;
        miningSwarmAlert = snapshot.miningSwarmAlert;
        miningSwarmWave = snapshot.miningSwarmWave;
        miningSwarmDepth = snapshot.miningSwarmDepth;
        miningSwarmEnemies = static_cast<int>(std::count_if(
            snapshot.miningEnemies.begin(),
            snapshot.miningEnemies.end(),
            [](const rocket::MiningEnemy& enemy) {
                return enemy.active && enemy.swarmAssociated;
            }));
        if (snapshot.screen == rocket::Screen::Mining) {
            miningViewsObserved = true;
            const std::size_t expectedCells = static_cast<std::size_t>(
                std::max(0, snapshot.miningWidth * snapshot.miningHeight));
            miningViewsValid = snapshot.miningCells.size() == expectedCells;
            miningViewChecksum = 0.0;
            for (const rocket::MiningCell& cell : snapshot.miningCells) {
                miningViewChecksum += cell.remainingToughness;
            }
            for (const rocket::MiningEnemy& enemy : snapshot.miningEnemies) {
                miningViewChecksum += enemy.health;
            }
            for (const rocket::MiningMiniDroneAgent& drone : snapshot.miningMiniDrones) {
                miningViewChecksum += drone.x + drone.y;
                if (drone.targetEnemyIndex >= 0) {
                    const std::size_t targetIndex = static_cast<std::size_t>(drone.targetEnemyIndex);
                    miningViewsValid = miningViewsValid && targetIndex < snapshot.miningEnemies.size();
                    if (targetIndex < snapshot.miningEnemies.size()) {
                        miningViewChecksum += snapshot.miningEnemies[targetIndex].health;
                    }
                }
            }
            for (const rocket::MiningProjectileVisual& projectile : snapshot.miningProjectiles) {
                miningViewChecksum += projectile.age;
            }
            for (const rocket::MiningDamageNumber& number : snapshot.miningDamageNumbers) {
                miningViewChecksum += number.amount;
            }
            for (const rocket::MiningGateMarker& marker : snapshot.miningGateMarkers) {
                miningViewChecksum += marker.x + marker.y;
            }
        }
    }
    void setPreferences(const rocket::AppPreferences& next) override
    {
        preferences = next;
        ++preferenceUpdateCount;
    }
    rocket::GraphicsFrameStatus endFrameAndPresent() override
    {
        ++presentCount;
        return rocket::GraphicsFrameStatus::Ready;
    }
    void shutdown() override { shutdownCalled = true; }
    rocket::AppPreferences preferences;
    bool initialized = false;
    bool shutdownCalled = false;
    int renderCount = 0;
    int preferenceUpdateCount = 0;
    int presentCount = 0;
    double animationTime = 0.0;
    double shipDamage = 0.0;
    double miningHeat = 0.0;
    double miningEvaDeathProgress = 0.0;
    double miningExtractionProgress = 0.0;
    double flybyInputY = 0.0;
    double launchCourseOffset = 0.0;
    double launchCourseVelocity = 0.0;
    double launchVelocityX = 0.0;
    double launchVelocityY = 0.0;
    double launchFuelCapacity = 0.0;
    double launchTravelProgress = 0.0;
    double launchDestructionElapsed = 0.0;
    double miningViewChecksum = 0.0;
    int launchAsteroidCount = 0;
    int launchDestinationTier = 0;
    int launchOriginTier = -1;
    bool straylightApproach = false;
    int surfacePushSteps = 0;
    int miningSwarmDepth = -1;
    int miningSwarmWave = 0;
    int miningSwarmEnemies = 0;
    rocket::MaterialInventory surfacePushMaterials;
    std::vector<rocket::MiningCellMaterial> surfacePushRewardMarkers;
    std::vector<int> surfacePushRewardDepthOffsets;
    std::vector<rocket::MiningCellMaterial> surfacePushForecastMarkers;
    std::vector<int> surfacePushForecastDepthOffsets;
    rocket::Screen screen = rocket::Screen::Hangar;
    bool titleScreen = false;
    double sceneFadeToBlack = 0.0;
    bool launchManualControlsEnabled = false;
    bool launchHeatEnabled = false;
    bool launchAsteroidsEnabled = false;
    bool launchFrontierTransfer = false;
    bool launchReturningHome = false;
    bool launchDestructionActive = false;
    rocket::LaunchFailureCause launchDestructionCause = rocket::LaunchFailureCause::None;
    bool launchLandingAuthorized = false;
    bool launchLandingLocalFrame = false;
    rocket::LaunchFailureCause lastLaunchFailureCause = rocket::LaunchFailureCause::None;
    bool miningViewsObserved = false;
    bool miningViewsValid = false;
    bool miningSwarmActive = false;
    bool miningSwarmAlert = false;
    bool miningOperatorRigTethered = false;
    bool miningEvaDeathActive = false;
    bool miningExtractionActive = false;
};

class FakeUi final : public rocket::IGameUi {
public:
    bool initialize(ActionHandler handler) override { actionHandler = std::move(handler); return true; }
    void setPanelPresentation(const rocket::PanelDocumentPresentation& value) override
    {
        presentation = value;
        html = value.contentMarkup;
        for (const rocket::ModalPresentation& modal : value.modals) {
            html += "<template data-modal=\"" + modal.id + "\"";
            if (modal.autoOpen) {
                html += " data-auto-modal=\"1\" data-modal-dismissible=\"";
                html += modal.dismissible ? "1" : "0";
                html += "\" data-modal-close-action=\"" + modal.closeAction
                    + "\" data-title=\"" + modal.title + "\"";
            } else {
                html += " data-title=\"" + modal.title + "\"";
                if (!modal.dismissible) {
                    html += " data-modal-dismissible=\"0\"";
                }
            }
            if (!modal.showClose) {
                html += " data-modal-hide-close=\"1\"";
            }
            html += ">" + modal.bodyMarkup + "</template>";
        }
        ++panelSetCount;
    }
    void setRealtimeHudState(const rocket::RealtimeHudState& value) override
    {
        hud = value;
        ++hudSetCount;
    }
    void render() override
    {
        ++renderCount;
        if (renderTimingHook) renderTimingHook();
    }
    bool mouseMove(int, int) override { return false; }
    bool mouseDown(int, int, int) override { return false; }
    bool mouseUp(int, int, int) override { return false; }
    bool mouseWheel(int, int, double) override { return false; }
    bool hitTest(int, int) const override { return false; }
    bool navigate(rocket::UiDirection direction) override
    {
        lastNavigation = direction;
        return navigateResult;
    }
    bool activateFocused() override
    {
        ++activateFocusedCount;
        return activateFocusedResult;
    }
    bool cancel() override
    {
        ++cancelCount;
        if (cancelClosesModal) {
            modalOpenValue = false;
        }
        return cancelResult;
    }
    bool scroll(float) override { return true; }
    bool modalOpen() const override { return modalOpenValue; }
    void setControllerPresentation(bool, rocket::ControllerFamily) override {}
    void setControllerFocusVisible(bool) override {}
    void setControllerResumeBlocked(bool, bool) override {}
    std::string focusedId() const override { return focusedIdValue; }
    void requestFocus(std::string_view id) override { requestedFocusId = std::string(id); }
    void openModal(const std::string& id) override
    {
        modalOpenValue = true;
        lastOpenedModal = id;
        ++openModalCount;
    }
    void closeModal() override
    {
        modalOpenValue = false;
        ++closeModalCount;
    }
    void dispatchAction(const std::string& action) override { if (actionHandler) actionHandler(action); }
    void refresh() override {}
    bool activateButtonLabel(const std::string&) override { return false; }
    void setPerformanceStats(const rocket::PerformanceStats& stats, bool visible) override
    {
        lastPerformanceStats = stats;
        performanceStatsVisible = visible;
        ++performanceStatsSetCount;
        if (visible && performanceStatsTimingHook) performanceStatsTimingHook();
    }
    void shutdown() override { shutdownCalled = true; }
    ActionHandler actionHandler;
    rocket::PanelDocumentPresentation presentation;
    std::string html;
    bool shutdownCalled = false;
    int renderCount = 0;
    int panelSetCount = 0;
    int hudSetCount = 0;
    int performanceStatsSetCount = 0;
    bool performanceStatsVisible = false;
    bool navigateResult = true;
    bool activateFocusedResult = true;
    bool cancelResult = true;
    bool cancelClosesModal = true;
    bool modalOpenValue = false;
    int activateFocusedCount = 0;
    int cancelCount = 0;
    int openModalCount = 0;
    int closeModalCount = 0;
    std::string lastOpenedModal;
    rocket::UiDirection lastNavigation = rocket::UiDirection::Up;
    std::string focusedIdValue = "action:primary";
    std::string requestedFocusId;
    rocket::RealtimeHudState hud;
    rocket::PerformanceStats lastPerformanceStats;
    std::function<void()> renderTimingHook;
    std::function<void()> performanceStatsTimingHook;
};

class FakeUiBridge final : public rocket::IUiBridge {
public:
    void setUiHostContext(const rocket::UiHostContext& value) override
    {
        hostContext = value;
        ++panelSetCount;
    }
    void setRmlUiEnabled(bool enabled) override
    {
        rmlUiEnabled = enabled;
        ++rmlUiEnabledSetCount;
    }
    void setModalOpen(bool open) override
    {
        modalOpen = open;
        ++modalOpenSetCount;
    }
    void setControllerPresentation(bool active, rocket::ControllerFamily family) override
    {
        controllerPresentationActive = active;
        controllerFamily = family;
    }
    void setControllerFocusVisible(bool visible) override { controllerFocusVisible = visible; }
    void setControllerResumeBlocked(bool blocked, bool connected) override
    {
        controllerResumeBlocked = blocked;
        controllerConnected = connected;
    }
    void preferencesChanged(const rocket::AppPreferences& value) override
    {
        lastPreferences = value;
        ++preferenceUpdateCount;
    }
    rocket::UiHostContext hostContext;
    int panelSetCount = 0;
    int preferenceUpdateCount = 0;
    int rmlUiEnabledSetCount = 0;
    int modalOpenSetCount = 0;
    bool rmlUiEnabled = false;
    bool modalOpen = false;
    bool controllerPresentationActive = false;
    bool controllerFocusVisible = false;
    bool controllerResumeBlocked = false;
    bool controllerConnected = false;
    rocket::ControllerFamily controllerFamily = rocket::ControllerFamily::Generic;
    rocket::AppPreferences lastPreferences;
};

class NullRmlRenderInterface final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(
        Rml::Span<const Rml::Vertex>,
        Rml::Span<const int>) override
    {
        return nextHandle_++;
    }
    void RenderGeometry(
        Rml::CompiledGeometryHandle,
        Rml::Vector2f,
        Rml::TextureHandle) override
    {
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String&) override
    {
        dimensions = {1, 1};
        return nextHandle_++;
    }
    Rml::TextureHandle GenerateTexture(
        Rml::Span<const Rml::byte>,
        Rml::Vector2i) override
    {
        return nextHandle_++;
    }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}

private:
    std::uintptr_t nextHandle_ = 1;
};

class NullRmlRenderHost final : public rocket::IRmlRenderHost {
public:
    bool initialize() override { return true; }
    Rml::RenderInterface& renderInterface() override { return renderer_; }
    void setViewport(const rocket::RmlRenderViewport&) override {}
    void setRootClip(const rocket::RmlRenderClip&) override {}
    bool beginFrame() override { return true; }
    void endFrame() override {}
    rocket::UiDiagnostics diagnostics() const override { return {}; }
    void shutdown() override {}

private:
    NullRmlRenderInterface renderer_;
};

std::string repositoryRootForRmlTests()
{
    const std::filesystem::path sourceCandidate =
        std::filesystem::path(__FILE__).parent_path().parent_path();
    if (std::filesystem::exists(
            sourceCandidate / "assets" / "fonts" / "SourceCodePro-Regular.ttf")) {
        return sourceCandidate.string();
    }

    std::filesystem::path candidate = std::filesystem::current_path();
    for (int depth = 0; depth < 5; ++depth) {
        if (std::filesystem::exists(candidate / "assets" / "fonts" / "SourceCodePro-Regular.ttf")) {
            return candidate.string();
        }
        candidate = candidate.parent_path();
    }
    assert(false && "RmlUi tests could not locate the repository font assets");
    return {};
}

struct AppFixture {
    FakeSaveStore saves;
    FakePreferenceStore preferences;
    FakeHost host;
    FakeController controllers;
    FakeTextureSource textures;
    FakeRenderer renderer;
    FakeUi ui;
    FakeUiBridge bridge;
    rocket::AppServices services {saves, preferences, host, controllers, textures, renderer, ui, bridge};
    rocket::GameRunner runner {services};
};

void completeTitleLaunch(AppFixture& fixture)
{
    fixture.runner.resetFrameClock();
    for (int frame = 0; frame < 11; ++frame) {
        fixture.host.now += 0.25;
        fixture.runner.frame();
    }
    fixture.host.now += 1.0 / 120.0;
    fixture.runner.frame();
}

void advanceSceneHandoff(AppFixture& fixture)
{
    // Four quarter-second frames reach opaque black; the fifth performs the
    // state handoff and starts the shared quarter-second fade-in.
    for (int frame = 0; frame < 5; ++frame) {
        fixture.host.now += 0.25;
        fixture.runner.frame();
    }
}

std::string activeMiningSave(double drillHeat)
{
    const rocket::ContentCatalog catalog = rocket::createDefaultContent();
    rocket::GameState state = rocket::createNewGame(catalog, 0xA11CEULL);
    state.run.destinationIndex = 2;
    rocket::startSurfaceExpedition(state, catalog);
    state.run.planetaryExpedition.miningSitePrepared = true;
    assert(rocket::startMiningRun(state, catalog).applied);
    state.run.mining.drillHeat = drillHeat;
    state.run.shipDamage = 37;
    return rocket::serializeSaveData(rocket::captureSaveData(state));
}

std::string fullShipMiningSave(bool hasOverflowOre)
{
    const rocket::ContentCatalog catalog = rocket::createDefaultContent();
    rocket::GameState state = rocket::createNewGame(catalog, 0xF011ULL);
    state.run.destinationIndex = 2;
    rocket::startSurfaceExpedition(state, catalog);
    state.run.planetaryExpedition.miningSitePrepared = true;
    assert(rocket::startMiningRun(state, catalog).applied);
    const int holdCapacity = rocket::shipHoldCapacity(state, catalog);
    if (state.run.expedition.travelInitialized) {
        state.run.expedition.cargo.materials.common = holdCapacity;
    } else {
        state.meta.materials.common = holdCapacity;
    }
    state.incomingMessages = {};
    if (hasOverflowOre) {
        state.run.mining.temporaryMaterials.common = 1;
        state.run.mining.cargo = 1;
    }
    return rocket::serializeSaveData(rocket::captureSaveData(state));
}

std::string readyMiningDepartureSave()
{
    const rocket::ContentCatalog catalog = rocket::createDefaultContent();
    rocket::GameState state = rocket::createNewGame(catalog, 0xD3A471ULL);
    state.run.destinationIndex = 2;
    rocket::startSurfaceExpedition(state, catalog);
    state.run.planetaryExpedition.miningSitePrepared = true;
    assert(rocket::startMiningRun(state, catalog).applied);
    rocket::MiningRunState& mining = state.run.mining;
    mining.droneX = mining.returnZoneX;
    mining.droneY = mining.returnZoneY;
    mining.temporaryMaterials.rare = 1;
    assert(rocket::miningAtReturnZone(mining));
    return rocket::serializeSaveData(rocket::captureSaveData(state));
}

std::string completedMoonReturnSave()
{
    const rocket::ContentCatalog catalog = rocket::createDefaultContent();
    rocket::GameState state = rocket::createNewGame(catalog, 0xE4174ULL);
    assert(rocket::initializeLiveExpedition(state, catalog));
    assert(rocket::performScenarioAction(state, catalog,
        rocket::content::scenario::lunarProspector, "briefing",
        rocket::ScenarioActionKind::AcknowledgeBriefing).applied);
    rocket::recordScenarioEvent(state, catalog, {rocket::ScenarioEventKind::SafeMaterialDelivered,
        rocket::content::scenario::lunarProspector, "delivery", "moon", "common", 20, 0});
    rocket::recordScenarioEvent(state, catalog, {rocket::ScenarioEventKind::ProtectedObjectiveExtracted,
        rocket::content::scenario::lunarProspector, "anomaly", "moon",
        rocket::content::miningSite::lunarAnomalyCrevice, 1, 0});
    assert(rocket::performScenarioAction(state, catalog,
        rocket::content::scenario::lunarProspector, "anomaly",
        rocket::ScenarioActionKind::ClaimReward).applied);
    auto& expedition = state.run.expedition;
    auto& flight = state.run.flight;
    expedition.openingInitialized = true;
    expedition.departureHistoryKnown = true;
    expedition.departureCount = 2;
    expedition.active = true;
    expedition.location = {"solar", "moon", rocket::CoordinateFrame::Body,
        {1.20, 0.0}, {0.03, 0.02}, 0.2, {}};
    expedition.decision = {};
    expedition.progression.pendingRunUpgradeChoices = 0;
    expedition.progression.runUpgradeOfferPending = false;
    expedition.progression.runUpgradeOfferCount = 0;
    expedition.progression.runUpgradeReturnScreen = rocket::Screen::Flight;
    flight.active = flight.physicalFlight = true;
    flight.mode = rocket::FlightMode::Orbit;
    flight.phase = rocket::FlightPhase::Transfer;
    flight.failureCause = rocket::LaunchFailureCause::None;
    rocket::restoreSystemLocation(expedition.location, flight);
    state.screen = rocket::Screen::Flight;
    state.meta.campaignIntroductionAcknowledged = true;
    state.incomingMessages = {};
    return rocket::serializeSaveData(rocket::captureSaveData(state));
}

rocket::GameState readyLiveMarsMissionState(const rocket::ContentCatalog& catalog)
{
    rocket::GameState state = rocket::createNewGame(catalog, 0xD0C4A25ULL);
    const auto moon = rocket::deserializeSaveData(completedMoonReturnSave());
    assert(moon);
    rocket::restoreSaveData(state, catalog, *moon);
    assert(rocket::performScenarioAction(state, catalog,
        rocket::content::scenario::marsBayExpansion, "briefing",
        rocket::ScenarioActionKind::AcknowledgeBriefing).applied);
    assert(rocket::recordScenarioEvent(state, catalog,
        {rocket::ScenarioEventKind::SafeMaterialDelivered,
         rocket::content::scenario::marsBayExpansion, "delivery", "mars", "common", 8, 0}));
    assert(rocket::recordScenarioEvent(state, catalog,
        {rocket::ScenarioEventKind::ArtifactRecovered,
         rocket::content::scenario::marsBayExpansion, "artifact", "mars",
         rocket::content::protectedObjective::marsSignalArtifact, 1, 0}));
    state.run.destinationIndex = 2;
    state.run.expedition.location = {"solar", "mars", rocket::CoordinateFrame::Body,
        {1.2, 0}, {0.03, 0.02}, 0.2, {}};
    rocket::restoreSystemLocation(state.run.expedition.location, state.run.flight);
    state.run.expedition.course.targetBodyId = "mars";
    state.run.expedition.coursePlayerSelected = false;
    state.run.expedition.progression.pendingRunUpgradeChoices = 0;
    state.run.expedition.progression.runUpgradeOfferPending = false;
    state.incomingMessages = {};
    return state;
}

void assertNoLegacyRecoveryActions(const rocket::PanelDocumentPresentation& panel)
{
    for (const std::string_view action : {
             "prepare_launch", "attempt_frontier", "open_jupiter_refit",
             "acknowledge_jupiter_window", "extract_surface", "ark_jump"}) {
        assert(panel.contentMarkup.find("data-rr-action=\"" + std::string(action) + "\"") == std::string::npos);
    }
    assert(panel.contentMarkup.find("Return to Surface Ops") == std::string::npos);
}

void liveMissionClaimPresentationAndDockRecovery()
{
    const auto catalog = rocket::createDefaultContent();
    const std::string claimAction = rocket::ui::actions::scenarioAction(
        rocket::content::scenario::marsBayExpansion, "artifact",
        static_cast<int>(rocket::ScenarioActionKind::ClaimReward));
    for (const auto screen : {rocket::Screen::Mining, rocket::Screen::Flight, rocket::Screen::Hangar}) {
        auto state = std::make_unique<rocket::GameState>(readyLiveMarsMissionState(catalog));
        if (screen == rocket::Screen::Mining) {
            rocket::startSurfaceExpedition(*state, catalog);
            state->run.planetaryExpedition.miningSitePrepared = true;
            assert(rocket::startMiningRun(*state, catalog).applied);
            state->run.mining.bodyId = "mars";
        } else if (screen == rocket::Screen::Hangar) {
            const auto* earth = rocket::systemBody(rocket::solarSystemDefinition(), "earth");
            assert(earth);
            const auto dock = rocket::systemDockPosition(*earth);
            state->run.expedition.location = {"solar", "earth", rocket::CoordinateFrame::System,
                dock, earth->velocity, 0, "earth.dock"};
            state->run.expedition.active = false;
            state->run.flight.active = false;
            rocket::restoreSystemLocation(state->run.expedition.location, state->run.flight);
        }
        state->screen = screen;
        const auto model = rocket::expeditionFlightModel(*state, catalog);
        rocket::PanelRenderContext context{*state, catalog, model, model};
        context.flightArmed = true;
        context.firstTimeIntroductionsEnabled = false;
        const auto panel = rocket::buildGamePanelPresentation(context);
        const auto claim = std::find_if(panel.modals.begin(), panel.modals.end(), [&](const auto& modal) {
            return modal.autoOpen && modal.bodyMarkup.find(claimAction) != std::string::npos;
        });
        assert(claim != panel.modals.end());
        assert(!claim->dismissible && !claim->showClose);
        assert(claim->title == "INCOMING MESSAGE");
        assert(claim->bodyMarkup.find("incoming-message-portrait") != std::string::npos);
        assert(claim->bodyMarkup.find("Claim mission reward") != std::string::npos);
        assertNoLegacyRecoveryActions(panel);
        assert(rocket::performScenarioAction(*state, catalog,
            rocket::content::scenario::marsBayExpansion, "artifact",
            rocket::ScenarioActionKind::ClaimReward).applied);
        const auto claimed = rocket::buildGamePanelPresentation(context);
        assert(std::none_of(claimed.modals.begin(), claimed.modals.end(), [&](const auto& modal) {
            return modal.autoOpen && modal.bodyMarkup.find(claimAction) != std::string::npos;
        }));
        assert(claimed.contentMarkup.find("COMPLETE") != std::string::npos);
        assert(rocket::nextSolarMission(*state, catalog)->bodyId == "io");
    }

    auto state = std::make_unique<rocket::GameState>(readyLiveMarsMissionState(catalog));
    assert(rocket::recoverExpedition(*state, rocket::solarSystemDefinition()) == rocket::ExpeditionResult::Applied);
    state->run.expedition.progression.pendingGraftConflicts.push_back({0,
        {0, rocket::content::drone::miningDrone, rocket::DroneModuleKind::CombatDrill},
        {0, rocket::content::drone::miningDrone, rocket::DroneModuleKind::SpectrumFilter}});
    const auto model = rocket::expeditionFlightModel(*state, catalog);
    rocket::PanelRenderContext context{*state, catalog, model, model};
    context.firstTimeIntroductionsEnabled = false;
    const auto panel = rocket::buildGamePanelPresentation(context);
    const auto graft = std::find_if(panel.modals.begin(), panel.modals.end(), [](const auto& modal) {
        return modal.id == "graft_conflict";
    });
    assert(graft != panel.modals.end() && graft->autoOpen && !graft->dismissible);
    assert(panel.contentMarkup.find("data-rr-action=\"expedition:depart\"") != std::string::npos);
    assertNoLegacyRecoveryActions(panel);
}

void liveWaypointModalActionOwnership()
{
    const auto catalog = rocket::createDefaultContent();
    auto state = std::make_unique<rocket::GameState>(rocket::createNewGame(catalog, 0x4A9D15ULL));
    assert(rocket::initializeLiveExpedition(*state, catalog));
    const auto model = rocket::expeditionFlightModel(*state, catalog);
    rocket::PanelRenderContext context{*state, catalog, model, model};
    context.firstTimeIntroductionsEnabled = false;
    FakePreferenceStore preferences;
    FakeHost host;
    FakeUiBridge bridge;
    NullRmlRenderHost renderHost;
    rocket::GameRmlUi ui(preferences, host, bridge, renderHost, repositoryRootForRmlTests());
    std::string dispatched;
    // An application may reject an action without changing presentation. The
    // native dispatcher must not dismiss the player's map before that decision.
    assert(ui.initialize([&](const std::string& action) { dispatched = action; }));
    ui.setPanelPresentation(rocket::buildGamePanelPresentation(context));
    ui.openModal("map");
    assert(ui.modalOpen());
    ui.dispatchAction("expedition:preview:moon");
    assert(ui.modalOpen());
    ui.dispatchAction("expedition:plot:straylight");
    assert(dispatched == "expedition:plot:straylight" && ui.modalOpen());
    ui.requestFocus("action:expedition:plot:moon");
    ui.refresh();
    assert(ui.focusedId() == "action:expedition:plot:moon");
    assert(ui.activateFocused());
    assert(dispatched == "expedition:plot:moon" && ui.modalOpen());
    ui.closeModal();
    assert(!ui.modalOpen());
    ui.shutdown();
}

void liveShipRecoveryAppFlows()
{
    const auto catalog = rocket::createDefaultContent();
    {
        auto opening = std::make_unique<rocket::GameState>(rocket::createNewGame(catalog, 0x7E721ULL));
        assert(rocket::initializeLiveExpedition(*opening, catalog));
        assert(rocket::beginEarthOpening(*opening, catalog));
        assert(rocket::acknowledgeIncomingMessage(opening->incomingMessages, "campaign.lunar_approach"));
        assert(rocket::launchEarthOpening(*opening, catalog) == rocket::ExpeditionResult::Applied);
        opening->run.flight.failureCause = rocket::LaunchFailureCause::HullBreach;
        assert(rocket::recoverExpedition(*opening, rocket::solarSystemDefinition()) == rocket::ExpeditionResult::Applied);
        AppFixture fixture;
        fixture.saves.value = rocket::serializeSaveData(rocket::captureSaveData(*opening));
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("continue_game");
        completeTitleLaunch(fixture);
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Flight));
        assert(fixture.ui.html.find("expedition:retry_opening") != std::string::npos);
        assert(fixture.ui.presentation.contentMarkup.find("ORBITAL DOCK") == std::string::npos);
        fixture.ui.dispatchAction("expedition:retry_opening");
        const auto retry = rocket::deserializeSaveData(fixture.saves.value);
        assert(retry && retry->flight.active && retry->expedition.active);
        assert(retry->flight.failureCause == rocket::LaunchFailureCause::None);
        assert(retry->expedition.decision.pendingId.empty());
        assert(fixture.ui.html.find("expedition:retry_opening") == std::string::npos);
        assertNoLegacyRecoveryActions(fixture.ui.presentation);
        fixture.runner.shutdown();
    }
    {
        AppFixture fixture;
        fixture.saves.value = completedMoonReturnSave();
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("continue_game");
        completeTitleLaunch(fixture);
        fixture.ui.dispatchAction("expedition:abandon");
        fixture.ui.dispatchAction("expedition:confirm_abandon");
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Hangar));
        const auto recovered = rocket::deserializeSaveData(fixture.saves.value);
        assert(recovered && rocket::operationalHomeDocked(recovered->expedition));
        assert(!recovered->flight.active && recovered->expedition.wrecks.size() == 1);
        assertNoLegacyRecoveryActions(fixture.ui.presentation);
        assert(fixture.ui.presentation.contentMarkup.find("data-rr-action=\"expedition:depart\"") != std::string::npos);
        fixture.ui.dispatchAction("expedition:depart");
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Flight));
        const auto departed = rocket::deserializeSaveData(fixture.saves.value);
        assert(departed && departed->expedition.undockReady);
        fixture.runner.shutdown();
    }
}

rocket::GameState readyJupiterDepartureState(const rocket::ContentCatalog& catalog)
{
    rocket::GameState state = rocket::createNewGame(catalog, 0x5A7A2EULL);
    const auto destination = std::find_if(
        catalog.destinations.begin(),
        catalog.destinations.end(),
        [](const rocket::Destination& candidate) {
            return candidate.id == rocket::content::destination::jupiter;
        });
    assert(destination != catalog.destinations.end());
    state.run.destinationIndex = static_cast<int>(
        std::distance(catalog.destinations.begin(), destination));
    state.meta.furthestTier = destination->tier;
    state.meta.unlockKeys.push_back(rocket::content::unlock::routeJupiter);
    state.meta.unlockKeys.push_back("outer_transfer_ready");
    rocket::startSurfaceExpedition(state, catalog);
    state.run.approach = {true, rocket::content::destination::jupiter};
    state.screen = rocket::Screen::ArrivalOps;
    return state;
}

std::string readyJupiterDepartureSave()
{
    const rocket::ContentCatalog catalog = rocket::createDefaultContent();
    return rocket::serializeSaveData(
        rocket::captureSaveData(readyJupiterDepartureState(catalog)));
}

rocket::GameState confirmedJupiterDepartureState(const rocket::ContentCatalog& catalog)
{
    rocket::GameState state = readyJupiterDepartureState(catalog);
    state.meta.launchLessons.stage = rocket::LaunchTrainingStage::Complete;
    state.meta.launchUpgrades.fuelTanks = 3;
    assert(rocket::performScenarioAction(
               state,
               catalog,
               rocket::content::scenario::outerTransfer,
               "briefing",
               rocket::ScenarioActionKind::AcknowledgeBriefing).applied);
    rocket::syncLaunchConfig(state, catalog);
    return state;
}

std::string confirmedJupiterDepartureSave()
{
    const rocket::ContentCatalog catalog = rocket::createDefaultContent();
    rocket::GameState state = confirmedJupiterDepartureState(catalog);
    // Active realtime Flyby state is intentionally not part of the campaign
    // save. Persist the equivalent ReadyToClaim Hangar state; the in-memory
    // RmlUi test below covers the result-screen binding itself.
    state.run.expedition.progression.expeditionLevel = 1;
    state.run.expedition.progression.expeditionExperience = 0.0;
    state.run.expedition.progression.pendingRunUpgradeChoices = 0;
    state.run.expedition.progression.runUpgradeOfferPending = false;
    state.run.expedition.progression.runUpgradeOfferCount = 0;
    state.screen = rocket::Screen::Hangar;
    return rocket::serializeSaveData(rocket::captureSaveData(state));
}

rocket::GameState uranusVectorReadyState(const rocket::ContentCatalog& catalog)
{
    rocket::GameState state = rocket::createNewGame(catalog, 0x6E7077ULL);
    state.meta.launchLessons.stage = rocket::LaunchTrainingStage::Complete;
    state.meta.unlockKeys.push_back(rocket::content::unlock::routeUranus);
    state.meta.artifacts.push_back(
        {"uranus_route_artifact", rocket::content::destination::uranus, false});
    state.run.destinationIndex = 5;
    state.meta.furthestTier = 5;
    rocket::ensureScenarioInstances(state, catalog);
    assert(rocket::performScenarioAction(
               state,
               catalog,
               rocket::content::scenario::uranusDeparture,
               "briefing",
               rocket::ScenarioActionKind::AcknowledgeBriefing).applied);
    rocket::ensureScenarioInstances(state, catalog);
    // Flight Data is the durable progress ledger produced by the arrival
    // activities. The scenario event mirrors that ledger; it does not grant
    // progress on its own.
    state.run.frontierReadiness = 2;
    assert(rocket::recordScenarioEvent(
        state,
        catalog,
        {rocket::ScenarioEventKind::FlightDataBanked,
         {},
         {},
         rocket::content::destination::uranus,
         rocket::content::destination::neptune,
         2,
         0}));
    state.screen = rocket::Screen::Hangar;
    rocket::syncLaunchConfig(state, catalog);
    return state;
}

std::string uranusVectorReadySave()
{
    const rocket::ContentCatalog catalog = rocket::createDefaultContent();
    return rocket::serializeSaveData(rocket::captureSaveData(
        uranusVectorReadyState(catalog)));
}

int jupiterDestinationIndex()
{
    const rocket::ContentCatalog catalog = rocket::createDefaultContent();
    const auto jupiter = std::find_if(
        catalog.destinations.begin(),
        catalog.destinations.end(),
        [](const rocket::Destination& destination) {
            return destination.id == rocket::content::destination::jupiter;
        });
    assert(jupiter != catalog.destinations.end());
    return static_cast<int>(std::distance(catalog.destinations.begin(), jupiter));
}

void assertJupiterSaturnRoutePersisted(std::string_view serializedSave)
{
    const std::optional<rocket::SaveData> save =
        rocket::deserializeSaveData(serializedSave);
    assert(save.has_value());
    assert(save->destinationIndex == jupiterDestinationIndex());
    assert(save->routeTransit.intent == rocket::RouteTransitIntent::Outbound);
    assert(save->routeTransit.routeLinkId == rocket::content::routeLink::jupiterSaturn);
    assert(save->routeTransit.originDestinationId == rocket::content::destination::jupiter);
    assert(save->routeTransit.targetDestinationId == rocket::content::destination::saturn);
}

std::string evaDeathMiningSave()
{
    const rocket::ContentCatalog catalog = rocket::createDefaultContent();
    rocket::GameState state = rocket::createNewGame(catalog, 0xDEA7E7AULL);
    state.run.destinationIndex = 2;
    rocket::startSurfaceExpedition(state, catalog);
    state.run.planetaryExpedition.miningSitePrepared = true;
    assert(rocket::startMiningRun(state, catalog).applied);
    rocket::MiningRunState& mining = state.run.mining;
    mining.operatorMode = rocket::MiningOperatorMode::Jetpack;
    mining.operatorPresent = true;
    mining.operatorX = mining.droneX;
    mining.operatorY = mining.droneY;
    mining.operatorIntegrity = 0.0;
    mining.failurePending = false;
    return rocket::serializeSaveData(rocket::captureSaveData(state));
}

std::string liveEvaDeathMiningSave()
{
    const auto catalog = rocket::createDefaultContent();
    auto state = std::make_unique<rocket::GameState>(rocket::createNewGame(catalog, 0xDEA741FEULL));
    assert(rocket::initializeLiveExpedition(*state, catalog));
    state->run.destinationIndex = 2;
    auto& e = state->run.expedition;
    e.active = true;
    e.openingInitialized = true;
    e.departureCount = 2;
    const auto* mars = rocket::systemBody(rocket::solarSystemDefinition(), "mars");
    assert(mars);
    e.location = {"solar", "mars", rocket::CoordinateFrame::Body,
        {0, 0.2}, {}, 0, mars->siteId + ":zone_1"};
    auto& flight = state->run.flight;
    rocket::restoreSystemLocation(e.location, flight);
    flight.physicalFlight = true;
    flight.active = false;
    flight.mode = rocket::FlightMode::Landing;
    flight.phase = rocket::FlightPhase::Landed;
    flight.landing.siteCommitted = true;
    rocket::startSurfaceExpedition(*state, catalog);
    state->run.planetaryExpedition.miningSitePrepared = true;
    assert(rocket::startMiningRun(*state, catalog).applied);
    auto& mining = state->run.mining;
    mining.bodyId = "mars";
    flight.landing.padGridX = flight.landing.touchdownGridX = mining.returnZoneX;
    flight.landing.padGridY = flight.landing.touchdownGridY = mining.returnZoneY;
    mining.operatorMode = rocket::MiningOperatorMode::Jetpack;
    mining.operatorPresent = true;
    mining.operatorX = mining.droneX;
    mining.operatorY = mining.droneY;
    mining.operatorIntegrity = 0;
    mining.temporaryMaterials.common = mining.cargo = 3;
    mining.stowedMaterials.common = mining.stowedCargo = e.cargo.materials.common = 4;
    e.progression.expeditionLevel = 2;
    e.progression.expeditionExperience = 4;
    e.progression.runRigUpgradeRanks = {{rocket::content::surfaceUpgrade::highTorqueMotor, 1}};
    e.progression.pendingRunUpgradeChoices = 0;
    e.progression.runUpgradeOfferPending = false;
    state->incomingMessages = {};
    rocket::storeVisitedSite(*state, e.location.siteId);
    return rocket::serializeSaveData(rocket::captureSaveData(*state));
}

std::string disabledRigEvaTowSave()
{
    const rocket::ContentCatalog catalog = rocket::createDefaultContent();
    rocket::GameState state = rocket::createNewGame(catalog, 0xE7A70FULL);
    state.run.destinationIndex = 2;
    rocket::startSurfaceExpedition(state, catalog);
    state.run.planetaryExpedition.miningSitePrepared = true;
    assert(rocket::startMiningRun(state, catalog).applied);
    rocket::MiningRunState& mining = state.run.mining;
    mining.operatorMode = rocket::MiningOperatorMode::Jetpack;
    mining.operatorPresent = true;
    mining.rigDisabled = true;
    mining.rigDepthZone = mining.depthZone;
    mining.operatorRigTethered = true;
    return rocket::serializeSaveData(rocket::captureSaveData(state));
}

std::string levelUpExpeditionSave()
{
    const rocket::ContentCatalog catalog = rocket::createDefaultContent();
    rocket::GameState state = rocket::createNewGame(catalog, 0x1E7E1ULL);
    state.run.destinationIndex = 2;
    rocket::startSurfaceExpedition(state, catalog);
    state.screen = rocket::Screen::SurfaceExpedition;
    const rocket::ExpeditionExperienceAward award = rocket::awardExpeditionExperience(
        state,
        rocket::expeditionExperienceThreshold(1) + rocket::expeditionExperienceThreshold(2),
        rocket::Screen::SurfaceExpedition);
    assert(award.levelsGained == 2);
    assert(state.run.expedition.progression.pendingRunUpgradeChoices == 2);
    return rocket::serializeSaveData(rocket::captureSaveData(state));
}

std::string freshSurfaceExpeditionSave()
{
    const rocket::ContentCatalog catalog = rocket::createDefaultContent();
    rocket::GameState state = rocket::createNewGame(catalog, 0x51A7EULL);
    assert(rocket::performScenarioAction(
               state,
               catalog,
               rocket::content::scenario::lunarProspector,
               "briefing",
               rocket::ScenarioActionKind::AcknowledgeBriefing)
               .applied);
    assert(rocket::recordScenarioEvent(
        state,
        catalog,
        {rocket::ScenarioEventKind::SafeMaterialDelivered,
         {},
         {},
         rocket::content::destination::moon,
         "common",
         rocket::tuning::research::prospectorCommonOreGoal,
         0}));
    assert(rocket::recordScenarioEvent(
        state,
        catalog,
        {rocket::ScenarioEventKind::ProtectedObjectiveExtracted,
         rocket::content::scenario::lunarProspector,
         "anomaly",
         rocket::content::destination::moon,
         rocket::content::miningSite::lunarAnomalyCrevice,
         1,
         0}));
    assert(rocket::performScenarioAction(
               state,
               catalog,
               rocket::content::scenario::lunarProspector,
               "anomaly",
               rocket::ScenarioActionKind::ClaimReward)
               .applied);
    state.run.destinationIndex = 2;
    state.meta.furthestTier = 2;
    rocket::startSurfaceExpedition(state, catalog);
    rocket::ui::briefings::acknowledge(
        state.meta.acknowledgedActivityBriefingIds,
        rocket::ui::briefings::mining);
    state.run.expedition.progression.pendingRunUpgradeChoices = 0;
    state.run.expedition.progression.runUpgradeOfferPending = false;
    state.run.expedition.progression.runUpgradeOffers = {};
    state.screen = rocket::Screen::SurfaceExpedition;
    return rocket::serializeSaveData(rocket::captureSaveData(state));
}

std::string firstSurfaceTutorialSave()
{
    const rocket::ContentCatalog catalog = rocket::createDefaultContent();
    rocket::GameState state = rocket::createNewGame(catalog, 0x5A7FACEULL);
    state.run.destinationIndex = 2;
    rocket::startSurfaceExpedition(state, catalog);
    state.screen = rocket::Screen::SurfaceExpedition;
    return rocket::serializeSaveData(rocket::captureSaveData(state));
}

std::string activeDroneBaySurfaceExpeditionSave()
{
    const rocket::ContentCatalog catalog = rocket::createDefaultContent();
    rocket::GameState state = rocket::createNewGame(catalog, 0xD20E0F5ULL);
    state.run.destinationIndex = 2;
    rocket::startSurfaceExpedition(state, catalog);
    state.meta.unlockKeys.push_back(rocket::content::unlock::droneBay);
    state.meta.droneBaySlots = 2;
    state.meta.materials.common = 20;
    rocket::ensureDroneBayState(state, catalog);
    rocket::ui::briefings::acknowledge(
        state.meta.acknowledgedActivityBriefingIds,
        rocket::ui::briefings::miniDrones);
    state.screen = rocket::Screen::SurfaceExpedition;
    return rocket::serializeSaveData(rocket::captureSaveData(state));
}

std::string readyProspectorClaimSave()
{
    const rocket::ContentCatalog catalog = rocket::createDefaultContent();
    rocket::GameState state = rocket::createNewGame(catalog, 0xF002ULL);
    state.run.destinationIndex = 1;
    state.meta.furthestTier = 1;
    state.meta.launchLessons.stage = rocket::LaunchTrainingStage::ThermalManagement;
    state.meta.launchUpgrades.fuelTanks = 1;
    state.meta.launchUpgrades.flightControls = 1;
    state.run.refitEntitled = true;
    state.run.credits = 22.0;
    assert(rocket::performScenarioAction(
               state,
               catalog,
               rocket::content::scenario::lunarProspector,
               "briefing",
               rocket::ScenarioActionKind::AcknowledgeBriefing)
               .applied);
    assert(rocket::recordScenarioEvent(
        state,
        catalog,
        {rocket::ScenarioEventKind::SafeMaterialDelivered,
         {},
         {},
         rocket::content::destination::moon,
         "common",
         rocket::tuning::research::prospectorCommonOreGoal,
         0}));
    rocket::Random rng(0xF002ULL);
    rocket::generateModuleOffers(state, catalog, rng);
    state.run.expedition.progression.pendingRunUpgradeChoices = 0;
    state.run.expedition.progression.runUpgradeOfferPending = false;
    state.run.expedition.progression.runUpgradeOffers = {};
    state.screen = rocket::Screen::Upgrade;
    rocket::syncLaunchConfig(state, catalog);
    return rocket::serializeSaveData(rocket::captureSaveData(state));
}

std::string readyMarsExpansionClaimSave()
{
    const rocket::ContentCatalog catalog = rocket::createDefaultContent();
    rocket::GameState state = rocket::createNewGame(catalog, 0xF003ULL);
    state.run.destinationIndex = 2;
    state.meta.furthestTier = 2;
    state.meta.launchLessons.stage = rocket::LaunchTrainingStage::HullIntegrity;
    state.meta.launchUpgrades.fuelTanks = 2;
    state.meta.launchUpgrades.flightControls = 1;
    state.meta.unlockKeys.push_back(rocket::content::unlock::routeMars);
    state.run.refitEntitled = true;
    state.run.credits = 83.0;
    assert(rocket::performScenarioAction(
               state,
               catalog,
               rocket::content::scenario::marsBayExpansion,
               "briefing",
               rocket::ScenarioActionKind::AcknowledgeBriefing)
               .applied);
    assert(rocket::recordScenarioEvent(
        state,
        catalog,
        {rocket::ScenarioEventKind::SafeMaterialDelivered,
         {},
         {},
         rocket::content::destination::mars,
         "common",
         rocket::tuning::research::marsBayCommonOreGoal,
         0}));
    rocket::Random rng(0xF003ULL);
    rocket::generateModuleOffers(state, catalog, rng);
    state.run.expedition.progression.pendingRunUpgradeChoices = 0;
    state.run.expedition.progression.runUpgradeOfferPending = false;
    state.run.expedition.progression.runUpgradeOffers = {};
    state.screen = rocket::Screen::Upgrade;
    rocket::syncLaunchConfig(state, catalog);
    return rocket::serializeSaveData(rocket::captureSaveData(state));
}

std::string activeJupiterSlingshotSave()
{
    const rocket::ContentCatalog catalog = rocket::createDefaultContent();
    rocket::GameState state = rocket::createNewGame(catalog, 0xF004ULL);
    state.run.destinationIndex = 2;
    state.meta.furthestTier = 2;
    state.meta.launchLessons.stage = rocket::LaunchTrainingStage::HullIntegrity;
    state.meta.launchUpgrades.fuelTanks = 2;
    state.meta.launchUpgrades.flightControls = 1;
    state.meta.unlockKeys.push_back(rocket::content::unlock::routeMars);
    state.meta.unlockKeys.push_back(rocket::content::unlock::routeJupiter);
    state.run.pendingTransferAssist = rocket::PendingTransferAssist {
        rocket::content::transferAssist::marsJupiter,
        rocket::content::destination::mars,
        rocket::content::destination::jupiter,
        rocket::FlybyGrade::Good,
        5.0,
        0.20,
        0.35};
    state.screen = rocket::Screen::Hangar;
    rocket::syncLaunchConfig(state, catalog);
    return rocket::serializeSaveData(rocket::captureSaveData(state));
}

std::string pendingStraylightDiscoverySave()
{
    const rocket::ContentCatalog catalog = rocket::createDefaultContent();
    rocket::GameState state = rocket::createNewGame(catalog, 0x57A11A7ULL);
    const auto neptune = std::find_if(
        catalog.destinations.begin(),
        catalog.destinations.end(),
        [](const rocket::Destination& destination) {
            return destination.id == rocket::content::destination::neptune;
        });
    assert(neptune != catalog.destinations.end());
    state.run.destinationIndex = static_cast<int>(
        std::distance(catalog.destinations.begin(), neptune));
    state.meta.furthestTier = 6;
    state.meta.launchLessons.stage = rocket::LaunchTrainingStage::Complete;
    state.meta.unlockKeys.push_back(rocket::content::unlock::routeNeptune);
    assert(rocket::recordScenarioEvent(
        state,
        catalog,
        {rocket::ScenarioEventKind::DestinationReached,
         {},
         {},
         {},
         rocket::content::destination::neptune,
         1,
         0}));
    const rocket::ScenarioActionOutcome discovery = rocket::performScenarioAction(
        state,
        catalog,
        rocket::content::scenario::neptuneDiscovery,
        "arrival",
        rocket::ScenarioActionKind::ClaimReward);
    assert(discovery.applied);
    rocket::scheduleStoryBriefing(
        state,
        discovery.transition.storyBriefing,
        discovery.transition.screen);
    state.screen = rocket::Screen::StoryBriefing;
    rocket::syncLaunchConfig(state, catalog);
    return rocket::serializeSaveData(rocket::captureSaveData(state));
}

void retiredJupiterDepartureBoardCannotResume()
{
    auto fixture = std::make_unique<AppFixture>();
    fixture->saves.value = readyJupiterDepartureSave();
    assert(fixture->runner.initialize());
    fixture->ui.dispatchAction("continue_game");
    completeTitleLaunch(*fixture);
    assert(fixture->runner.app().currentScreen() == static_cast<int>(rocket::Screen::Hangar));

    fixture->ui.dispatchAction(rocket::ui::actions::scenarioAction(
        rocket::content::scenario::outerTransfer,
        "flyby",
        static_cast<int>(rocket::ScenarioActionKind::BeginActivity)));
    assert(fixture->runner.app().currentScreen() == static_cast<int>(rocket::Screen::Hangar));
    fixture->runner.shutdown();
}

void jupiterDepartureConfirmationQueuesSaturn()
{
    auto fixture = std::make_unique<AppFixture>();
    fixture->saves.value = confirmedJupiterDepartureSave();
    assert(fixture->runner.initialize());
    fixture->ui.dispatchAction("continue_game");
    completeTitleLaunch(*fixture);

    fixture->ui.dispatchAction(
        std::string(rocket::ui::actions::scenarioActionPrefix) +
        rocket::content::scenario::outerTransfer + "|flyby|" +
        std::to_string(static_cast<int>(rocket::ScenarioActionKind::AcknowledgeBriefing)));
    assert(fixture->runner.app().currentScreen() == static_cast<int>(rocket::Screen::Hangar));
    fixture->host.now += 1.0 / 120.0;
    fixture->runner.frame();

    assertJupiterSaturnRoutePersisted(fixture->saves.value);

    fixture->ui.dispatchAction(std::string(rocket::ui::actions::attemptFrontier));
    assert(fixture->runner.app().currentScreen() == static_cast<int>(rocket::Screen::Flight));
    fixture->host.now += 1.0 / 120.0;
    fixture->runner.frame();
    assert(fixture->renderer.launchDestinationTier == 4);
    assert(fixture->renderer.launchOriginTier == 3);
    fixture->runner.shutdown();
}

void uranusVectorGenericClaimQueuesNeptune()
{
    const std::string action = rocket::ui::actions::scenarioAction(
        rocket::content::scenario::uranusDeparture,
        "vector",
        static_cast<int>(rocket::ScenarioActionKind::ClaimReward));

    auto fixture = std::make_unique<AppFixture>();
    fixture->saves.value = uranusVectorReadySave();
    assert(fixture->runner.initialize());
    fixture->ui.dispatchAction("continue_game");
    completeTitleLaunch(*fixture);
    fixture->ui.dispatchAction(action);
    fixture->host.now += 1.0 / 120.0;
    fixture->runner.frame();

    const std::optional<rocket::SaveData> save =
        rocket::deserializeSaveData(fixture->saves.value);
    assert(save.has_value());
    assert(save->destinationIndex == 5);
    assert(save->routeTransit.intent == rocket::RouteTransitIntent::Outbound);
    assert(save->routeTransit.routeLinkId == rocket::content::routeLink::uranusNeptune);
    assert(save->routeTransit.originDestinationId == rocket::content::destination::uranus);
    assert(save->routeTransit.targetDestinationId == rocket::content::destination::neptune);
    assert(std::find(
               save->unlockKeys.begin(),
               save->unlockKeys.end(),
               rocket::content::unlock::routeNeptune) != save->unlockKeys.end());
    fixture->runner.shutdown();
}

void straylightApproachRunsAndEndsActOne()
{
    auto fixture = std::make_unique<AppFixture>();
    fixture->saves.value = pendingStraylightDiscoverySave();
    assert(fixture->runner.initialize());
    fixture->ui.dispatchAction("continue_game");
    completeTitleLaunch(*fixture);
    assert(fixture->runner.app().currentScreen() == static_cast<int>(rocket::Screen::StoryBriefing));
    assert(fixture->ui.html.find("SOMETHING JUST BLOCKED THE STARS") != std::string::npos);
    assert(fixture->ui.html.find("The Ark") == std::string::npos);
    assert(fixture->ui.html.find("<h2>Straylight</h2>") == std::string::npos);
    assert(fixture->ui.html.find("data-rr-action=\"acknowledge_story_briefing\"") != std::string::npos);

    fixture->ui.dispatchAction("acknowledge_story_briefing");
    assert(fixture->runner.app().currentScreen() == static_cast<int>(rocket::Screen::Flight));
    assert(fixture->ui.html.find("REACH THE CONTACT") != std::string::npos);
    assert(fixture->ui.html.find("REACH STRAYLIGHT") == std::string::npos);
    fixture->host.now += 1.0 / 60.0;
    fixture->runner.frame();
    assert(fixture->renderer.straylightApproach);
    assert(!fixture->renderer.launchManualControlsEnabled);
    assert(!fixture->renderer.launchHeatEnabled);
    assert(!fixture->renderer.launchAsteroidsEnabled);
    const std::optional<rocket::SaveData> resumable =
        rocket::deserializeSaveData(fixture->saves.value);
    assert(resumable.has_value());
    assert(resumable->storyBriefing.pending == rocket::StoryBriefingId::StraylightApproach);
    rocket::GameState resumed = rocket::createNewGame(rocket::createDefaultContent(), resumable->seed);
    const rocket::ContentCatalog resumedCatalog = rocket::createDefaultContent();
    rocket::restoreSaveData(resumed, resumedCatalog, *resumable);
    assert(resumed.screen == rocket::Screen::StoryBriefing);

    fixture->ui.dispatchAction("start_launch");
    for (int frame = 0;
         frame < 200 && fixture->renderer.launchTravelProgress <= 0.05;
         ++frame) {
        fixture->host.now += 0.05;
        fixture->runner.frame();
    }
    assert(fixture->renderer.launchTravelProgress > 0.05);
    fixture->runner.app().returnHome();
    fixture->host.now += 0.05;
    fixture->runner.frame();
    assert(!fixture->renderer.launchReturningHome);
    assert(fixture->runner.app().currentScreen() == static_cast<int>(rocket::Screen::Flight));
    fixture->runner.shutdown();
}

} // namespace

int main()
{
    {
        AppFixture fixture;
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("sfx:activate");
        fixture.runner.app().newGame();
        fixture.ui.dispatchAction("sfx:activate");
        auto events = fixture.runner.app().consumePendingAudioEvents();
        assert(events.size() == 1 && events.front().cue == rocket::GameAudioCue::TakeoffIgnition);
        fixture.runner.app().newGame();
        assert(fixture.runner.app().consumePendingAudioEvents().empty());
        fixture.runner.shutdown();
    }
    {
        AppFixture fixture;
        assert(fixture.runner.initialize());
        auto& app = fixture.runner.app();
        assert(app.thrustAudioLevel() == 0.0);
        app.debugStartLaunchLesson(2);
        app.launchMove(0.0, 1.0);
        app.tick(1.0 / 60.0);
        assert(app.thrustAudioLevel() > 0.0);
        fixture.ui.modalOpenValue = true;
        assert(app.thrustAudioLevel() == 0.0);
        fixture.ui.modalOpenValue = false;
        assert(app.thrustAudioLevel() > 0.0);
        app.cutEngines();
        assert(app.thrustAudioLevel() == 0.0);
        app.debugStartMining();
        assert(app.thrustAudioLevel() == 0.0);
        fixture.runner.shutdown();
    }
    {
        rocket::AudioCueLimiter limiter;
        assert(limiter.admit(rocket::GameAudioCue::Drill, 1.0));
        assert(!limiter.admit(rocket::GameAudioCue::Drill, 1.1));
        assert(limiter.admit(rocket::GameAudioCue::Failure, 1.1));
        assert(!limiter.admit(rocket::GameAudioCue::Drill, 1.3));
        assert(limiter.admit(rocket::GameAudioCue::Drill, 1.43));
        assert(!limiter.admit(rocket::GameAudioCue::Count, 2.0));
        AppFixture fixture;
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("sfx:focus");
        fixture.ui.dispatchAction("sfx:focus");
        auto events = fixture.runner.app().consumePendingAudioEvents();
        assert(events.size() == 1 && events.front().cue == rocket::GameAudioCue::UiFocus);
        fixture.runner.app().miningScanner(); // Invalid outside mining: no scan or success sound.
        fixture.runner.app().buyOffer(-1);
        assert(fixture.runner.app().consumePendingAudioEvents().empty());
        fixture.runner.shutdown();
    }
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif

    liveMissionClaimPresentationAndDockRecovery();
    liveWaypointModalActionOwnership();
    liveShipRecoveryAppFlows();

    {
        AppFixture fixture;
        assert(fixture.runner.initialize());
        fixture.runner.app().debugStartMoonApproach();
        assert(fixture.runner.app().currentScreen()==static_cast<int>(rocket::Screen::Flight));
        assert(fixture.ui.html.find("Ready to launch")==std::string::npos);
        for(int i=0;i<65;++i) fixture.runner.app().tick(1.0/60.0);
        fixture.ui.dispatchAction("start_launch");
        assert(fixture.ui.html.find("Ready to launch")!=std::string::npos);
        fixture.ui.modalOpenValue=true;
        const auto paused=fixture.runner.app().deterministicStateHash();
        for(int i=0;i<120;++i) fixture.runner.app().tick(1.0/60.0);
        assert(fixture.runner.app().deterministicStateHash()==paused);
        fixture.ui.dispatchAction("ack_incoming_message:campaign.lunar_approach");
        assert(!fixture.ui.modalOpenValue);
        fixture.runner.app().tick(.05);
        assert(fixture.runner.app().deterministicStateHash()!=paused);
        assert(fixture.saves.storeCount==0);
        fixture.runner.shutdown();
    }
    {
        const auto catalog = rocket::createDefaultContent();
        auto state = rocket::createNewGame(catalog, 0xD0CULL);
        state.screen = rocket::Screen::Hangar;
        assert(rocket::initializeLiveExpedition(state, catalog));
        state.meta.unlockKeys.push_back(rocket::content::unlock::droneBay);
        const auto fuel = state.run.flight.fuelRemaining;
        const auto hull = state.run.flight.hullRemaining;
        AppFixture fixture;
        fixture.saves.value = rocket::serializeSaveData(rocket::captureSaveData(state));
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("continue_game");
        completeTitleLaunch(fixture);
        assert(fixture.ui.html.find("Drone Ops") != std::string::npos);
        fixture.ui.dispatchAction("drone_ops");
        fixture.runner.app().renderUi();
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::DroneOps));
        assert(fixture.ui.html.find("Return to Dock") != std::string::npos);
        fixture.ui.dispatchAction("back_to_surface_ops");
        fixture.runner.app().renderUi();
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Hangar));
        assert(fixture.ui.html.find("ORBITAL DOCK") != std::string::npos);
        const auto saved = rocket::deserializeSaveData(fixture.saves.value);
        assert(saved && saved->flight.fuelRemaining == fuel && saved->flight.hullRemaining == hull);
        fixture.runner.shutdown();
    }
    {
        AppFixture fixture;
        assert(fixture.runner.initialize());
        fixture.runner.app().debugStartExpedition();
        assert(fixture.ui.html.find("ORBITAL DOCK") != std::string::npos);
        assert(fixture.ui.html.find("expedition-dock-departure") != std::string::npos);
        assert(fixture.ui.html.find("expedition-dock-status") != std::string::npos);
        assert(fixture.ui.html.find("dock-status-credits") != std::string::npos);
        assert(fixture.ui.html.find("DEPART FOR Moon") != std::string::npos);
        assert(fixture.ui.html.find("Change waypoint") != std::string::npos);
        fixture.runner.app().tick(.01);
        fixture.ui.dispatchAction("ack_incoming_message:campaign.earth_dock.moon_first");
        fixture.ui.dispatchAction("expedition:map");
        assert(fixture.ui.modalOpenValue);
        assert(fixture.ui.html.find("SET WAYPOINT") != std::string::npos);
        assert(fixture.ui.html.find("DOCKED / CHOOSE NEXT DESTINATION") != std::string::npos);
        assert(fixture.ui.html.find("Depart from the dock.") != std::string::npos);
        assert(fixture.ui.html.find("Back to dock") != std::string::npos);
        assert(fixture.ui.html.find("Set waypoint: Moon") != std::string::npos);
        assert(fixture.ui.html.find("solar-orbit") != std::string::npos);
        assert(fixture.ui.html.find("expedition:preview:earth") != std::string::npos);
        assert(fixture.ui.html.find("expedition:preview:moon") != std::string::npos);
        assert(fixture.ui.html.find("expedition:preview:mars") == std::string::npos);
        assert(fixture.ui.html.find("expedition:preview:venus") == std::string::npos);
        assert(fixture.ui.html.find("expedition:preview:straylight") == std::string::npos);
        const auto paused = fixture.runner.app().deterministicStateHash();
        for (int i=0;i<60;++i) fixture.runner.app().tick(1.0/60.0);
        assert(fixture.runner.app().deterministicStateHash()==paused);
        fixture.ui.dispatchAction("expedition:preview:venus");
        assert(fixture.runner.app().deterministicStateHash()==paused);
        assert(fixture.ui.modalOpenValue);
        assert(fixture.ui.html.find("Set waypoint: Venus") == std::string::npos);
        assert(fixture.ui.html.find("Set waypoint: Moon") != std::string::npos);
        fixture.ui.dispatchAction("expedition:close");
        fixture.ui.dispatchAction("expedition:map");
        assert(fixture.ui.html.find("Set waypoint: Moon") != std::string::npos);
        fixture.ui.dispatchAction("expedition:preview:earth");
        assert(fixture.ui.html.find("Already docked at Earth") != std::string::npos);
        assert(fixture.ui.html.find("planet surface is not landable") != std::string::npos);
        fixture.ui.dispatchAction("expedition:preview:moon");
        fixture.ui.dispatchAction("expedition:plot:moon");
        assert(!fixture.ui.modalOpenValue);
        assert(fixture.runner.app().currentScreen()==static_cast<int>(rocket::Screen::Hangar));
        fixture.ui.dispatchAction("expedition:depart");
        assert(fixture.runner.app().currentScreen()==static_cast<int>(rocket::Screen::Flight));
        fixture.runner.app().launchMove(0,0);
        fixture.runner.app().launchMove(0,1);
        fixture.runner.app().tick(.05);
        fixture.runner.app().launchMove(0,0);
        fixture.ui.dispatchAction("expedition:cruise");
        fixture.ui.dispatchAction("expedition:map");
        assert(fixture.ui.html.find("CRUISE WILL RESUME")!=std::string::npos);
        const auto cruisePaused=fixture.runner.app().deterministicStateHash();
        fixture.runner.app().tick(.1);
        assert(fixture.runner.app().deterministicStateHash()==cruisePaused);
        fixture.ui.dispatchAction("expedition:close");
        fixture.ui.dispatchAction("expedition:abandon");
        fixture.ui.dispatchAction("expedition:confirm_abandon");
        assert(fixture.runner.app().currentScreen()==static_cast<int>(rocket::Screen::Hangar));
        assert(fixture.ui.html.find("Replacement")!=std::string::npos);
        assert(fixture.saves.storeCount==0);
        fixture.runner.shutdown();
    }
    {
        // The Earth dock orientation waits for a reload at the dock. It must
        // not interrupt an in-progress session just because a failure hands
        // the player back to Earth.
        const auto catalog = rocket::createDefaultContent();
        auto state = rocket::createNewGame(catalog, 0xEAD0CULL);
        state.screen = rocket::Screen::Hangar;
        assert(rocket::initializeLiveExpedition(state, catalog));
        state.run.expedition.openingInitialized = true;
        state.run.expedition.departureCount = 1;
        state.meta.lunarProspectorClaimed = true;
        AppFixture fixture;
        fixture.saves.value = rocket::serializeSaveData(rocket::captureSaveData(state));
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("continue_game");
        completeTitleLaunch(fixture);
        fixture.runner.app().tick(.01);
        const auto reloaded = rocket::deserializeSaveData(fixture.saves.value);
        assert(reloaded && std::any_of(
            reloaded->incomingMessages.pending.begin(), reloaded->incomingMessages.pending.end(),
            [](const rocket::IncomingMessageOccurrence& message) {
                return message.id == "campaign.earth_dock.services";
            }));
        fixture.runner.shutdown();
    }
    {
        const auto catalog = rocket::createDefaultContent();
        auto state = rocket::createNewGame(catalog, 0xD0C5A7ULL);
        state.screen = rocket::Screen::Hangar;
        assert(rocket::initializeLiveExpedition(state, catalog));
        state.meta.unlockKeys.push_back(rocket::content::unlock::routeMars);
        state.run.expedition.course.targetBodyId = "earth";
        state.meta.campaignIntroductionAcknowledged = true;
        state.incomingMessages.acknowledgedMessages.push_back("earth_dock_intro");
        AppFixture fixture;
        fixture.saves.value = rocket::serializeSaveData(rocket::captureSaveData(state));
        assert(fixture.runner.initialize());
        const auto recovered = rocket::deserializeSaveData(fixture.saves.value);
        assert(recovered && recovered->expedition.course.targetBodyId == "moon");
        fixture.ui.dispatchAction("continue_game");
        completeTitleLaunch(fixture);
        assert(fixture.ui.html.find("NEXT MISSION: Moon") != std::string::npos);
        assert(fixture.ui.html.find("WAYPOINT: Moon") != std::string::npos);
        assert(fixture.ui.html.find("DEPART FOR Moon") != std::string::npos);
        fixture.ui.dispatchAction("expedition:map");
        fixture.ui.dispatchAction("expedition:preview:moon");
        assert(fixture.ui.html.find("Set waypoint: Moon") != std::string::npos);
        fixture.ui.dispatchAction("expedition:plot:moon");
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Hangar));
        assert(fixture.ui.html.find("Moon waypoint set. Depart dock when ready.") != std::string::npos);
        assert(fixture.ui.html.find("DEPART FOR Moon") != std::string::npos);
        fixture.runner.shutdown();
    }
    {
        const auto catalog = rocket::createDefaultContent();
        auto state = rocket::createNewGame(catalog, 0xD0C612ULL);
        assert(rocket::initializeLiveExpedition(state, catalog));
        const auto& system = rocket::solarSystemDefinition();
        const auto* earth = rocket::systemBody(system, "earth");
        assert(earth);
        const auto dock = rocket::systemDockPosition(*earth);
        auto& expedition = state.run.expedition;
        auto& flight = state.run.flight;
        state.screen = rocket::Screen::Flight;
        state.meta.campaignIntroductionAcknowledged = true;
        state.incomingMessages.acknowledgedMessages.push_back("earth_dock_intro");
        expedition.active = true;
        expedition.departureCount = 1;
        expedition.course.targetBodyId = "earth";
        expedition.location = {system.id, "", rocket::CoordinateFrame::System,
            {dock.x + rocket::expeditionDockRadius * 0.5, dock.y},
            {earth->velocity.x + 0.5, earth->velocity.y}, 0, {}};
        rocket::restoreSystemLocation(expedition.location, flight);
        flight.active = flight.physicalFlight = true;
        flight.mode = rocket::FlightMode::Travel;
        AppFixture fixture;
        fixture.saves.value = rocket::serializeSaveData(rocket::captureSaveData(state));
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("continue_game");
        completeTitleLaunch(fixture);
        assert(fixture.ui.html.find("class=\"expedition-dock-action\"") != std::string::npos);
        assert(fixture.ui.html.find("class=\"ok\" data-ui-focus-id=\"action:expedition:dock\"") != std::string::npos);
        assert(fixture.ui.html.find("slow below 0.20 relative speed") != std::string::npos);
        fixture.runner.shutdown();
    }
    {
        AppFixture fixture;
        assert(fixture.runner.initialize());
        fixture.runner.app().debugShowIncomingMessage();
        assert(fixture.ui.html.find("INCOMING MESSAGE") != std::string::npos);
        fixture.ui.modalOpenValue = true;
        const auto pausedHash = fixture.runner.app().deterministicStateHash();
        for (int frame = 0; frame < 120; ++frame) fixture.runner.app().tick(1.0 / 60.0);
        assert(fixture.runner.app().deterministicStateHash() == pausedHash);
        fixture.ui.dispatchAction("ack_incoming_message:preview.recovery");
        assert(!fixture.ui.modalOpenValue);
        const auto acknowledgedHash = fixture.runner.app().deterministicStateHash();
        fixture.runner.app().miningMove(1.0, -1.0);
        fixture.runner.app().miningKeyboardDrill(true);
        fixture.runner.app().miningFire(true);
        assert(fixture.runner.app().deterministicStateHash() == acknowledgedHash);
        fixture.ui.dispatchAction("ack_incoming_message:preview.recovery");
        assert(fixture.runner.app().deterministicStateHash() == acknowledgedHash);
        fixture.runner.app().miningMove(0.0, 0.0);
        fixture.runner.app().miningKeyboardDrill(false);
        fixture.runner.app().miningFire(false);
        fixture.runner.app().miningMove(1.0, -1.0);
        fixture.runner.app().tick(1.0 / 60.0);
        assert(fixture.runner.app().deterministicStateHash() != acknowledgedHash);
        assert(fixture.saves.storeCount == 0);
        fixture.runner.shutdown();
    }
#if !defined(__EMSCRIPTEN__)
#endif

#if !defined(__EMSCRIPTEN__)
    // The packaged document shell is loaded once. Changing screen-family
    // templates and patching live HUD values must only update stable hosts,
    // while semantic focus and modal boundaries remain intact.
    {
        const rocket::ContentCatalog catalog = rocket::createDefaultContent();
        rocket::GameState hangar = rocket::createNewGame(catalog, 0x7E6D1A7EULL);
        hangar.screen = rocket::Screen::Hangar;
        rocket::Random hangarRng(0x7E6D1A7EULL);
        const rocket::PreparedLaunch hangarLaunch =
            rocket::prepareLaunch(hangar, catalog, hangarRng);
        rocket::PanelRenderContext hangarContext {
            hangar,
            catalog,
            hangarLaunch,
            hangarLaunch};
        hangarContext.firstTimeIntroductionsEnabled = false;

        FakePreferenceStore preferences;
        FakeHost host;
        host.metrics = {1280, 800, 1280, 800, 1.0F};
        FakeUiBridge bridge;
        NullRmlRenderHost renderHost;
        rocket::GameRmlUi ui(
            preferences,
            host,
            bridge,
            renderHost,
            repositoryRootForRmlTests());
        std::string dispatchedAction;
        assert(ui.initialize([&](const std::string& action) { dispatchedAction = action; }));
        assert(bridge.rmlUiEnabled);

        ui.setPanelPresentation(rocket::buildGamePanelPresentation(hangarContext));
        ui.requestFocus("action:prepare_launch");
        ui.refresh();
        assert(ui.focusedId() == "action:prepare_launch");
        ui.render();
        const rocket::UiDiagnostics initialDiagnostics = ui.diagnostics();
        assert(initialDiagnostics.documentRebuilds == 1);

        // Preflight Launch is mounted in the scene-overlay host. Semantic
        // focus must cross that persistent-host boundary instead of retaining
        // a stale Hangar header target such as Menu.
        hangar.screen = rocket::Screen::Flight;
        rocket::PanelRenderContext preflightContext {
            hangar,
            catalog,
            hangarLaunch,
            hangarLaunch};
        preflightContext.flightArmed = false;
        preflightContext.preflightReady = false;
        preflightContext.firstTimeIntroductionsEnabled = false;
        ui.setPanelPresentation(rocket::buildGamePanelPresentation(preflightContext));
        ui.refresh();
        ui.requestFocus("action:start_launch");
        assert(ui.focusedId() == "action:start_launch");
        dispatchedAction.clear();
        assert(ui.activateFocused());
        assert(dispatchedAction == "start_launch");

        hangar.screen = rocket::Screen::Hangar;
        ui.setPanelPresentation(rocket::buildGamePanelPresentation(hangarContext));
        ui.refresh();
        ui.requestFocus("action:prepare_launch");
        assert(ui.focusedId() == "action:prepare_launch");

        // Nested modal expansion keeps each layer's semantic focus. Closing
        // Settings returns to its launcher in the non-dismissible System
        // Menu, and closing that layer restores the original panel control.
        ui.requestFocus("modal:system_menu");
        ui.refresh();
        assert(ui.focusedId() == "modal:system_menu");
        assert(ui.activateFocused());
        assert(ui.modalOpen());
        assert(!ui.cancel());
        ui.requestFocus("modal:settings");
        ui.refresh();
        assert(ui.focusedId() == "modal:settings");
        assert(ui.activateFocused());
        assert(ui.modalOpen());
        ui.closeModal();
        assert(ui.modalOpen());
        assert(ui.focusedId() == "modal:settings");
        ui.closeModal();
        assert(!ui.modalOpen());
        assert(ui.focusedId() == "modal:system_menu");

        // With an existing save, New Game opens a confirmation modal instead
        // of dispatching the action directly. Exercise that native path with
        // the real RmlUi binding dispatcher: opening the modal rebuilds the
        // bindings that supplied the click, so the dispatcher must own its
        // binding data for the duration of the callback.
        rocket::PanelRenderContext savedTitleContext {
            hangar,
            catalog,
            hangarLaunch,
            hangarLaunch};
        savedTitleContext.titleScreenActive = true;
        savedTitleContext.hasSavedGame = true;
        savedTitleContext.firstTimeIntroductionsEnabled = false;
        ui.setPanelPresentation(rocket::buildGamePanelPresentation(savedTitleContext));
        ui.requestFocus("modal:new_game_confirm");
        ui.refresh();
        assert(ui.focusedId() == "modal:new_game_confirm");
        assert(ui.activateFocused());
        assert(ui.modalOpen());
        ui.closeModal();
        assert(!ui.modalOpen());
        assert(ui.focusedId() == "modal:new_game_confirm");

        rocket::GameState flyby = rocket::createNewGame(catalog, 0xF17B7ULL);
        rocket::LaunchOutcome moonArrival;
        moonArrival.type = rocket::LaunchResultType::MissionComplete;
        moonArrival.frontierTransfer = true;
        moonArrival.destinationId = rocket::content::destination::moon;
        rocket::initializeLiveExpedition(flyby, catalog);
        flyby.screen = rocket::Screen::Flight;
        rocket::Random flybyRng(0xF17B7ULL);
        const rocket::PreparedLaunch flybyLaunch =
            rocket::prepareLaunch(flyby, catalog, flybyRng);
        rocket::PanelRenderContext flybyContext {
            flyby,
            catalog,
            flybyLaunch,
            flybyLaunch};
        flyby.run.flight = rocket::beginLaunchFlight(flybyLaunch, rocket::currentDestination(flyby, catalog));
        flybyContext.flightArmed = true;
        flybyContext.firstTimeIntroductionsEnabled = false;
        const rocket::PanelDocumentPresentation flybyPresentation =
            rocket::buildGamePanelPresentation(flybyContext);
        assert(flybyPresentation.metadata.screen == rocket::Screen::Flight);

        ui.setPanelPresentation(flybyPresentation);
        ui.requestFocus("modal:inventory");
        ui.refresh();
        assert(ui.focusedId() == "modal:inventory");
        ui.render();
        const rocket::UiDiagnostics transitionDiagnostics = ui.diagnostics();
        assert(transitionDiagnostics.documentRebuilds == 0);
        assert(transitionDiagnostics.panelRebuilds > 0);

        assert(ui.activateFocused());
        assert(ui.modalOpen());
        const std::string activeModalFocus = ui.focusedId();
        ui.render(); // Clear the modal host rebuild before measuring HUD-only work.

        rocket::RealtimeHudState hud;
        rocket::buildRealtimeHudState(flybyContext, hud);
        assert(!hud.patches.empty());
        ui.setRealtimeHudState(hud);
        ui.render();
        const rocket::UiDiagnostics hudDiagnostics = ui.diagnostics();
        assert(hudDiagnostics.documentRebuilds == 0);
        assert(hudDiagnostics.panelRebuilds == 0);
        assert(hudDiagnostics.hudPatches > 0);
        assert(ui.modalOpen());
        assert(ui.focusedId() == activeModalFocus);
        assert(std::none_of(
            host.logMessages.begin(),
            host.logMessages.end(),
            [](const std::string& message) {
                return message.find("Realtime RmlUi patch target is missing") != std::string::npos;
            }));

        assert(ui.cancel());
        assert(!ui.modalOpen());
        assert(ui.focusedId() == "modal:inventory");

        const auto applyRealtimePresentation = [&](const rocket::PanelRenderContext& context) {
            const std::size_t logStart = host.logMessages.size();
            ui.setPanelPresentation(rocket::buildGamePanelPresentation(context));
            rocket::RealtimeHudState realtime;
            rocket::buildRealtimeHudState(context, realtime);
            assert(!realtime.patches.empty());
            ui.setRealtimeHudState(realtime);
            ui.render();
            for (std::size_t i = logStart; i < host.logMessages.size(); ++i) {
                if (host.logMessages[i].find("Realtime RmlUi patch target is missing") != std::string::npos)
                    std::cerr << "Screen " << static_cast<int>(context.state.screen) << ": " << host.logMessages[i] << '\n';
            }
            assert(std::none_of(
                host.logMessages.begin() + static_cast<std::ptrdiff_t>(logStart),
                host.logMessages.end(),
                [](const std::string& message) {
                    return message.find("Realtime RmlUi patch target is missing") != std::string::npos;
                }));
        };

        rocket::GameState launch = rocket::createNewGame(catalog, 0x1A0C4ULL);
        launch.screen = rocket::Screen::Flight;
        rocket::Random launchRng(0x1A0C4ULL);
        const rocket::PreparedLaunch launchModel =
            rocket::prepareLaunch(launch, catalog, launchRng);
        rocket::PanelRenderContext launchContext {
            launch,
            catalog,
            launchModel,
            launchModel};
        launchContext.firstTimeIntroductionsEnabled = false;
        applyRealtimePresentation(launchContext);

        rocket::GameState mining = rocket::createNewGame(catalog, 0xA11CEULL);
        mining.run.destinationIndex = 2;
        rocket::startSurfaceExpedition(mining, catalog);
        mining.run.planetaryExpedition.miningSitePrepared = true;
        assert(rocket::startMiningRun(mining, catalog).applied);
        rocket::Random miningRng(0xA11CEULL);
        const rocket::PreparedLaunch miningLaunch =
            rocket::prepareLaunch(mining, catalog, miningRng);
        rocket::PanelRenderContext miningContext {
            mining,
            catalog,
            miningLaunch,
            miningLaunch};
        miningContext.firstTimeIntroductionsEnabled = false;
        applyRealtimePresentation(miningContext);

        rocket::GameState ioMining = rocket::createNewGame(catalog, 0x10A11ULL);
        ioMining.run.destinationIndex = 3;
        ioMining.meta.furthestTier = 3;
        rocket::startSurfaceExpedition(ioMining, catalog);
        ioMining.run.planetaryExpedition.miningSitePrepared = true;
        assert(rocket::startMiningRun(ioMining, catalog).applied);
        rocket::Random ioMiningRng(0x10A11ULL);
        const rocket::PreparedLaunch ioMiningLaunch =
            rocket::prepareLaunch(ioMining, catalog, ioMiningRng);
        rocket::PanelRenderContext ioMiningContext {
            ioMining,
            catalog,
            ioMiningLaunch,
            ioMiningLaunch};
        ioMiningContext.firstTimeIntroductionsEnabled = false;
        applyRealtimePresentation(ioMiningContext);

        rocket::GameState scan = rocket::createNewGame(catalog, 0x5CA11ULL);
        scan.run.destinationIndex = 2;
        rocket::startSurfaceExpedition(scan, catalog);
        rocket::Random scanRng(0x5CA11ULL);
        assert(rocket::startMiningRun(scan, catalog).applied);
        const rocket::PreparedLaunch scanLaunch =
            rocket::prepareLaunch(scan, catalog, scanRng);
        rocket::PanelRenderContext scanContext {
            scan,
            catalog,
            scanLaunch,
            scanLaunch};
        scanContext.firstTimeIntroductionsEnabled = false;
        ui.setPanelPresentation(rocket::buildGamePanelPresentation(scanContext));
        ui.openModal(std::string(rocket::ui::modals::inventory));
        assert(ui.modalOpen());
        applyRealtimePresentation(scanContext);
        ui.closeModal();

        auto results = std::make_unique<rocket::GameState>(
            rocket::createNewGame(catalog, 0xDEB21EFULL));
        results->screen = rocket::Screen::Results;
        results->lastOutcome.type = rocket::LaunchResultType::SafeEject;
        results->lastOutcome.recoveryMethod = rocket::RecoveryMethod::ReturnHome;
        results->lastOutcome.ejectMultiplier = 1.1;
        results->lastOutcome.crashMultiplier = 1.5;
        rocket::Random resultsRng(0xDEB21EFULL);
        const rocket::PreparedLaunch resultsLaunch =
            rocket::prepareLaunch(*results, catalog, resultsRng);
        const rocket::PanelDocumentPresentation resultsPresentation =
            rocket::buildGamePanelPresentation({
                *results,
                catalog,
                resultsLaunch,
                resultsLaunch});
        assert(resultsPresentation.templateKind == rocket::PanelTemplateKind::Results);
        ui.setPanelPresentation(resultsPresentation);
        assert(ui.modalOpen());
        const std::string mandatoryModalFocus = ui.focusedId();
        assert(!mandatoryModalFocus.empty());
        ui.render();
        assert(ui.diagnostics().documentRebuilds == 0);
        assert(!ui.cancel());
        assert(ui.modalOpen());
        assert(ui.focusedId() == mandatoryModalFocus);
        assert(bridge.modalOpen);
        ui.setControllerPresentation(true, rocket::ControllerFamily::Xbox);
        ui.setControllerFocusVisible(true);
        ui.setControllerResumeBlocked(true, true);
        assert(bridge.controllerPresentationActive);
        assert(bridge.controllerFocusVisible);
        assert(bridge.controllerResumeBlocked);
        assert(bridge.controllerConnected);
        ui.shutdown();
        assert(!bridge.modalOpen);
        assert(!bridge.rmlUiEnabled);
        assert(!bridge.controllerPresentationActive);
        assert(!bridge.controllerFocusVisible);
        assert(!bridge.controllerResumeBlocked);
        assert(!bridge.controllerConnected);
    }

    // Typed modal records are an API boundary, not loosely parsed markup.
    // Empty and duplicate IDs must fail loudly and leave the last valid
    // presentation active instead of producing an ambiguous modal stack.
    {
        FakePreferenceStore preferences;
        FakeHost host;
        FakeUiBridge bridge;
        NullRmlRenderHost renderHost;
        rocket::GameRmlUi ui(
            preferences,
            host,
            bridge,
            renderHost,
            repositoryRootForRmlTests());
        assert(ui.initialize([](const std::string&) {}));

        rocket::PanelDocumentPresentation valid;
        valid.contentMarkup = "<div>Valid panel</div>";
        valid.modals.push_back({"valid_modal", "Valid modal", "<p>Valid body</p>"});
        ui.setPanelPresentation(valid);

        rocket::PanelDocumentPresentation emptyId = valid;
        emptyId.modals.push_back({"", "Invalid modal", "<p>Invalid body</p>"});
        const std::size_t emptyLogStart = host.logMessages.size();
        ui.setPanelPresentation(emptyId);
        assert(std::any_of(
            host.logMessages.begin() + static_cast<std::ptrdiff_t>(emptyLogStart),
            host.logMessages.end(),
            [](const std::string& message) {
                return message.find(
                           "Invalid RmlUi panel presentation: modal at index 1 has an empty id.")
                    != std::string::npos;
            }));

        rocket::PanelDocumentPresentation duplicateId = valid;
        duplicateId.modals.push_back(
            {"valid_modal", "Duplicate modal", "<p>Duplicate body</p>"});
        const std::size_t duplicateLogStart = host.logMessages.size();
        ui.setPanelPresentation(duplicateId);
        assert(std::any_of(
            host.logMessages.begin() + static_cast<std::ptrdiff_t>(duplicateLogStart),
            host.logMessages.end(),
            [](const std::string& message) {
                return message.find(
                           "Invalid RmlUi panel presentation: duplicate modal id 'valid_modal'.")
                    != std::string::npos;
            }));

        ui.openModal("valid_modal");
        assert(ui.modalOpen());
        ui.closeModal();
        ui.shutdown();
    }

    // The packaged document, templates, styles, and fonts must resolve when
    // the runtime asset root itself contains spaces.
    {
        const std::filesystem::path repositoryRoot = repositoryRootForRmlTests();
        const auto nonce = std::chrono::high_resolution_clock::now()
                               .time_since_epoch()
                               .count();
        const std::filesystem::path temporaryRoot =
            std::filesystem::temp_directory_path()
            / ("orebit rmlui assets " + std::to_string(nonce));
        const std::filesystem::path temporaryAssets = temporaryRoot / "assets";
        std::filesystem::create_directories(temporaryAssets);
        std::filesystem::copy(
            repositoryRoot / "assets" / "fonts",
            temporaryAssets / "fonts",
            std::filesystem::copy_options::recursive);
        std::filesystem::copy(
            repositoryRoot / "assets" / "ui",
            temporaryAssets / "ui",
            std::filesystem::copy_options::recursive);

        FakePreferenceStore preferences;
        FakeHost host;
        FakeUiBridge bridge;
        NullRmlRenderHost renderHost;
        rocket::GameRmlUi ui(
            preferences,
            host,
            bridge,
            renderHost,
            temporaryRoot.string());
        assert(ui.initialize([](const std::string&) {}));
        assert(bridge.rmlUiEnabled);
        ui.shutdown();
        assert(!bridge.rmlUiEnabled);

        std::error_code cleanupError;
        std::filesystem::remove_all(temporaryRoot, cleanupError);
        assert(!cleanupError);
    }

    // Initialization should fail atomically, identify the exact missing
    // packaged asset, and leave the browser/native host disabled.
    {
        const std::filesystem::path repositoryRoot = repositoryRootForRmlTests();
        const auto nonce = std::chrono::high_resolution_clock::now()
                               .time_since_epoch()
                               .count();
        const std::filesystem::path temporaryRoot =
            std::filesystem::temp_directory_path()
            / ("orebit-rmlui-missing-assets-" + std::to_string(nonce));
        const std::filesystem::path temporaryFonts =
            temporaryRoot / "assets" / "fonts";
        std::filesystem::create_directories(temporaryFonts);
        for (const std::string_view fontName : {
                 std::string_view("SourceCodePro-Regular.ttf"),
                 std::string_view("SourceCodePro-Semibold.ttf"),
                 std::string_view("SourceCodePro-It.ttf")}) {
            assert(std::filesystem::copy_file(
                repositoryRoot / "assets" / "fonts" / std::string(fontName),
                temporaryFonts / std::string(fontName)));
        }

        FakePreferenceStore preferences;
        FakeHost host;
        FakeUiBridge bridge;
        bridge.rmlUiEnabled = true;
        NullRmlRenderHost renderHost;
        rocket::GameRmlUi ui(
            preferences,
            host,
            bridge,
            renderHost,
            temporaryRoot.string());
        assert(!ui.initialize([](const std::string&) {}));
        assert(!bridge.rmlUiEnabled);
        const std::filesystem::path expectedMissingAsset =
            temporaryRoot / "assets" / "ui" / "panel.rml";
        assert(std::any_of(
            host.logMessages.begin(),
            host.logMessages.end(),
            [&expectedMissingAsset](const std::string& message) {
                return message.find("Required RmlUi asset is missing:") != std::string::npos
                    && message.find(expectedMissingAsset.string()) != std::string::npos;
            }));

        std::error_code cleanupError;
        std::filesystem::remove_all(temporaryRoot, cleanupError);
        assert(!cleanupError);
    }

    // Surface Ops presents its immediate operations as one visible horizontal
    // action row. Left/right follows Mine -> Survey -> Push -> Extract, while
    // Up returns to the Drone Ops callout above the row.
    {
        const rocket::ContentCatalog catalog = rocket::createDefaultContent();
        rocket::GameState state = rocket::createNewGame(catalog, 0x5A7FACEULL);
        state.run.destinationIndex = 2;
        state.meta.furthestTier = 2;
        assert(rocket::performScenarioAction(
                   state,
                   catalog,
                   rocket::content::scenario::lunarProspector,
                   "briefing",
                   rocket::ScenarioActionKind::AcknowledgeBriefing).applied);
        assert(rocket::recordScenarioEvent(
            state,
            catalog,
            {rocket::ScenarioEventKind::SafeMaterialDelivered,
             {}, {}, rocket::content::destination::moon, "common",
             rocket::tuning::research::prospectorCommonOreGoal, 0}));
        assert(rocket::recordScenarioEvent(
            state,
            catalog,
            {rocket::ScenarioEventKind::ProtectedObjectiveExtracted,
             rocket::content::scenario::lunarProspector,
             "anomaly",
             rocket::content::destination::moon,
             rocket::content::miningSite::lunarAnomalyCrevice,
             1,
             0}));
        assert(rocket::performScenarioAction(
                   state,
                   catalog,
                   rocket::content::scenario::lunarProspector,
                   "anomaly",
                   rocket::ScenarioActionKind::ClaimReward).applied);
        assert(rocket::performScenarioAction(
                   state,
                   catalog,
                   rocket::content::scenario::marsBayExpansion,
                   "briefing",
                   rocket::ScenarioActionKind::AcknowledgeBriefing).applied);
        rocket::startSurfaceExpedition(state, catalog);
        rocket::ensureDroneBayState(state, catalog);
        rocket::ui::briefings::acknowledge(
            state.meta.acknowledgedActivityBriefingIds,
            rocket::ui::briefings::miniDrones);
        rocket::ui::briefings::acknowledge(
            state.meta.acknowledgedActivityBriefingIds,
            rocket::ui::briefings::mining);
        state.run.planetaryExpedition.miningSitePrepared = true;
        state.run.planetaryExpedition.depthProspects.push_back({1, 1});
        state.screen = rocket::Screen::SurfaceExpedition;
        rocket::Random rng(0x5A7FACEULL);
        const rocket::PreparedLaunch launch = rocket::prepareLaunch(state, catalog, rng);
        rocket::PanelRenderContext panelContext {state, catalog, launch, launch};
        panelContext.firstTimeIntroductionsEnabled = false;

        FakePreferenceStore preferences;
        FakeHost host;
        host.metrics = {1861, 618, 4337, 1440, 2.33F};
        FakeUiBridge bridge;
        NullRmlRenderHost renderHost;
        std::string pointerAction;
        rocket::GameRmlUi ui(
            preferences,
            host,
            bridge,
            renderHost,
            repositoryRootForRmlTests());
        assert(ui.initialize([&pointerAction](const std::string& action) {
            pointerAction = action;
        }));
        const rocket::PanelDocumentPresentation surfacePresentation =
            rocket::buildGamePanelPresentation(panelContext);
        const std::string& surfaceHtml = surfacePresentation.contentMarkup;
        assert(surfaceHtml.find("data-ui-focus-id=\"action:drone_ops\"") != std::string::npos);
        ui.setPanelPresentation(surfacePresentation);
        ui.setControllerPresentation(true, rocket::ControllerFamily::Xbox);
        ui.requestFocus("action:mine_surface");
        ui.refresh();

        // Survey is unavailable at the prepared depth. Disabled controls do
        // not receive focus; the first navigation input enters the actionable
        // portion of the row.
        constexpr std::array<std::string_view, 2> focusPath {
            "action:mine_surface",
            "action:extract_surface",
        };
        assert(ui.focusedId() == focusPath.front());
        assert(!ui.navigate(rocket::UiDirection::Left));
        assert(ui.focusedId() == focusPath.front());
        for (std::size_t index = 1; index < focusPath.size(); ++index) {
            assert(ui.navigate(rocket::UiDirection::Right));
            assert(ui.focusedId() == focusPath[index]);
        }
        assert(!ui.navigate(rocket::UiDirection::Right));
        for (std::size_t index = focusPath.size() - 1; index > 0; --index) {
            assert(ui.navigate(rocket::UiDirection::Left));
            assert(ui.focusedId() == focusPath[index - 1]);
        }
        assert(ui.navigate(rocket::UiDirection::Up));
        assert(ui.focusedId() == "action:drone_ops");
        assert(ui.navigate(rocket::UiDirection::Down));
        assert(ui.focusedId() != "action:drone_ops");

        // The shared titlebar is a horizontal row. Left/right follows its
        // visible order and stops at the row edges instead of wrapping.
        ui.requestFocus("modal:inventory");
        ui.refresh();
        assert(ui.focusedId() == "modal:inventory");
        assert(ui.navigate(rocket::UiDirection::Left));
        assert(ui.focusedId() == "modal:map");
        assert(!ui.navigate(rocket::UiDirection::Left));
        assert(ui.focusedId() == "modal:map");
        assert(ui.navigate(rocket::UiDirection::Right));
        assert(ui.focusedId() == "modal:inventory");
        assert(ui.navigate(rocket::UiDirection::Right));
        assert(ui.focusedId() == "modal:system_menu");
        assert(!ui.navigate(rocket::UiDirection::Right));
        assert(ui.focusedId() == "modal:system_menu");

        // Right-aligned utility rows map vertically to the matching titlebar
        // controls even when responsive transforms move the board itself.
        ui.requestFocus("modal:mission_log");
        ui.refresh();
        assert(ui.focusedId() == "modal:mission_log");
        assert(ui.navigate(rocket::UiDirection::Up));
        assert(ui.focusedId() == "modal:system_menu");
        assert(ui.navigate(rocket::UiDirection::Down));
        assert(ui.focusedId() == "modal:mission_log");
        ui.requestFocus("modal:surface");
        ui.refresh();
        assert(ui.navigate(rocket::UiDirection::Up));
        assert(ui.focusedId() == "modal:inventory");

        // A wide, short workspace still caps its centered work lane at 1200
        // px. Every Surface Ops card button must remain pointer-reachable
        // inside that lane instead of being positioned from the outer monitor
        // width.
        // Survey is intentionally disabled at the configured depth limit;
        // pointer reachability applies to the remaining actionable controls.
        constexpr std::array<std::string_view, 2> surfaceActions {
            rocket::ui::actions::mineSurface,
            rocket::ui::actions::extractSurface,
        };
        std::array<bool, surfaceActions.size()> pointerReachable {};
        int digPointerX = -1;
        int digPointerY = -1;
        for (int x = 340; x <= 1520; x += 10) {
            for (int y = 340; y <= 610; y += 10) {
                pointerAction.clear();
                ui.mouseDown(x, y, 0);
                ui.mouseUp(x, y, 0);
                ui.render();
                for (std::size_t index = 0; index < surfaceActions.size(); ++index) {
                    pointerReachable[index] = pointerReachable[index]
                        || pointerAction == surfaceActions[index];
                }
                if (pointerAction == rocket::ui::actions::mineSurface) {
                    digPointerX = x;
                    digPointerY = y;
                }
            }
        }
        const bool allSurfaceActionsReachable = std::all_of(
            pointerReachable.begin(),
            pointerReachable.end(),
            [](bool reachable) { return reachable; });
        if (!allSurfaceActionsReachable) {
            std::cerr << "Surface action reachability:"
                      << " mine=" << pointerReachable[0]
                      << " extract=" << pointerReachable[1] << '\n';
        }
        assert(allSurfaceActionsReachable);
        assert(digPointerX >= 0 && digPointerY >= 0);

        // The first Dig click opens a modal and rebuilds its Rml document. Its
        // raw mouse-up path must not retain a binding pointer from the old
        // document tree; that was a native hard crash on Steam Deck.
        rocket::GameState digIntroductionState = state;
        digIntroductionState.meta.acknowledgedActivityBriefingIds.erase(
            std::remove(
                digIntroductionState.meta.acknowledgedActivityBriefingIds.begin(),
                digIntroductionState.meta.acknowledgedActivityBriefingIds.end(),
                std::string(rocket::ui::briefings::mining)),
            digIntroductionState.meta.acknowledgedActivityBriefingIds.end());
        rocket::ui::briefings::acknowledge(
            digIntroductionState.meta.acknowledgedActivityBriefingIds,
            rocket::ui::briefings::surfaceSurveyComplete);
        rocket::Random digIntroductionRng(0xD161D161ULL);
        const rocket::PreparedLaunch digIntroductionLaunch =
            rocket::prepareLaunch(digIntroductionState, catalog, digIntroductionRng);
        rocket::PanelRenderContext digIntroductionContext {
            digIntroductionState,
            catalog,
            digIntroductionLaunch,
            digIntroductionLaunch};
        digIntroductionContext.firstTimeIntroductionsEnabled = true;
        ui.setPanelPresentation(
            rocket::buildGamePanelPresentation(digIntroductionContext));
        ui.refresh();
        for (int attempt = 0; attempt < 12; ++attempt) {
            ui.mouseDown(digPointerX, digPointerY, 0);
            ui.mouseUp(digPointerX, digPointerY, 0);
            // Pointer activation queues the modal until RmlUi has left its raw
            // mouse-up dispatch. This is the native Deck crash regression path.
            assert(!ui.modalOpen());
            ui.render();
            assert(ui.modalOpen());
            ui.closeModal();
        }

        // Once the briefing has been acknowledged, the same Dig button
        // dispatches the real surface action instead of opening a modal. That
        // action also mutates the panel state, so it must leave raw mouse-up
        // before it runs.
        rocket::GameState directDigState = digIntroductionState;
        rocket::ui::briefings::acknowledge(
            directDigState.meta.acknowledgedActivityBriefingIds,
            rocket::ui::briefings::mining);
        rocket::Random directDigRng(0xD161D162ULL);
        const rocket::PreparedLaunch directDigLaunch =
            rocket::prepareLaunch(directDigState, catalog, directDigRng);
        rocket::PanelRenderContext directDigContext {
            directDigState,
            catalog,
            directDigLaunch,
            directDigLaunch};
        directDigContext.firstTimeIntroductionsEnabled = true;
        ui.setPanelPresentation(rocket::buildGamePanelPresentation(directDigContext));
        ui.refresh();
        pointerAction.clear();
        ui.mouseDown(digPointerX, digPointerY, 0);
        ui.mouseUp(digPointerX, digPointerY, 0);
        assert(pointerAction.empty());
        ui.render();
        assert(pointerAction == rocket::ui::actions::mineSurface);
        ui.shutdown();
    }

    // Screen templates without a utility row still need an explicit way out
    // of the shared Map / Inventory / Menu titlebar. Approach is the compact
    // representative: Down must enter the Flyby / Orbit / Landing lane.
    {
        const rocket::ContentCatalog catalog = rocket::createDefaultContent();
        rocket::GameState state = rocket::createNewGame(catalog, 0xA770ACULL);
        rocket::LaunchOutcome moonArrival;
        moonArrival.type = rocket::LaunchResultType::MissionComplete;
        moonArrival.frontierTransfer = true;
        moonArrival.destinationId = rocket::content::destination::moon;
        rocket::startArrivalOps(state, moonArrival);
        state.screen = rocket::Screen::ArrivalOps;
        rocket::Random rng(0xA770ACULL);
        const rocket::PreparedLaunch launch = rocket::prepareLaunch(state, catalog, rng);
        rocket::PanelRenderContext panelContext {state, catalog, launch, launch};
        panelContext.firstTimeIntroductionsEnabled = false;

        FakePreferenceStore preferences;
        FakeHost host;
        host.metrics = {1280, 800, 1280, 800, 1.0F};
        FakeUiBridge bridge;
        NullRmlRenderHost renderHost;
        rocket::GameRmlUi ui(
            preferences,
            host,
            bridge,
            renderHost,
            repositoryRootForRmlTests());
        assert(ui.initialize([](const std::string&) {}));
        ui.setPanelPresentation(rocket::buildGamePanelPresentation(panelContext));
        ui.setControllerPresentation(true, rocket::ControllerFamily::Xbox);
        ui.requestFocus("modal:map");
        ui.refresh();

        assert(ui.navigate(rocket::UiDirection::Down));
        const std::string arrivalFocus = ui.focusedId();
        assert(arrivalFocus == "action:arrival_landing");
        ui.shutdown();
    }

    // Thermal runaway uses the same physical destruction beat as a collision
    // while retaining its own failure cause and debrief copy.
    {
        const auto catalog = rocket::createDefaultContent();
        auto state = rocket::createNewGame(catalog, 0x7EADULL);
        assert(rocket::initializeLiveExpedition(state, catalog));
        assert(rocket::departHome(state, catalog) == rocket::ExpeditionResult::Applied);
        auto model = rocket::expeditionFlightModel(state, catalog);
        rocket::advanceExpeditionFlight(state.run.expedition, state.run.flight, model,
            rocket::expeditionEnvironment(state, catalog), rocket::solarSystemDefinition(), {0,1,false,true}, .05);
        state.run.expedition.location.frame = rocket::CoordinateFrame::System;
        state.run.expedition.location.bodyId.clear();
        state.run.flight.positionX = 8;
        state.run.flight.positionY = 5;
        state.run.flight.velocityX = state.run.flight.velocityY = 0;
        state.run.flight.heat = 1;
        state.run.flight.heatFailureSeconds = rocket::tuning::launch::pilotingHeatFailureSeconds * 4 - .1;
        rocket::captureSystemLocation(state.run.expedition.location, state.run.flight);
        state.screen = rocket::Screen::Flight;
        state.meta.campaignIntroductionAcknowledged = true;
        state.incomingMessages = {};
        AppFixture fixture;
        fixture.saves.value = rocket::serializeSaveData(rocket::captureSaveData(state));
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("continue_game");
        for (int frame=0; frame<300 && fixture.runner.app().currentScreen()!=static_cast<int>(rocket::Screen::Flight); ++frame) {
            fixture.host.now += 1.0/60.0;
            fixture.runner.frame();
        }
        fixture.controllers.frame.connected = true;
        fixture.controllers.frame.family = rocket::ControllerFamily::Xbox;
        fixture.controllers.frame.meaningfulInput = true;
        fixture.controllers.frame.leftY = -1.0;

        for (int frame = 0;
             frame < 2400 && !fixture.renderer.launchDestructionActive;
             ++frame) {
            fixture.host.now += 1.0 / 60.0;
            fixture.runner.frame();
        }
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Flight));
        assert(fixture.renderer.launchDestructionActive);
        assert(fixture.renderer.launchDestructionCause == rocket::LaunchFailureCause::ThermalRunaway);
        assert(fixture.runner.app().inputContext() == rocket::InputContext::Stamp);
        assert(fixture.host.hapticCount > 0);
        assert(fixture.renderer.sceneFadeToBlack == 0.0);
        const double frozenProgress = fixture.renderer.launchTravelProgress;
        const double frozenCourse = fixture.renderer.launchCourseOffset;

        fixture.runner.app().launchMove(1.0, -1.0);
        for (int frame = 0; frame < 20; ++frame) {
            fixture.host.now += 1.0 / 60.0;
            fixture.runner.frame();
        }
        assert(fixture.renderer.launchDestructionActive);
        assert(fixture.renderer.launchDestructionElapsed >
            rocket::tuning::session::flightDestructionHoldSeconds);
        assert(std::abs(fixture.renderer.launchTravelProgress - frozenProgress) < 0.000001);
        assert(std::abs(fixture.renderer.launchCourseOffset - frozenCourse) < 0.000001);

        for (int frame = 0;
             frame < 120 &&
             fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Flight);
             ++frame) {
            fixture.host.now += 1.0 / 60.0;
            fixture.runner.frame();
        }
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Hangar));
        assert(!fixture.renderer.launchDestructionActive);
        const auto recovered = rocket::deserializeSaveData(fixture.saves.value);
        assert(recovered && !recovered->expedition.wrecks.empty());
        fixture.runner.shutdown();
    }

    // Hangar keeps Details in the shared titlebar, with operations and launch
    // below it. Keep both axes explicit so a controller can reach the Details
    // gateway and cannot become stranded in the header on the Steam Deck.
    {
        const rocket::ContentCatalog catalog = rocket::createDefaultContent();
        rocket::GameState state = rocket::createNewGame(catalog, 0x48A6A2ULL);
        state.screen = rocket::Screen::Hangar;
        rocket::Random rng(0x48A6A2ULL);
        const rocket::PreparedLaunch launch = rocket::prepareLaunch(state, catalog, rng);
        rocket::PanelRenderContext panelContext {state, catalog, launch, launch};
        panelContext.firstTimeIntroductionsEnabled = false;

        FakePreferenceStore preferences;
        FakeHost host;
        host.metrics = {1280, 800, 1280, 800, 1.0F};
        FakeUiBridge bridge;
        NullRmlRenderHost renderHost;
        rocket::GameRmlUi ui(
            preferences,
            host,
            bridge,
            renderHost,
            repositoryRootForRmlTests());
        assert(ui.initialize([](const std::string&) {}));
        ui.setPanelPresentation(rocket::buildGamePanelPresentation(panelContext));
        ui.setControllerPresentation(true, rocket::ControllerFamily::Xbox);
        ui.requestFocus("modal:map");
        ui.refresh();

        assert(ui.navigate(rocket::UiDirection::Right));
        assert(ui.focusedId() == "modal:inventory");
        assert(ui.navigate(rocket::UiDirection::Right));
        assert(ui.focusedId().starts_with("modal:"));
        assert(ui.navigate(rocket::UiDirection::Down));
        assert(ui.focusedId() == "action:prepare_launch");
        assert(ui.navigate(rocket::UiDirection::Up));
        assert(ui.focusedId().starts_with("modal:"));
        assert(ui.focusedId() == "modal:map"
            || ui.focusedId() == "modal:inventory"
            || ui.focusedId() == "modal:hangar_details"
            || ui.focusedId() == "modal:system_menu");

        // Disabled controls are correctly absent from the focus list, but an
        // entirely disabled operation row must not break the route between
        // Details and the launch actions below it.
        state.run.credits = 0.0;
        state.run.shipDamage = 0;
        const rocket::PreparedLaunch unavailableOpsLaunch = rocket::prepareLaunch(state, catalog, rng);
        rocket::PanelRenderContext unavailableOpsContext {
            state,
            catalog,
            unavailableOpsLaunch,
            unavailableOpsLaunch};
        unavailableOpsContext.firstTimeIntroductionsEnabled = false;
        ui.setPanelPresentation(rocket::buildGamePanelPresentation(unavailableOpsContext));
        ui.requestFocus("modal:hangar_details");
        ui.refresh();

        assert(ui.navigate(rocket::UiDirection::Down));
        assert(ui.focusedId() == "action:prepare_launch");
        assert(ui.navigate(rocket::UiDirection::Up));
        assert(ui.focusedId() == "modal:map"
            || ui.focusedId() == "modal:inventory"
            || ui.focusedId() == "modal:hangar_details"
            || ui.focusedId() == "modal:system_menu");
        ui.shutdown();
    }

    // Drone Ops owns the full viewport. Its right-aligned titlebar and
    // workspace actions must stay inside the panel's 16 px edge even when
    // button padding is present at a compact resolution.
    {
        const rocket::ContentCatalog catalog = rocket::createDefaultContent();
        rocket::GameState state = rocket::createNewGame(catalog, 0xD20E0F5ULL);
        state.run.destinationIndex = 2;
        rocket::startSurfaceExpedition(state, catalog);
        state.meta.unlockKeys.push_back(rocket::content::unlock::droneBay);
        state.meta.unlockKeys.push_back(rocket::content::unlock::droneSupportSuite);
        state.meta.droneBaySlots = 2;
        state.meta.ownedDroneIds.push_back(rocket::content::drone::miningDrone);
        rocket::ensureDroneBayState(state, catalog);
        rocket::ui::briefings::acknowledge(
            state.meta.acknowledgedActivityBriefingIds,
            rocket::ui::briefings::miniDrones);
        state.screen = rocket::Screen::DroneOps;
        rocket::Random rng(0xD20E0F5ULL);
        const rocket::PreparedLaunch launch = rocket::prepareLaunch(state, catalog, rng);
        rocket::PanelRenderContext panelContext {state, catalog, launch, launch};
        panelContext.firstTimeIntroductionsEnabled = false;

        FakePreferenceStore preferences;
        FakeHost host;
        host.metrics = {1280, 800, 1280, 800, 1.0F};
        FakeUiBridge bridge;
        NullRmlRenderHost renderHost;
        std::string pointerAction;
        rocket::GameRmlUi ui(
            preferences,
            host,
            bridge,
            renderHost,
            repositoryRootForRmlTests());
        assert(ui.initialize([&pointerAction](const std::string& action) {
            pointerAction = action;
        }));
        ui.setPanelPresentation(rocket::buildGamePanelPresentation(panelContext));

        // Details is part of every drone card's controller path, and must
        // promote its full profile into the matching modal rather than
        // forcing a mouse-only route to the bundled capability chips.
        ui.requestFocus("modal:drone_details_0");
        ui.refresh();
        assert(ui.focusedId() == "modal:drone_details_0");
        assert(ui.activateFocused());
        assert(ui.modalOpen());
        ui.closeModal();

        // An empty bay still has to connect the shared titlebar, Drone Ops
        // workspace controls, and the roster. Otherwise controller focus can
        // become stranded in Map / Inventory / Menu after the last unequip.
        ui.setControllerPresentation(true, rocket::ControllerFamily::Xbox);
        ui.requestFocus("modal:map");
        ui.refresh();
        assert(ui.navigate(rocket::UiDirection::Down));
        assert(ui.focusedId() == "modal:surface");
        assert(ui.navigate(rocket::UiDirection::Down));
        assert(ui.focusedId().starts_with("modal:drone_details_")
            || ui.focusedId().starts_with("action:equip_drone:"));
        assert(ui.navigate(rocket::UiDirection::Up));
        assert(ui.focusedId() == "modal:surface");
        assert(ui.navigate(rocket::UiDirection::Up));
        assert(ui.focusedId() == "modal:map");

        // Active Drone Controls actions must be able to leave the roster for
        // Active Loadout, then return through the same horizontal seam.
        rocket::GameState loadedState = state;
        loadedState.meta.equippedDroneIds.push_back(rocket::content::drone::miningDrone);
        rocket::Random loadedRng(0xD20E0F6ULL);
        const rocket::PreparedLaunch loadedLaunch = rocket::prepareLaunch(loadedState, catalog, loadedRng);
        rocket::PanelRenderContext loadedPanelContext {loadedState, catalog, loadedLaunch, loadedLaunch};
        loadedPanelContext.firstTimeIntroductionsEnabled = false;
        ui.setPanelPresentation(rocket::buildGamePanelPresentation(loadedPanelContext));
        ui.requestFocus("action:equip_drone:1");
        ui.refresh();
        assert(ui.navigate(rocket::UiDirection::Right));
        // The purchased/unowned frame may expose no adjacent action until its
        // fabrication affordance is focused; navigation itself is the stable
        // contract here.
        assert(ui.navigate(rocket::UiDirection::Left));

        // The loadout is a visual 2 x 3 grid at every workspace height.
        // Directional navigation must follow those rows and columns instead
        // of treating the six slots as the former single vertical rail.
        rocket::GameState gridState = state;
        gridState.meta.droneBaySlots = 6;
        gridState.meta.ownedDroneIds.assign(6, rocket::content::drone::miningDrone);
        gridState.meta.equippedDroneIds.assign(6, rocket::content::drone::miningDrone);
        rocket::ensureDroneBayState(gridState, catalog);
        rocket::Random gridRng(0xD20E0F7ULL);
        const rocket::PreparedLaunch gridLaunch = rocket::prepareLaunch(gridState, catalog, gridRng);
        rocket::PanelRenderContext gridPanelContext {gridState, catalog, gridLaunch, gridLaunch};
        gridPanelContext.firstTimeIntroductionsEnabled = false;
        ui.setPanelPresentation(rocket::buildGamePanelPresentation(gridPanelContext));
        ui.requestFocus("action:unequip_drone_slot:0");
        ui.refresh();
        assert(ui.navigate(rocket::UiDirection::Right));
        assert(ui.focusedId() == "action:unequip_drone_slot:1");
        assert(ui.navigate(rocket::UiDirection::Down));
        assert(ui.focusedId() == "action:unequip_drone_slot:3");
        assert(ui.navigate(rocket::UiDirection::Left));
        assert(ui.focusedId() == "action:unequip_drone_slot:2");
        assert(ui.navigate(rocket::UiDirection::Down));
        assert(ui.focusedId() == "action:unequip_drone_slot:4");
        assert(ui.navigate(rocket::UiDirection::Right));
        assert(ui.focusedId() == "action:unequip_drone_slot:5");

        const auto click = [&ui](int x, int y) {
            ui.mouseDown(x, y, 0);
            ui.mouseUp(x, y, 0);
        };

        click(1272, 35);
        assert(!ui.modalOpen());
        click(1220, 35);
        ui.render();
        assert(ui.modalOpen());
        ui.closeModal();

        pointerAction.clear();
        click(1272, 112);
        assert(pointerAction.empty());
        click(1150, 112);
        assert(pointerAction.empty());
        ui.render();
        assert(pointerAction == rocket::ui::actions::backToSurfaceOps);

        // A tall desktop viewport used to reinstate the vertical rail. Keep
        // the same grid and its controller mapping after the layout relaxes.
        host.metrics = {1920, 1200, 1920, 1200, 1.0F};
        ui.setPanelPresentation(rocket::buildGamePanelPresentation(gridPanelContext));
        ui.requestFocus("action:unequip_drone_slot:0");
        ui.refresh();
        assert(ui.navigate(rocket::UiDirection::Right));
        assert(ui.focusedId() == "action:unequip_drone_slot:1");
        assert(ui.navigate(rocket::UiDirection::Down));
        assert(ui.focusedId() == "action:unequip_drone_slot:3");
        ui.shutdown();
    }
#endif

    // Render views must alias authoritative storage and retain the complete
    // enemy array so a mini-drone's gameplay target index is unchanged.
    rocket::MiningRunState renderViewFixture;
    renderViewFixture.terrain.cells.resize(2);
    renderViewFixture.gate.markers.resize(1);
    renderViewFixture.enemies.resize(3);
    renderViewFixture.enemies[0].active = false;
    renderViewFixture.enemies[1].type = rocket::MiningEnemyType::Beetle;
    renderViewFixture.miniDrones.resize(1);
    renderViewFixture.miniDrones[0].targetEnemyIndex = 1;
    renderViewFixture.combatProjectiles.resize(1);
    renderViewFixture.damageNumbers.resize(1);
    rocket::RenderSnapshot renderViewSnapshot;
    renderViewSnapshot.bindMiningFrameViews(renderViewFixture);
    assert(renderViewSnapshot.miningCells.data() == renderViewFixture.terrain.cells.data());
    assert(renderViewSnapshot.miningGateMarkers.data() == renderViewFixture.gate.markers.data());
    assert(renderViewSnapshot.miningEnemies.data() == renderViewFixture.enemies.data());
    assert(renderViewSnapshot.miningMiniDrones.data() == renderViewFixture.miniDrones.data());
    assert(renderViewSnapshot.miningProjectiles.data() == renderViewFixture.combatProjectiles.data());
    assert(renderViewSnapshot.miningDamageNumbers.data() == renderViewFixture.damageNumbers.data());
    const int targetEnemyIndex = renderViewSnapshot.miningMiniDrones.front().targetEnemyIndex;
    assert(targetEnemyIndex == 1);
    assert(renderViewSnapshot.miningEnemies[static_cast<std::size_t>(targetEnemyIndex)].type == rocket::MiningEnemyType::Beetle);

    // Empty and corrupt storage both start at the title without offering a
    // Continue action. Save detection must validate the payload, not merely
    // test whether storage returned non-empty bytes.
    {
        AppFixture fixture;
        assert(fixture.runner.initialize());
        assert(fixture.saves.loadCount == 1);
        assert(fixture.saves.storeCount == 0);
        assert(fixture.ui.html.find("data-panel-mode=\"title\"") != std::string::npos);
        assert(fixture.ui.html.find("data-rr-action=\"new_game\"") != std::string::npos);
        assert(fixture.ui.html.find("data-rr-action=\"continue_game\"") == std::string::npos);
        assert(fixture.bridge.hostContext.screen == fixture.ui.presentation.metadata.screen);
        assert(fixture.bridge.hostContext.titleScreenActive);
        fixture.runner.shutdown();
    }
    {
        AppFixture fixture;
        fixture.saves.value = "not a Rocket Rogue save";
        assert(fixture.runner.initialize());
        assert(fixture.saves.loadCount == 1);
        assert(fixture.ui.html.find("data-panel-mode=\"title\"") != std::string::npos);
        assert(fixture.ui.html.find("data-rr-action=\"continue_game\"") == std::string::npos);
        fixture.runner.shutdown();
    }

    // Performance-overlay publication can consume time immediately and defer
    // geometry/layout work until the following UI render. Neither cost belongs
    // in the gameplay frame or CPU percentiles reported by that overlay.
    {
        AppFixture fixture;
        fixture.preferences.value.performanceStatsEnabled = true;
        bool deferredOverlayWork = false;
        fixture.ui.performanceStatsTimingHook = [&]() {
            fixture.host.now += 0.050;
            deferredOverlayWork = true;
        };
        fixture.ui.renderTimingHook = [&]() {
            if (!deferredOverlayWork) return;
            fixture.host.now += 0.080;
            deferredOverlayWork = false;
        };

        assert(fixture.runner.initialize());
        for (int frame = 0; frame < 30 && fixture.ui.performanceStatsSetCount < 2; ++frame) {
            fixture.host.now += 1.0 / 60.0;
            fixture.runner.frame();
        }
        assert(fixture.ui.performanceStatsSetCount >= 2);
        assert(fixture.ui.performanceStatsVisible);
        assert(fixture.ui.lastPerformanceStats.p95FrameTimeMilliseconds < 20.0);
        assert(fixture.ui.lastPerformanceStats.p95CpuFrameMilliseconds < 1.0);
        fixture.runner.shutdown();
    }

    // A valid save is restored once and held motionless behind the animated
    // title. Continue only dismisses the title; it does not rewrite progress.
    {
        AppFixture fixture;
        fixture.saves.value = activeMiningSave(0.78);
        const std::string originalSave = fixture.saves.value;
        assert(fixture.runner.initialize());
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Mining));
        assert(fixture.ui.html.find("data-rr-action=\"continue_game\"") != std::string::npos);
        const std::size_t continuePosition = fixture.ui.html.find("data-rr-action=\"continue_game\"");
        const std::size_t separatorPosition = fixture.ui.html.find("title-menu-separator");
        const std::size_t newGamePosition = fixture.ui.html.find("data-modal-open=\"new_game_confirm\"");
        assert(continuePosition < separatorPosition && separatorPosition < newGamePosition);
        fixture.host.now += 0.20;
        fixture.runner.frame();
        assert(fixture.renderer.titleScreen);
        assert(fixture.renderer.screen == rocket::Screen::Hangar);
        assert(fixture.saves.storeCount == 0);
        assert(fixture.saves.value == originalSave);

        fixture.ui.dispatchAction("continue_game");
        fixture.runner.resetFrameClock();
        for (int frame = 0; frame < 7; ++frame) {
            fixture.host.now += 0.25;
            fixture.runner.frame();
        }
        assert(fixture.renderer.titleScreen);
        assert(fixture.renderer.sceneFadeToBlack > 0.0 && fixture.renderer.sceneFadeToBlack < 1.0);
        assert(fixture.ui.presentation.runtime.sceneTransitionActive);
        assert(fixture.ui.html.empty());
        for (int frame = 0; frame < 4 && fixture.renderer.titleScreen; ++frame) {
            fixture.host.now += 0.25;
            fixture.runner.frame();
        }
        assert(!fixture.renderer.titleScreen);
        assert(fixture.renderer.screen == rocket::Screen::Mining);
        assert(fixture.renderer.sceneFadeToBlack > 0.0);
        assert(fixture.ui.html.empty());
        assert(std::abs(fixture.renderer.shipDamage - 37.0) < 0.0001);
        assert(std::abs(fixture.renderer.miningHeat - 0.78) < 0.0001);
        assert(fixture.saves.storeCount == 0);
        assert(fixture.saves.value == originalSave);
        fixture.host.now += 0.25;
        fixture.runner.frame();
        assert(fixture.renderer.sceneFadeToBlack == 0.0);
        assert(!fixture.ui.presentation.runtime.sceneTransitionActive);
        assert(fixture.ui.html.find("rr-scene-transition") == std::string::npos);
        fixture.runner.shutdown();
    }

    // Filling the ship is allowed without interruption. The warning begins
    // only after newly mined ore exists outside that already-full hold.
    {
        const auto shipOverflowWarningPending = [](const std::string& payload) {
            const auto save = rocket::deserializeSaveData(payload);
            assert(save.has_value());
            return std::any_of(
                save->incomingMessages.pending.begin(),
                save->incomingMessages.pending.end(),
                [](const rocket::IncomingMessageOccurrence& message) {
                    return message.messageId == "ship_full_tip";
                });
        };

        AppFixture exactlyFull;
        exactlyFull.saves.value = fullShipMiningSave(false);
        assert(exactlyFull.runner.initialize());
        exactlyFull.ui.dispatchAction("continue_game");
        completeTitleLaunch(exactlyFull);
        exactlyFull.host.now += 1.0 / 60.0;
        exactlyFull.runner.frame();
        assert(!shipOverflowWarningPending(exactlyFull.saves.value));
        exactlyFull.runner.shutdown();

        AppFixture overCapacity;
        overCapacity.saves.value = fullShipMiningSave(true);
        assert(overCapacity.runner.initialize());
        overCapacity.ui.dispatchAction("continue_game");
        completeTitleLaunch(overCapacity);
        overCapacity.host.now += 1.0 / 60.0;
        overCapacity.runner.frame();
        assert(shipOverflowWarningPending(overCapacity.saves.value));
        overCapacity.runner.shutdown();
    }

    // Depart Planet keeps the landed scene alive for the complete bay-close,
    // ignition, and ascent ritual. Settlement happens once in memory, but the
    // save is not replaced until the cinematic hands off to the next screen.
    {
        AppFixture fixture;
        fixture.saves.value = readyMiningDepartureSave();
        const std::string preDepartureSave = fixture.saves.value;
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("continue_game");
        completeTitleLaunch(fixture);
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Mining));
        assert(fixture.ui.html.find("data-rr-action=\"mining_depart\"") != std::string::npos);
        const int storesBeforeDeparture = fixture.saves.storeCount;

        fixture.ui.dispatchAction("mining_depart");
        fixture.host.now += 1.0 / 120.0;
        fixture.runner.frame();
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Mining));
        assert(fixture.renderer.miningExtractionActive);
        assert(fixture.renderer.miningExtractionProgress > 0.0);
        assert(fixture.ui.html.find("data-mining-extraction=\"1\"") != std::string::npos);
        assert(fixture.ui.html.find("DEPARTING") != std::string::npos);
        assert(fixture.saves.storeCount == storesBeforeDeparture);
        assert(fixture.saves.value == preDepartureSave);

        // Repeated activation during the ritual is inert and cannot bank the
        // recovered Rare Ore a second time.
        fixture.ui.dispatchAction("mining_depart");
        assert(fixture.saves.storeCount == storesBeforeDeparture);
        assert(fixture.saves.value == preDepartureSave);

        for (int frame = 0; frame < 32; ++frame) {
            fixture.host.now += 0.10;
            fixture.runner.frame();
        }
        assert(fixture.renderer.miningExtractionActive);
        assert(fixture.renderer.miningExtractionProgress > 0.90);
        assert(fixture.renderer.miningExtractionProgress < 1.0);
        assert(fixture.saves.value == preDepartureSave);

        for (int frame = 0; frame < 4 && fixture.renderer.sceneFadeToBlack <= 0.0; ++frame) {
            fixture.host.now += 0.10;
            fixture.runner.frame();
        }
        assert(fixture.renderer.miningExtractionActive);
        assert(fixture.renderer.miningExtractionProgress == 1.0);
        assert(fixture.renderer.sceneFadeToBlack > 0.0);
        assert(fixture.saves.value == preDepartureSave);

        advanceSceneHandoff(fixture);
        assert(!fixture.renderer.miningExtractionActive);
        assert(fixture.runner.app().currentScreen() != static_cast<int>(rocket::Screen::Mining));
        assert(fixture.runner.app().currentScreen() != static_cast<int>(rocket::Screen::SurfaceExpedition));
        assert(fixture.saves.storeCount == storesBeforeDeparture + 1);
        const std::optional<rocket::SaveData> departed = rocket::deserializeSaveData(fixture.saves.value);
        assert(departed.has_value());
        assert(departed->materials.rare == 1);
        assert(!departed->planetaryExpedition.active);
        assert(!departed->mining.active);
        fixture.runner.shutdown();
    }

    // Planet arrival is a short automatic ceremony, not another results
    // modal. It owns the impact haptic and presentation for two seconds, then
    // hands control to the prepared Approach exactly once.
    {
        AppFixture fixture;
        assert(fixture.runner.initialize());
        fixture.controllers.frame.connected = true;
        fixture.controllers.frame.family = rocket::ControllerFamily::Xbox;
        fixture.controllers.frame.meaningfulInput = true;
        fixture.host.now += 1.0 / 120.0;
        fixture.runner.frame();
        const int storesBeforeArrival = fixture.saves.storeCount;
        const int hapticsBeforeArrival = fixture.host.hapticCount;
        fixture.runner.app().debugShowArrivalCelebration();
        fixture.host.now += 1.0 / 120.0;
        fixture.runner.frame();
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::ArrivalFanfare));
        assert(fixture.runner.app().inputContext() == rocket::InputContext::Stamp);
        assert(fixture.ui.html.find("data-arrival-fanfare=\"1\"") != std::string::npos);
        assert(fixture.ui.html.find("ARRIVAL CONFIRMED") != std::string::npos);
        assert(fixture.ui.html.find("data-rr-action=") == std::string::npos);
        assert(fixture.host.hapticCount == hapticsBeforeArrival + 1);
        assert(fixture.saves.storeCount == storesBeforeArrival);

        // The retired skip action is inert; no key/button can turn this beat
        // into either a prompt or an early-dismiss path.
        fixture.ui.dispatchAction("skip_arrival_fanfare");
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::ArrivalFanfare));

        for (int frame = 0; frame < 119; ++frame) {
            fixture.host.now += 1.0 / 60.0;
            fixture.runner.frame();
        }
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::ArrivalFanfare));

        fixture.host.now += 1.0 / 60.0;
        fixture.runner.frame();
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Mining));
        assert(fixture.runner.app().inputContext() != rocket::InputContext::Stamp);
        assert(fixture.ui.html.find("data-arrival-fanfare=\"1\"") == std::string::npos);
        assert(fixture.saves.storeCount == storesBeforeArrival);
        fixture.runner.shutdown();
    }

    // EVA death gets a presentation-only impact beat and camera blackout
    // before the unchanged mining-failure modal becomes actionable.
    {
        AppFixture fixture;
        fixture.saves.value = evaDeathMiningSave();
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("continue_game");
        completeTitleLaunch(fixture);
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Mining));

        fixture.host.now += 1.0 / 60.0;
        fixture.runner.frame();
        assert(fixture.renderer.miningEvaDeathActive);
        assert(fixture.ui.html.find("data-modal=\"mining_failure\" data-auto-modal=\"1\"") == std::string::npos);
        fixture.runner.app().miningFailureAck();
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Mining));

        for (int frame = 0; frame < 3; ++frame) {
            fixture.host.now += 0.25;
            fixture.runner.frame();
        }
        assert(fixture.renderer.miningEvaDeathActive);
        assert(fixture.renderer.miningEvaDeathProgress > 0.0 && fixture.renderer.miningEvaDeathProgress < 1.0);
        assert(fixture.renderer.sceneFadeToBlack == 0.0);
        assert(fixture.ui.html.find("data-modal=\"mining_failure\" data-auto-modal=\"1\"") == std::string::npos);

        bool sawCameraFade = false;
        for (int frame = 0; frame < 16; ++frame) {
            fixture.host.now += 0.25;
            fixture.runner.frame();
            sawCameraFade = sawCameraFade || fixture.renderer.sceneFadeToBlack > 0.0;
            if (fixture.ui.html.find("data-modal=\"mining_failure\" data-auto-modal=\"1\"") != std::string::npos) {
                break;
            }
        }
        assert(sawCameraFade);
        assert(fixture.renderer.sceneFadeToBlack == 0.0);
        assert(!fixture.renderer.miningEvaDeathActive);
        assert(fixture.ui.html.find("data-modal=\"mining_failure\" data-auto-modal=\"1\"") != std::string::npos);
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Mining));
        fixture.runner.shutdown();
    }

    // A live EVA loss returns to the surviving ship, with an actionable mining
    // bay, without entering the retired Surface Ops or remote Hangar screens.
    {
        AppFixture fixture;
        fixture.saves.value = liveEvaDeathMiningSave();
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("continue_game");
        completeTitleLaunch(fixture);
        for (int frame = 0; frame < 24; ++frame) {
            fixture.host.now += 0.25;
            fixture.runner.frame();
            if (fixture.ui.html.find("data-modal=\"mining_failure\" data-auto-modal=\"1\"") != std::string::npos)
                break;
        }
        assert(fixture.ui.html.find("data-modal=\"mining_failure\" data-auto-modal=\"1\"") != std::string::npos);
        assert(fixture.ui.html.find("Return to Surface Ops") == std::string::npos);
        fixture.ui.dispatchAction("mining_failure_ack");
        for (int frame = 0; frame < 10; ++frame) {
            fixture.host.now += 0.25;
            fixture.runner.frame();
        }
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Mining));
        const auto saved = rocket::deserializeSaveData(fixture.saves.value);
        assert(saved && saved->mining.active && !saved->mining.failurePending);
        assert(saved->flight.landing.siteCommitted && saved->planetaryExpedition.active);
        assert(saved->mining.depthZone == saved->mining.shipDepthZone);
        assert(rocket::miningAtReturnZone(saved->mining));
        assert(saved->expedition.cargo.materials.common == 4);
        assert(saved->expedition.progression.expeditionLevel == 2);
        assert(saved->expedition.progression.runRigUpgradeRanks.size() == 1);
        assert(saved->expedition.wrecks.empty());
        assert(fixture.ui.presentation.contentMarkup.find("data-rr-action=\"mining_depart\"") != std::string::npos);
        assertNoLegacyRecoveryActions(fixture.ui.presentation);
        fixture.runner.shutdown();
    }

    // Disabled Mining Rigs are valid EVA tow targets. The render snapshot
    // must retain that active line so the player can see what the suit is
    // recovering after a save/restore cycle.
    {
        AppFixture fixture;
        fixture.saves.value = disabledRigEvaTowSave();
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("continue_game");
        completeTitleLaunch(fixture);
        assert(fixture.renderer.screen == rocket::Screen::Mining);
        assert(fixture.renderer.miningOperatorRigTethered);
        fixture.runner.shutdown();
    }

    // Launch lesson previews are deterministic, immediately playable debug
    // sandboxes. Moving between all four lessons and back to the real campaign
    // must never write or replace the player's persisted save.
    {
        AppFixture fixture;
        fixture.saves.value = activeMiningSave(0.37);
        const std::string originalSave = fixture.saves.value;
        assert(fixture.runner.initialize());
        const int originalStoreCount = fixture.saves.storeCount;

        struct ExpectedLaunchLesson {
            bool manualControls;
            bool heat;
            bool asteroids;
            bool arrival;
            double fuelCapacity;
            int asteroidCount;
            std::string_view objective;
        };
        static constexpr std::array<ExpectedLaunchLesson, 4> expected {{
            {false, false, false, false, 10.0, 0, "TURN AROUND ON LOW FUEL"},
            {true, false, false, false, 15.0, 0, "CALIBRATE FLIGHT CONTROLS"},
            {true, true, false, true, 20.0, 0, "REACH Mars"},
            {true, true, true, true, 25.0, 10, "REACH Jupiter"}
        }};

        for (int lessonIndex = 0; lessonIndex < static_cast<int>(expected.size()); ++lessonIndex) {
            fixture.runner.app().debugStartLaunchLesson(lessonIndex);
            assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Flight));
            assert(fixture.runner.app().inputContext() == rocket::InputContext::Launch);
            fixture.host.now += 1.0 / 120.0;
            fixture.runner.frame();

            const ExpectedLaunchLesson& lesson = expected[static_cast<std::size_t>(lessonIndex)];
            assert(fixture.renderer.launchManualControlsEnabled == lesson.manualControls);
            assert(fixture.renderer.launchHeatEnabled == lesson.heat);
            assert(fixture.renderer.launchAsteroidsEnabled == lesson.asteroids);
            assert(fixture.renderer.launchFrontierTransfer == lesson.arrival);
            assert(std::abs(fixture.renderer.launchFuelCapacity - lesson.fuelCapacity) < 0.0001);
            assert(fixture.renderer.launchAsteroidCount == lesson.asteroidCount);
            assert(fixture.saves.storeCount == originalStoreCount);
            assert(fixture.saves.value == originalSave);
        }

        fixture.runner.app().debugExit();
        fixture.host.now += 1.0 / 120.0;
        fixture.runner.frame();
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Mining));
        assert(fixture.saves.storeCount == originalStoreCount);
        assert(fixture.saves.value == originalSave);

        const std::uint64_t stateBeforeInvalidLesson = fixture.runner.app().deterministicStateHash();
        fixture.runner.app().debugStartLaunchLesson(5);
        assert(fixture.runner.app().deterministicStateHash() == stateBeforeInvalidLesson);
        assert(fixture.saves.value == originalSave);
        fixture.runner.app().debugStartLaunchLesson(4);
        for (int frame=0;frame<48;++frame) {
            fixture.host.now += 1.0/60.0;
            fixture.runner.frame();
        }
        assert(fixture.ui.html.find("Asteroid belt ahead")!=std::string::npos);
        assert(fixture.ui.html.find("Hull Plating")!=std::string::npos);
        assert(fixture.ui.html.find("Flight Controls")!=std::string::npos);
        assert(fixture.saves.value == originalSave);
        assert(fixture.saves.storeCount == originalStoreCount);
        fixture.runner.shutdown();
    }

    // Once Fuel and Flight Controls are calibrated, even the green current-
    // destination launch is an arrival flight. It must not silently fall back
    // to the retired proving-return mode.
    {
        const rocket::ContentCatalog catalog = rocket::createDefaultContent();
        rocket::GameState moonState = rocket::createNewGame(catalog, 0xA221);
        moonState.run.destinationIndex = 1;
        moonState.meta.furthestTier = 1;
        moonState.meta.launchLessons.stage = rocket::LaunchTrainingStage::ThermalManagement;
        moonState.meta.launchUpgrades.fuelTanks = 2;
        moonState.meta.launchUpgrades.flightControls = 1;
        moonState.screen = rocket::Screen::Hangar;
        rocket::syncLaunchConfig(moonState, catalog);

        AppFixture fixture;
        fixture.saves.value = rocket::serializeSaveData(rocket::captureSaveData(moonState));
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("continue_game");
        completeTitleLaunch(fixture);
        fixture.runner.app().prepareForLaunch();
        fixture.host.now += 1.0 / 120.0;
        fixture.runner.frame();
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Flight));
        assert(fixture.ui.requestedFocusId == "action:start_launch");
        assert(fixture.renderer.launchFrontierTransfer);
        assert(fixture.renderer.launchDestinationTier == 1);
        fixture.runner.shutdown();
    }

    // Refit cards install directly; no separate preview-selection action or
    // duplicate selected-offer panel is required.
    {
        AppFixture fixture;
        assert(fixture.runner.initialize());
        fixture.runner.app().debugShowRefit();
        fixture.host.now += 1.0 / 60.0;
        fixture.runner.frame();
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Upgrade));
        assert(fixture.ui.html.find("data-rr-action=\"buy_offer:0\"") != std::string::npos);
        assert(fixture.ui.html.find("data-rr-action=\"buy_offer:1\"") != std::string::npos);
        assert(fixture.ui.html.find("data-rr-action=\"buy_offer:2\"") != std::string::npos);
        assert(fixture.ui.html.find("selected-refit-detail") == std::string::npos);
        fixture.runner.shutdown();
    }

    // Level Up cards commit directly, then retain the chosen border for the
    // short resolve beat before returning to the continuous Mining activity.
    {
        AppFixture fixture;
        assert(fixture.runner.initialize());
        fixture.runner.app().debugShowSurfaceUpgrade();
        fixture.host.now += 1.0 / 60.0;
        fixture.runner.frame();
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::SurfaceUpgrade));
        assert(fixture.ui.html.find("data-rr-action=\"surface_upgrade:0\"") != std::string::npos);
        assert(fixture.ui.html.find("data-rr-action=\"surface_upgrade:1\"") != std::string::npos);
        assert(fixture.ui.html.find("data-rr-action=\"surface_upgrade:2\"") != std::string::npos);
        assert(fixture.ui.html.find("selected-upgrade-detail") == std::string::npos);

        fixture.ui.dispatchAction("surface_upgrade:1");
        fixture.host.now += 0.11;
        fixture.runner.frame();
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Mining));
        fixture.runner.shutdown();
    }

    // Old saved assists cannot inject fuel, velocity, or steering penalties.
    {
        const auto catalog = rocket::createDefaultContent();
        auto state = rocket::createNewGame(catalog, 77);
        const auto saved = rocket::deserializeSaveData(activeJupiterSlingshotSave());
        assert(saved.has_value());
        rocket::restoreSaveData(state, catalog, *saved);
        rocket::Random rng(77);
        const auto launch = rocket::prepareLaunch(state, catalog, rng);
        const auto restoredFlight = rocket::beginLaunchFlight(launch, rocket::currentDestination(state, catalog));
        state.run.pendingTransferAssist = {};
        state.run.nextLaunchFuelBoost = 0.0;
        state.run.nextLaunchSpeedBoost = 0.0;
        state.run.nextLaunchInstabilityPenalty = 0.0;
        rocket::Random baselineRng(77);
        const auto baseline = rocket::prepareLaunch(state, catalog, baselineRng);
        const auto baselineFlight = rocket::beginLaunchFlight(baseline, rocket::currentDestination(state, catalog));
        assert(restoredFlight.fuelRemaining == baselineFlight.fuelRemaining);
        assert(restoredFlight.velocityX == baselineFlight.velocityX);
        assert(restoredFlight.velocityY == baselineFlight.velocityY);
        assert(launch.controlChaos == baseline.controlChaos);
    }


    // Failed replacement preserves both the prior save and the title barrier.
    {
        AppFixture fixture;
        fixture.saves.value = activeMiningSave(0.65);
        const std::string originalSave = fixture.saves.value;
        fixture.saves.failStore = true;
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("new_game");
        completeTitleLaunch(fixture);
        assert(fixture.renderer.titleScreen);
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Mining));
        assert(fixture.ui.html.find("data-panel-mode=\"title\"") != std::string::npos);
        assert(fixture.saves.storeCount == 1);
        assert(fixture.saves.clearCount == 0);
        assert(fixture.saves.value == originalSave);
        fixture.runner.shutdown();
    }

    // A global modal is the only controller focus scope while it is visible.
    // Mapped Accept activates its focused control, direct shortcuts cannot
    // replace it, and one mapped Cancel closes only that modal layer.
    {
        AppFixture fixture;
        fixture.preferences.value.controller.swapConfirmCancel = true;
        assert(fixture.runner.initialize());
        fixture.runner.app().debugStartMining();
        fixture.host.now += 1.0 / 60.0;
        fixture.runner.frame();
        assert(fixture.runner.app().inputContext() == rocket::InputContext::MiningActive);

        fixture.ui.openModal("settings");
        // Give the app one neutral frame to observe the externally opened UI
        // modal and fence any realtime input before taking the state snapshot.
        fixture.host.now += 1.0 / 60.0;
        fixture.runner.frame();
        const std::uint64_t stateBeforeModalInput = fixture.runner.app().deterministicStateHash();
        fixture.controllers.frame.connected = true;
        fixture.controllers.frame.family = rocket::ControllerFamily::Xbox;
        fixture.controllers.frame.meaningfulInput = true;
        fixture.controllers.frame.leftY = 0.85;
        fixture.controllers.frame.pressed.set(static_cast<std::size_t>(rocket::ControllerButton::East));
        fixture.host.now += 1.0 / 60.0;
        fixture.runner.frame();
        assert(fixture.runner.app().inputContext() == rocket::InputContext::Paused);
        assert(fixture.ui.activateFocusedCount == 1);
        assert(fixture.ui.cancelCount == 0);
        assert(fixture.ui.modalOpenValue);
        assert(fixture.runner.app().deterministicStateHash() == stateBeforeModalInput);

        const int modalOpenCountBeforeShortcut = fixture.ui.openModalCount;
        fixture.controllers.frame.pressed.set(static_cast<std::size_t>(rocket::ControllerButton::Menu));
        fixture.host.now += 1.0 / 60.0;
        fixture.runner.frame();
        assert(fixture.ui.openModalCount == modalOpenCountBeforeShortcut);
        assert(fixture.ui.lastOpenedModal == "settings");
        assert(fixture.ui.modalOpenValue);
        assert(fixture.runner.app().deterministicStateHash() == stateBeforeModalInput);

        fixture.controllers.frame.pressed.set(static_cast<std::size_t>(rocket::ControllerButton::South));
        fixture.host.now += 1.0 / 60.0;
        fixture.runner.frame();
        assert(fixture.ui.cancelCount == 1);
        assert(!fixture.ui.modalOpenValue);
        assert(fixture.runner.app().inputContext() == rocket::InputContext::Paused);
        assert(fixture.runner.app().deterministicStateHash() == stateBeforeModalInput);

        fixture.controllers.frame.leftY = 0.0;
        fixture.controllers.frame.meaningfulInput = false;
        fixture.host.now += 1.0 / 60.0;
        fixture.runner.frame();
        assert(fixture.runner.app().inputContext() == rocket::InputContext::MiningActive);
        assert(fixture.ui.cancelCount == 1);
        fixture.runner.shutdown();
    }

    // Launch steering keeps the controller's screen-space X direction through
    // routing and app dispatch: stick-left must move the ship left.
    {
        AppFixture fixture;
        assert(fixture.runner.initialize());
        fixture.runner.app().debugStartLaunchLesson(1);
        fixture.host.now += 1.0 / 60.0;
        fixture.runner.frame();
        assert(fixture.runner.app().inputContext() == rocket::InputContext::Launch);

        fixture.controllers.frame.connected = true;
        fixture.controllers.frame.family = rocket::ControllerFamily::Xbox;
        fixture.controllers.frame.meaningfulInput = true;
        fixture.controllers.frame.leftX = -0.80;
        for (int frame = 0; frame < 60; ++frame) {
            fixture.host.now += 1.0 / 60.0;
            fixture.runner.frame();
        }
        assert(fixture.renderer.launchCourseVelocity < 0.0);
        fixture.runner.shutdown();
    }

    // A blocking result modal keeps controller focus without advancing the
    // result-scene animation underneath the focused Continue action.
    {
        AppFixture fixture;
        assert(fixture.runner.initialize());
        fixture.runner.app().debugShowResults();
        fixture.host.now += 1.0 / 60.0;
        fixture.runner.frame();
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Results));
        assert(fixture.ui.html.find("<template data-modal=\"launch_outcome\" data-auto-modal=\"1\"") != std::string::npos);
        assert(fixture.ui.html.find("data-rr-action=\"next\"") != std::string::npos);
        const auto launchOutcomeModal = std::find_if(
            fixture.ui.presentation.modals.begin(),
            fixture.ui.presentation.modals.end(),
            [](const rocket::ModalPresentation& modal) {
                return modal.id == rocket::ui::modals::launchOutcome;
            });
        assert(launchOutcomeModal != fixture.ui.presentation.modals.end());

        fixture.ui.openModal(std::string(rocket::ui::modals::launchOutcome));
        fixture.ui.focusedIdValue = "action:next";
        fixture.controllers.frame.connected = true;
        fixture.controllers.frame.family = rocket::ControllerFamily::Xbox;
        fixture.controllers.frame.meaningfulInput = true;
        fixture.controllers.frame.navigation = rocket::UiDirection::Down;
        fixture.controllers.frame.leftY = 0.85;
        const double resultAnimationBeforeNavigation = fixture.renderer.animationTime;
        const std::uint64_t stateBeforeNavigation = fixture.runner.app().deterministicStateHash();

        fixture.host.now += 1.0 / 60.0;
        fixture.runner.frame();
        assert(fixture.runner.app().inputContext() == rocket::InputContext::Paused);
        assert(fixture.ui.lastNavigation == rocket::UiDirection::Down);
        assert(fixture.ui.focusedIdValue == "action:next");
        assert(std::abs(fixture.renderer.animationTime - resultAnimationBeforeNavigation) < 0.000001);
        assert(fixture.runner.app().deterministicStateHash() == stateBeforeNavigation);
        fixture.runner.shutdown();
    }

    // Reaching the Moon during the uncalibrated controls lesson freezes the
    // flight into a visible impact cinematic before the existing red result
    // modal resolves the destructive collision.
    {
        AppFixture fixture;
        assert(fixture.runner.initialize());
        fixture.runner.app().debugStartLaunchLesson(1);
        fixture.controllers.frame.connected = true;
        fixture.controllers.frame.family = rocket::ControllerFamily::Xbox;
        fixture.controllers.frame.meaningfulInput = true;

        for (int frame = 0;
             frame < 1500 && !fixture.renderer.launchDestructionActive;
             ++frame) {
            const double steer = std::clamp(
                -fixture.renderer.launchCourseOffset * 5.5 -
                    fixture.renderer.launchCourseVelocity * 2.4,
                -1.0,
                1.0);
            fixture.controllers.frame.leftX = steer;
            fixture.host.now += 1.0 / 60.0;
            fixture.runner.frame();
        }
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Flight));
        assert(fixture.renderer.launchDestructionActive);
        assert(fixture.renderer.launchDestructionCause == rocket::LaunchFailureCause::LunarImpact);
        assert(!fixture.renderer.launchLandingAuthorized);
        assert(!fixture.renderer.launchLandingLocalFrame);
        assert(fixture.renderer.sceneFadeToBlack == 0.0);
        assert(fixture.ui.presentation.contentMarkup.find("rr-hud-launch-status") != std::string::npos);
        assert(fixture.runner.app().inputContext() == rocket::InputContext::Stamp);
        assert(fixture.host.hapticCount > 0);
        const double collisionProgress = fixture.renderer.launchTravelProgress;
        const double collisionCourse = fixture.renderer.launchCourseOffset;

        fixture.runner.app().launchMove(-1.0, 1.0);
        fixture.runner.app().returnHome();
        fixture.runner.app().cutEngines();
        for (int frame = 0; frame < 20; ++frame) {
            fixture.host.now += 1.0 / 60.0;
            fixture.runner.frame();
        }
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Flight));
        assert(fixture.renderer.launchDestructionActive);
        assert(fixture.renderer.launchDestructionElapsed > rocket::tuning::session::flightDestructionHoldSeconds);
        assert(std::abs(fixture.renderer.launchTravelProgress - collisionProgress) < 0.000001);
        assert(std::abs(fixture.renderer.launchCourseOffset - collisionCourse) < 0.000001);

        for (int frame = 0;
             frame < 120 &&
             fixture.renderer.launchDestructionElapsed <
                 rocket::tuning::session::flightDestructionSequenceSeconds - 2.0 / 60.0;
             ++frame) {
            fixture.host.now += 1.0 / 60.0;
            fixture.runner.frame();
        }
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Flight));
        assert(fixture.renderer.launchDestructionActive);
        for (int frame = 0;
             frame < 5 && fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Flight);
             ++frame) {
            fixture.host.now += 1.0 / 60.0;
            fixture.runner.frame();
        }
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Results));
        assert(!fixture.renderer.launchDestructionActive);
        assert(fixture.renderer.lastLaunchFailureCause == rocket::LaunchFailureCause::LunarImpact);
        const auto lunarImpactModal = std::find_if(
            fixture.ui.presentation.modals.begin(),
            fixture.ui.presentation.modals.end(),
            [](const rocket::ModalPresentation& modal) {
                return modal.id == rocket::ui::modals::launchOutcome;
            });
        assert(lunarImpactModal != fixture.ui.presentation.modals.end());
        assert(lunarImpactModal->tone == rocket::ModalTone::Negative);
        fixture.runner.shutdown();
    }

    FakeSaveStore saves;
    FakePreferenceStore preferences;
    FakeHost host;
    FakeController controllers;
    FakeTextureSource textures;
    FakeRenderer renderer;
    FakeUi ui;
    FakeUiBridge bridge;
    rocket::AppServices services {saves, preferences, host, controllers, textures, renderer, ui, bridge};
    rocket::GameRunner runner(services);

    assert(runner.initialize());
    assert(renderer.initialized);
    assert(!ui.html.empty());
    assert(bridge.hostContext.screen == ui.presentation.metadata.screen);
    assert(bridge.hostContext.titleScreenActive);
    assert(ui.html.find("data-panel-mode=\"title\"") != std::string::npos);
    assert(ui.html.find("data-rr-action=\"continue_game\"") == std::string::npos);
    assert(preferences.loadCount == 1);
    assert(controllers.preferenceUpdateCount == 1);
    assert(renderer.preferenceUpdateCount == 1);

    controllers.frame.connected = true;
    controllers.frame.family = rocket::ControllerFamily::Xbox;
    controllers.frame.pressed.set(static_cast<std::size_t>(rocket::ControllerButton::South));
    host.now += 1.0 / 60.0;
    runner.frame();
    assert(renderer.renderCount == 1);
    assert(ui.renderCount == 1);
    assert(renderer.presentCount == 1);
    assert(host.hapticCount == 1);
    assert(controllers.preferenceUpdateCount == 1);
    assert(renderer.preferenceUpdateCount == 1);
    assert(preferences.loadCount == 1);

    rocket::AppPreferences changedPreferences = preferences.value;
    changedPreferences.controller.invertFlightY = true;
    changedPreferences.cameraShakeDisabled = true;
    changedPreferences.helpDisabled = true;
    changedPreferences.gameSpeed = 1.5;
    assert(preferences.store(changedPreferences));
    host.now += 1.0 / 60.0;
    runner.frame();
    assert(preferences.loadCount == 2);
    assert(controllers.preferenceUpdateCount == 2);
    assert(renderer.preferenceUpdateCount == 2);
    assert(controllers.preferences.invertFlightY);
    assert(renderer.preferences.cameraShakeDisabled);
    assert(runner.app().controllerPreferences().invertFlightY);

    // A revision may advance after a redundant store, but unchanged values do
    // not need to be copied into frame consumers again.
    assert(preferences.store(changedPreferences));
    host.now += 1.0 / 60.0;
    runner.frame();
    assert(preferences.loadCount == 3);
    assert(controllers.preferenceUpdateCount == 2);
    assert(renderer.preferenceUpdateCount == 2);

    // Frame pacing is a renderer/platform preference only. Changing it must
    // reach the renderer without perturbing controller or gameplay state.
    rocket::AppPreferences frameLimitedPreferences = changedPreferences;
    frameLimitedPreferences.frameLimitMode = rocket::FrameLimitMode::Battery30;
    assert(preferences.store(frameLimitedPreferences));
    host.now += 1.0 / 60.0;
    runner.frame();
    assert(preferences.loadCount == 4);
    assert(controllers.preferenceUpdateCount == 2);
    assert(renderer.preferenceUpdateCount == 3);
    assert(renderer.preferences.frameLimitMode == rocket::FrameLimitMode::Battery30);

    runner.app().debugStartMining();
    host.now += 1.0 / 60.0;
    runner.frame();
    assert(!renderer.titleScreen);
    assert(ui.html.find("data-panel-mode=\"title\"") == std::string::npos);
    const int uiPanelUpdatesBeforeRealtimeFrame = ui.panelSetCount;
    const int bridgePanelUpdatesBeforeRealtimeFrame = bridge.panelSetCount;
    const int uiHudUpdatesBeforeRealtimeFrame = ui.hudSetCount;
    host.now += 0.20;
    runner.frame();
    assert(ui.panelSetCount == uiPanelUpdatesBeforeRealtimeFrame);
    assert(bridge.panelSetCount == bridgePanelUpdatesBeforeRealtimeFrame);
    assert(ui.hudSetCount == uiHudUpdatesBeforeRealtimeFrame + 1);
    assert(!ui.hud.patches.empty());

    const double animationTimeBeforeSuspend = renderer.animationTime;
    host.now += 10.0;
    runner.resetFrameClock();
    runner.frame();
    assert(renderer.animationTime == animationTimeBeforeSuspend);

    runner.app().debugStartCombatMining();
    host.now += 1.0 / 60.0;
    runner.frame();
    assert(renderer.miningViewsObserved);
    assert(renderer.miningViewsValid);
    assert(std::isfinite(renderer.miningViewChecksum));

    // XP thresholds open a persisted mandatory Level Up draft. The first
    // frame is fenced so a held/queued activation cannot choose a card.
    {
        AppFixture levelUpFixture;
        levelUpFixture.saves.value = levelUpExpeditionSave();
        assert(levelUpFixture.runner.initialize());
        levelUpFixture.ui.dispatchAction("continue_game");
        completeTitleLaunch(levelUpFixture);
        assert(levelUpFixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::SurfaceUpgrade));
        assert(levelUpFixture.ui.html.find("rr-level-up-draft") != std::string::npos);
        assert(levelUpFixture.ui.html.find("aria-label=\"Expedition experience\"") != std::string::npos);
        assert(levelUpFixture.ui.html.find("data-rr-action=\"next\"") == std::string::npos);
        assert(levelUpFixture.ui.html.find("data-rr-action=\"reroll_offers\"") == std::string::npos);
        assert(levelUpFixture.ui.html.find("surface_module_frame") == std::string::npos);

        const std::optional<rocket::SaveData> persisted = rocket::deserializeSaveData(levelUpFixture.saves.value);
        assert(persisted.has_value());
        assert(persisted->expedition.progression.runUpgradeOfferPending);
        assert(persisted->expedition.progression.pendingRunUpgradeChoices == 2);

        const auto advanceLevelUp = [&](double seconds) {
            const int frames = static_cast<int>(std::ceil(seconds * 60.0));
            for (int frame = 0; frame < frames; ++frame) {
                levelUpFixture.host.now += 1.0 / 60.0;
                levelUpFixture.runner.frame();
            }
        };

        levelUpFixture.ui.dispatchAction("surface_upgrade:0");
        assert(levelUpFixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::SurfaceUpgrade));
        assert(levelUpFixture.saves.storeCount == 1);
        advanceLevelUp(0.36);
        levelUpFixture.ui.dispatchAction("surface_upgrade:0");
        advanceLevelUp(0.11);
        assert(levelUpFixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::SurfaceUpgrade));
        assert(levelUpFixture.ui.html.find("1 PICKS REMAIN") != std::string::npos);
        advanceLevelUp(0.25);
        levelUpFixture.ui.dispatchAction("surface_upgrade:0");
        advanceLevelUp(0.11);
        assert(levelUpFixture.runner.app().currentScreen() != static_cast<int>(rocket::Screen::SurfaceUpgrade));
        levelUpFixture.runner.shutdown();
    }

    // Incompatible payloads remain untouched until the player explicitly
    // chooses New Campaign. They are never exposed as a Continue target.
    const auto requireFreshCampaignBoundary = [](std::string payload) {
        AppFixture fixture;
        fixture.saves.value = std::move(payload);
        const std::string original = fixture.saves.value;
        assert(fixture.runner.initialize());
        assert(fixture.saves.clearCount == 0);
        assert(fixture.saves.storeCount == 0);
        assert(fixture.saves.value == original);
        assert(fixture.ui.html.find("data-rr-action=\"continue_game\"") == std::string::npos);
        assert(fixture.ui.html.find("This save was created by an older campaign version") != std::string::npos);
        fixture.runner.shutdown();
    };
    std::string v22Save = levelUpExpeditionSave();
    const std::size_t versionOffset = v22Save.find("version=23");
    assert(versionOffset != std::string::npos);
    v22Save.replace(versionOffset, 10, "version=22");
    requireFreshCampaignBoundary(v22Save);
    requireFreshCampaignBoundary("RR_SAVE_V0\ncredits=1\n");
    std::string futureSave = levelUpExpeditionSave();
    const std::size_t futureVersionOffset = futureSave.find("version=23");
    assert(futureVersionOffset != std::string::npos);
    futureSave.replace(futureVersionOffset, 10, "version=24");
    requireFreshCampaignBoundary(futureSave);

    assert(host.viewportMetrics().logicalWidth == 1280);
    assert(host.viewportMetrics().drawableWidth == 2560);
    assert(!host.fullscreen());
    assert(host.setFullscreen(true));
    assert(host.fullscreen());

    runner.shutdown();
    assert(controllers.resetCalled);
    assert(renderer.shutdownCalled);
    assert(ui.shutdownCalled);
    return 0;
}
