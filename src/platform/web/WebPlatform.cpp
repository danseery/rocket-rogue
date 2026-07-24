#include "platform/web/WebPlatform.h"

#include "platform/web/WebGamepadSource.h"

#include <algorithm>
#include <cstdlib>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <limits>
#include <sstream>
#include <utility>

namespace rocket {
namespace {

UiSurfaceKind uiSurfaceKindForPanelHtml(std::string_view html) noexcept
{
    if (html.find("data-panel-mode=\"mining-fullscreen\"") != std::string_view::npos) {
        return UiSurfaceKind::Mining;
    }
    if (html.find("data-panel-mode=\"title\"") != std::string_view::npos
        || html.find("data-panel-mode=\"story-briefing\"") != std::string_view::npos
        || html.find("data-panel-mode=\"results\"") != std::string_view::npos
        || html.find("data-panel-mode=\"workspace\"") != std::string_view::npos
        || html.find("data-panel-mode=\"drone-workspace\"") != std::string_view::npos
        || html.find("data-panel-mode=\"arrival-fanfare\"") != std::string_view::npos
        || html.find("data-panel-mode=\"mission-stamp\"") != std::string_view::npos) {
        return UiSurfaceKind::Fullscreen;
    }
    return UiSurfaceKind::PersistentPanel;
}

EM_JS(char*, rr_web_string_preference, (int field), {
    let value = "";
    try {
        if (field === 0) value = localStorage.getItem("rocket_rogue_resolution") || "auto";
        else if (field === 1) value = localStorage.getItem("rocket_rogue_help_dismissed_v1") || "[]";
        else if (field === 2) value = localStorage.getItem("rocket_rogue_frame_limit_mode") || "platform_default";
        else if (field === 3) value = localStorage.getItem("rocket_rogue_keyboard_drill_mode") || "toggle";
    } catch (error) {}
    const length = lengthBytesUTF8(value) + 1;
    const result = _malloc(length);
    stringToUTF8(value, result, length);
    return result;
});

EM_JS(double, rr_web_number_preference, (int field), {
    try {
        if (field === 0) {
            const value = Number(localStorage.getItem("rocket_rogue_game_speed") || "1");
            return Number.isFinite(value) ? Math.min(8, Math.max(0.25, value)) : 1;
        }
    } catch (error) {}
    return 1;
});

EM_JS(int, rr_web_bool_preference, (int field), {
    try {
        if (field === 0) return localStorage.getItem("rocket_rogue_debug_tools") === "1";
        if (field === 1) return localStorage.getItem("rocket_rogue_help_disabled") === "1";
        if (field === 2) return localStorage.getItem("rocket_rogue_camera_shake_disabled") === "1";
        if (field === 3) return document.fullscreenElement ? 1 : 0;
        if (field === 4) return localStorage.getItem("rocket_rogue_performance_stats") === "1";
    } catch (error) {}
    return 0;
});

EM_JS(void, rr_web_install_preference_revision_observer, (), {
    const state = Module.RocketPreferenceRevisionState = Module.RocketPreferenceRevisionState || {
        revision: 0,
        installed: false
    };
    if (state.installed) return;
    state.installed = true;

    const bump = () => { state.revision += 1; };
    const matches = (event, selector) => {
        const target = event && event.target;
        return !!(target && target.closest && target.closest(selector));
    };
    const changeSelector = [
        "[data-resolution-select]",
        "[data-frame-limit-select]",
        "[data-game-speed-select]",
        "[data-keyboard-drill-mode-select]",
        "[data-controller-prompt-select]",
        "[data-controller-deadzone-select]",
        "[data-help-toggle]",
        "[data-camera-shake-toggle]",
        "[data-debug-tools-toggle]"
    ].join(",");
    const clickSelector = [
        "button[data-help-toggle]",
        "button[data-camera-shake-toggle]",
        "button[data-debug-tools-toggle]",
        "button[data-controller-invert-toggle]",
        "button[data-controller-swap-toggle]",
        "button[data-controller-vibration-toggle]"
    ].join(",");

    document.addEventListener("change", (event) => {
        if (matches(event, changeSelector)) bump();
    }, true);
    document.addEventListener("click", (event) => {
        if (matches(event, clickSelector)) bump();
    }, true);
    globalThis.addEventListener("storage", (event) => {
        if (!event.key || event.key.startsWith("rocket_rogue_")) bump();
    });
    document.addEventListener("fullscreenchange", bump);
});

EM_JS(double, rr_web_preference_revision, (), {
    const state = Module.RocketPreferenceRevisionState;
    return state ? Number(state.revision) || 0 : 0;
});

EM_JS(void, rr_web_bump_preference_revision, (), {
    const state = Module.RocketPreferenceRevisionState = Module.RocketPreferenceRevisionState || {
        revision: 0,
        installed: false
    };
    state.revision += 1;
});

EM_JS(int, rr_web_store_preferences,
    (const char* resolutionPtr, const char* frameLimitPtr, const char* drillModePtr, double gameSpeed, int debugTools, int performanceStats, int helpDisabled, int cameraShakeDisabled, const char* dismissedPtr), {
        try {
            const resolution = UTF8ToString(resolutionPtr || 0) || "auto";
            const frameLimit = UTF8ToString(frameLimitPtr || 0) || "platform_default";
            const drillMode = UTF8ToString(drillModePtr || 0) || "toggle";
            const dismissed = UTF8ToString(dismissedPtr || 0) || "[]";
            if (resolution === "auto") localStorage.removeItem("rocket_rogue_resolution");
            else localStorage.setItem("rocket_rogue_resolution", resolution);
            if (Number(gameSpeed) === 1) localStorage.removeItem("rocket_rogue_game_speed");
            else localStorage.setItem("rocket_rogue_game_speed", String(gameSpeed));
            if (frameLimit === "platform_default") localStorage.removeItem("rocket_rogue_frame_limit_mode");
            else localStorage.setItem("rocket_rogue_frame_limit_mode", frameLimit);
            if (drillMode === "hold") localStorage.setItem("rocket_rogue_keyboard_drill_mode", "hold");
            else localStorage.removeItem("rocket_rogue_keyboard_drill_mode");
            const setFlag = (key, enabled) => enabled ? localStorage.setItem(key, "1") : localStorage.removeItem(key);
            setFlag("rocket_rogue_debug_tools", debugTools !== 0);
            setFlag("rocket_rogue_performance_stats", performanceStats !== 0);
            setFlag("rocket_rogue_help_disabled", helpDisabled !== 0);
            setFlag("rocket_rogue_camera_shake_disabled", cameraShakeDisabled !== 0);
            localStorage.setItem("rocket_rogue_help_dismissed_v1", dismissed);
            return 1;
        } catch (error) { return 0; }
    });

EM_JS(void, rr_web_sync_canvas, (), {
    const sync = Module.RocketSyncCanvas = Module.RocketSyncCanvas || (() => {
        const canvas = document.getElementById("canvas");
        if (!canvas) return;
        const viewport = globalThis.visualViewport;
        const width = Math.max(1, Math.round((viewport && viewport.width) || innerWidth || canvas.clientWidth || 1));
        const height = Math.max(1, Math.round((viewport && viewport.height) || innerHeight || canvas.clientHeight || 1));
        canvas.style.width = width + "px";
        canvas.style.height = height + "px";
        let targetWidth = 0, targetHeight = 0;
        const preset = document.body.dataset.renderResolution || "auto";
        const match = /^(1280x800|1920x1080|2560x1440|3840x2160)$/.exec(preset);
        if (match) [targetWidth, targetHeight] = match[1].split("x").map(Number);
        if (targetWidth > 0) {
            const scale = Math.min(targetWidth / width, targetHeight / height);
            canvas.width = Math.max(1, Math.round(width * scale));
            canvas.height = Math.max(1, Math.round(height * scale));
        } else {
            const scale = Math.max(1, Math.min(2, devicePixelRatio || 1));
            canvas.width = Math.max(1, Math.ceil(width * scale));
            canvas.height = Math.max(1, Math.ceil(height * scale));
        }
        Module.RocketViewportRevision = (Module.RocketViewportRevision || 0) + 1;
    });
    sync();
    if (!Module.RocketCanvasSyncInstalled) {
        Module.RocketCanvasSyncInstalled = true;
        globalThis.addEventListener("resize", sync, {passive: true});
        globalThis.visualViewport?.addEventListener("resize", sync, {passive: true});
    }
});

EM_JS(double, rr_web_viewport_revision, (), { return Number(Module.RocketViewportRevision || 0); });

EM_JS(int, rr_web_metric, (int field), {
    const canvas = document.getElementById("canvas");
    if (field === 0) return Math.max(1, Math.round((visualViewport && visualViewport.width) || innerWidth || 1));
    if (field === 1) return Math.max(1, Math.round((visualViewport && visualViewport.height) || innerHeight || 1));
    if (field === 2) return Math.max(1, (canvas && canvas.width) || innerWidth || 1);
    return Math.max(1, (canvas && canvas.height) || innerHeight || 1);
});

EM_JS(int, rr_web_environment, (int field), {
    if (field === 0) return document.hasFocus() ? 1 : 0;
    if (field === 1) return document.visibilityState === "visible" ? 1 : 0;
    if (field === 2) return document.fullscreenEnabled ? 1 : 0;
    return document.fullscreenElement ? 1 : 0;
});

EM_JS(int, rr_web_set_fullscreen, (int enabled), {
    try {
        if (enabled && !document.fullscreenElement) {
            const promise = document.documentElement.requestFullscreen();
            if (promise && promise.catch) promise.catch(() => {});
        } else if (!enabled && document.fullscreenElement) {
            const promise = document.exitFullscreen();
            if (promise && promise.catch) promise.catch(() => {});
        }
        return 1;
    } catch (error) { return 0; }
});

EM_JS(void, rr_web_request_image, (const char* keyPtr, const char* pathPtr), {
    const key = UTF8ToString(keyPtr), path = UTF8ToString(pathPtr);
    Module.RocketArt = Module.RocketArt || {};
    if (Module.RocketArt[key]) return;
    const image = new Image();
    const record = {image, ready: false, failed: false, width: 0, height: 0};
    image.onload = () => { record.ready = true; record.width = image.naturalWidth || image.width; record.height = image.naturalHeight || image.height; };
    image.onerror = () => { record.failed = true; };
    image.src = path;
    Module.RocketArt[key] = record;
});

EM_JS(int, rr_web_image_status, (const char* keyPtr), {
    const record = Module.RocketArt && Module.RocketArt[UTF8ToString(keyPtr)];
    return record && record.failed ? -1 : (record && record.ready ? 1 : 0);
});

EM_JS(int, rr_web_image_dimensions, (const char* keyPtr, int* dimensions), {
    const record = Module.RocketArt && Module.RocketArt[UTF8ToString(keyPtr)];
    if (!record || !record.ready || record.width <= 0 || record.height <= 0) return 0;
    HEAP32[dimensions >> 2] = record.width;
    HEAP32[(dimensions + 4) >> 2] = record.height;
    return 1;
});

EM_JS(int, rr_web_decode_image_rgba, (const char* keyPtr, unsigned char* destination, int capacity), {
    const record = Module.RocketArt && Module.RocketArt[UTF8ToString(keyPtr)];
    if (!record || !record.ready || record.width <= 0 || record.height <= 0) return 0;
    const required = record.width * record.height * 4;
    if (!destination || capacity < required) return 0;
    try {
        const canvas = document.createElement("canvas");
        canvas.width = record.width;
        canvas.height = record.height;
        const context = canvas.getContext("2d", {alpha: true, willReadFrequently: true});
        if (!context) return 0;
        context.clearRect(0, 0, record.width, record.height);
        context.drawImage(record.image, 0, 0);
        const pixels = context.getImageData(0, 0, record.width, record.height).data;
        HEAPU8.set(pixels, destination);
        return 1;
    } catch (error) {
        console.warn("OREBIT image decode failed", error);
        return 0;
    }
});

EM_JS(void, rr_web_set_panel, (const char* valuePtr), {
    if (window.RocketBridge && RocketBridge.setPanel) RocketBridge.setPanel(UTF8ToString(valuePtr));
    Module.RocketViewportRevision = (Module.RocketViewportRevision || 0) + 1;
});
EM_JS(void, rr_web_set_realtime_hud, (const char* valuePtr), {
    if (!window.RocketBridge || !RocketBridge.setRealtimeHudState) return;
    try { RocketBridge.setRealtimeHudState(JSON.parse(UTF8ToString(valuePtr || 0) || "[]")); }
    catch (error) { console.warn("Rocket Rogue realtime HUD update failed", error); }
});
EM_JS(void, rr_web_set_rml_enabled, (int value), {
    const rmlEnabled = value !== 0;
    document.body.classList.toggle("rmlui-enabled", rmlEnabled);
    document.body.dataset.uiRenderer = rmlEnabled ? "rmlui" : "unavailable";
    const startupStatus = document.getElementById("ui-startup-status");
    if (startupStatus && !rmlEnabled) {
        startupStatus.textContent = "RmlUi initialization failed. Check the browser console for details.";
    }
    if (window.RocketBridge && RocketBridge.syncSceneOverlays) RocketBridge.syncSceneOverlays();
    Module.RocketViewportRevision = (Module.RocketViewportRevision || 0) + 1;
});
EM_JS(void, rr_web_set_ui_viewport_layout,
    (int layoutClass,
        int sceneX, int sceneY, int sceneWidth, int sceneHeight,
        int panelX, int panelY, int panelWidth, int panelHeight,
        int topPanelX, int topPanelY, int topPanelWidth, int topPanelHeight,
        int hudX, int hudY, int hudWidth, int hudHeight), {
    if (!window.RocketBridge || !RocketBridge.setUiViewportLayout) return;
    const layoutClassNames = ["fullscreen", "rail", "dock", "mining"];
    RocketBridge.setUiViewportLayout({
        layoutClass: layoutClassNames[layoutClass] || "fullscreen",
        sceneRect: {x: sceneX, y: sceneY, width: sceneWidth, height: sceneHeight},
        panelRect: {x: panelX, y: panelY, width: panelWidth, height: panelHeight},
        topPanelRect: {x: topPanelX, y: topPanelY, width: topPanelWidth, height: topPanelHeight},
        hudSafeRect: {x: hudX, y: hudY, width: hudWidth, height: hudHeight}
    });
});
EM_JS(void, rr_web_set_modal_open, (int value), {
    const open = value !== 0;
    if (window.RocketBridge && RocketBridge.setModalOpen) {
        RocketBridge.setModalOpen(open);
        return;
    }
    document.body.classList.toggle("rmlui-modal-open", open);
});
EM_JS(void, rr_web_controller_presentation, (int active, int family), { if (window.RocketBridge && RocketBridge.setControllerPresentation) RocketBridge.setControllerPresentation(active !== 0, family); });
EM_JS(void, rr_web_controller_focus, (int value), { document.body.classList.toggle("controller-focus-visible", value !== 0); });
EM_JS(void, rr_web_controller_resume, (int blocked, int connected), { if (window.RocketBridge && RocketBridge.setControllerResumeBlocked) RocketBridge.setControllerResumeBlocked(blocked !== 0, connected !== 0); });
EM_JS(void, rr_web_preferences_changed, (const char* resolutionPtr, int debugTools, int performanceStats), {
    const resolution = UTF8ToString(resolutionPtr || 0);
    document.body.dataset.renderResolution = resolution;
    const tools = document.getElementById("debug-tools"); if (tools) tools.classList.toggle("is-visible", debugTools !== 0);
    document.body.classList.toggle("performance-stats-enabled", performanceStats !== 0);
    if (window.RocketBridge && RocketBridge.setResolutionPreset) RocketBridge.setResolutionPreset(resolution);
    if (Module.RocketSyncCanvas) Module.RocketSyncCanvas();
    if (window.RocketBridge && RocketBridge.syncSettingsControls) RocketBridge.syncSettingsControls();
    if (window.RocketBridge && RocketBridge.syncDebugToolsControls) RocketBridge.syncDebugToolsControls();
    if (window.RocketBridge && RocketBridge.syncPerformanceStatsControls) RocketBridge.syncPerformanceStatsControls();
});
EM_JS(void, rr_web_set_performance_stats, (const char* htmlPtr, int visible), {
    if (window.RocketBridge && RocketBridge.setPerformanceStats) {
        RocketBridge.setPerformanceStats(UTF8ToString(htmlPtr || 0), visible !== 0);
    }
});

std::string takeJsString(char* value)
{
    const std::string result = value ? value : "";
    std::free(value);
    return result;
}

FrameLimitMode parseFrameLimitMode(std::string_view value)
{
    if (value == "smooth60") return FrameLimitMode::Smooth60;
    if (value == "balanced") return FrameLimitMode::Balanced;
    if (value == "battery30") return FrameLimitMode::Battery30;
    if (value == "display") return FrameLimitMode::Display;
    return FrameLimitMode::PlatformDefault;
}

const char* frameLimitModeName(FrameLimitMode mode)
{
    switch (mode) {
    case FrameLimitMode::Smooth60: return "smooth60";
    case FrameLimitMode::Balanced: return "balanced";
    case FrameLimitMode::Battery30: return "battery30";
    case FrameLimitMode::Display: return "display";
    case FrameLimitMode::PlatformDefault:
    default: return "platform_default";
    }
}

std::vector<std::string> parseSimpleJsonStrings(const std::string& json)
{
    std::vector<std::string> result;
    bool quoted = false, escaped = false;
    std::string value;
    for (char c : json) {
        if (!quoted) { if (c == '"') { quoted = true; value.clear(); } continue; }
        if (escaped) { value.push_back(c); escaped = false; }
        else if (c == '\\') escaped = true;
        else if (c == '"') { quoted = false; result.push_back(value); }
        else value.push_back(c);
    }
    return result;
}

std::string encodeSimpleJsonStrings(const std::vector<std::string>& values)
{
    std::ostringstream stream; stream << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) stream << ','; stream << '"';
        for (char c : values[i]) { if (c == '"' || c == '\\') stream << '\\'; stream << c; }
        stream << '"';
    }
    stream << ']'; return stream.str();
}

void appendJsonString(std::ostringstream& stream, std::string_view value)
{
    stream << '"';
    for (const unsigned char c : value) {
        switch (c) {
        case '"': stream << "\\\""; break;
        case '\\': stream << "\\\\"; break;
        case '\b': stream << "\\b"; break;
        case '\f': stream << "\\f"; break;
        case '\n': stream << "\\n"; break;
        case '\r': stream << "\\r"; break;
        case '\t': stream << "\\t"; break;
        default:
            if (c < 0x20) {
                constexpr char hex[] = "0123456789abcdef";
                stream << "\\u00" << hex[(c >> 4) & 0xf] << hex[c & 0xf];
            } else {
                stream << static_cast<char>(c);
            }
            break;
        }
    }
    stream << '"';
}

std::string encodeRealtimeHudState(const RealtimeHudState& state)
{
    std::ostringstream stream;
    stream << '[';
    for (std::size_t index = 0; index < state.patches.size(); ++index) {
        const RealtimeHudPatch& patch = state.patches[index];
        if (index > 0) {
            stream << ',';
        }
        stream << "{\"id\":";
        appendJsonString(stream, patch.elementId);
        if (patch.updateText) {
            stream << ",\"text\":";
            appendJsonString(stream, patch.text);
        }
        if (patch.updateClass) {
            stream << ",\"className\":";
            appendJsonString(stream, patch.cssClass);
        }
        stream << '}';
    }
    stream << ']';
    return stream.str();
}

} // namespace

WebPreferenceStore::WebPreferenceStore()
{
    rr_web_install_preference_revision_observer();
}

AppPreferences WebPreferenceStore::load()
{
    const std::uint64_t currentRevision = revision();
    if (loaded_ && observedRevision_ == currentRevision) {
        return cached_;
    }

    AppPreferences preferences;
    preferences.controller = loadWebControllerPreferences();
    preferences.resolutionPreset = takeJsString(rr_web_string_preference(0));
    preferences.frameLimitMode = parseFrameLimitMode(takeJsString(rr_web_string_preference(2)));
    preferences.miningDrillMode = takeJsString(rr_web_string_preference(3)) == "hold" ? MiningDrillMode::Hold : MiningDrillMode::Toggle;
    preferences.gameSpeed = rr_web_number_preference(0);
    preferences.debugToolsEnabled = rr_web_bool_preference(0) != 0;
    preferences.performanceStatsEnabled = rr_web_bool_preference(4) != 0;
    preferences.helpDisabled = rr_web_bool_preference(1) != 0;
    preferences.cameraShakeDisabled = rr_web_bool_preference(2) != 0;
    preferences.fullscreen = rr_web_bool_preference(3) != 0;
    preferences.dismissedHelpTopics = parseSimpleJsonStrings(takeJsString(rr_web_string_preference(1)));
    cached_ = std::move(preferences);
    observedRevision_ = currentRevision;
    loaded_ = true;
    return cached_;
}

bool WebPreferenceStore::store(const AppPreferences& preferences)
{
    storeWebControllerPreferences(preferences.controller);
    // Controller preferences use a separate JSON key, so mark the aggregate
    // cache dirty even if a later general-preference write fails.
    rr_web_bump_preference_revision();
    loaded_ = false;
    const std::string dismissed = encodeSimpleJsonStrings(preferences.dismissedHelpTopics);
    const char* drillMode = preferences.miningDrillMode == MiningDrillMode::Hold ? "hold" : "toggle";
    if (rr_web_store_preferences(preferences.resolutionPreset.c_str(), frameLimitModeName(preferences.frameLimitMode), drillMode, preferences.gameSpeed,
            preferences.debugToolsEnabled, preferences.performanceStatsEnabled, preferences.helpDisabled,
            preferences.cameraShakeDisabled, dismissed.c_str()) != 0) {
        // Reload lazily so normalization performed by the JS storage boundary
        // remains authoritative without returning to per-frame localStorage reads.
        lastError_.clear();
        return true;
    }
    lastError_ = "Browser preference storage failed."; return false;
}

std::uint64_t WebPreferenceStore::revision() const
{
    return static_cast<std::uint64_t>(std::max(0.0, rr_web_preference_revision()));
}

std::string WebPreferenceStore::lastError() const { return lastError_; }

WebPlatformHost::WebPlatformHost(WebGamepadSource& gamepads) : gamepads_(gamepads) {}

bool WebPlatformHost::createGraphicsContext()
{
    rr_web_sync_canvas();
    EmscriptenWebGLContextAttributes attributes; emscripten_webgl_init_context_attributes(&attributes);
    attributes.majorVersion = 2; attributes.minorVersion = 0; attributes.alpha = EM_FALSE;
    attributes.depth = EM_FALSE; attributes.stencil = EM_FALSE; attributes.antialias = EM_TRUE;
    const auto context = emscripten_webgl_create_context("#canvas", &attributes);
    return context > 0 && emscripten_webgl_make_context_current(context) == EMSCRIPTEN_RESULT_SUCCESS;
}

double WebPlatformHost::monotonicSeconds() const { return emscripten_get_now() / 1000.0; }
ViewportMetrics WebPlatformHost::viewportMetrics()
{
    const std::uint64_t revision = static_cast<std::uint64_t>(std::max(0.0, rr_web_viewport_revision()));
    if (revision == viewportRevision_) {
        return cachedViewportMetrics_;
    }
    cachedViewportMetrics_.logicalWidth = rr_web_metric(0);
    cachedViewportMetrics_.logicalHeight = rr_web_metric(1);
    cachedViewportMetrics_.drawableWidth = rr_web_metric(2);
    cachedViewportMetrics_.drawableHeight = rr_web_metric(3);
    cachedViewportMetrics_.densityRatio = static_cast<float>(cachedViewportMetrics_.drawableWidth)
        / static_cast<float>(std::max(1, cachedViewportMetrics_.logicalWidth));
    viewportRevision_ = revision;
    return cachedViewportMetrics_;
}
bool WebPlatformHost::focused() const { return rr_web_environment(0) != 0; }
bool WebPlatformHost::visible() const { return rr_web_environment(1) != 0; }
bool WebPlatformHost::fullscreenAvailable() const { return rr_web_environment(2) != 0; }
bool WebPlatformHost::fullscreen() const { return rr_web_environment(3) != 0; }
bool WebPlatformHost::setFullscreen(bool enabled) { return rr_web_set_fullscreen(enabled ? 1 : 0) != 0; }
void WebPlatformHost::log(PlatformLogLevel level, std::string_view message)
{
    const std::string copy(message);
    emscripten_log(level == PlatformLogLevel::Error ? EM_LOG_ERROR : (level == PlatformLogLevel::Warning ? EM_LOG_WARN : EM_LOG_CONSOLE), "%s", copy.c_str());
}
bool WebPlatformHost::haptic(double duration, double weak, double strong) { return gamepads_.playHaptic(duration, weak, strong); }

void WebTextureSource::request(std::string_view key, std::string_view path)
{
    const std::string keyCopy(key), pathCopy(path); rr_web_request_image(keyCopy.c_str(), pathCopy.c_str());
}
TextureStatus WebTextureSource::status(std::string_view key) const
{
    const std::string copy(key); const int status = rr_web_image_status(copy.c_str());
    if (status < 0) { lastError_ = "Required web asset failed to load: " + copy; return TextureStatus::Failed; }
    return status > 0 ? TextureStatus::Ready : TextureStatus::Pending;
}
std::optional<DecodedImageView> WebTextureSource::decodedImage(std::string_view key) const
{
    const std::string copy(key);
    if (const auto found = decodedImages_.find(copy); found != decodedImages_.end()) {
        return DecodedImageView {found->second.width, found->second.height, found->second.rgba};
    }
    if (status(key) != TextureStatus::Ready) {
        return std::nullopt;
    }

    int dimensions[2] = {};
    if (rr_web_image_dimensions(copy.c_str(), dimensions) == 0
        || dimensions[0] <= 0 || dimensions[1] <= 0) {
        lastError_ = "Required web asset has invalid dimensions: " + copy;
        return std::nullopt;
    }

    const std::size_t byteCount = static_cast<std::size_t>(dimensions[0])
        * static_cast<std::size_t>(dimensions[1]) * 4U;
    if (byteCount > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        lastError_ = "Required web asset is too large to decode: " + copy;
        return std::nullopt;
    }

    DecodedImage image;
    image.width = dimensions[0];
    image.height = dimensions[1];
    image.rgba.resize(byteCount);
    if (rr_web_decode_image_rgba(copy.c_str(), image.rgba.data(), static_cast<int>(byteCount)) == 0) {
        lastError_ = "Required web asset could not be decoded to RGBA: " + copy;
        return std::nullopt;
    }

    auto [found, inserted] = decodedImages_.emplace(copy, std::move(image));
    (void)inserted;
    return DecodedImageView {found->second.width, found->second.height, found->second.rgba};
}
void WebTextureSource::releaseDecodedImage(std::string_view key) { decodedImages_.erase(std::string(key)); }
std::string WebTextureSource::lastError() const { return lastError_; }

void WebUiBridge::setUiViewportLayout(const UiViewportLayout& layout)
{
    rr_web_set_ui_viewport_layout(
        static_cast<int>(layout.layoutClass),
        layout.sceneRect.x,
        layout.sceneRect.y,
        layout.sceneRect.width,
        layout.sceneRect.height,
        layout.panelRect.x,
        layout.panelRect.y,
        layout.panelRect.width,
        layout.panelRect.height,
        layout.topPanelRect.x,
        layout.topPanelRect.y,
        layout.topPanelRect.width,
        layout.topPanelRect.height,
        layout.hudSafeRect.x,
        layout.hudSafeRect.y,
        layout.hudSafeRect.width,
        layout.hudSafeRect.height);
}

UiSurfaceKind WebUiBridge::viewportSurfaceKind() const noexcept
{
    return viewportSurfaceKind_;
}

void WebUiBridge::setPanelHtml(std::string_view html)
{
    viewportSurfaceKind_ = uiSurfaceKindForPanelHtml(html);
    const std::string copy(html);
    rr_web_set_panel(copy.c_str());
}
void WebUiBridge::setRealtimeHudState(const RealtimeHudState& state)
{
    const std::string json = encodeRealtimeHudState(state);
    rr_web_set_realtime_hud(json.c_str());
}
void WebUiBridge::setRmlUiEnabled(bool enabled) { rr_web_set_rml_enabled(enabled); }
void WebUiBridge::setModalOpen(bool open) { rr_web_set_modal_open(open); }
void WebUiBridge::setControllerPresentation(bool active, ControllerFamily family) { rr_web_controller_presentation(active, static_cast<int>(family)); }
void WebUiBridge::setControllerFocusVisible(bool visible) { rr_web_controller_focus(visible); }
void WebUiBridge::setControllerResumeBlocked(bool blocked, bool connected) { rr_web_controller_resume(blocked, connected); }
void WebUiBridge::preferencesChanged(const AppPreferences& preferences)
{
    rr_web_preferences_changed(
        preferences.resolutionPreset.c_str(),
        preferences.debugToolsEnabled,
        preferences.performanceStatsEnabled);
}
void WebUiBridge::setPerformanceStats(std::string_view html, bool visible)
{
    const std::string copy(html);
    rr_web_set_performance_stats(copy.c_str(), visible ? 1 : 0);
}

} // namespace rocket
