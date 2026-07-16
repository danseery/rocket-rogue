#include "game/GameRunner.h"
#include "platform/AppServices.h"

#include <cassert>

namespace {

class FakeSaveStore final : public rocket::ISaveStore {
public:
    std::string load() override { return value; }
    bool storeAtomic(std::string_view data) override { value = data; return true; }
    bool clear() override { value.clear(); return true; }
    std::string lastError() const override { return {}; }
    std::string value;
};

class FakePreferenceStore final : public rocket::IPreferenceStore {
public:
    rocket::AppPreferences load() override { return value; }
    bool store(const rocket::AppPreferences& next) override { value = next; return true; }
    std::string lastError() const override { return {}; }
    rocket::AppPreferences value;
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
    void present() override { ++presentCount; }
    rocket::OpenGlDialect openGlDialect() const override { return rocket::OpenGlDialect::DesktopCore33; }
    mutable double now = 1.0;
    rocket::ViewportMetrics metrics {1280, 800, 2560, 1600, 2.0F, -1.0F};
    bool fullscreenValue = false;
    int hapticCount = 0;
    int presentCount = 0;
};

class FakeController final : public rocket::IControllerSource {
public:
    rocket::ControllerFrame sampleFrame(double) override
    {
        rocket::ControllerFrame result = frame;
        frame.pressed.reset();
        return result;
    }
    rocket::InputSource activeSource() const override { return rocket::InputSource::Controller; }
    void reset() override { resetCalled = true; }
    rocket::ControllerFrame frame;
    bool resetCalled = false;
};

class FakeTextureSource final : public rocket::ITextureSource {
public:
    void request(std::string_view, std::string_view) override {}
    rocket::TextureStatus status(std::string_view) const override { return rocket::TextureStatus::Ready; }
    bool uploadToOpenGl(std::string_view, unsigned int, int&, int&) override { return true; }
    std::string lastError() const override { return {}; }
};

class FakeRenderer final : public rocket::IGameRenderer {
public:
    bool initialize() override { initialized = true; return true; }
    void render(const rocket::RenderSnapshot&) override { ++renderCount; }
    void shutdown() override { shutdownCalled = true; }
    bool initialized = false;
    bool shutdownCalled = false;
    int renderCount = 0;
};

class FakeUi final : public rocket::IGameUi {
public:
    bool initialize(ActionHandler handler) override { actionHandler = std::move(handler); return true; }
    void setPanelHtml(const std::string& value) override { html = value; }
    void render() override { ++renderCount; }
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
    void shutdown() override { shutdownCalled = true; }
    ActionHandler actionHandler;
    std::string html;
    bool shutdownCalled = false;
    int renderCount = 0;
};

class FakeUiBridge final : public rocket::IUiBridge {
public:
    void setPanelHtml(std::string_view value) override { html = value; }
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
};

} // namespace

int main()
{
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

    controllers.frame.connected = true;
    controllers.frame.family = rocket::ControllerFamily::Xbox;
    controllers.frame.pressed.set(static_cast<std::size_t>(rocket::ControllerButton::South));
    host.now += 1.0 / 60.0;
    runner.frame();
    assert(renderer.renderCount == 1);
    assert(ui.renderCount == 1);
    assert(host.presentCount == 1);
    assert(host.hapticCount == 1);

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
