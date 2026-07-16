#pragma once

#include "input/ControllerInput.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rocket {

struct RenderSnapshot;

enum class PlatformLogLevel {
    Info,
    Warning,
    Error
};

enum class OpenGlDialect {
    WebGl2,
    DesktopCore33
};

struct ViewportMetrics {
    int logicalWidth = 1280;
    int logicalHeight = 800;
    int drawableWidth = 1280;
    int drawableHeight = 800;
    float densityRatio = 1.0F;
    float sceneLeftNdc = -1.0F;
};

struct AppPreferences {
    ControllerPreferences controller;
    std::string resolutionPreset = "auto";
    double gameSpeed = 1.0;
    bool debugToolsEnabled = false;
    bool helpDisabled = false;
    bool cameraShakeDisabled = false;
    bool fullscreen = false;
    std::vector<std::string> dismissedHelpTopics;
};

class ISaveStore {
public:
    virtual ~ISaveStore() = default;
    virtual std::string load() = 0;
    virtual bool storeAtomic(std::string_view data) = 0;
    virtual bool clear() = 0;
    virtual std::string lastError() const = 0;
    virtual std::string_view description() const { return "Versioned local save data"; }
};

class IPreferenceStore {
public:
    virtual ~IPreferenceStore() = default;
    virtual AppPreferences load() = 0;
    virtual bool store(const AppPreferences& preferences) = 0;
    virtual std::string lastError() const = 0;
};

class IPlatformHost {
public:
    virtual ~IPlatformHost() = default;
    virtual double monotonicSeconds() const = 0;
    virtual ViewportMetrics viewportMetrics() = 0;
    virtual bool focused() const = 0;
    virtual bool visible() const = 0;
    virtual bool fullscreenAvailable() const = 0;
    virtual bool fullscreen() const = 0;
    virtual bool setFullscreen(bool enabled) = 0;
    virtual void log(PlatformLogLevel level, std::string_view message) = 0;
    virtual bool haptic(double durationSeconds, double weakMagnitude, double strongMagnitude) = 0;
    virtual void present() = 0;
    virtual OpenGlDialect openGlDialect() const = 0;
};

class IControllerSource {
public:
    virtual ~IControllerSource() = default;
    virtual ControllerFrame sampleFrame(double realTimeSeconds) = 0;
    virtual std::optional<ControllerFrame> syntheticPreviewFrame() const { return std::nullopt; }
    virtual InputSource activeSource() const = 0;
    virtual void reset() = 0;
};

enum class TextureStatus {
    Pending,
    Ready,
    Failed
};

class ITextureSource {
public:
    virtual ~ITextureSource() = default;
    virtual void request(std::string_view key, std::string_view relativePath) = 0;
    virtual TextureStatus status(std::string_view key) const = 0;
    virtual bool uploadToOpenGl(
        std::string_view key,
        unsigned int texture,
        int& width,
        int& height) = 0;
    virtual std::string lastError() const = 0;
};

class IGameRenderer {
public:
    virtual ~IGameRenderer() = default;
    virtual bool initialize() = 0;
    virtual void render(const RenderSnapshot& snapshot) = 0;
    virtual void shutdown() = 0;
    virtual std::string_view description() const { return "Shared OpenGL renderer"; }
};

class IGameUi {
public:
    using ActionHandler = std::function<void(const std::string&)>;

    virtual ~IGameUi() = default;
    virtual bool initialize(ActionHandler actionHandler) = 0;
    virtual void setPanelHtml(const std::string& html) = 0;
    virtual void render() = 0;
    virtual bool mouseMove(int x, int y) = 0;
    virtual bool mouseDown(int x, int y, int button) = 0;
    virtual bool mouseUp(int x, int y, int button) = 0;
    virtual bool mouseWheel(int x, int y, double deltaY) = 0;
    virtual bool hitTest(int x, int y) const = 0;
    virtual bool navigate(UiDirection direction) = 0;
    virtual bool activateFocused() = 0;
    virtual bool cancel() = 0;
    virtual bool scroll(float amount) = 0;
    virtual bool modalOpen() const = 0;
    virtual void setControllerPresentation(bool active, ControllerFamily family) = 0;
    virtual void setControllerFocusVisible(bool visible) = 0;
    virtual void setControllerResumeBlocked(bool blocked, bool controllerConnected) = 0;
    virtual std::string focusedId() const = 0;
    virtual void openModal(const std::string& id) = 0;
    virtual void closeModal() = 0;
    virtual void dismissHelp(const std::string& topic) = 0;
    virtual void dispatchAction(const std::string& action) = 0;
    virtual void refresh() = 0;
    virtual bool activateButtonLabel(const std::string& label) = 0;
    virtual void shutdown() = 0;
};

// The web adapter mirrors selected RmlUi state into the browser-shell fallback.
// Native platforms use a no-op implementation while RmlUi remains authoritative.
class IUiBridge {
public:
    virtual ~IUiBridge() = default;
    virtual void setPanelHtml(std::string_view html) = 0;
    virtual void setRmlUiEnabled(bool enabled) = 0;
    virtual void setModalOpen(bool open) = 0;
    virtual void setControllerPresentation(bool active, ControllerFamily family) = 0;
    virtual void setControllerFocusVisible(bool visible) = 0;
    virtual void setControllerResumeBlocked(bool blocked, bool connected) = 0;
    virtual bool navigate(UiDirection direction) = 0;
    virtual bool activate() = 0;
    virtual bool cancel() = 0;
    virtual bool scroll(double amount) = 0;
    virtual bool modalOpen() const = 0;
    virtual bool openModal(std::string_view id) = 0;
    virtual void closeModal() = 0;
    virtual std::string focusedId() const = 0;
    virtual void preferencesChanged(const AppPreferences& preferences) = 0;
};

struct AppServices {
    ISaveStore& saves;
    IPreferenceStore& preferences;
    IPlatformHost& host;
    IControllerSource& controllers;
    ITextureSource& textures;
    IGameRenderer& renderer;
    IGameUi& ui;
    IUiBridge& uiBridge;
};

} // namespace rocket
