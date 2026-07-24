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
#include "platform/AppServices.h"

#include "../.deps/RmlUi/Include/RmlUi/Core/RenderInterface.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>

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
    void log(rocket::PlatformLogLevel, std::string_view) override {}
    bool haptic(double, double, double) override { ++hapticCount; return true; }
    mutable double now = 1.0;
    rocket::ViewportMetrics metrics {1280, 800, 2560, 1600, 2.0F};
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
        flybyInputY = snapshot.flybyInputY;
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
    void preferencesChanged(const rocket::AppPreferences& value) override
    {
        lastPreferences = value;
        ++preferenceUpdateCount;
    }
    std::string html;
    int panelSetCount = 0;
    int hudSetCount = 0;
    int preferenceUpdateCount = 0;
    rocket::AppPreferences lastPreferences;
    rocket::RealtimeHudState hud;
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

std::string freshSurfaceExpeditionSave()
{
    const rocket::ContentCatalog catalog = rocket::createDefaultContent();
    rocket::GameState state = rocket::createNewGame(catalog, 0x51A7EULL);
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
    rocket::ensureDroneBayState(state, catalog);
    rocket::ui::briefings::acknowledge(
        state.meta.acknowledgedActivityBriefingIds,
        rocket::ui::briefings::miniDrones);
    state.screen = rocket::Screen::SurfaceExpedition;
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
    // Surface Ops presents its immediate operations as one visible horizontal
    // action row. Left/right follows Mine -> Survey -> Push -> Extract, while
    // Up returns to the Drone Ops callout above the row.
    {
        const rocket::ContentCatalog catalog = rocket::createDefaultContent();
        rocket::GameState state = rocket::createNewGame(catalog, 0x5A7FACEULL);
        state.run.destinationIndex = 2;
        rocket::startSurfaceExpedition(state, catalog);
        state.meta.unlockKeys.push_back(rocket::content::unlock::droneBay);
        state.meta.droneBaySlots = 2;
        rocket::ensureDroneBayState(state, catalog);
        rocket::ui::briefings::acknowledge(
            state.meta.acknowledgedActivityBriefingIds,
            rocket::ui::briefings::miniDrones);
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
        const std::string surfaceHtml = rocket::buildGamePanelHtml(panelContext);
        assert(surfaceHtml.find("data-ui-focus-id=\"action:drone_ops\"") != std::string::npos);
        ui.setPanelHtml(surfaceHtml);
        ui.setControllerPresentation(true, rocket::ControllerFamily::Xbox);
        ui.requestFocus("action:mine_surface");
        ui.refresh();

        constexpr std::array<std::string_view, 4> focusPath {
            "action:mine_surface",
            "action:survey_surface",
            "action:push_surface",
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
        for (int x = 340; x <= 1520; x += 10) {
            for (int y = 340; y <= 610; y += 10) {
                pointerAction.clear();
                ui.mouseDown(x, y, 0);
                ui.mouseUp(x, y, 0);
                for (std::size_t index = 0; index < surfaceActions.size(); ++index) {
                    pointerReachable[index] = pointerReachable[index]
                        || pointerAction == surfaceActions[index];
                }
            }
        }
        assert(std::all_of(pointerReachable.begin(), pointerReachable.end(), [](bool reachable) {
            return reachable;
        }));
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
        state.meta.droneBaySlots = 2;
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
        host.metrics = {1010, 1000, 1010, 1000, 1.0F};
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
        ui.setPanelHtml(rocket::buildGamePanelHtml(panelContext));

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
        assert(ui.focusedId().starts_with("modal:drone_details_"));
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
        ui.setPanelHtml(rocket::buildGamePanelHtml(loadedPanelContext));
        ui.requestFocus("action:equip_drone:0");
        ui.refresh();
        assert(ui.navigate(rocket::UiDirection::Right));
        assert(ui.focusedId().starts_with("modal:drone_details_"));
        while (ui.navigate(rocket::UiDirection::Right)) {
            if (ui.focusedId() == "action:unequip_drone_slot:0") {
                break;
            }
        }
        assert(ui.focusedId() == "action:unequip_drone_slot:0");
        assert(ui.navigate(rocket::UiDirection::Left));
        assert(ui.focusedId().starts_with("modal:drone_details_"));

        const auto click = [&ui](int x, int y) {
            ui.mouseDown(x, y, 0);
            ui.mouseUp(x, y, 0);
        };

        click(1002, 35);
        assert(!ui.modalOpen());
        click(950, 35);
        assert(ui.modalOpen());
        ui.closeModal();

        pointerAction.clear();
        click(1002, 112);
        assert(pointerAction.empty());
        click(900, 112);
        assert(pointerAction == rocket::ui::actions::backToSurfaceOps);
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

    // Field Upgrade cards commit directly; no preview-selection state or
    // duplicate selected-offer section is required.
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
        fixture.host.now += 1.0 / 60.0;
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

    // New Game must atomically replace any previous valid save with a fresh,
    // immediately resumable initial state.
    {
        AppFixture fixture;
        fixture.saves.value = activeMiningSave(0.65);
        fixture.preferences.value.debugToolsEnabled = true;
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("new_game");
        fixture.host.now += 1.0 / 60.0;
        fixture.runner.frame();
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

    // The mining overview CTA must acknowledge the introduction only when it
    // successfully starts the first mining run.
    {
        AppFixture fixture;
        fixture.saves.value = freshSurfaceExpeditionSave();
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("continue_game");
        assert(fixture.runner.app().currentScreen() == static_cast<int>(rocket::Screen::SurfaceExpedition));
        assert(fixture.ui.html.find("data-ui-modal=\"mining_introduction\"") != std::string::npos);

        const std::optional<rocket::SaveData> beforeMining = rocket::deserializeSaveData(fixture.saves.value);
        assert(beforeMining.has_value());
        assert(!rocket::ui::briefings::acknowledged(beforeMining->acknowledgedActivityBriefingIds, rocket::ui::briefings::mining));

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

    // Returning to Earth resolves into a non-dismissible launch outcome modal.
    // D-pad navigation may remain active inside that modal, but it must not
    // advance the result-scene animation underneath the focused Continue action.
    {
        AppFixture fixture;
        assert(fixture.runner.initialize());
        fixture.ui.dispatchAction("new_game");
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
    assert(ui.html.find("Show introductions") != std::string::npos);

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
