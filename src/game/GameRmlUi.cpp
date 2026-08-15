#include "game/GameRmlUi.h"
#include "game/IRmlRenderHost.h"

#include "core/UiViewportLayout.h"
#include "input/UiFocusNavigation.h"

#include <utility>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/StyleSheetContainer.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/StringUtilities.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace rocket {
namespace {

constexpr int kPhaseBoardFrameWidth = 736;
constexpr int kPhaseContentLaneWidth = 704;
constexpr int kPhaseLaneInset = 16;
constexpr int kPhaseCardSlotWidth = 170;
constexpr int kPhaseCardGap = 8;
constexpr int kPhaseCommonButtonWidth = 132;
constexpr int kPhaseCommonChipSlotWidth = 132;
constexpr int kWorkspaceContentMaxWidth = 1200;
constexpr int kWorkspaceHorizontalPadding = 24;
constexpr int kDroneWorkspaceHorizontalPadding = 16;

IPreferenceStore* g_preferences = nullptr;
IPlatformHost* g_host = nullptr;
IUiBridge* g_uiBridge = nullptr;

AppPreferences currentPreferences()
{
    return g_preferences ? g_preferences->load() : AppPreferences {};
}

void storePreferences(AppPreferences preferences)
{
    if (!g_preferences || !g_uiBridge) return;
    g_preferences->store(preferences);
    g_uiBridge->preferencesChanged(preferences);
}

double rr_rml_now_seconds() { return g_host ? g_host->monotonicSeconds() : 0.0; }
int rr_rml_viewport_width() { return g_host ? g_host->viewportMetrics().logicalWidth : 1280; }
int rr_rml_viewport_height() { return g_host ? g_host->viewportMetrics().logicalHeight : 800; }
int rr_rml_drawing_width() { return g_host ? g_host->viewportMetrics().drawableWidth : 1280; }
int rr_rml_drawing_height() { return g_host ? g_host->viewportMetrics().drawableHeight : 800; }
double rr_rml_density_ratio() { return g_host ? g_host->viewportMetrics().densityRatio : 1.0; }

int rr_rml_resolution_preset()
{
    static constexpr std::string_view options[] = {"auto", "1280x800", "1920x1080", "2560x1440", "3840x2160"};
    const std::string value = currentPreferences().resolutionPreset;
    for (int i = 0; i < 5; ++i) if (value == options[i]) return i;
    return 0;
}

void rr_rml_set_resolution_preset(const char* value)
{
    AppPreferences preferences = currentPreferences();
    preferences.resolutionPreset = value ? value : "auto";
    storePreferences(std::move(preferences));
}

int rr_rml_desktop_fullscreen_available() { return g_host && g_host->fullscreenAvailable() ? 1 : 0; }
int rr_rml_desktop_fullscreen_enabled() { return g_host && g_host->fullscreen() ? 1 : 0; }
void rr_rml_set_desktop_fullscreen(int enabled)
{
    if (!g_host || !g_host->setFullscreen(enabled != 0)) return;
    AppPreferences preferences = currentPreferences();
    preferences.fullscreen = enabled != 0;
    storePreferences(std::move(preferences));
}

int rr_rml_frame_limit_preference()
{
    switch (currentPreferences().frameLimitMode) {
    case FrameLimitMode::Smooth60: return 1;
    case FrameLimitMode::Balanced: return 2;
    case FrameLimitMode::Battery30: return 3;
    case FrameLimitMode::Display: return 4;
    case FrameLimitMode::AutoPower: return 5;
    case FrameLimitMode::PlatformDefault:
    default: return 0;
    }
}

void rr_rml_set_frame_limit_preference(const char* rawValue)
{
    AppPreferences preferences = currentPreferences();
    const std::string_view value = rawValue ? rawValue : "";
    if (value == "smooth60") preferences.frameLimitMode = FrameLimitMode::Smooth60;
    else if (value == "balanced") preferences.frameLimitMode = FrameLimitMode::Balanced;
    else if (value == "battery30") preferences.frameLimitMode = FrameLimitMode::Battery30;
    else if (value == "display") preferences.frameLimitMode = FrameLimitMode::Display;
    else if (value == "auto_power") preferences.frameLimitMode = FrameLimitMode::AutoPower;
    else preferences.frameLimitMode = FrameLimitMode::PlatformDefault;
    storePreferences(std::move(preferences));
}

int rr_rml_keyboard_drill_mode_preference()
{
    return currentPreferences().miningDrillMode == MiningDrillMode::Hold ? 1 : 0;
}

void rr_rml_set_keyboard_drill_mode_preference(const char* rawValue)
{
    AppPreferences preferences = currentPreferences();
    preferences.miningDrillMode = rawValue && std::string_view(rawValue) == "hold"
        ? MiningDrillMode::Hold
        : MiningDrillMode::Toggle;
    storePreferences(std::move(preferences));
}

int rr_rml_controller_prompt_preference()
{
    switch (currentPreferences().controller.promptFamily) {
    case ControllerPromptFamily::Xbox: return 1;
    case ControllerPromptFamily::PlayStation: return 2;
    case ControllerPromptFamily::SteamDeck: return 3;
    case ControllerPromptFamily::Generic: return 4;
    case ControllerPromptFamily::Auto:
    default: return 0;
    }
}
int rr_rml_controller_deadzone_preference()
{
    static constexpr double options[] = {0.10, 0.15, 0.20, 0.25, 0.30, 0.35};
    const double value = currentPreferences().controller.stickDeadzone;
    int closest = 2;
    for (int i = 0; i < 6; ++i) if (std::abs(options[i] - value) < std::abs(options[closest] - value)) closest = i;
    return closest;
}
int rr_rml_controller_boolean_preference(int field)
{
    const ControllerPreferences preferences = currentPreferences().controller;
    if (field == 0) return preferences.invertFlightY ? 1 : 0;
    if (field == 1) return preferences.swapConfirmCancel ? 1 : 0;
    return preferences.vibrationEnabled ? 1 : 0;
}
void rr_rml_set_controller_preference(const char* fieldValue, const char* rawValue)
{
    AppPreferences preferences = currentPreferences();
    const std::string_view field = fieldValue ? fieldValue : "";
    const std::string_view value = rawValue ? rawValue : "";
    if (field == "promptFamily") {
        if (value == "xbox") preferences.controller.promptFamily = ControllerPromptFamily::Xbox;
        else if (value == "playstation") preferences.controller.promptFamily = ControllerPromptFamily::PlayStation;
        else if (value == "steamdeck") preferences.controller.promptFamily = ControllerPromptFamily::SteamDeck;
        else if (value == "generic") preferences.controller.promptFamily = ControllerPromptFamily::Generic;
        else preferences.controller.promptFamily = ControllerPromptFamily::Auto;
    } else if (field == "stickDeadzone") {
        try { preferences.controller.stickDeadzone = std::stod(std::string(value)); } catch (...) {}
    } else if (field == "invertFlightY") preferences.controller.invertFlightY = value == "true";
    else if (field == "swapConfirmCancel") preferences.controller.swapConfirmCancel = value == "true";
    else if (field == "vibrationEnabled") preferences.controller.vibrationEnabled = value == "true";
    storePreferences(std::move(preferences));
}

void rr_rml_set_enabled(int enabled) { if (g_uiBridge) g_uiBridge->setRmlUiEnabled(enabled != 0); }
void rr_rml_set_modal_open(int enabled) { if (g_uiBridge) g_uiBridge->setModalOpen(enabled != 0); }
double rr_rml_game_speed_multiplier() { return currentPreferences().gameSpeed; }
void rr_rml_set_game_speed_multiplier(const char* value)
{
    AppPreferences preferences = currentPreferences();
    try { preferences.gameSpeed = std::clamp(std::stod(value ? value : "1"), 0.25, 8.0); } catch (...) { preferences.gameSpeed = 1.0; }
    storePreferences(std::move(preferences));
}
int rr_rml_debug_tools_enabled() { return currentPreferences().debugToolsEnabled ? 1 : 0; }
void rr_rml_set_debug_tools_enabled(int enabled) { AppPreferences p = currentPreferences(); p.debugToolsEnabled = enabled != 0; storePreferences(std::move(p)); }
int rr_rml_performance_stats_enabled() { return currentPreferences().performanceStatsEnabled ? 1 : 0; }
void rr_rml_set_performance_stats_enabled(int enabled) { AppPreferences p = currentPreferences(); p.performanceStatsEnabled = enabled != 0; storePreferences(std::move(p)); }
int rr_rml_help_disabled() { return currentPreferences().helpDisabled ? 1 : 0; }
void rr_rml_set_help_disabled(int disabled) { AppPreferences p = currentPreferences(); p.helpDisabled = disabled != 0; storePreferences(std::move(p)); }
int rr_rml_camera_shake_disabled() { return currentPreferences().cameraShakeDisabled ? 1 : 0; }
void rr_rml_set_camera_shake_disabled(int disabled) { AppPreferences p = currentPreferences(); p.cameraShakeDisabled = disabled != 0; storePreferences(std::move(p)); }

class RmlSystemInterface final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override
    {
        return rr_rml_now_seconds();
    }

    bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
    {
        if (g_host) {
            g_host->log(
                type == Rml::Log::LT_ERROR || type == Rml::Log::LT_ASSERT
                    ? PlatformLogLevel::Error
                    : (type == Rml::Log::LT_WARNING ? PlatformLogLevel::Warning : PlatformLogLevel::Info),
                std::string("RmlUi: ") + message);
        }
        return true;
    }
};


struct ElementButtonBinding {
    Rml::Element* element = nullptr;
    RmlButtonBinding binding;
};

void replaceAll(std::string& text, std::string_view from, std::string_view to)
{
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
}

std::string removeHiddenElements(std::string html)
{
    std::size_t hiddenSearch = 0;
    while ((hiddenSearch = html.find(" hidden", hiddenSearch)) != std::string::npos) {
        const std::size_t tagStart = html.rfind('<', hiddenSearch);
        const std::size_t tagEnd = html.find('>', hiddenSearch);
        if (tagStart == std::string::npos || tagEnd == std::string::npos || html.compare(tagStart, 2, "</") == 0) {
            hiddenSearch += 7;
            continue;
        }

        const std::size_t tagNameStart = tagStart + 1;
        std::size_t tagNameEnd = tagNameStart;
        while (tagNameEnd < tagEnd && !std::isspace(static_cast<unsigned char>(html[tagNameEnd])) && html[tagNameEnd] != '>') {
            ++tagNameEnd;
        }

        const std::string tagName = html.substr(tagNameStart, tagNameEnd - tagNameStart);
        const std::string closeTag = "</" + tagName + ">";
        const std::size_t closeStart = html.find(closeTag, tagEnd + 1);
        if (closeStart == std::string::npos) {
            html.erase(tagStart, tagEnd - tagStart + 1);
            hiddenSearch = tagStart;
            continue;
        }

        html.erase(tagStart, closeStart + closeTag.size() - tagStart);
        hiddenSearch = tagStart;
    }
    return html;
}

std::string normalizeBooleanAttributes(std::string html)
{
    static constexpr std::string_view names[] = {
        "disabled", "checked", "selected", "data-preflight-launch", "data-arrival-fanfare",
        "data-flyby-run", "data-orbit-run",
        "data-help-settings", "data-help-toggle", "data-camera-shake-settings", "data-camera-shake-toggle", "data-resolution-settings", "data-resolution-select",
        "data-desktop-fullscreen-settings", "data-desktop-fullscreen-toggle",
        "data-frame-limit-settings", "data-frame-limit-select",
        "data-game-speed-settings", "data-game-speed-select",
        "data-keyboard-drill-mode-settings", "data-keyboard-drill-mode-select",
        "data-debug-tools-settings", "data-debug-tools-toggle",
        "data-performance-stats-settings", "data-performance-stats-toggle"
    };

    for (const std::string_view name : names) {
        replaceAll(html, std::string(" ") + std::string(name) + " ", std::string(" ") + std::string(name) + "=\"1\" ");
        replaceAll(html, std::string(" ") + std::string(name) + ">", std::string(" ") + std::string(name) + "=\"1\">");
        replaceAll(html, std::string(" ") + std::string(name) + "/>", std::string(" ") + std::string(name) + "=\"1\"/>");
    }

    replaceAll(html, "<input ", "<input ");
    replaceAll(html, "checked><span>", "checked=\"1\"/><span>");
    replaceAll(html, "checked><", "checked=\"1\"/><");
    return html;
}

std::string sanitizeRml(std::string html)
{
    html = normalizeBooleanAttributes(std::move(html));
    html = removeHiddenElements(std::move(html));

    replaceAll(html, "<section", "<div");
    replaceAll(html, "</section>", "</div>");
    replaceAll(html, "<article", "<div");
    replaceAll(html, "</article>", "</div>");
    replaceAll(html, "<small", "<span");
    replaceAll(html, "</small>", "</span>");
    replaceAll(html, "<ul", "<div");
    replaceAll(html, "</ul>", "</div>");
    replaceAll(html, "<ol", "<div");
    replaceAll(html, "</ol>", "</div>");
    replaceAll(html, "<li", "<p");
    replaceAll(html, "</li>", "</p>");
    replaceAll(html, "<label", "<div");
    replaceAll(html, "</label>", "</div>");
    replaceAll(html, "<input", "<span");
    return html;
}

std::string currentGameSpeedOptionValue()
{
    const double speed = rr_rml_game_speed_multiplier();
    struct Option {
        double numeric;
        const char* value;
    };
    static constexpr Option options[] = {
        {0.5, "0.5"},
        {1.0, "1"},
        {1.5, "1.5"},
        {2.0, "2"},
        {3.0, "3"},
        {5.0, "5"},
        {8.0, "8"},
    };

    for (const Option& option : options) {
        if (std::abs(speed - option.numeric) < 0.01) {
            return option.value;
        }
    }
    return "1";
}

std::string currentResolutionOptionValue()
{
    static constexpr const char* options[] = {
        "auto", "1280x800", "1920x1080", "2560x1440", "3840x2160"
    };
    const int index = std::clamp(rr_rml_resolution_preset(), 0, 4);
    return options[index];
}

std::string currentFrameLimitOptionValue()
{
    static constexpr const char* options[] = {
        "platform_default", "smooth60", "balanced", "battery30", "display", "auto_power"
    };
    return options[std::clamp(rr_rml_frame_limit_preference(), 0, 5)];
}

std::string selectCurrentResolution(std::string html)
{
    if (html.find("data-resolution-select") == std::string::npos) {
        return html;
    }

    const std::string value = currentResolutionOptionValue();
    const std::string needle = "<option value=\"" + value + "\">";
    const std::size_t optionStart = html.find(needle);
    if (optionStart != std::string::npos) {
        html.insert(optionStart + needle.size() - 1, " selected=\"1\"");
    }
    return html;
}

std::string selectCurrentGameSpeed(std::string html)
{
    if (html.find("data-game-speed-select") == std::string::npos) {
        return html;
    }

    const std::string value = currentGameSpeedOptionValue();
    const std::string needle = "<option value=\"" + value + "\">";
    const std::size_t optionStart = html.find(needle);
    if (optionStart != std::string::npos) {
        html.insert(optionStart + needle.size() - 1, " selected=\"1\"");
    }
    return html;
}

std::string selectCurrentFrameLimit(std::string html)
{
    const std::size_t selectStart = html.find("data-frame-limit-select");
    if (selectStart == std::string::npos) {
        return html;
    }

    const std::string value = currentFrameLimitOptionValue();
    const std::string needle = "<option value=\"" + value + "\">";
    const std::size_t optionStart = html.find(needle, selectStart);
    if (optionStart != std::string::npos) {
        html.insert(optionStart + needle.size() - 1, " selected=\"1\"");
    }
    return html;
}

std::string selectCurrentKeyboardDrillMode(std::string html)
{
    const std::size_t selectStart = html.find("data-keyboard-drill-mode-select");
    if (selectStart == std::string::npos) {
        return html;
    }
    const std::string value = rr_rml_keyboard_drill_mode_preference() == 1 ? "hold" : "toggle";
    const std::string needle = "<option value=\"" + value + "\">";
    const std::size_t optionStart = html.find(needle, selectStart);
    if (optionStart != std::string::npos) {
        html.insert(optionStart + needle.size() - 1, " selected=\"1\"");
    }
    return html;
}

std::string selectCurrentControllerPrompt(std::string html)
{
    static constexpr const char* options[] = {"auto", "xbox", "playstation", "steamdeck", "generic"};
    if (html.find("data-controller-prompt-select") == std::string::npos) {
        return html;
    }
    const std::string value = options[std::clamp(rr_rml_controller_prompt_preference(), 0, 4)];
    const std::string needle = "<option value=\"" + value + "\">";
    const std::size_t optionStart = html.find(needle, html.find("data-controller-prompt-select"));
    if (optionStart != std::string::npos) {
        html.insert(optionStart + needle.size() - 1, " selected=\"1\"");
    }
    return html;
}

std::string selectCurrentControllerDeadzone(std::string html)
{
    static constexpr const char* options[] = {"0.10", "0.15", "0.20", "0.25", "0.30", "0.35"};
    if (html.find("data-controller-deadzone-select") == std::string::npos) {
        return html;
    }
    const std::string value = options[std::clamp(rr_rml_controller_deadzone_preference(), 0, 5)];
    const std::string needle = "<option value=\"" + value + "\">";
    const std::size_t optionStart = html.find(needle, html.find("data-controller-deadzone-select"));
    if (optionStart != std::string::npos) {
        html.insert(optionStart + needle.size() - 1, " selected=\"1\"");
    }
    return html;
}

std::string syncControllerToggle(std::string html, std::string_view attribute, bool enabled, std::string_view enabledLabel, std::string_view disabledLabel)
{
    const std::size_t attrStart = html.find(attribute);
    if (attrStart == std::string::npos) {
        return html;
    }
    const std::size_t tagEnd = html.find('>', attrStart);
    const std::size_t closeStart = html.find("</button>", tagEnd == std::string::npos ? attrStart : tagEnd);
    if (tagEnd != std::string::npos && closeStart != std::string::npos) {
        const std::string label = std::string(enabled ? enabledLabel : disabledLabel);
        html.replace(
            tagEnd + 1,
            closeStart - tagEnd - 1,
            "<span class=\"rr-button-label\">" + label + "</span>");
    }
    return html;
}

std::string syncCurrentControllerPreferences(std::string html)
{
    html = selectCurrentControllerDeadzone(selectCurrentControllerPrompt(std::move(html)));
    html = syncControllerToggle(std::move(html), "data-controller-invert-toggle", rr_rml_controller_boolean_preference(0) != 0,
        "Disable inverted Y", "Enable inverted Y");
    html = syncControllerToggle(std::move(html), "data-controller-swap-toggle", rr_rml_controller_boolean_preference(1) != 0,
        "Use standard confirm / cancel", "Swap confirm and cancel");
    return syncControllerToggle(std::move(html), "data-controller-vibration-toggle", rr_rml_controller_boolean_preference(2) != 0,
        "Disable vibration", "Enable vibration");
}

std::string syncCurrentDebugToolsToggle(std::string html)
{
    return syncControllerToggle(
        std::move(html),
        "data-debug-tools-toggle",
        rr_rml_debug_tools_enabled() != 0,
        "Hide debug tools",
        "Show debug tools");
}

std::string syncCurrentPerformanceStatsToggle(std::string html)
{
    return syncControllerToggle(
        std::move(html),
        "data-performance-stats-toggle",
        rr_rml_performance_stats_enabled() != 0,
        "Hide performance stats",
        "Show performance stats");
}

std::string syncCurrentHelpToggle(std::string html)
{
    return syncControllerToggle(
        std::move(html),
        "data-help-toggle",
        rr_rml_help_disabled() == 0,
        "Hide introductions",
        "Show introductions");
}

std::string syncCurrentCameraShakeToggle(std::string html)
{
    return syncControllerToggle(
        std::move(html),
        "data-camera-shake-toggle",
        rr_rml_camera_shake_disabled() != 0,
        "Enable camera shake",
        "Disable camera shake");
}

std::string syncDesktopFullscreenToggle(std::string html)
{
    const std::string_view sectionMarker = "data-desktop-fullscreen-settings";
    const std::size_t marker = html.find(sectionMarker);
    if (marker == std::string::npos) {
        return html;
    }
    if (rr_rml_desktop_fullscreen_available() == 0) {
        const std::size_t sectionStart = html.rfind("<section", marker);
        const std::size_t sectionEnd = html.find("</section>", marker);
        if (sectionStart != std::string::npos && sectionEnd != std::string::npos) {
            html.erase(sectionStart, sectionEnd + std::string_view("</section>").size() - sectionStart);
        }
        return html;
    }
    return syncControllerToggle(
        std::move(html),
        "data-desktop-fullscreen-toggle",
        rr_rml_desktop_fullscreen_enabled() != 0,
        "Exit fullscreen",
        "Enter fullscreen");
}

std::string autoPowerStatusText()
{
    if (currentPreferences().frameLimitMode != FrameLimitMode::AutoPower) {
        return "Auto Power is opt-in; unsupported devices remain uncapped.";
    }
    if (!g_host) return "Auto Power status unavailable; no automatic cap.";
    const AutoPowerEnvironment environment = g_host->autoPowerEnvironment();
    if (!environment.eligible) {
        return "Auto Power unavailable on this device; no automatic cap.";
    }
    const double refreshRate = g_host->displayRefreshRateHz();
    if (environment.powerSource == PowerSource::Unknown || refreshRate <= 0.0) {
        return "Auto Power is waiting for stable power and refresh data.";
    }
    const char* source = environment.powerSource == PowerSource::Battery ? "Battery" : "External";
    const double targetFramesPerSecond = environment.powerSource == PowerSource::Battery
        ? refreshRate / 2.0
        : refreshRate;
    return std::string(source) + " · "
        + std::to_string(static_cast<int>(std::lround(refreshRate))) + " Hz → "
        + std::to_string(static_cast<int>(std::lround(targetFramesPerSecond))) + " FPS";
}

std::string syncAutoPowerStatus(std::string html)
{
    const std::size_t marker = html.find("data-frame-limit-status");
    if (marker == std::string::npos) return html;
    const std::size_t tagEnd = html.find('>', marker);
    const std::size_t close = html.find("</p>", tagEnd == std::string::npos ? marker : tagEnd);
    if (tagEnd != std::string::npos && close != std::string::npos) {
        html.replace(tagEnd + 1, close - tagEnd - 1, autoPowerStatusText());
    }
    return html;
}

void refreshAutoPowerStatusElement();

std::string syncSettingsControls(std::string html)
{
    return syncAutoPowerStatus(syncDesktopFullscreenToggle(syncCurrentControllerPreferences(syncCurrentCameraShakeToggle(syncCurrentHelpToggle(
        syncCurrentPerformanceStatsToggle(syncCurrentDebugToolsToggle(
            selectCurrentKeyboardDrillMode(selectCurrentGameSpeed(selectCurrentFrameLimit(selectCurrentResolution(std::move(html))))))))))));
}

std::string collapsedText(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    bool pendingSpace = false;
    for (const char ch : text) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            pendingSpace = !out.empty();
            continue;
        }
        if (pendingSpace && !out.empty()) {
            out.push_back(' ');
        }
        pendingSpace = false;
        out.push_back(ch);
    }
    return out;
}

std::string textFromMarkup(std::string_view markup)
{
    std::string text;
    text.reserve(markup.size());
    bool inTag = false;
    for (const char ch : markup) {
        if (ch == '<') {
            inTag = true;
            text.push_back(' ');
            continue;
        }
        if (ch == '>') {
            inTag = false;
            text.push_back(' ');
            continue;
        }
        if (!inTag) {
            text.push_back(ch);
        }
    }
    return collapsedText(text);
}

RmlButtonBinding buttonBindingFromElement(Rml::Element& element)
{
    RmlButtonBinding binding;
    binding.focusId = element.GetAttribute<Rml::String>("data-ui-focus-id", "");
    binding.label = textFromMarkup(element.GetInnerRML());
    binding.action = element.GetAttribute<Rml::String>("data-rr-action", "");
    binding.modal = element.GetAttribute<Rml::String>("data-ui-modal", "");
    binding.close = element.HasAttribute("data-ui-close-modal");
    binding.helpToggle = element.HasAttribute("data-help-toggle");
    binding.cameraShakeToggle = element.HasAttribute("data-camera-shake-toggle");
    binding.desktopFullscreenToggle = element.HasAttribute("data-desktop-fullscreen-toggle");
    binding.debugToolsToggle = element.HasAttribute("data-debug-tools-toggle");
    binding.performanceStatsToggle = element.HasAttribute("data-performance-stats-toggle");
    if (element.HasAttribute("data-controller-invert-toggle")) {
        binding.controllerSetting = "invertFlightY";
    } else if (element.HasAttribute("data-controller-swap-toggle")) {
        binding.controllerSetting = "swapConfirmCancel";
    } else if (element.HasAttribute("data-controller-vibration-toggle")) {
        binding.controllerSetting = "vibrationEnabled";
    }
    if (binding.focusId.empty()) {
        if (!binding.action.empty()) {
            binding.focusId = "action:" + binding.action;
        } else if (!binding.modal.empty()) {
            binding.focusId = "modal:" + binding.modal;
        } else if (binding.close) {
            binding.focusId = "modal:close";
        } else if (!binding.controllerSetting.empty()) {
            binding.focusId = "setting:" + binding.controllerSetting;
        }
    }
    return binding;
}

RmlPanelMode panelModeForPresentation(const PanelDocumentPresentation& presentation)
{
    if (presentation.runtime.titleScreen) {
        return RmlPanelMode::Title;
    }
    switch (presentation.templateKind) {
    case PanelTemplateKind::Mining:
        return RmlPanelMode::MiningFullscreen;
    case PanelTemplateKind::Takeover:
        return RmlPanelMode::StoryBriefing;
    case PanelTemplateKind::Results:
        return RmlPanelMode::Results;
    case PanelTemplateKind::Workspace:
        return RmlPanelMode::Workspace;
    case PanelTemplateKind::SurfaceMinigame:
        return RmlPanelMode::PhaseBoard;
    case PanelTemplateKind::ControlPanel:
        return RmlPanelMode::Control;
    case PanelTemplateKind::LegacyRaw:
        break;
    }

    if (presentation.metadata.screen == Screen::DroneOps) {
        return RmlPanelMode::DroneWorkspace;
    }
    if (presentation.metadata.screen == Screen::ArrivalFanfare) {
        return RmlPanelMode::ArrivalFanfare;
    }
    if ((presentation.metadata.screen == Screen::Flyby || presentation.metadata.screen == Screen::Orbit)
        && presentation.metadata.interaction == PanelInteractionMode::Takeover) {
        return RmlPanelMode::MissionStamp;
    }
    switch (presentation.metadata.layoutMode) {
    case PanelLayoutMode::Fullscreen:
        return RmlPanelMode::Workspace;
    case PanelLayoutMode::PhaseBoard:
        return RmlPanelMode::PhaseBoard;
    case PanelLayoutMode::ControlPanel:
    default:
        return RmlPanelMode::Control;
    }
}

std::string_view panelTemplateName(PanelTemplateKind kind)
{
    switch (kind) {
    case PanelTemplateKind::Workspace: return "rr-workspace-shell";
    case PanelTemplateKind::ControlPanel: return "rr-control-shell";
    case PanelTemplateKind::SurfaceMinigame: return "rr-surface-minigame-shell";
    case PanelTemplateKind::Mining: return "rr-mining-shell";
    case PanelTemplateKind::Takeover: return "rr-takeover-shell";
    case PanelTemplateKind::Results: return "rr-results-shell";
    case PanelTemplateKind::LegacyRaw:
    default:
        return {};
    }
}

std::string_view panelTemplateContentId(PanelTemplateKind kind)
{
    switch (kind) {
    case PanelTemplateKind::Workspace: return "rr-workspace-content";
    case PanelTemplateKind::ControlPanel: return "rr-control-content";
    case PanelTemplateKind::SurfaceMinigame: return "rr-surface-minigame-content";
    case PanelTemplateKind::Mining: return "rr-mining-content";
    case PanelTemplateKind::Takeover: return "rr-takeover-content";
    case PanelTemplateKind::Results: return "rr-results-content";
    case PanelTemplateKind::LegacyRaw:
    default:
        return "rr-panel";
    }
}

std::string_view panelTemplateShellClass(PanelTemplateKind kind)
{
    switch (kind) {
    case PanelTemplateKind::Workspace: return "rr-shell rr-workspace-shell";
    case PanelTemplateKind::ControlPanel: return "rr-shell rr-control-shell";
    case PanelTemplateKind::SurfaceMinigame: return "rr-shell rr-surface-minigame-shell";
    case PanelTemplateKind::Mining: return "rr-shell rr-mining-shell";
    case PanelTemplateKind::Takeover: return "rr-shell rr-takeover-shell";
    case PanelTemplateKind::Results: return "rr-shell rr-results-shell";
    case PanelTemplateKind::LegacyRaw:
    default:
        return {};
    }
}

std::string_view visualFamilyClass(PanelVisualFamily family)
{
    switch (family) {
    case PanelVisualFamily::Management: return "management-family-panel rr-family-management";
    case PanelVisualFamily::Decision: return "decision-family-panel rr-family-decision";
    case PanelVisualFamily::LiveHud: return "live-hud-family-panel rr-family-live-hud";
    case PanelVisualFamily::SurfaceMinigame: return "rr-family-surface-minigame";
    case PanelVisualFamily::MiningHud: return "rr-family-mining";
    case PanelVisualFamily::Selection: return "selection-family-panel rr-family-selection";
    case PanelVisualFamily::ResultsModal: return "rr-family-results";
    case PanelVisualFamily::Fullscreen:
    default:
        return {};
    }
}

std::string_view screenFamilyClass(Screen screen)
{
    switch (screen) {
    case Screen::Hangar: return "hangar-family-panel rr-family-hangar";
    case Screen::Navigation: return "navigation-family-panel rr-family-navigation";
    case Screen::Research: return "research-family-panel rr-family-research";
    case Screen::DroneOps: return "drone-workspace-screen-panel rr-family-drone-ops";
    case Screen::ArrivalOps: return "arrival-family-panel rr-family-arrival";
    case Screen::Upgrade: return "selection-screen-panel rr-family-selection";
    case Screen::Launch: return "flight-family-panel";
    case Screen::Mining: return "rr-family-mining";
    case Screen::Results: return "rr-family-results";
    default:
        return {};
    }
}

bool samePanelStructure(
    const PanelDocumentPresentation& left,
    const PanelDocumentPresentation& right)
{
    return left.templateKind == right.templateKind
        && left.metadata.screen == right.metadata.screen
        && left.metadata.visualFamily == right.metadata.visualFamily
        && left.metadata.layoutMode == right.metadata.layoutMode
        && left.metadata.surface == right.metadata.surface
        && left.metadata.interaction == right.metadata.interaction
        && left.metadata.overlay == right.metadata.overlay
        && left.metadata.legacyContentOwnsLaneGeometry
            == right.metadata.legacyContentOwnsLaneGeometry
        && left.metadata.variant == right.metadata.variant
        && left.runtime.titleScreen == right.runtime.titleScreen
        && left.runtime.responsiveViewport == right.runtime.responsiveViewport
        && left.runtime.gameplayInputHelper == right.runtime.gameplayInputHelper
        && left.runtime.preflightReady == right.runtime.preflightReady
        && left.runtime.miningEvaActive == right.runtime.miningEvaActive;
}

std::string panelPresentationValidationError(
    const PanelDocumentPresentation& presentation)
{
    std::vector<std::string_view> modalIds;
    modalIds.reserve(presentation.modals.size());
    for (std::size_t index = 0; index < presentation.modals.size(); ++index) {
        const ModalPresentation& modal = presentation.modals[index];
        if (modal.id.empty()) {
            return "modal at index " + std::to_string(index) + " has an empty id";
        }
        if (std::find(modalIds.begin(), modalIds.end(), modal.id) != modalIds.end()) {
            return "duplicate modal id '" + modal.id + "'";
        }
        modalIds.push_back(modal.id);
    }
    return {};
}

bool panelUsesTitle(RmlPanelMode mode)
{
    return mode == RmlPanelMode::Title;
}

bool panelUsesStoryBriefing(RmlPanelMode mode)
{
    return mode == RmlPanelMode::StoryBriefing;
}

bool panelUsesResults(RmlPanelMode mode)
{
    return mode == RmlPanelMode::Results;
}

bool panelUsesDroneWorkspace(RmlPanelMode mode)
{
    return mode == RmlPanelMode::DroneWorkspace;
}

bool panelUsesWorkspace(RmlPanelMode mode)
{
    return mode == RmlPanelMode::Workspace;
}

bool panelUsesPhaseBoard(RmlPanelMode mode)
{
    return mode == RmlPanelMode::PhaseBoard;
}

bool panelUsesMiningFullscreen(RmlPanelMode mode)
{
    return mode == RmlPanelMode::MiningFullscreen;
}

bool panelUsesMissionStamp(RmlPanelMode mode)
{
    return mode == RmlPanelMode::ArrivalFanfare || mode == RmlPanelMode::MissionStamp;
}

bool panelUsesResponsiveViewport(RmlPanelMode mode)
{
    return mode == RmlPanelMode::Control || mode == RmlPanelMode::PhaseBoard;
}

std::string nativeSceneOverlayMarkup(const PanelDocumentPresentation& presentation)
{
    switch (presentation.metadata.overlay) {
    case PanelOverlayKind::PreflightLaunch: {
        const bool ready = presentation.runtime.preflightReady;
        std::string markup = "<button id=\"rr-scene-launch-control\" class=\"native-scene-launch-control rr-text-button\" "
            "data-rr-action=\"start_launch\" data-ui-focus-id=\"action:start_launch\"";
        if (!ready) {
            markup += " disabled=\"1\"";
        }
        markup += ">";
        markup += "<span class=\"rr-button-label\">";
        markup += ready ? "Launch" : "Securing Mining Rig";
        markup += "</span></button>";
        return markup;
    }
    case PanelOverlayKind::TelemetryLegend:
        return R"(<div id="rr-telemetry-chart-legend" class="native-telemetry-chart-legend">
<div class="native-telemetry-legend-chip warn"><span class="swatch"></span><strong>Warning</strong></div>
<div class="native-telemetry-legend-chip heat"><span class="swatch"></span><strong>Heat</strong></div>
<div class="native-telemetry-legend-chip threshold"><span class="swatch"></span><strong>Caution line</strong></div>
</div>)";
    case PanelOverlayKind::SurfaceScanReadout:
        return "<div id=\"rr-scan-scene-readout\"><strong>"
            + Rml::StringUtilities::EncodeRml(
                presentation.runtime.overlayValue.empty() ? "0%" : presentation.runtime.overlayValue)
            + "</strong></div>";
    case PanelOverlayKind::None:
    default:
        return {};
    }
}

Rml::Rectanglei panelBounds(RmlPanelMode mode)
{
    const int viewportWidth = std::max(1, rr_rml_viewport_width());
    const int viewportHeight = std::max(1, rr_rml_viewport_height());
    if (mode == RmlPanelMode::Title || mode == RmlPanelMode::StoryBriefing
        || mode == RmlPanelMode::Results || mode == RmlPanelMode::DroneWorkspace
        || mode == RmlPanelMode::Workspace
        || mode == RmlPanelMode::MiningFullscreen) {
        return Rml::Rectanglei::FromPositionSize({0, 0}, {viewportWidth, viewportHeight});
    }
    if (mode == RmlPanelMode::ArrivalFanfare) {
        const int width = std::clamp(viewportWidth - 48, 320, 520);
        const int height = std::clamp(viewportHeight - 48, 210, 258);
        const int left = std::max(16, (viewportWidth - width) / 2);
        const int top = std::max(16, (viewportHeight - height) / 2 - 24);
        return Rml::Rectanglei::FromPositionSize({left, top}, {width, height});
    }
    if (mode == RmlPanelMode::MissionStamp) {
        const int width = std::clamp(viewportWidth - 48, 320, 560);
        const int height = std::clamp(viewportHeight - 48, 230, 270);
        const int left = std::max(16, (viewportWidth - width) / 2);
        const int top = std::max(16, (viewportHeight - height) / 2 - 24);
        return Rml::Rectanglei::FromPositionSize({left, top}, {width, height});
    }
    const UiViewportLayout layout = resolveUiViewportLayout(
        viewportWidth,
        viewportHeight,
        UiSurfaceKind::PersistentPanel);
    return Rml::Rectanglei::FromPositionSize(
        {layout.panelRect.x, layout.panelRect.y},
        {std::max(1, layout.panelRect.width), std::max(1, layout.panelRect.height)});
}

Rml::Rectanglei expandedPanelClip(RmlPanelMode mode)
{
    const Rml::Rectanglei bounds = panelBounds(mode);
    if (mode == RmlPanelMode::Title || mode == RmlPanelMode::StoryBriefing
        || mode == RmlPanelMode::Results || mode == RmlPanelMode::DroneWorkspace
        || mode == RmlPanelMode::Workspace
        || mode == RmlPanelMode::MiningFullscreen) {
        return bounds;
    }
    if (panelUsesResponsiveViewport(mode)) {
        // Persistent UI must not capture or render into the protected scene.
        // Shadows and scrollbars stay inside the border-box instead of using
        // the legacy 40 px expansion.
        return bounds;
    }
    return Rml::Rectanglei::FromPositionSize(
        {std::max(0, bounds.Left() - 4), std::max(0, bounds.Top() - 4)},
        {bounds.Width() + 40, bounds.Height() + 40});
}

const ModalPresentation* findModal(const std::vector<ModalPresentation>& modals, std::string_view id)
{
    const auto it = std::find_if(modals.begin(), modals.end(), [id](const ModalPresentation& modal) {
        return modal.id == id;
    });
    return it == modals.end() ? nullptr : &*it;
}

bool panelUsesBottomDockLayout(RmlPanelMode mode)
{
    if (!panelUsesResponsiveViewport(mode)) {
        return false;
    }
    const UiViewportLayout layout = resolveUiViewportLayout(
        std::max(1, rr_rml_viewport_width()),
        std::max(1, rr_rml_viewport_height()),
        UiSurfaceKind::PersistentPanel);
    return layout.layoutClass == UiLayoutClass::BottomDock;
}

bool applyPanelRcssProperties(Rml::Element& element, RmlPanelMode mode)
{
    const Rml::Rectanglei bounds = panelBounds(mode);
    const int viewportWidth = std::max(1, rr_rml_viewport_width());
    const int viewportHeight = std::max(1, rr_rml_viewport_height());
    const int panelWidth = bounds.Width();
    const int panelHeight = std::max(180, bounds.Height());
    const bool responsiveViewport = panelUsesResponsiveViewport(mode);
    const UiViewportLayout responsiveLayout = resolveUiViewportLayout(
        viewportWidth,
        viewportHeight,
        UiSurfaceKind::PersistentPanel);
    const bool bottomDock = panelUsesBottomDockLayout(mode);
    const UiRect sceneRect = responsiveViewport
        ? responsiveLayout.sceneRect
        : UiRect {0, 0, viewportWidth, viewportHeight};
    const UiRect hudSafeRect = responsiveViewport
        ? responsiveLayout.hudSafeRect
        : sceneRect;
    // Persistent rails use a 14 px inset and 1 px border. Full-screen
    // workspaces instead have 24 px side padding and cap their centered work
    // lane at 1200 px. Resolve against the lane that actually owns the
    // descendants; using the outer viewport width here pushes fixed Surface
    // Ops summaries beyond the capped workspace and strands their buttons
    // outside the clipped right edge on wide, short displays.
    const int responsiveContentWidth = panelUsesWorkspace(mode)
        ? std::max(1, std::min(
            kWorkspaceContentMaxWidth,
            panelWidth - kWorkspaceHorizontalPadding * 2))
        : std::max(1, panelWidth - 30);
    const int centeredWorkspaceOffset = std::max(
        0,
        (panelWidth - kWorkspaceHorizontalPadding * 2 - kWorkspaceContentMaxWidth) / 2);
    const bool compactResponsivePanel = responsiveContentWidth < kPhaseContentLaneWidth;
    const int responsiveMetricWidth = std::min(
        150,
        std::max(72, (responsiveContentWidth - kPhaseCardGap) / 2));
    const int responsiveCardWidth = compactResponsivePanel
        ? responsiveContentWidth
        : std::min(292, std::max(170, (responsiveContentWidth - kPhaseCardGap) / 2));
    const int responsiveToolbarButtonWidth = std::min(
        132,
        std::max(104, (responsiveContentWidth - kPhaseCardGap) / 2));
    const int responsiveHeaderButtonWidth = bottomDock
        ? 96
        : std::max(56, (responsiveContentWidth - kPhaseCardGap) / 3);
    const int responsivePanelTitleWidth = bottomDock
        ? std::min(220, responsiveContentWidth / 3)
        : responsiveContentWidth;
    const int responsivePanelHeaderActionsWidth = bottomDock
        ? std::min(316, responsiveContentWidth - responsivePanelTitleWidth - kPhaseCardGap)
        : responsiveContentWidth;
    const int responsiveSurfaceButtonWidth = std::min(
        104,
        std::max(84, responsiveContentWidth / 3));
    const int responsiveSurfaceSummaryWidth = std::max(
        80,
        responsiveContentWidth - responsiveSurfaceButtonWidth - 28);
    const int responsivePushMetricsWidth = std::clamp(
        responsiveContentWidth * 44 / 100,
        128,
        196);
    const int responsivePushRewardsWidth = std::max(
        96,
        responsiveContentWidth - responsivePushMetricsWidth - 10);
    const int responsivePushMetricWidth = std::max(
        52,
        (responsivePushMetricsWidth - 8) / 2);
    // Drone Ops owns the full viewport, but its persistent controls must still
    // fit the narrowest supported management viewport. Keep these lanes
    // resolved here so native RmlUi receives the same concrete geometry every
    // frame instead of relying on a browser-only measurement heuristic.
    // The RmlUi buttons retain an 8 px leading margin, including the first
    // control in a row. Account for all three margins here; the previous
    // two-gap width underreported each action lane and clipped its last
    // button on the browser-backed native canvas.
    const int droneWorkspaceInnerWidth = std::max(
        1,
        panelWidth - kDroneWorkspaceHorizontalPadding * 2);
    const int droneHeaderButtonWidth = std::clamp(droneWorkspaceInnerWidth / 12, 72, 104);
    const int droneHeaderActionsWidth = droneHeaderButtonWidth * 3 + 24;
    const int droneSecondaryActionWidth = std::clamp(droneWorkspaceInnerWidth / 13, 76, 104);
    const int droneDoneActionWidth = std::clamp(droneWorkspaceInnerWidth / 5, 150, 188);
    const int droneWorkspaceActionsWidth = droneSecondaryActionWidth * 2 + droneDoneActionWidth + 24;
    // The loadout is the decision surface, not a narrow sidebar. Give it two
    // fifths of compact workspaces while retaining a useful roster lane, then
    // cap it so wide displays still devote most of their area to drone cards.
    const int droneLoadoutBenchWidth = std::clamp(droneWorkspaceInnerWidth * 2 / 5, 360, 420);
    // A 1080p display commonly leaves the canvas roughly 900 px tall once
    // window chrome is accounted for. Use one compact vertical rhythm for that
    // tier instead of forcing independent scrollbars into both management
    // columns. The 2K workspace keeps its existing, more relaxed density.
    const bool compactDroneWorkspaceVertical = viewportHeight <= 1080;
    const int droneWorkspaceVerticalPadding = compactDroneWorkspaceVertical ? 8 : 14;
    const int droneHeaderHeight = compactDroneWorkspaceVertical ? 50 : 58;
    const int droneHeaderGap = compactDroneWorkspaceVertical ? 6 : 10;
    const int droneHeaderBottomPadding = compactDroneWorkspaceVertical ? 6 : 10;
    const int droneHeaderButtonHeight = compactDroneWorkspaceVertical ? 36 : 40;
    const int droneToolbarHeight = compactDroneWorkspaceVertical ? 60 : 72;
    const int droneToolbarGap = compactDroneWorkspaceVertical ? 6 : 10;
    const int droneToolbarVerticalPadding = compactDroneWorkspaceVertical ? 6 : 8;
    const int droneToolbarButtonHeight = compactDroneWorkspaceVertical ? 40 : 46;
    const int droneMissionStripHeight = compactDroneWorkspaceVertical ? 58 : 66;
    const int droneMissionStripGap = compactDroneWorkspaceVertical ? 6 : 10;
    const int droneTopRowHeight = compactDroneWorkspaceVertical ? 72 : 92;
    const int droneTopRowGap = compactDroneWorkspaceVertical ? 6 : 10;
    const int droneBayVerticalPadding = compactDroneWorkspaceVertical ? 7 : 10;
    const int droneBayChipHeight = compactDroneWorkspaceVertical ? 40 : 48;
    const int droneBayButtonHeight = compactDroneWorkspaceVertical ? 40 : 44;
    const int droneSectionHeadingHeight = compactDroneWorkspaceVertical ? 38 : 45;
    const int droneSectionHeadingGap = compactDroneWorkspaceVertical ? 6 : 10;
    constexpr int kDroneWorkspaceMainGap = 8;
    constexpr int kDroneRosterPanelChrome = 26;
    constexpr int kDroneControlColumns = 3;
    constexpr int kDroneControlCardHorizontalGap = 12;
    constexpr int kDroneControlCardMinWidth = 250;
    const int droneControlCardVerticalGap = compactDroneWorkspaceVertical ? 8 : 12;
    // Card capability chips now live in the per-drone Details modal. Keep the
    // roster cards deliberately compact and equal-height at every resolution.
    const int droneControlCardHeight = compactDroneWorkspaceVertical ? 224 : 240;
    const int droneControlCardPadding = compactDroneWorkspaceVertical ? 8 : 10;
    const int droneCardFooterButtonHeight = compactDroneWorkspaceVertical ? 36 : 40;
    const int droneLoadoutSlotMinHeight = compactDroneWorkspaceVertical ? 102 : 112;
    const std::string droneLoadoutSlotHeight = compactDroneWorkspaceVertical ? "102px" : "auto";
    const std::string droneLoadoutSlotMaxHeight = compactDroneWorkspaceVertical ? "102px" : "none";
    const int droneLoadoutSlotGap = compactDroneWorkspaceVertical ? 6 : 8;
    const int droneLoadoutSlotVerticalPadding = compactDroneWorkspaceVertical ? 5 : 9;
    const int droneLoadoutButtonHeight = compactDroneWorkspaceVertical ? 28 : 36;
    const int droneLoadoutStatMarginTop = compactDroneWorkspaceVertical ? 2 : 7;
    const int droneLoadoutChipMinHeight = compactDroneWorkspaceVertical ? 20 : 25;
    const int droneLoadoutChipVerticalPadding = compactDroneWorkspaceVertical ? 2 : 4;
    // The six-slot bench is always a visual 2 x 3 grid. A tall desktop has
    // the vertical room, but keeping a one-column rail there still hides the
    // lower slots and disagrees with the controller's visual navigation.
    const int droneLoadoutColumnGap = compactDroneWorkspaceVertical ? 6 : 8;
    const int droneLoadoutSlotWidth = std::max(
        1,
        (droneLoadoutBenchWidth - 24 - droneLoadoutColumnGap) / 2);
    const int refitChoiceCardHeight = std::clamp(viewportHeight - 240, 210, 360);
    const int arrivalChoiceCardHeight = std::clamp(viewportHeight - 220, 190, 280);
    // Surface Ops is a top-to-bottom decision sequence: the choices follow
    // Drone Ops instead of occupying a detached bottom dock. At compact
    // heights, reserve enough vertical room for every card footer to remain
    // pointer-reachable without changing the relaxed desktop card height.
    const int surfaceOpsChoiceCardHeight = std::clamp(viewportHeight - 434, 156, 244);
    // Full-screen workspaces reserve 16 px above the shared header, a
    // 58 px header, its 12 px gap, and 28 px below the decision stack.
    // Expose the remaining height to reusable fixed-action workspaces rather
    // than embedding viewport math in a screen-specific stylesheet.
    const int workspaceActionStackHeight = std::max(280, panelHeight - 114);
    const int surfaceUpgradeChoiceCardHeight = std::clamp(viewportHeight - 240, 130, 220);
    const int hangarOperationCardHeight = std::clamp(viewportHeight - 350, 130, 210);
    const int droneRosterWidth = std::max(
        1,
        droneWorkspaceInnerWidth - droneLoadoutBenchWidth - kDroneWorkspaceMainGap);
    const int droneRosterContentWidth = std::max(
        1,
        droneRosterWidth - kDroneRosterPanelChrome);
    // Resolve one shared flex basis for all six cards. Three cards plus two
    // space-between gutters fill the roster lane exactly, so the second row
    // cannot grow a lone card to a different size.
    const int droneControlCardWidth = std::max(
        viewportWidth >= 1280 ? kDroneControlCardMinWidth : 1,
        (droneRosterContentWidth - kDroneControlCardHorizontalGap * (kDroneControlColumns - 1))
            / kDroneControlColumns);
    const int modalGutter = 16;
    const int modalDefaultWidth = std::max(1, std::min(640, viewportWidth - modalGutter * 2));
    const int modalInventoryWidth = std::max(1, std::min(760, viewportWidth - modalGutter * 2));
    const int modalMapWidth = std::max(1, std::min(920, viewportWidth - modalGutter * 2));
    const int modalTallHeight = std::max(1, std::min(viewportHeight - modalGutter * 2, (viewportHeight * 88) / 100));
    const int modalTallTop = std::max(modalGutter, (viewportHeight - modalTallHeight) / 2);
    const int modalDefaultLeft = std::max(modalGutter, (viewportWidth - modalDefaultWidth) / 2);
    const int modalInventoryLeft = std::max(modalGutter, (viewportWidth - modalInventoryWidth) / 2);
    const int modalMapLeft = std::max(modalGutter, (viewportWidth - modalMapWidth) / 2);
    const int modalMissionHeight = std::max(1, std::min(300, viewportHeight - modalGutter * 2));
    const int modalMissionTop = std::max(modalGutter, (viewportHeight - modalMissionHeight) / 2);
    const int modalNewGameWidth = std::max(1, std::min(560, viewportWidth - modalGutter * 2));
    const int modalNewGameHeight = std::max(1, std::min(230, viewportHeight - modalGutter * 2));
    const int modalNewGameLeft = std::max(modalGutter, (viewportWidth - modalNewGameWidth) / 2);
    const int modalNewGameTop = std::max(modalGutter, (viewportHeight - modalNewGameHeight) / 2);
    const int modalActivityWidth = std::max(1, std::min(560, viewportWidth - modalGutter * 2));
    // Briefings include a fixed 54 px action footer below their authored
    // content. A 320 px border-box clips that footer after modal padding and
    // the title row are removed, including the very first launch CTA.
    const int modalActivityHeight = std::max(1, std::min(360, viewportHeight - modalGutter * 2));
    const int modalActivityLeft = std::max(modalGutter, (viewportWidth - modalActivityWidth) / 2);
    const int modalActivityTop = std::max(modalGutter, (viewportHeight - modalActivityHeight) / 2);
    // Outcome summaries contain three consequence rows plus a persistent
    // action lane. Size the border-box for that authored content instead of
    // clipping it inside the legacy 280 px content-box.
    const int modalOutcomeHeight = std::max(1, std::min(360, viewportHeight - modalGutter * 2));
    const int modalOutcomeTop = std::max(modalGutter, (viewportHeight - modalOutcomeHeight) / 2);
    const bool modalOutcomeNeedsScroll = modalOutcomeHeight < 320;
    const int panelPromptLeft = bounds.Left() + 4;
    const int panelPromptRight = std::max(4, viewportWidth - (bounds.Left() + bounds.Width()) + 4);
    const int panelPromptBottom = std::max(4, viewportHeight - (bounds.Top() + bounds.Height()) + 4);
    const int resultsCardWidth = std::max(1, std::min(640, viewportWidth - modalGutter * 2));
    const int resultsCardHeight = std::max(1, std::min(220, viewportHeight - modalGutter * 2));
    const int resultsCardLeft = std::max(modalGutter, (viewportWidth - resultsCardWidth) / 2);
    const int resultsCardTop = std::max(modalGutter, (viewportHeight - resultsCardHeight) / 2);
    const int arrivalTitleSize = mode == RmlPanelMode::MissionStamp ? (panelWidth < 420 ? 31 : 40) : (panelWidth < 420 ? 34 : 46);
    const int left = bounds.Left();
    const int top = bounds.Top();
    const UiViewportLayout miningViewportLayout = resolveUiViewportLayout(
        viewportWidth,
        viewportHeight,
        UiSurfaceKind::Mining);
    const UiRect miningSceneRect = miningViewportLayout.sceneRect;
    const UiRect miningBottomRect = miningViewportLayout.panelRect;
    const int miningInset = miningBottomRect.x;
    const int miningRailWidth = std::max(1, miningBottomRect.width);
    const int miningTopHeight = std::max(1, miningSceneRect.y - miningInset * 2);
    const int miningBottomHeight = std::max(1, miningBottomRect.height);
    const int miningBottomTop = miningBottomRect.y;
    const bool compactMining = viewportWidth < 1100;
    const int miningUtilityButtonWidth = compactMining ? 76 : 92;
    const int miningUtilityWidth = miningUtilityButtonWidth * 3 + 8;
    const int miningObjectiveWidth = std::clamp(miningRailWidth * 23 / 100, 220, 300);
    const int miningObjectiveTop = miningInset + 4;
    const int miningTitleWidth = miningObjectiveWidth;
    const int miningVitalsLeft = miningObjectiveWidth + 20;
    const int miningVitalGap = 5;
    const int miningVitalWidth = std::clamp(
        (miningRailWidth - miningObjectiveWidth - miningUtilityWidth - 56 - miningVitalGap * 3) / 4,
        72,
        105);
    const int miningVitalsWidth = miningVitalWidth * 4 + miningVitalGap * 4;
    const int miningPlayfieldTop = miningSceneRect.y;
    const int miningPayloadWidth = std::clamp(miningRailWidth * 40 / 100, 360, 500);
    const int miningActionWidth = compactMining ? 126 : 154;
    const int miningCommandWidth = std::clamp(miningRailWidth * 30 / 100, 300, 380);
    const int miningCommandLeft = std::max(0, miningRailWidth - miningCommandWidth - 10);
    const int miningPrimaryActionWidth = std::min(180, miningRailWidth);
    const int miningPrimaryActionLeft = std::max(0, (miningRailWidth - miningPrimaryActionWidth) / 2);
    const int miningPrimaryActionTop = std::min(44, std::max(8, miningBottomHeight - 48));
    const bool compactTitle = panelWidth < 800 || panelHeight < 680;
    const int titleContentWidth = std::clamp(panelWidth - (compactTitle ? 48 : 120), 360, 760);
    const int titleContentLeft = std::max(0, (panelWidth - titleContentWidth) / 2);
    const int titleContentTop = std::max(18, (panelHeight - (compactTitle ? 510 : 590)) / 2);
    const int titleContentPadding = compactTitle ? 22 : 34;
    const int titleInnerWidth = std::max(1, titleContentWidth - titleContentPadding * 2 - 2);
    const int titleLetterWidth = compactTitle ? 52 : 84;
    const int titleLetterSize = compactTitle ? 58 : 94;
    const int titleLockupWidth = titleLetterWidth * 6;
    const int titleLockupLeft = std::max(0, (titleInnerWidth - titleLockupWidth) / 2);
    const int titleMenuWidth = compactTitle ? 300 : 340;
    const int titleMenuLeft = std::max(0, (titleInnerWidth - titleMenuWidth) / 2);
    const int openingControlsWidth = std::clamp(panelWidth * 33 / 100, 420, 480);
    const int openingControlsConnectedWidth = std::clamp(panelWidth * 45 / 100, 560, 660);
    const int openingControlsRight = std::max(24, panelWidth / 40);
    const int openingControlsBottom = std::max(28, panelHeight / 24);
    const int storyContentLeft = std::max(28, panelWidth / 10);
    const int storyIntroductionWidth = std::max(1, std::min(720, panelWidth - openingControlsWidth - openingControlsRight - storyContentLeft - 28));
    const int storyIntroductionConnectedWidth = std::max(1, std::min(720, panelWidth - openingControlsConnectedWidth - openingControlsRight - storyContentLeft - 28));
    // Keep native RmlUi scene controls in the same world-space projection as
    // SceneComposer. Telemetry is authored at x=[0.18, 0.94], y=-0.58.
    const float nativeScenePadding = mode == RmlPanelMode::MiningFullscreen
        ? 1.0F
        : (responsiveLayout.layoutClass == UiLayoutClass::BottomDock ? 0.56F : 0.92F);
    const float nativeSceneUnit = std::max(
        1.0F,
        static_cast<float>(std::min(sceneRect.width, sceneRect.height)) * 0.5F * nativeScenePadding);
    const int nativeSceneCenterX = sceneRect.x + sceneRect.width / 2;
    const int nativeSceneCenterY = sceneRect.y + sceneRect.height / 2;
    const int launchAnchorX = nativeSceneCenterX - static_cast<int>(0.18F * nativeSceneUnit);
    const int launchAnchorY = nativeSceneCenterY + static_cast<int>(0.50F * nativeSceneUnit);
    const int nativeLaunchWidth = std::min(196, std::max(1, hudSafeRect.width));
    const int nativeLaunchHeight = std::min(62, std::max(1, hudSafeRect.height));
    const int nativeLaunchLeft = std::clamp(
        launchAnchorX - nativeLaunchWidth / 2,
        hudSafeRect.x,
        std::max(hudSafeRect.x, uiRectRight(hudSafeRect) - nativeLaunchWidth));
    const int nativeLaunchTop = std::clamp(
        launchAnchorY - nativeLaunchHeight / 2,
        hudSafeRect.y,
        std::max(hudSafeRect.y, uiRectBottom(hudSafeRect) - nativeLaunchHeight));

    const int telemetryAnchorLeft = nativeSceneCenterX + static_cast<int>(0.18F * nativeSceneUnit);
    const int telemetryAnchorRight = nativeSceneCenterX + static_cast<int>(0.94F * nativeSceneUnit);
    const int telemetryAnchorTop = nativeSceneCenterY + static_cast<int>(0.58F * nativeSceneUnit) - 34;
    const int nativeTelemetryWidth = std::min(
        std::max(1, telemetryAnchorRight - telemetryAnchorLeft),
        std::max(1, hudSafeRect.width));
    const int nativeTelemetryHeight = std::min(24, std::max(1, hudSafeRect.height));
    const int nativeTelemetryLeft = std::clamp(
        telemetryAnchorLeft,
        hudSafeRect.x,
        std::max(hudSafeRect.x, uiRectRight(hudSafeRect) - nativeTelemetryWidth));
    const int nativeLegendTop = std::clamp(
        telemetryAnchorTop,
        hudSafeRect.y,
        std::max(hudSafeRect.y, uiRectBottom(hudSafeRect) - nativeTelemetryHeight));

    bool applied = true;
    applied = element.SetProperty(
                  "--rr-workspace-action-stack-height",
                  std::to_string(workspaceActionStackHeight) + "px")
        && applied;
    // --rr-legacy-layout-001: std::to_string(viewportWidth)px
    applied = element.SetProperty("--rr-legacy-layout-001", std::to_string(viewportWidth) + "px") && applied;
    // --rr-legacy-layout-002: std::to_string(viewportHeight)px
    applied = element.SetProperty("--rr-legacy-layout-002", std::to_string(viewportHeight) + "px") && applied;
    // --rr-legacy-layout-003: std::to_string(left)px
    applied = element.SetProperty("--rr-legacy-layout-003", std::to_string(left) + "px") && applied;
    // --rr-legacy-layout-004: std::to_string(top)px
    applied = element.SetProperty("--rr-legacy-layout-004", std::to_string(top) + "px") && applied;
    // --rr-legacy-layout-005: std::to_string(panelWidth)px
    applied = element.SetProperty("--rr-legacy-layout-005", std::to_string(panelWidth) + "px") && applied;
    // --rr-legacy-layout-006: std::to_string(panelHeight)px
    applied = element.SetProperty("--rr-legacy-layout-006", std::to_string(panelHeight) + "px") && applied;
    // --rr-legacy-layout-007: std::to_string(nativeLaunchLeft)px
    applied = element.SetProperty("--rr-legacy-layout-007", std::to_string(nativeLaunchLeft) + "px") && applied;
    // --rr-legacy-layout-008: std::to_string(nativeLaunchTop)px
    applied = element.SetProperty("--rr-legacy-layout-008", std::to_string(nativeLaunchTop) + "px") && applied;
    // --rr-legacy-layout-009: std::to_string(nativeLaunchWidth)px
    applied = element.SetProperty("--rr-legacy-layout-009", std::to_string(nativeLaunchWidth) + "px") && applied;
    // --rr-legacy-layout-010: std::to_string(nativeLaunchHeight)px
    applied = element.SetProperty("--rr-legacy-layout-010", std::to_string(nativeLaunchHeight) + "px") && applied;
    // --rr-legacy-layout-011: std::to_string(nativeTelemetryLeft)px
    applied = element.SetProperty("--rr-legacy-layout-011", std::to_string(nativeTelemetryLeft) + "px") && applied;
    // --rr-legacy-layout-012: std::to_string(nativeLegendTop)px
    applied = element.SetProperty("--rr-legacy-layout-012", std::to_string(nativeLegendTop) + "px") && applied;
    // --rr-legacy-layout-013: std::to_string(nativeTelemetryWidth)px
    applied = element.SetProperty("--rr-legacy-layout-013", std::to_string(nativeTelemetryWidth) + "px") && applied;
    // --rr-legacy-layout-014: std::to_string(nativeTelemetryHeight)px
    applied = element.SetProperty("--rr-legacy-layout-014", std::to_string(nativeTelemetryHeight) + "px") && applied;
    // --rr-legacy-layout-015: std::to_string(std::max(28, panelWidth / 10))px
    applied = element.SetProperty("--rr-legacy-layout-015", std::to_string(std::max(28, panelWidth / 10)) + "px") && applied;
    // --rr-legacy-layout-016: std::to_string(std::max(34, panelHeight / 10))px
    applied = element.SetProperty("--rr-legacy-layout-016", std::to_string(std::max(34, panelHeight / 10)) + "px") && applied;
    // --rr-legacy-layout-017: std::to_string(std::max(640, panelWidth - std::max(56, panelWidth / 5)))px
    applied = element.SetProperty("--rr-legacy-layout-017", std::to_string(std::max(640, panelWidth - std::max(56, panelWidth / 5))) + "px") && applied;
    // --rr-legacy-layout-018: std::to_string(storyContentLeft)px
    applied = element.SetProperty("--rr-legacy-layout-018", std::to_string(storyContentLeft) + "px") && applied;
    // --rr-legacy-layout-019: std::to_string(std::max(34, panelHeight / 12))px
    applied = element.SetProperty("--rr-legacy-layout-019", std::to_string(std::max(34, panelHeight / 12)) + "px") && applied;
    // --rr-legacy-layout-020: std::to_string(storyIntroductionWidth)px
    applied = element.SetProperty("--rr-legacy-layout-020", std::to_string(storyIntroductionWidth) + "px") && applied;
    // --rr-legacy-layout-021: std::to_string(storyIntroductionConnectedWidth)px
    applied = element.SetProperty("--rr-legacy-layout-021", std::to_string(storyIntroductionConnectedWidth) + "px") && applied;
    // --rr-legacy-layout-022: std::to_string(openingControlsRight)px
    applied = element.SetProperty("--rr-legacy-layout-022", std::to_string(openingControlsRight) + "px") && applied;
    // --rr-legacy-layout-023: std::to_string(openingControlsBottom)px
    applied = element.SetProperty("--rr-legacy-layout-023", std::to_string(openingControlsBottom) + "px") && applied;
    // --rr-legacy-layout-024: std::to_string(openingControlsWidth)px
    applied = element.SetProperty("--rr-legacy-layout-024", std::to_string(openingControlsWidth) + "px") && applied;
    // --rr-legacy-layout-025: std::to_string(openingControlsConnectedWidth)px
    applied = element.SetProperty("--rr-legacy-layout-025", std::to_string(openingControlsConnectedWidth) + "px") && applied;
    // --rr-legacy-layout-026: std::to_string(titleContentLeft)px
    applied = element.SetProperty("--rr-legacy-layout-026", std::to_string(titleContentLeft) + "px") && applied;
    // --rr-legacy-layout-027: std::to_string(titleContentTop)px
    applied = element.SetProperty("--rr-legacy-layout-027", std::to_string(titleContentTop) + "px") && applied;
    // --rr-legacy-layout-028: std::to_string(titleContentWidth)px
    applied = element.SetProperty("--rr-legacy-layout-028", std::to_string(titleContentWidth) + "px") && applied;
    // --rr-legacy-layout-029: std::string(compactTitle ? "520" : "610")px
    applied = element.SetProperty("--rr-legacy-layout-029", std::string(compactTitle ? "520" : "610") + "px") && applied;
    // --rr-legacy-layout-030: std::string(compactTitle ? "18px 22px" : "26px 34px")
    applied = element.SetProperty("--rr-legacy-layout-030", std::string(compactTitle ? "18px 22px" : "26px 34px")) && applied;
    // --rr-legacy-layout-031: std::string(compactTitle ? "10" : "12")px
    applied = element.SetProperty("--rr-legacy-layout-031", std::string(compactTitle ? "10" : "12") + "px") && applied;
    // --rr-legacy-layout-032: std::to_string(titleLockupLeft)px
    applied = element.SetProperty("--rr-legacy-layout-032", std::to_string(titleLockupLeft) + "px") && applied;
    // --rr-legacy-layout-033: std::to_string(titleLockupWidth)px
    applied = element.SetProperty("--rr-legacy-layout-033", std::to_string(titleLockupWidth) + "px") && applied;
    // --rr-legacy-layout-034: std::string(compactTitle ? "78" : "118")px
    applied = element.SetProperty("--rr-legacy-layout-034", std::string(compactTitle ? "78" : "118") + "px") && applied;
    // --rr-legacy-layout-035: std::string(compactTitle ? "12" : "18")px
    applied = element.SetProperty("--rr-legacy-layout-035", std::string(compactTitle ? "12" : "18") + "px") && applied;
    // --rr-legacy-layout-036: std::string(compactTitle ? "2" : "4")px
    applied = element.SetProperty("--rr-legacy-layout-036", std::string(compactTitle ? "2" : "4") + "px") && applied;
    // --rr-legacy-layout-037: std::to_string(titleLetterWidth)px
    applied = element.SetProperty("--rr-legacy-layout-037", std::to_string(titleLetterWidth) + "px") && applied;
    // --rr-legacy-layout-038: std::to_string(titleLetterSize)px
    applied = element.SetProperty("--rr-legacy-layout-038", std::to_string(titleLetterSize) + "px") && applied;
    // --rr-legacy-layout-039: std::string(compactTitle ? "14" : "17")px
    applied = element.SetProperty("--rr-legacy-layout-039", std::string(compactTitle ? "14" : "17") + "px") && applied;
    // --rr-legacy-layout-040: std::string(compactTitle ? "12" : "16")px
    applied = element.SetProperty("--rr-legacy-layout-040", std::string(compactTitle ? "12" : "16") + "px") && applied;
    // --rr-legacy-layout-041: std::string(compactTitle ? "10" : "14")px
    applied = element.SetProperty("--rr-legacy-layout-041", std::string(compactTitle ? "10" : "14") + "px") && applied;
    // --rr-legacy-layout-042: std::to_string(std::max(22, (titleInnerWidth - 250) / 2))px
    applied = element.SetProperty("--rr-legacy-layout-042", std::to_string(std::max(22, (titleInnerWidth - 250) / 2)) + "px") && applied;
    // --rr-legacy-layout-043: std::to_string(titleMenuLeft)px
    applied = element.SetProperty("--rr-legacy-layout-043", std::to_string(titleMenuLeft) + "px") && applied;
    // --rr-legacy-layout-044: std::to_string(titleMenuWidth)px
    applied = element.SetProperty("--rr-legacy-layout-044", std::to_string(titleMenuWidth) + "px") && applied;
    // --rr-legacy-layout-045: std::string(compactTitle ? "42" : "48")px
    applied = element.SetProperty("--rr-legacy-layout-045", std::string(compactTitle ? "42" : "48") + "px") && applied;
    // --rr-legacy-layout-046: std::string(compactTitle ? "15" : "17")px
    applied = element.SetProperty("--rr-legacy-layout-046", std::string(compactTitle ? "15" : "17") + "px") && applied;
    // --rr-legacy-layout-047: std::string(compactTitle ? "10" : "15")px
    applied = element.SetProperty("--rr-legacy-layout-047", std::string(compactTitle ? "10" : "15") + "px") && applied;
    // --rr-legacy-layout-048: std::to_string(panelHeight / 3)px
    applied = element.SetProperty("--rr-legacy-layout-048", std::to_string(panelHeight / 3) + "px") && applied;
    // --rr-legacy-layout-049: std::to_string(modalNewGameLeft)px
    applied = element.SetProperty("--rr-legacy-layout-049", std::to_string(modalNewGameLeft) + "px") && applied;
    // --rr-legacy-layout-050: std::to_string(modalNewGameTop)px
    applied = element.SetProperty("--rr-legacy-layout-050", std::to_string(modalNewGameTop) + "px") && applied;
    // --rr-legacy-layout-051: std::to_string(modalNewGameWidth)px
    applied = element.SetProperty("--rr-legacy-layout-051", std::to_string(modalNewGameWidth) + "px") && applied;
    // --rr-legacy-layout-052: std::to_string(modalNewGameHeight)px
    applied = element.SetProperty("--rr-legacy-layout-052", std::to_string(modalNewGameHeight) + "px") && applied;
    // --rr-legacy-layout-053: std::to_string(arrivalTitleSize)px
    applied = element.SetProperty("--rr-legacy-layout-053", std::to_string(arrivalTitleSize) + "px") && applied;
    // --rr-legacy-layout-054: std::to_string(miningInset)px
    applied = element.SetProperty("--rr-legacy-layout-054", std::to_string(miningInset) + "px") && applied;
    // --rr-legacy-layout-055: std::to_string(miningRailWidth)px
    applied = element.SetProperty("--rr-legacy-layout-055", std::to_string(miningRailWidth) + "px") && applied;
    // --rr-legacy-layout-056: std::to_string(miningTopHeight)px
    applied = element.SetProperty("--rr-legacy-layout-056", std::to_string(miningTopHeight) + "px") && applied;
    // --rr-legacy-layout-057: std::to_string(miningSceneRect.x + std::max(0, miningSceneRect.width / 2 - 190))px
    applied = element.SetProperty("--rr-legacy-layout-057", std::to_string(miningSceneRect.x + std::max(0, miningSceneRect.width / 2 - 190)) + "px") && applied;
    // --rr-legacy-layout-058: std::to_string(miningPlayfieldTop + 8)px
    applied = element.SetProperty("--rr-legacy-layout-058", std::to_string(miningPlayfieldTop + 8) + "px") && applied;
    // --rr-legacy-layout-059: std::to_string(miningVitalsLeft)px
    applied = element.SetProperty("--rr-legacy-layout-059", std::to_string(miningVitalsLeft) + "px") && applied;
    // --rr-legacy-layout-060: std::to_string(miningVitalsWidth)px
    applied = element.SetProperty("--rr-legacy-layout-060", std::to_string(miningVitalsWidth) + "px") && applied;
    // --rr-legacy-layout-061: std::to_string(miningVitalWidth)px
    applied = element.SetProperty("--rr-legacy-layout-061", std::to_string(miningVitalWidth) + "px") && applied;
    // --rr-legacy-layout-062: std::to_string(std::max(44, miningTopHeight - 14))px
    applied = element.SetProperty("--rr-legacy-layout-062", std::to_string(std::max(44, miningTopHeight - 14)) + "px") && applied;
    // --rr-legacy-layout-063: std::to_string(miningVitalGap)px
    applied = element.SetProperty("--rr-legacy-layout-063", std::to_string(miningVitalGap) + "px") && applied;
    // --rr-legacy-layout-064: std::to_string(std::max(1, miningTitleWidth - 4))px
    applied = element.SetProperty("--rr-legacy-layout-064", std::to_string(std::max(1, miningTitleWidth - 4)) + "px") && applied;
    // --rr-legacy-layout-065: std::to_string(std::max(1, miningTitleWidth - 6))px
    applied = element.SetProperty("--rr-legacy-layout-065", std::to_string(std::max(1, miningTitleWidth - 6)) + "px") && applied;
    // --rr-legacy-layout-066: std::to_string((std::max(1, miningTitleWidth - 6) * 10) / 100)px
    applied = element.SetProperty("--rr-legacy-layout-066", std::to_string((std::max(1, miningTitleWidth - 6) * 10) / 100) + "px") && applied;
    // --rr-legacy-layout-067: std::to_string((std::max(1, miningTitleWidth - 6) * 20) / 100)px
    applied = element.SetProperty("--rr-legacy-layout-067", std::to_string((std::max(1, miningTitleWidth - 6) * 20) / 100) + "px") && applied;
    // --rr-legacy-layout-068: std::to_string((std::max(1, miningTitleWidth - 6) * 30) / 100)px
    applied = element.SetProperty("--rr-legacy-layout-068", std::to_string((std::max(1, miningTitleWidth - 6) * 30) / 100) + "px") && applied;
    // --rr-legacy-layout-069: std::to_string((std::max(1, miningTitleWidth - 6) * 40) / 100)px
    applied = element.SetProperty("--rr-legacy-layout-069", std::to_string((std::max(1, miningTitleWidth - 6) * 40) / 100) + "px") && applied;
    // --rr-legacy-layout-070: std::to_string((std::max(1, miningTitleWidth - 6) * 50) / 100)px
    applied = element.SetProperty("--rr-legacy-layout-070", std::to_string((std::max(1, miningTitleWidth - 6) * 50) / 100) + "px") && applied;
    // --rr-legacy-layout-071: std::to_string((std::max(1, miningTitleWidth - 6) * 60) / 100)px
    applied = element.SetProperty("--rr-legacy-layout-071", std::to_string((std::max(1, miningTitleWidth - 6) * 60) / 100) + "px") && applied;
    // --rr-legacy-layout-072: std::to_string((std::max(1, miningTitleWidth - 6) * 70) / 100)px
    applied = element.SetProperty("--rr-legacy-layout-072", std::to_string((std::max(1, miningTitleWidth - 6) * 70) / 100) + "px") && applied;
    // --rr-legacy-layout-073: std::to_string((std::max(1, miningTitleWidth - 6) * 80) / 100)px
    applied = element.SetProperty("--rr-legacy-layout-073", std::to_string((std::max(1, miningTitleWidth - 6) * 80) / 100) + "px") && applied;
    // --rr-legacy-layout-074: std::to_string((std::max(1, miningTitleWidth - 6) * 90) / 100)px
    applied = element.SetProperty("--rr-legacy-layout-074", std::to_string((std::max(1, miningTitleWidth - 6) * 90) / 100) + "px") && applied;
    // --rr-legacy-layout-075: std::to_string(miningUtilityWidth)px
    applied = element.SetProperty("--rr-legacy-layout-075", std::to_string(miningUtilityWidth) + "px") && applied;
    // --rr-legacy-layout-076: std::to_string(miningUtilityButtonWidth)px
    applied = element.SetProperty("--rr-legacy-layout-076", std::to_string(miningUtilityButtonWidth) + "px") && applied;
    // --rr-legacy-layout-077: std::to_string(miningSceneRect.x)px
    applied = element.SetProperty("--rr-legacy-layout-077", std::to_string(miningSceneRect.x) + "px") && applied;
    // --rr-legacy-layout-078: std::to_string(miningPlayfieldTop)px
    applied = element.SetProperty("--rr-legacy-layout-078", std::to_string(miningPlayfieldTop) + "px") && applied;
    // --rr-legacy-layout-079: std::to_string(miningSceneRect.width)px
    applied = element.SetProperty("--rr-legacy-layout-079", std::to_string(miningSceneRect.width) + "px") && applied;
    // --rr-legacy-layout-080: std::to_string(miningSceneRect.height)px
    applied = element.SetProperty("--rr-legacy-layout-080", std::to_string(miningSceneRect.height) + "px") && applied;
    // --rr-legacy-layout-081: std::to_string(miningSceneRect.y)px
    applied = element.SetProperty("--rr-legacy-layout-081", std::to_string(miningSceneRect.y) + "px") && applied;
    // --rr-legacy-layout-082: std::to_string(std::max(8, miningSceneRect.width / 2 - 120))px
    applied = element.SetProperty("--rr-legacy-layout-082", std::to_string(std::max(8, miningSceneRect.width / 2 - 120)) + "px") && applied;
    // --rr-legacy-layout-083: std::to_string(std::max(8, miningSceneRect.height - 30))px
    applied = element.SetProperty("--rr-legacy-layout-083", std::to_string(std::max(8, miningSceneRect.height - 30)) + "px") && applied;
    // --rr-legacy-layout-084: std::to_string(miningBottomTop)px
    applied = element.SetProperty("--rr-legacy-layout-084", std::to_string(miningBottomTop) + "px") && applied;
    // --rr-legacy-layout-085: std::to_string(miningBottomHeight)px
    applied = element.SetProperty("--rr-legacy-layout-085", std::to_string(miningBottomHeight) + "px") && applied;
    // --rr-legacy-layout-086: std::to_string(miningPayloadWidth)px
    applied = element.SetProperty("--rr-legacy-layout-086", std::to_string(miningPayloadWidth) + "px") && applied;
    // --rr-legacy-layout-087: std::to_string(miningCommandLeft)px
    applied = element.SetProperty("--rr-legacy-layout-087", std::to_string(miningCommandLeft) + "px") && applied;
    // --rr-legacy-layout-088: std::to_string(miningCommandWidth)px
    applied = element.SetProperty("--rr-legacy-layout-088", std::to_string(miningCommandWidth) + "px") && applied;
    // --rr-legacy-layout-089: std::to_string(miningActionWidth)px
    applied = element.SetProperty("--rr-legacy-layout-089", std::to_string(miningActionWidth) + "px") && applied;
    // --rr-legacy-layout-090: std::to_string(miningPrimaryActionLeft)px
    applied = element.SetProperty("--rr-legacy-layout-090", std::to_string(miningPrimaryActionLeft) + "px") && applied;
    // --rr-legacy-layout-091: std::to_string(miningPrimaryActionTop)px
    applied = element.SetProperty("--rr-legacy-layout-091", std::to_string(miningPrimaryActionTop) + "px") && applied;
    // --rr-legacy-layout-092: std::to_string(miningPrimaryActionWidth)px
    applied = element.SetProperty("--rr-legacy-layout-092", std::to_string(miningPrimaryActionWidth) + "px") && applied;
    // --rr-legacy-layout-093: std::to_string(std::min(520, miningRailWidth))px
    applied = element.SetProperty("--rr-legacy-layout-093", std::to_string(std::min(520, miningRailWidth)) + "px") && applied;
    // --rr-legacy-layout-094: std::to_string(kPhaseContentLaneWidth)px
    applied = element.SetProperty("--rr-legacy-layout-094", std::to_string(kPhaseContentLaneWidth) + "px") && applied;
    // --rr-legacy-layout-095: std::to_string(kPhaseLaneInset)px
    applied = element.SetProperty("--rr-legacy-layout-095", std::to_string(kPhaseLaneInset) + "px") && applied;
    // --rr-legacy-layout-096: std::to_string(miningInset + 9)px
    applied = element.SetProperty("--rr-legacy-layout-096", std::to_string(miningInset + 9) + "px") && applied;
    // --rr-legacy-layout-097: std::to_string(miningObjectiveTop)px
    applied = element.SetProperty("--rr-legacy-layout-097", std::to_string(miningObjectiveTop) + "px") && applied;
    // --rr-legacy-layout-098: std::to_string(miningObjectiveWidth)px
    applied = element.SetProperty("--rr-legacy-layout-098", std::to_string(miningObjectiveWidth) + "px") && applied;
    // --rr-legacy-layout-099: std::to_string(std::max(1, miningTopHeight - 8))px
    applied = element.SetProperty("--rr-legacy-layout-099", std::to_string(std::max(1, miningTopHeight - 8)) + "px") && applied;
    // --rr-legacy-layout-100: std::to_string(kPhaseCardSlotWidth)px
    applied = element.SetProperty("--rr-legacy-layout-100", std::to_string(kPhaseCardSlotWidth) + "px") && applied;
    // --rr-legacy-layout-101: std::to_string(kPhaseCardGap)px
    applied = element.SetProperty("--rr-legacy-layout-101", std::to_string(kPhaseCardGap) + "px") && applied;
    // --rr-legacy-layout-102: std::to_string(kPhaseCommonButtonWidth)px
    applied = element.SetProperty("--rr-legacy-layout-102", std::to_string(kPhaseCommonButtonWidth) + "px") && applied;
    // --rr-legacy-layout-103: std::to_string(kPhaseCommonChipSlotWidth)px
    applied = element.SetProperty("--rr-legacy-layout-103", std::to_string(kPhaseCommonChipSlotWidth) + "px") && applied;
    // --rr-legacy-layout-104: std::to_string(kPhaseBoardFrameWidth)px
    applied = element.SetProperty("--rr-legacy-layout-104", std::to_string(kPhaseBoardFrameWidth) + "px") && applied;
    // --rr-legacy-layout-105: std::to_string(modalDefaultLeft)px
    applied = element.SetProperty("--rr-legacy-layout-105", std::to_string(modalDefaultLeft) + "px") && applied;
    // --rr-legacy-layout-106: std::to_string(modalTallTop)px
    applied = element.SetProperty("--rr-legacy-layout-106", std::to_string(modalTallTop) + "px") && applied;
    // --rr-legacy-layout-107: std::to_string(modalDefaultWidth)px
    applied = element.SetProperty("--rr-legacy-layout-107", std::to_string(modalDefaultWidth) + "px") && applied;
    // --rr-legacy-layout-108: std::to_string(modalTallHeight)px
    applied = element.SetProperty("--rr-legacy-layout-108", std::to_string(modalTallHeight) + "px") && applied;
    // --rr-legacy-layout-109: std::to_string(modalInventoryLeft)px
    applied = element.SetProperty("--rr-legacy-layout-109", std::to_string(modalInventoryLeft) + "px") && applied;
    // --rr-legacy-layout-110: std::to_string(modalInventoryWidth)px
    applied = element.SetProperty("--rr-legacy-layout-110", std::to_string(modalInventoryWidth) + "px") && applied;
    // --rr-legacy-layout-111: std::to_string(modalMapLeft)px
    applied = element.SetProperty("--rr-legacy-layout-111", std::to_string(modalMapLeft) + "px") && applied;
    // --rr-legacy-layout-112: std::to_string(modalMapWidth)px
    applied = element.SetProperty("--rr-legacy-layout-112", std::to_string(modalMapWidth) + "px") && applied;
    // --rr-legacy-layout-113: std::to_string(modalMissionTop)px
    applied = element.SetProperty("--rr-legacy-layout-113", std::to_string(modalMissionTop) + "px") && applied;
    // --rr-legacy-layout-114: std::to_string(modalMissionHeight)px
    applied = element.SetProperty("--rr-legacy-layout-114", std::to_string(modalMissionHeight) + "px") && applied;
    // --rr-legacy-layout-115: std::to_string(modalOutcomeTop)px
    applied = element.SetProperty("--rr-legacy-layout-115", std::to_string(modalOutcomeTop) + "px") && applied;
    // --rr-legacy-layout-116: std::to_string(modalOutcomeHeight)px
    applied = element.SetProperty("--rr-legacy-layout-116", std::to_string(modalOutcomeHeight) + "px") && applied;
    // --rr-legacy-layout-117: std::string(modalOutcomeNeedsScroll ? "auto" : "hidden")
    applied = element.SetProperty("--rr-legacy-layout-117", std::string(modalOutcomeNeedsScroll ? "auto" : "hidden")) && applied;
    // --rr-legacy-layout-118: std::string(modalOutcomeNeedsScroll ? "8px" : "0px")
    applied = element.SetProperty("--rr-legacy-layout-118", std::string(modalOutcomeNeedsScroll ? "8px" : "0px")) && applied;
    // --rr-legacy-layout-119: std::to_string(modalActivityLeft)px
    applied = element.SetProperty("--rr-legacy-layout-119", std::to_string(modalActivityLeft) + "px") && applied;
    // --rr-legacy-layout-120: std::to_string(modalActivityTop)px
    applied = element.SetProperty("--rr-legacy-layout-120", std::to_string(modalActivityTop) + "px") && applied;
    // --rr-legacy-layout-121: std::to_string(modalActivityWidth)px
    applied = element.SetProperty("--rr-legacy-layout-121", std::to_string(modalActivityWidth) + "px") && applied;
    // --rr-legacy-layout-122: std::to_string(modalActivityHeight)px
    applied = element.SetProperty("--rr-legacy-layout-122", std::to_string(modalActivityHeight) + "px") && applied;
    // --rr-legacy-layout-123: std::to_string(panelPromptLeft)px
    applied = element.SetProperty("--rr-legacy-layout-123", std::to_string(panelPromptLeft) + "px") && applied;
    // --rr-legacy-layout-124: std::to_string(panelPromptRight)px
    applied = element.SetProperty("--rr-legacy-layout-124", std::to_string(panelPromptRight) + "px") && applied;
    // --rr-legacy-layout-125: std::to_string(panelPromptBottom)px
    applied = element.SetProperty("--rr-legacy-layout-125", std::to_string(panelPromptBottom) + "px") && applied;
    // --rr-legacy-layout-126: std::to_string(miningInset + 4)px
    applied = element.SetProperty("--rr-legacy-layout-126", std::to_string(miningInset + 4) + "px") && applied;
    // --rr-legacy-layout-127: std::to_string(resultsCardLeft)px
    applied = element.SetProperty("--rr-legacy-layout-127", std::to_string(resultsCardLeft) + "px") && applied;
    // --rr-legacy-layout-128: std::to_string(resultsCardTop)px
    applied = element.SetProperty("--rr-legacy-layout-128", std::to_string(resultsCardTop) + "px") && applied;
    // --rr-legacy-layout-129: std::to_string(resultsCardWidth)px
    applied = element.SetProperty("--rr-legacy-layout-129", std::to_string(resultsCardWidth) + "px") && applied;
    // --rr-legacy-layout-130: std::to_string(resultsCardHeight)px
    applied = element.SetProperty("--rr-legacy-layout-130", std::to_string(resultsCardHeight) + "px") && applied;
    // --rr-legacy-layout-131: std::to_string(std::max(1, resultsCardWidth - 30))px
    applied = element.SetProperty("--rr-legacy-layout-131", std::to_string(std::max(1, resultsCardWidth - 30)) + "px") && applied;
    // --rr-legacy-layout-132: std::to_string(std::max(1, resultsCardWidth - 60))px
    applied = element.SetProperty("--rr-legacy-layout-132", std::to_string(std::max(1, resultsCardWidth - 60)) + "px") && applied;
    // --rr-legacy-layout-133: std::to_string(responsiveContentWidth)px
    applied = element.SetProperty("--rr-legacy-layout-133", std::to_string(responsiveContentWidth) + "px") && applied;
    // --rr-legacy-layout-134: std::to_string(responsivePushMetricsWidth)px
    applied = element.SetProperty("--rr-legacy-layout-134", std::to_string(responsivePushMetricsWidth) + "px") && applied;
    // --rr-legacy-layout-135: std::to_string(responsivePushMetricWidth)px
    applied = element.SetProperty("--rr-legacy-layout-135", std::to_string(responsivePushMetricWidth) + "px") && applied;
    // --rr-legacy-layout-136: std::to_string(responsivePushRewardsWidth)px
    applied = element.SetProperty("--rr-legacy-layout-136", std::to_string(responsivePushRewardsWidth) + "px") && applied;
    // --rr-legacy-layout-137: std::to_string(responsiveToolbarButtonWidth)px
    applied = element.SetProperty("--rr-legacy-layout-137", std::to_string(responsiveToolbarButtonWidth) + "px") && applied;
    // --rr-legacy-layout-138: std::to_string(responsiveHeaderButtonWidth)px
    applied = element.SetProperty("--rr-legacy-layout-138", std::to_string(responsiveHeaderButtonWidth) + "px") && applied;
    // --rr-legacy-layout-139: std::to_string(responsivePanelTitleWidth)px
    applied = element.SetProperty("--rr-legacy-layout-139", std::to_string(responsivePanelTitleWidth) + "px") && applied;
    // --rr-legacy-layout-140: std::to_string(responsivePanelHeaderActionsWidth)px
    applied = element.SetProperty("--rr-legacy-layout-140", std::to_string(responsivePanelHeaderActionsWidth) + "px") && applied;
    // --rr-legacy-layout-141: std::to_string(responsiveSurfaceSummaryWidth)px
    applied = element.SetProperty("--rr-legacy-layout-141", std::to_string(responsiveSurfaceSummaryWidth) + "px") && applied;
    // --rr-legacy-layout-142: std::to_string(responsiveSurfaceButtonWidth)px
    applied = element.SetProperty("--rr-legacy-layout-142", std::to_string(responsiveSurfaceButtonWidth) + "px") && applied;
    // --rr-legacy-layout-143: std::to_string(responsiveMetricWidth)px
    applied = element.SetProperty("--rr-legacy-layout-143", std::to_string(responsiveMetricWidth) + "px") && applied;
    // --rr-legacy-layout-144: std::to_string(responsiveCardWidth)px
    applied = element.SetProperty("--rr-legacy-layout-144", std::to_string(responsiveCardWidth) + "px") && applied;
    // --rr-legacy-layout-145: std::to_string(nativeSceneCenterX - 34)px
    applied = element.SetProperty("--rr-legacy-layout-145", std::to_string(nativeSceneCenterX - 34) + "px") && applied;
    // --rr-legacy-layout-146: std::to_string(nativeSceneCenterY + 116)px
    applied = element.SetProperty("--rr-legacy-layout-146", std::to_string(nativeSceneCenterY + 116) + "px") && applied;
    // --rr-legacy-layout-147: std::to_string(std::max(150, miningObjectiveWidth - 20))px
    applied = element.SetProperty("--rr-legacy-layout-147", std::to_string(std::max(150, miningObjectiveWidth - 20)) + "px") && applied;
    // --rr-legacy-layout-148: std::to_string(miningPayloadWidth + 24)px
    applied = element.SetProperty("--rr-legacy-layout-148", std::to_string(miningPayloadWidth + 24) + "px") && applied;
    // --rr-legacy-layout-149: std::to_string(std::max(1, miningRailWidth - miningPayloadWidth - 36))px
    applied = element.SetProperty("--rr-legacy-layout-149", std::to_string(std::max(1, miningRailWidth - miningPayloadWidth - 36)) + "px") && applied;
    // --rr-legacy-layout-150: std::to_string(kWorkspaceHorizontalPadding)px
    applied = element.SetProperty("--rr-legacy-layout-150", std::to_string(kWorkspaceHorizontalPadding) + "px") && applied;
    // --rr-legacy-layout-151: std::to_string(kWorkspaceContentMaxWidth)px
    applied = element.SetProperty("--rr-legacy-layout-151", std::to_string(kWorkspaceContentMaxWidth) + "px") && applied;
    applied = element.SetProperty("--rr-workspace-centered-offset", std::to_string(centeredWorkspaceOffset) + "px") && applied;
    // --rr-legacy-layout-152: std::to_string(droneWorkspaceVerticalPadding)px
    applied = element.SetProperty("--rr-legacy-layout-152", std::to_string(droneWorkspaceVerticalPadding) + "px") && applied;
    // --rr-legacy-layout-153: std::to_string(kDroneWorkspaceHorizontalPadding)px
    applied = element.SetProperty("--rr-legacy-layout-153", std::to_string(kDroneWorkspaceHorizontalPadding) + "px") && applied;
    // --rr-legacy-layout-154: std::to_string(droneHeaderHeight)px
    applied = element.SetProperty("--rr-legacy-layout-154", std::to_string(droneHeaderHeight) + "px") && applied;
    // --rr-legacy-layout-155: std::to_string(droneWorkspaceInnerWidth)px
    applied = element.SetProperty("--rr-legacy-layout-155", std::to_string(droneWorkspaceInnerWidth) + "px") && applied;
    // --rr-legacy-layout-156: std::to_string(droneHeaderGap)px
    applied = element.SetProperty("--rr-legacy-layout-156", std::to_string(droneHeaderGap) + "px") && applied;
    // --rr-legacy-layout-157: std::to_string(droneHeaderBottomPadding)px
    applied = element.SetProperty("--rr-legacy-layout-157", std::to_string(droneHeaderBottomPadding) + "px") && applied;
    // --rr-legacy-layout-158: std::to_string(droneHeaderActionsWidth)px
    applied = element.SetProperty("--rr-legacy-layout-158", std::to_string(droneHeaderActionsWidth) + "px") && applied;
    // --rr-legacy-layout-159: std::to_string(droneHeaderButtonWidth)px
    applied = element.SetProperty("--rr-legacy-layout-159", std::to_string(droneHeaderButtonWidth) + "px") && applied;
    // --rr-legacy-layout-160: std::to_string(droneHeaderButtonHeight)px
    applied = element.SetProperty("--rr-legacy-layout-160", std::to_string(droneHeaderButtonHeight) + "px") && applied;
    // --rr-legacy-layout-161: std::to_string(droneToolbarHeight)px
    applied = element.SetProperty("--rr-legacy-layout-161", std::to_string(droneToolbarHeight) + "px") && applied;
    // --rr-legacy-layout-162: std::to_string(droneToolbarGap)px
    applied = element.SetProperty("--rr-legacy-layout-162", std::to_string(droneToolbarGap) + "px") && applied;
    // --rr-legacy-layout-163: std::to_string(droneToolbarVerticalPadding)px
    applied = element.SetProperty("--rr-legacy-layout-163", std::to_string(droneToolbarVerticalPadding) + "px") && applied;
    // --rr-legacy-layout-164: std::to_string(droneWorkspaceActionsWidth)px
    applied = element.SetProperty("--rr-legacy-layout-164", std::to_string(droneWorkspaceActionsWidth) + "px") && applied;
    // --rr-legacy-layout-165: std::to_string(droneToolbarButtonHeight)px
    applied = element.SetProperty("--rr-legacy-layout-165", std::to_string(droneToolbarButtonHeight) + "px") && applied;
    // --rr-legacy-layout-166: std::to_string(droneSecondaryActionWidth)px
    applied = element.SetProperty("--rr-legacy-layout-166", std::to_string(droneSecondaryActionWidth) + "px") && applied;
    // --rr-legacy-layout-167: std::to_string(droneDoneActionWidth)px
    applied = element.SetProperty("--rr-legacy-layout-167", std::to_string(droneDoneActionWidth) + "px") && applied;
    // --rr-legacy-layout-168: std::to_string(droneTopRowHeight)px
    applied = element.SetProperty("--rr-legacy-layout-168", std::to_string(droneTopRowHeight) + "px") && applied;
    // --rr-legacy-layout-169: std::to_string(droneTopRowGap)px
    applied = element.SetProperty("--rr-legacy-layout-169", std::to_string(droneTopRowGap) + "px") && applied;
    // --rr-legacy-layout-170: std::to_string(droneBayVerticalPadding)px
    applied = element.SetProperty("--rr-legacy-layout-170", std::to_string(droneBayVerticalPadding) + "px") && applied;
    // --rr-legacy-layout-171: std::to_string(droneBayChipHeight)px
    applied = element.SetProperty("--rr-legacy-layout-171", std::to_string(droneBayChipHeight) + "px") && applied;
    // --rr-legacy-layout-172: std::to_string(droneBayButtonHeight)px
    applied = element.SetProperty("--rr-legacy-layout-172", std::to_string(droneBayButtonHeight) + "px") && applied;
    // --rr-legacy-layout-173: std::to_string(droneRosterWidth)px
    applied = element.SetProperty("--rr-legacy-layout-173", std::to_string(droneRosterWidth) + "px") && applied;
    // --rr-legacy-layout-174: std::to_string(kDroneWorkspaceMainGap)px
    applied = element.SetProperty("--rr-legacy-layout-174", std::to_string(kDroneWorkspaceMainGap) + "px") && applied;
    // --rr-legacy-layout-175: std::to_string(droneLoadoutBenchWidth)px
    applied = element.SetProperty("--rr-legacy-layout-175", std::to_string(droneLoadoutBenchWidth) + "px") && applied;
    // --rr-legacy-layout-176: std::to_string(droneSectionHeadingHeight)px
    applied = element.SetProperty("--rr-legacy-layout-176", std::to_string(droneSectionHeadingHeight) + "px") && applied;
    // --rr-legacy-layout-177: std::to_string(droneSectionHeadingGap)px
    applied = element.SetProperty("--rr-legacy-layout-177", std::to_string(droneSectionHeadingGap) + "px") && applied;
    // --rr-legacy-layout-178: std::to_string(droneRosterContentWidth)px
    applied = element.SetProperty("--rr-legacy-layout-178", std::to_string(droneRosterContentWidth) + "px") && applied;
    // --rr-legacy-layout-179: std::to_string(droneControlCardWidth)px
    applied = element.SetProperty("--rr-legacy-layout-179", std::to_string(droneControlCardWidth) + "px") && applied;
    // --rr-legacy-layout-180: std::to_string(droneControlCardHeight)px
    applied = element.SetProperty("--rr-legacy-layout-180", std::to_string(droneControlCardHeight) + "px") && applied;
    // --rr-legacy-layout-181: std::to_string(droneControlCardVerticalGap)px
    applied = element.SetProperty("--rr-legacy-layout-181", std::to_string(droneControlCardVerticalGap) + "px") && applied;
    // --rr-legacy-layout-182: std::to_string(droneControlCardPadding)px
    applied = element.SetProperty("--rr-legacy-layout-182", std::to_string(droneControlCardPadding) + "px") && applied;
    // --rr-legacy-layout-183: std::to_string(droneCardFooterButtonHeight)px
    applied = element.SetProperty("--rr-legacy-layout-183", std::to_string(droneCardFooterButtonHeight) + "px") && applied;
    // --rr-legacy-layout-184: std::to_string(droneLoadoutSlotMinHeight)px
    applied = element.SetProperty("--rr-legacy-layout-184", std::to_string(droneLoadoutSlotMinHeight) + "px") && applied;
    // --rr-legacy-layout-185: droneLoadoutSlotHeight
    applied = element.SetProperty("--rr-legacy-layout-185", droneLoadoutSlotHeight) && applied;
    // --rr-legacy-layout-186: droneLoadoutSlotMaxHeight
    applied = element.SetProperty("--rr-legacy-layout-186", droneLoadoutSlotMaxHeight) && applied;
    // --rr-legacy-layout-187: std::to_string(droneLoadoutSlotGap)px
    applied = element.SetProperty("--rr-legacy-layout-187", std::to_string(droneLoadoutSlotGap) + "px") && applied;
    // --rr-legacy-layout-188: std::to_string(droneLoadoutSlotVerticalPadding)px
    applied = element.SetProperty("--rr-legacy-layout-188", std::to_string(droneLoadoutSlotVerticalPadding) + "px") && applied;
    // --rr-legacy-layout-189: std::to_string(droneLoadoutButtonHeight)px
    applied = element.SetProperty("--rr-legacy-layout-189", std::to_string(droneLoadoutButtonHeight) + "px") && applied;
    // --rr-legacy-layout-190: std::to_string(droneLoadoutStatMarginTop)px
    applied = element.SetProperty("--rr-legacy-layout-190", std::to_string(droneLoadoutStatMarginTop) + "px") && applied;
    // --rr-legacy-layout-191: std::to_string(droneLoadoutChipMinHeight)px
    applied = element.SetProperty("--rr-legacy-layout-191", std::to_string(droneLoadoutChipMinHeight) + "px") && applied;
    // --rr-legacy-layout-192: std::to_string(droneLoadoutChipVerticalPadding)px
    applied = element.SetProperty("--rr-legacy-layout-192", std::to_string(droneLoadoutChipVerticalPadding) + "px") && applied;
    // --rr-legacy-layout-193: std::to_string(refitChoiceCardHeight)px
    applied = element.SetProperty("--rr-legacy-layout-193", std::to_string(refitChoiceCardHeight) + "px") && applied;
    // --rr-legacy-layout-194: std::to_string(surfaceUpgradeChoiceCardHeight)px
    applied = element.SetProperty("--rr-legacy-layout-194", std::to_string(surfaceUpgradeChoiceCardHeight) + "px") && applied;
    // --rr-legacy-layout-195: std::to_string(arrivalChoiceCardHeight)px
    applied = element.SetProperty("--rr-legacy-layout-195", std::to_string(arrivalChoiceCardHeight) + "px") && applied;
    // --rr-legacy-layout-196: std::to_string(hangarOperationCardHeight)px
    applied = element.SetProperty("--rr-legacy-layout-196", std::to_string(hangarOperationCardHeight) + "px") && applied;
    // --rr-legacy-layout-197: std::to_string(surfaceOpsChoiceCardHeight)px
    applied = element.SetProperty("--rr-legacy-layout-197", std::to_string(surfaceOpsChoiceCardHeight) + "px") && applied;
    // --rr-legacy-layout-198: std::to_string(droneMissionStripHeight)px
    applied = element.SetProperty("--rr-legacy-layout-198", std::to_string(droneMissionStripHeight) + "px") && applied;
    // --rr-legacy-layout-199: std::to_string(droneMissionStripGap)px
    applied = element.SetProperty("--rr-legacy-layout-199", std::to_string(droneMissionStripGap) + "px") && applied;
    // --rr-legacy-layout-200: std::to_string(droneLoadoutColumnGap)px
    applied = element.SetProperty("--rr-legacy-layout-200", std::to_string(droneLoadoutColumnGap) + "px") && applied;
    // --rr-legacy-layout-201: std::to_string(droneLoadoutSlotWidth)px
    applied = element.SetProperty("--rr-legacy-layout-201", std::to_string(droneLoadoutSlotWidth) + "px") && applied;
    return applied;
}

ControllerFamily promptControllerFamily(ControllerFamily detected)
{
    switch (rr_rml_controller_prompt_preference()) {
    case 1:
        return ControllerFamily::Xbox;
    case 2:
        return ControllerFamily::PlayStation;
    case 3:
        return ControllerFamily::SteamDeck;
    case 4:
        return ControllerFamily::Generic;
    default:
        return detected;
    }
}

struct ControllerPromptLabels {
    const char* south;
    const char* east;
    const char* west;
    const char* north;
    const char* leftBumper;
    const char* rightBumper;
    const char* leftTrigger;
    const char* rightTrigger;
    const char* menu;
    const char* view;
};

ControllerPromptLabels controllerPromptLabels(ControllerFamily family)
{
    switch (family) {
    case ControllerFamily::Xbox:
        return {"A", "B", "X", "Y", "LB", "RB", "LT", "RT", "Menu", "View"};
    case ControllerFamily::PlayStation:
        return {"Cross", "Circle", "Square", "Triangle", "L1", "R1", "L2", "R2", "Options", "Create"};
    case ControllerFamily::SteamDeck:
        return {"A", "B", "X", "Y", "L1", "R1", "L2", "R2", "Menu", "View"};
    case ControllerFamily::Generic:
    default:
        return {"South", "East", "West", "North", "LB", "RB", "LT", "RT", "Menu", "View"};
    }
}

std::string withOpeningControllerLabels(std::string markup, ControllerFamily family)
{
    const ControllerPromptLabels labels = controllerPromptLabels(promptControllerFamily(family));
    const auto replaceToken = [&](std::string_view token, std::string_view value) {
        std::size_t position = 0;
        while ((position = markup.find(token, position)) != std::string::npos) {
            markup.replace(position, token.size(), value);
            position += value.size();
        }
    };
    replaceToken("{{controller_south}}", labels.south);
    replaceToken("{{controller_east}}", labels.east);
    replaceToken("{{controller_west}}", labels.west);
    replaceToken("{{controller_north}}", labels.north);
    replaceToken("{{controller_lb}}", labels.leftBumper);
    replaceToken("{{controller_rb}}", labels.rightBumper);
    replaceToken("{{controller_lt}}", labels.leftTrigger);
    replaceToken("{{controller_rt}}", labels.rightTrigger);
    replaceToken("{{controller_menu}}", labels.menu);
    replaceToken("{{controller_view}}", labels.view);
    return markup;
}

std::string inputPromptBar(
    const PanelDocumentPresentation& presentation,
    ControllerFamily family,
    bool controllerActive,
    bool modalOpen,
    bool modalDismissible)
{
    const ControllerPromptLabels labels = controllerPromptLabels(promptControllerFamily(family));
    const bool swapConfirmCancel = rr_rml_controller_boolean_preference(1) != 0;
    const char* confirm = swapConfirmCancel ? labels.east : labels.south;
    const char* cancel = swapConfirmCancel ? labels.south : labels.east;
    const Screen screen = presentation.metadata.screen;
    const bool mining = screen == Screen::Mining;
    const bool flyby = screen == Screen::Flyby
        && presentation.metadata.interaction == PanelInteractionMode::Realtime;
    const bool orbit = screen == Screen::Orbit
        && presentation.metadata.interaction == PanelInteractionMode::Realtime;
    if (!controllerActive && (modalOpen || (!mining && !flyby && !orbit))) {
        return {};
    }

    std::string prompt = "<div id=\"rr-controller-prompt-bar\"";
    if (mining && !modalOpen) {
        prompt += " class=\"mining-input-helper\"";
    } else if (presentation.runtime.responsiveViewport && !modalOpen) {
        prompt += " class=\"panel-input-helper\"";
    }
    prompt += ">";
    const auto item = [&](const char* button, const char* action) {
        return std::string("<span><strong>") + button + "</strong> " + action + "</span>";
    };
    const auto describedItem = [&](std::string_view action, std::string_view button, std::string_view purpose = {}) {
        std::string result = "<span>" + std::string(action) + " (" + std::string(button) + ")";
        if (!purpose.empty()) {
            result += " - " + std::string(purpose);
        }
        return result + "</span>";
    };

    if (!controllerActive) {
        if (mining) {
            if (presentation.runtime.miningEvaActive) {
                prompt += describedItem("Thrust", "WASD / Arrows")
                    + describedItem("Aim", "Mouse")
                    + describedItem("Fire", "Left click")
                    + describedItem("Drill", "Right click")
                    + describedItem("Scan", "E");
            } else {
                prompt += describedItem("Move", "WASD / Arrows")
                    + describedItem("Drill", "Space / Left click")
                    + describedItem("Scan", "E");
            }
            if (presentation.runtime.miningTetherAvailable) {
                prompt += describedItem("Tether", "T");
            }
            prompt += describedItem(presentation.runtime.miningEvaActive ? "Enter rig" : "Exit rig", "F");
            if (presentation.runtime.miningStowAvailable) {
                prompt += describedItem("Stow / Leave", "R");
            }
            if (presentation.runtime.miningAbortAvailable) {
                prompt += describedItem("Recall", "Esc");
            }
        } else if (flyby) {
            prompt += describedItem("Accelerate / Slow", "W/S or Up/Down")
                + describedItem("Turn", "A/D or Left/Right")
                + describedItem("Abort", "Esc", "Records a Miss");
        } else if (orbit) {
            prompt += describedItem("Prograde / Retrograde", "W/S or Up/Down")
                + describedItem("Tighten / Widen", "A/D or Left/Right")
                + describedItem("Abort", "Esc", "Records a Miss");
        }
        return prompt + "</div>";
    }

    if (modalOpen) {
        prompt += item("L-stick / D-pad", "Navigate") + item(confirm, "Select");
        if (modalDismissible) {
            prompt += item(cancel, "Back");
        }
        prompt += item("R-stick", "Scroll");
    } else if (presentation.runtime.titleScreen) {
        prompt += item("L-stick / D-pad", "Navigate") + item(confirm, "Select") + item(labels.menu, "Settings");
    } else if (presentation.metadata.interaction == PanelInteractionMode::Takeover
        && (screen == Screen::StoryBriefing || screen == Screen::ArrivalFanfare)) {
        prompt += item(labels.south, "Continue") + item(labels.menu, "Pause");
    } else if (mining) {
        if (presentation.runtime.miningEvaActive) {
            prompt += describedItem("Thrust", "L-stick")
                + describedItem("Aim", "R-stick")
                + describedItem("Fire", labels.rightTrigger)
                + describedItem("Drill", labels.leftTrigger)
                + describedItem("Scan", labels.west);
        } else {
            prompt += describedItem("Move", "L-stick")
                + describedItem("Drill", labels.rightTrigger)
                + describedItem("Scan", labels.west);
        }
        if (presentation.runtime.miningTetherAvailable) {
            prompt += describedItem("Tether", labels.north);
        }
        prompt += describedItem(
            presentation.runtime.miningEvaActive ? "Enter rig" : "Exit rig",
            std::string("Hold ") + labels.south);
        if (presentation.runtime.miningStowAvailable) {
            prompt += describedItem("Stow / Leave", std::string("Tap ") + labels.south);
        }
        if (presentation.runtime.miningAbortAvailable) {
            prompt += describedItem("Recall", labels.east);
        }
        prompt += item(labels.menu, "Pause");
    } else if (flyby) {
        prompt += describedItem("Accelerate / Slow", "L-stick vertical")
            + describedItem("Turn", "L-stick horizontal")
            + describedItem("Abort", labels.east, "Hold to record a Miss")
            + item(labels.menu, "Pause");
    } else if (orbit) {
        prompt += describedItem("Prograde / Retrograde", "L-stick vertical")
            + describedItem("Tighten / Widen", "L-stick horizontal")
            + describedItem("Abort", labels.east, "Hold to record a Miss")
            + item(labels.menu, "Pause");
    } else if (screen == Screen::SurfaceScan || screen == Screen::SurfacePush) {
        prompt += item(labels.south, "Pulse / push") + item(labels.west, screen == Screen::SurfaceScan ? "Log survey" : "Set start depth")
            + item(labels.east, "Hold: abort") + item(labels.menu, "Pause");
    } else if (screen == Screen::Launch
        && presentation.metadata.overlay != PanelOverlayKind::PreflightLaunch) {
        if (presentation.contentMarkup.find("data-launch-manual-controls=\"1\"") != std::string::npos) {
            prompt += describedItem("Steer / throttle", "L-stick");
        }
        prompt += describedItem("Turn Around", labels.south);
        if (presentation.contentMarkup.find("data-rr-action=\"cut_engines\"") != std::string::npos) {
            prompt += describedItem("Engines Off / On", labels.west);
        }
        prompt += item(labels.menu, "Pause");
    } else if (presentation.metadata.overlay == PanelOverlayKind::PreflightLaunch) {
        prompt += item(
            labels.south,
            presentation.runtime.launchQueued ? "Launch queued" : "Launch")
            + item(labels.menu, "Pause");
    } else {
        prompt += item("L-stick / D-pad", "Navigate") + item(confirm, "Select")
            + item(cancel, "Back") + item("R-stick", "Scroll") + item(labels.menu, "Pause");
    }
    return prompt + "</div>";
}

std::string controllerResumeModalBody(std::string body, bool blocked, bool controllerConnected)
{
    if (!blocked) {
        return body;
    }
    const std::size_t marker = body.find("data-controller-resume=\"1\"");
    if (marker == std::string::npos) {
        return body;
    }
    const std::size_t tagEnd = body.find('>', marker);
    if (tagEnd == std::string::npos) {
        return body;
    }
    body.insert(tagEnd, " disabled=\"1\"");
    const std::size_t contentStart = body.find('>', marker) + 1;
    const std::size_t contentEnd = body.find("</button>", contentStart);
    if (contentEnd != std::string::npos) {
        body.replace(
            contentStart,
            contentEnd - contentStart,
            controllerConnected ? "Release controller" : "Reconnect controller");
    }
    return body;
}

std::string performanceStatsMarkup(const PerformanceStats& stats)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(1);
    out << "<div class=\"performance-title\"><strong>" << stats.framesPerSecond
        << " FPS</strong><span>" << stats.frameTimeMilliseconds << " ms avg</span></div>";
    out << "<p>Frame " << stats.latestFrameTimeMilliseconds << " ms | median "
        << stats.medianFrameTimeMilliseconds << " | p95 " << stats.p95FrameTimeMilliseconds
        << " | p99 " << stats.p99FrameTimeMilliseconds << " ms</p>";
    out << "<p>CPU work " << stats.cpuFrameMilliseconds << " ms | median "
        << stats.medianCpuFrameMilliseconds << " | p95 " << stats.p95CpuFrameMilliseconds
        << " | p99 " << stats.p99CpuFrameMilliseconds << " ms</p>";
    out << "<p>Input " << stats.inputMilliseconds << " | Sim " << stats.simulationMilliseconds
        << " ms (" << stats.simulationSteps << " steps)</p>";
    out << "<p>Scene " << stats.sceneRenderMilliseconds << " | UI " << stats.uiRenderMilliseconds
        << " | Present " << stats.presentMilliseconds << " ms</p>";
    out << "<p>Pacing " << (stats.platform.verticalSyncActive ? "FIFO/VSync" : "no VSync");
    if (stats.platform.softwareFrameLimiterActive || stats.renderer.softwareFrameLimiterActive) {
        out << " + software limiter";
    }
    out << " | Limit " << (stats.platform.frameLimiterMilliseconds + stats.renderer.limiterIdleMilliseconds)
        << " | Idle " << stats.platform.idleMilliseconds << " ms</p>";
    if (stats.platform.suspendedWakeups > 0) {
        out << "<p>Suspended wakeups " << stats.platform.suspendedWakeups << " total / "
            << stats.platform.suspendedWakeupsPerSecond << " per second</p>";
    }
    out << "<p>Scene draws " << stats.renderer.sceneDrawCalls << " | Vertices "
        << stats.renderer.sceneVertices << "</p>";
    out << "<p>GPU " << stats.renderer.gpuFrameMilliseconds << " ms | Queue-present return "
        << stats.renderer.presentIntervalMilliseconds << " ms | Target "
        << stats.renderer.targetFramesPerSecond << " FPS | Deadline misses "
        << stats.renderer.missedRefreshes << "</p>";
    out << "<p>Scene uploads " << stats.renderer.bufferUploads << " | "
        << std::setprecision(1) << (static_cast<double>(stats.renderer.uploadedBytes) / 1024.0) << " KiB</p>";
    out << "<p>Pipeline events " << stats.renderer.pipelineCreationsThisFrame << " | Device memory "
        << (static_cast<double>(stats.renderer.deviceMemoryBytes) / 1048576.0) << " MiB</p>";
    out << "<p>Startup " << stats.startupMilliseconds << " ms | Decode "
        << stats.textures.decodeMilliseconds << " ms / Upload " << stats.textures.uploadMilliseconds
        << " ms</p>";
    out << "<p>Textures initialized " << stats.textures.decodedTextures << " decoded / "
        << stats.textures.uploadedTextures << " uploaded | "
        << (static_cast<double>(stats.textures.uploadedBytes) / 1048576.0) << " MiB</p>";
    out << "<p>UI rebuilds " << stats.ui.documentRebuilds << " doc / "
        << stats.ui.panelRebuilds << " panel | HUD " << stats.ui.hudPatches << " patches</p>";
    out << "<p>UI geometry "
        << stats.ui.compiledGeometry << " compiled / " << stats.ui.renderedGeometry << " rendered</p>";
    out << "<p>Textures " << stats.renderer.texturesReady << " ready | "
        << stats.renderer.texturesPending << " pending";
    if (stats.renderer.texturesFailed > 0) {
        out << " | <span class=\"performance-warning\">" << stats.renderer.texturesFailed << " failed</span>";
    }
    out << "</p>";
    out << "<p>Viewport " << stats.viewport.logicalWidth << "x" << stats.viewport.logicalHeight
        << " -> " << stats.viewport.drawableWidth << "x" << stats.viewport.drawableHeight
        << " @" << std::setprecision(2) << stats.viewport.densityRatio << "x</p>";
    if (stats.simulationDeltaClamped) {
        out << "<p class=\"performance-warning\">Simulation delta clamped after a frame stall</p>";
    }
    return out.str();
}

RmlSystemInterface g_systemInterface;
Rml::Context* g_context = nullptr;
Rml::ElementDocument* g_document = nullptr;
std::vector<ElementButtonBinding> g_elementButtonBindings;
struct FocusTarget {
    Rml::Element* element = nullptr;
    std::string id;
    float centerX = 0.0f;
    float centerY = 0.0f;
    UiFocusRect bounds;
};
std::vector<FocusTarget> g_focusTargets;
bool g_focusTargetsModalScoped = false;
bool g_displayPreferenceChanged = false;
std::string g_renderedAutoPowerStatus;

void refreshAutoPowerStatusElement()
{
    if (!g_document) return;
    Rml::Element* element = g_document->GetElementById("frame-limit-status");
    if (!element) return;
    const std::string status = autoPowerStatusText();
    if (status == g_renderedAutoPowerStatus) return;
    element->SetInnerRML(Rml::StringUtilities::EncodeRml(status));
    g_renderedAutoPowerStatus = status;
}

enum class ControllerFocusRow {
    None,
    Choices,
    HangarChoices,
    HangarActions,
    DroneChoices,
    DroneLoadout,
    SurfaceChoices,
    SurfaceCallout,
    Actions,
    Titlebar,
    Utilities
};

ControllerFocusRow controllerFocusRow(const FocusTarget& target)
{
    if (!target.element) {
        return ControllerFocusRow::None;
    }
    if (target.element->Closest(".hangar-controller-choice-row")) {
        return ControllerFocusRow::HangarChoices;
    }
    if (target.element->Closest(".hangar-controller-action-row")) {
        return ControllerFocusRow::HangarActions;
    }
    if (target.element->Closest(".drone-controller-choice-row")) {
        return ControllerFocusRow::DroneChoices;
    }
    if (target.element->Closest(".drone-controller-loadout-row")) {
        return ControllerFocusRow::DroneLoadout;
    }
    if (target.element->Closest(".surface-controller-action-row")) {
        return ControllerFocusRow::SurfaceChoices;
    }
    if (target.element->Closest(".surface-controller-callout")) {
        return ControllerFocusRow::SurfaceCallout;
    }
    if (target.element->Closest(".controller-choice-row")) {
        return ControllerFocusRow::Choices;
    }
    if (target.element->Closest(".controller-action-row")) {
        return ControllerFocusRow::Actions;
    }
    if (target.element->Closest(".panel-head-actions")) {
        return ControllerFocusRow::Titlebar;
    }
    if (target.element->Closest(".utility-actions")) {
        return ControllerFocusRow::Utilities;
    }
    return ControllerFocusRow::None;
}

FocusTarget* directionalControllerRowTarget(
    FocusTarget& current,
    ControllerFocusRow destinationRow,
    UiDirection direction)
{
    const bool horizontal = direction == UiDirection::Left || direction == UiDirection::Right;
    const float directionSign = direction == UiDirection::Left || direction == UiDirection::Up ? -1.0f : 1.0f;
    FocusTarget* best = nullptr;
    float bestScore = std::numeric_limits<float>::max();
    for (FocusTarget& target : g_focusTargets) {
        if (controllerFocusRow(target) != destinationRow) {
            continue;
        }
        const float primary = directionSign * (horizontal
            ? target.centerX - current.centerX
            : target.centerY - current.centerY);
        if (primary <= 1.0f) {
            continue;
        }
        // Drone Ops deliberately puts the work toolbar and loadout to the
        // right of a wider card grid. Keep semantic handoffs stable even
        // when the target sits outside the current control's x/y column.
        const float secondary = std::abs(horizontal
            ? target.centerY - current.centerY
            : target.centerX - current.centerX);
        const float score = primary + secondary * 0.25f;
        if (score < bestScore) {
            bestScore = score;
            best = &target;
        }
    }
    return best;
}

FocusTarget* firstDirectionalControllerRowTarget(
    FocusTarget& current,
    UiDirection direction,
    std::initializer_list<ControllerFocusRow> destinationRows)
{
    for (const ControllerFocusRow destinationRow : destinationRows) {
        if (FocusTarget* target = directionalControllerRowTarget(current, destinationRow, direction)) {
            return target;
        }
    }
    return nullptr;
}

FocusTarget* droneLoadoutGridTarget(FocusTarget& current, UiDirection direction)
{
    if (!current.element) {
        return nullptr;
    }
    Rml::Element* currentSlot = current.element->Closest(".drone-loadout-slot");
    if (!currentSlot) {
        return nullptr;
    }
    const int currentIndex = currentSlot->GetAttribute<int>("data-drone-slot-index", -1);
    if (currentIndex < 0) {
        return nullptr;
    }

    int destinationIndex = -1;
    switch (direction) {
    case UiDirection::Left:
        if (currentIndex % 2 == 1) destinationIndex = currentIndex - 1;
        break;
    case UiDirection::Right:
        if (currentIndex % 2 == 0) destinationIndex = currentIndex + 1;
        break;
    case UiDirection::Up:
        if (currentIndex >= 2) destinationIndex = currentIndex - 2;
        break;
    case UiDirection::Down:
        if (currentIndex < 4) destinationIndex = currentIndex + 2;
        break;
    case UiDirection::Count:
        break;
    }
    if (destinationIndex < 0) {
        return nullptr;
    }

    for (FocusTarget& target : g_focusTargets) {
        if (controllerFocusRow(target) != ControllerFocusRow::DroneLoadout || !target.element) {
            continue;
        }
        Rml::Element* slot = target.element->Closest(".drone-loadout-slot");
        if (slot && slot->GetAttribute<int>("data-drone-slot-index", -1) == destinationIndex) {
            return &target;
        }
    }
    return nullptr;
}

FocusTarget* rightAlignedControllerRowTarget(
    FocusTarget& current,
    ControllerFocusRow destinationRow)
{
    const ControllerFocusRow sourceRow = controllerFocusRow(current);
    std::vector<FocusTarget*> sourceTargets;
    std::vector<FocusTarget*> destinationTargets;
    for (FocusTarget& target : g_focusTargets) {
        const ControllerFocusRow row = controllerFocusRow(target);
        if (row == sourceRow) {
            sourceTargets.push_back(&target);
        } else if (row == destinationRow) {
            destinationTargets.push_back(&target);
        }
    }
    const auto currentIt = std::find(sourceTargets.begin(), sourceTargets.end(), &current);
    if (currentIt == sourceTargets.end() || destinationTargets.empty()) {
        return nullptr;
    }
    const std::size_t sourceIndex = static_cast<std::size_t>(std::distance(sourceTargets.begin(), currentIt));
    const std::size_t distanceFromRight = sourceTargets.size() - sourceIndex - 1;
    const std::size_t destinationIndex = destinationTargets.size() - std::min(
        distanceFromRight + 1,
        destinationTargets.size());
    return destinationTargets[destinationIndex];
}

// The shared titlebar appears above several screen templates whose main
// controls are neither a utility row nor a specialised workspace row (for
// example Approach's Flyby / Orbit / Landing cards). Treat the first visible
// control below the titlebar as the primary content lane so every template
// has a controller route out of Map / Inventory / Menu.
FocusTarget* titlebarContentTarget(FocusTarget& current)
{
    FocusTarget* best = nullptr;
    float bestScore = std::numeric_limits<float>::max();
    for (FocusTarget& target : g_focusTargets) {
        if (controllerFocusRow(target) == ControllerFocusRow::Titlebar) {
            continue;
        }
        const float verticalDistance = target.centerY - current.centerY;
        if (verticalDistance <= 1.0f) {
            continue;
        }
        const float horizontalDistance = std::abs(target.centerX - current.centerX);
        const float score = verticalDistance + horizontalDistance * 0.25f;
        if (score < bestScore) {
            bestScore = score;
            best = &target;
        }
    }
    return best;
}

class RmlSettingsEventListener final : public Rml::EventListener {
public:
    void ProcessEvent(Rml::Event& event) override
    {
        Rml::Element* target = event.GetTargetElement();
        if (auto* control = dynamic_cast<Rml::ElementFormControl*>(target)) {
            if (control->GetTagName() == "select" && control->HasAttribute("data-resolution-select")) {
                rr_rml_set_resolution_preset(control->GetValue().c_str());
                g_displayPreferenceChanged = true;
                return;
            }
            if (control->GetTagName() == "select" && control->HasAttribute("data-game-speed-select")) {
                rr_rml_set_game_speed_multiplier(control->GetValue().c_str());
                return;
            }
            if (control->GetTagName() == "select" && control->HasAttribute("data-frame-limit-select")) {
                rr_rml_set_frame_limit_preference(control->GetValue().c_str());
                return;
            }
            if (control->GetTagName() == "select" && control->HasAttribute("data-keyboard-drill-mode-select")) {
                rr_rml_set_keyboard_drill_mode_preference(control->GetValue().c_str());
                return;
            }
            if (control->GetTagName() == "select" && control->HasAttribute("data-controller-prompt-select")) {
                rr_rml_set_controller_preference("promptFamily", control->GetValue().c_str());
                g_displayPreferenceChanged = true;
                return;
            }
            if (control->GetTagName() == "select" && control->HasAttribute("data-controller-deadzone-select")) {
                rr_rml_set_controller_preference("stickDeadzone", control->GetValue().c_str());
                return;
            }
        }

        if (auto* input = dynamic_cast<Rml::ElementFormControlInput*>(target)) {
            if (input->HasAttribute("data-camera-shake-toggle")) {
                rr_rml_set_camera_shake_disabled(input->HasAttribute("checked") ? 0 : 1);
                return;
            }
            if (input->HasAttribute("data-debug-tools-toggle")) {
                rr_rml_set_debug_tools_enabled(input->HasAttribute("checked") ? 1 : 0);
                return;
            }
        }
    }
};

RmlSettingsEventListener g_settingsEventListener;

bool dispatchButtonBinding(GameRmlUi& owner, const RmlButtonBinding& binding)
{
    if (binding.close) {
        owner.closeModal();
        return true;
    }
    if (!binding.modal.empty()) {
        owner.openModal(binding.modal);
        return true;
    }
    if (!binding.action.empty()) {
        owner.dispatchAction(binding.action);
        return true;
    }
    if (binding.helpToggle) {
        rr_rml_set_help_disabled(rr_rml_help_disabled() == 0 ? 1 : 0);
        owner.refresh();
        return true;
    }
    if (binding.cameraShakeToggle) {
        rr_rml_set_camera_shake_disabled(rr_rml_camera_shake_disabled() == 0 ? 1 : 0);
        owner.refresh();
        return true;
    }
    if (binding.desktopFullscreenToggle) {
        rr_rml_set_desktop_fullscreen(rr_rml_desktop_fullscreen_enabled() == 0 ? 1 : 0);
        owner.refresh();
        return true;
    }
    if (binding.debugToolsToggle) {
        rr_rml_set_debug_tools_enabled(rr_rml_debug_tools_enabled() == 0 ? 1 : 0);
        owner.refresh();
        return true;
    }
    if (binding.performanceStatsToggle) {
        rr_rml_set_performance_stats_enabled(rr_rml_performance_stats_enabled() == 0 ? 1 : 0);
        owner.refresh();
        return true;
    }
    if (!binding.controllerSetting.empty()) {
        int field = binding.controllerSetting == "invertFlightY" ? 0 : (binding.controllerSetting == "swapConfirmCancel" ? 1 : 2);
        const bool current = rr_rml_controller_boolean_preference(field) != 0;
        rr_rml_set_controller_preference(binding.controllerSetting.c_str(), current ? "false" : "true");
        owner.refresh();
        return true;
    }
    return false;
}

void collectButtonElements(Rml::Element* element, std::vector<Rml::Element*>& buttons)
{
    if (!element) {
        return;
    }
    if (element->GetTagName() == "button") {
        buttons.push_back(element);
    }
    const int children = element->GetNumChildren(true);
    for (int index = 0; index < children; ++index) {
        collectButtonElements(element->GetChild(index), buttons);
    }
}

std::vector<RmlButtonBinding> bindLoadedButtons()
{
    g_elementButtonBindings.clear();
    if (!g_document) {
        return {};
    }

    std::vector<Rml::Element*> elements;
    collectButtonElements(g_document, elements);
    std::vector<RmlButtonBinding> bindings;
    bindings.reserve(elements.size());
    g_elementButtonBindings.reserve(elements.size());
    for (Rml::Element* element : elements) {
        RmlButtonBinding binding = buttonBindingFromElement(*element);
        bindings.push_back(binding);
        g_elementButtonBindings.push_back({element, std::move(binding)});
    }
    return bindings;
}

bool focusableElement(Rml::Element* element)
{
    if (!element || !element->IsVisible(true)) {
        return false;
    }
    const Rml::String& tag = element->GetTagName();
    if (tag != "button" && tag != "select" && !(tag == "input" && element->GetAttribute<Rml::String>("type", "") == "checkbox")) {
        return false;
    }
    if (auto* control = dynamic_cast<Rml::ElementFormControl*>(element); control && control->IsDisabled()) {
        return false;
    }
    const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
    return size.x > 1.0f && size.y > 1.0f;
}

std::string derivedFocusId(Rml::Element* element)
{
    std::string id = element->GetAttribute<Rml::String>("data-ui-focus-id", "");
    if (!id.empty()) {
        return id;
    }
    if (element->HasAttribute("data-resolution-select")) return "setting:resolution";
    if (element->HasAttribute("data-frame-limit-select")) return "setting:frame_limit";
    if (element->HasAttribute("data-game-speed-select")) return "setting:game_speed";
    if (element->HasAttribute("data-keyboard-drill-mode-select")) return "setting:keyboard_drill_mode";
    if (element->HasAttribute("data-controller-prompt-select")) return "setting:controller_prompt";
    if (element->HasAttribute("data-controller-deadzone-select")) return "setting:controller_deadzone";
    if (element->GetTagName() == "input") {
        const std::string name = element->GetAttribute<Rml::String>("name", element->GetId());
        if (!name.empty()) return "setting:" + name;
    }
    const auto bound = std::find_if(g_elementButtonBindings.begin(), g_elementButtonBindings.end(), [element](const ElementButtonBinding& entry) {
        return entry.element == element;
    });
    if (bound != g_elementButtonBindings.end()) {
        return bound->binding.focusId;
    }
    return {};
}

void collectFocusableElements(Rml::Element* element, std::vector<Rml::Element*>& elements)
{
    if (!element) {
        return;
    }
    if (focusableElement(element)) {
        elements.push_back(element);
    }
    const int children = element->GetNumChildren(true);
    for (int index = 0; index < children; ++index) {
        collectFocusableElements(element->GetChild(index), elements);
    }
}

void clearFocusTargets()
{
    for (FocusTarget& target : g_focusTargets) {
        if (!target.element) {
            continue;
        }
        target.element->SetClass("rr-controller-focus", false);
        target.element->Blur();
    }
    g_focusTargets.clear();
}

Rml::Element* focusGeometryElement(Rml::Element* control)
{
    if (!control) {
        return nullptr;
    }
    static constexpr std::string_view visualItemSelectors[] = {
        ".ui-choice-row",
        ".surface-choice-row",
        ".upgrade-draft-card",
        ".pilot-card",
        ".drone-loadout-slot",
        ".drone-ops-callout",
    };
    for (const std::string_view selector : visualItemSelectors) {
        if (Rml::Element* item = control->Closest(std::string(selector))) {
            return item;
        }
    }
    return control;
}

void collectFocusTargets(bool modalOpen)
{
    clearFocusTargets();
    g_focusTargetsModalScoped = modalOpen;
    if (!g_document) {
        return;
    }
    Rml::Element* root = modalOpen ? g_document->GetElementById("rr-modal") : g_document->GetElementById("rr-panel");
    if (!root) {
        return;
    }
    std::vector<Rml::Element*> elements;
    collectFocusableElements(root, elements);
    std::vector<std::string> seen;
    g_focusTargets.reserve(elements.size());
    for (Rml::Element* element : elements) {
        std::string id = derivedFocusId(element);
        if (id.empty()) continue;
        const int duplicates = static_cast<int>(std::count(seen.begin(), seen.end(), id));
        seen.push_back(id);
        if (duplicates > 0) {
            id += "#" + std::to_string(duplicates);
        }
        Rml::Element* geometryElement = focusGeometryElement(element);
        Rml::Rectanglef bounds;
        if (!Rml::ElementUtilities::GetBoundingBox(bounds, geometryElement, Rml::BoxArea::Border)) {
            const Rml::Vector2f offset = geometryElement->GetAbsoluteOffset(Rml::BoxArea::Border);
            const Rml::Vector2f size = geometryElement->GetBox().GetSize(Rml::BoxArea::Border);
            bounds = Rml::Rectanglef::FromPositionSize(offset, size);
        }
        g_focusTargets.push_back({
            element,
            std::move(id),
            bounds.Center().x,
            bounds.Center().y,
            {bounds.Left(), bounds.Top(), bounds.Right(), bounds.Bottom()}});
    }
}

FocusTarget* findFocusTarget(std::string_view id)
{
    const auto it = std::find_if(g_focusTargets.begin(), g_focusTargets.end(), [id](const FocusTarget& target) {
        return target.id == id;
    });
    return it == g_focusTargets.end() ? nullptr : &*it;
}

bool applyControllerFocus(
    FocusTarget* target,
    std::string& focusedId,
    float& centerX,
    float& centerY,
    bool& hasCenter)
{
    if (!target || !target->element) {
        return false;
    }
    for (FocusTarget& candidate : g_focusTargets) {
        if (candidate.element) {
            candidate.element->SetClass("rr-controller-focus", candidate.element == target->element);
        }
    }
    target->element->Focus(true);
    target->element->ScrollIntoView(false);
    focusedId = target->id;
    centerX = target->centerX;
    centerY = target->centerY;
    hasCenter = true;
    return true;
}

FocusTarget* nearestFocusTarget(float centerX, float centerY)
{
    FocusTarget* nearest = nullptr;
    float best = std::numeric_limits<float>::max();
    for (FocusTarget& target : g_focusTargets) {
        const float dx = target.centerX - centerX;
        const float dy = target.centerY - centerY;
        const float score = dx * dx + dy * dy;
        if (score < best) {
            best = score;
            nearest = &target;
        }
    }
    return nearest;
}

FocusTarget* defaultFocusTarget()
{
    const auto explicitDefault = std::find_if(g_focusTargets.begin(), g_focusTargets.end(), [](const FocusTarget& target) {
        return target.element && target.element->HasAttribute("data-ui-default-focus");
    });
    if (explicitDefault != g_focusTargets.end()) {
        return &*explicitDefault;
    }
    if (g_focusTargetsModalScoped) {
        const auto primary = std::find_if(g_focusTargets.begin(), g_focusTargets.end(), [](const FocusTarget& target) {
            if (!target.element) {
                return false;
            }
            return target.element->IsClassSet("ok")
                || target.element->Closest(".primary-actions")
                || target.element->Closest(".draft-actions")
                || target.element->Closest(".final-actions")
                || target.element->Closest(".card-footer");
        });
        return primary != g_focusTargets.end()
            ? &*primary
            : (g_focusTargets.empty() ? nullptr : &g_focusTargets.front());
    }
    const auto controllerChoice = std::find_if(g_focusTargets.begin(), g_focusTargets.end(), [](const FocusTarget& target) {
        return controllerFocusRow(target) == ControllerFocusRow::Choices;
    });
    if (controllerChoice != g_focusTargets.end()) {
        return &*controllerChoice;
    }
    const auto primary = std::find_if(g_focusTargets.begin(), g_focusTargets.end(), [](const FocusTarget& target) {
        if (!target.element) return false;
        return target.element->Closest(".primary-actions") || target.element->Closest(".draft-actions") ||
            target.element->Closest(".final-actions") || target.element->Closest(".card-footer");
    });
    if (primary != g_focusTargets.end()) {
        primary->element->SetAttribute("data-ui-default-focus", "1");
        return &*primary;
    }
    const auto nonTitlebar = std::find_if(g_focusTargets.begin(), g_focusTargets.end(), [](const FocusTarget& target) {
        return target.element && !target.element->Closest(".panel-head-actions");
    });
    return nonTitlebar != g_focusTargets.end() ? &*nonTitlebar : (g_focusTargets.empty() ? nullptr : &g_focusTargets.front());
}

bool activateButtonElement(GameRmlUi& owner, Rml::Element* target)
{
    if (!target) {
        return false;
    }

    Rml::Element* button = target->GetTagName() == "button" ? target : target->Closest("button");
    if (!button) {
        return false;
    }
    if (auto* control = dynamic_cast<Rml::ElementFormControl*>(button);
        control && control->IsDisabled()) {
        return false;
    }

    const auto bound = std::find_if(g_elementButtonBindings.begin(), g_elementButtonBindings.end(), [&](const ElementButtonBinding& entry) {
        return entry.element == button;
    });
    if (bound != g_elementButtonBindings.end() && dispatchButtonBinding(owner, bound->binding)) {
        return true;
    }

    const Rml::String closeModal = button->GetAttribute<Rml::String>("data-ui-close-modal", "");
    if (!closeModal.empty()) {
        owner.closeModal();
        return true;
    }

    const std::string modalId = button->GetAttribute<Rml::String>("data-ui-modal", "");
    if (!modalId.empty()) {
        owner.openModal(modalId);
        return true;
    }

    const std::string action = button->GetAttribute<Rml::String>("data-rr-action", "");
    if (!action.empty()) {
        owner.dispatchAction(action);
        return true;
    }

    std::string label;
    std::vector<Rml::Element*> stack {button};
    while (!stack.empty()) {
        Rml::Element* element = stack.back();
        stack.pop_back();
        if (auto* text = dynamic_cast<Rml::ElementText*>(element)) {
            label += text->GetText();
            label.push_back(' ');
            continue;
        }
        for (int index = element->GetNumChildren(true) - 1; index >= 0; --index) {
            stack.push_back(element->GetChild(index));
        }
    }
    if (label.empty()) {
        label = textFromMarkup(button->GetInnerRML());
    }
    return owner.activateButtonLabel(label);
}

Rml::Element* buttonElementAtPoint(Rml::Context& context, const Rml::Vector2f& point)
{
    Rml::Element* element = context.GetElementAtPoint(point);
    if (element && element->GetTagName() == "sliderbar") {
        element = context.GetElementAtPoint(point, element);
    }
    return element && element->GetTagName() == "button" ? element : (element ? element->Closest("button") : nullptr);
}

} // namespace

GameRmlUi::GameRmlUi(
    IPreferenceStore& preferences,
    IPlatformHost& host,
    IUiBridge& uiBridge,
    IRmlRenderHost& renderHost,
    std::string assetRoot)
    : preferences_(preferences), host_(host), uiBridge_(uiBridge), renderHost_(renderHost), assetRoot_(std::move(assetRoot))
{
}

bool GameRmlUi::initialize(ActionHandler actionHandler)
{
    if (initialized_ || rmlInitialized_ || renderHostInitialized_) {
        shutdown();
    }
    g_preferences = &preferences_;
    g_host = &host_;
    g_uiBridge = &uiBridge_;
    actionHandler_ = std::move(actionHandler);

    Rml::SetSystemInterface(&g_systemInterface);
    const auto failInitialization = [&](std::string message) {
        if (!message.empty()) {
            host_.log(PlatformLogLevel::Error, std::move(message));
        }
        shutdown();
        return false;
    };

    if (!renderHost_.initialize()) {
        return failInitialization("Unable to initialize the RmlUi render host.");
    }
    renderHostInitialized_ = true;
    Rml::SetRenderInterface(&renderHost_.renderInterface());

    if (!Rml::Initialise()) {
        return failInitialization("Unable to initialize RmlUi.");
    }
    rmlInitialized_ = true;

    const std::filesystem::path fontRoot = std::filesystem::path(assetRoot_) / "assets" / "fonts";
    const std::filesystem::path regularFont = fontRoot / "SourceCodePro-Regular.ttf";
    const std::filesystem::path semiboldFont = fontRoot / "SourceCodePro-Semibold.ttf";
    const std::filesystem::path italicFont = fontRoot / "SourceCodePro-It.ttf";
    const auto loadFamily = [&](const char* family) {
        return Rml::LoadFontFace(
                   regularFont.string(), family, Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal)
            && Rml::LoadFontFace(
                semiboldFont.string(), family, Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Bold)
            && Rml::LoadFontFace(
                italicFont.string(), family, Rml::Style::FontStyle::Italic, Rml::Style::FontWeight::Normal);
    };
    if (!loadFamily("source-code-pro") || !loadFamily("rmlui-debugger-font")) {
        return failInitialization(
            "Unable to load bundled Source Code Pro fonts from " + fontRoot.string());
    }

    g_context = Rml::CreateContext("rocket-ui", {rr_rml_viewport_width(), rr_rml_viewport_height()});
    if (!g_context) {
        return failInitialization("Unable to create the RmlUi context rocket-ui.");
    }
    g_context->SetDensityIndependentPixelRatio(static_cast<float>(rr_rml_density_ratio()));

    const std::filesystem::path uiRoot = std::filesystem::path(assetRoot_) / "assets" / "ui";
    const std::array<std::filesystem::path, 16> requiredUiAssets {
        uiRoot / "panel.rml",
        uiRoot / "styles" / "all.rcss",
        uiRoot / "styles" / "tokens.rcss",
        uiRoot / "styles" / "primitives.rcss",
        uiRoot / "styles" / "shells.rcss",
        uiRoot / "styles" / "families.rcss",
        uiRoot / "styles" / "screen-exceptions.rcss",
        uiRoot / "styles" / "legacy.rcss",
        uiRoot / "templates" / "rr-document-shell.rml",
        uiRoot / "templates" / "rr-workspace-shell.rml",
        uiRoot / "templates" / "rr-control-shell.rml",
        uiRoot / "templates" / "rr-surface-minigame-shell.rml",
        uiRoot / "templates" / "rr-mining-shell.rml",
        uiRoot / "templates" / "rr-takeover-shell.rml",
        uiRoot / "templates" / "rr-results-shell.rml",
        uiRoot / "templates" / "rr-modal-shell.rml",
    };
    for (const std::filesystem::path& asset : requiredUiAssets) {
        if (!std::filesystem::is_regular_file(asset)) {
            return failInitialization("Required RmlUi asset is missing: " + asset.string());
        }
    }

    const std::filesystem::path stylePath = uiRoot / "styles" / "all.rcss";
    std::ifstream styleStream(stylePath, std::ios::binary);
    externalRcss_.assign(
        std::istreambuf_iterator<char>(styleStream),
        std::istreambuf_iterator<char>());
    if (!styleStream.good() && !styleStream.eof()) {
        return failInitialization("Unable to read required RmlUi stylesheet: " + stylePath.string());
    }
    if (externalRcss_.empty()) {
        return failInitialization("Required RmlUi stylesheet is empty: " + stylePath.string());
    }
    Rml::SharedPtr<Rml::StyleSheetContainer> stylesheet =
        Rml::Factory::InstanceStyleSheetString(externalRcss_);
    if (!stylesheet) {
        return failInitialization("Malformed RmlUi stylesheet: " + stylePath.string());
    }

    const std::filesystem::path documentPath = uiRoot / "panel.rml";
    g_document = g_context->LoadDocument(documentPath.generic_string());
    if (!g_document) {
        return failInitialization("Unable to load required RmlUi document: " + documentPath.string());
    }
    static constexpr std::string_view requiredHostIds[] {
        "rr-document",
        "rr-scene-overlay-host",
        "rr-panel",
        "rr-modal-host",
        "rr-controller-prompt-host",
        "rr-performance-host",
    };
    for (std::string_view id : requiredHostIds) {
        if (!g_document->GetElementById(std::string(id))) {
            return failInitialization(
                "Malformed RmlUi document " + documentPath.string()
                + ": missing required host #" + std::string(id));
        }
    }

    Rml::Element* documentElement = g_document->GetElementById("rr-document");
    Rml::Element* panelHost = g_document->GetElementById("rr-panel");
    Rml::Element* modalHost = g_document->GetElementById("rr-modal-host");
    g_document->SetStyleSheetContainer(std::move(stylesheet));
    if (!applyPanelRcssProperties(*g_document, panelMode_)) {
        return failInitialization(
            "Failed to apply RmlUi runtime layout properties: " + stylePath.string());
    }
    g_document->SetProperty("--rr-phase-board-width", std::to_string(kPhaseBoardFrameWidth) + "px");
    g_document->SetProperty("--rr-phase-content-width", std::to_string(kPhaseContentLaneWidth) + "px");
    g_document->SetProperty("--rr-phase-lane-inset", std::to_string(kPhaseLaneInset) + "px");
    g_document->SetProperty("--rr-workspace-max-width", std::to_string(kWorkspaceContentMaxWidth) + "px");
    g_document->SetProperty("--rr-workspace-inset", std::to_string(kWorkspaceHorizontalPadding) + "px");

    struct TemplateProbe {
        std::string_view name;
        std::string_view contentId;
        Rml::Element* host;
    };
    const std::array<TemplateProbe, 7> templateProbes {{
        {"rr-workspace-shell", "rr-workspace-content", panelHost},
        {"rr-control-shell", "rr-control-content", panelHost},
        {"rr-surface-minigame-shell", "rr-surface-minigame-content", panelHost},
        {"rr-mining-shell", "rr-mining-content", panelHost},
        {"rr-takeover-shell", "rr-takeover-content", panelHost},
        {"rr-results-shell", "rr-results-content", panelHost},
        {"rr-modal-shell", "rr-modal", modalHost},
    }};
    for (const TemplateProbe& probe : templateProbes) {
        probe.host->SetInnerRML(
            "<template src=\"" + std::string(probe.name)
            + "\"><span data-rr-template-probe=\"1\"></span></template>");
        g_context->Update();
        Rml::Element* contentElement =
            g_document->GetElementById(std::string(probe.contentId));
        if (!contentElement
            || !contentElement->QuerySelector("[data-rr-template-probe]")) {
            const std::filesystem::path templatePath =
                uiRoot / "templates" / (std::string(probe.name) + ".rml");
            return failInitialization(
                "Malformed RmlUi template did not expand into its content target: "
                + templatePath.string() + " (#" + std::string(probe.contentId) + ")");
        }
        probe.host->SetInnerRML("");
    }

    g_document->AddEventListener(Rml::EventId::Change, &g_settingsEventListener);
    g_document->Show();
    g_context->Update();
    buttonBindings_ = bindLoadedButtons();

    ++pendingDocumentRebuilds_;
    rr_rml_set_enabled(1);
    initialized_ = true;
    layoutViewportWidth_ = rr_rml_viewport_width();
    layoutViewportHeight_ = rr_rml_viewport_height();
    return true;
}

void GameRmlUi::setPanelPresentation(const PanelDocumentPresentation& presentation)
{
    if (const std::string error = panelPresentationValidationError(presentation);
        !error.empty()) {
        host_.log(
            PlatformLogLevel::Error,
            "Invalid RmlUi panel presentation: " + error + ".");
        return;
    }

    const auto modalEqual = [](const ModalPresentation& left, const ModalPresentation& right) {
        return left.id == right.id
            && left.title == right.title
            && left.bodyMarkup == right.bodyMarkup
            && left.closeAction == right.closeAction
            && left.autoOpen == right.autoOpen
            && left.dismissible == right.dismissible
            && left.showClose == right.showClose;
    };
    const bool presentationStateUnchanged = samePanelStructure(presentation_, presentation)
        && presentation_.runtime.launchQueued == presentation.runtime.launchQueued
        && presentation_.runtime.miningStowAvailable == presentation.runtime.miningStowAvailable
        && presentation_.runtime.miningTetherAvailable == presentation.runtime.miningTetherAvailable
        && presentation_.runtime.miningAbortAvailable == presentation.runtime.miningAbortAvailable
        && presentation_.runtime.overlayValue == presentation.runtime.overlayValue;
    const bool panelMarkupUnchanged = presentation_.contentMarkup == presentation.contentMarkup;
    const bool modalsUnchanged = presentation_.modals.size() == presentation.modals.size()
        && std::equal(
            presentation_.modals.begin(),
            presentation_.modals.end(),
            presentation.modals.begin(),
            modalEqual);
    if (presentationStateUnchanged && panelMarkupUnchanged && modalsUnchanged) {
        return;
    }

    const bool rebuildPanel = !panelMarkupUnchanged
        || presentation_.templateKind != presentation.templateKind;
    const bool rebuildPanelShell = presentation_.templateKind != presentation.templateKind;
    const RmlPanelMode nextPanelMode = panelModeForPresentation(presentation);
    const std::vector<ModalPresentation>& modals = presentation.modals;
    const auto autoModal = std::find_if(modals.begin(), modals.end(), [](const ModalPresentation& modal) {
        return modal.autoOpen;
    });
    const bool activeModalRemainsValid = openModalId_.empty() || findModal(modals, openModalId_);
    const bool modalHierarchyRemainsValid = activeModalRemainsValid
        && std::all_of(modalStack_.begin(), modalStack_.end(), [&](const std::string& modalId) {
            return findModal(modals, modalId) != nullptr;
        });
    const std::string previousModalId = openModalId_;

    presentation_ = presentation;
    panelMode_ = nextPanelMode;
    if (!openModalId_.empty() && !modalHierarchyRemainsValid) {
        clearFocusTargets();
        openModalId_.clear();
        modalStack_.clear();
        modalFocusStack_.clear();
        focusedId_ = modalReturnFocusId_;
        modalReturnFocusId_.clear();
        hasLastFocusCenter_ = false;
    }
    if (openModalId_.empty()) {
        if (autoModal != modals.end()) {
            modalReturnFocusId_ = focusedId_;
            clearFocusTargets();
            modalScrollPositions_.erase(autoModal->id);
            openModalId_ = autoModal->id;
            focusedId_.clear();
            hasLastFocusCenter_ = false;
        }
    }

    if (!initialized_) {
        return;
    }

    const bool modalChanged = !modalsUnchanged || previousModalId != openModalId_;
    refreshPersistentHosts(
        rebuildPanel,
        rebuildPanelShell,
        modalChanged,
        true,
        true,
        previousModalId != openModalId_);
}

void GameRmlUi::setRealtimeHudState(const RealtimeHudState& state)
{
    if (!initialized_ || !g_document) {
        return;
    }

    for (const RealtimeHudPatch& patch : state.patches) {
        Rml::Element* element = g_document->GetElementById(patch.elementId);
        if (!element) {
            if (patch.elementId == "rr-scan-scene-readout" && !openModalId_.empty()) {
                // Scene overlays are deliberately unmounted while a modal owns
                // the viewport. The overlay is rebuilt on close and the next
                // realtime frame supplies its current value.
                continue;
            }
            host_.log(
                PlatformLogLevel::Error,
                "Realtime RmlUi patch target is missing: #" + patch.elementId);
            continue;
        }
        if (patch.updateText) {
            element->SetInnerRML(Rml::StringUtilities::EncodeRml(patch.text));
        }
        if (patch.updateClass) {
            element->SetAttribute("class", patch.cssClass);
        }
        ++pendingHudPatches_;
    }
}

void GameRmlUi::render()
{
    if (!initialized_ || !g_context) {
        return;
    }

    const ViewportMetrics viewport = host_.viewportMetrics();
    const int viewportWidth = viewport.logicalWidth;
    const int viewportHeight = viewport.logicalHeight;
    g_context->SetDimensions({viewportWidth, viewportHeight});
    g_context->SetDensityIndependentPixelRatio(viewport.densityRatio);
    if (g_displayPreferenceChanged
        || viewportWidth != layoutViewportWidth_
        || viewportHeight != layoutViewportHeight_) {
        g_displayPreferenceChanged = false;
        layoutViewportWidth_ = viewportWidth;
        layoutViewportHeight_ = viewportHeight;
        refreshPersistentHosts(false, false, false, false, false, false);
        if (initialized_) {
            clearFocusTargets();
            rebindAndRestoreFocus(false);
        }
    }

    renderHost_.setViewport({
        viewportWidth,
        viewportHeight,
        viewport.drawableWidth,
        viewport.drawableHeight,
    });
    Rml::Rectanglei rootClip;
    if (!openModalId_.empty() || controllerPresentationActive_ || presentation_.runtime.gameplayInputHelper
        || performanceStatsVisible_ || presentation_.metadata.overlay != PanelOverlayKind::None) {
        rootClip = Rml::Rectanglei::FromSize({viewportWidth, viewportHeight});
    } else {
        rootClip = expandedPanelClip(panelMode_);
    }
    renderHost_.setRootClip({rootClip.Left(), rootClip.Top(), rootClip.Right(), rootClip.Bottom()});
    refreshAutoPowerStatusElement();
    g_context->Update();
    if (renderHost_.beginFrame()) {
        g_context->Render();
        renderHost_.endFrame();
    }
    uiDiagnostics_ = renderHost_.diagnostics();
    uiDiagnostics_.documentRebuilds = pendingDocumentRebuilds_;
    uiDiagnostics_.panelRebuilds = pendingPanelRebuilds_;
    uiDiagnostics_.hudPatches = pendingHudPatches_;
    pendingDocumentRebuilds_ = 0;
    pendingPanelRebuilds_ = 0;
    pendingHudPatches_ = 0;
}

bool GameRmlUi::mouseMove(int x, int y)
{
    if (!initialized_ || !g_context) {
        return false;
    }
    // RmlUi is laid out and projected in the platform's logical viewport.
    // Browser pointer events and SDL window mouse events use that same space.
    // Scaling input to the drawable framebuffer here caused hit testing to
    // drift whenever the display density differed from 1x.
    g_context->ProcessMouseMove(x, y, 0);
    return hitTest(x, y);
}

bool GameRmlUi::mouseDown(int x, int y, int button)
{
    if (!initialized_ || !g_context) {
        return false;
    }
    pressedButton_ = nullptr;
    pressedButtonAtSeconds_ = 0.0;
    g_context->ProcessMouseMove(x, y, 0);
    const bool overUi = hitTest(x, y);
    if (overUi) {
        g_context->ProcessMouseButtonDown(std::max(0, button), 0);
    }
    if (button == 0 && overUi) {
        pressedButton_ = buttonElementAtPoint(*g_context, {static_cast<float>(x), static_cast<float>(y)});
        if (pressedButton_ && modalOpen() && !pressedButton_->Closest("#rr-modal")) {
            // The scrim owns the full viewport while a modal is active. Never
            // retain a button found in the panel beneath it.
            pressedButton_ = nullptr;
        }
        if (pressedButton_) {
            pressedButtonAtSeconds_ = rr_rml_now_seconds();
        }
    }
    return overUi;
}

bool GameRmlUi::mouseUp(int x, int y, int button)
{
    if (!initialized_ || !g_context) {
        return false;
    }
    g_context->ProcessMouseMove(x, y, 0);
    g_context->ProcessMouseButtonUp(std::max(0, button), 0);
    if (!hitTest(x, y)) {
        pressedButton_ = nullptr;
        return false;
    }
    const Rml::Vector2f point {static_cast<float>(x), static_cast<float>(y)};
    Rml::Element* releasedButton = buttonElementAtPoint(*g_context, point);
    if (releasedButton && modalOpen() && !releasedButton->Closest("#rr-modal")) {
        releasedButton = nullptr;
    }
    Rml::Element* pressedButton = pressedButton_;
    const double pressedAt = pressedButtonAtSeconds_;
    pressedButton_ = nullptr;
    pressedButtonAtSeconds_ = 0.0;
    if (button != 0 || !pressedButton || releasedButton != pressedButton) {
        return true;
    }
    const Rml::String holdValue = pressedButton->GetAttribute<Rml::String>("data-controller-hold-seconds", "");
    const double holdSeconds = holdValue.empty() ? 0.0 : std::max(0.0, std::atof(holdValue.c_str()));
    if (holdSeconds > 0.0 && rr_rml_now_seconds() - pressedAt + 0.001 < holdSeconds) {
        return true;
    }
    activateButtonElement(*this, pressedButton);
    return true;
}

bool GameRmlUi::mouseWheel(int x, int y, double deltaY)
{
    if (!initialized_ || !g_context || !hitTest(x, y)) {
        return false;
    }
    g_context->ProcessMouseMove(x, y, 0);
    // Both browser deltaY and the SDL adapter use the DOM convention here:
    // positive means the user scrolled down. Preserve that sign for RmlUi;
    // negating it reverses every mouse wheel direction.
    g_context->ProcessMouseWheel(static_cast<float>(deltaY / 90.0), 0);
    return true;
}

bool GameRmlUi::hitTest(int x, int y) const
{
    if (!initialized_) {
        return false;
    }
    if (!openModalId_.empty()) {
        return true;
    }
    if (panelMode_ == RmlPanelMode::MiningFullscreen && g_context) {
        const Rml::Vector2f point {static_cast<float>(x), static_cast<float>(y)};
        return buttonElementAtPoint(*g_context, point) != nullptr;
    }
    if (presentation_.metadata.overlay == PanelOverlayKind::PreflightLaunch && g_context) {
        const Rml::Vector2f point {static_cast<float>(x), static_cast<float>(y)};
        if (buttonElementAtPoint(*g_context, point) != nullptr) {
            return true;
        }
    }
    const Rml::Rectanglei bounds = expandedPanelClip(panelMode_);
    return x >= bounds.Left() && y >= bounds.Top() && x <= bounds.Right() && y <= bounds.Bottom();
}

bool GameRmlUi::navigate(UiDirection direction)
{
    if (!initialized_ || !g_document) {
        return false;
    }
    const bool modalScope = modalOpen();
    if (g_focusTargets.empty() || g_focusTargetsModalScoped != modalScope) {
        collectFocusTargets(modalScope);
    }
    if (g_focusTargets.empty()) {
        return false;
    }

    FocusTarget* current = findFocusTarget(focusedId_);
    if (!current) {
        FocusTarget* fallback = hasLastFocusCenter_ ? nearestFocusTarget(lastFocusCenterX_, lastFocusCenterY_) : defaultFocusTarget();
        return applyControllerFocus(fallback, focusedId_, lastFocusCenterX_, lastFocusCenterY_, hasLastFocusCenter_);
    }

    if ((direction == UiDirection::Left || direction == UiDirection::Right)) {
        if (auto* select = dynamic_cast<Rml::ElementFormControlSelect*>(current->element)) {
            const int delta = direction == UiDirection::Left ? -1 : 1;
            const int next = std::clamp(select->GetSelection() + delta, 0, std::max(0, select->GetNumOptions() - 1));
            if (next != select->GetSelection()) {
                select->SetSelection(next);
                // Programmatic RmlUi selection does not emit Change on its
                // own. Dispatch it explicitly so controller left/right has
                // the same persistence behavior as direct pointer input.
                Rml::Dictionary parameters;
                parameters["value"] = select->GetValue();
                select->DispatchEvent(Rml::EventId::Change, parameters);
                return true;
            }
        }
    }

    const ControllerFocusRow currentRow = controllerFocusRow(*current);
    if (currentRow == ControllerFocusRow::DroneLoadout) {
        if (FocusTarget* gridTarget = droneLoadoutGridTarget(*current, direction)) {
            return applyControllerFocus(
                gridTarget,
                focusedId_,
                lastFocusCenterX_,
                lastFocusCenterY_,
                hasLastFocusCenter_);
        }
        // Left from the first column still crosses the roster/loadout seam
        // below. Every other missing neighbor is the visual edge of the 2x3
        // loadout grid.
        if (direction != UiDirection::Left || current->element->Closest(".drone-loadout-slot")
                ->GetAttribute<int>("data-drone-slot-index", -1) % 2 == 1) {
            return false;
        }
    }
    if ((direction == UiDirection::Left || direction == UiDirection::Right) && currentRow == ControllerFocusRow::SurfaceChoices) {
        return applyControllerFocus(
            directionalControllerRowTarget(*current, currentRow, direction),
            focusedId_,
            lastFocusCenterX_,
            lastFocusCenterY_,
            hasLastFocusCenter_);
    }
    if ((direction == UiDirection::Left || direction == UiDirection::Right)
        && (currentRow == ControllerFocusRow::Titlebar
            || currentRow == ControllerFocusRow::Utilities)) {
        // Horizontal utility lanes follow their authored visual order.
        // Keeping navigation within the semantic row prevents a large card or
        // contextual action below the header from becoming a geometrically
        // closer target on wide, short workspaces.
        return applyControllerFocus(
            directionalControllerRowTarget(*current, currentRow, direction),
            focusedId_,
            lastFocusCenterX_,
            lastFocusCenterY_,
            hasLastFocusCenter_);
    }
    if ((direction == UiDirection::Right && currentRow == ControllerFocusRow::DroneChoices) ||
        (direction == UiDirection::Left && currentRow == ControllerFocusRow::DroneLoadout)) {
        // Stay within the visible pane while there is another actionable
        // control in that direction. At its edge, cross the roster/loadout
        // split instead of allowing focus to become trapped in the roster.
        if (FocusTarget* adjacent = directionalControllerRowTarget(*current, currentRow, direction)) {
            return applyControllerFocus(adjacent, focusedId_, lastFocusCenterX_, lastFocusCenterY_, hasLastFocusCenter_);
        }
        const ControllerFocusRow adjacentPane = currentRow == ControllerFocusRow::DroneChoices
            ? ControllerFocusRow::DroneLoadout
            : ControllerFocusRow::DroneChoices;
        return applyControllerFocus(
            directionalControllerRowTarget(*current, adjacentPane, direction),
            focusedId_,
            lastFocusCenterX_,
            lastFocusCenterY_,
            hasLastFocusCenter_);
    }

    ControllerFocusRow destinationRow = ControllerFocusRow::None;
    if (direction == UiDirection::Down && currentRow == ControllerFocusRow::Utilities
        && current->element->Closest(".phase-board-hangar")) {
        // Hangar operation buttons disappear from the focus graph when every
        // operation is unavailable. Treat that empty logical row as
        // transparent so Details still reaches the always-actionable launch
        // row instead of splitting the screen into two focus islands.
        return applyControllerFocus(
            firstDirectionalControllerRowTarget(
                *current,
                direction,
                {ControllerFocusRow::HangarChoices, ControllerFocusRow::HangarActions}),
            focusedId_,
            lastFocusCenterX_,
            lastFocusCenterY_,
            hasLastFocusCenter_);
    }
    if (direction == UiDirection::Up && currentRow == ControllerFocusRow::HangarActions) {
        return applyControllerFocus(
            firstDirectionalControllerRowTarget(
                *current,
                direction,
                {ControllerFocusRow::HangarChoices, ControllerFocusRow::Utilities}),
            focusedId_,
            lastFocusCenterX_,
            lastFocusCenterY_,
            hasLastFocusCenter_);
    }

    if (direction == UiDirection::Up && currentRow == ControllerFocusRow::HangarChoices) {
        destinationRow = ControllerFocusRow::Utilities;
    } else if (direction == UiDirection::Down && currentRow == ControllerFocusRow::HangarChoices) {
        destinationRow = ControllerFocusRow::HangarActions;
    } else if (direction == UiDirection::Up && currentRow == ControllerFocusRow::SurfaceChoices) {
        destinationRow = ControllerFocusRow::SurfaceCallout;
    } else if (direction == UiDirection::Down && currentRow == ControllerFocusRow::SurfaceCallout) {
        destinationRow = ControllerFocusRow::SurfaceChoices;
    } else if (direction == UiDirection::Up && currentRow == ControllerFocusRow::DroneChoices) {
        destinationRow = ControllerFocusRow::Utilities;
    } else if (direction == UiDirection::Down && currentRow == ControllerFocusRow::Utilities
        && current->element->Closest(".drone-workspace")) {
        destinationRow = ControllerFocusRow::DroneChoices;
    } else if (direction == UiDirection::Up && currentRow == ControllerFocusRow::Utilities) {
        destinationRow = ControllerFocusRow::Titlebar;
    } else if (direction == UiDirection::Down && currentRow == ControllerFocusRow::Titlebar) {
        destinationRow = ControllerFocusRow::Utilities;
    }
    if (destinationRow != ControllerFocusRow::None) {
        const bool semanticWorkspaceHandoff = currentRow == ControllerFocusRow::HangarChoices
            || destinationRow == ControllerFocusRow::HangarChoices
            || currentRow == ControllerFocusRow::HangarActions
            || destinationRow == ControllerFocusRow::HangarActions
            || currentRow == ControllerFocusRow::DroneChoices
            || destinationRow == ControllerFocusRow::DroneChoices
            || currentRow == ControllerFocusRow::SurfaceChoices
            || destinationRow == ControllerFocusRow::SurfaceChoices;
        FocusTarget* destination = semanticWorkspaceHandoff
            ? directionalControllerRowTarget(*current, destinationRow, direction)
            : rightAlignedControllerRowTarget(*current, destinationRow);
        if (!destination && direction == UiDirection::Down && currentRow == ControllerFocusRow::Titlebar) {
            destination = titlebarContentTarget(*current);
        }
        return applyControllerFocus(
            destination,
            focusedId_,
            lastFocusCenterX_,
            lastFocusCenterY_,
            hasLastFocusCenter_);
    }

    std::vector<UiFocusRect> bounds;
    bounds.reserve(g_focusTargets.size());
    for (const FocusTarget& target : g_focusTargets) {
        bounds.push_back(target.bounds);
    }
    const std::size_t currentIndex = static_cast<std::size_t>(current - g_focusTargets.data());
    const std::optional<std::size_t> nextIndex = directionalFocusTarget(bounds, currentIndex, direction);
    FocusTarget* bestTarget = nextIndex ? &g_focusTargets[*nextIndex] : nullptr;
    return applyControllerFocus(bestTarget, focusedId_, lastFocusCenterX_, lastFocusCenterY_, hasLastFocusCenter_);
}

bool GameRmlUi::activateFocused()
{
    if (!initialized_ || !g_document) {
        return false;
    }
    const bool modalScope = modalOpen();
    if (g_focusTargets.empty() || g_focusTargetsModalScoped != modalScope) {
        collectFocusTargets(modalScope);
    }
    FocusTarget* target = findFocusTarget(focusedId_);
    if (!target) {
        if (!navigate(UiDirection::Down)) {
            return false;
        }
        target = findFocusTarget(focusedId_);
    }
    if (!target) {
        return false;
    }
    if (dynamic_cast<Rml::ElementFormControlSelect*>(target->element)) {
        return true;
    }
    if (target->element->GetTagName() == "input" && target->element->GetAttribute<Rml::String>("type", "") == "checkbox") {
        target->element->Click();
        return true;
    }
    return activateButtonElement(*this, target->element);
}

bool GameRmlUi::cancel()
{
    if (!initialized_) {
        return false;
    }
    if (!openModalId_.empty()) {
        const ModalPresentation* modal = findModal(presentation_.modals, openModalId_);
        if (modal && !modal->dismissible) {
            return false;
        }
        closeModal();
        return true;
    }
    FocusTarget* target = findFocusTarget(focusedId_);
    if (!target) {
        return false;
    }
    target->element->SetClass("rr-controller-focus", false);
    target->element->Blur();
    focusedId_.clear();
    return true;
}

bool GameRmlUi::scroll(float amount)
{
    if (!initialized_ || !g_document) {
        return false;
    }
    const bool modalScope = modalOpen();
    if (g_focusTargets.empty() || g_focusTargetsModalScoped != modalScope) {
        collectFocusTargets(modalScope);
    }
    Rml::Element* element = nullptr;
    if (FocusTarget* target = findFocusTarget(focusedId_)) {
        element = target->element;
    }
    while (element && element->GetScrollHeight() <= element->GetClientHeight() + 1.0f) {
        element = element->GetParentNode();
    }
    if (!element) {
        Rml::ElementList scrollBodies;
        g_document->GetElementsByClassName(scrollBodies, "modal-scroll-body");
        element = !scrollBodies.empty() ? scrollBodies.front() : g_document->GetElementById("rr-panel");
    }
    if (!element) {
        return false;
    }
    const float pixels = std::abs(amount) <= 1.0f ? amount * 100.0f : amount;
    element->SetScrollTop(element->GetScrollTop() + pixels);
    return true;
}

bool GameRmlUi::modalOpen() const
{
    return initialized_ && !openModalId_.empty();
}

void GameRmlUi::setControllerPresentation(bool active, ControllerFamily family)
{
    uiBridge_.setControllerPresentation(active, family);
    if (controllerPresentationActive_ == active && controllerFamily_ == family) {
        return;
    }
    const bool controllerLabelsChanged = controllerFamily_ != family
        && presentation_.contentMarkup.find("{{controller_") != std::string::npos;
    controllerPresentationActive_ = active;
    controllerFamily_ = family;
    if (initialized_) {
        refreshPersistentHosts(
            controllerLabelsChanged,
            false,
            false,
            false,
            true,
            false);
    }
}

void GameRmlUi::setControllerFocusVisible(bool visible)
{
    uiBridge_.setControllerFocusVisible(visible);
    if (controllerFocusVisible_ == visible) {
        return;
    }
    controllerFocusVisible_ = visible;
    if (initialized_) {
        refreshPersistentHosts(false, false, false, false, false, false);
    }
}

void GameRmlUi::setControllerResumeBlocked(bool blocked, bool controllerConnected)
{
    uiBridge_.setControllerResumeBlocked(blocked, controllerConnected);
    if (controllerResumeBlocked_ == blocked && controllerResumeConnected_ == controllerConnected) {
        return;
    }
    controllerResumeBlocked_ = blocked;
    controllerResumeConnected_ = controllerConnected;
    if (initialized_) {
        refreshPersistentHosts(
            false,
            false,
            !openModalId_.empty(),
            false,
            true,
            false);
    }
}

std::string GameRmlUi::focusedId() const
{
    return initialized_ ? focusedId_ : std::string {};
}

void GameRmlUi::requestFocus(std::string_view id)
{
    if (id.empty()) {
        return;
    }
    if (!initialized_) {
        return;
    }
    pendingFocusId_ = std::string(id);
}

void GameRmlUi::openModal(const std::string& id)
{
    if (!initialized_) {
        return;
    }
    if (id.empty() || id == openModalId_) {
        return;
    }
    if (!findModal(presentation_.modals, id)) {
        return;
    }
    if (!openModalId_.empty()) {
        modalStack_.push_back(openModalId_);
        modalFocusStack_.push_back(focusedId_);
    } else {
        modalReturnFocusId_ = focusedId_;
    }
    modalScrollPositions_.erase(id);
    clearFocusTargets();
    openModalId_ = id;
    focusedId_.clear();
    hasLastFocusCenter_ = false;
    refreshPersistentHosts(false, false, true, true, true, true);
}

void GameRmlUi::closeModal()
{
    if (!initialized_) {
        return;
    }
    if (openModalId_.empty()) {
        return;
    }
    std::string closeAction;
    if (modalStack_.empty()) {
        if (const ModalPresentation* modal = findModal(presentation_.modals, openModalId_)) {
            closeAction = modal->closeAction;
        }
    }
    clearFocusTargets();
    if (!modalStack_.empty()) {
        openModalId_ = modalStack_.back();
        modalStack_.pop_back();
        focusedId_ = modalFocusStack_.empty() ? std::string() : modalFocusStack_.back();
        if (!modalFocusStack_.empty()) {
            modalFocusStack_.pop_back();
        }
    } else {
        openModalId_.clear();
        focusedId_ = modalReturnFocusId_;
        modalReturnFocusId_.clear();
    }
    refreshPersistentHosts(false, false, true, true, true, true);
    if (!closeAction.empty() && actionHandler_) {
        actionHandler_(closeAction);
    }
}

void GameRmlUi::dispatchAction(const std::string& action)
{
    const bool closesModal = !openModalId_.empty();
    if (closesModal) {
        clearFocusTargets();
        openModalId_.clear();
        modalStack_.clear();
        modalFocusStack_.clear();
        modalReturnFocusId_.clear();
    }
    if (actionHandler_) {
        actionHandler_(action);
    }
    constexpr std::string_view refitSelectionPrefix = "select_refit_offer:";
    if (action.starts_with(refitSelectionPrefix) && action.size() > refitSelectionPrefix.size()) {
        // Refit cards are selection controls. Confirming one should continue
        // naturally at its newly revealed installation action, but only when
        // that action is actually enabled in the next panel render.
        pendingFocusId_ = "action:buy_offer:" + action.substr(refitSelectionPrefix.size());
    }
    if (closesModal) {
        refreshPersistentHosts(false, false, true, true, true, true);
    }
}

bool GameRmlUi::applyPendingFocusIfAvailable()
{
    if (pendingFocusId_.empty()) {
        return false;
    }
    const std::string requestedId = std::move(pendingFocusId_);
    pendingFocusId_.clear();
    FocusTarget* target = findFocusTarget(requestedId);
    if (!target) {
        return false;
    }
    return applyControllerFocus(
        target,
        focusedId_,
        lastFocusCenterX_,
        lastFocusCenterY_,
        hasLastFocusCenter_);
}

void GameRmlUi::refresh()
{
    refreshPersistentHosts(
        true,
        false,
        !openModalId_.empty(),
        true,
        true,
        true);
}

bool GameRmlUi::activateButtonLabel(const std::string& label)
{
    const std::string collapsed = collapsedText(label);
    const auto it = std::find_if(buttonBindings_.begin(), buttonBindings_.end(), [&](const RmlButtonBinding& binding) {
        return binding.label == collapsed;
    });
    if (it == buttonBindings_.end()) {
        return false;
    }

    if (it->close) {
        closeModal();
        return true;
    }
    if (!it->modal.empty()) {
        openModal(it->modal);
        return true;
    }
    if (!it->action.empty()) {
        dispatchAction(it->action);
        return true;
    }
    if (it->helpToggle) {
        rr_rml_set_help_disabled(rr_rml_help_disabled() == 0 ? 1 : 0);
        refresh();
        return true;
    }
    if (it->cameraShakeToggle) {
        rr_rml_set_camera_shake_disabled(rr_rml_camera_shake_disabled() == 0 ? 1 : 0);
        refresh();
        return true;
    }
    if (it->desktopFullscreenToggle) {
        rr_rml_set_desktop_fullscreen(rr_rml_desktop_fullscreen_enabled() == 0 ? 1 : 0);
        refresh();
        return true;
    }
    if (it->debugToolsToggle) {
        rr_rml_set_debug_tools_enabled(rr_rml_debug_tools_enabled() == 0 ? 1 : 0);
        refresh();
        return true;
    }
    if (it->performanceStatsToggle) {
        rr_rml_set_performance_stats_enabled(rr_rml_performance_stats_enabled() == 0 ? 1 : 0);
        refresh();
        return true;
    }
    if (!it->controllerSetting.empty()) {
        const int field = it->controllerSetting == "invertFlightY" ? 0 : (it->controllerSetting == "swapConfirmCancel" ? 1 : 2);
        const bool current = rr_rml_controller_boolean_preference(field) != 0;
        rr_rml_set_controller_preference(it->controllerSetting.c_str(), current ? "false" : "true");
        refresh();
        return true;
    }
    return false;
}

void GameRmlUi::setPerformanceStats(const PerformanceStats& stats, bool visible)
{
    const std::string nextHtml = visible ? performanceStatsMarkup(stats) : std::string {};

    const bool htmlChanged = nextHtml != performanceStatsHtml_;
    const bool visibilityChanged = visible != performanceStatsVisible_;
    if (!htmlChanged && !visibilityChanged) {
        return;
    }

    performanceStatsHtml_ = nextHtml;
    performanceStatsVisible_ = visible;
    if (!initialized_ || !g_document) {
        return;
    }

    Rml::Element* performanceElement = g_document->GetElementById("rr-performance-stats");
    if (!performanceElement) {
        rebuildPerformanceHost();
        return;
    }
    if (htmlChanged) {
        performanceElement->SetInnerRML(performanceStatsHtml_);
    }
    if (visibilityChanged) {
        performanceElement->SetClass(
            "performance-hidden",
            !openModalId_.empty() || !performanceStatsVisible_ || performanceStatsHtml_.empty());
    }
}

UiDiagnostics GameRmlUi::diagnostics() const
{
    return uiDiagnostics_;
}

bool GameRmlUi::applyDocumentPresentationState()
{
    if (!g_context || !g_document) {
        return false;
    }
    layoutViewportWidth_ = rr_rml_viewport_width();
    layoutViewportHeight_ = rr_rml_viewport_height();

    const std::filesystem::path stylePath =
        std::filesystem::path(assetRoot_) / "assets" / "ui" / "styles" / "all.rcss";
    const bool modalVisible = !openModalId_.empty();
    const bool panelInputHelperVisible = presentation_.runtime.responsiveViewport
        && !modalVisible
        && (controllerPresentationActive_ || presentation_.runtime.gameplayInputHelper);
    std::string documentClass = "rr-document";
    if (controllerFocusVisible_) {
        documentClass += " controller-focus-visible";
    }
    if (controllerPresentationActive_) {
        documentClass += " controller-connected";
    }
    if (panelInputHelperVisible) {
        documentClass += " panel-input-helper-visible";
    }
    if (panelUsesResponsiveViewport(panelMode_)) {
        documentClass += panelUsesBottomDockLayout(panelMode_)
            ? " rr-bottom-dock"
            : " rr-side-rail";
    } else {
        documentClass += " rr-fullscreen";
    }
    Rml::Element* documentElement = g_document->GetElementById("rr-document");
    Rml::Element* panelElement = g_document->GetElementById("rr-panel");
    if (!documentElement || !panelElement) {
        const std::filesystem::path documentPath =
            std::filesystem::path(assetRoot_) / "assets" / "ui" / "panel.rml";
        host_.log(
            PlatformLogLevel::Error,
            "Malformed RmlUi document lost a required persistent host: " + documentPath.string());
        return false;
    }
    if (!applyPanelRcssProperties(*g_document, panelMode_)) {
        host_.log(
            PlatformLogLevel::Error,
            "Failed to apply RmlUi runtime layout properties: " + stylePath.string());
        return false;
    }
    documentElement->SetAttribute("class", documentClass);
    g_document->SetProperty("--rr-phase-board-width", std::to_string(kPhaseBoardFrameWidth) + "px");
    g_document->SetProperty("--rr-phase-content-width", std::to_string(kPhaseContentLaneWidth) + "px");
    g_document->SetProperty("--rr-phase-lane-inset", std::to_string(kPhaseLaneInset) + "px");
    g_document->SetProperty("--rr-workspace-max-width", std::to_string(kWorkspaceContentMaxWidth) + "px");
    g_document->SetProperty("--rr-workspace-inset", std::to_string(kWorkspaceHorizontalPadding) + "px");

    const bool titleScreen = panelUsesTitle(panelMode_);
    const bool storyBriefing = panelUsesStoryBriefing(panelMode_);
    const bool results = panelUsesResults(panelMode_);
    const bool droneWorkspace = panelUsesDroneWorkspace(panelMode_);
    const bool workspace = panelUsesWorkspace(panelMode_);
    const bool phaseBoard = panelUsesPhaseBoard(panelMode_);
    const bool miningFullscreen = panelUsesMiningFullscreen(panelMode_);
    const bool arrivalFanfare = panelUsesMissionStamp(panelMode_);
    std::string panelClass = "rr-panel ";
    panelClass += titleScreen
        ? "title-screen-panel-mode"
        : (storyBriefing
            ? "story-briefing-panel-mode"
            : (results
                ? "results-panel-mode"
                : (droneWorkspace
                    ? "drone-workspace-panel"
                    : (workspace
                        ? "workspace-panel phase-board-panel"
                        : (miningFullscreen
                            ? "mining-fullscreen-panel"
                            : (arrivalFanfare
                                ? "arrival-fanfare-panel-mode"
                                : (phaseBoard ? "phase-board-panel" : "control-panel")))))));
    switch (presentation_.metadata.surface) {
    case PanelSurfaceKind::SurfaceOps:
        panelClass += " surface-ops-panel";
        break;
    case PanelSurfaceKind::SurfaceScan:
        panelClass += " surface-scan-panel";
        break;
    case PanelSurfaceKind::DroneOps:
        panelClass += " drone-ops-panel";
        if (rr_rml_viewport_width() < 1280 || rr_rml_viewport_height() < 800) {
            panelClass += " drone-workspace-constrained";
        }
        break;
    default:
        break;
    }
    if (presentation_.metadata.screen == Screen::Navigation) {
        panelClass += " navigation-panel";
    }
    if (presentation_.metadata.variant == "refit") {
        panelClass += " draft-room-panel";
    }
    if (const std::string_view familyClass =
            visualFamilyClass(presentation_.metadata.visualFamily);
        !familyClass.empty()) {
        panelClass += " " + std::string(familyClass);
    }
    if (const std::string_view familyClass = screenFamilyClass(presentation_.metadata.screen);
        !familyClass.empty()) {
        panelClass += " " + std::string(familyClass);
    }
    panelElement->SetAttribute("class", panelClass);

    const std::string contentId(panelTemplateContentId(presentation_.templateKind));
    if (Rml::Element* contentElement = g_document->GetElementById(contentId)) {
        if (Rml::Element* shell = contentElement->Closest(".rr-shell")) {
            std::string shellClass(panelTemplateShellClass(presentation_.templateKind));
            if (presentation_.metadata.legacyContentOwnsLaneGeometry) {
                shellClass += " rr-legacy-content-owns-lane";
            }
            if (const std::string_view familyClass =
                    visualFamilyClass(presentation_.metadata.visualFamily);
                !familyClass.empty()) {
                shellClass += " " + std::string(familyClass);
            }
            if (const std::string_view familyClass =
                    screenFamilyClass(presentation_.metadata.screen);
                !familyClass.empty()) {
                shellClass += " " + std::string(familyClass);
            }
            shell->SetAttribute("class", shellClass);
        }
    }
    return true;
}

bool GameRmlUi::rebuildPanelHost(bool rebuildShell)
{
    Rml::Element* panelElement = g_document
        ? g_document->GetElementById("rr-panel")
        : nullptr;
    if (!panelElement) {
        const std::filesystem::path documentPath =
            std::filesystem::path(assetRoot_) / "assets" / "ui" / "panel.rml";
        host_.log(
            PlatformLogLevel::Error,
            "Malformed RmlUi document lost persistent host #rr-panel: "
                + documentPath.string());
        return false;
    }

    const std::string panelBody = withOpeningControllerLabels(
        syncSettingsControls(sanitizeRml(presentation_.contentMarkup)),
        controllerFamily_);
    const std::string contentId(panelTemplateContentId(presentation_.templateKind));
    Rml::Element* contentElement = g_document->GetElementById(contentId);
    const std::string_view templateName = panelTemplateName(presentation_.templateKind);
    if (!rebuildShell && contentElement) {
        contentElement->SetInnerRML(panelBody);
    } else if (templateName.empty()) {
        panelElement->SetInnerRML(panelBody);
    } else {
        panelElement->SetInnerRML(
            "<template src=\"" + std::string(templateName) + "\">"
            + panelBody + "</template>");
    }

    contentElement = g_document->GetElementById(contentId);
    if (!contentElement) {
        const std::filesystem::path templatePath =
            std::filesystem::path(assetRoot_) / "assets" / "ui" / "templates"
            / (std::string(panelTemplateName(presentation_.templateKind)) + ".rml");
        host_.log(
            PlatformLogLevel::Error,
            "Malformed RmlUi template is missing its content target: " + templatePath.string());
        return false;
    }
    return true;
}

bool GameRmlUi::rebuildOverlayHost()
{
    Rml::Element* overlayHost = g_document
        ? g_document->GetElementById("rr-scene-overlay-host")
        : nullptr;
    if (!overlayHost) {
        host_.log(
            PlatformLogLevel::Error,
            "Malformed RmlUi document lost persistent host #rr-scene-overlay-host.");
        return false;
    }
    const ModalPresentation* activeModal = findModal(presentation_.modals, openModalId_);
    overlayHost->SetInnerRML(activeModal ? std::string {} : nativeSceneOverlayMarkup(presentation_));
    return true;
}

bool GameRmlUi::rebuildModalHost()
{
    Rml::Element* modalHost = g_document
        ? g_document->GetElementById("rr-modal-host")
        : nullptr;
    if (!modalHost) {
        host_.log(
            PlatformLogLevel::Error,
            "Malformed RmlUi document lost persistent host #rr-modal-host.");
        return false;
    }
    Rml::ElementList oldScrollBodies;
    g_document->GetElementsByClassName(oldScrollBodies, "modal-scroll-body");
    if (!renderedModalId_.empty() && !oldScrollBodies.empty()) {
        modalScrollPositions_[renderedModalId_] =
            oldScrollBodies.front()->GetScrollTop();
    }

    const ModalPresentation* activeModal = findModal(presentation_.modals, openModalId_);
    if (activeModal) {
        std::string modalContent =
            "<div class=\"modal-head\"><h2 id=\"rr-modal-title\">"
            + Rml::StringUtilities::EncodeRml(activeModal->title) + "</h2>";
        if (activeModal->dismissible && activeModal->showClose) {
            modalContent +=
                "<button class=\"ghost rr-text-button\" data-ui-close-modal=\"1\" "
                "data-ui-focus-id=\"modal:close\"><span class=\"rr-button-label\">Close</span></button>";
        }
        modalContent += "</div><div class=\"modal-scroll-body\">"
            + withOpeningControllerLabels(
                syncSettingsControls(sanitizeRml(controllerResumeModalBody(
                    activeModal->bodyMarkup,
                    controllerResumeBlocked_,
                    controllerResumeConnected_))),
                controllerFamily_)
            + "</div>";
        modalHost->SetInnerRML(
            "<template src=\"rr-modal-shell\">" + modalContent + "</template>");
        Rml::Element* modalElement = g_document->GetElementById("rr-modal");
        if (!modalElement) {
            const std::filesystem::path templatePath =
                std::filesystem::path(assetRoot_) / "assets" / "ui" / "templates"
                / "rr-modal-shell.rml";
            host_.log(
                PlatformLogLevel::Error,
                "Malformed RmlUi modal template did not expand its #rr-modal target: "
                    + templatePath.string());
            return false;
        }
        std::string modalClass = "rr-modal-surface modal-" + activeModal->id;
        const std::string_view toneClass = modalToneCssClass(activeModal->tone);
        if (!toneClass.empty()) {
            modalClass += " ";
            modalClass += toneClass;
        }
        modalElement->SetAttribute("class", modalClass);
    } else {
        modalHost->SetInnerRML("");
    }
    renderedModalId_ = activeModal ? activeModal->id : std::string {};
    rr_rml_set_modal_open(openModalId_.empty() ? 0 : 1);
    if (activeModal) {
        const auto savedScroll = modalScrollPositions_.find(activeModal->id);
        if (savedScroll == modalScrollPositions_.end()) {
            return true;
        }
        g_context->Update();
        Rml::ElementList newScrollBodies;
        g_document->GetElementsByClassName(newScrollBodies, "modal-scroll-body");
        if (!newScrollBodies.empty()) {
            newScrollBodies.front()->SetScrollTop(savedScroll->second);
        }
    }
    return true;
}

bool GameRmlUi::rebuildPromptHost()
{
    Rml::Element* promptHost = g_document
        ? g_document->GetElementById("rr-controller-prompt-host")
        : nullptr;
    if (!promptHost) {
        host_.log(
            PlatformLogLevel::Error,
            "Malformed RmlUi document lost persistent host #rr-controller-prompt-host.");
        return false;
    }
    const ModalPresentation* activeModal = findModal(presentation_.modals, openModalId_);
    promptHost->SetInnerRML(
        controllerPresentationActive_ || presentation_.runtime.gameplayInputHelper
            ? inputPromptBar(
                presentation_,
                controllerFamily_,
                controllerPresentationActive_,
                activeModal != nullptr,
                activeModal == nullptr || activeModal->dismissible)
            : std::string {});
    return true;
}

bool GameRmlUi::rebuildPerformanceHost()
{
    Rml::Element* performanceHost = g_document
        ? g_document->GetElementById("rr-performance-host")
        : nullptr;
    if (!performanceHost) {
        host_.log(
            PlatformLogLevel::Error,
            "Malformed RmlUi document lost persistent host #rr-performance-host.");
        return false;
    }
    const ModalPresentation* activeModal = findModal(presentation_.modals, openModalId_);
    std::string performanceMarkup = "<div id=\"rr-performance-stats\"";
    if (activeModal || !performanceStatsVisible_ || performanceStatsHtml_.empty()) {
        performanceMarkup += " class=\"performance-hidden\"";
    }
    performanceMarkup += ">" + performanceStatsHtml_ + "</div>";
    performanceHost->SetInnerRML(performanceMarkup);
    return true;
}

void GameRmlUi::rebindAndRestoreFocus(bool restoreFocus)
{
    g_document->RemoveEventListener(Rml::EventId::Change, &g_settingsEventListener);
    g_document->AddEventListener(Rml::EventId::Change, &g_settingsEventListener);
    buttonBindings_ = bindLoadedButtons();
    g_context->Update();
    collectFocusTargets(!openModalId_.empty());
    const bool appliedPendingFocus = applyPendingFocusIfAvailable();
    const bool shouldRestoreFocus = restoreFocus
        || controllerPresentationActive_
        || !openModalId_.empty()
        || !focusedId_.empty();
    if (!appliedPendingFocus && shouldRestoreFocus && !g_focusTargets.empty()) {
        FocusTarget* target = findFocusTarget(focusedId_);
        if (!target && hasLastFocusCenter_) {
            target = nearestFocusTarget(lastFocusCenterX_, lastFocusCenterY_);
        }
        if (!target) {
            target = defaultFocusTarget();
        }
        applyControllerFocus(target, focusedId_, lastFocusCenterX_, lastFocusCenterY_, hasLastFocusCenter_);
    }
}

void GameRmlUi::refreshPersistentHosts(
    bool rebuildPanel,
    bool rebuildPanelShell,
    bool rebuildModal,
    bool rebuildOverlay,
    bool rebuildPrompt,
    bool rebuildPerformance)
{
    if (!initialized_ || !g_context || !g_document) {
        return;
    }

    const bool focusTreeChanged = rebuildPanel || rebuildModal;
    const bool bindingTreeChanged = focusTreeChanged || rebuildOverlay || rebuildPrompt;
    if (focusTreeChanged) {
        clearFocusTargets();
    }
    if (bindingTreeChanged) {
        pressedButton_ = nullptr;
        pressedButtonAtSeconds_ = 0.0;
        g_elementButtonBindings.clear();
    }

    bool valid = applyDocumentPresentationState();
    if (valid && rebuildPanel) {
        ++pendingPanelRebuilds_;
        valid = rebuildPanelHost(rebuildPanelShell);
        if (valid) {
            valid = applyDocumentPresentationState();
        }
    }
    if (valid && rebuildModal) {
        valid = rebuildModalHost();
    }
    if (valid && rebuildOverlay) {
        valid = rebuildOverlayHost();
    }
    if (valid && rebuildPrompt) {
        valid = rebuildPromptHost();
    }
    if (valid && rebuildPerformance) {
        valid = rebuildPerformanceHost();
    }
    if (!valid) {
        shutdown();
        return;
    }

    if (bindingTreeChanged || rebuildPerformance) {
        rebindAndRestoreFocus(focusTreeChanged);
    } else {
        g_context->Update();
    }
}

void GameRmlUi::rebuildDocument()
{
    refreshPersistentHosts(true, true, true, true, true, true);
}

void GameRmlUi::shutdown()
{
    if (g_context) {
        if (g_document) {
            clearFocusTargets();
            g_document->RemoveEventListener(Rml::EventId::Change, &g_settingsEventListener);
            g_context->UnloadDocument(g_document);
            g_document = nullptr;
        }
        Rml::RemoveContext("rocket-ui");
        g_context = nullptr;
    }
    if (rmlInitialized_) {
        Rml::Shutdown();
        rmlInitialized_ = false;
    }
    if (renderHostInitialized_) {
        renderHost_.shutdown();
        renderHostInitialized_ = false;
    }
    g_elementButtonBindings.clear();
    g_focusTargets.clear();
    g_focusTargetsModalScoped = false;
    buttonBindings_.clear();
    pressedButton_ = nullptr;
    pressedButtonAtSeconds_ = 0.0;
    rr_rml_set_enabled(0);
    uiBridge_.setModalOpen(false);
    uiBridge_.setControllerPresentation(false, ControllerFamily::Generic);
    uiBridge_.setControllerFocusVisible(false);
    uiBridge_.setControllerResumeBlocked(false, false);
    uiBridge_.setRmlUiEnabled(false);
    pendingDocumentRebuilds_ = 0;
    pendingPanelRebuilds_ = 0;
    pendingHudPatches_ = 0;
    uiDiagnostics_ = {};
    presentation_ = {};
    openModalId_.clear();
    renderedModalId_.clear();
    modalStack_.clear();
    modalFocusStack_.clear();
    modalScrollPositions_.clear();
    focusedId_.clear();
    pendingFocusId_.clear();
    modalReturnFocusId_.clear();
    performanceStatsHtml_.clear();
    hasLastFocusCenter_ = false;
    controllerPresentationActive_ = false;
    controllerFocusVisible_ = false;
    controllerResumeBlocked_ = false;
    controllerResumeConnected_ = false;
    performanceStatsVisible_ = false;
    controllerFamily_ = ControllerFamily::Generic;
    panelMode_ = RmlPanelMode::Control;
    layoutViewportWidth_ = 0;
    layoutViewportHeight_ = 0;
    externalRcss_.clear();
    initialized_ = false;
    actionHandler_ = {};
    if (g_preferences == &preferences_) g_preferences = nullptr;
    if (g_host == &host_) g_host = nullptr;
    if (g_uiBridge == &uiBridge_) g_uiBridge = nullptr;
}

} // namespace rocket
