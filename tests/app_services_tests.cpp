#include "game/GameRunner.h"
#include "game/GamePanel.h"
#include "game/GameRmlUi.h"
#include "game/IRmlRenderHost.h"
#include "core/ContentIds.h"
#include "core/GameState.h"
#include "core/GameUi.h"
#include "core/MiningSystem.h"
#include "core/ResearchSystem.h"
#include "core/SaveData.h"
#include "core/ScenarioSystem.h"
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
    std::string lastError() const override { return failStore || failClear ? "Injected save failure." : ""; }
    std::string value;
    int loadCount = 0;
    int storeCount = 0;
    int clearCount = 0;
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
        shipDamage = snapshot.shipDamage;
        miningHeat = snapshot.miningHeat;
        flybyInputY = snapshot.flybyInputY;
        launchCourseOffset = snapshot.launchCourseOffset;
        launchCourseVelocity = snapshot.launchCourseVelocity;
        launchManualControlsEnabled = snapshot.launchManualControlsEnabled;
        launchHeatEnabled = snapshot.launchHeatEnabled;
        launchAsteroidsEnabled = snapshot.launchAsteroidsEnabled;
        launchFrontierTransfer = snapshot.frontierTransfer;
        launchFuelCapacity = snapshot.launchFuelCapacity;
        launchAsteroidCount = snapshot.launchAsteroidCount;
        launchDestinationTier = snapshot.destinationTier;
        launchTravelProgress = snapshot.travelProgress;
        launchLunarImpactActive = snapshot.launchLunarImpactActive;
        launchLunarImpactElapsed = snapshot.launchLunarImpactElapsed;
        lastLaunchFailureCause = snapshot.lastLaunchFailureCause;
        surfacePushSteps = snapshot.surfacePushSteps;
        surfacePushMaterials = snapshot.surfacePushMaterials;
        surfacePushRewardMarkers = snapshot.surfacePushRewardMarkers;
        surfacePushRewardDepthOffsets = snapshot.surfacePushRewardDepthOffsets;
        surfacePushForecastMarkers = snapshot.surfacePushForecastMarkers;
        surfacePushForecastDepthOffsets = snapshot.surfacePushForecastDepthOffsets;
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
    double flybyInputY = 0.0;
    double launchCourseOffset = 0.0;
    double launchCourseVelocity = 0.0;
    double launchFuelCapacity = 0.0;
    double launchTravelProgress = 0.0;
    double launchLunarImpactElapsed = 0.0;
    double miningViewChecksum = 0.0;
    int launchAsteroidCount = 0;
    int launchDestinationTier = 0;
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
    bool launchManualControlsEnabled = false;
    bool launchHeatEnabled = false;
    bool launchAsteroidsEnabled = false;
    bool launchFrontierTransfer = false;
    bool launchLunarImpactActive = false;
    rocket::LaunchFailureCause lastLaunchFailureCause = rocket::LaunchFailureCause::None;
    bool miningViewsObserved = false;
    bool miningViewsValid = false;
    bool miningSwarmActive = false;
    bool miningSwarmAlert = false;
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
    for (int frame = 0; frame < 6; ++frame) {
        fixture.host.now += 0.25;
        fixture.runner.frame();
    }
    fixture.host.now += 1.0 / 120.0;
    fixture.runner.frame();
}

std::string activeMiningSave(double drillHeat)
{
    const rocket::ContentCatalog catalog = rocket::createDefaultContent();
    rocket::GameState state = rocket::createNewGame(catalog, 0xA11CEULL);
    state.run.destinationIndex = 2;
    rocket::startSurfaceExpedition(state, catalog);
    state.run.surfaceExpedition.miningSitePrepared = true;
    assert(rocket::startMiningRun(state, catalog).applied);
    state.run.mining.drillHeat = drillHeat;
    state.run.shipDamage = 37;
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
    assert(state.run.surfaceExpedition.pendingRunUpgradeChoices == 2);
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
    assert(rocket::performScenarioAction(
               state,
               catalog,
               rocket::content::scenario::lunarProspector,
               "delivery",
               rocket::ScenarioActionKind::ClaimReward)
               .applied);
    state.run.destinationIndex = 2;
    state.meta.furthestTier = 2;
    rocket::startSurfaceExpedition(state, catalog);
    rocket::ui::briefings::acknowledge(
        state.meta.acknowledgedActivityBriefingIds,
        rocket::ui::briefings::mining);
    state.run.surfaceExpedition.pendingRunUpgradeChoices = 0;
    state.run.surfaceExpedition.runUpgradeOfferPending = false;
    state.run.surfaceExpedition.runUpgradeOffers = {};
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
    state.run.surfaceExpedition.pendingRunUpgradeChoices = 0;
    state.run.surfaceExpedition.runUpgradeOfferPending = false;
    state.run.surfaceExpedition.runUpgradeOffers = {};
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
    state.run.surfaceExpedition.pendingRunUpgradeChoices = 0;
    state.run.surfaceExpedition.runUpgradeOfferPending = false;
    state.run.surfaceExpedition.runUpgradeOffers = {};
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
        rocket::tuning::flyby::jupiterSlingshotFuelSavings,
        rocket::tuning::flyby::slingshotSpeedBoost,
        rocket::tuning::flyby::jupiterSlingshotGoodInstabilityPenalty};
    state.screen = rocket::Screen::Hangar;
    rocket::syncLaunchConfig(state, catalog);
    return rocket::serializeSaveData(rocket::captureSaveData(state));
}

} // namespace

int main()
{
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
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
        assert(ui.initialize([](const std::string&) {}));
        assert(bridge.rmlUiEnabled);

        ui.setPanelPresentation(rocket::buildGamePanelPresentation(hangarContext));
        ui.requestFocus("action:prepare_launch");
        ui.refresh();
        assert(ui.focusedId() == "action:prepare_launch");
        ui.render();
        const rocket::UiDiagnostics initialDiagnostics = ui.diagnostics();
        assert(initialDiagnostics.documentRebuilds == 1);

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
        rocket::startArrivalOps(flyby, moonArrival);
        rocket::startArrivalFlybyRun(flyby, catalog);
        assert(flyby.screen == rocket::Screen::Flyby && !flyby.run.flyby.completed);
        rocket::Random flybyRng(0xF17B7ULL);
        const rocket::PreparedLaunch flybyLaunch =
            rocket::prepareLaunch(flyby, catalog, flybyRng);
        rocket::PanelRenderContext flybyContext {
            flyby,
            catalog,
            flybyLaunch,
            flybyLaunch};
        flybyContext.firstTimeIntroductionsEnabled = false;
        const rocket::PanelDocumentPresentation flybyPresentation =
            rocket::buildGamePanelPresentation(flybyContext);
        assert(flybyPresentation.templateKind == rocket::PanelTemplateKind::ControlPanel);

        ui.setPanelPresentation(flybyPresentation);
        ui.requestFocus("modal:flight_details");
        ui.refresh();
        assert(ui.focusedId() == "modal:flight_details");
        ui.render();
        const rocket::UiDiagnostics transitionDiagnostics = ui.diagnostics();
        assert(transitionDiagnostics.documentRebuilds == 0);
        assert(transitionDiagnostics.panelRebuilds > 0);

        assert(ui.activateFocused());
        assert(ui.modalOpen());
        const std::string activeModalFocus = ui.focusedId();
        assert(activeModalFocus.empty());
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
        assert(ui.focusedId() == "modal:flight_details");

        const auto applyRealtimePresentation = [&](const rocket::PanelRenderContext& context) {
            const std::size_t logStart = host.logMessages.size();
            ui.setPanelPresentation(rocket::buildGamePanelPresentation(context));
            rocket::RealtimeHudState realtime;
            rocket::buildRealtimeHudState(context, realtime);
            assert(!realtime.patches.empty());
            ui.setRealtimeHudState(realtime);
            ui.render();
            assert(std::none_of(
                host.logMessages.begin() + static_cast<std::ptrdiff_t>(logStart),
                host.logMessages.end(),
                [](const std::string& message) {
                    return message.find("Realtime RmlUi patch target is missing") != std::string::npos;
                }));
        };

        rocket::GameState launch = rocket::createNewGame(catalog, 0x1A0C4ULL);
        launch.screen = rocket::Screen::Launch;
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
        mining.run.surfaceExpedition.miningSitePrepared = true;
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
        ioMining.run.surfaceExpedition.miningSitePrepared = true;
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
        assert(rocket::startSurfaceScanRun(scan, scanRng).applied);
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

        rocket::GameState results = rocket::createNewGame(catalog, 0xDEB21EFULL);
        results.screen = rocket::Screen::Results;
        results.lastOutcome.type = rocket::LaunchResultType::SafeEject;
        results.lastOutcome.recoveryMethod = rocket::RecoveryMethod::ReturnHome;
        results.lastOutcome.ejectMultiplier = 1.1;
        results.lastOutcome.crashMultiplier = 1.5;
        rocket::Random resultsRng(0xDEB21EFULL);
        const rocket::PreparedLaunch resultsLaunch =
            rocket::prepareLaunch(results, catalog, resultsRng);
        const rocket::PanelDocumentPresentation resultsPresentation =
            rocket::buildGamePanelPresentation({
                results,
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
        assert(rocket::performScenarioAction(
                   state,
                   catalog,
                   rocket::content::scenario::lunarProspector,
                   "delivery",
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
        state.run.surfaceExpedition.miningSitePrepared = true;
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
        ui.requestFocus("action:survey_surface");
        ui.refresh();

        constexpr std::array<std::string_view, 4> focusPath {
            "action:survey_surface",
            "action:push_surface",
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
        constexpr std::array<std::string_view, 4> surfaceActions {
            rocket::ui::actions::mineSurface,
            rocket::ui::actions::surveySurface,
            rocket::ui::actions::pushSurface,
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
                if (pointerAction == rocket::ui::actions::pushSurface) {
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
                      << " survey=" << pointerReachable[1]
                      << " push=" << pointerReachable[2]
                      << " extract=" << pointerReachable[3] << '\n';
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
            rocket::ui::briefings::surfaceDigIntroduction);
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
        assert(pointerAction == rocket::ui::actions::pushSurface);
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
        assert(arrivalFocus == "action:arrival_flyby"
            || arrivalFocus == "action:arrival_orbit"
            || arrivalFocus == "action:arrival_landing");
        ui.shutdown();
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
        assert(ui.focusedId() == "modal:hangar_details");
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
        if (rocket::Astronaut* pilot = rocket::activeAstronaut(state)) {
            pilot->stress = 0;
        }
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
        fixture.host.now += 0.20;
        fixture.runner.frame();
        assert(fixture.renderer.titleScreen);
        assert(fixture.renderer.screen == rocket::Screen::Hangar);
        assert(fixture.saves.storeCount == 0);
        assert(fixture.saves.value == originalSave);

        fixture.ui.dispatchAction("continue_game");
        completeTitleLaunch(fixture);
        assert(!fixture.renderer.titleScreen);
        assert(fixture.renderer.screen == rocket::Screen::Mining);
        assert(std::abs(fixture.renderer.shipDamage - 37.0) < 0.0001);
        assert(std::abs(fixture.renderer.miningHeat - 0.78) < 0.0001);
        assert(fixture.saves.storeCount == 0);
        assert(fixture.saves.value == originalSave);
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
            assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Launch));
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
        fixture.runner.app().debugStartLaunchLesson(4);
        assert(fixture.runner.app().deterministicStateHash() == stateBeforeInvalidLesson);
        assert(fixture.saves.value == originalSave);
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
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Launch));
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
    // short resolve beat before returning to Surface Ops.
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
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::SurfaceExpedition));
        fixture.runner.shutdown();
    }

    // Surface minigame Confirm remains useful after D-pad navigation even if
    // the UI bridge temporarily has no valid focused control.
    for (const bool scanScreen : {true, false}) {
        AppFixture fixture;
        assert(fixture.runner.initialize());
        if (scanScreen) {
            fixture.runner.app().debugStartSurfaceScan();
        } else {
            fixture.runner.app().debugStartSurfacePush();
        }
        fixture.host.now += 1.0 / 60.0;
        fixture.runner.frame();
        const std::string initialPanel = fixture.ui.html;

        fixture.ui.activateFocusedResult = false;
        fixture.controllers.frame.connected = true;
        fixture.controllers.frame.family = rocket::ControllerFamily::Xbox;
        fixture.controllers.frame.meaningfulInput = true;
        fixture.controllers.frame.navigation = rocket::UiDirection::Right;
        fixture.controllers.frame.pressed.set(static_cast<std::size_t>(rocket::ControllerButton::DpadRight));
        fixture.host.now += 1.0 / 60.0;
        fixture.runner.frame();

        fixture.controllers.frame.navigation.reset();
        fixture.controllers.frame.pressed.set(static_cast<std::size_t>(rocket::ControllerButton::South));
        fixture.host.now += 1.0 / 60.0;
        fixture.runner.frame();
        fixture.host.now += 1.0 / 60.0;
        fixture.runner.frame();

        assert(fixture.ui.activateFocusedCount == 1);
        assert(fixture.ui.html != initialPanel);
        fixture.runner.shutdown();
    }

    // Push Deeper's player-facing renderer must receive both the banked scan
    // forecast and the confirmed markers produced by the real application
    // state. Scene-only fixtures cannot catch snapshot handoff regressions.
    {
        AppFixture fixture;
        assert(fixture.runner.initialize());
        fixture.runner.app().debugStartSurfacePush();
        fixture.host.now += 1.0 / 60.0;
        fixture.runner.frame();
        assert(fixture.renderer.screen == rocket::Screen::SurfacePush);
        assert(std::find(
            fixture.renderer.surfacePushForecastMarkers.begin(),
            fixture.renderer.surfacePushForecastMarkers.end(),
            rocket::MiningCellMaterial::ArtifactCache) !=
            fixture.renderer.surfacePushForecastMarkers.end());

        fixture.ui.dispatchAction("surface_push_step");
        fixture.host.now += 1.0 / 60.0;
        fixture.runner.frame();
        assert(fixture.renderer.surfacePushSteps == 1);
        assert(fixture.renderer.surfacePushMaterials.common >= 1);
        assert(fixture.renderer.surfacePushMaterials.rare >= 1);
        assert(fixture.renderer.surfacePushRewardMarkers.size() >= 3);
        assert(fixture.renderer.surfacePushRewardDepthOffsets.size() ==
            fixture.renderer.surfacePushRewardMarkers.size());
        assert(std::all_of(
            fixture.renderer.surfacePushRewardDepthOffsets.begin(),
            fixture.renderer.surfacePushRewardDepthOffsets.end(),
            [](int depth) { return depth == 1; }));
        fixture.runner.shutdown();
    }

    // New Game must atomically replace any previous valid save with a fresh,
    // immediately resumable initial state.
    {
        AppFixture fixture;
        fixture.saves.value = activeMiningSave(0.65);
        fixture.preferences.value.debugToolsEnabled = true;
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("new_game");
        completeTitleLaunch(fixture);
        assert(!fixture.renderer.titleScreen);
        assert(fixture.renderer.screen == rocket::Screen::StoryBriefing);
        assert(!fixture.preferences.value.debugToolsEnabled);
        assert(fixture.bridge.preferenceUpdateCount == 1);
        assert(!fixture.bridge.lastPreferences.debugToolsEnabled);
        assert(fixture.saves.storeCount == 1);
        assert(fixture.saves.clearCount == 0);
        const std::optional<rocket::SaveData> fresh = rocket::deserializeSaveData(fixture.saves.value);
        assert(fresh.has_value());
        assert(fresh->screen == rocket::Screen::StoryBriefing);
        assert(fresh->storyBriefing.pending == rocket::StoryBriefingId::CampaignIntroduction);
        assert(fresh->shipDamage == 0);
        fixture.runner.shutdown();
    }

    // Starting over also returns the application to its player-facing mode:
    // debug tooling must not survive a reset into a fresh campaign.
    {
        AppFixture fixture;
        fixture.preferences.value.debugToolsEnabled = true;
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("reset_save");
        assert(!fixture.preferences.value.debugToolsEnabled);
        assert(fixture.bridge.preferenceUpdateCount == 1);
        assert(!fixture.bridge.lastPreferences.debugToolsEnabled);
        fixture.runner.shutdown();
    }

    // The first-flight modal's CTA must perform the original action and save
    // its acknowledgment before entering the non-restorable launch session.
    {
        AppFixture fixture;
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("new_game");
        completeTitleLaunch(fixture);
        fixture.ui.dispatchAction("acknowledge_story_briefing");
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Hangar));
        assert(fixture.ui.html.find("data-ui-modal=\"launch_introduction\"") != std::string::npos);

        const std::optional<rocket::SaveData> beforeLaunch = rocket::deserializeSaveData(fixture.saves.value);
        assert(beforeLaunch.has_value());
        assert(!rocket::ui::briefings::acknowledged(beforeLaunch->acknowledgedActivityBriefingIds, rocket::ui::briefings::launch));

        fixture.ui.dispatchAction("prepare_launch");
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Launch));
        const std::optional<rocket::SaveData> afterLaunch = rocket::deserializeSaveData(fixture.saves.value);
        assert(afterLaunch.has_value());
        assert(afterLaunch->screen == rocket::Screen::Hangar);
        assert(rocket::ui::briefings::acknowledged(afterLaunch->acknowledgedActivityBriefingIds, rocket::ui::briefings::launch));
        fixture.runner.shutdown();
    }

    // Surface Ops teaches its shared sequence once, even if optional activity
    // introductions are disabled. Disabled Dig and Mine actions must remain
    // inert to direct UI/controller dispatch until their banked prerequisites
    // are complete.
    {
        AppFixture fixture;
        fixture.saves.value = firstSurfaceTutorialSave();
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("continue_game");
        completeTitleLaunch(fixture);
        fixture.runner.app().setFirstTimeIntroductionsEnabled(false);
        fixture.runner.frame();
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::SurfaceExpedition));

        fixture.ui.dispatchAction("push_surface");
        fixture.ui.dispatchAction("mine_surface");
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::SurfaceExpedition));

        fixture.ui.dispatchAction("survey_surface");
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::SurfaceScan));
        std::optional<rocket::SaveData> saved = rocket::deserializeSaveData(fixture.saves.value);
        assert(saved.has_value());
        assert(rocket::ui::briefings::acknowledged(
            saved->acknowledgedActivityBriefingIds,
            rocket::ui::briefings::surfaceSurveyIntroduction));

        fixture.ui.dispatchAction("surface_scan_pulse");
        fixture.ui.dispatchAction("surface_scan_bank");
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::SurfaceExpedition));
        fixture.runner.frame();
        saved = rocket::deserializeSaveData(fixture.saves.value);
        assert(saved.has_value());
        assert(rocket::ui::briefings::acknowledged(
            saved->acknowledgedActivityBriefingIds,
            rocket::ui::briefings::surfaceSurveyComplete));

        fixture.ui.dispatchAction("push_surface");
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::SurfacePush));
        saved = rocket::deserializeSaveData(fixture.saves.value);
        assert(saved.has_value());
        assert(rocket::ui::briefings::acknowledged(
            saved->acknowledgedActivityBriefingIds,
            rocket::ui::briefings::surfaceDigIntroduction));

        fixture.ui.dispatchAction("surface_push_step");
        fixture.ui.dispatchAction("surface_push_bank");
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::SurfaceExpedition));
        fixture.runner.frame();
        saved = rocket::deserializeSaveData(fixture.saves.value);
        assert(saved.has_value());
        assert(rocket::ui::briefings::acknowledged(
            saved->acknowledgedActivityBriefingIds,
            rocket::ui::briefings::surfaceDigComplete));

        fixture.runner.shutdown();
    }

    // Mandatory campaign mining cannot start until its explicit story
    // briefing is accepted. The optional Help preference is not involved.
    {
        const auto savedScenario = [](const rocket::SaveData& save, std::string_view id) {
            const auto found = std::find_if(
                save.scenarios.begin(),
                save.scenarios.end(),
                [id](const rocket::ScenarioInstance& instance) { return instance.id == id; });
            return found == save.scenarios.end() ? nullptr : &*found;
        };
        AppFixture fixture;
        fixture.saves.value = freshSurfaceExpeditionSave();
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("continue_game");
        completeTitleLaunch(fixture);
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::SurfaceExpedition));
        assert(fixture.ui.html.find("data-modal=\"scenario_mars_bay_expansion_briefing\" data-auto-modal=\"1\"") != std::string::npos);
        assert(fixture.ui.html.find("data-scenario-id=\"mars_bay_expansion\" data-scenario-step-id=\"briefing\"") != std::string::npos);
        assert(fixture.ui.html.find("data-ui-modal=\"mining_introduction\"") == std::string::npos);

        const std::optional<rocket::SaveData> beforeMining = rocket::deserializeSaveData(fixture.saves.value);
        assert(beforeMining.has_value());
        const rocket::ScenarioInstance* beforeMars = savedScenario(
            *beforeMining,
            rocket::content::scenario::marsBayExpansion);
        const rocket::ScenarioStepProgress* beforeBriefing = beforeMars == nullptr
            ? nullptr
            : rocket::findScenarioStepProgress(*beforeMars, "briefing");
        assert(beforeBriefing != nullptr && !beforeBriefing->briefingAcknowledged);

        fixture.ui.dispatchAction("mine_surface");
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::SurfaceExpedition));

        fixture.ui.dispatchAction(
            std::string(rocket::ui::actions::scenarioActionPrefix) +
            rocket::content::scenario::marsBayExpansion + "|briefing|" +
            std::to_string(static_cast<int>(rocket::ScenarioActionKind::AcknowledgeBriefing)));
        const std::optional<rocket::SaveData> acceptedMining = rocket::deserializeSaveData(fixture.saves.value);
        assert(acceptedMining.has_value());
        const rocket::ScenarioInstance* acceptedMars = savedScenario(
            *acceptedMining,
            rocket::content::scenario::marsBayExpansion);
        const rocket::ScenarioStepProgress* acceptedBriefing = acceptedMars == nullptr
            ? nullptr
            : rocket::findScenarioStepProgress(*acceptedMars, "briefing");
        assert(acceptedBriefing != nullptr && acceptedBriefing->briefingAcknowledged);

        fixture.ui.dispatchAction("mine_surface");
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Mining));
        const std::optional<rocket::SaveData> afterMining = rocket::deserializeSaveData(fixture.saves.value);
        assert(afterMining.has_value());
        assert(rocket::ui::briefings::acknowledged(afterMining->acknowledgedActivityBriefingIds, rocket::ui::briefings::mining));
        fixture.runner.shutdown();
    }

    // Drone Ops is a Surface Ops sub-screen: loadout edits persist while it is
    // open, and both its explicit Done action and controller Cancel return to
    // the still-active Surface Expedition.
    {
        AppFixture fixture;
        fixture.saves.value = activeDroneBaySurfaceExpeditionSave();
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("continue_game");
        completeTitleLaunch(fixture);
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::SurfaceExpedition));

        fixture.ui.dispatchAction("drone_ops");
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::DroneOps));
        std::optional<rocket::SaveData> saved = rocket::deserializeSaveData(fixture.saves.value);
        assert(saved.has_value());
        assert(saved->screen == rocket::Screen::DroneOps);
        assert(saved->surfaceExpedition.active);

        fixture.ui.dispatchAction("equip_drone:0");
        saved = rocket::deserializeSaveData(fixture.saves.value);
        assert(saved.has_value());
        assert(std::find(
            saved->equippedDroneIds.begin(),
            saved->equippedDroneIds.end(),
            rocket::content::drone::miningDrone) != saved->equippedDroneIds.end());

        fixture.ui.dispatchAction("back_to_surface_ops");
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::SurfaceExpedition));
        saved = rocket::deserializeSaveData(fixture.saves.value);
        assert(saved.has_value());
        assert(saved->screen == rocket::Screen::SurfaceExpedition);
        assert(saved->surfaceExpedition.active);
        assert(std::find(
            saved->equippedDroneIds.begin(),
            saved->equippedDroneIds.end(),
            rocket::content::drone::miningDrone) != saved->equippedDroneIds.end());

        fixture.ui.dispatchAction("drone_ops");
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::DroneOps));
        fixture.controllers.frame.connected = true;
        fixture.controllers.frame.family = rocket::ControllerFamily::Xbox;
        fixture.controllers.frame.meaningfulInput = true;
        fixture.controllers.frame.pressed.set(static_cast<std::size_t>(rocket::ControllerButton::East));
        fixture.host.now += 1.0 / 60.0;
        fixture.runner.frame();
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::SurfaceExpedition));
        saved = rocket::deserializeSaveData(fixture.saves.value);
        assert(saved.has_value());
        assert(saved->screen == rocket::Screen::SurfaceExpedition);
        assert(saved->surfaceExpedition.active);
        assert(std::find(
            saved->equippedDroneIds.begin(),
            saved->equippedDroneIds.end(),
            rocket::content::drone::miningDrone) != saved->equippedDroneIds.end());
        fixture.runner.shutdown();
    }

    // Completing Prospector onboarding hands the existing earned refit to the
    // single Fuel Tanks II lesson instead of returning to a dead-end Hangar.
    {
        AppFixture fixture;
        fixture.saves.value = readyProspectorClaimSave();
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("continue_game");
        completeTitleLaunch(fixture);
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Upgrade));

        fixture.ui.dispatchAction(
            std::string(rocket::ui::actions::scenarioActionPrefix) +
            rocket::content::scenario::lunarProspector + "|delivery|" +
            std::to_string(static_cast<int>(rocket::ScenarioActionKind::ClaimReward)));
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::DroneOps));

        fixture.ui.dispatchAction("back_to_surface_ops");
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Upgrade));
        const std::optional<rocket::SaveData> saved = rocket::deserializeSaveData(fixture.saves.value);
        assert(saved.has_value());
        assert(saved->refitEntitled);
        assert(!saved->offerModuleIds.empty());
        assert(saved->offerModuleIds.front() == rocket::content::module::fuelTanks2);
        assert(saved->offerModuleIds.size() == 1);
        fixture.runner.shutdown();
    }

    // Claiming the Mars contract opens the saved Jupiter options beat. The
    // player may review either independent contributor before opening Refit.
    {
        AppFixture fixture;
        fixture.saves.value = readyMarsExpansionClaimSave();
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("continue_game");
        completeTitleLaunch(fixture);
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Upgrade));

        fixture.ui.dispatchAction(
            std::string(rocket::ui::actions::scenarioActionPrefix) +
            rocket::content::scenario::marsBayExpansion + "|delivery|" +
            std::to_string(static_cast<int>(rocket::ScenarioActionKind::ClaimReward)));
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Hangar));
        fixture.host.now += 1.0 / 120.0;
        fixture.runner.frame();
        const std::optional<rocket::SaveData> saved = rocket::deserializeSaveData(fixture.saves.value);
        assert(saved.has_value());
        for (int pendingChoice = 0;
             pendingChoice < 4 && fixture.ui.html.find("surface_upgrade:0") != std::string::npos;
             ++pendingChoice) {
            fixture.ui.dispatchAction("surface_upgrade:0");
            fixture.host.now += 1.0 / 120.0;
            fixture.runner.frame();
        }
        assert(std::find(
            saved->unlockKeys.begin(),
            saved->unlockKeys.end(),
            rocket::content::unlock::routeJupiter) != saved->unlockKeys.end());
        const auto marsScenario = std::find_if(
            saved->scenarios.begin(),
            saved->scenarios.end(),
            [](const rocket::ScenarioInstance& instance) {
                return instance.id == rocket::content::scenario::marsBayExpansion;
            });
        const rocket::ScenarioStepProgress* funding = marsScenario == saved->scenarios.end()
            ? nullptr
            : rocket::findScenarioStepProgress(*marsScenario, "funding");
        assert(funding != nullptr && !funding->briefingAcknowledged);

        fixture.ui.dispatchAction(std::string(rocket::ui::actions::openJupiterRefit));
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Upgrade));
        const std::optional<rocket::SaveData> acknowledged = rocket::deserializeSaveData(fixture.saves.value);
        assert(acknowledged.has_value());
        const auto acknowledgedMars = std::find_if(
            acknowledged->scenarios.begin(),
            acknowledged->scenarios.end(),
            [](const rocket::ScenarioInstance& instance) {
                return instance.id == rocket::content::scenario::marsBayExpansion;
            });
        const rocket::ScenarioStepProgress* acknowledgedFunding =
            acknowledgedMars == acknowledged->scenarios.end()
            ? nullptr
            : rocket::findScenarioStepProgress(*acknowledgedMars, "funding");
        assert(acknowledgedFunding != nullptr && acknowledgedFunding->briefingAcknowledged);
        fixture.runner.shutdown();
    }

    // Beginning the Jupiter segment spends borrowed Mars momentum immediately
    // and persists that consumption, while permanent tank ranks are untouched.
    {
        AppFixture fixture;
        fixture.saves.value = activeJupiterSlingshotSave();
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("continue_game");
        completeTitleLaunch(fixture);
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Hangar));
        fixture.host.now += 1.0 / 120.0;
        fixture.runner.frame();
        assert(fixture.ui.html.find("SLINGSHOT ACTIVE // WILD RIDE") != std::string::npos);
        assert(fixture.ui.html.find("+35% flight instability") != std::string::npos);

        fixture.ui.dispatchAction(std::string(rocket::ui::actions::continueJupiterSlingshot));
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Launch));
        const std::optional<rocket::SaveData> spent = rocket::deserializeSaveData(fixture.saves.value);
        assert(spent.has_value());
        assert(!spent->jupiterSlingshotActive);
        assert(!spent->pendingTransferAssist.active());
        assert(spent->nextLaunchFuelBoost == 0.0);
        assert(spent->nextLaunchSpeedBoost == 0.0);
        assert(spent->nextLaunchInstabilityPenalty == 0.0);
        assert(spent->launchUpgrades.fuelTanks == 2);
        fixture.runner.shutdown();
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
        fixture.runner.app().debugStartFlyby();
        fixture.host.now += 1.0 / 60.0;
        fixture.runner.frame();
        assert(fixture.runner.app().inputContext() == rocket::InputContext::FlybyActive);
        assert(std::abs(fixture.renderer.flybyInputY) < 0.000001);

        fixture.ui.openModal("settings");
        fixture.controllers.frame.connected = true;
        fixture.controllers.frame.family = rocket::ControllerFamily::Xbox;
        fixture.controllers.frame.meaningfulInput = true;
        fixture.controllers.frame.leftY = 0.85;
        fixture.controllers.frame.pressed.set(static_cast<std::size_t>(rocket::ControllerButton::East));
        const std::uint64_t stateBeforeModalInput = fixture.runner.app().deterministicStateHash();
        fixture.host.now += 1.0 / 60.0;
        fixture.runner.frame();
        assert(fixture.runner.app().inputContext() == rocket::InputContext::Paused);
        assert(fixture.ui.activateFocusedCount == 1);
        assert(fixture.ui.cancelCount == 0);
        assert(fixture.ui.modalOpenValue);
        assert(std::abs(fixture.renderer.flybyInputY) < 0.000001);
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
        assert(fixture.runner.app().inputContext() == rocket::InputContext::FlybyActive);
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

    // Returning to Earth resolves into a non-dismissible launch outcome modal.
    // D-pad navigation may remain active inside that modal, but it must not
    // advance the result-scene animation underneath the focused Continue action.
    {
        AppFixture fixture;
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("new_game");
        completeTitleLaunch(fixture);
        fixture.ui.dispatchAction("acknowledge_story_briefing");
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Hangar));

        fixture.runner.app().prepareForLaunch();
        fixture.runner.app().startLaunch();
        for (int frame = 0;
             frame < 600 && fixture.runner.app().inputContext() != rocket::InputContext::Launch;
             ++frame) {
            fixture.host.now += 1.0 / 60.0;
            fixture.runner.frame();
        }
        assert(fixture.runner.app().inputContext() == rocket::InputContext::Launch);

        fixture.runner.app().returnHome();
        for (int frame = 0;
             frame < 600 && fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Launch);
             ++frame) {
            fixture.host.now += 1.0 / 60.0;
            fixture.runner.frame();
        }
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
        assert(launchOutcomeModal->tone == rocket::ModalTone::Negative);

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
             frame < 1500 && !fixture.renderer.launchLunarImpactActive;
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
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Launch));
        assert(fixture.renderer.launchLunarImpactActive);
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
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Launch));
        assert(fixture.renderer.launchLunarImpactActive);
        assert(fixture.renderer.launchLunarImpactElapsed > rocket::tuning::session::lunarImpactHoldSeconds);
        assert(std::abs(fixture.renderer.launchTravelProgress - collisionProgress) < 0.000001);
        assert(std::abs(fixture.renderer.launchCourseOffset - collisionCourse) < 0.000001);

        for (int frame = 0;
             frame < 120 &&
             fixture.renderer.launchLunarImpactElapsed <
                 rocket::tuning::session::lunarImpactSequenceSeconds - 2.0 / 60.0;
             ++frame) {
            fixture.host.now += 1.0 / 60.0;
            fixture.runner.frame();
        }
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Launch));
        assert(fixture.renderer.launchLunarImpactActive);
        for (int frame = 0;
             frame < 5 && fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Launch);
             ++frame) {
            fixture.host.now += 1.0 / 60.0;
            fixture.runner.frame();
        }
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Results));
        assert(!fixture.renderer.launchLunarImpactActive);
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

    runner.app().debugStartFlyby();
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
        assert(persisted->surfaceExpedition.runUpgradeOfferPending);
        assert(persisted->surfaceExpedition.pendingRunUpgradeChoices == 2);

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

    // An older v12 payload is rejected at the title boundary with Continue
    // unavailable and the exact fresh-start notice.
    {
        AppFixture oldSaveFixture;
        oldSaveFixture.saves.value = levelUpExpeditionSave();
        const std::size_t versionOffset = oldSaveFixture.saves.value.find("version=13");
        assert(versionOffset != std::string::npos);
        oldSaveFixture.saves.value.replace(versionOffset, 10, "version=12");
        assert(oldSaveFixture.runner.initialize());
        assert(oldSaveFixture.ui.html.find("data-rr-action=\"continue_game\"") == std::string::npos);
        assert(oldSaveFixture.ui.html.find("Progression update requires a new game.") != std::string::npos);
        oldSaveFixture.runner.shutdown();
    }

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
