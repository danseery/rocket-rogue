#include "platform/web/WebPlatform.h"

#include "platform/web/WebGamepadSource.h"

#include <algorithm>
#include <cstdlib>
#include <emscripten.h>
#include <emscripten/html5.h>
#include <sstream>

namespace rocket {
namespace {

EM_JS(char*, rr_web_string_preference, (int field), {
    let value = "";
    try {
        if (field === 0) value = localStorage.getItem("rocket_rogue_resolution") || "auto";
        else if (field === 1) value = localStorage.getItem("rocket_rogue_help_dismissed_v1") || "[]";
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
    } catch (error) {}
    return 0;
});

EM_JS(int, rr_web_store_preferences,
    (const char* resolutionPtr, double gameSpeed, int debugTools, int helpDisabled, int cameraShakeDisabled, const char* dismissedPtr), {
        try {
            const resolution = UTF8ToString(resolutionPtr || 0) || "auto";
            const dismissed = UTF8ToString(dismissedPtr || 0) || "[]";
            if (resolution === "auto") localStorage.removeItem("rocket_rogue_resolution");
            else localStorage.setItem("rocket_rogue_resolution", resolution);
            if (Number(gameSpeed) === 1) localStorage.removeItem("rocket_rogue_game_speed");
            else localStorage.setItem("rocket_rogue_game_speed", String(gameSpeed));
            const setFlag = (key, enabled) => enabled ? localStorage.setItem(key, "1") : localStorage.removeItem(key);
            setFlag("rocket_rogue_debug_tools", debugTools !== 0);
            setFlag("rocket_rogue_help_disabled", helpDisabled !== 0);
            setFlag("rocket_rogue_camera_shake_disabled", cameraShakeDisabled !== 0);
            localStorage.setItem("rocket_rogue_help_dismissed_v1", dismissed);
            return 1;
        } catch (error) { return 0; }
    });

EM_JS(void, rr_web_sync_canvas, (), {
    const canvas = document.getElementById("canvas");
    if (!canvas) return;
    const viewport = globalThis.visualViewport;
    const width = Math.max(1, Math.round((viewport && viewport.width) || innerWidth || canvas.clientWidth || 1));
    const height = Math.max(1, Math.round((viewport && viewport.height) || innerHeight || canvas.clientHeight || 1));
    canvas.style.width = width + "px";
    canvas.style.height = height + "px";
    let targetWidth = 0, targetHeight = 0;
    try {
        const preset = localStorage.getItem("rocket_rogue_resolution") || "auto";
        const match = /^(1280x800|1920x1080|2560x1440|3840x2160)$/.exec(preset);
        if (match) [targetWidth, targetHeight] = match[1].split("x").map(Number);
    } catch (error) {}
    if (targetWidth > 0) {
        const scale = Math.min(targetWidth / width, targetHeight / height);
        canvas.width = Math.max(1, Math.round(width * scale));
        canvas.height = Math.max(1, Math.round(height * scale));
    } else {
        const scale = Math.max(1, Math.min(2, devicePixelRatio || 1));
        canvas.width = Math.max(1, Math.ceil(width * scale));
        canvas.height = Math.max(1, Math.ceil(height * scale));
    }
});

EM_JS(int, rr_web_metric, (int field), {
    const canvas = document.getElementById("canvas");
    if (field === 0) return Math.max(1, Math.round((visualViewport && visualViewport.width) || innerWidth || 1));
    if (field === 1) return Math.max(1, Math.round((visualViewport && visualViewport.height) || innerHeight || 1));
    if (field === 2) return Math.max(1, (canvas && canvas.width) || innerWidth || 1);
    return Math.max(1, (canvas && canvas.height) || innerHeight || 1);
});

EM_JS(double, rr_web_scene_left_ndc, (), {
    const canvas = document.getElementById("canvas");
    const panel = document.getElementById("panel");
    const width = (canvas && canvas.clientWidth) || innerWidth || 1;
    if (width <= 720 || (panel && panel.querySelector(".mining-fullscreen"))) return -1;
    const flybyVisible = panel && panel.querySelector("[data-flyby-run]");
    const left = document.body.classList.contains("rmlui-enabled") && flybyVisible
        ? 16 + 482 + 24
        : (panel ? Math.max(0, panel.getBoundingClientRect().right + 24) : 0);
    if (width - left < 520) return -1;
    return Math.max(-1, Math.min(0.45, left / width * 2 - 1));
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

EM_JS(int, rr_web_upload_image, (const char* keyPtr, int textureId, int* dimensions), {
    const record = Module.RocketArt && Module.RocketArt[UTF8ToString(keyPtr)];
    if (!record || !record.ready || !GLctx || !GL || !GL.textures[textureId]) return 0;
    GLctx.bindTexture(GLctx.TEXTURE_2D, GL.textures[textureId]);
    GLctx.pixelStorei(GLctx.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
    GLctx.texParameteri(GLctx.TEXTURE_2D, GLctx.TEXTURE_WRAP_S, GLctx.CLAMP_TO_EDGE);
    GLctx.texParameteri(GLctx.TEXTURE_2D, GLctx.TEXTURE_WRAP_T, GLctx.CLAMP_TO_EDGE);
    GLctx.texParameteri(GLctx.TEXTURE_2D, GLctx.TEXTURE_MIN_FILTER, GLctx.NEAREST);
    GLctx.texParameteri(GLctx.TEXTURE_2D, GLctx.TEXTURE_MAG_FILTER, GLctx.NEAREST);
    GLctx.texImage2D(GLctx.TEXTURE_2D, 0, GLctx.RGBA, GLctx.RGBA, GLctx.UNSIGNED_BYTE, record.image);
    HEAP32[dimensions >> 2] = record.width;
    HEAP32[(dimensions + 4) >> 2] = record.height;
    return 1;
});

EM_JS(void, rr_web_set_panel, (const char* valuePtr), { if (window.RocketBridge && RocketBridge.setPanel) RocketBridge.setPanel(UTF8ToString(valuePtr)); });
EM_JS(void, rr_web_set_rml_enabled, (int value), { document.body.classList.toggle("rmlui-enabled", value !== 0); });
EM_JS(void, rr_web_set_modal_open, (int value), {
    const open = value !== 0; document.body.classList.toggle("rmlui-modal-open", open);
    const control = document.getElementById("scene-launch-control");
    if (control && open) { control.classList.remove("is-visible"); control.hidden = true; control.setAttribute("aria-hidden", "true"); }
    else if (control) { control.removeAttribute("aria-hidden"); if (window.RocketBridge && RocketBridge.syncSceneOverlays) RocketBridge.syncSceneOverlays(); }
});
EM_JS(void, rr_web_controller_presentation, (int active, int family), { if (window.RocketBridge && RocketBridge.setControllerPresentation) RocketBridge.setControllerPresentation(active !== 0, family); });
EM_JS(void, rr_web_controller_focus, (int value), { document.body.classList.toggle("controller-focus-visible", value !== 0); });
EM_JS(void, rr_web_controller_resume, (int blocked, int connected), { if (window.RocketBridge && RocketBridge.setControllerResumeBlocked) RocketBridge.setControllerResumeBlocked(blocked !== 0, connected !== 0); });
EM_JS(int, rr_web_ui_action, (int action, double value, const char* stringPtr), {
    if (!window.RocketBridge) return 0;
    if (action === 0 && RocketBridge.controllerNavigate) return RocketBridge.controllerNavigate(value) ? 1 : 0;
    if (action === 1 && RocketBridge.controllerActivate) return RocketBridge.controllerActivate() ? 1 : 0;
    if (action === 2 && RocketBridge.controllerCancel) return RocketBridge.controllerCancel() ? 1 : 0;
    if (action === 3 && RocketBridge.controllerScroll) return RocketBridge.controllerScroll(value) ? 1 : 0;
    if (action === 4 && RocketBridge.controllerModalOpen) return RocketBridge.controllerModalOpen() ? 1 : 0;
    if (action === 5 && RocketBridge.controllerOpenModal) return RocketBridge.controllerOpenModal(UTF8ToString(stringPtr || 0)) ? 1 : 0;
    if (action === 6 && RocketBridge.controllerCloseModal) { RocketBridge.controllerCloseModal(); return 1; }
    return 0;
});
EM_JS(char*, rr_web_focused_id, (), {
    const value = window.RocketBridge && RocketBridge.controllerFocusedId ? String(RocketBridge.controllerFocusedId() || "") : "";
    const length = lengthBytesUTF8(value) + 1, result = _malloc(length); stringToUTF8(value, result, length); return result;
});
EM_JS(void, rr_web_preferences_changed, (const char* resolutionPtr, int debugTools), {
    const resolution = UTF8ToString(resolutionPtr || 0);
    document.body.dataset.renderResolution = resolution;
    const tools = document.getElementById("debug-tools"); if (tools) tools.classList.toggle("is-visible", debugTools !== 0);
    if (window.RocketBridge && RocketBridge.setResolutionPreset) RocketBridge.setResolutionPreset(resolution);
    if (window.RocketBridge && RocketBridge.syncSettingsControls) RocketBridge.syncSettingsControls();
    if (window.RocketBridge && RocketBridge.syncDebugToolsControls) RocketBridge.syncDebugToolsControls();
});

std::string takeJsString(char* value)
{
    const std::string result = value ? value : "";
    std::free(value);
    return result;
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

} // namespace

AppPreferences WebPreferenceStore::load()
{
    AppPreferences preferences;
    preferences.controller = loadWebControllerPreferences();
    preferences.resolutionPreset = takeJsString(rr_web_string_preference(0));
    preferences.gameSpeed = rr_web_number_preference(0);
    preferences.debugToolsEnabled = rr_web_bool_preference(0) != 0;
    preferences.helpDisabled = rr_web_bool_preference(1) != 0;
    preferences.cameraShakeDisabled = rr_web_bool_preference(2) != 0;
    preferences.fullscreen = rr_web_bool_preference(3) != 0;
    preferences.dismissedHelpTopics = parseSimpleJsonStrings(takeJsString(rr_web_string_preference(1)));
    return preferences;
}

bool WebPreferenceStore::store(const AppPreferences& preferences)
{
    storeWebControllerPreferences(preferences.controller);
    const std::string dismissed = encodeSimpleJsonStrings(preferences.dismissedHelpTopics);
    if (rr_web_store_preferences(preferences.resolutionPreset.c_str(), preferences.gameSpeed,
            preferences.debugToolsEnabled, preferences.helpDisabled, preferences.cameraShakeDisabled, dismissed.c_str()) != 0) {
        lastError_.clear(); return true;
    }
    lastError_ = "Browser preference storage failed."; return false;
}

std::string WebPreferenceStore::lastError() const { return lastError_; }

WebPlatformHost::WebPlatformHost(WebGamepadSource& gamepads) : gamepads_(gamepads) {}

bool WebPlatformHost::createGraphicsContext()
{
    EmscriptenWebGLContextAttributes attributes; emscripten_webgl_init_context_attributes(&attributes);
    attributes.majorVersion = 2; attributes.minorVersion = 0; attributes.alpha = EM_FALSE;
    attributes.depth = EM_FALSE; attributes.stencil = EM_FALSE; attributes.antialias = EM_TRUE;
    const auto context = emscripten_webgl_create_context("#canvas", &attributes);
    return context > 0 && emscripten_webgl_make_context_current(context) == EMSCRIPTEN_RESULT_SUCCESS;
}

double WebPlatformHost::monotonicSeconds() const { return emscripten_get_now() / 1000.0; }
ViewportMetrics WebPlatformHost::viewportMetrics()
{
    rr_web_sync_canvas();
    return {rr_web_metric(0), rr_web_metric(1), rr_web_metric(2), rr_web_metric(3), 1.0F, static_cast<float>(rr_web_scene_left_ndc())};
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
void WebPlatformHost::present() {}
OpenGlDialect WebPlatformHost::openGlDialect() const { return OpenGlDialect::WebGl2; }

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
bool WebTextureSource::uploadToOpenGl(std::string_view key, unsigned int texture, int& width, int& height)
{
    int dimensions[2] = {}; const std::string copy(key);
    if (rr_web_upload_image(copy.c_str(), static_cast<int>(texture), dimensions) == 0) return false;
    width = dimensions[0]; height = dimensions[1]; return true;
}
std::string WebTextureSource::lastError() const { return lastError_; }

void WebUiBridge::setPanelHtml(std::string_view html) { const std::string copy(html); rr_web_set_panel(copy.c_str()); }
void WebUiBridge::setRmlUiEnabled(bool enabled) { rr_web_set_rml_enabled(enabled); }
void WebUiBridge::setModalOpen(bool open) { rr_web_set_modal_open(open); }
void WebUiBridge::setControllerPresentation(bool active, ControllerFamily family) { rr_web_controller_presentation(active, static_cast<int>(family)); }
void WebUiBridge::setControllerFocusVisible(bool visible) { rr_web_controller_focus(visible); }
void WebUiBridge::setControllerResumeBlocked(bool blocked, bool connected) { rr_web_controller_resume(blocked, connected); }
bool WebUiBridge::navigate(UiDirection direction) { return rr_web_ui_action(0, static_cast<int>(direction), nullptr) != 0; }
bool WebUiBridge::activate() { return rr_web_ui_action(1, 0, nullptr) != 0; }
bool WebUiBridge::cancel() { return rr_web_ui_action(2, 0, nullptr) != 0; }
bool WebUiBridge::scroll(double amount) { return rr_web_ui_action(3, amount, nullptr) != 0; }
bool WebUiBridge::modalOpen() const { return rr_web_ui_action(4, 0, nullptr) != 0; }
bool WebUiBridge::openModal(std::string_view id) { const std::string copy(id); return rr_web_ui_action(5, 0, copy.c_str()) != 0; }
void WebUiBridge::closeModal() { rr_web_ui_action(6, 0, nullptr); }
std::string WebUiBridge::focusedId() const { return takeJsString(rr_web_focused_id()); }
void WebUiBridge::preferencesChanged(const AppPreferences& preferences) { rr_web_preferences_changed(preferences.resolutionPreset.c_str(), preferences.debugToolsEnabled); }

} // namespace rocket
