#include "performance/BenchmarkScenarioDriver.h"

#include "game/RocketGameApp.h"
#include "game/GameRunner.h"
#include "platform/AppServices.h"
#include "render/SceneComposer.h"

#include <bit>
#include <cassert>
#include <cstdio>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace {

constexpr std::uint64_t kExpectedLongRunMiningHash = 0x432b379d478a4c39ULL;

class FakeSaveStore final : public rocket::ISaveStore {
public:
    std::string load() override { ++loadCount; return {}; }
    bool storeAtomic(std::string_view) override { ++storeCount; return true; }
    bool clear() override { ++clearCount; return true; }
    std::string lastError() const override { return {}; }

    int loadCount = 0;
    int storeCount = 0;
    int clearCount = 0;
};

class FakePreferenceStore final : public rocket::IPreferenceStore {
public:
    rocket::AppPreferences load() override { return {}; }
    bool store(const rocket::AppPreferences&) override { return true; }
    std::string lastError() const override { return {}; }
};

class FakeHost final : public rocket::IPlatformHost {
public:
    double monotonicSeconds() const override { return now; }
    rocket::ViewportMetrics viewportMetrics() override { return {}; }
    bool focused() const override { return true; }
    bool visible() const override { return true; }
    bool fullscreenAvailable() const override { return true; }
    bool fullscreen() const override { return false; }
    bool setFullscreen(bool) override { return true; }
    void log(rocket::PlatformLogLevel, std::string_view) override {}
    bool haptic(double, double, double) override { return true; }

    double now = 1.0;
};

class FakeControllerSource final : public rocket::IControllerSource {
public:
    rocket::ControllerFrame sampleFrame(double) override { return {}; }
    rocket::InputSource activeSource() const override { return rocket::InputSource::None; }
    void reset() override {}
};

class FakeTextureSource final : public rocket::ITextureSource {
public:
    void request(std::string_view, std::string_view) override {}
    rocket::TextureStatus status(std::string_view) const override { return rocket::TextureStatus::Ready; }
    std::string lastError() const override { return {}; }
};

std::uint64_t mix(std::uint64_t hash, std::uint64_t value)
{
    hash ^= value;
    hash *= 1099511628211ULL;
    return hash;
}

class FakeRenderer final : public rocket::IGameRenderer {
public:
    FakeRenderer()
    {
        composer.setViewport({1280, 800, 1280, 800, 1.0F});
        for (std::size_t index = 1; index < rocket::textureIndex(rocket::TextureId::Count); ++index) {
            composer.setTextureReady(static_cast<rocket::TextureId>(index), true);
        }
    }

    bool initialize() override { return true; }
    void render(const rocket::RenderSnapshot& snapshot) override
    {
        screen = snapshot.screen;
        titleScreen = snapshot.titleScreen;
        miningCells = snapshot.miningCells.size();
        miningEnemies = snapshot.miningEnemies.size();
        miningDrones = snapshot.miningMiniDrones.size();
        std::uint64_t value = 1469598103934665603ULL;
        for (const rocket::MiningCell& cell : snapshot.miningCells) {
            value = mix(value, static_cast<std::uint64_t>(cell.material));
            value = mix(value, std::bit_cast<std::uint64_t>(cell.remainingToughness));
            value = mix(value, static_cast<std::uint64_t>(cell.feature));
            value = mix(value, static_cast<std::uint64_t>(cell.enemy));
        }
        for (const rocket::MiningEnemy& enemy : snapshot.miningEnemies) {
            value = mix(value, static_cast<std::uint64_t>(enemy.type));
            value = mix(value, std::bit_cast<std::uint64_t>(enemy.x));
            value = mix(value, std::bit_cast<std::uint64_t>(enemy.y));
            value = mix(value, std::bit_cast<std::uint64_t>(enemy.health));
        }
        for (const rocket::MiningMiniDroneAgent& drone : snapshot.miningMiniDrones) {
            value = mix(value, static_cast<std::uint64_t>(drone.role));
            value = mix(value, std::bit_cast<std::uint64_t>(drone.x));
            value = mix(value, std::bit_cast<std::uint64_t>(drone.y));
        }
        presentationSignature = value;
        const rocket::ScenePacket& packet = composer.compose(snapshot);
        draws.assign(packet.draws.begin(), packet.draws.end());
    }
    void shutdown() override {}

    rocket::Screen screen = rocket::Screen::Hangar;
    bool titleScreen = false;
    std::size_t miningCells = 0;
    std::size_t miningEnemies = 0;
    std::size_t miningDrones = 0;
    std::uint64_t presentationSignature = 0;
    rocket::SceneComposer composer;
    std::vector<rocket::SceneDraw> draws;
};

class FakeUi final : public rocket::IGameUi {
public:
    bool initialize(ActionHandler handler) override { actionHandler = std::move(handler); return true; }
    void setPanelHtml(const std::string&) override {}
    void render() override {}
    bool mouseMove(int, int) override { return false; }
    bool mouseDown(int, int, int) override { return false; }
    bool mouseUp(int, int, int) override { return false; }
    bool mouseWheel(int, int, double) override { return false; }
    bool hitTest(int, int) const override { return false; }
    bool navigate(rocket::UiDirection) override { return false; }
    bool activateFocused() override { return false; }
    bool cancel() override { return false; }
    bool scroll(float) override { return false; }
    bool modalOpen() const override { return false; }
    void setControllerPresentation(bool, rocket::ControllerFamily) override {}
    void setControllerFocusVisible(bool) override {}
    void setControllerResumeBlocked(bool, bool) override {}
    std::string focusedId() const override { return {}; }
    void openModal(const std::string&) override {}
    void closeModal() override {}
    void dispatchAction(const std::string& action) override { if (actionHandler) actionHandler(action); }
    void refresh() override {}
    bool activateButtonLabel(const std::string&) override { return false; }
    void shutdown() override {}

    ActionHandler actionHandler;
};

class FakeUiBridge final : public rocket::IUiBridge {
public:
    void setPanelHtml(std::string_view) override {}
    void setRmlUiEnabled(bool) override {}
    void setModalOpen(bool) override {}
    void setControllerPresentation(bool, rocket::ControllerFamily) override {}
    void setControllerFocusVisible(bool) override {}
    void setControllerResumeBlocked(bool, bool) override {}
    void preferencesChanged(const rocket::AppPreferences&) override {}
};

struct Fixture {
    FakeSaveStore saves;
    FakePreferenceStore preferences;
    FakeHost host;
    FakeControllerSource controllers;
    FakeTextureSource textures;
    FakeRenderer renderer;
    FakeUi ui;
    FakeUiBridge bridge;
    rocket::AppServices services {saves, preferences, host, controllers, textures, renderer, ui, bridge};
    rocket::RocketGameApp app {services};
};

struct Observation {
    rocket::performance::BenchmarkScenarioSetupResult setup;
    rocket::InputContext inputContext = rocket::InputContext::Ui;
    int saveWrites = 0;
    bool titleScreen = false;
    std::size_t miningCells = 0;
    std::size_t miningEnemies = 0;
    std::size_t miningDrones = 0;
    std::uint64_t presentationSignature = 0;
};

Observation observe(
    rocket::performance::NativeBenchmarkScenario scenario,
    std::uint64_t seed = 0x0BEB17ULL)
{
    Fixture fixture;
    assert(fixture.app.initialize());
    rocket::performance::NativeBenchmarkOptions options;
    options.enabled = true;
    options.scenario = scenario;
    options.seed = seed;
    rocket::performance::BenchmarkScenarioDriver driver;
    Observation result;
    result.setup = driver.setup(fixture.app, options);
    result.inputContext = fixture.app.inputContext();
    fixture.app.renderScene();
    result.saveWrites = fixture.saves.storeCount;
    result.titleScreen = fixture.renderer.titleScreen;
    result.miningCells = fixture.renderer.miningCells;
    result.miningEnemies = fixture.renderer.miningEnemies;
    result.miningDrones = fixture.renderer.miningDrones;
    result.presentationSignature = fixture.renderer.presentationSignature;
    fixture.app.shutdown();
    return result;
}

void testScenarioRoutingAndSaveSuppression()
{
    using rocket::performance::NativeBenchmarkScenario;

    const Observation title = observe(NativeBenchmarkScenario::Title);
    assert(title.setup);
    assert(title.setup.screen == rocket::Screen::Hangar);
    assert(title.titleScreen);
    assert(title.saveWrites == 0);

    const Observation hangar = observe(NativeBenchmarkScenario::Hangar);
    assert(hangar.setup);
    assert(hangar.setup.screen == rocket::Screen::Hangar);
    assert(hangar.inputContext == rocket::InputContext::Ui);
    assert(hangar.saveWrites == 0);

    const Observation launch = observe(NativeBenchmarkScenario::Launch);
    assert(launch.setup);
    assert(launch.setup.screen == rocket::Screen::Launch);
    assert(launch.inputContext == rocket::InputContext::Launch);
    assert(launch.saveWrites == 0);

    const Observation flyby = observe(NativeBenchmarkScenario::Flyby);
    assert(flyby.setup);
    assert(flyby.setup.screen == rocket::Screen::Flyby);
    assert(flyby.inputContext == rocket::InputContext::FlybyActive);
    assert(flyby.saveWrites == 0);

    const Observation orbit = observe(NativeBenchmarkScenario::Orbit);
    assert(orbit.setup);
    assert(orbit.setup.screen == rocket::Screen::Orbit);
    assert(orbit.inputContext == rocket::InputContext::OrbitActive);
    assert(orbit.saveWrites == 0);

    const Observation surfaceOps = observe(NativeBenchmarkScenario::SurfaceOps);
    assert(surfaceOps.setup);
    assert(surfaceOps.setup.screen == rocket::Screen::SurfaceExpedition);
    assert(surfaceOps.inputContext == rocket::InputContext::Ui);
    assert(surfaceOps.saveWrites == 0);

    const Observation surfaceScan = observe(NativeBenchmarkScenario::SurfaceScan);
    const Observation repeatedSurfaceScan = observe(NativeBenchmarkScenario::SurfaceScan);
    assert(surfaceScan.setup);
    assert(surfaceScan.setup.screen == rocket::Screen::SurfaceScan);
    assert(surfaceScan.inputContext == rocket::InputContext::SurfaceScan);
    assert(surfaceScan.saveWrites == 0);
    assert(surfaceScan.setup.gameplayStateHash.has_value());
    assert(surfaceScan.setup.gameplayStateHash == repeatedSurfaceScan.setup.gameplayStateHash);
}

void testFixedSeedHighEntityMining()
{
    using rocket::performance::NativeBenchmarkScenario;
    constexpr std::uint64_t seed = 0x1234ABCDEFULL;
    const Observation first = observe(NativeBenchmarkScenario::Mining, seed);
    const Observation repeated = observe(NativeBenchmarkScenario::Mining, seed);
    const Observation differentSeed = observe(NativeBenchmarkScenario::Mining, seed + 1U);

    assert(first.setup);
    assert(first.setup.screen == rocket::Screen::Mining);
    assert(first.saveWrites == 0);
    assert(first.miningCells >= 1000U);
    assert(first.miningEnemies > 0U);
    assert(first.miningDrones > 0U);
    assert(first.miningCells == repeated.miningCells);
    assert(first.miningEnemies == repeated.miningEnemies);
    assert(first.miningDrones == repeated.miningDrones);
    assert(first.presentationSignature == repeated.presentationSignature);
    assert(first.presentationSignature != differentSeed.presentationSignature);
    assert(first.setup.gameplayStateHash.has_value());
    assert(first.setup.gameplayStateHash == repeated.setup.gameplayStateHash);
    assert(first.setup.gameplayStateHash != differentSeed.setup.gameplayStateHash);
}

void testDisabledOptionsAreRejectedWithoutMutation()
{
    Fixture fixture;
    assert(fixture.app.initialize());
    rocket::performance::NativeBenchmarkOptions options;
    options.enabled = false;
    options.scenario = rocket::performance::NativeBenchmarkScenario::Mining;
    rocket::performance::BenchmarkScenarioDriver driver;
    const rocket::performance::BenchmarkScenarioSetupResult result = driver.setup(fixture.app, options);
    assert(!result);
    assert(result.screen == rocket::Screen::Hangar);
    assert(fixture.saves.storeCount == 0);
    assert(driver.sampleGameplayStateHash(fixture.app).has_value());
    fixture.app.shutdown();
}

void testLongRunMiningSubmissionBudget()
{
    Fixture fixture;
    assert(fixture.app.initialize());
    rocket::performance::NativeBenchmarkOptions options;
    options.enabled = true;
    options.scenario = rocket::performance::NativeBenchmarkScenario::Mining;
    options.seed = 0x0BEB17ULL;
    rocket::performance::BenchmarkScenarioDriver driver;
    assert(driver.setup(fixture.app, options));
    for (int frame = 0; frame < 4200; ++frame) {
        fixture.app.tick(1.0 / 60.0);
    }
    fixture.renderer.composer.setPresentationTime(70.0);
    fixture.app.renderScene();

    std::size_t triangles = 0;
    std::size_t instances = 0;
    for (const rocket::SceneDraw& draw : fixture.renderer.draws) {
        triangles += draw.drawType == rocket::SceneDrawType::Triangles ? 1U : 0U;
        instances += draw.drawType == rocket::SceneDrawType::InstancedQuad ? 1U : 0U;
    }
    const std::uint64_t stateHash = fixture.app.deterministicStateHash();
    if (stateHash != kExpectedLongRunMiningHash) {
        std::fprintf(stderr, "Long-run mining state hash: 0x%llx\n", static_cast<unsigned long long>(stateHash));
    }
    assert(stateHash == kExpectedLongRunMiningHash);
    assert(fixture.renderer.draws.size() <= 33U);
    assert(fixture.renderer.draws.size() == 5U);
    assert(triangles == 0U);
    assert(instances == 5U);
    fixture.app.shutdown();
}

void testLongRunMiningRunnerHash()
{
    FakeSaveStore saves;
    FakePreferenceStore preferences;
    FakeHost host;
    FakeControllerSource controllers;
    FakeTextureSource textures;
    FakeRenderer renderer;
    FakeUi ui;
    FakeUiBridge bridge;
    rocket::AppServices services {
        saves, preferences, host, controllers, textures, renderer, ui, bridge};
    rocket::GameRunner runner(services);
    assert(runner.initialize());
    rocket::performance::NativeBenchmarkOptions options;
    options.enabled = true;
    options.scenario = rocket::performance::NativeBenchmarkScenario::Mining;
    options.seed = 0x0BEB17ULL;
    rocket::performance::BenchmarkScenarioDriver driver;
    assert(driver.setup(runner.app(), options));
    for (int frame = 0; frame < 4200; ++frame) {
        runner.frameForBenchmark(1.0 / 60.0);
    }
    const std::uint64_t runnerStateHash = runner.app().deterministicStateHash();
    if (runnerStateHash != kExpectedLongRunMiningHash) {
        std::fprintf(stderr, "Long-run mining runner hash: 0x%llx\n", static_cast<unsigned long long>(runnerStateHash));
    }
    assert(runnerStateHash == kExpectedLongRunMiningHash);
    runner.shutdown();
}

} // namespace

int main()
{
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    testScenarioRoutingAndSaveSuppression();
    testFixedSeedHighEntityMining();
    testDisabledOptionsAreRejectedWithoutMutation();
    testLongRunMiningSubmissionBudget();
    testLongRunMiningRunnerHash();
    return 0;
}
