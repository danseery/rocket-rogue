#pragma once

#include "input/ControllerInput.h"
#include "platform/FrameLimitPolicy.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
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

struct ViewportMetrics {
    int logicalWidth = 1280;
    int logicalHeight = 800;
    int drawableWidth = 1280;
    int drawableHeight = 800;
    float densityRatio = 1.0F;
    float sceneLeftNdc = -1.0F;
};

struct RendererDiagnostics {
    int sceneDrawCalls = 0;
    int sceneVertices = 0;
    int bufferUploads = 0;
    std::size_t uploadedBytes = 0;
    int texturesReady = 0;
    int texturesPending = 0;
    int texturesFailed = 0;
    double gpuFrameMilliseconds = 0.0;
    // CPU time deliberately spent waiting for frame retirement/swapchain
    // acquisition or an explicit presentation deadline. Benchmark CPU work
    // excludes this pacing time so FIFO blocking is comparable with GL VSync.
    double limiterIdleMilliseconds = 0.0;
    double presentIntervalMilliseconds = 0.0;
    // The gameplay simulation cadence requested by the selected frame-limit
    // mode. Presentation may resolve to a refresh-compatible divisor while
    // deterministic benchmark simulation remains fixed to this nominal rate.
    double nominalTargetFramesPerSecond = 0.0;
    double targetFramesPerSecond = 0.0;
    int missedRefreshes = 0;
    int pipelineCreationsThisFrame = 0;
    std::size_t deviceMemoryBytes = 0;
    bool softwareFrameLimiterActive = false;
};

struct TextureDiagnostics {
    double decodeMilliseconds = 0.0;
    double uploadMilliseconds = 0.0;
    std::size_t decodedBytes = 0;
    std::size_t uploadedBytes = 0;
    int decodedTextures = 0;
    int uploadedTextures = 0;
};

struct UiDiagnostics {
    int documentRebuilds = 0;
    int panelRebuilds = 0;
    int hudPatches = 0;
    int compiledGeometry = 0;
    int renderedGeometry = 0;
};

struct PlatformDiagnostics {
    double frameLimiterMilliseconds = 0.0;
    double idleMilliseconds = 0.0;
    int suspendedWakeups = 0;
    double suspendedWakeupsPerSecond = 0.0;
    bool verticalSyncActive = false;
    bool softwareFrameLimiterActive = false;
};

struct RealtimeHudPatch {
    std::string elementId;
    std::string text;
    std::string cssClass;
    bool updateText = false;
    bool updateClass = false;
};

// Compact, presentation-only state for values that change while a realtime
// screen is active. Element ids refer to stable nodes emitted by GamePanel;
// changing screen structure continues to use setPanelHtml().
struct RealtimeHudState {
    std::vector<RealtimeHudPatch> patches;
};

struct PerformanceStats {
    double startupMilliseconds = 0.0;
    double framesPerSecond = 0.0;
    double latestFrameTimeMilliseconds = 0.0;
    double frameTimeMilliseconds = 0.0;
    double medianFrameTimeMilliseconds = 0.0;
    double p95FrameTimeMilliseconds = 0.0;
    double p99FrameTimeMilliseconds = 0.0;
    double cpuFrameMilliseconds = 0.0;
    double medianCpuFrameMilliseconds = 0.0;
    double p95CpuFrameMilliseconds = 0.0;
    double p99CpuFrameMilliseconds = 0.0;
    double inputMilliseconds = 0.0;
    double simulationMilliseconds = 0.0;
    double sceneRenderMilliseconds = 0.0;
    double uiRenderMilliseconds = 0.0;
    double presentMilliseconds = 0.0;
    int simulationSteps = 0;
    bool simulationDeltaClamped = false;
    ViewportMetrics viewport;
    RendererDiagnostics renderer;
    TextureDiagnostics textures;
    UiDiagnostics ui;
    PlatformDiagnostics platform;
};

struct AppPreferences {
    ControllerPreferences controller;
    std::string resolutionPreset = "auto";
    FrameLimitMode frameLimitMode = FrameLimitMode::PlatformDefault;
    double gameSpeed = 1.0;
    bool debugToolsEnabled = false;
    bool performanceStatsEnabled = false;
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
    // Cheap change token for frame-critical consumers. Implementations advance
    // this only when their authoritative preference state may have changed.
    virtual std::uint64_t revision() const { return 0; }
    virtual std::string lastError() const = 0;
};

class IPlatformHost {
public:
    virtual ~IPlatformHost() = default;
    virtual double monotonicSeconds() const = 0;
    virtual double displayRefreshRateHz() const { return 0.0; }
    virtual ViewportMetrics viewportMetrics() = 0;
    virtual bool focused() const = 0;
    virtual bool visible() const = 0;
    virtual bool fullscreenAvailable() const = 0;
    virtual bool fullscreen() const = 0;
    virtual bool setFullscreen(bool enabled) = 0;
    virtual void log(PlatformLogLevel level, std::string_view message) = 0;
    virtual bool haptic(double durationSeconds, double weakMagnitude, double strongMagnitude) = 0;
    virtual void paceFrame() {}
    virtual PlatformDiagnostics diagnostics() const { return {}; }
};

class IControllerSource {
public:
    virtual ~IControllerSource() = default;
    virtual ControllerFrame sampleFrame(double realTimeSeconds) = 0;
    virtual std::optional<ControllerFrame> syntheticPreviewFrame() const { return std::nullopt; }
    virtual void setPreferences(const ControllerPreferences&) {}
    virtual InputSource activeSource() const = 0;
    virtual void reset() = 0;
};

enum class TextureStatus {
    Pending,
    Ready,
    Failed
};

struct DecodedImageView {
    int width = 0;
    int height = 0;
    std::span<const std::uint8_t> rgba;

    constexpr bool valid() const noexcept
    {
        return width > 0 && height > 0
            && rgba.size() == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4U;
    }
};

class ITextureSource {
public:
    virtual ~ITextureSource() = default;
    virtual void request(std::string_view key, std::string_view relativePath) = 0;
    virtual TextureStatus status(std::string_view key) const = 0;
    virtual std::optional<DecodedImageView> decodedImage(std::string_view) const { return std::nullopt; }
    virtual void releaseDecodedImage(std::string_view) {}
    virtual std::string lastError() const = 0;
    virtual TextureDiagnostics diagnostics() const { return {}; }
};

enum class GraphicsFrameStatus {
    Ready,
    Skipped,
    Fatal
};

class IGameRenderer {
public:
    virtual ~IGameRenderer() = default;
    virtual bool initialize() = 0;
    virtual void render(const RenderSnapshot& snapshot) = 0;
    // Explicit-API backends finish the shared scene/UI command buffer and
    // present here. WebGL leaves this as a no-op because the browser commits
    // the immediate-mode commands from its main-loop callback.
    virtual GraphicsFrameStatus endFrameAndPresent() { return GraphicsFrameStatus::Ready; }
    virtual GraphicsFrameStatus frameStatus() const { return GraphicsFrameStatus::Ready; }
    virtual void shutdown() = 0;
    virtual void setPreferences(const AppPreferences&) {}
    virtual RendererDiagnostics diagnostics() const { return {}; }
    virtual std::string_view description() const { return "Graphics renderer"; }
};

class IGameUi {
public:
    using ActionHandler = std::function<void(const std::string&)>;

    virtual ~IGameUi() = default;
    virtual bool initialize(ActionHandler actionHandler) = 0;
    virtual void setPanelHtml(const std::string& html) = 0;
    virtual void setRealtimeHudState(const RealtimeHudState&) {}
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
    virtual void setPerformanceStats(const PerformanceStats&, bool) {}
    virtual UiDiagnostics diagnostics() const { return {}; }
    virtual void shutdown() = 0;
};

// The web adapter mirrors selected RmlUi state into the browser-shell fallback.
// Native platforms use a no-op implementation while RmlUi remains authoritative.
class IUiBridge {
public:
    virtual ~IUiBridge() = default;
    virtual void setPanelHtml(std::string_view html) = 0;
    virtual void setRealtimeHudState(const RealtimeHudState&) {}
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
    virtual void setPerformanceStats(std::string_view, bool) {}
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
