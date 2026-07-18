#include "game/GameRunner.h"
#include "core/MiningSystem.h"
#include "core/ResearchSystem.h"
#include "core/SaveData.h"
#include "platform/AppServices.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>

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
    void log(rocket::PlatformLogLevel, std::string_view) override {}
    bool haptic(double, double, double) override { ++hapticCount; return true; }
    mutable double now = 1.0;
    rocket::ViewportMetrics metrics {1280, 800, 2560, 1600, 2.0F, -1.0F};
    bool fullscreenValue = false;
    int hapticCount = 0;
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
    double miningViewChecksum = 0.0;
    rocket::Screen screen = rocket::Screen::Hangar;
    bool titleScreen = false;
    bool miningViewsObserved = false;
    bool miningViewsValid = false;
};

class FakeUi final : public rocket::IGameUi {
public:
    bool initialize(ActionHandler handler) override { actionHandler = std::move(handler); return true; }
    void setPanelHtml(const std::string& value) override { html = value; ++panelSetCount; }
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
    bool navigate(rocket::UiDirection) override { return true; }
    bool activateFocused() override { return true; }
    bool cancel() override { return true; }
    bool scroll(float) override { return true; }
    bool modalOpen() const override { return false; }
    void setControllerPresentation(bool, rocket::ControllerFamily) override {}
    void setControllerFocusVisible(bool) override {}
    void setControllerResumeBlocked(bool, bool) override {}
    std::string focusedId() const override { return "action:primary"; }
    void openModal(const std::string&) override {}
    void closeModal() override {}
    void dismissHelp(const std::string&) override {}
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
    std::string html;
    bool shutdownCalled = false;
    int renderCount = 0;
    int panelSetCount = 0;
    int hudSetCount = 0;
    int performanceStatsSetCount = 0;
    bool performanceStatsVisible = false;
    rocket::RealtimeHudState hud;
    rocket::PerformanceStats lastPerformanceStats;
    std::function<void()> renderTimingHook;
    std::function<void()> performanceStatsTimingHook;
};

class FakeUiBridge final : public rocket::IUiBridge {
public:
    void setPanelHtml(std::string_view value) override { html = value; ++panelSetCount; }
    void setRealtimeHudState(const rocket::RealtimeHudState& value) override
    {
        hud = value;
        ++hudSetCount;
    }
    void setRmlUiEnabled(bool) override {}
    void setModalOpen(bool) override {}
    void setControllerPresentation(bool, rocket::ControllerFamily) override {}
    void setControllerFocusVisible(bool) override {}
    void setControllerResumeBlocked(bool, bool) override {}
    bool navigate(rocket::UiDirection) override { return false; }
    bool activate() override { return false; }
    bool cancel() override { return false; }
    bool scroll(double) override { return false; }
    bool modalOpen() const override { return false; }
    bool openModal(std::string_view) override { return false; }
    void closeModal() override {}
    std::string focusedId() const override { return {}; }
    void preferencesChanged(const rocket::AppPreferences&) override {}
    std::string html;
    int panelSetCount = 0;
    int hudSetCount = 0;
    rocket::RealtimeHudState hud;
};

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

} // namespace

int main()
{
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
        assert(fixture.ui.html == fixture.bridge.html);
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
        fixture.runner.resetFrameClock();
        fixture.runner.frame();
        assert(!fixture.renderer.titleScreen);
        assert(fixture.renderer.screen == rocket::Screen::Mining);
        assert(std::abs(fixture.renderer.shipDamage - 37.0) < 0.0001);
        assert(std::abs(fixture.renderer.miningHeat - 0.78) < 0.0001);
        assert(fixture.saves.storeCount == 0);
        assert(fixture.saves.value == originalSave);
        fixture.runner.shutdown();
    }

    // New Game must atomically replace any previous valid save with a fresh,
    // immediately resumable initial state.
    {
        AppFixture fixture;
        fixture.saves.value = activeMiningSave(0.65);
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("new_game");
        fixture.host.now += 1.0 / 60.0;
        fixture.runner.frame();
        assert(!fixture.renderer.titleScreen);
        assert(fixture.renderer.screen == rocket::Screen::Hangar);
        assert(fixture.saves.storeCount == 1);
        assert(fixture.saves.clearCount == 0);
        const std::optional<rocket::SaveData> fresh = rocket::deserializeSaveData(fixture.saves.value);
        assert(fresh.has_value());
        assert(fresh->screen == rocket::Screen::Hangar);
        assert(fresh->shipDamage == 0);
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
        fixture.host.now += 1.0 / 60.0;
        fixture.runner.frame();
        assert(fixture.renderer.titleScreen);
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::Mining));
        assert(fixture.ui.html.find("data-panel-mode=\"title\"") != std::string::npos);
        assert(fixture.saves.storeCount == 1);
        assert(fixture.saves.clearCount == 0);
        assert(fixture.saves.value == originalSave);
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
    assert(ui.html == bridge.html);
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
    const int bridgeHudUpdatesBeforeRealtimeFrame = bridge.hudSetCount;
    host.now += 0.20;
    runner.frame();
    assert(ui.panelSetCount == uiPanelUpdatesBeforeRealtimeFrame);
    assert(bridge.panelSetCount == bridgePanelUpdatesBeforeRealtimeFrame);
    assert(ui.hudSetCount == uiHudUpdatesBeforeRealtimeFrame + 1);
    assert(bridge.hudSetCount == bridgeHudUpdatesBeforeRealtimeFrame + 1);
    assert(!ui.hud.patches.empty());
    assert(!bridge.hud.patches.empty());

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
