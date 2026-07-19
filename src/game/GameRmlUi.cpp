#include "game/GameRmlUi.h"
#include "game/IRmlRenderHost.h"

#include <utility>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/EventListener.h>
#include <RmlUi/Core/Log.h>
#include <RmlUi/Core/SystemInterface.h>
#include <RmlUi/Core/StringUtilities.h>

#include "FontSource.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace rocket {
namespace {

constexpr int kPanelWidth = 448;
constexpr int kPanelInset = 16;
constexpr int kPhaseBoardFrameWidth = 736;
constexpr int kPhaseContentLaneWidth = 704;
constexpr int kPhaseLaneInset = 16;
constexpr int kPhaseCardSlotWidth = 170;
constexpr int kPhaseCardGap = 8;
constexpr int kPhaseCommonButtonWidth = 132;
constexpr int kPhaseCommonChipSlotWidth = 132;

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

void rr_rml_dom_set_controller_presentation(int active, int family) { if (g_uiBridge) g_uiBridge->setControllerPresentation(active != 0, static_cast<ControllerFamily>(family)); }
void rr_rml_dom_set_controller_focus_visible(int visible) { if (g_uiBridge) g_uiBridge->setControllerFocusVisible(visible != 0); }
void rr_rml_dom_set_controller_resume_blocked(int blocked, int connected) { if (g_uiBridge) g_uiBridge->setControllerResumeBlocked(blocked != 0, connected != 0); }
int rr_rml_dom_navigate(int direction) { return g_uiBridge && g_uiBridge->navigate(static_cast<UiDirection>(direction)) ? 1 : 0; }
int rr_rml_dom_activate() { return g_uiBridge && g_uiBridge->activate() ? 1 : 0; }
int rr_rml_dom_cancel() { return g_uiBridge && g_uiBridge->cancel() ? 1 : 0; }
int rr_rml_dom_scroll(double amount) { return g_uiBridge && g_uiBridge->scroll(amount) ? 1 : 0; }
int rr_rml_dom_modal_open() { return g_uiBridge && g_uiBridge->modalOpen() ? 1 : 0; }
int rr_rml_dom_open_modal(const char* value) { return g_uiBridge && g_uiBridge->openModal(value ? value : "") ? 1 : 0; }
void rr_rml_dom_close_modal() { if (g_uiBridge) g_uiBridge->closeModal(); }
int rr_rml_dom_focused_id(char* buffer, int capacity)
{
    if (!buffer || capacity <= 0) return 0;
    const std::string value = g_uiBridge ? g_uiBridge->focusedId() : std::string {};
    const int length = std::min(capacity - 1, static_cast<int>(value.size()));
    std::copy_n(value.data(), length, buffer);
    buffer[length] = '\0';
    return length;
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
int rr_rml_help_topic_hidden(const char* value)
{
    const AppPreferences preferences = currentPreferences();
    return preferences.helpDisabled || std::find(preferences.dismissedHelpTopics.begin(), preferences.dismissedHelpTopics.end(), value ? value : "") != preferences.dismissedHelpTopics.end();
}
void rr_rml_dismiss_help_topic(const char* value)
{
    if (!value || !*value) return;
    AppPreferences preferences = currentPreferences();
    if (std::find(preferences.dismissedHelpTopics.begin(), preferences.dismissedHelpTopics.end(), value) == preferences.dismissedHelpTopics.end()) preferences.dismissedHelpTopics.emplace_back(value);
    storePreferences(std::move(preferences));
}

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


struct ModalTemplate {
    std::string id;
    std::string title;
    std::string body;
    std::string closeAction;
    bool autoOpen = false;
    bool dismissible = true;
    bool showClose = true;
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

std::string attributeValue(std::string_view tag, std::string_view name)
{
    const std::string needle = std::string(name) + "=\"";
    const std::size_t start = tag.find(needle);
    if (start == std::string_view::npos) {
        return {};
    }
    const std::size_t valueStart = start + needle.size();
    const std::size_t valueEnd = tag.find('"', valueStart);
    if (valueEnd == std::string_view::npos) {
        return {};
    }
    return std::string(tag.substr(valueStart, valueEnd - valueStart));
}

std::vector<ModalTemplate> extractModals(const std::string& html)
{
    std::vector<ModalTemplate> modals;
    std::size_t search = 0;
    while (true) {
        const std::size_t open = html.find("<template", search);
        if (open == std::string::npos) {
            break;
        }
        const std::size_t tagEnd = html.find('>', open);
        const std::size_t close = tagEnd == std::string::npos ? std::string::npos : html.find("</template>", tagEnd + 1);
        if (tagEnd == std::string::npos || close == std::string::npos) {
            break;
        }

        const std::string_view tag(html.data() + open, tagEnd - open + 1);
        ModalTemplate modal;
        modal.id = attributeValue(tag, "data-modal");
        modal.title = attributeValue(tag, "data-title");
        modal.autoOpen = attributeValue(tag, "data-auto-modal") == "1";
        modal.dismissible = attributeValue(tag, "data-modal-dismissible") != "0";
        modal.showClose = attributeValue(tag, "data-modal-hide-close") != "1";
        modal.closeAction = attributeValue(tag, "data-modal-close-action");
        modal.body = html.substr(tagEnd + 1, close - tagEnd - 1);
        if (!modal.id.empty()) {
            modals.push_back(std::move(modal));
        }
        search = close + std::string_view("</template>").size();
    }
    return modals;
}

std::string removeTemplates(std::string html)
{
    std::size_t search = 0;
    while (true) {
        const std::size_t open = html.find("<template", search);
        if (open == std::string::npos) {
            break;
        }
        const std::size_t close = html.find("</template>", open);
        if (close == std::string::npos) {
            html.erase(open);
            break;
        }
        html.erase(open, close + std::string_view("</template>").size() - open);
        search = open;
    }
    return html;
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

std::string removeDismissedHelpCards(std::string html)
{
    std::size_t search = 0;
    while ((search = html.find("data-help-topic", search)) != std::string::npos) {
        const std::size_t tagStart = html.rfind('<', search);
        const std::size_t tagEnd = html.find('>', search);
        if (tagStart == std::string::npos || tagEnd == std::string::npos || html.compare(tagStart, 2, "</") == 0) {
            search += std::string_view("data-help-topic").size();
            continue;
        }

        const std::string_view tag(html.data() + tagStart, tagEnd - tagStart + 1);
        const std::string topic = attributeValue(tag, "data-help-topic");
        if (topic.empty() || rr_rml_help_topic_hidden(topic.c_str()) == 0) {
            search = tagEnd + 1;
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
        } else {
            html.erase(tagStart, closeStart + closeTag.size() - tagStart);
        }
        search = tagStart;
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
    html = removeDismissedHelpCards(std::move(html));

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
        "platform_default", "smooth60", "balanced", "battery30", "display"
    };
    return options[std::clamp(rr_rml_frame_limit_preference(), 0, 4)];
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
        html.replace(tagEnd + 1, closeStart - tagEnd - 1, std::string(enabled ? enabledLabel : disabledLabel));
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
    if (html.find("data-debug-tools-toggle") == std::string::npos) {
        return html;
    }

    const std::string needle = "data-debug-tools-toggle";
    const std::size_t attrStart = html.find(needle);
    if (attrStart == std::string::npos) {
        return html;
    }
    const std::size_t tagStart = html.rfind('<', attrStart);
    const std::size_t tagEnd = html.find('>', attrStart);
    const std::size_t closeStart = html.find("</button>", tagEnd == std::string::npos ? attrStart : tagEnd);
    if (tagStart != std::string::npos && tagEnd != std::string::npos && closeStart != std::string::npos) {
        const bool enabled = rr_rml_debug_tools_enabled() != 0;
        const std::string label = enabled ? "Hide debug tools" : "Show debug tools";
        html.replace(tagEnd + 1, closeStart - tagEnd - 1, label);
    }
    return html;
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
    if (html.find("data-help-toggle") == std::string::npos) {
        return html;
    }

    const std::string needle = "data-help-toggle";
    const std::size_t attrStart = html.find(needle);
    if (attrStart == std::string::npos) {
        return html;
    }
    const std::size_t tagEnd = html.find('>', attrStart);
    const std::size_t closeStart = html.find("</button>", tagEnd == std::string::npos ? attrStart : tagEnd);
    if (tagEnd != std::string::npos && closeStart != std::string::npos) {
        const bool enabled = rr_rml_help_disabled() == 0;
        const std::string label = enabled ? "Hide mission help" : "Show mission help";
        html.replace(tagEnd + 1, closeStart - tagEnd - 1, label);
    }
    return html;
}

std::string syncCurrentCameraShakeToggle(std::string html)
{
    if (html.find("data-camera-shake-toggle") == std::string::npos) {
        return html;
    }

    const std::string needle = "data-camera-shake-toggle";
    const std::size_t attrStart = html.find(needle);
    if (attrStart == std::string::npos) {
        return html;
    }
    const std::size_t tagEnd = html.find('>', attrStart);
    const std::size_t closeStart = html.find("</button>", tagEnd == std::string::npos ? attrStart : tagEnd);
    if (tagEnd != std::string::npos && closeStart != std::string::npos) {
        const bool disabled = rr_rml_camera_shake_disabled() != 0;
        const std::string label = disabled ? "Enable camera shake" : "Disable camera shake";
        html.replace(tagEnd + 1, closeStart - tagEnd - 1, label);
    }
    return html;
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

std::string syncSettingsControls(std::string html)
{
    return syncDesktopFullscreenToggle(syncCurrentControllerPreferences(syncCurrentCameraShakeToggle(syncCurrentHelpToggle(
        syncCurrentPerformanceStatsToggle(syncCurrentDebugToolsToggle(
            selectCurrentKeyboardDrillMode(selectCurrentGameSpeed(selectCurrentFrameLimit(selectCurrentResolution(std::move(html)))))))))));
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

std::vector<RmlButtonBinding> extractButtonBindings(const std::string& html)
{
    std::vector<RmlButtonBinding> bindings;
    std::size_t pos = 0;
    while (true) {
        const std::size_t tagStart = html.find("<button", pos);
        if (tagStart == std::string::npos) {
            break;
        }
        const std::size_t tagEnd = html.find('>', tagStart);
        if (tagEnd == std::string::npos) {
            break;
        }
        const std::size_t closeStart = html.find("</button>", tagEnd + 1);
        if (closeStart == std::string::npos) {
            pos = tagEnd + 1;
            continue;
        }

        const std::string_view tag(html.data() + tagStart, tagEnd - tagStart + 1);
        RmlButtonBinding binding;
        binding.focusId = attributeValue(tag, "data-ui-focus-id");
        binding.label = textFromMarkup(std::string_view(html.data() + tagEnd + 1, closeStart - tagEnd - 1));
        binding.action = attributeValue(tag, "data-rr-action");
        binding.modal = attributeValue(tag, "data-ui-modal");
        binding.helpDismiss = attributeValue(tag, "data-help-dismiss");
        binding.close = !attributeValue(tag, "data-ui-close-modal").empty();
        binding.helpToggle = !attributeValue(tag, "data-help-toggle").empty();
        binding.cameraShakeToggle = !attributeValue(tag, "data-camera-shake-toggle").empty();
        binding.desktopFullscreenToggle = !attributeValue(tag, "data-desktop-fullscreen-toggle").empty();
        binding.debugToolsToggle = !attributeValue(tag, "data-debug-tools-toggle").empty();
        binding.performanceStatsToggle = !attributeValue(tag, "data-performance-stats-toggle").empty();
        if (!attributeValue(tag, "data-controller-invert-toggle").empty()) {
            binding.controllerSetting = "invertFlightY";
        } else if (!attributeValue(tag, "data-controller-swap-toggle").empty()) {
            binding.controllerSetting = "swapConfirmCancel";
        } else if (!attributeValue(tag, "data-controller-vibration-toggle").empty()) {
            binding.controllerSetting = "vibrationEnabled";
        }
        if (binding.focusId.empty()) {
            if (!binding.action.empty()) {
                binding.focusId = "action:" + binding.action;
            } else if (!binding.modal.empty()) {
                binding.focusId = "modal:" + binding.modal;
            } else if (binding.close) {
                binding.focusId = "modal:close";
            } else if (!binding.helpDismiss.empty()) {
                binding.focusId = "help-dismiss:" + binding.helpDismiss;
            } else if (!binding.controllerSetting.empty()) {
                binding.focusId = "setting:" + binding.controllerSetting;
            }
        }
        bindings.push_back(std::move(binding));
        pos = closeStart + std::string_view("</button>").size();
    }
    return bindings;
}

RmlPanelMode panelModeForHtml(std::string_view html)
{
    if (html.find("data-panel-mode=\"title\"") != std::string_view::npos) {
        return RmlPanelMode::Title;
    }
    if (html.find("data-panel-mode=\"mining-fullscreen\"") != std::string_view::npos) {
        return RmlPanelMode::MiningFullscreen;
    }
    if (html.find("data-panel-mode=\"story-briefing\"") != std::string_view::npos) {
        return RmlPanelMode::StoryBriefing;
    }
    if (html.find("data-panel-mode=\"arrival-fanfare\"") != std::string_view::npos) {
        return RmlPanelMode::ArrivalFanfare;
    }
    if (html.find("data-panel-mode=\"mission-stamp\"") != std::string_view::npos) {
        return RmlPanelMode::MissionStamp;
    }
    if (html.find("data-panel-mode=\"phase-board\"") != std::string_view::npos) {
        return RmlPanelMode::PhaseBoard;
    }
    return RmlPanelMode::Control;
}

bool panelUsesTitle(RmlPanelMode mode)
{
    return mode == RmlPanelMode::Title;
}

bool panelUsesStoryBriefing(RmlPanelMode mode)
{
    return mode == RmlPanelMode::StoryBriefing;
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

bool panelUsesSurfaceOps(std::string_view html)
{
    return html.find("surface-ops-screen") != std::string_view::npos;
}

bool panelUsesDroneOps(std::string_view html)
{
    return html.find("phase-board-drone-ops") != std::string_view::npos;
}

bool panelUsesNavigation(std::string_view html)
{
    return html.find("phase-board-navigation") != std::string_view::npos;
}

bool panelUsesDraftRoom(std::string_view html)
{
    return html.find("phase-board-draft-room") != std::string_view::npos;
}

enum class NativeSceneOverlayMode {
    None,
    PreflightLaunch,
    TelemetryLegend,
};

NativeSceneOverlayMode nativeSceneOverlayMode(std::string_view html)
{
    if (html.find("data-preflight-launch") != std::string_view::npos) {
        return NativeSceneOverlayMode::PreflightLaunch;
    }

    const bool flight = html.find("class=\"cockpit-hud flight-hud\"") != std::string_view::npos;
    const bool debrief = html.find("phase-board-results") != std::string_view::npos;
    const bool flyby = html.find("data-flyby-run") != std::string_view::npos;
    const bool orbit = html.find("data-orbit-run") != std::string_view::npos;
    return (flight || debrief) && !flyby && !orbit
        ? NativeSceneOverlayMode::TelemetryLegend
        : NativeSceneOverlayMode::None;
}

bool nativePreflightLaunchReady(std::string_view html)
{
    return html.find("data-preflight-ready=\"1\"") != std::string_view::npos;
}

std::string nativeSceneOverlayMarkup(std::string_view panelHtml)
{
    switch (nativeSceneOverlayMode(panelHtml)) {
    case NativeSceneOverlayMode::PreflightLaunch: {
        const bool ready = nativePreflightLaunchReady(panelHtml);
        std::string markup = "<button id=\"rr-scene-launch-control\" class=\"native-scene-launch-control\" "
            "data-rr-action=\"start_launch\" data-ui-focus-id=\"action:start_launch\"";
        if (!ready) {
            markup += " disabled=\"1\"";
        }
        markup += ">";
        markup += ready ? "Launch" : "Securing Drone";
        markup += "</button>";
        return markup;
    }
    case NativeSceneOverlayMode::TelemetryLegend:
        return R"(<div id="rr-telemetry-chart-legend" class="native-telemetry-chart-legend">
<div class="native-telemetry-legend-chip warn"><span class="swatch"></span><strong>Warning</strong></div>
<div class="native-telemetry-legend-chip heat"><span class="swatch"></span><strong>Heat</strong></div>
<div class="native-telemetry-legend-chip threshold"><span class="swatch"></span><strong>Caution line</strong></div>
</div>)";
    case NativeSceneOverlayMode::None:
    default:
        return {};
    }
}

bool samePanelStructure(std::string_view lhs, std::string_view rhs)
{
    return panelModeForHtml(lhs) == panelModeForHtml(rhs)
        && panelUsesSurfaceOps(lhs) == panelUsesSurfaceOps(rhs)
        && panelUsesDroneOps(lhs) == panelUsesDroneOps(rhs)
        && panelUsesNavigation(lhs) == panelUsesNavigation(rhs)
        && panelUsesDraftRoom(lhs) == panelUsesDraftRoom(rhs)
        && nativeSceneOverlayMode(lhs) == nativeSceneOverlayMode(rhs)
        && nativePreflightLaunchReady(lhs) == nativePreflightLaunchReady(rhs);
}

Rml::Rectanglei panelBounds(RmlPanelMode mode)
{
    const int viewportWidth = rr_rml_viewport_width();
    const int viewportHeight = rr_rml_viewport_height();
    if (mode == RmlPanelMode::Title || mode == RmlPanelMode::StoryBriefing || mode == RmlPanelMode::MiningFullscreen) {
        return Rml::Rectanglei::FromPositionSize({0, 0}, {std::max(1, viewportWidth), std::max(1, viewportHeight)});
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
    const bool phaseBoard = panelUsesPhaseBoard(mode);
    const int left = phaseBoard ? 16 : kPanelInset;
    const int top = phaseBoard ? 16 : kPanelInset;
    const int width = phaseBoard ? std::clamp(viewportWidth - 64, 560, kPhaseBoardFrameWidth) : kPanelWidth + 34;
    const int height = phaseBoard
        ? std::max(360, viewportHeight - 64)
        : std::max(1, viewportHeight - 56);
    return Rml::Rectanglei::FromPositionSize({left, top}, {width, height});
}

Rml::Rectanglei expandedPanelClip(RmlPanelMode mode)
{
    const Rml::Rectanglei bounds = panelBounds(mode);
    if (mode == RmlPanelMode::Title || mode == RmlPanelMode::StoryBriefing || mode == RmlPanelMode::MiningFullscreen) {
        return bounds;
    }
    return Rml::Rectanglei::FromPositionSize(
        {std::max(0, bounds.Left() - 4), std::max(0, bounds.Top() - 4)},
        {bounds.Width() + 40, bounds.Height() + 40});
}

const ModalTemplate* findModal(const std::vector<ModalTemplate>& modals, std::string_view id)
{
    const auto it = std::find_if(modals.begin(), modals.end(), [id](const ModalTemplate& modal) {
        return modal.id == id;
    });
    return it == modals.end() ? nullptr : &*it;
}

std::string panelRcss(RmlPanelMode mode)
{
    const Rml::Rectanglei bounds = panelBounds(mode);
    const int viewportWidth = std::max(1, rr_rml_viewport_width());
    const int viewportHeight = std::max(1, rr_rml_viewport_height());
    const int panelWidth = bounds.Width();
    const int panelHeight = std::max(180, bounds.Height());
    const int modalGutter = 16;
    const int modalDefaultWidth = std::max(1, std::min(640, viewportWidth - modalGutter * 2));
    const int modalInventoryWidth = std::max(1, std::min(760, viewportWidth - modalGutter * 2));
    const int modalMapWidth = std::max(1, std::min(920, viewportWidth - modalGutter * 2));
    const int modalTallHeight = std::max(1, std::min(viewportHeight - modalGutter * 2, (viewportHeight * 88) / 100));
    const int modalTallTop = std::max(modalGutter, (viewportHeight - modalTallHeight) / 2);
    const int modalDefaultLeft = std::max(modalGutter, (viewportWidth - modalDefaultWidth) / 2);
    const int modalInventoryLeft = std::max(modalGutter, (viewportWidth - modalInventoryWidth) / 2);
    const int modalMapLeft = std::max(modalGutter, (viewportWidth - modalMapWidth) / 2);
    const int modalBriefHeight = std::max(1, std::min(340, viewportHeight - modalGutter * 2));
    const int modalBriefTop = std::max(modalGutter, (viewportHeight - modalBriefHeight) / 2);
    const int modalMissionHeight = std::max(1, std::min(300, viewportHeight - modalGutter * 2));
    const int modalMissionTop = std::max(modalGutter, (viewportHeight - modalMissionHeight) / 2);
    const int modalNewGameWidth = std::max(1, std::min(560, viewportWidth - modalGutter * 2));
    const int modalNewGameHeight = std::max(1, std::min(230, viewportHeight - modalGutter * 2));
    const int modalNewGameLeft = std::max(modalGutter, (viewportWidth - modalNewGameWidth) / 2);
    const int modalNewGameTop = std::max(modalGutter, (viewportHeight - modalNewGameHeight) / 2);
    const int modalActivityWidth = std::max(1, std::min(560, viewportWidth - modalGutter * 2));
    const int modalActivityHeight = std::max(1, std::min(320, viewportHeight - modalGutter * 2));
    const int modalActivityLeft = std::max(modalGutter, (viewportWidth - modalActivityWidth) / 2);
    const int modalActivityTop = std::max(modalGutter, (viewportHeight - modalActivityHeight) / 2);
    const int arrivalTitleSize = mode == RmlPanelMode::MissionStamp ? (panelWidth < 420 ? 31 : 40) : (panelWidth < 420 ? 34 : 46);
    const int left = bounds.Left();
    const int top = bounds.Top();
    const bool compactMining = panelWidth < 1220;
    const int miningInset = compactMining ? 10 : 18;
    const int miningRailWidth = std::max(1, panelWidth - miningInset * 2);
    const int miningTopHeight = compactMining ? 118 : 62;
    const int miningObjectiveTop = miningInset + miningTopHeight + 12;
    const int miningBottomHeight = compactMining ? 164 : 126;
    const int miningBottomTop = std::max(miningTopHeight + 18, panelHeight - miningBottomHeight - miningInset);
    const int miningUtilityWidth = compactMining ? 104 : 108;
    const int miningTitleWidth = compactMining ? std::max(1, miningRailWidth - miningUtilityWidth - 12) : 246;
    const int miningVitalWidth = compactMining ? 78 : 86;
    const int miningVitalGap = 5;
    const int miningVitalsWidth = compactMining ? miningRailWidth : (miningVitalWidth * 6 + miningVitalGap * 5 + 84);
    const int miningVitalsLeft = compactMining ? 0 : std::max(0, (miningRailWidth - miningVitalsWidth) / 2);
    const int miningPlayfieldTop = miningTopHeight + miningInset + (compactMining ? 30 : 40);
    const int miningPayloadWidth = compactMining ? miningRailWidth : std::max(260, miningRailWidth - 378);
    const int miningActionWidth = compactMining ? 126 : 154;
    const int miningCommandWidth = compactMining ? miningRailWidth : std::min(380, miningRailWidth);
    const int miningCommandLeft = compactMining ? 0 : std::max(0, miningRailWidth - miningCommandWidth - 4);
    const int miningPrimaryActionWidth = compactMining ? std::min(180, miningRailWidth) : 170;
    const int miningPrimaryActionLeft = std::max(0, (miningRailWidth - miningPrimaryActionWidth) / 2);
    const int miningPrimaryActionTop = std::max(8, miningBottomHeight - 56);
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
    const float nativeSceneUnit = std::max(1.0F, static_cast<float>(std::min(viewportWidth, viewportHeight)) * 0.46F);
    const int nativeSceneCenterX = viewportWidth / 2;
    const int nativeSceneCenterY = viewportHeight / 2;
    const int nativeLaunchCenterX = nativeSceneCenterX - static_cast<int>(0.18F * nativeSceneUnit);
    const int nativeLaunchCenterY = nativeSceneCenterY + static_cast<int>(0.50F * nativeSceneUnit);
    const int nativeTelemetryLeft = nativeSceneCenterX + static_cast<int>(0.18F * nativeSceneUnit);
    const int nativeTelemetryRight = nativeSceneCenterX + static_cast<int>(0.94F * nativeSceneUnit);
    const int nativeTelemetryTop = nativeSceneCenterY + static_cast<int>(0.58F * nativeSceneUnit);
    const int nativeLegendTop = std::max(8, nativeTelemetryTop - 34);
    const int nativeTelemetryWidth = std::max(1, nativeTelemetryRight - nativeTelemetryLeft);

    return R"(
body {
    position: absolute;
    left: 0px;
    top: 0px;
    width: )" + std::to_string(viewportWidth) + R"(px;
    height: )" + std::to_string(viewportHeight) + R"(px;
    overflow: hidden;
    color: #edf4f8;
    font-family: rmlui-debugger-font;
    font-size: 14px;
}
div, p, h1, h2, h3, aside {
    display: block;
}
span, strong, small {
    display: block;
}
scrollbarvertical {
    display: block;
    width: 10px;
    background-color: #0b1118;
    border-width: 0px;
}
scrollbarvertical slidertrack {
    display: block;
    width: 10px;
    background-color: #111c26;
}
scrollbarvertical sliderbar {
    display: block;
    width: 8px;
    min-height: 30px;
    margin-left: 1px;
    background-color: #4e6b80;
    border-radius: 4px;
}
scrollbarvertical sliderarrowdec,
scrollbarvertical sliderarrowinc,
scrollbarhorizontal,
scrollbarcorner {
    display: none;
    visibility: hidden;
    width: 0px;
    height: 0px;
    background-color: transparent;
    border-width: 0px;
}
#rr-panel {
    position: absolute;
    left: )" + std::to_string(left) + R"(px;
    top: )" + std::to_string(top) + R"(px;
    width: )" + std::to_string(panelWidth) + R"(px;
    height: )" + std::to_string(panelHeight) + R"(px;
    overflow-x: hidden;
    overflow-y: auto;
    padding: 14px;
    background-color: #0b1118;
    border-width: 1px;
    border-color: #4e6b80;
    border-radius: 8px;
}
#rr-scene-launch-control {
    position: fixed;
    z-index: 8;
    left: )" + std::to_string(nativeLaunchCenterX - 98) + R"(px;
    top: )" + std::to_string(nativeLaunchCenterY - 31) + R"(px;
    width: 196px;
    height: 62px;
    padding: 0px 16px;
    color: #edfdf4;
    background-color: #1d6e53;
    border-width: 1px;
    border-color: #7ae4ad;
    border-radius: 9px;
    font-size: 20px;
    font-weight: bold;
    text-align: center;
    text-transform: uppercase;
}
#rr-scene-launch-control:disabled {
    color: #9eafb4;
    background-color: #13252a;
    border-color: #34566a;
}
#rr-telemetry-chart-legend {
    position: fixed;
    z-index: 7;
    left: )" + std::to_string(nativeTelemetryLeft) + R"(px;
    top: )" + std::to_string(nativeLegendTop) + R"(px;
    width: )" + std::to_string(nativeTelemetryWidth) + R"(px;
    display: flex;
    flex-direction: row;
    gap: 6px;
    pointer-events: none;
}
.native-telemetry-legend-chip {
    box-sizing: border-box;
    flex: 1 1 0px;
    min-width: 0px;
    height: 24px;
    padding: 3px 4px;
    background-color: rgba(9, 16, 24, 0.82);
    border-width: 1px;
    border-color: rgba(137, 178, 211, 0.26);
    border-radius: 6px;
}
.native-telemetry-legend-chip .swatch {
    display: inline-block;
    width: 10px;
    height: 10px;
    margin-right: 4px;
    vertical-align: middle;
    border-width: 1px;
    border-color: rgba(255, 255, 255, 0.18);
    border-radius: 5px;
}
.native-telemetry-legend-chip strong {
    display: inline-block;
    color: #e7f5f8;
    font-size: 9px;
    line-height: 14px;
    vertical-align: middle;
    text-transform: uppercase;
}
.native-telemetry-legend-chip.warn {
    border-color: rgba(97, 220, 255, 0.40);
}
.native-telemetry-legend-chip.warn .swatch {
    background-color: #55d9ff;
}
.native-telemetry-legend-chip.heat {
    border-color: rgba(255, 196, 96, 0.40);
}
.native-telemetry-legend-chip.heat .swatch {
    background-color: #ffc45b;
}
.native-telemetry-legend-chip.threshold {
    border-color: rgba(255, 209, 102, 0.40);
}
.native-telemetry-legend-chip.threshold .swatch {
    height: 3px;
    margin-top: 4px;
    margin-bottom: 3px;
    background-color: #ffd166;
}
#rr-panel.title-screen-panel-mode {
    left: 0px;
    top: 0px;
    width: )" + std::to_string(panelWidth) + R"(px;
    height: )" + std::to_string(panelHeight) + R"(px;
    overflow: hidden;
    padding: 0px;
    background-color: transparent;
    border-width: 0px;
    border-radius: 0px;
}
#rr-panel.story-briefing-panel-mode {
    left: 0px;
    top: 0px;
    width: )" + std::to_string(panelWidth) + R"(px;
    height: )" + std::to_string(panelHeight) + R"(px;
    overflow: hidden;
    padding: 0px;
    background-color: transparent;
    border-width: 0px;
    border-radius: 0px;
}
.story-briefing {
    position: relative;
    width: )" + std::to_string(panelWidth) + R"(px;
    height: )" + std::to_string(panelHeight) + R"(px;
    overflow: hidden;
    background-color: rgba(2, 7, 13, 0.46);
}
.story-vignette {
    position: absolute;
    left: 0px;
    top: 0px;
    width: 100%;
    height: 100%;
    background-color: rgba(2, 7, 13, 0.28);
}
.story-introduction .story-vignette {
    background-color: rgba(2, 7, 13, 0.12);
}
.story-content {
    box-sizing: border-box;
    position: absolute;
    left: )" + std::to_string(std::max(28, panelWidth / 10)) + R"(px;
    top: )" + std::to_string(std::max(34, panelHeight / 10)) + R"(px;
    width: )" + std::to_string(std::max(640, panelWidth - std::max(56, panelWidth / 5))) + R"(px;
    padding: 24px 30px;
    background-color: rgba(3, 12, 20, 0.88);
    border-left-width: 3px;
    border-color: #63e5ff;
}
.story-introduction .story-content {
    left: )" + std::to_string(storyContentLeft) + R"(px;
    top: )" + std::to_string(std::max(34, panelHeight / 12)) + R"(px;
    width: )" + std::to_string(storyIntroductionWidth) + R"(px;
    padding: 24px 28px;
    background-color: rgba(3, 10, 17, 0.94);
}
body.controller-connected .story-introduction .story-content {
    width: )" + std::to_string(storyIntroductionConnectedWidth) + R"(px;
}
.story-straylight .story-content { border-color: #ffd166; }
.story-kicker {
    display: block;
    color: #84dff2;
    font-size: 12px;
    font-weight: bold;
    letter-spacing: 2px;
}
.story-content h1 {
    margin-top: 8px;
    margin-bottom: 8px;
    color: #f4fbff;
    font-size: 54px;
    line-height: 1;
    letter-spacing: 3px;
}
.story-straylight .story-content h1 { color: #ffd166; font-effect: glow(2px #6f5519); }
.story-lead {
    width: 82%;
    color: #d7e9ef;
    font-size: 18px;
    line-height: 1.35;
}
.story-introduction .story-content h1 {
    margin-top: 9px;
    margin-bottom: 18px;
    color: #ffd166;
    font-size: 46px;
    line-height: 1;
    letter-spacing: 2px;
}
.story-exposition {
    width: 100%;
}
.story-exposition p {
    margin-top: 0px;
    margin-bottom: 13px;
    color: #d7e9ef;
    font-size: 16px;
    line-height: 1.45;
}
.story-exposition .story-directive {
    margin-top: 17px;
    margin-bottom: 0px;
    color: #fff0b7;
    font-weight: bold;
}
.story-introduction .story-action {
    width: 220px;
    margin-top: 18px;
}
.story-beats {
    display: flex;
    flex-direction: row;
    width: 100%;
    margin-top: 22px;
}
.story-beats article {
    box-sizing: border-box;
    width: 31%;
    min-height: 132px;
    margin-right: 2%;
    padding: 14px;
    background-color: rgba(8, 27, 39, 0.86);
    border-top-width: 1px;
    border-color: rgba(99, 229, 255, 0.38);
}
.story-beats article span { color: #63e5ff; font-size: 11px; font-weight: bold; }
.story-beats article h2 { margin-top: 5px; margin-bottom: 6px; color: #f1f8fa; font-size: 18px; }
.story-beats article p { color: #b9ced6; font-size: 13px; line-height: 1.35; }
.story-action {
    width: 280px;
    min-height: 48px;
    margin-top: 22px;
    font-size: 15px;
    font-weight: bold;
}
.opening-controls {
    box-sizing: border-box;
    position: absolute;
    right: )" + std::to_string(openingControlsRight) + R"(px;
    bottom: )" + std::to_string(openingControlsBottom) + R"(px;
    display: flex;
    flex-direction: column;
    width: )" + std::to_string(openingControlsWidth) + R"(px;
    padding: 12px 14px 14px;
    background-color: rgba(3, 12, 20, 0.90);
    border-left-width: 2px;
    border-color: rgba(99, 229, 255, 0.58);
}
.opening-controls-kicker {
    width: 100%;
    margin-bottom: 8px;
    color: #84dff2;
    font-size: 10px;
    font-weight: bold;
    letter-spacing: 1px;
}
.opening-control-cards {
    display: flex;
    flex-direction: row;
    width: 100%;
}
.opening-control-card {
    box-sizing: border-box;
    width: 100%;
    min-height: 218px;
    margin-right: 2%;
    padding: 10px 12px;
    border-top-width: 1px;
    border-color: rgba(99, 229, 255, 0.38);
    background-color: rgba(8, 27, 39, 0.82);
}
.opening-control-title {
    display: block;
    color: #84dff2;
    font-size: 11px;
    font-weight: bold;
    text-transform: uppercase;
}
.opening-control-row {
    width: 100%;
    margin-top: 8px;
}
.opening-control-row > strong {
    color: #fff0b7;
    font-size: 10px;
    text-transform: uppercase;
}
.opening-control-row p {
    margin-top: 2px;
    margin-bottom: 0px;
    color: #c5d7de;
    font-size: 10px;
    line-height: 1.22;
}
.opening-control-row p strong { display: inline; color: #fff0b7; }
.opening-controller-controls { display: none; }
body.controller-connected .opening-keyboard-controls { width: 48%; }
body.controller-connected .opening-controller-controls { display: block; width: 48%; }
body.controller-connected .opening-controls { width: )" + std::to_string(openingControlsConnectedWidth) + R"(px; }
.title-screen {
    position: relative;
    width: )" + std::to_string(panelWidth) + R"(px;
    height: )" + std::to_string(panelHeight) + R"(px;
    overflow: hidden;
}
.title-content {
    box-sizing: border-box;
    position: absolute;
    left: )" + std::to_string(titleContentLeft) + R"(px;
    top: )" + std::to_string(titleContentTop) + R"(px;
    width: )" + std::to_string(titleContentWidth) + R"(px;
    min-height: )" + std::string(compactTitle ? "520" : "610") + R"(px;
    padding: )" + std::string(compactTitle ? "18px 22px" : "26px 34px") + R"(;
    text-align: center;
    background-color: rgba(3, 10, 16, 0.76);
    border-width: 1px;
    border-color: rgba(71, 220, 255, 0.28);
    border-radius: 14px;
}
.title-kicker {
    width: 100%;
    color: rgba(163, 226, 239, 0.78);
    font-size: )" + std::string(compactTitle ? "10" : "12") + R"(px;
    font-weight: bold;
    line-height: 1.15;
    text-align: center;
    text-transform: uppercase;
}
.orebit-lockup {
    position: relative;
    left: )" + std::to_string(titleLockupLeft) + R"(px;
    display: flex;
    flex-direction: row;
    width: )" + std::to_string(titleLockupWidth) + R"(px;
    height: )" + std::string(compactTitle ? "78" : "118") + R"(px;
    margin-top: )" + std::string(compactTitle ? "12" : "18") + R"(px;
    margin-bottom: )" + std::string(compactTitle ? "2" : "4") + R"(px;
}
.orebit-letter {
    display: block;
    width: )" + std::to_string(titleLetterWidth) + R"(px;
    color: #f4c95d;
    font-size: )" + std::to_string(titleLetterSize) + R"(px;
    font-weight: bold;
    line-height: 1;
    text-align: center;
    font-effect: glow(2px #9b761e);
    animation: orebit-letter-float 2.8s cubic-in-out infinite alternate;
}
.orebit-bit {
    color: #63e5ff;
    font-effect: glow(2px #155d78);
}
.orebit-letter-r { animation: 3.25s cubic-in-out infinite alternate orebit-letter-float; }
.orebit-letter-e { animation: 2.55s cubic-in-out infinite alternate orebit-letter-float; }
.orebit-letter-b { animation: 3.5s cubic-in-out infinite alternate orebit-letter-float; }
.orebit-letter-i { animation: 2.7s cubic-in-out infinite alternate orebit-letter-float; }
.orebit-letter-t { animation: 3.1s cubic-in-out infinite alternate orebit-letter-float; }
@keyframes orebit-letter-float {
    from {
        transform: translateY(-5px) rotate(-1.5deg);
        opacity: 0.88;
    }
    to {
        transform: translateY(6px) rotate(1.5deg);
        opacity: 1;
    }
}
.title-tagline {
    width: 100%;
    margin-top: 0px;
    color: #edf7fb;
    font-size: )" + std::string(compactTitle ? "14" : "17") + R"(px;
    font-weight: bold;
    line-height: 1.15;
    text-align: center;
    text-transform: uppercase;
}
.title-divider {
    display: flex;
    flex-direction: row;
    align-items: center;
    width: 100%;
    margin-top: )" + std::string(compactTitle ? "12" : "16") + R"(px;
    margin-bottom: )" + std::string(compactTitle ? "10" : "14") + R"(px;
}
.title-divider span {
    width: )" + std::to_string(std::max(22, (titleInnerWidth - 250) / 2)) + R"(px;
    height: 1px;
    background-color: rgba(71, 220, 255, 0.38);
}
.title-divider strong {
    width: 250px;
    color: rgba(244, 201, 93, 0.78);
    font-size: 10px;
    line-height: 1;
    text-align: center;
}
.title-menu {
    position: relative;
    left: )" + std::to_string(titleMenuLeft) + R"(px;
    display: flex;
    flex-direction: column;
    width: )" + std::to_string(titleMenuWidth) + R"(px;
}
.title-menu .title-action {
    width: )" + std::to_string(titleMenuWidth) + R"(px;
    min-height: )" + std::string(compactTitle ? "42" : "48") + R"(px;
    height: auto;
    margin-top: 7px;
    padding: 8px 18px;
    color: #eaf7fb;
    font-size: )" + std::string(compactTitle ? "15" : "17") + R"(px;
    font-weight: bold;
    line-height: 1.1;
    text-transform: uppercase;
    background-color: rgba(8, 26, 36, 0.91);
    border-width: 1px;
    border-color: rgba(99, 229, 255, 0.42);
    border-radius: 6px;
}
.title-menu .title-action:hover,
body.controller-focus-visible .title-menu .title-action.rr-controller-focus {
    color: #ffffff;
    background-color: rgba(18, 59, 77, 0.96);
    border-color: #63e5ff;
}
.title-menu .title-continue {
    color: #fff0b7;
    background-color: rgba(60, 48, 17, 0.92);
    border-color: rgba(244, 201, 93, 0.68);
}
.title-menu .title-continue:hover,
body.controller-focus-visible .title-menu .title-continue.rr-controller-focus {
    background-color: rgba(92, 69, 19, 0.96);
    border-color: #f4c95d;
}
.title-save-state {
    width: 100%;
    margin-top: 12px;
    color: rgba(157, 177, 189, 0.72);
    font-size: 10px;
    font-weight: bold;
    line-height: 1;
    text-align: center;
}
.title-save-state.save-found { color: #7ce7b0; }
.title-notice {
    width: 100%;
    margin-top: 7px;
    color: #ffd166;
    font-size: 11px;
    line-height: 1.2;
    text-align: center;
}
.title-footer {
    width: 100%;
    margin-top: )" + std::string(compactTitle ? "10" : "15") + R"(px;
    color: rgba(127, 160, 176, 0.58);
    font-size: 10px;
    line-height: 1;
    text-align: center;
}
.title-scanline {
    position: absolute;
    left: 0px;
    width: )" + std::to_string(panelWidth) + R"(px;
    height: 1px;
    background-color: rgba(99, 229, 255, 0.18);
    animation: 7s linear 0s infinite orebit-scanline-sweep;
}
.title-scanline-a { top: 0px; }
.title-scanline-b {
    top: )" + std::to_string(panelHeight / 3) + R"(px;
    animation: 10s linear infinite orebit-scanline-sweep;
    background-color: rgba(244, 201, 93, 0.14);
}
@keyframes orebit-scanline-sweep {
    from { transform: translateY(0px); opacity: 0; }
    12% { opacity: 1; }
    88% { opacity: 0.72; }
    to { transform: translateY()" + std::to_string(panelHeight) + R"(px); opacity: 0; }
}
#rr-modal.modal-new_game_confirm {
    left: )" + std::to_string(modalNewGameLeft) + R"(px;
    top: )" + std::to_string(modalNewGameTop) + R"(px;
    width: )" + std::to_string(modalNewGameWidth) + R"(px;
    height: )" + std::to_string(modalNewGameHeight) + R"(px;
    margin-left: 0px;
    border-color: #a77f32;
}
#rr-panel.surface-ops-panel {
    overflow-x: hidden;
    overflow-y: auto;
}
#rr-panel.navigation-panel {
    overflow-x: hidden;
    overflow-y: auto;
}
#rr-panel.arrival-fanfare-panel-mode {
    box-sizing: border-box;
    overflow: hidden;
    padding: 10px;
    background-color: #0b101a;
    border-width: 1px;
    border-color: #8a7131;
    border-radius: 12px;
}
#rr-panel.arrival-fanfare-panel-mode > .panel-head,
#rr-panel.arrival-fanfare-panel-mode > .status,
#rr-panel.arrival-fanfare-panel-mode > .panel-kpis {
    display: none;
}
.arrival-fanfare-panel {
    box-sizing: border-box;
    display: block;
    width: 100%;
    height: 100%;
    padding: 10px 8px;
    background-color: #0d1320;
    border-width: 1px;
    border-color: #31566b;
    border-radius: 9px;
}
.arrival-stamp-kicker {
    color: #ffd166;
    font-size: 13px;
    font-weight: bold;
    line-height: 1;
    text-transform: uppercase;
}
.arrival-stamp-title {
    width: 100%;
    margin-top: 7px;
    margin-bottom: 5px;
    color: #f8fbff;
    font-size: )" + std::to_string(arrivalTitleSize) + R"(px;
    line-height: 0.95;
    text-transform: uppercase;
}
.arrival-stamp-destination {
    color: #dceaf2;
    font-size: 15px;
    line-height: 1.15;
}
.arrival-stamp-tags {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    width: 100%;
    margin-top: 12px;
}
.arrival-stamp-tags span {
    min-height: 24px;
    margin-right: 7px;
    margin-bottom: 5px;
    padding: 5px 8px;
    color: #dff7ff;
    font-size: 13px;
    font-weight: bold;
    line-height: 1.1;
    background-color: #102835;
    border-width: 1px;
    border-color: #34566a;
    border-radius: 14px;
}
.arrival-stamp-tags span.gold {
    color: #ffe6a5;
    background-color: #332b15;
    border-color: #8a7131;
}
.arrival-stamp-continue {
    width: 320px;
    min-height: 28px;
    height: 28px;
    margin-top: 3px;
    margin-right: 0px;
    padding: 0px;
    color: #9eafbc;
    font-size: 13px;
    line-height: 28px;
    text-align: left;
    background-color: transparent;
    border-width: 0px;
}
#rr-panel.mining-fullscreen-panel {
    left: 0px;
    top: 0px;
    width: )" + std::to_string(panelWidth) + R"(px;
    height: )" + std::to_string(panelHeight) + R"(px;
    overflow: hidden;
    padding: 0px;
    background-color: transparent;
    border-width: 0px;
    border-radius: 0px;
}
#rr-panel.mining-fullscreen-panel .panel-head,
#rr-panel.mining-fullscreen-panel .status,
#rr-panel.mining-fullscreen-panel .panel-kpis {
    display: none;
}
.mining-fullscreen {
    position: relative;
    width: )" + std::to_string(panelWidth) + R"(px;
    height: )" + std::to_string(panelHeight) + R"(px;
}
.mining-top-rail,
.mining-bottom-rail,
.mining-failure-banner {
    position: absolute;
    border-width: 1px;
    border-color: rgba(71, 220, 255, 0.34);
    border-radius: 8px;
    background-color: rgba(5, 13, 18, 0.86);
}
.mining-top-rail {
    left: )" + std::to_string(miningInset) + R"(px;
    top: )" + std::to_string(miningInset) + R"(px;
    width: )" + std::to_string(miningRailWidth) + R"(px;
    min-height: )" + std::to_string(miningTopHeight) + R"(px;
    padding: 5px 8px;
    display: block;
}
.mining-run-title {
    width: )" + std::to_string(miningTitleWidth) + R"(px;
    margin-right: 10px;
}
.mining-run-title span,
.mining-utility-cluster span,
.mining-command-dock span,
.mining-payload-strip > span {
    color: rgba(154, 230, 244, 0.80);
    font-size: 12px;
    text-transform: uppercase;
}
.mining-run-title strong {
    color: #f1fbff;
    font-size: 23px;
    line-height: 1.05;
}
.mining-run-title small {
    color: rgba(215, 232, 234, 0.78);
    font-size: 12px;
    line-height: 1.12;
}
.mining-run-title.debug-arena strong {
    display: block;
    font-size: 14px;
}
.mining-run-title.debug-arena .mining-arena-metadata {
    font-size: 10px;
}
.mining-vitals {
    position: )" + std::string(compactMining ? "relative" : "absolute") + R"(;
    left: )" + std::to_string(miningVitalsLeft) + R"(px;
    top: )" + std::string(compactMining ? "0px" : "5px") + R"(;
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    width: )" + std::to_string(miningVitalsWidth) + R"(px;
    margin-top: )" + std::string(compactMining ? "8px" : "0px") + R"(;
    margin-right: 0px;
}
.mining-vitals .metric {
    width: )" + std::to_string(miningVitalWidth) + R"(px;
    height: 42px;
    min-height: 42px;
    margin-right: )" + std::to_string(miningVitalGap) + R"(px;
    margin-bottom: 5px;
    padding: 3px 5px;
    background-color: rgba(8, 30, 39, 0.94);
    border-width: 1px;
    border-color: #2f7d99;
}
.mining-vitals .metric strong {
    font-size: 24px;
    line-height: 0.98;
}
.mining-vitals .metric span {
    font-size: 12px;
    line-height: 1.0;
}
.mining-vitals .metric.mining-alert-caution {
    border-color: #c8a446;
}
.mining-vitals .metric.mining-alert-caution strong,
.mining-vitals .metric.mining-alert-caution span {
    color: #ffe6a5;
}
.mining-vitals .metric.mining-alert-caution.mining-alert-pulse-1 {
    background-color: rgba(31, 38, 29, 0.92);
}
.mining-vitals .metric.mining-alert-caution.mining-alert-pulse-2 {
    background-color: rgba(42, 45, 29, 0.94);
}
.mining-vitals .metric.mining-alert-caution.mining-alert-pulse-3 {
    background-color: rgba(53, 53, 29, 0.96);
}
.mining-vitals .metric.mining-alert-critical {
    border-color: #c95d50;
}
.mining-vitals .metric.mining-alert-critical strong,
.mining-vitals .metric.mining-alert-critical span {
    color: #ffd8d3;
}
.mining-vitals .metric.mining-alert-critical.mining-alert-pulse-1 {
    background-color: rgba(37, 27, 30, 0.92);
}
.mining-vitals .metric.mining-alert-critical.mining-alert-pulse-2 {
    background-color: rgba(50, 30, 32, 0.94);
}
.mining-vitals .metric.mining-alert-critical.mining-alert-pulse-3 {
    background-color: rgba(64, 34, 35, 0.96);
}
.mining-vitals .metric.mining-vital-broken strong {
    color: #b8322b;
    font-weight: bold;
    text-transform: uppercase;
}
.mining-vitals .metric.mining-vital-broken.mining-alert-pulse-1 strong {
    color: #d94439;
}
.mining-vitals .metric.mining-vital-broken.mining-alert-pulse-2 strong {
    color: #f45b4d;
}
.mining-vitals .metric.mining-vital-broken.mining-alert-pulse-3 strong {
    color: #ff8278;
}
.mining-health-strip {
    width: )" + std::to_string(std::max(1, miningTitleWidth - 4)) + R"(px;
    margin-top: 5px;
}
.mining-health-strip > div {
    display: flex;
    flex-direction: row;
    justify-content: space-between;
}
.mining-health-strip span {
    color: rgba(169, 231, 246, 0.88);
    font-size: 12px;
    text-transform: uppercase;
}
.mining-health-strip strong {
    color: #e7fbff;
    font-size: 17px;
}
.mining-health-bar {
    width: )" + std::to_string(std::max(1, miningTitleWidth - 6)) + R"(px;
    height: 9px;
    margin-top: 3px;
    background-color: rgba(1, 8, 13, 0.92);
    border-width: 1px;
    border-color: rgba(127, 236, 255, 0.18);
}
.mining-health-bar i {
    display: block;
    height: 9px;
    background-color: #4be7ff;
}
.mining-health-bar .health-fill-0 { width: 0px; }
.mining-health-bar .health-fill-10 { width: )" + std::to_string((std::max(1, miningTitleWidth - 6) * 10) / 100) + R"(px; }
.mining-health-bar .health-fill-20 { width: )" + std::to_string((std::max(1, miningTitleWidth - 6) * 20) / 100) + R"(px; }
.mining-health-bar .health-fill-30 { width: )" + std::to_string((std::max(1, miningTitleWidth - 6) * 30) / 100) + R"(px; }
.mining-health-bar .health-fill-40 { width: )" + std::to_string((std::max(1, miningTitleWidth - 6) * 40) / 100) + R"(px; }
.mining-health-bar .health-fill-50 { width: )" + std::to_string((std::max(1, miningTitleWidth - 6) * 50) / 100) + R"(px; }
.mining-health-bar .health-fill-60 { width: )" + std::to_string((std::max(1, miningTitleWidth - 6) * 60) / 100) + R"(px; }
.mining-health-bar .health-fill-70 { width: )" + std::to_string((std::max(1, miningTitleWidth - 6) * 70) / 100) + R"(px; }
.mining-health-bar .health-fill-80 { width: )" + std::to_string((std::max(1, miningTitleWidth - 6) * 80) / 100) + R"(px; }
.mining-health-bar .health-fill-90 { width: )" + std::to_string((std::max(1, miningTitleWidth - 6) * 90) / 100) + R"(px; }
.mining-health-bar .health-fill-100 { width: )" + std::to_string(std::max(1, miningTitleWidth - 6)) + R"(px; }
.mining-utility-cluster {
    position: absolute;
    right: 8px;
    top: 6px;
    width: )" + std::to_string(miningUtilityWidth) + R"(px;
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    justify-content: flex-end;
}
.mining-utility-cluster button {
    width: 102px;
    min-height: 24px;
    height: auto;
    line-height: 1.15;
    margin-left: 4px;
    margin-bottom: 5px;
    padding: 4px 4px;
    font-size: 12px;
}
.mining-playfield-space {
    position: absolute;
    left: )" + std::to_string(miningInset) + R"(px;
    top: )" + std::to_string(miningPlayfieldTop) + R"(px;
    width: )" + std::to_string(miningRailWidth) + R"(px;
    height: )" + std::to_string(std::max(1, miningBottomTop - miningPlayfieldTop - 12)) + R"(px;
}
.mining-bottom-rail {
    left: )" + std::to_string(miningInset) + R"(px;
    top: )" + std::to_string(miningBottomTop) + R"(px;
    width: )" + std::to_string(miningRailWidth) + R"(px;
    min-height: )" + std::to_string(miningBottomHeight) + R"(px;
    padding: 10px;
    display: block;
}
.mining-payload-strip {
    width: )" + std::to_string(miningPayloadWidth) + R"(px;
    margin-right: 12px;
}
.mining-payload-strip .stat-grid,
.mining-artifact-strip .stat-grid {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
}
.mining-payload-strip .stat-chip,
.mining-artifact-strip .stat-chip {
    width: 112px;
    min-height: 18px;
    padding: 4px 6px;
    margin-top: 5px;
    margin-right: 6px;
    font-size: 11px;
}
.mining-artifact-strip {
    width: )" + std::to_string(miningPayloadWidth) + R"(px;
    margin-top: 6px;
}
.mining-command-dock {
    position: absolute;
    left: )" + std::to_string(miningCommandLeft) + R"(px;
    top: 10px;
    width: )" + std::to_string(miningCommandWidth) + R"(px;
}
.mining-command-dock strong {
    color: #f1fbff;
    font-size: 17px;
    margin-bottom: 6px;
}
.mining-command-dock .system-actions {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    justify-content: flex-end;
}
.mining-command-dock .system-actions button {
    width: )" + std::to_string(miningActionWidth) + R"(px;
    min-height: 40px;
    height: auto;
    line-height: 1.15;
    margin-right: 8px;
    margin-bottom: 7px;
    padding: 7px 4px;
    font-size: 13px;
}
.mining-ship-service-marker {
    display: none;
}
.mining-recall-dock {
    position: absolute;
    left: )" + std::to_string(miningPrimaryActionLeft) + R"(px;
    top: )" + std::to_string(miningPrimaryActionTop) + R"(px;
    width: )" + std::to_string(miningPrimaryActionWidth) + R"(px;
}
.mining-recall-dock .system-actions button {
    width: )" + std::to_string(miningPrimaryActionWidth) + R"(px;
    min-height: 40px;
    height: auto;
    line-height: 1.15;
    padding: 7px 4px;
    font-size: 13px;
}
.mining-failure-banner {
    left: )" + std::to_string(miningInset) + R"(px;
    top: )" + std::to_string(miningPlayfieldTop) + R"(px;
    width: )" + std::to_string(std::min(520, miningRailWidth)) + R"(px;
    padding: 10px;
    border-color: rgba(255, 95, 87, 0.52);
    background-color: rgba(42, 10, 13, 0.90);
}
.mining-failure-banner strong {
    color: #ffe1df;
    font-size: 17px;
}
.mining-failure-banner span {
    color: rgba(255, 226, 222, 0.82);
    font-size: 12px;
    line-height: 1.22;
}
.phase-lane,
.phase-title-row,
.phase-footer-lane {
    width: )" + std::to_string(kPhaseContentLaneWidth) + R"(px;
    margin-left: )" + std::to_string(kPhaseLaneInset) + R"(px;
    margin-right: )" + std::to_string(kPhaseLaneInset) + R"(px;
}
.objective-strip {
    display: block;
    padding: 10px 12px;
    margin-bottom: 10px;
    border-width: 1px;
    border-color: rgba(255, 209, 102, 0.52);
    background-color: rgba(34, 29, 13, 0.90);
}
.objective-strip span { color: #ffd166; font-size: 11px; }
.objective-strip strong { display: block; color: #fff2c0; font-size: 14px; }
.objective-strip p { margin: 3px 0 0 0; color: rgba(255, 244, 205, 0.78); font-size: 11px; }
.mining-objective-strip { position: absolute; left: )" + std::to_string(miningInset) + R"(px; top: )" + std::to_string(miningObjectiveTop) + R"(px; width: 520px; z-index: 4; }
.phase-row,
.phase-title-row,
.phase-action-grid,
.phase-footer-lane {
    display: flex;
    flex-direction: row;
    flex-wrap: nowrap;
}
.phase-title-row {
    justify-content: space-between;
}
.phase-action-grid {
    width: )" + std::to_string(kPhaseContentLaneWidth) + R"(px;
    align-items: stretch;
}
.phase-card-slot {
    display: block;
    flex: none;
    width: )" + std::to_string(kPhaseCardSlotWidth) + R"(px;
    margin-right: )" + std::to_string(kPhaseCardGap) + R"(px;
}
.phase-card-slot.is-last {
    margin-right: 0px;
}
.phase-card-slot > .ops-card,
.phase-card-slot > .pilot-card,
.phase-card-slot > .upgrade-draft-card {
    margin-right: 0px;
}
.phase-common-button {
    width: )" + std::to_string(kPhaseCommonButtonWidth) + R"(px;
}
.phase-chip-slot {
    width: )" + std::to_string(kPhaseCommonChipSlotWidth) + R"(px;
}
.panel-head, .phase-titlebar, .card-footer, .draft-card-footer, .utility-row, .utility-actions, .pilot-card-top, .card-topline, .card-kicker, .slot-topline, .recipe-topline {
    display: flex;
    flex-direction: row;
    justify-content: space-between;
}
.action-row {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    align-items: center;
}
.phase-board, .board-primary, .draft-hero, .draft-board, .surface-command, .surface-quickbar, .cockpit-hud, .arrival-fanfare-panel {
    display: block;
    width: 100%;
}
.summary-grid, .metric-grid, .metric-strip, .panel-kpis, .ops-grid, .pilot-card-grid, .inventory-grid, .stat-grid, .chip-strip, .actions, .action-row, .warning-grid, .surface-kpi-grid, .surface-quickbar, .draft-context, .result-grid, .achievement-grid {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
}
.ops-card, .pilot-card, .upgrade-draft-card, .arrival-card, .nav-card, .inventory-item, .achievement-card, .crew-fate-card, .result-group, .drone-recipe-card, .drone-loadout-slot, .drone-control-card, .modal-body {
    display: flex;
    flex-direction: column;
}
.card-topline, .card-kicker, .pilot-card-top, .slot-topline, .recipe-topline, .card-title, .card-copy, .metric-strip, .chip-strip, .utility-actions {
    flex: none;
}
.ops-card h3, .pilot-card h3, .upgrade-draft-card h3, .arrival-card h3, .nav-card h3,
.ops-card p, .pilot-card p, .upgrade-draft-card p, .arrival-card p, .nav-card p,
.module-impact, .drone-build-hook, .drone-upgrade-summary, .inventory-art, .inventory-copy, .card-title, .card-copy {
    flex: none;
}
.stat-grid, .chip-strip, .metric-strip {
    flex: none;
    align-content: flex-start;
    align-items: flex-start;
    gap: 5px;
    row-gap: 5px;
    column-gap: 5px;
}
.stat-grid .stat-chip {
    flex: none;
}
.card-footer, .draft-card-footer {
    flex: none;
    margin-top: auto;
    gap: 8px;
    row-gap: 8px;
    column-gap: 8px;
}
.action-row {
    flex: none;
    gap: 8px;
    row-gap: 8px;
    column-gap: 8px;
}
.card-footer button, .draft-card-footer button, .actions button, .action-row button, .utility-row button, .compact-tools button, .utility-actions button {
    flex: none;
    white-space: normal;
}
.modal-body {
    flex: auto;
    min-height: 0px;
}
.panel-head {
    margin-bottom: 10px;
}
.phase-board-panel .panel-head {
    width: 736px;
}
.phase-board-panel .panel-title {
    width: 240px;
}
.phase-board-panel .panel-head-actions {
    display: flex;
    flex-direction: row;
    flex-wrap: nowrap;
    width: 416px;
    margin-right: 0px;
    justify-content: flex-end;
}
.control-panel .panel-head {
    width: 100%;
}
.control-panel .panel-title {
    width: 120px;
}
.control-panel .panel-head-actions {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    width: 316px;
    margin-top: 0px;
    justify-content: flex-end;
}
.panel-head-actions button {
    min-width: 92px;
    width: 118px;
    margin-top: 0px;
}
.phase-board-panel .panel-head-actions button {
    min-width: 0px;
    width: 96px;
    margin-right: 8px;
}
.control-panel .panel-head-actions button {
    min-width: 0px;
    width: 92px;
    margin-top: 0px;
    margin-right: 8px;
}
.game-mark, .card-topline span, .card-kicker span, .pilot-card-top span {
    color: #7e90a0;
    font-size: 12px;
}
.panel-kpis {
    margin-top: 10px;
    margin-bottom: 8px;
}
.panel-kpis .metric {
    width: 150px;
}
.panel-kpis .metric-chapter span {
    display: none;
}
.phase-board-panel .status,
.phase-board-panel .phase-status,
.phase-board-panel .panel-kpis {
    width: 704px;
}
.phase-board-panel .panel-kpis .metric {
    width: 150px;
}
.phase-board-panel.surface-ops-panel .panel-kpis {
    display: none;
}
.phase-board-panel.surface-ops-panel .panel-head {
    box-sizing: border-box;
    width: )" + std::to_string(kPhaseContentLaneWidth) + R"(px;
    margin-left: )" + std::to_string(kPhaseLaneInset) + R"(px;
    margin-right: )" + std::to_string(kPhaseLaneInset) + R"(px;
}
.phase-board-panel.surface-ops-panel .panel-title {
    flex: 1 1 auto;
    min-width: 0px;
}
.phase-board-panel.surface-ops-panel .panel-head-actions {
    flex: 0 0 auto;
    width: auto;
    margin-left: auto;
}
.phase-board-panel.surface-ops-panel .panel-head-actions button {
    margin-left: 8px;
    margin-right: 0px;
}
.phase-board-panel.drone-ops-panel .panel-kpis {
    display: none;
}
.phase-board-panel.navigation-panel .panel-kpis {
    display: none;
}
.phase-board-panel.navigation-panel .panel-head {
    box-sizing: border-box;
    width: )" + std::to_string(kPhaseContentLaneWidth) + R"(px;
    margin-left: )" + std::to_string(kPhaseLaneInset) + R"(px;
    margin-right: )" + std::to_string(kPhaseLaneInset) + R"(px;
    margin-bottom: 6px;
}
.phase-board-panel.navigation-panel .panel-title {
    flex: 1 1 auto;
    min-width: 0px;
}
.phase-board-panel.navigation-panel .panel-head-actions {
    flex: 0 0 auto;
    width: auto;
    margin-left: auto;
}
.phase-board-panel.navigation-panel .panel-head-actions button {
    margin-left: 8px;
    margin-right: 0px;
}
.phase-board-panel.navigation-panel .status {
    box-sizing: border-box;
    width: )" + std::to_string(kPhaseContentLaneWidth) + R"(px;
    margin-left: )" + std::to_string(kPhaseLaneInset) + R"(px;
    margin-right: )" + std::to_string(kPhaseLaneInset) + R"(px;
    margin-bottom: 6px;
}
.phase-board-panel .phase-titlebar {
    width: 704px;
    margin-top: 8px;
    margin-bottom: 10px;
}
.phase-board-panel .phase-titlebar > div {
    width: 520px;
}
.phase-board-panel .phase-titlebar h2 {
    margin-top: 0px;
}
.phase-board-panel .phase-titlebar p {
    width: 510px;
    line-height: 1.32;
}
.phase-board-panel .compact-tools {
    width: 150px;
    justify-content: flex-end;
}
.phase-board-panel .compact-tools button {
    width: 118px;
    margin-right: 0px;
}
.phase-board-arrival,
.phase-board-research,
.phase-board-drone-ops {
    width: 736px;
}
.phase-board-drone-ops {
    padding-bottom: 8px;
}
.phase-board-panel.drone-ops-panel .panel-head {
    margin-bottom: 8px;
}
.phase-board-panel.drone-ops-panel .status {
    margin-bottom: 6px;
}
.phase-board-panel.drone-ops-panel .phase-titlebar {
    margin-top: 4px;
    margin-bottom: 6px;
}
.phase-board-panel.drone-ops-panel .phase-titlebar > div {
    width: 430px;
}
.phase-board-panel.drone-ops-panel .phase-titlebar p {
    width: 410px;
    line-height: 1.18;
}
.phase-board-panel.drone-ops-panel .compact-tools {
    display: flex;
    flex-direction: row;
    flex-wrap: nowrap;
    width: 238px;
    justify-content: flex-end;
}
.phase-board-panel.drone-ops-panel .compact-tools button {
    width: 106px;
    min-height: 30px;
    margin-left: 6px;
    margin-right: 0px;
    padding: 5px 7px;
}
.phase-board-arrival {
    padding-bottom: 0px;
}
.phase-board-arrival .result-grid,
.phase-board-results .result-grid,
.phase-board-arrival .achievement-grid,
.phase-board-results .achievement-grid {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    width: 736px;
    margin-top: 6px;
}
.phase-board-arrival .crew-fate-card,
.phase-board-results .crew-fate-card {
    width: 704px;
    margin-top: 8px;
    margin-right: 0px;
    padding: 11px 12px;
}
.phase-board-arrival .crew-fate-card p,
.phase-board-results .crew-fate-card p {
    width: 646px;
    margin-top: 4px;
    line-height: 1.35;
}
.phase-board-arrival .crew-fate-signal,
.phase-board-results .crew-fate-signal {
    display: none;
}
.phase-board-arrival .result-group,
.phase-board-results .result-group,
.phase-board-arrival .achievement-card,
.phase-board-results .achievement-card {
    padding: 8px 10px;
}
.phase-board-arrival .result-group,
.phase-board-results .result-group {
    width: 334px;
    min-height: 88px;
    margin-top: 6px;
    margin-right: 8px;
}
.phase-board-arrival .result-group.primary,
.phase-board-results .result-group.primary {
    width: 704px;
    margin-right: 0px;
}
.phase-board-arrival .result-group h3,
.phase-board-results .result-group h3 {
    margin-bottom: 5px;
    padding-bottom: 4px;
    border-bottom-width: 1px;
    border-bottom-color: #34485a;
}
.phase-board-arrival .result-row,
.phase-board-results .result-row {
    display: flex;
    flex-direction: row;
    justify-content: space-between;
    width: 100%;
    min-height: 24px;
    margin-top: 3px;
    padding-top: 3px;
    border-top-width: 1px;
    border-top-color: #263b4c;
}
.phase-board-arrival .result-group h3 + .result-row,
.phase-board-results .result-group h3 + .result-row {
    border-top-width: 0px;
}
.phase-board-arrival .result-row span,
.phase-board-results .result-row span {
    width: 126px;
    color: #8295a5;
    font-size: 12px;
}
.phase-board-arrival .result-row strong,
.phase-board-results .result-row strong {
    width: 160px;
    margin-top: 0px;
}
.phase-board-arrival .result-group.primary .result-row span,
.phase-board-results .result-group.primary .result-row span {
    width: 178px;
}
.phase-board-arrival .result-group.primary .result-row strong,
.phase-board-results .result-group.primary .result-row strong {
    width: 486px;
}
.phase-board-arrival .board-note,
.phase-board-results .board-note {
    width: 704px;
    margin-top: 8px;
    padding: 8px 12px;
    color: #d7c276;
    background-color: #101923;
    border-width: 1px;
    border-color: #4c4728;
    border-radius: 6px;
}
.phase-board-arrival .achievement-card,
.phase-board-results .achievement-card {
    width: 704px;
    margin-right: 0px;
}
.phase-board-arrival .achievement-card p,
.phase-board-results .achievement-card p {
    width: 670px;
}
.phase-board-arrival .tutorial-card {
    width: 704px;
    padding: 11px 12px;
    margin-top: 10px;
    margin-bottom: 12px;
    background-color: #101923;
    border-color: #2f4354;
}
.phase-board-arrival .tutorial-card div {
    width: 680px;
}
.phase-board-arrival .tutorial-card p {
    width: 680px;
    line-height: 1.32;
}
.phase-board-arrival .tutorial-card button {
    width: 140px;
    height: 34px;
    line-height: 34px;
    margin-top: 8px;
}
.phase-board-arrival > .metric-grid,
.phase-board-research .focus-metrics,
.phase-board-drone-ops .focus-metrics {
    width: 704px;
    margin-top: 4px;
    margin-bottom: 6px;
}
.phase-board-arrival .approach-metrics {
    width: 704px;
    margin-top: 4px;
    margin-bottom: 6px;
    flex-wrap: nowrap;
}
.phase-board-arrival .approach-metrics .surface-kpi {
    width: 154px;
    min-height: 30px;
    margin-top: 4px;
    margin-right: 8px;
    padding: 5px 9px;
}
.phase-board-arrival .approach-metrics .surface-kpi span {
    font-size: 11px;
    line-height: 1.1;
}
.phase-board-arrival .approach-metrics .surface-kpi strong {
    font-size: 13px;
    line-height: 1.1;
}
.phase-board-arrival > .metric-grid .metric,
.phase-board-research .focus-metrics .metric,
.phase-board-drone-ops .focus-metrics .metric {
    width: 154px;
    margin-right: 8px;
}
.phase-board-arrival .ops-grid,
.phase-board-research .ops-grid,
.phase-board-drone-ops .ops-grid {
    width: 704px;
    margin-top: 6px;
}
.phase-board-arrival .ops-grid {
    width: 704px;
    flex-wrap: nowrap;
    margin-top: 4px;
    margin-bottom: 6px;
}
.phase-board-arrival .arrival-card,
.phase-board-research .ops-card,
.phase-board-drone-ops .drone-card {
    width: 204px;
    min-height: 172px;
    padding: 10px;
    margin-top: 8px;
    margin-right: 8px;
}
.phase-board-research .ops-card {
    width: 204px;
}
.phase-board-research .ops-card {
    min-height: 238px;
}
.phase-board-drone-ops .ops-grid {
    width: 704px;
    margin-top: 4px;
}
.phase-board-drone-ops .drone-card {
    width: 205px;
    min-height: 330px;
    padding: 8px;
    margin-top: 6px;
    margin-right: 7px;
}
.phase-board-arrival .arrival-card {
    width: 209px;
    min-height: 250px;
    margin-right: 8px;
    padding: 10px 9px;
}
.phase-board-arrival .arrival-card .card-topline {
    width: 191px;
    min-height: 34px;
    overflow: hidden;
}
.phase-board-arrival .arrival-card .card-topline span {
    width: 88px;
    min-height: 34px;
    line-height: 1.15;
    overflow: hidden;
}
.phase-board-arrival .arrival-card h3 {
    min-height: 20px;
    overflow: hidden;
}
.phase-board-arrival .arrival-card p,
.phase-board-research .ops-card p,
.phase-board-drone-ops .drone-card p {
    width: 194px;
    min-height: 52px;
    margin-top: 5px;
    margin-bottom: 7px;
    line-height: 1.28;
}
.phase-board-research .ops-card p {
    min-height: 54px;
    overflow: visible;
}
.phase-board-drone-ops .drone-card p {
    width: 189px;
    min-height: 38px;
    margin-top: 3px;
    margin-bottom: 4px;
    line-height: 1.16;
    overflow: visible;
}
.phase-board-arrival .arrival-card p {
    width: 191px;
    max-height: none;
    overflow: visible;
}
.phase-board-research .ops-card .module-impact {
    display: block;
    width: 194px;
    min-height: 30px;
    margin-top: 4px;
    margin-bottom: 4px;
    font-size: 13px;
    line-height: 1.25;
    overflow: visible;
}
.phase-board-research .ops-card .stat-grid,
.phase-board-drone-ops .drone-card .stat-grid {
    width: 194px;
    min-height: 50px;
    margin-top: 4px;
    justify-content: center;
}
.phase-board-drone-ops .drone-card .stat-grid {
    width: 189px;
    min-height: 54px;
    height: auto;
    margin-top: 2px;
}
.phase-board-drone-ops .drone-card .drone-build-hook {
    width: 177px;
    min-height: 52px;
    margin-top: 5px;
    margin-bottom: 5px;
    padding: 6px;
    border-width: 1px;
    border-color: rgba(71, 220, 255, 0.18);
    background-color: rgba(71, 220, 255, 0.06);
    color: #bfeef7;
    font-size: 11px;
    line-height: 1.20;
    overflow: visible;
}
.phase-board-drone-ops .drone-card .drone-upgrade-summary {
    width: 189px;
    min-height: 40px;
    margin-top: 2px;
    margin-bottom: 5px;
    color: #ffd166;
    font-size: 11px;
    line-height: 1.15;
    overflow: visible;
}
)" + R"(
.phase-board-drone-ops .drone-card .stat-chip {
    max-width: 92px;
    min-height: 22px;
    padding: 4px 6px;
    font-size: 11px;
    line-height: 1.15;
}
.phase-board-drone-ops .drone-card .stat-chip.wide {
    max-width: 118px;
}
.phase-board-drone-ops .drone-card .module-art {
    width: 194px;
    height: 44px;
    margin-top: 4px;
    margin-bottom: 6px;
    background-color: #0c141d;
    border-width: 1px;
    border-color: #3c596a;
    border-radius: 6px;
}
.phase-board-drone-ops .drone-card .module-art {
    width: 189px;
    height: 34px;
    margin-top: 3px;
    margin-bottom: 4px;
}
.phase-board-drone-ops .drone-card .module-art span {
    width: 100%;
    color: #8fd8f0;
    font-size: 22px;
    line-height: 44px;
    text-align: center;
}
.phase-board-drone-ops .drone-card .module-art span {
    font-size: 19px;
    line-height: 34px;
}
.phase-board-drone-ops .drone-card h3 {
    margin-top: 2px;
    margin-bottom: 0px;
}
.phase-board-arrival .arrival-card .card-footer,
.phase-board-research .ops-card .card-footer,
.phase-board-drone-ops .drone-card .card-footer {
    min-height: 48px;
    height: auto;
    margin-top: auto;
    padding-top: 9px;
    border-top-width: 1px;
    border-top-color: #263b4c;
    align-items: center;
}
.phase-board-drone-ops .drone-card .card-footer {
    width: 189px;
    min-height: 34px;
    height: auto;
    margin-top: auto;
    padding-top: 6px;
}
.phase-board-arrival .arrival-card .card-footer {
    width: 191px;
    min-height: 48px;
    height: auto;
    margin-top: auto;
}
.phase-board-arrival .arrival-card .card-footer span,
.phase-board-research .ops-card .card-footer span,
.phase-board-drone-ops .drone-card .card-footer span {
    width: 76px;
    color: #d7c276;
    font-size: 12px;
    line-height: 1.15;
}
.phase-board-drone-ops .drone-card .card-footer span {
    width: 45px;
    font-size: 11px;
}
.phase-board-arrival .arrival-card .card-footer span {
    width: 80px;
    min-height: 34px;
    white-space: nowrap;
    overflow: hidden;
}
.phase-board-arrival .arrival-card .card-footer button,
.phase-board-research .ops-card .card-footer button,
.phase-board-drone-ops .drone-card .card-footer button {
    width: 104px;
    min-height: 36px;
    height: auto;
    line-height: 1.15;
    margin-top: 0px;
    margin-right: 0px;
    padding-left: 5px;
    padding-right: 5px;
    font-size: 12px;
}
.phase-board-drone-ops .drone-card .card-footer button {
    width: 63px;
    min-height: 28px;
    height: auto;
    line-height: 1.15;
    font-size: 11px;
}
.phase-board-arrival .arrival-card .card-footer button {
    width: 102px;
    min-height: 36px;
    height: auto;
    line-height: 1.15;
    white-space: normal;
}
.phase-board-research .phase-advisory,
.phase-board-drone-ops .resource-bank {
    width: 704px;
}
.phase-board-drone-ops .drone-top-row {
    display: flex;
    flex-direction: row;
    width: 704px;
    margin-top: 6px;
    margin-bottom: 6px;
    gap: 8px;
}
.phase-board-drone-ops .drone-top-row .resource-bank {
    margin-top: 0px;
    margin-bottom: 0px;
}
.phase-board-drone-ops .drone-build-guidance {
    width: 240px;
    display: flex;
    flex-direction: row;
    justify-content: flex-start;
    align-items: flex-start;
    padding: 7px;
    margin-top: 0px;
    margin-bottom: 0px;
    background-color: rgba(33, 28, 46, 0.76);
    border-color: rgba(183, 132, 255, 0.24);
}
.phase-board-drone-ops .drone-build-guidance .drone-guidance-copy {
    width: 94px;
}
.phase-board-drone-ops .drone-build-guidance h2 {
    color: #d8c7ff;
    margin-top: 0px;
    margin-bottom: 2px;
}
.phase-board-drone-ops .drone-build-guidance p {
    width: 92px;
    margin-top: 0px;
    margin-bottom: 0px;
    font-size: 12px;
    line-height: 1.14;
}
.phase-board-drone-ops .drone-build-guidance .drone-guidance-stats {
    width: 136px;
    margin-top: 0px;
    justify-content: flex-end;
}
.phase-board-drone-ops .drone-build-guidance .stat-chip {
    width: 126px;
    min-height: 16px;
    padding: 2px 5px;
    margin-left: 0px;
    margin-bottom: 3px;
    font-size: 10px;
}
.phase-board-drone-ops .drone-build-guidance .stat-chip.wide {
    width: 126px;
}
.phase-board-drone-ops .drone-loadout-bench {
    width: 704px;
    margin-top: 6px;
    margin-bottom: 8px;
    padding: 8px 10px;
}
.phase-board-drone-ops .drone-loadout-grid {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    width: 684px;
}
.phase-board-drone-ops .drone-loadout-slot {
    width: 196px;
    min-height: 96px;
    padding: 6px;
    margin-right: 7px;
    margin-bottom: 7px;
    border-width: 1px;
    border-color: rgba(137, 178, 211, 0.16);
    background-color: rgba(255, 255, 255, 0.03);
}
.phase-board-drone-ops .drone-loadout-slot.filled {
    border-color: rgba(71, 220, 255, 0.28);
    background-color: rgba(19, 48, 58, 0.58);
}
.phase-board-drone-ops .drone-loadout-slot.open {
    border-color: rgba(112, 224, 168, 0.28);
    background-color: rgba(20, 54, 39, 0.52);
}
.phase-board-drone-ops .drone-loadout-slot.locked {
    border-color: rgba(137, 178, 211, 0.12);
    background-color: rgba(255, 255, 255, 0.018);
}
.phase-board-drone-ops .drone-loadout-slot.role-attack {
    border-color: rgba(71, 220, 255, 0.38);
}
.phase-board-drone-ops .drone-loadout-slot.role-defense {
    border-color: rgba(183, 255, 241, 0.36);
}
.phase-board-drone-ops .drone-loadout-slot.role-mining {
    border-color: rgba(112, 224, 168, 0.34);
}
.phase-board-drone-ops .drone-loadout-slot.role-resource {
    border-color: rgba(255, 209, 102, 0.34);
}
.phase-board-drone-ops .drone-loadout-slot.role-survey {
    border-color: rgba(183, 132, 255, 0.34);
}
.phase-board-drone-ops .drone-loadout-slot.role-hazard {
    border-color: rgba(106, 224, 166, 0.42);
}
.phase-board-drone-ops .drone-loadout-slot .slot-topline {
    width: 196px;
    min-height: 16px;
    color: #89b2d3;
    font-size: 11px;
}
.phase-board-drone-ops .drone-loadout-slot .slot-topline span {
    width: 74px;
}
.phase-board-drone-ops .drone-loadout-slot .slot-topline strong {
    width: 114px;
    color: #ffd166;
    text-align: right;
}
.phase-board-drone-ops .drone-loadout-slot h3 {
    margin-top: 2px;
    margin-bottom: 1px;
    font-size: 13px;
}
.phase-board-drone-ops .drone-loadout-slot p {
    max-height: none;
    margin-top: 2px;
    margin-bottom: 0px;
    line-height: 1.15;
    overflow: visible;
}
.phase-board-drone-ops .drone-loadout-slot .slot-role {
    min-height: 14px;
    color: #bfeef7;
}
.phase-board-drone-ops .drone-loadout-slot .stat-grid {
    min-height: 16px;
    margin-top: 2px;
}
.phase-board-drone-ops .drone-loadout-slot .stat-chip {
    width: 58px;
    min-height: 15px;
    padding: 2px 5px;
    margin-right: 4px;
    margin-bottom: 3px;
    font-size: 11px;
    line-height: 1.12;
}
.phase-board-drone-ops .drone-loadout-slot .stat-chip.wide {
    width: 88px;
}
.phase-board-drone-ops .drone-combat-forecast {
    width: 704px;
    padding: 8px 10px;
    margin-top: 6px;
    margin-bottom: 6px;
    background-color: rgba(37, 32, 17, 0.72);
    border-color: rgba(255, 209, 102, 0.26);
}
.phase-board-drone-ops .drone-combat-forecast .drone-forecast-copy {
    width: 180px;
}
.phase-board-drone-ops .drone-combat-forecast h2 {
    color: #ffd166;
    margin-top: 0px;
}
.phase-board-drone-ops .drone-combat-forecast p {
    width: 170px;
    font-size: 12px;
    line-height: 1.18;
}
.phase-board-drone-ops .drone-combat-forecast .drone-forecast-stats {
    width: 482px;
    margin-top: 0px;
    justify-content: flex-end;
}
.phase-board-drone-ops .drone-combat-forecast .stat-chip {
    width: 84px;
    min-height: 24px;
    padding: 4px 5px;
    margin-left: 5px;
    margin-bottom: 5px;
}
.phase-board-drone-ops .drone-combat-forecast .stat-chip.wide {
    width: 112px;
}
.phase-board-research .board-primary,
.phase-board-drone-ops .board-primary {
    width: 704px;
    margin-top: 10px;
    padding: 8px 10px;
}
.phase-board-research .board-primary h2,
.phase-board-drone-ops .board-primary h2 {
    margin-top: 0px;
}
.phase-board-research .actions,
.phase-board-drone-ops .actions {
    width: 704px;
    margin-top: 12px;
    justify-content: center;
}
.phase-board-research .actions button,
.phase-board-drone-ops .actions button {
    width: 210px;
    min-height: 38px;
    height: auto;
    line-height: 1.15;
}
.phase-board-drone-ops .drone-combat-forecast {
    padding: 7px 9px;
    margin-top: 5px;
    margin-bottom: 5px;
}
.phase-board-drone-ops .drone-combat-forecast .drone-forecast-copy {
    width: 188px;
}
.phase-board-drone-ops .drone-combat-forecast p {
    width: 180px;
    font-size: 11px;
    line-height: 1.12;
}
.phase-board-drone-ops .drone-combat-forecast .drone-forecast-stats {
    width: 480px;
}
.phase-board-drone-ops .drone-combat-forecast {
    display: none;
}
.phase-board-drone-ops .drone-loadout-bench,
.phase-board-drone-ops .drone-roster {
    margin-top: 6px;
    margin-bottom: 6px;
    padding: 7px 9px;
}
.phase-board-drone-ops .section-heading {
    display: flex;
    flex-direction: row;
    justify-content: space-between;
    align-items: baseline;
    width: 684px;
    margin-bottom: 4px;
}
.phase-board-drone-ops .section-heading h2 {
    margin-top: 0px;
    margin-bottom: 0px;
}
.phase-board-drone-ops .section-heading p {
    width: 360px;
    margin-top: 0px;
    margin-bottom: 0px;
    color: #9eaebe;
    font-size: 11px;
    line-height: 1.1;
    text-align: right;
}
.phase-board-drone-ops .drone-loadout-slot {
    min-height: 96px;
    width: 196px;
}
.phase-board-drone-ops .drone-loadout-slot p {
    max-height: none;
}
.phase-board-drone-ops .drone-control-grid {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    width: 684px;
}
.phase-board-drone-ops .drone-control-card {
    width: 196px;
    min-height: 92px;
    padding: 6px;
    margin-right: 7px;
    margin-bottom: 7px;
    border-width: 1px;
    border-color: rgba(137, 178, 211, 0.16);
    border-radius: 6px;
    background-color: rgba(255, 255, 255, 0.03);
}
.phase-board-drone-ops .drone-control-card h3 {
    margin-top: 2px;
    margin-bottom: 1px;
    font-size: 12px;
}
.phase-board-drone-ops .drone-control-status {
    width: 188px;
    min-height: 12px;
    max-height: 14px;
    margin-top: 0px;
    margin-bottom: 2px;
    color: #9eaebe;
    font-size: 10px;
    line-height: 1.12;
    overflow: hidden;
}
.phase-board-drone-ops .drone-control-card .stat-grid {
    width: 188px;
    min-height: 28px;
    margin-top: 1px;
}
.phase-board-drone-ops .drone-control-card .stat-chip {
    width: 84px;
    min-height: 15px;
    padding: 2px 5px;
    font-size: 10px;
    line-height: 1.1;
}
.phase-board-drone-ops .drone-control-card .card-footer {
    width: 188px;
    min-height: 24px;
    margin-top: auto;
    padding-top: 3px;
    border-top-width: 1px;
    border-top-color: #263b4c;
    gap: 5px;
}
.phase-board-drone-ops .drone-control-card .card-footer button {
    width: 82px;
    min-height: 22px;
    padding: 3px 5px;
    font-size: 9px;
    line-height: 1.1;
}
.phase-board-navigation {
    width: 736px;
    padding-bottom: 12px;
}
.phase-board-navigation .ark-status,
.phase-board-navigation .navigation-map {
    box-sizing: border-box;
    width: )" + std::to_string(kPhaseContentLaneWidth) + R"(px;
    margin-left: )" + std::to_string(kPhaseLaneInset) + R"(px;
    margin-right: )" + std::to_string(kPhaseLaneInset) + R"(px;
    margin-top: 6px;
    padding: 8px 10px;
}
.phase-board-navigation .navigation-map {
    padding-right: 0px;
}
.phase-board-navigation .ark-status {
    display: flex;
    flex-direction: row;
    justify-content: space-between;
    align-items: center;
}
.phase-board-navigation .ark-status > div {
    width: 338px;
}
.phase-board-navigation .ark-status .stat-grid {
    width: 300px;
    margin-top: 0px;
    justify-content: flex-end;
}
.phase-board-navigation .nav-grid {
    display: flex;
    flex-direction: row;
    flex-wrap: nowrap;
    width: 100%;
    margin-top: 4px;
    align-items: stretch;
    gap: 8px;
}
.phase-board-navigation .nav-card {
    box-sizing: border-box;
    flex: 1 1 0px;
    width: 0px;
    min-width: 0px;
    min-height: 168px;
    margin-top: 6px;
    margin-right: 0px;
    padding: 8px 10px;
}
.phase-board-navigation .nav-card.selected {
    background-color: #10241f;
    border-color: #72e0a8;
}
.phase-board-navigation .nav-card .card-kicker span {
    width: 140px;
    height: 14px;
    font-size: 10px;
    overflow: hidden;
}
.phase-board-navigation .nav-card h3 {
    min-height: 18px;
    margin-top: 4px;
    margin-bottom: 2px;
    font-size: 14px;
    overflow: hidden;
}
.phase-board-navigation .nav-card p {
    width: auto;
    align-self: stretch;
    min-height: 26px;
    margin-top: 2px;
    margin-bottom: 5px;
    font-size: 12px;
    line-height: 1.12;
    overflow: visible;
}
.phase-board-navigation .nav-card .stat-grid {
    width: auto;
    align-self: stretch;
    min-height: 38px;
    margin-top: 0px;
    align-content: flex-start;
    align-items: flex-start;
}
.phase-board-navigation .nav-card .stat-chip {
    width: 124px;
    min-height: 16px;
    line-height: 1.08;
    margin-top: 0px;
    margin-right: 5px;
    margin-bottom: 4px;
    padding: 2px 5px;
    font-size: 10px;
}
.phase-board-navigation .nav-card .stat-chip.wide {
    width: 136px;
}
.phase-board-navigation .nav-card .card-footer {
    width: auto;
    align-self: stretch;
    min-height: 32px;
    height: auto;
    margin-top: auto;
    padding-top: 5px;
    border-top-width: 1px;
    border-top-color: #263b4c;
    align-items: center;
}
.phase-board-navigation .nav-card .card-footer span {
    flex: 1 1 auto;
    width: auto;
    min-height: 24px;
    color: #d7c276;
    font-size: 12px;
    line-height: 1.15;
    overflow: hidden;
}
.phase-board-navigation .nav-card .card-footer button {
    width: 140px;
    min-height: 30px;
    height: auto;
    line-height: 1.15;
    margin-top: 0px;
    margin-right: 0px;
    padding-left: 6px;
    padding-right: 6px;
    font-size: 11px;
}
.phase-board-drone-ops .drone-bay-strip {
    display: flex;
    flex-direction: row;
    justify-content: space-between;
    align-items: center;
    width: 412px;
    padding: 7px;
    margin-top: 0px;
}
.phase-board-drone-ops .drone-bay-strip .drone-bay-copy {
    width: 150px;
}
.phase-board-drone-ops .drone-bay-strip h2 {
    margin-top: 0px;
    margin-bottom: 2px;
}
.phase-board-drone-ops .drone-bay-strip p {
    width: 146px;
    margin-top: 0px;
    margin-bottom: 0px;
    line-height: 1.18;
}
.phase-board-drone-ops .drone-bay-strip .stat-grid {
    margin-top: 0px;
}
.phase-board-drone-ops .drone-bay-stats {
    width: 86px;
}
.phase-board-drone-ops .drone-bay-materials {
    width: 86px;
}
.phase-board-drone-ops .drone-bay-strip .stat-chip {
    max-width: 84px;
    min-height: 22px;
    padding: 4px 6px;
    font-size: 11px;
    line-height: 1.12;
}
.phase-board-drone-ops .drone-bay-strip .stat-chip.wide {
    max-width: 116px;
}
.phase-board-drone-ops .drone-bay-strip button {
    width: 78px;
    min-height: 28px;
    height: auto;
    line-height: 1.15;
    padding-left: 6px;
    padding-right: 6px;
    font-size: 10px;
    white-space: normal;
}
.phase-board-drone-ops .drone-roster {
    margin-top: 6px;
    padding: 8px 10px;
}
.phase-board-drone-ops .board-primary h2 {
    margin-bottom: 2px;
}
.phase-board-surface {
    width: )" + std::to_string(kPhaseBoardFrameWidth) + R"(px;
    padding-bottom: 44px;
}
.phase-board-surface .phase-titlebar,
.phase-board-surface .surface-command,
.phase-board-surface .surface-quickbar,
.phase-board-surface .surface-kpi-grid,
.phase-board-surface .drone-ops-callout,
.phase-board-surface .surface-arena-forecast,
.phase-board-surface .surface-primary-action,
.phase-board-surface .board-primary {
    width: )" + std::to_string(kPhaseContentLaneWidth) + R"(px;
    margin-left: )" + std::to_string(kPhaseLaneInset) + R"(px;
    margin-right: )" + std::to_string(kPhaseLaneInset) + R"(px;
}
.phase-board-surface .phase-titlebar {
    margin-bottom: 4px;
}
.phase-board-surface .phase-titlebar > div,
.phase-board-surface .phase-titlebar p {
    width: 330px;
}
.phase-board-surface .compact-tools {
    width: 374px;
    justify-content: flex-end;
}
.phase-board-surface.surface-ops-screen .compact-tools {
    width: 374px;
    margin-right: 0px;
}
.phase-board-surface .compact-tools button {
    width: 96px;
    margin-left: 6px;
    margin-right: 0px;
}
.phase-board-surface .surface-command {
    display: flex;
    flex-direction: row;
    justify-content: space-between;
    padding: 0px;
    margin-top: 8px;
}
.phase-board-surface .surface-site-card,
.phase-board-surface .surface-posture {
    min-height: 76px;
    padding: 10px;
    background-color: #131c26;
    border-width: 1px;
    border-color: #35495a;
    border-radius: 6px;
}
.phase-board-surface .surface-site-card {
    width: 326px;
    margin-right: 8px;
}
.phase-board-surface .surface-site-card span {
    color: #7f8f9f;
    font-size: 12px;
}
.phase-board-surface .surface-site-card strong {
    display: block;
    margin-top: 4px;
    color: #dce6ee;
    font-size: 15px;
}
.phase-board-surface .surface-site-card p {
    margin-top: 6px;
    line-height: 1.25;
}
.phase-board-surface .surface-posture {
    width: 326px;
    margin-top: 0px;
    margin-right: 0px;
}
.phase-board-surface .surface-kpi-grid {
    margin-top: 8px;
    margin-bottom: 6px;
}
.phase-board-surface .surface-quickbar {
    flex-wrap: nowrap;
    margin-top: 4px;
    margin-bottom: 4px;
}
.phase-board-surface.surface-ops-screen .phase-titlebar,
.phase-board-surface.surface-ops-screen .surface-quickbar,
.phase-board-surface.surface-ops-screen .surface-kpi-grid,
.phase-board-surface.surface-ops-screen .drone-ops-callout,
.phase-board-surface.surface-ops-screen .surface-arena-forecast,
.phase-board-surface.surface-ops-screen .surface-primary-action,
.phase-board-surface.surface-ops-screen .board-primary {
    box-sizing: border-box;
    width: )" + std::to_string(kPhaseContentLaneWidth) + R"(px;
    margin-left: )" + std::to_string(kPhaseLaneInset) + R"(px;
    margin-right: )" + std::to_string(kPhaseLaneInset) + R"(px;
}
.phase-board-surface.surface-ops-screen > .phase-titlebar {
    display: flex;
    flex-direction: row;
    flex-wrap: nowrap;
    align-items: flex-start;
    justify-content: space-between;
}
.phase-board-surface.surface-ops-screen > .phase-titlebar > div {
    flex: 1 1 auto;
    width: auto;
    min-width: 0px;
}
.phase-board-surface.surface-ops-screen > .phase-titlebar > .compact-tools {
    flex: 0 0 auto;
    width: auto;
    margin-left: auto;
}
.phase-board-surface .surface-kpi {
    width: 128px;
    min-height: 40px;
    padding: 7px 8px;
}
.phase-board-surface .surface-quickbar .surface-kpi {
    width: 74px;
    min-height: 32px;
    padding: 4px 6px;
    margin-top: 0px;
    margin-right: 6px;
}
.phase-board-surface.surface-ops-screen .surface-quickbar .surface-kpi {
    box-sizing: border-box;
    flex: 1 1 74px;
    width: auto;
}
.phase-board-surface .surface-quickbar .surface-quick-item.is-last {
    margin-right: 0px;
}
.phase-board-surface .surface-quickbar .surface-quick-site {
    width: 104px;
}
.phase-board-surface.surface-ops-screen .surface-quickbar .surface-quick-site {
    flex: 1 1 104px;
    width: auto;
}
.phase-board-surface .surface-quickbar .surface-quick-next {
    width: 184px;
}
.phase-board-surface.surface-ops-screen .surface-quickbar .surface-quick-next {
    flex: 2 1 184px;
    width: auto;
}
.phase-board-surface .surface-kpi strong {
    font-size: 15px;
    margin-top: 3px;
}
.phase-board-surface .surface-quickbar .surface-kpi span {
    font-size: 11px;
}
.phase-board-surface .surface-quickbar .surface-kpi strong {
    font-size: 13px;
    line-height: 1.15;
    margin-top: 2px;
}
.phase-board-surface .resource-bank {
    display: flex;
    flex-direction: row;
    justify-content: space-between;
    padding: 8px 10px;
    margin-top: 4px;
}
.phase-board-surface .resource-bank > div {
    width: 520px;
}
.phase-board-surface .resource-bank button {
    width: )" + std::to_string(kPhaseCommonButtonWidth) + R"(px;
    min-height: 32px;
    height: auto;
    line-height: 1.15;
    margin-top: 3px;
    margin-right: 0px;
}
.phase-board-surface.surface-ops-screen .drone-ops-callout > div {
    flex: 1 1 auto;
    width: auto;
    min-width: 0px;
}
.phase-board-surface.surface-ops-screen .drone-ops-callout button {
    flex: 0 0 140px;
    width: 140px;
    margin-left: auto;
    margin-right: 0px;
}
.phase-board-surface.surface-ops-screen .drone-ops-callout {
    min-height: 44px;
    padding: 6px 10px;
}
.phase-board-surface.surface-ops-screen .drone-ops-callout h2 {
    font-size: 14px;
    line-height: 1.1;
}
.phase-board-surface.surface-ops-screen .drone-ops-callout p {
    margin-top: 2px;
    font-size: 11px;
    line-height: 1.1;
}
.phase-board-surface.surface-ops-screen .surface-arena-forecast {
    min-height: 0px;
    padding: 5px 10px;
    margin-top: 0px;
}
.phase-board-surface.surface-ops-screen .surface-arena-forecast > div {
    width: 100%;
}
.phase-board-surface.surface-ops-screen .surface-arena-forecast h2 {
    margin-top: 0px;
    margin-bottom: 2px;
    font-size: 13px;
    line-height: 1.1;
}
.phase-board-surface.surface-ops-screen .surface-arena-forecast p {
    margin-top: 0px;
    font-size: 11px;
    line-height: 1.1;
}
.phase-board-surface.surface-ops-screen .surface-actions {
    padding-bottom: 4px;
}
.phase-board-surface .surface-primary-action {
    display: flex;
    flex-direction: row;
    justify-content: flex-start;
    padding: 9px;
    margin-top: 7px;
    border-color: #4c6d5a;
    background-color: #111f22;
}
.phase-board-surface .surface-primary-copy {
    width: 496px;
    margin-right: 18px;
}
.phase-board-surface .surface-action-topline {
    display: flex;
    flex-direction: row;
    justify-content: flex-start;
    margin-bottom: 5px;
}
.phase-board-surface .surface-action-topline span {
    width: 188px;
    color: #d7c276;
    font-size: 12px;
    line-height: 1.15;
}
.phase-board-surface .surface-primary-action p {
    width: 482px;
    min-height: 26px;
    margin-top: 5px;
    line-height: 1.25;
}
.phase-board-surface .surface-primary-action .stat-grid {
    width: 482px;
    min-height: 28px;
    margin-top: 5px;
}
.phase-board-surface .surface-primary-control {
    width: 132px;
    margin-top: 8px;
}
.phase-board-surface .surface-primary-control span {
    display: block;
    width: 132px;
    margin-bottom: 8px;
    color: #d7c276;
    font-size: 12px;
    text-align: center;
}
.phase-board-surface .surface-primary-action button {
    width: 132px;
    min-height: 38px;
    height: auto;
    line-height: 1.15;
    margin-top: 0px;
    margin-right: 0px;
}
.phase-board-surface .board-primary {
    padding: 2px 0px 24px 0px;
    margin-top: 2px;
}
.phase-board-surface .surface-actions .phase-titlebar {
    width: 704px;
    margin-bottom: 0px;
}
.phase-board-surface.surface-ops-screen .surface-actions .phase-titlebar {
    width: 704px;
}
.phase-board-surface .surface-actions .phase-titlebar h2 {
    margin-top: 6px;
    margin-bottom: 4px;
}
.phase-board-surface .surface-actions .ops-grid {
    width: )" + std::to_string(kPhaseContentLaneWidth) + R"(px;
}
.phase-board-surface.surface-ops-screen .surface-actions .ops-grid {
    box-sizing: border-box;
    display: flex;
    flex-direction: row;
    flex-wrap: nowrap;
    width: 100%;
}
.phase-board-surface .surface-action-slot {
    width: )" + std::to_string(kPhaseCardSlotWidth) + R"(px;
    margin-right: )" + std::to_string(kPhaseCardGap) + R"(px;
}
.phase-board-surface.surface-ops-screen .surface-action-slot {
    flex: 1 1 0px;
    width: auto;
}
.phase-board-surface .surface-action-slot.is-last {
    margin-right: 0px;
}
.phase-board-surface .surface-action-slot .surface-action-card {
    margin-right: 0px;
}
.phase-board-surface.surface-ops-screen .surface-action-slot .surface-action-card {
    box-sizing: border-box;
    width: 100%;
}
.phase-board-surface .surface-action-card {
    display: flex;
    flex-direction: column;
    width: 154px;
    min-height: 292px;
    padding: 7px;
    margin-right: 6px;
}
.phase-board-surface.surface-ops-screen .surface-action-card {
    width: 154px;
    min-height: 344px;
}
.phase-board-surface .surface-action-card .card-topline,
.phase-board-surface .surface-action-card h3,
.phase-board-surface .surface-action-card p,
.phase-board-surface .surface-action-card .stat-grid {
    flex: none;
}
.phase-board-surface .surface-action-card.featured-action {
    border-color: #4c6d5a;
    background-color: #111f22;
}
.phase-board-surface .surface-action-card h3 {
    margin-top: 3px;
    margin-bottom: 3px;
    font-size: 14px;
}
.phase-board-surface .surface-action-card p {
    width: 140px;
    min-height: 62px;
    margin-top: 5px;
    margin-bottom: 6px;
    font-size: 12px;
    line-height: 1.22;
    overflow: visible;
}
.phase-board-surface.surface-ops-screen .surface-action-card p {
    width: 140px;
    min-height: 72px;
}
.phase-board-surface .surface-action-card .stat-grid {
    width: 140px;
    min-height: 82px;
    height: auto;
    align-content: flex-start;
    align-items: flex-start;
    flex-wrap: wrap;
}
.phase-board-surface.surface-ops-screen .surface-action-card .stat-grid {
    width: 140px;
    min-height: 0px;
    height: auto;
}
.phase-board-surface .surface-action-card .stat-grid .stat-chip {
    flex: none;
    width: 62px;
    min-height: 16px;
    margin-top: 0px;
    margin-bottom: 3px;
    padding: 0px 4px;
    font-size: 11px;
    line-height: 16px;
    overflow: hidden;
}
.phase-board-surface .surface-action-card .stat-grid .stat-chip.wide {
    width: 132px;
}
.phase-board-surface.surface-ops-screen .surface-action-card .stat-grid .stat-chip.wide {
    width: 132px;
}
.phase-board-surface .surface-action-card .card-footer {
    width: 140px;
    min-height: 52px;
    margin-top: auto;
    padding-top: 6px;
    border-top-width: 1px;
    border-top-color: #263b4c;
    display: flex;
    flex-direction: column;
    align-items: center;
}
.phase-board-surface.surface-ops-screen .surface-action-card .card-footer {
    width: 140px;
    min-height: 60px;
    margin-top: auto;
}
.phase-board-surface .surface-action-card .card-footer span {
    width: 140px;
    min-height: 16px;
    color: #d7c276;
    font-size: 11px;
    line-height: 16px;
    overflow: hidden;
}
.phase-board-surface.surface-ops-screen .surface-action-card .card-footer span {
    width: 140px;
    min-height: 18px;
    line-height: 18px;
}
.phase-board-surface .surface-action-card .card-footer button {
    width: 104px;
    min-height: 27px;
    height: auto;
    line-height: 1.15;
    margin-top: 3px;
    margin-left: 0px;
    margin-right: 0px;
    padding-left: 3px;
    padding-right: 3px;
    font-size: 11px;
}
.phase-board-surface.surface-ops-screen .surface-action-card .card-footer button {
    width: 120px;
    min-height: 30px;
    height: auto;
    line-height: 1.15;
    margin-left: 0px;
}
.phase-board-surface-minigame .surface-minigame,
.phase-board-surface-minigame .minigame-readout,
.phase-board-surface-minigame .minigame-callout,
.phase-board-surface-minigame .minigame-actions {
    width: 704px;
}
.phase-board-surface-minigame .surface-minigame {
    margin-top: 6px;
}
.phase-board-surface-minigame .phase-titlebar {
    margin-bottom: 6px;
}
.phase-board-surface-minigame .minigame-readout {
    display: flex;
    flex-direction: row;
    justify-content: flex-start;
    margin-top: 2px;
}
.phase-board-surface-minigame .minigame-metrics {
    width: 456px;
    flex-shrink: 0;
}
.phase-board-surface-minigame .minigame-metric-row {
    display: flex;
    flex-direction: row;
    width: 456px;
}
.phase-board-surface-minigame .minigame-metrics .metric {
    width: 204px;
    min-height: 52px;
    margin-right: 8px;
    margin-bottom: 8px;
}
.phase-board-surface-minigame .minigame-rewards {
    width: 210px;
    flex-shrink: 0;
    margin-left: 16px;
    justify-content: flex-end;
    align-content: flex-start;
}
.phase-board-surface-minigame .minigame-rewards .stat-chip {
    width: 132px;
    height: 28px;
    min-height: 28px;
    margin-left: 6px;
    margin-bottom: 6px;
    line-height: 28px;
    text-align: center;
    white-space: nowrap;
    overflow: hidden;
}
.phase-board-surface-minigame .minigame-callout {
    min-height: 78px;
    border-color: #5a4e25;
    background-color: #111c21;
}
.phase-board-surface-minigame .minigame-callout > div {
    width: 650px;
}
.phase-board-surface-minigame .minigame-callout h2 {
    color: #f2d15f;
}
.phase-board-surface-minigame .minigame-actions {
    display: flex;
    flex-direction: row;
    justify-content: center;
    margin-top: 10px;
}
.phase-board-surface-minigame .minigame-actions button {
    width: 176px;
    min-height: 38px;
    height: auto;
    line-height: 1.15;
    margin-right: 8px;
}
.phase-board-surface-upgrade {
    width: 736px;
    padding-bottom: 24px;
}
.phase-board-surface-upgrade .draft-card-grid {
    width: 736px;
}
.phase-board-surface-upgrade .upgrade-draft-card {
    width: 206px;
    min-height: 368px;
    padding: 10px;
    margin-top: 8px;
    margin-right: 8px;
    background-color: #151c24;
    border-width: 2px;
    border-color: #4f8e6b;
}
.phase-board-surface-upgrade .upgrade-draft-card .pilot-card-top {
    margin-bottom: 6px;
    padding-bottom: 4px;
    border-bottom-width: 1px;
    border-bottom-color: #34485a;
}
.phase-board-surface-upgrade .upgrade-draft-card .draft-art {
    height: 48px;
    margin-top: 4px;
    margin-bottom: 6px;
    background-color: #102019;
    border-width: 1px;
    border-color: #4f8e6b;
    border-radius: 6px;
}
.phase-board-surface-upgrade .upgrade-draft-card .draft-art span {
    width: 100%;
    color: #8de0b3;
    font-size: 24px;
    line-height: 48px;
    text-align: center;
}
.phase-board-surface-upgrade .upgrade-draft-card h3,
.phase-board-refit .upgrade-draft-card h3 {
    min-height: 34px;
    height: auto;
    margin-bottom: 5px;
    line-height: 1.16;
    overflow: hidden;
}
.phase-board-surface-upgrade .upgrade-draft-card p {
    min-height: 48px;
    margin-bottom: 6px;
    line-height: 1.28;
    overflow: hidden;
}
.phase-board-surface-upgrade .upgrade-draft-card .stat-grid {
    width: 186px;
    min-height: 0px;
    height: auto;
    margin-top: 5px;
    justify-content: center;
    align-content: flex-start;
    align-items: flex-start;
}
.phase-board-surface-upgrade .upgrade-draft-card .stat-chip {
    width: 70px;
    min-height: 20px;
    line-height: 1.15;
    margin-top: 0px;
    margin-right: 5px;
    margin-bottom: 4px;
    padding: 0px 4px;
    font-size: 11px;
    overflow: hidden;
}
.phase-board-surface-upgrade .upgrade-draft-card .stat-chip.wide {
    width: 146px;
    margin-right: 0px;
}
.phase-board-surface-upgrade .upgrade-draft-card .draft-card-footer {
    min-height: 52px;
    height: auto;
    margin-top: auto;
    padding-top: 8px;
    border-top-width: 1px;
    border-top-color: #34485a;
    align-items: center;
}
.phase-board-surface-upgrade .upgrade-draft-card .draft-card-footer span {
    width: 78px;
    color: #d7c276;
    font-size: 12px;
    line-height: 12px;
}
.phase-board-surface-upgrade .upgrade-draft-card .draft-card-footer button {
    width: 98px;
    min-height: 34px;
    height: auto;
    line-height: 1.15;
    margin-top: 0px;
    margin-right: 0px;
    padding-left: 6px;
    padding-right: 6px;
    font-size: 12px;
}
.phase-board-surface-upgrade .draft-actions {
    flex-wrap: nowrap;
    width: 704px;
    margin-left: 0px;
    margin-top: 12px;
    margin-bottom: 28px;
    justify-content: center;
}
.phase-board-surface-upgrade .draft-actions button {
    width: 224px;
    min-height: 38px;
    height: auto;
    line-height: 1.15;
    margin-right: 8px;
}
.phase-board-hangar {
    width: 736px;
    padding-bottom: 28px;
}
.phase-board-hangar .summary-grid,
.phase-board-hangar .ops-grid {
    width: 704px;
}
.phase-board-hangar .summary-card {
    width: 202px;
}
.phase-board-hangar .ops-card {
    width: 202px;
    min-height: 182px;
    padding: 10px;
    margin-top: 10px;
    margin-right: 8px;
}
.phase-board-hangar .ops-card h3 {
    margin-bottom: 7px;
}
.phase-board-hangar .ops-card .ops-detail {
    color: #9aabba;
    min-height: 78px;
    margin-top: 0px;
    margin-bottom: 10px;
    line-height: 1.28;
    overflow: visible;
}
.phase-board-hangar .ops-card .card-footer {
    display: flex;
    flex-direction: row;
    align-items: center;
    justify-content: space-between;
    min-height: 48px;
    height: auto;
    margin-top: auto;
    padding-top: 10px;
    border-top-width: 1px;
    border-top-color: #263b4c;
}
.phase-board-hangar .ops-card .ops-cost {
    color: #d7c276;
    width: 86px;
    padding: 0px;
    font-size: 13px;
    line-height: 1.2;
    overflow: hidden;
}
.phase-board-hangar .ops-card .card-footer button {
    width: 92px;
    min-height: 36px;
    height: auto;
    line-height: 1.15;
    margin-top: 0px;
    margin-right: 0px;
}
.phase-board-hangar .hangar-actions {
    width: 704px;
    margin-top: 12px;
    margin-bottom: 18px;
    justify-content: center;
}
.phase-board-hangar .hangar-actions button {
    width: 196px;
    margin-right: 8px;
}
.phase-board-draft-room .draft-hero {
    width: 708px;
    margin-top: 8px;
    margin-bottom: 10px;
    padding: 12px;
    background-color: #111b23;
    border-width: 1px;
    border-color: #6d5d35;
    border-radius: 8px;
}
.phase-board-draft-room .draft-hero span,
.phase-board-draft-room .draft-card-footer span {
    color: #d7c276;
}
.phase-board-draft-room .draft-hero h2 {
    margin-top: 3px;
    margin-bottom: 5px;
    color: #edf4f8;
}
.phase-board-draft-room .draft-board {
    width: 736px;
    margin-top: 8px;
}
.phase-board-refit {
    width: 736px;
    padding-bottom: 24px;
}
.phase-board-panel.draft-room-panel .panel-kpis {
    margin-top: 6px;
    margin-bottom: 6px;
}
.phase-board-panel.draft-room-panel .panel-kpis .metric {
    height: 54px;
    padding: 7px 9px;
}
.phase-board-panel.draft-room-panel .panel-kpis .metric strong {
    line-height: 17px;
}
.phase-board-panel.draft-room-panel .panel-kpis .metric span {
    line-height: 12px;
}
.phase-board-refit .draft-card-grid {
    width: 736px;
    justify-content: center;
}
.phase-board-refit .upgrade-draft-card {
    width: 206px;
    min-height: 368px;
    padding: 10px;
    margin-top: 8px;
    margin-right: 8px;
    background-color: #151c24;
    border-width: 2px;
    border-color: #6d5d35;
}
.phase-board-refit .upgrade-draft-card .pilot-card-top {
    margin-bottom: 6px;
    padding-bottom: 4px;
    border-bottom-width: 1px;
    border-bottom-color: #34485a;
}
.phase-board-refit .upgrade-draft-card .draft-art {
    height: 48px;
    margin-top: 4px;
    margin-bottom: 6px;
    background-color: #0c141d;
    border-width: 1px;
    border-color: #3c596a;
    border-radius: 6px;
}
.phase-board-refit .upgrade-draft-card .draft-art span {
    width: 100%;
    color: #8fd8f0;
    font-size: 24px;
    line-height: 48px;
    text-align: center;
}
)" + R"(
.phase-board-refit .upgrade-draft-card p {
    min-height: 46px;
    margin-bottom: 5px;
    line-height: 1.25;
    overflow: hidden;
}
.phase-board-refit .upgrade-draft-card .module-impact {
    color: #edf4f8;
    font-size: 13px;
    min-height: 30px;
    line-height: 14px;
    overflow: hidden;
}
.phase-board-refit .upgrade-draft-card .stat-grid {
    width: 186px;
    min-height: 0px;
    height: auto;
    margin-top: 5px;
    justify-content: center;
    align-content: flex-start;
    align-items: flex-start;
}
.phase-board-refit .upgrade-draft-card .stat-chip {
    width: 70px;
    min-height: 20px;
    line-height: 1.15;
    margin-top: 0px;
    margin-right: 5px;
    margin-bottom: 4px;
    padding: 0px 4px;
    font-size: 11px;
    overflow: hidden;
}
.phase-board-refit .upgrade-draft-card .stat-chip.wide {
    width: 146px;
    margin-right: 0px;
}
.phase-board-refit .upgrade-draft-card .draft-card-footer {
    min-height: 60px;
    height: auto;
    margin-top: auto;
    padding-top: 8px;
    border-top-width: 1px;
    border-top-color: #34485a;
    align-items: center;
}
.phase-board-refit .upgrade-draft-card .draft-card-footer span {
    width: 78px;
    min-height: 44px;
    font-size: 12px;
    line-height: 12px;
    overflow: hidden;
}
.phase-board-refit .upgrade-draft-card .draft-card-footer button {
    width: 98px;
    min-height: 44px;
    height: auto;
    line-height: 1.15;
    margin-top: 0px;
    margin-right: 0px;
    padding-left: 6px;
    padding-right: 6px;
    font-size: 12px;
}
.phase-board-refit .draft-actions {
    flex-wrap: nowrap;
    width: 704px;
    margin-left: 0px;
    margin-top: 8px;
    margin-bottom: 28px;
    justify-content: center;
}
.phase-board-refit .draft-actions button {
    width: 224px;
    min-height: 38px;
    height: auto;
    line-height: 1.15;
    margin-right: 8px;
}
.phase-board-results {
    width: 736px;
    padding-bottom: 28px;
}
.phase-board-results .debrief-hero {
    width: 704px;
    margin-top: 8px;
    margin-bottom: 12px;
    padding: 14px;
    background-color: #111b23;
    border-width: 1px;
    border-color: #6d5d35;
    border-radius: 8px;
}
.phase-board-results .debrief-hero span {
    color: #d7c276;
    font-size: 12px;
}
.phase-board-results .debrief-hero h2 {
    margin-top: 4px;
    margin-bottom: 6px;
    color: #edf4f8;
    font-size: 20px;
}
.phase-board-results .debrief-hero p {
    width: 672px;
    line-height: 1.35;
}
.phase-board-results .debrief-handoff {
    display: flex;
    flex-direction: row;
    align-items: center;
    width: 704px;
    min-height: 112px;
    margin-top: 8px;
    margin-bottom: 10px;
    padding: 10px 12px;
    background-color: #101923;
    border-width: 1px;
    border-color: #31566b;
    border-radius: 8px;
}
.phase-board-results .debrief-handoff-copy {
    width: 184px;
    margin-right: 14px;
}
.phase-board-results .debrief-handoff-copy span {
    color: #d7c276;
    font-size: 11px;
}
.phase-board-results .debrief-handoff-copy h3 {
    margin-top: 4px;
    margin-bottom: 5px;
    color: #edf4f8;
    font-size: 16px;
}
.phase-board-results .debrief-handoff-copy p {
    width: 180px;
    margin-top: 0px;
    margin-bottom: 0px;
    color: #9eaebe;
    font-size: 12px;
    line-height: 1.22;
}
.phase-board-results .debrief-handoff-plan {
    width: 360px;
    margin-right: 12px;
}
.phase-board-results .debrief-phase-track {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    width: 360px;
    margin-top: 0px;
    margin-bottom: 0px;
    padding: 0px;
}
.phase-board-results .phase-step-card {
    display: flex;
    flex-direction: column;
    width: 76px;
    min-height: 42px;
    margin-right: 7px;
    margin-bottom: 6px;
    padding: 6px 7px;
    background-color: #0d1d27;
    border-width: 1px;
    border-color: #31566b;
    border-radius: 6px;
}
.phase-board-results .phase-step-card.active {
    border-color: #70e0a8;
    background-color: #0d2119;
}
.phase-board-results .phase-step-card.done {
    border-color: #3d6174;
    background-color: #102332;
}
.phase-board-results .phase-step-card.pending {
    border-color: #34485a;
}
.phase-board-results .phase-step-card span {
    color: #9eaebe;
    font-size: 10px;
}
.phase-board-results .phase-step-card strong {
    margin-top: 3px;
    color: #edf4f8;
    font-size: 11px;
}
.phase-board-results .phase-step-card.active strong {
    color: #70e0a8;
}
.phase-board-results .debrief-handoff-actions {
    display: flex;
    flex-direction: column;
    width: 108px;
}
.phase-board-results .debrief-handoff-actions button {
    width: 106px;
    min-height: 34px;
    margin-right: 0px;
}
.phase-board-results .result-grid,
.phase-board-results .achievement-grid {
    display: flex;
    flex-direction: row;
    flex-wrap: wrap;
    width: 736px;
    margin-top: 8px;
}
.phase-board-results .crew-fate-card,
.phase-board-results .result-group,
.phase-board-results .achievement-card {
    padding: 11px 12px;
}
.phase-board-results .crew-fate-card {
    width: 704px;
    margin-top: 8px;
    margin-right: 0px;
}
.phase-board-results .crew-fate-card p {
    width: 646px;
    margin-top: 4px;
    line-height: 1.35;
}
.phase-board-results .crew-fate-signal {
    display: none;
}
.phase-board-results .result-group {
    width: 334px;
    min-height: 104px;
    margin-top: 8px;
    margin-right: 8px;
}
.phase-board-results .result-group.primary {
    width: 704px;
    margin-right: 0px;
}
.phase-board-results .result-group h3 {
    margin-bottom: 8px;
    padding-bottom: 6px;
    border-bottom-width: 1px;
    border-bottom-color: #34485a;
}
.phase-board-results .result-row {
    display: flex;
    flex-direction: row;
    justify-content: space-between;
    width: 100%;
    min-height: 30px;
    margin-top: 5px;
    padding-top: 5px;
    border-top-width: 1px;
    border-top-color: #263b4c;
}
.phase-board-results .result-group h3 + .result-row {
    border-top-width: 0px;
}
.phase-board-results .result-row span {
    width: 126px;
    color: #8295a5;
    font-size: 12px;
}
.phase-board-results .result-row strong {
    width: 160px;
    margin-top: 0px;
}
.phase-board-results .result-group.primary .result-row span {
    width: 178px;
}
.phase-board-results .result-group.primary .result-row strong {
    width: 486px;
}
.phase-board-results .board-note {
    width: 704px;
    margin-top: 8px;
    padding: 8px 12px;
    color: #d7c276;
    background-color: #101923;
    border-width: 1px;
    border-color: #4c4728;
    border-radius: 6px;
}
.phase-board-results .achievement-card {
    width: 704px;
    margin-right: 0px;
}
.phase-board-results .achievement-card p {
    width: 670px;
}
.phase-board-results .actions {
    width: 704px;
    margin-left: 16px;
    margin-right: 16px;
    margin-top: 16px;
    margin-bottom: 22px;
    justify-content: center;
}
.phase-board-results .actions button {
    width: 230px;
    min-height: 42px;
    height: auto;
    line-height: 1.15;
    margin-right: 0px;
}
.metric, .summary-card, .ops-card, .pilot-card, .inventory-item, .resource-bank, .phase-advisory, .cockpit-hud, .surface-primary-action, .achievement-card, .crew-fate-card, .tutorial-card, .result-group {
    margin-top: 8px;
    margin-right: 8px;
    padding: 9px 10px;
    background-color: #131c26;
    border-width: 1px;
    border-color: #35495a;
    border-radius: 6px;
}
.inventory-modal {
    width: 724px;
    margin-top: 10px;
}
.inventory-layout {
    display: flex;
    flex-direction: row;
    width: 724px;
}
.inventory-side-column {
    width: 164px;
    margin-right: 10px;
}
.inventory-main-column {
    width: 550px;
}
.inventory-modal.inventory-main-only,
.inventory-layout-main-only,
.inventory-layout-main-only .inventory-main-column,
.inventory-layout-main-only .inventory-section,
.inventory-layout-main-only .inventory-section-head,
.inventory-layout-main-only .inventory-grid {
    width: 724px;
}
.inventory-layout-main-only .inventory-section-head p {
    width: 696px;
}
.inventory-side-column .inventory-section {
    width: 164px;
    margin-top: 0px;
    padding-top: 0px;
    border-top-width: 0px;
}
.inventory-side-column .inventory-section-head {
    width: 164px;
}
.inventory-side-column .inventory-section-head p {
    width: 150px;
}
.inventory-side-column .inventory-grid {
    width: 164px;
}
.solar-map {
    display: block;
    width: 888px;
    color: #dce7ee;
}
.solar-map-summary,
.solar-system-track,
.solar-map-row,
.solar-map-lower,
.solar-map-legend,
.solar-map-section-head {
    display: flex;
    flex-direction: row;
}
.solar-map-summary {
    width: 888px;
    margin-bottom: 10px;
}
.solar-map-summary > div {
    width: 276px;
    margin-right: 8px;
    padding: 7px 9px;
    background-color: #111b24;
    border-width: 1px;
    border-color: #334a5b;
    border-radius: 5px;
}
.solar-map-summary span,
.solar-map-state,
.solar-map-section-head span {
    display: block;
    color: #8495a3;
    font-size: 10px;
}
.solar-map-summary strong {
    display: block;
    margin-top: 2px;
    color: #edf4f8;
    font-size: 13px;
}
.solar-map-section {
    display: block;
    width: 888px;
    margin-top: 8px;
    padding-top: 8px;
    border-top-width: 1px;
    border-color: #263845;
}
.solar-map-section-head {
    width: 100%;
    justify-content: space-between;
    align-items: baseline;
    margin-bottom: 7px;
}
.solar-map-section-head h3 {
    color: #dce7ee;
    font-size: 13px;
}
.solar-system-track {
    width: 888px;
    flex-wrap: nowrap;
    align-items: flex-start;
    padding-top: 7px;
    padding-bottom: 5px;
    background-color: #081018;
    border-width: 1px;
    border-color: #243c4c;
    border-radius: 6px;
}
.solar-map-row {
    flex-wrap: nowrap;
    align-items: flex-start;
}
.solar-map-node {
    display: flex;
    flex-direction: column;
    flex: none;
    width: 96px;
    min-height: 76px;
    margin-right: 6px;
    text-align: center;
    align-items: center;
}
.solar-system-track .solar-map-node {
    width: 88px;
    margin-right: 8px;
}
.solar-map-node strong {
    display: block;
    width: 100%;
    margin-top: 5px;
    color: #e8f0f4;
    font-size: 11px;
    line-height: 1.1;
}
)" + R"(
.solar-map-glyph {
    display: flex;
    flex: none;
    width: 44px;
    height: 44px;
    margin-left: auto;
    margin-right: auto;
    align-items: center;
    justify-content: center;
    color: #071018;
    background-color: #6f7b84;
    border-width: 2px;
    border-color: #a9b5bd;
    border-radius: 24px;
}
.solar-map-glyph span {
    font-size: 10px;
    font-weight: bold;
}
.map-sun.is-explored .solar-map-glyph {
    width: 50px;
    height: 50px;
    background-color: #f2c94c;
    border-color: #fff0a2;
}
.map-mercury.is-explored .solar-map-glyph { background-color: #9ba1a6; }
.map-venus.is-explored .solar-map-glyph { background-color: #d6a45e; }
.map-earth.is-explored .solar-map-glyph { background-color: #45a8d4; border-color: #9ee8ff; }
.map-mars.is-explored .solar-map-glyph { background-color: #b85b43; border-color: #ef9a78; }
.map-jupiter.is-explored .solar-map-glyph { background-color: #c6a173; border-color: #f3d2a1; }
.map-saturn.is-explored .solar-map-glyph { background-color: #c7a75f; border-color: #f5da8a; }
.map-uranus.is-explored .solar-map-glyph { background-color: #75c8cd; border-color: #b9f3f0; }
.map-neptune.is-explored .solar-map-glyph { background-color: #4774c9; border-color: #91b2ff; }
.map-moon.is-explored .solar-map-glyph { background-color: #929ba2; border-color: #d2d9dd; }
.map-vessel .solar-map-glyph,
.map-field .solar-map-glyph {
    width: 58px;
    height: 30px;
    border-radius: 5px;
}
.map-vessel.is-explored .solar-map-glyph { background-color: #4a879f; border-color: #8edcf2; }
.map-anomaly.is-explored .solar-map-glyph { background-color: #5b4f82; border-color: #b6a5ec; }
.map-field.is-explored .solar-map-glyph { background-color: #665f55; border-color: #afa28e; }
.solar-map-node.is-charted .solar-map-glyph {
    color: #a6b0b8;
    background-color: #1a2127;
    border-color: #59646d;
}
.solar-map-node.is-charted strong,
.solar-map-node.is-charted .solar-map-state {
    color: #7f8a92;
}
.solar-map-node.is-unknown .solar-map-glyph {
    color: #ff655f;
    background-color: #07090c;
    border-color: #5b6269;
}
.solar-map-node.is-unknown .solar-map-glyph span {
    color: #ff655f;
    font-size: 22px;
}
.solar-map-node.is-unknown strong {
    color: #a2a9ae;
}
.solar-map-node.is-unknown .solar-map-state {
    color: #d76762;
}
.solar-map-lower {
    width: 888px;
    align-items: flex-start;
}
.solar-map-lower-section {
    width: 282px;
    margin-right: 8px;
}
.solar-map-lower-section .solar-map-node {
    width: 82px;
    margin-right: 5px;
}
.solar-map-legend {
    width: 888px;
    margin-top: 10px;
    padding-top: 8px;
    border-top-width: 1px;
    border-color: #263845;
    justify-content: flex-end;
}
.solar-map-legend span {
    margin-left: 12px;
    font-size: 10px;
}
.solar-map-legend .legend-explored { color: #7fd4ac; }
.solar-map-legend .legend-charted { color: #8c969e; }
.solar-map-legend .legend-unknown { color: #ff655f; }
.inventory-summary {
    width: 550px;
    margin-top: 4px;
    margin-bottom: 8px;
}
.inventory-layout-main-only .inventory-summary {
    width: 724px;
}
.inventory-summary .metric {
    width: 154px;
    height: 42px;
    padding: 8px 10px;
    margin-right: 10px;
    margin-top: 8px;
    background-color: #101c27;
    border-color: #46677d;
}
.inventory-layout-main-only .inventory-summary .metric {
    width: 184px;
}
.inventory-summary .metric span {
    width: 134px;
    white-space: nowrap;
}
.inventory-layout-main-only .inventory-summary .metric span {
    width: 164px;
}
.inventory-section {
    width: 550px;
    margin-top: 8px;
    padding-top: 8px;
    border-top-width: 1px;
    border-top-color: #26394a;
}
.inventory-section-head {
    width: 550px;
    margin-bottom: 6px;
}
.inventory-section-head h3 {
    margin-top: 0px;
    color: #edf4f8;
    font-size: 16px;
}
.inventory-section-head p {
    width: 520px;
    margin-top: 4px;
    line-height: 1.32;
}
.inventory-grid {
    width: 550px;
}
.inventory-item {
    width: 110px;
    min-height: 88px;
    padding: 10px;
    margin-top: 8px;
    margin-right: 10px;
    background-color: #101923;
    border-color: #46677d;
}
.inventory-art {
    width: 42px;
    height: 34px;
    padding-top: 8px;
    margin-bottom: 8px;
    background-color: #0b131c;
    border-width: 1px;
    border-color: #40596d;
    border-radius: 6px;
}
.inventory-art span {
    color: #8de0ff;
    font-size: 15px;
    text-align: center;
}
.inventory-copy {
    width: 110px;
}
.inventory-copy h3 {
    margin-top: 0px;
    color: #edf4f8;
    font-size: 15px;
    line-height: 1.18;
}
.inventory-copy p {
    width: 106px;
    margin-top: 5px;
    color: #9eb0bf;
    font-size: 13px;
    line-height: 1.28;
}
.inventory-count {
    width: 100px;
    min-height: 20px;
    margin-top: 9px;
    padding: 5px 8px;
    background-color: #0b131c;
    border-width: 1px;
    border-color: #40596d;
    border-radius: 5px;
    color: #edf4f8;
    font-size: 13px;
    text-align: center;
}
.inventory-section-resources .inventory-item {
    width: 118px;
    min-height: 88px;
    padding: 10px;
    margin-right: 8px;
}
.inventory-layout-main-only .inventory-section-resources .inventory-item {
    width: 148px;
}
.inventory-section-resources .inventory-copy {
    width: 118px;
}
.inventory-layout-main-only .inventory-section-resources .inventory-copy {
    width: 148px;
}
.inventory-section-resources .inventory-copy h3 {
    font-size: 15px;
    overflow: hidden;
    white-space: nowrap;
}
.inventory-section-resources .inventory-copy p {
    width: 112px;
    font-size: 13px;
}
.inventory-layout-main-only .inventory-section-resources .inventory-copy p {
    width: 142px;
}
.inventory-section-resources .inventory-art {
    width: 42px;
    height: 34px;
    padding-top: 8px;
}
.inventory-section-resources .inventory-count {
    width: 100px;
}
.inventory-side-column .inventory-item {
    width: 140px;
    min-height: 58px;
    padding: 6px 8px;
    margin-top: 5px;
    margin-right: 0px;
}
.inventory-side-column .inventory-art {
    width: 30px;
    height: 24px;
    padding-top: 5px;
    margin-bottom: 4px;
}
.inventory-side-column .inventory-copy {
    width: 184px;
}
.inventory-side-column .inventory-copy h3 {
    font-size: 13px;
}
.inventory-side-column .inventory-copy p {
    width: 178px;
    margin-top: 2px;
    font-size: 11px;
    line-height: 1.1;
}
.inventory-side-column .inventory-count {
    width: 70px;
    min-height: 16px;
    margin-top: 3px;
    padding: 3px 5px;
    font-size: 11px;
}
.inventory-item.drone-slot.equipped {
    border-color: #5ed993;
}
.inventory-item.drone-slot.open {
    background-color: #102432;
    border-color: #4aa9d8;
}
.inventory-item.drone-slot.open .inventory-art,
.inventory-item.drone-slot.open .inventory-count {
    background-color: #153247;
    border-color: #4aa9d8;
    color: #8de0ff;
}
.inventory-item.drone-slot.locked {
    background-color: #101923;
    border-color: #35495a;
}
.inventory-item.drone-slot.locked .inventory-art,
.inventory-item.drone-slot.locked .inventory-count {
    background-color: #0b131c;
    border-color: #35495a;
    color: #8295a5;
}
.inventory-item.blueprint {
    background-color: #112638;
    border-color: #4aa9d8;
}
.inventory-item.blueprint .inventory-art,
.inventory-item.blueprint .inventory-count {
    background-color: #153247;
    border-color: #4aa9d8;
    color: #8de0ff;
}
.inventory-item.blueprint .inventory-art span {
    color: #8de0ff;
}
.inventory-item.common {
    background-color: #171d23;
    border-color: #647382;
}
.inventory-item.common .inventory-art,
.inventory-item.common .inventory-count {
    background-color: #111820;
    border-color: #647382;
    color: #c2cbd3;
}
.inventory-item.common .inventory-art span {
    color: #c2cbd3;
}
.inventory-item.uncommon {
    background-color: #102b25;
    border-color: #52d990;
}
.inventory-item.uncommon .inventory-art,
.inventory-item.uncommon .inventory-count {
    background-color: #163d31;
    border-color: #52d990;
    color: #8bf0bd;
}
.inventory-item.uncommon .inventory-art span {
    color: #8bf0bd;
}
.inventory-item.rare {
    background-color: #2b2412;
    border-color: #d7aa3a;
}
.inventory-item.rare .inventory-art,
.inventory-item.rare .inventory-count {
    background-color: #3c3014;
    border-color: #d7aa3a;
    color: #ffd166;
}
.inventory-item.rare .inventory-art span {
    color: #ffd166;
}
.inventory-item.exotic {
    background-color: #2a1734;
    border-color: #e05aaf;
}
.inventory-item.exotic .inventory-art,
.inventory-item.exotic .inventory-count {
    background-color: #3b1c47;
    border-color: #e05aaf;
    color: #ffaad9;
}
.inventory-item.exotic .inventory-art span {
    color: #ffaad9;
}
.inventory-item.artifact {
    background-color: #241b38;
    border-color: #a974ff;
}
.inventory-item.artifact .inventory-art,
.inventory-item.artifact .inventory-count {
    background-color: #30224d;
    border-color: #a974ff;
    color: #d2b7ff;
}
.inventory-item.artifact .inventory-art span {
    color: #d2b7ff;
}
.inventory-item.module {
    background-color: #112638;
    border-color: #4aa9d8;
}
.inventory-item.module .inventory-art,
.inventory-item.module .inventory-count {
    background-color: #153247;
    border-color: #4aa9d8;
}
.inventory-item.module .inventory-art span {
    color: #8de0ff;
}
.inventory-item.drone {
    background-color: #123026;
    border-color: #5ed993;
}
.inventory-item.drone .inventory-art,
.inventory-item.drone .inventory-count {
    background-color: #173d2f;
    border-color: #5ed993;
}
.inventory-item.drone .inventory-art span {
    color: #8bf0bd;
}
.upgrade-draft-card.rarity-common,
.ops-card.rarity-common,
.inventory-item.rarity-common {
    background-color: #171d23;
    border-color: #647382;
}
.upgrade-draft-card.rarity-common .draft-art,
.ops-card.rarity-common .module-art,
.inventory-item.rarity-common .inventory-art,
.inventory-item.rarity-common .inventory-count {
    background-color: #111820;
    border-color: #647382;
    color: #c2cbd3;
}
.upgrade-draft-card.rarity-common .draft-art span,
.ops-card.rarity-common .module-art span,
.inventory-item.rarity-common .inventory-art span {
    color: #c2cbd3;
}
.upgrade-draft-card.rarity-uncommon,
.ops-card.rarity-uncommon,
.inventory-item.rarity-uncommon {
    background-color: #102b25;
    border-color: #52d990;
}
.upgrade-draft-card.rarity-uncommon .draft-art,
.ops-card.rarity-uncommon .module-art,
.inventory-item.rarity-uncommon .inventory-art,
.inventory-item.rarity-uncommon .inventory-count {
    background-color: #163d31;
    border-color: #52d990;
    color: #8bf0bd;
}
.upgrade-draft-card.rarity-uncommon .draft-art span,
.ops-card.rarity-uncommon .module-art span,
.inventory-item.rarity-uncommon .inventory-art span {
    color: #8bf0bd;
}
.upgrade-draft-card.rarity-rare,
.ops-card.rarity-rare,
.inventory-item.rarity-rare {
    background-color: #2b2412;
    border-color: #d7aa3a;
}
.upgrade-draft-card.rarity-rare .draft-art,
.ops-card.rarity-rare .module-art,
.inventory-item.rarity-rare .inventory-art,
.inventory-item.rarity-rare .inventory-count {
    background-color: #3c3014;
    border-color: #d7aa3a;
    color: #ffd166;
}
.upgrade-draft-card.rarity-rare .draft-art span,
.ops-card.rarity-rare .module-art span,
.inventory-item.rarity-rare .inventory-art span {
    color: #ffd166;
}
)" + R"(
.upgrade-draft-card.rarity-prototype,
.ops-card.rarity-prototype,
.inventory-item.rarity-prototype {
    background-color: #241b38;
    border-color: #a974ff;
}
.upgrade-draft-card.rarity-prototype .draft-art,
.ops-card.rarity-prototype .module-art,
.inventory-item.rarity-prototype .inventory-art,
.inventory-item.rarity-prototype .inventory-count {
    background-color: #30224d;
    border-color: #a974ff;
    color: #d2b7ff;
}
.upgrade-draft-card.rarity-prototype .draft-art span,
.ops-card.rarity-prototype .module-art span,
.inventory-item.rarity-prototype .inventory-art span {
    color: #d2b7ff;
}
.upgrade-draft-card.rarity-exotic,
.ops-card.rarity-exotic,
.inventory-item.rarity-exotic {
    background-color: #2a1734;
    border-color: #e05aaf;
}
.upgrade-draft-card.rarity-exotic .draft-art,
.ops-card.rarity-exotic .module-art,
.inventory-item.rarity-exotic .inventory-art,
.inventory-item.rarity-exotic .inventory-count {
    background-color: #3b1c47;
    border-color: #e05aaf;
    color: #ffaad9;
}
.upgrade-draft-card.rarity-exotic .draft-art span,
.ops-card.rarity-exotic .module-art span,
.inventory-item.rarity-exotic .inventory-art span {
    color: #ffaad9;
}
.resource-bank, .phase-advisory, .cockpit-hud, .surface-primary-action, .tutorial-card, .draft-hero, .draft-board, .board-primary {
    width: 100%;
}
.metric, .surface-kpi {
    width: 128px;
}
.summary-card {
    width: 184px;
}
.ops-card, .pilot-card, .upgrade-card, .upgrade-draft-card, .surface-action-card {
    width: 292px;
}
.flight-readout .metric, .focus-metrics .metric {
    width: 132px;
}
.control-panel .metric-grid {
    width: 390px;
    justify-content: flex-start;
}
.control-panel .phase-board-surface-minigame .minigame-readout {
    width: 704px;
}
.control-panel .phase-board-surface-minigame .minigame-metrics {
    width: 456px;
    flex-shrink: 0;
}
.control-panel .phase-board-surface-minigame .minigame-metric-row {
    width: 456px;
}
.control-panel .phase-board-surface-minigame .minigame-rewards {
    width: 210px;
    flex-shrink: 0;
    margin-left: 16px;
}
.control-panel .panel-kpis {
    width: 390px;
    justify-content: flex-start;
}
.control-panel .panel-kpis .metric,
.control-panel .flight-readout .metric,
.control-panel .focus-metrics .metric {
    width: 160px;
    padding: 7px 9px;
    margin-top: 6px;
}
.control-panel .telemetry .metric,
.control-panel .mining-metrics .metric {
    width: 168px;
}
.control-panel .phase-board-mining {
    width: 430px;
    padding-bottom: 12px;
}
.control-panel .phase-board-mining .phase-titlebar {
    width: 420px;
    margin-top: 8px;
    margin-bottom: 8px;
}
.control-panel .phase-board-mining .phase-titlebar > div {
    width: 282px;
}
.control-panel .phase-board-mining .phase-titlebar h2 {
    margin-top: 0px;
}
.control-panel .phase-board-mining .phase-titlebar p {
    width: 278px;
    line-height: 1.24;
}
.control-panel .phase-board-mining .compact-tools {
    width: 126px;
    justify-content: flex-end;
}
.control-panel .phase-board-mining .compact-tools button {
    width: 112px;
    height: 34px;
    line-height: 34px;
    margin-top: 0px;
    margin-right: 0px;
}
.control-panel .phase-board-mining .tutorial-card,
.control-panel .phase-board-mining .phase-advisory,
.control-panel .phase-board-mining .mining-health-strip,
.control-panel .phase-board-mining .mining-combat-strip,
.control-panel .phase-board-mining .mining-payload,
.control-panel .phase-board-mining .mining-hud {
    width: 400px;
}
.control-panel .phase-board-mining .mining-health-strip {
    padding: 8px 10px;
    margin-top: 8px;
    margin-bottom: 4px;
    border-width: 1px;
    border-color: rgba(71, 220, 255, 0.28);
    background-color: rgba(7, 24, 32, 0.86);
}
.control-panel .phase-board-mining .mining-health-strip > div {
    width: 380px;
    height: 22px;
}
.control-panel .phase-board-mining .mining-health-strip span {
    display: inline-block;
    width: 250px;
    color: rgba(169, 231, 246, 0.88);
    font-size: 12px;
    text-transform: uppercase;
}
.control-panel .phase-board-mining .mining-health-strip strong {
    display: inline-block;
    width: 118px;
    color: #e7fbff;
    font-size: 22px;
    text-align: right;
}
.control-panel .phase-board-mining .mining-health-bar {
    width: 378px;
    height: 12px;
    margin-top: 4px;
    margin-bottom: 0px;
    background-color: rgba(1, 8, 13, 0.92);
}
.control-panel .phase-board-mining .mining-health-bar i {
    display: block;
    height: 12px;
    background-color: #4be7ff;
}
.control-panel .phase-board-mining .mining-health-bar .health-fill-0 { width: 0px; }
.control-panel .phase-board-mining .mining-health-bar .health-fill-10 { width: 38px; }
.control-panel .phase-board-mining .mining-health-bar .health-fill-20 { width: 76px; }
.control-panel .phase-board-mining .mining-health-bar .health-fill-30 { width: 113px; }
.control-panel .phase-board-mining .mining-health-bar .health-fill-40 { width: 151px; }
.control-panel .phase-board-mining .mining-health-bar .health-fill-50 { width: 189px; }
.control-panel .phase-board-mining .mining-health-bar .health-fill-60 { width: 227px; }
.control-panel .phase-board-mining .mining-health-bar .health-fill-70 { width: 265px; }
.control-panel .phase-board-mining .mining-health-bar .health-fill-80 { width: 302px; }
.control-panel .phase-board-mining .mining-health-bar .health-fill-90 { width: 340px; }
.control-panel .phase-board-mining .mining-health-bar .health-fill-100 { width: 378px; }
.control-panel .phase-board-mining .mining-health-strip p {
    width: 380px;
    margin-top: 0px;
    margin-bottom: 0px;
    color: rgba(199, 224, 226, 0.76);
    font-size: 12px;
    line-height: 1.22;
}
.control-panel .phase-board-mining .mining-combat-strip {
    padding: 10px;
    margin-top: 8px;
    margin-bottom: 6px;
    border-width: 1px;
    border-color: rgba(71, 220, 255, 0.26);
    background-color: rgba(9, 28, 36, 0.84);
}
.control-panel .phase-board-mining .mining-combat-strip > div {
    width: 382px;
}
.control-panel .phase-board-mining .mining-combat-strip h2 {
    margin-top: 0px;
    margin-bottom: 4px;
    color: #dffbff;
    font-size: 15px;
}
.control-panel .phase-board-mining .mining-combat-strip p {
    width: 382px;
    margin-top: 0px;
    margin-bottom: 6px;
    color: rgba(199, 224, 226, 0.76);
    font-size: 12px;
    line-height: 1.20;
}
.control-panel .phase-board-mining .mining-combat-strip .stat-grid {
    width: 390px;
    justify-content: flex-start;
}
.control-panel .phase-board-mining .mining-combat-strip .stat-chip {
    width: 104px;
    min-height: 17px;
    padding: 4px 6px;
    margin-top: 4px;
    margin-right: 5px;
    font-size: 11px;
    line-height: 1.12;
}
.control-panel .phase-board-mining .mining-payload {
    padding: 8px 10px;
    margin-top: 6px;
    margin-bottom: 0px;
}
.control-panel .phase-board-mining .mining-payload > div {
    width: 382px;
}
.control-panel .phase-board-mining .mining-payload h2 {
    margin-top: 0px;
    margin-bottom: 4px;
}
.control-panel .phase-board-mining .mining-payload p {
    width: 382px;
    margin-bottom: 5px;
    line-height: 1.24;
}
.control-panel .phase-board-mining .mining-payload .stat-grid {
    width: 390px;
    justify-content: flex-start;
}
.control-panel .phase-board-mining .mining-payload .stat-chip {
    width: 104px;
    min-height: 17px;
    padding: 4px 6px;
    margin-top: 4px;
    margin-right: 5px;
    font-size: 11px;
    line-height: 1.12;
}
.control-panel .phase-board-mining .mining-metrics {
    width: 420px;
    margin-top: 4px;
    margin-bottom: 4px;
    justify-content: flex-start;
}
.control-panel .phase-board-mining .mining-metrics .metric {
    width: 112px;
    min-height: 36px;
    margin-top: 4px;
    margin-right: 4px;
    padding: 5px 6px;
}
.control-panel .phase-board-mining .mining-metrics .metric strong {
    font-size: 16px;
    line-height: 1.05;
}
.control-panel .phase-board-mining .mining-metrics .metric span {
    font-size: 11px;
    line-height: 1.12;
}
.control-panel .phase-board-mining .mining-hud {
    margin-top: 6px;
    margin-bottom: 6px;
    padding: 10px;
}
.control-panel .phase-board-mining .mining-hud .system-actions {
    width: 390px;
    justify-content: flex-start;
    flex-wrap: wrap;
    margin-top: 8px;
}
.control-panel .phase-board-mining .mining-hud .system-actions button {
    min-width: 0px;
    width: 174px;
    height: 42px;
    line-height: 42px;
    margin-top: 0px;
    margin-right: 8px;
    margin-bottom: 6px;
    padding-top: 0px;
    padding-left: 4px;
    padding-right: 4px;
    padding-bottom: 0px;
    font-size: 13px;
}
.control-panel h2 {
    margin-top: 10px;
    margin-bottom: 5px;
}
.control-panel .warning-grid {
    width: 390px;
    justify-content: center;
    margin-top: 6px;
    margin-bottom: 8px;
}
.control-panel .warning-button {
    min-width: 0px;
    width: 104px;
    height: 46px;
    line-height: 1.1;
    padding: 4px 5px;
    margin-top: 4px;
    margin-right: 6px;
    text-align: left;
}
.control-panel .warning-button strong {
    display: block;
    width: 100%;
    color: #9eb0bd;
    font-size: 11px;
    text-align: center;
}
.control-panel .warning-button span {
    display: block;
    width: 100%;
    margin-top: 2px;
    color: #edf4f8;
    font-size: 23px;
    line-height: 1.05;
    text-align: center;
}
.control-panel .tutorial-card,
.control-panel .phase-advisory {
    width: 370px;
}
.control-panel .tutorial-card {
    display: block;
    margin-top: 8px;
    margin-bottom: 12px;
    padding: 10px;
    background-color: #101923;
    border-color: #2f4354;
}
.control-panel .tutorial-card div {
    display: block;
    width: 344px;
}
.control-panel .tutorial-card span {
    color: #7f91a0;
    font-size: 12px;
}
.control-panel .tutorial-card strong {
    margin-top: 2px;
    margin-bottom: 4px;
}
.control-panel .tutorial-card p {
    width: 344px;
    line-height: 1.25;
}
.control-panel .tutorial-card button {
    display: block;
    min-width: 0px;
    width: 128px;
    height: 32px;
    line-height: 32px;
    margin-top: 8px;
    margin-right: 0px;
}
.control-panel .telemetry-status,
.control-panel .phase-copy,
.control-panel .cockpit-hold-copy {
    width: 390px;
}
.control-panel .telemetry-status {
    margin-top: 6px;
    margin-bottom: 8px;
    line-height: 1.25;
}
.control-panel .cockpit-hud {
    width: 390px;
    margin-bottom: 12px;
    padding: 9px;
    height: auto;
    min-height: 0px;
}
.control-panel .actions {
    width: 372px;
    justify-content: center;
}
.control-panel .actions button {
    width: 176px;
    height: 42px;
    line-height: 42px;
    margin-right: 8px;
}
.control-panel .flight-hud .primary-actions {
    flex-wrap: nowrap;
    width: 372px;
    column-gap: 8px;
}
.control-panel .flight-hud .primary-actions button {
    width: auto;
    flex-grow: 1;
    flex-shrink: 1;
    flex-basis: 0px;
    height: 42px;
    line-height: 42px;
    margin-right: 0px;
}
.control-panel .flight-hud .system-actions {
    flex-wrap: nowrap;
    width: 372px;
    margin-top: 8px;
    column-gap: 8px;
}
.stat-chip, .telemetry-legend-chip, .surface-kpi {
    margin-top: 5px;
    margin-right: 5px;
    padding: 5px 7px;
    background-color: #102835;
    border-width: 1px;
    border-color: #34566a;
    border-radius: 5px;
    overflow: hidden;
}
.stat-chip, .telemetry-legend-chip {
    white-space: nowrap;
}
.stat-grid .stat-chip {
    width: 108px;
}
.phase-board-drone-ops .drone-card .stat-grid .stat-chip {
    width: 82px;
    min-height: 20px;
    padding: 4px 5px;
    font-size: 11px;
    line-height: 1.12;
}
.phase-board-drone-ops .drone-card .stat-grid .stat-chip.wide {
    width: 112px;
}
.phase-board-drone-ops .drone-bay-strip .stat-grid .stat-chip {
    width: 76px;
    min-height: 20px;
    padding: 4px 5px;
    font-size: 11px;
    line-height: 1.12;
}
.phase-board-drone-ops .drone-bay-strip .stat-grid .stat-chip.wide {
    width: 106px;
}
)" + R"(
h1 {
    font-size: 21px;
    margin-bottom: 4px;
}
h2 {
    font-size: 17px;
    color: #99a9b8;
    margin-top: 12px;
    margin-bottom: 6px;
}
h3 {
    font-size: 15px;
    margin-bottom: 4px;
}
p, span {
    color: #99a9b8;
    line-height: 1.35;
}
strong {
    color: #edf4f8;
}
.metric span, .stat-chip span, .summary-card span, .surface-kpi span {
    font-size: 12px;
}
.metric strong, .stat-chip strong, .summary-card strong, .surface-kpi strong {
    margin-top: 2px;
}
.card-footer span, .draft-card-footer span, .card-topline span, .card-kicker span, .pilot-card-top span {
    display: inline-block;
}
.detail-stack {
    display: flex;
    flex-direction: column;
    width: 100%;
    margin-top: 8px;
}
.detail-row {
    display: flex;
    flex-direction: row;
    justify-content: space-between;
    width: 572px;
    margin-top: 7px;
    padding: 7px 8px;
    background-color: #111a24;
    border-width: 1px;
    border-color: #243749;
    border-radius: 5px;
}
.detail-row span, .detail-row strong {
    display: inline-block;
}
.detail-row span {
    width: 128px;
    color: #7f91a0;
}
.detail-row strong {
    width: 408px;
    line-height: 1.28;
}
.detail-section {
    margin-top: 10px;
    margin-bottom: 4px;
    color: #edf4f8;
}
button {
    display: inline-block;
    min-width: 92px;
    min-height: 36px;
    height: auto;
    line-height: 1.15;
    margin-top: 6px;
    margin-right: 6px;
    padding: 7px 9px;
    color: #edf4f8;
    text-align: center;
    background-color: #153244;
    border-width: 1px;
    border-color: #4e6b80;
    border-radius: 6px;
}
.actions button {
    width: 184px;
}
.control-panel .flight-hud .system-actions button {
    width: auto;
    flex-grow: 1;
    flex-shrink: 1;
    flex-basis: 0px;
    box-sizing: border-box;
    padding: 6px;
    height: auto;
    min-height: 38px;
    line-height: 1.15;
    margin-right: 0px;
    font-size: 12px;
    white-space: normal;
    overflow: visible;
    text-align: center;
}
.card-footer button, .draft-card-footer button, .summary-card button, .resource-bank button {
    width: 116px;
}
.phase-board-drone-ops .drone-control-card .card-footer button {
    width: 82px;
    min-width: 82px;
    height: 18px;
    min-height: 18px;
    margin-top: 0px;
    margin-right: 0px;
    margin-bottom: 0px;
    padding: 2px 5px;
    font-size: 9px;
    line-height: 1.1;
}
.phase-board-drone-ops .drone-bay-strip button {
    width: 78px;
    min-width: 78px;
    min-height: 28px;
    margin-top: 0px;
    margin-right: 0px;
    padding: 4px 5px;
    font-size: 10px;
    line-height: 1.1;
}
.phase-board-drone-ops .drone-control-card,
.phase-board-drone-ops .drone-loadout-slot {
    border-radius: 8px;
    border-width: 2px;
    border-color: #31566b;
    background-color: #0d1d27;
}
.phase-board-drone-ops .drone-top-row {
    width: 704px;
    margin-top: 8px;
    margin-bottom: 8px;
}
.phase-board-drone-ops .drone-bay-strip {
    width: 684px;
    min-height: 116px;
    padding: 10px;
    justify-content: flex-start;
}
.phase-board-drone-ops .drone-bay-strip .drone-bay-copy {
    width: 198px;
    margin-right: 18px;
}
.phase-board-drone-ops .drone-bay-strip p {
    width: 190px;
    line-height: 1.2;
}
.phase-board-drone-ops .drone-bay-stats,
.phase-board-drone-ops .drone-bay-materials {
    width: 132px;
    margin-right: 12px;
}
.phase-board-drone-ops .drone-bay-materials {
    margin-right: 24px;
}
.phase-board-drone-ops .drone-bay-strip .stat-chip {
    width: 122px;
    max-width: 122px;
    min-height: 24px;
    padding: 4px 6px;
}
.phase-board-drone-ops .drone-bay-strip button {
    width: 100px;
    min-width: 100px;
    min-height: 34px;
}
.phase-board-drone-ops .drone-control-card {
    height: 208px;
    min-height: 208px;
    padding: 8px;
    overflow: hidden;
}
.phase-board-drone-ops .drone-control-card.rarity-common {
    border-color: #31566b;
}
.phase-board-drone-ops .drone-control-card.rarity-uncommon {
    border-color: #3a735c;
    background-color: #0d2222;
}
.phase-board-drone-ops .drone-control-card.rarity-rare {
    border-color: #806b2c;
    background-color: #201c12;
}
.phase-board-drone-ops .drone-card-head {
    display: flex;
    flex-direction: row;
    align-items: flex-start;
    width: 184px;
    min-height: 32px;
}
.phase-board-drone-ops .drone-role-mark {
    display: block;
    width: 22px;
    height: 22px;
    margin-right: 7px;
    padding-top: 3px;
    color: #dffbff;
    text-align: center;
    font-size: 12px;
    line-height: 1;
    border-width: 1px;
    border-color: #4a7f96;
    border-radius: 6px;
    background-color: #15384a;
}
.phase-board-drone-ops .drone-card-id {
    display: block;
    width: 155px;
}
.phase-board-drone-ops .drone-card-id .card-topline {
    width: 155px;
    min-height: 11px;
    font-size: 9px;
    color: #89b2d3;
}
.phase-board-drone-ops .drone-card-id .card-title {
    width: 155px;
    margin-top: 2px;
    margin-bottom: 0px;
    font-size: 12px;
}
.phase-board-drone-ops .drone-control-status {
    width: 184px;
    min-height: 15px;
    max-height: 15px;
    margin-top: 3px;
    padding-top: 2px;
    color: #bfeef7;
    font-size: 10px;
    border-top-width: 1px;
    border-top-color: #243b4c;
}
.phase-board-drone-ops .drone-control-card .stat-grid {
    width: 184px;
    height: 104px;
    min-height: 104px;
    max-height: 104px;
    margin-top: 5px;
    gap: 2px;
    row-gap: 2px;
    column-gap: 2px;
    overflow: hidden;
}
.phase-board-drone-ops .drone-control-card .stat-chip {
    width: 160px;
    max-width: 160px;
    min-height: 15px;
    padding: 2px 5px;
}
.phase-board-drone-ops .drone-control-card .stat-chip.wide {
    width: 160px;
}
.phase-board-drone-ops .drone-control-card .card-footer {
    width: 184px;
    display: flex;
    flex-direction: row;
    flex-wrap: nowrap;
    min-height: 24px;
    padding-top: 5px;
    border-top-color: #2d5265;
    justify-content: space-between;
}
.phase-board-drone-ops .drone-loadout-slot {
    height: 118px;
    min-height: 118px;
    padding: 8px;
    background-color: #0b1720;
}
.phase-board-drone-ops .drone-loadout-slot.filled {
    border-color: #367486;
    background-color: #0d252d;
}
.phase-board-drone-ops .drone-loadout-slot.open {
    border-color: #397459;
    background-color: #0d2119;
}
.phase-board-drone-ops .drone-loadout-slot.locked {
    border-color: #2a3845;
    background-color: #0e141a;
}
.phase-board-drone-ops .slot-card-head {
    display: flex;
    flex-direction: row;
    justify-content: space-between;
    align-items: center;
    width: 184px;
    min-height: 22px;
}
.phase-board-drone-ops .slot-number {
    display: block;
    width: 24px;
    min-height: 16px;
    padding-top: 2px;
    color: #dffbff;
    text-align: center;
    font-size: 10px;
    border-width: 1px;
    border-color: #456b80;
    border-radius: 5px;
    background-color: #132f3d;
}
.phase-board-drone-ops .slot-state {
    width: 142px;
    color: #ffd166;
    text-align: right;
    font-size: 10px;
}
.phase-board-drone-ops .slot-card-head button {
    width: 78px;
    min-width: 78px;
    height: 17px;
    min-height: 17px;
    margin-top: 0px;
    margin-right: 0px;
    margin-bottom: 0px;
    padding: 1px 5px;
    font-size: 9px;
    line-height: 1;
}
.phase-board-drone-ops .slot-card-body {
    width: 184px;
    min-height: 32px;
    margin-top: 3px;
}
.phase-board-drone-ops .slot-card-body .card-title {
    margin-top: 0px;
    margin-bottom: 1px;
    font-size: 12px;
}
.phase-board-drone-ops .slot-card-body .slot-role {
    min-height: 11px;
    margin-top: 0px;
    color: #bfeef7;
    font-size: 10px;
}
.phase-board-drone-ops .drone-loadout-slot .stat-grid {
    width: 184px;
    min-height: 17px;
    margin-top: 4px;
}
.phase-board-drone-ops .drone-loadout-slot .stat-chip {
    width: 76px;
    min-height: 14px;
    padding: 2px 5px;
    font-size: 9px;
}
)" + R"(
.phase-board-drone-ops .drone-loadout-slot .stat-chip.wide {
    width: 92px;
}
button:hover {
    background-color: #1f4b62;
}
button.disabled,
button:disabled {
    color: #8a939c;
    background-color: #252b31;
    border-color: #47515a;
}
button.disabled:hover,
button:disabled:hover {
    color: #8a939c;
    background-color: #252b31;
    border-color: #47515a;
}
button.ok {
    background-color: #1d4a39;
    border-color: #5ba77f;
}
button.warn {
    background-color: #4c3f1c;
    border-color: #b99a3f;
}
button.rare {
    color: #ffd166;
    background-color: #3c3014;
    border-color: #d7aa3a;
}
button.danger {
    background-color: #4a2421;
    border-color: #b85b51;
}
button.ghost {
    color: #99a9b8;
    background-color: #1a222d;
}
button.settings-toggle {
    width: 184px;
    min-height: 36px;
    color: #edf4f8;
    background-color: #173044;
    border-color: #4aa9d8;
}
button.settings-toggle:hover {
    color: #edf4f8;
    background-color: #1f4b62;
    border-color: #67d2ff;
}
.settings-control button.settings-toggle {
    margin-top: 6px;
}
.settings-control {
    margin-top: 12px;
}
.settings-control h3 {
    margin-bottom: 4px;
}
.settings-control p {
    width: 560px;
}
.settings-control select {
    display: block;
    width: 240px;
    height: 36px;
    margin-top: 6px;
}
.settings-checkbox {
    display: block;
    width: 240px;
    min-height: 26px;
    margin-top: 6px;
    color: #edf4f8;
}
.settings-checkbox input {
    display: inline-block;
    width: 18px;
    height: 18px;
    margin-right: 8px;
    background-color: #0c141d;
    border-width: 1px;
    border-color: #4e6b80;
    border-radius: 4px;
}
.settings-checkbox input:checked {
    background-color: #1d4a39;
    border-color: #72e0a8;
}
.settings-checkbox span {
    display: inline-block;
    height: 22px;
    line-height: 22px;
}
.settings-control select selectvalue {
    width: auto;
    height: 28px;
    margin-right: 0px;
    padding: 8px 10px 0px 10px;
    color: #edf4f8;
    background-color: #131c26;
    border-width: 1px;
    border-color: #4e6b80;
    border-radius: 6px;
}
.settings-control select selectarrow {
    width: 0px;
    height: 0px;
    margin: 0px;
    padding: 0px;
    border-width: 0px;
    background-color: transparent;
}
.settings-control select:hover selectvalue {
    background-color: #1f4b62;
}
.settings-control select selectbox {
    width: 240px;
    margin-top: 2px;
    padding: 4px;
    background-color: #0c121a;
    border-width: 1px;
    border-color: #4e6b80;
    border-radius: 6px;
}
.settings-control select selectbox option {
    display: block;
    width: auto;
    padding: 6px 8px;
    color: #edf4f8;
    background-color: #131c26;
}
.settings-control select selectbox option:hover,
.settings-control select selectbox option:checked {
    background-color: #1f4b62;
}
.status, .phase-status {
    color: #ffd166;
    margin-top: 10px;
}
.danger, .critical {
    border-color: #c95d50;
}
.warn, .caution {
    border-color: #c8a446;
}
.ok {
    border-color: #5ba77f;
}
#rr-modal-scrim {
    position: absolute;
    left: 0px;
    top: 0px;
    width: )" + std::to_string(viewportWidth) + R"(px;
    height: )" + std::to_string(viewportHeight) + R"(px;
    background-color: #05070a;
}
#rr-modal {
    position: absolute;
    left: )" + std::to_string(modalDefaultLeft) + R"(px;
    top: )" + std::to_string(modalTallTop) + R"(px;
    width: )" + std::to_string(modalDefaultWidth) + R"(px;
    height: )" + std::to_string(modalTallHeight) + R"(px;
    padding: 16px;
    display: flex;
    flex-direction: column;
    background-color: #0c121a;
    border-width: 1px;
    border-color: #4e6b80;
    border-radius: 8px;
    overflow: hidden;
}
#rr-modal.modal-inventory {
    left: )" + std::to_string(modalInventoryLeft) + R"(px;
    top: )" + std::to_string(modalTallTop) + R"(px;
    width: )" + std::to_string(modalInventoryWidth) + R"(px;
    height: )" + std::to_string(modalTallHeight) + R"(px;
    margin-left: 0px;
    padding: 18px;
}
#rr-modal.modal-map {
    left: )" + std::to_string(modalMapLeft) + R"(px;
    top: )" + std::to_string(modalTallTop) + R"(px;
    width: )" + std::to_string(modalMapWidth) + R"(px;
    height: )" + std::to_string(modalTallHeight) + R"(px;
    margin-left: 0px;
    padding: 16px;
}
#rr-modal.modal-phase_briefing {
    top: )" + std::to_string(modalBriefTop) + R"(px;
    height: )" + std::to_string(modalBriefHeight) + R"(px;
    padding: 20px;
}
#rr-modal.modal-surface {
    top: )" + std::to_string(modalTallTop) + R"(px;
    height: )" + std::to_string(modalTallHeight) + R"(px;
    padding: 20px;
}
#rr-modal.modal-mission_log {
    top: )" + std::to_string(modalMissionTop) + R"(px;
    height: )" + std::to_string(modalMissionHeight) + R"(px;
    padding: 20px;
}
#rr-modal.modal-launch_outcome {
    top: )" + std::to_string(std::max(24, (viewportHeight - 280) / 2)) + R"(px;
    height: 280px;
    padding: 20px;
}
#rr-modal.modal-flyby_introduction,
#rr-modal.modal-orbit_introduction,
#rr-modal.modal-landing_introduction,
#rr-modal.modal-mini_drone_introduction {
    left: )" + std::to_string(modalActivityLeft) + R"(px;
    top: )" + std::to_string(modalActivityTop) + R"(px;
    width: )" + std::to_string(modalActivityWidth) + R"(px;
    height: )" + std::to_string(modalActivityHeight) + R"(px;
    padding: 20px;
}
#rr-modal.modal-flyby_introduction .modal-head,
#rr-modal.modal-orbit_introduction .modal-head,
#rr-modal.modal-landing_introduction .modal-head,
#rr-modal.modal-mini_drone_introduction .modal-head {
    margin-bottom: 12px;
}
#rr-modal.modal-flyby_introduction .modal-head button,
#rr-modal.modal-orbit_introduction .modal-head button,
#rr-modal.modal-landing_introduction .modal-head button,
#rr-modal.modal-mini_drone_introduction .modal-head button {
    width: 82px;
    min-height: 36px;
    padding: 7px 10px;
}
.activity-introduction {
    display: flex;
    flex-direction: column;
    min-height: 218px;
}
.activity-introduction-kicker {
    margin-bottom: 10px;
    color: #69d9f0;
    font-size: 11px;
    letter-spacing: 1px;
    text-transform: uppercase;
}
.activity-introduction-setup {
    margin: 0px 0px 14px 0px;
    color: #c5d7de;
    font-size: 14px;
    line-height: 1.4;
}
.activity-introduction-payoff {
    display: flex;
    flex-direction: column;
    padding: 11px 12px;
    background-color: #101c27;
    border-left-width: 3px;
    border-left-color: #f0c75e;
}
.activity-introduction-payoff span {
    margin-bottom: 5px;
    color: #f0c75e;
    font-size: 10px;
    letter-spacing: 1px;
    text-transform: uppercase;
}
.activity-introduction-payoff strong {
    color: #e5edf1;
    font-size: 14px;
    line-height: 1.35;
}
.activity-introduction-actions {
    margin-top: auto;
    justify-content: flex-end;
}
.activity-introduction-actions button {
    width: 176px;
    min-height: 42px;
}
.launch-outcome-summary {
    display: flex;
    flex-direction: column;
    min-height: 190px;
}
.launch-outcome-consequence {
    min-height: 60px;
    max-height: 60px;
    margin-top: 12px;
    margin-bottom: 8px;
    color: #c5d7de;
    font-size: 14px;
    line-height: 1.35;
    overflow: hidden;
}
.launch-outcome-progression {
    min-height: 20px;
    max-height: 20px;
    color: #f0d47a;
    font-size: 14px;
    overflow: hidden;
}
.launch-outcome-actions {
    margin-top: auto;
}
.launch-outcome-actions button {
    width: 48%;
}
.modal-head {
    display: flex;
    flex: none;
    flex-direction: row;
    justify-content: space-between;
    margin-bottom: 8px;
}
.modal-scroll-body {
    display: block;
    flex: auto;
    min-height: 0px;
    width: 100%;
    overflow-x: hidden;
    overflow-y: auto;
    padding-right: 8px;
}
/* The lightweight web renderer does not implement RmlUi's advanced layer/filter path used by box-shadow. */
body.controller-focus-visible .rr-controller-focus {
    border-color: #f4c95d;
}
#rr-controller-prompt-bar {
    position: fixed;
    left: 16px;
    right: 16px;
    bottom: 8px;
    height: 28px;
    display: flex;
    flex-direction: row;
    justify-content: center;
    align-items: center;
    gap: 14px;
    padding: 4px 10px;
    background-color: #071019e8;
    border-width: 1px;
    border-color: #36556a;
    border-radius: 6px;
    color: #b9c9d4;
    font-size: 12px;
}
#rr-controller-prompt-bar strong {
    color: #f4c95d;
}
#rr-performance-stats {
    position: fixed;
    right: 16px;
    top: 16px;
    width: 324px;
    padding: 10px 12px;
    color: #d8e6ee;
    background-color: #071019e8;
    border-width: 1px;
    border-color: #4e8099;
    border-radius: 6px;
    font-size: 11px;
    line-height: 1.25;
}
#rr-performance-stats.performance-hidden {
    display: none;
}
#rr-performance-stats .performance-title {
    display: flex;
    flex-direction: row;
    justify-content: space-between;
    margin-bottom: 4px;
    color: #f4c95d;
    font-size: 13px;
}
#rr-performance-stats p {
    margin-top: 2px;
    margin-bottom: 0px;
    color: #b9c9d4;
}
#rr-performance-stats .performance-warning {
    color: #ff8b7d;
    font-weight: bold;
}
)";
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
    const char* rightTrigger;
    const char* menu;
    const char* view;
};

ControllerPromptLabels controllerPromptLabels(ControllerFamily family)
{
    switch (family) {
    case ControllerFamily::Xbox:
        return {"A", "B", "X", "Y", "LB", "RB", "RT", "Menu", "View"};
    case ControllerFamily::PlayStation:
        return {"Cross", "Circle", "Square", "Triangle", "L1", "R1", "R2", "Options", "Create"};
    case ControllerFamily::SteamDeck:
        return {"A", "B", "X", "Y", "L1", "R1", "R2", "Menu", "View"};
    case ControllerFamily::Generic:
    default:
        return {"South", "East", "West", "North", "LB", "RB", "RT", "Menu", "View"};
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
    replaceToken("{{controller_rt}}", labels.rightTrigger);
    replaceToken("{{controller_menu}}", labels.menu);
    replaceToken("{{controller_view}}", labels.view);
    return markup;
}

std::string controllerPromptBar(std::string_view panelHtml, ControllerFamily family, bool modalOpen, bool modalDismissible)
{
    const ControllerPromptLabels labels = controllerPromptLabels(promptControllerFamily(family));
    const bool swapConfirmCancel = rr_rml_controller_boolean_preference(1) != 0;
    const char* confirm = swapConfirmCancel ? labels.east : labels.south;
    const char* cancel = swapConfirmCancel ? labels.south : labels.east;
    std::string prompt = "<div id=\"rr-controller-prompt-bar\">";
    const auto item = [&](const char* button, const char* action) {
        return std::string("<span><strong>") + button + "</strong> " + action + "</span>";
    };
    if (modalOpen) {
        prompt += item("L-stick / D-pad", "Navigate") + item(confirm, "Select");
        if (modalDismissible) {
            prompt += item(cancel, "Back");
        }
        prompt += item("R-stick", "Scroll");
    } else if (panelHtml.find("data-panel-mode=\"title\"") != std::string_view::npos) {
        prompt += item("L-stick / D-pad", "Navigate") + item(confirm, "Select") + item(labels.menu, "Settings");
    } else if (panelHtml.find("data-panel-mode=\"story-briefing\"") != std::string_view::npos ||
               panelHtml.find("data-panel-mode=\"mission-stamp\"") != std::string_view::npos ||
               panelHtml.find("data-panel-mode=\"arrival-fanfare\"") != std::string_view::npos) {
        prompt += item(labels.south, "Continue") + item(labels.menu, "Pause");
    } else if (panelHtml.find("data-panel-mode=\"mining-fullscreen\"") != std::string_view::npos) {
        prompt += item("L-stick", "Move / face") + item(labels.rightTrigger, "Drill") + item(labels.west, "Scan") +
            item(labels.north, "Tether") + item(labels.leftBumper, "Drill repair") + item(labels.rightBumper, "Rig repair") +
            item(labels.south, "Stow") + item(labels.east, "Hold: recall") + item(labels.menu, "Pause");
    } else if (panelHtml.find("data-flyby-run") != std::string_view::npos || panelHtml.find("data-orbit-run") != std::string_view::npos) {
        prompt += item("L-stick", "Fly") + item(labels.east, "Hold: abort") + item(labels.menu, "Pause");
    } else if (panelHtml.find("push-minigame") != std::string_view::npos || panelHtml.find("scan-minigame") != std::string_view::npos) {
        prompt += item(labels.south, "Pulse / push") + item(labels.west, "Bank") + item(labels.east, "Hold: abort") + item(labels.menu, "Pause");
    } else if (panelHtml.find("data-rr-action=\"return_home\"") != std::string_view::npos) {
        prompt += item(labels.south, "Return") + item(labels.east, "Hold: Eject") + item(labels.west, "Engines") +
            item(labels.north, "Pressure relief") + item(labels.rightBumper, "Hold: Jettison") + item(labels.menu, "Pause");
    } else if (panelHtml.find("data-preflight-launch") != std::string_view::npos) {
        prompt += item(labels.south,
            panelHtml.find("data-preflight-queued=\"1\"") != std::string_view::npos ? "Launch queued" : "Launch") +
            item(labels.menu, "Pause");
    } else {
        prompt += item("L-stick / D-pad", "Navigate") + item(confirm, "Select") + item(cancel, "Back") +
            item("R-stick", "Scroll") + item(labels.menu, "Pause");
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

std::string buildDocumentRml(
    const std::string& panelHtml,
    const std::string& openModalId,
    bool controllerPresentationActive,
    bool controllerFocusVisible,
    bool controllerResumeBlocked,
    bool controllerResumeConnected,
    ControllerFamily controllerFamily,
    const std::string& performanceStatsHtml,
    bool performanceStatsVisible)
{
    const std::vector<ModalTemplate> modals = extractModals(panelHtml);
    std::string activeModalId = openModalId;
    if (activeModalId.empty()) {
        const auto autoModal = std::find_if(modals.begin(), modals.end(), [](const ModalTemplate& modal) {
            return modal.autoOpen;
        });
        if (autoModal != modals.end()) {
            activeModalId = autoModal->id;
        }
    }

    const RmlPanelMode panelMode = panelModeForHtml(panelHtml);
    const bool titleScreen = panelUsesTitle(panelMode);
    const bool storyBriefing = panelUsesStoryBriefing(panelMode);
    const bool phaseBoard = panelUsesPhaseBoard(panelMode);
    const bool miningFullscreen = panelUsesMiningFullscreen(panelMode);
    const bool arrivalFanfare = panelUsesMissionStamp(panelMode);
    const bool surfaceOps = panelUsesSurfaceOps(panelHtml);
    const bool droneOps = panelUsesDroneOps(panelHtml);
    const bool navigation = panelUsesNavigation(panelHtml);
    const bool draftRoom = panelUsesDraftRoom(panelHtml);
    std::string body = withOpeningControllerLabels(
        syncSettingsControls(sanitizeRml(removeTemplates(panelHtml))),
        controllerFamily);

    std::string document = "<rml><head><style>" + panelRcss(panelMode) + "</style></head><body";
    if (controllerFocusVisible || controllerPresentationActive) {
        document += " class=\"";
        if (controllerFocusVisible) {
            document += "controller-focus-visible";
        }
        if (controllerFocusVisible && controllerPresentationActive) {
            document += " ";
        }
        if (controllerPresentationActive) {
            document += "controller-connected";
        }
        document += "\"";
    }
    document += ">";
    document += "<div id=\"rr-panel\" class=\"" +
        std::string(titleScreen
                ? "title-screen-panel-mode"
                : (storyBriefing
                ? "story-briefing-panel-mode"
                : (miningFullscreen
                ? "mining-fullscreen-panel"
                : (arrivalFanfare ? "arrival-fanfare-panel-mode" : (phaseBoard ? "phase-board-panel" : "control-panel")))));
    if (surfaceOps) {
        document += " surface-ops-panel";
    }
    if (droneOps) {
        document += " drone-ops-panel";
    }
    if (navigation) {
        document += " navigation-panel";
    }
    if (draftRoom) {
        document += " draft-room-panel";
    }
    document += "\">";
    document += body;
    document += "</div>";
    document += nativeSceneOverlayMarkup(panelHtml);

    const ModalTemplate* activeModal = findModal(modals, activeModalId);
    if (activeModal) {
        const ModalTemplate* modal = activeModal;
        document += "<div id=\"rr-modal-scrim\"></div>";
        document += "<div id=\"rr-modal\" class=\"modal-" + activeModalId + "\"><div class=\"modal-head\"><h2>";
        document += modal->title;
        document += "</h2>";
        if (modal->dismissible && modal->showClose) {
            document += "<button class=\"ghost\" data-ui-close-modal=\"1\" data-ui-focus-id=\"modal:close\">Close</button>";
        }
        document += "</div><div class=\"modal-scroll-body\">";
        document += syncSettingsControls(sanitizeRml(controllerResumeModalBody(
            modal->body,
            controllerResumeBlocked,
            controllerResumeConnected)));
        document += "</div></div>";
    }

    if (controllerPresentationActive) {
        document += controllerPromptBar(panelHtml, controllerFamily, activeModal != nullptr, activeModal == nullptr || activeModal->dismissible);
    }

    document += "<div id=\"rr-performance-stats\"";
    if (!performanceStatsVisible || performanceStatsHtml.empty()) {
        document += " class=\"performance-hidden\"";
    }
    document += ">" + performanceStatsHtml + "</div>";

    document += "</body></rml>";
    return document;
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
};
std::vector<FocusTarget> g_focusTargets;
bool g_displayPreferenceChanged = false;

enum class ControllerFocusRow {
    None,
    Choices,
    Actions
};

ControllerFocusRow controllerFocusRow(const FocusTarget& target)
{
    if (!target.element) {
        return ControllerFocusRow::None;
    }
    if (target.element->Closest(".controller-choice-row")) {
        return ControllerFocusRow::Choices;
    }
    if (target.element->Closest(".controller-action-row")) {
        return ControllerFocusRow::Actions;
    }
    return ControllerFocusRow::None;
}

FocusTarget* nearestControllerRowTarget(ControllerFocusRow row, float centerX)
{
    FocusTarget* nearest = nullptr;
    float bestDistance = std::numeric_limits<float>::max();
    for (FocusTarget& target : g_focusTargets) {
        if (controllerFocusRow(target) != row) {
            continue;
        }
        const float distance = std::abs(target.centerX - centerX);
        if (distance < bestDistance) {
            bestDistance = distance;
            nearest = &target;
        }
    }
    return nearest;
}

FocusTarget* cycleControllerRowTarget(FocusTarget& current, UiDirection direction)
{
    const ControllerFocusRow row = controllerFocusRow(current);
    if (row == ControllerFocusRow::None || (direction != UiDirection::Left && direction != UiDirection::Right)) {
        return nullptr;
    }

    std::vector<FocusTarget*> rowTargets;
    for (FocusTarget& target : g_focusTargets) {
        if (controllerFocusRow(target) == row) {
            rowTargets.push_back(&target);
        }
    }
    std::sort(rowTargets.begin(), rowTargets.end(), [](const FocusTarget* lhs, const FocusTarget* rhs) {
        return lhs->centerX < rhs->centerX;
    });
    const auto currentIt = std::find(rowTargets.begin(), rowTargets.end(), &current);
    if (currentIt == rowTargets.end() || rowTargets.empty()) {
        return nullptr;
    }
    const std::size_t index = static_cast<std::size_t>(std::distance(rowTargets.begin(), currentIt));
    const std::size_t next = direction == UiDirection::Left
        ? (index + rowTargets.size() - 1) % rowTargets.size()
        : (index + 1) % rowTargets.size();
    return rowTargets[next];
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
    if (!binding.helpDismiss.empty()) {
        owner.dismissHelp(binding.helpDismiss);
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

void bindLoadedButtons(const std::string& documentRml)
{
    g_elementButtonBindings.clear();
    if (!g_document) {
        return;
    }

    std::vector<Rml::Element*> elements;
    collectButtonElements(g_document, elements);
    const std::vector<RmlButtonBinding> bindings = extractButtonBindings(documentRml);
    const std::size_t count = std::min(elements.size(), bindings.size());
    g_elementButtonBindings.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        g_elementButtonBindings.push_back({elements[index], bindings[index]});
    }
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

void collectFocusTargets(bool modalOpen)
{
    g_focusTargets.clear();
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
        const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
        const Rml::Vector2f size = element->GetBox().GetSize(Rml::BoxArea::Border);
        g_focusTargets.push_back({element, std::move(id), offset.x + size.x * 0.5f, offset.y + size.y * 0.5f});
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

    const Rml::String helpDismiss = button->GetAttribute<Rml::String>("data-help-dismiss", "");
    if (!helpDismiss.empty()) {
        owner.dismissHelp(helpDismiss);
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
    IRmlRenderHost& renderHost)
    : preferences_(preferences), host_(host), uiBridge_(uiBridge), renderHost_(renderHost)
{
}

bool GameRmlUi::initialize(ActionHandler actionHandler)
{
    g_preferences = &preferences_;
    g_host = &host_;
    g_uiBridge = &uiBridge_;
    actionHandler_ = std::move(actionHandler);

    Rml::SetSystemInterface(&g_systemInterface);

    if (!renderHost_.initialize()) {
        return false;
    }
    Rml::SetRenderInterface(&renderHost_.renderInterface());

    if (!Rml::Initialise()) {
        renderHost_.shutdown();
        return false;
    }

    const auto normal = Rml::Span<const Rml::byte>(reinterpret_cast<const Rml::byte*>(courier_prime_code), sizeof(courier_prime_code));
    const auto italic = Rml::Span<const Rml::byte>(reinterpret_cast<const Rml::byte*>(courier_prime_code_italic), sizeof(courier_prime_code_italic));
    Rml::LoadFontFace(normal, "rmlui-debugger-font", Rml::Style::FontStyle::Normal, Rml::Style::FontWeight::Normal);
    Rml::LoadFontFace(italic, "rmlui-debugger-font", Rml::Style::FontStyle::Italic, Rml::Style::FontWeight::Normal);

    g_context = Rml::CreateContext("rocket-ui", {rr_rml_viewport_width(), rr_rml_viewport_height()});
    if (!g_context) {
        Rml::Shutdown();
        renderHost_.shutdown();
        return false;
    }
    g_context->SetDensityIndependentPixelRatio(static_cast<float>(rr_rml_density_ratio()));

    rr_rml_set_enabled(1);
    initialized_ = true;
    return true;
}

void GameRmlUi::setPanelHtml(const std::string& html)
{
    if (panelHtml_ == html) {
        return;
    }

    const bool structureUnchanged = samePanelStructure(panelHtml_, html);
    const RmlPanelMode nextPanelMode = panelModeForHtml(html);
    const std::vector<ModalTemplate> modals = extractModals(html);
    const auto autoModal = std::find_if(modals.begin(), modals.end(), [](const ModalTemplate& modal) {
        return modal.autoOpen;
    });
    const bool autoModalWillOpen = openModalId_.empty() && autoModal != modals.end();
    const bool activeModalRemainsValid = openModalId_.empty() || findModal(modals, openModalId_);
    const ViewportMetrics viewport = host_.viewportMetrics();
    const bool viewportUnchanged = layoutViewportWidth_ == viewport.logicalWidth
        && layoutViewportHeight_ == viewport.logicalHeight;
    Rml::Element* panelElement = g_document ? g_document->GetElementById("rr-panel") : nullptr;
    const bool canUpdatePanelInPlace = initialized_ && g_context && g_document && panelElement
        && structureUnchanged
        && nextPanelMode == panelMode_
        && openModalId_.empty()
        && activeModalRemainsValid
        && !autoModalWillOpen
        && !g_displayPreferenceChanged
        && viewportUnchanged;

    panelHtml_ = html;
    panelMode_ = nextPanelMode;
    buttonBindings_ = extractButtonBindings(panelHtml_);
    if (!openModalId_.empty() && !findModal(modals, openModalId_)) {
        openModalId_.clear();
        modalStack_.clear();
        modalFocusStack_.clear();
    }
    if (openModalId_.empty()) {
        if (autoModal != modals.end()) {
            modalReturnFocusId_ = focusedId_;
            openModalId_ = autoModal->id;
            focusedId_.clear();
            hasLastFocusCenter_ = false;
        }
    }

    if (canUpdatePanelInPlace) {
        ++pendingPanelRebuilds_;
        pressedButton_ = nullptr;
        pressedButtonAtSeconds_ = 0.0;
        const std::string panelBody = withOpeningControllerLabels(
            syncSettingsControls(sanitizeRml(removeTemplates(panelHtml_))),
            controllerFamily_);
        panelElement->SetInnerRML(panelBody);

        if (controllerPresentationActive_) {
            Rml::Element* promptElement = g_document->GetElementById("rr-controller-prompt-bar");
            if (promptElement) {
                const std::string promptMarkup = controllerPromptBar(panelHtml_, controllerFamily_, false, true);
                const std::size_t contentStart = promptMarkup.find('>');
                const std::size_t contentEnd = promptMarkup.rfind("</div>");
                if (contentStart != std::string::npos && contentEnd != std::string::npos && contentEnd > contentStart) {
                    promptElement->SetInnerRML(promptMarkup.substr(contentStart + 1, contentEnd - contentStart - 1));
                }
            }
        }

        g_document->RemoveEventListener(Rml::EventId::Change, &g_settingsEventListener);
        g_document->AddEventListener(Rml::EventId::Change, &g_settingsEventListener);
        bindLoadedButtons(panelBody);
        g_context->Update();
        collectFocusTargets(false);
        if (controllerPresentationActive_ && !g_focusTargets.empty()) {
            FocusTarget* target = findFocusTarget(focusedId_);
            if (!target && hasLastFocusCenter_) {
                target = nearestFocusTarget(lastFocusCenterX_, lastFocusCenterY_);
            }
            if (!target) {
                target = defaultFocusTarget();
            }
            applyControllerFocus(target, focusedId_, lastFocusCenterX_, lastFocusCenterY_, hasLastFocusCenter_);
        }
        return;
    }
    rebuildDocument();
}

void GameRmlUi::setRealtimeHudState(const RealtimeHudState& state)
{
    if (!initialized_ || !g_document) {
        return;
    }

    for (const RealtimeHudPatch& patch : state.patches) {
        Rml::Element* element = g_document->GetElementById(patch.elementId);
        if (!element) {
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
    if (g_displayPreferenceChanged
        || viewportWidth != layoutViewportWidth_
        || viewportHeight != layoutViewportHeight_) {
        g_displayPreferenceChanged = false;
        layoutViewportWidth_ = viewportWidth;
        layoutViewportHeight_ = viewportHeight;
        rebuildDocument();
    }

    g_context->SetDimensions({viewportWidth, viewportHeight});
    g_context->SetDensityIndependentPixelRatio(viewport.densityRatio);
    renderHost_.setViewport({
        viewportWidth,
        viewportHeight,
        viewport.drawableWidth,
        viewport.drawableHeight,
    });
    Rml::Rectanglei rootClip;
    if (!openModalId_.empty() || controllerPresentationActive_ || performanceStatsVisible_
        || nativeSceneOverlayMode(panelHtml_) != NativeSceneOverlayMode::None) {
        rootClip = Rml::Rectanglei::FromSize({viewportWidth, viewportHeight});
    } else {
        rootClip = expandedPanelClip(panelMode_);
    }
    renderHost_.setRootClip({rootClip.Left(), rootClip.Top(), rootClip.Right(), rootClip.Bottom()});
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
    g_context->ProcessMouseWheel(static_cast<float>(-deltaY / 90.0), 0);
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
    if (nativeSceneOverlayMode(panelHtml_) == NativeSceneOverlayMode::PreflightLaunch && g_context) {
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
        return rr_rml_dom_navigate(static_cast<int>(direction)) != 0;
    }
    if (g_focusTargets.empty()) {
        collectFocusTargets(modalOpen());
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
                // the same persistence behavior as the DOM fallback.
                Rml::Dictionary parameters;
                parameters["value"] = select->GetValue();
                select->DispatchEvent(Rml::EventId::Change, parameters);
                return true;
            }
        }
    }

    const ControllerFocusRow currentRow = controllerFocusRow(*current);
    if (currentRow != ControllerFocusRow::None) {
        if (direction == UiDirection::Left || direction == UiDirection::Right) {
            return applyControllerFocus(
                cycleControllerRowTarget(*current, direction),
                focusedId_,
                lastFocusCenterX_,
                lastFocusCenterY_,
                hasLastFocusCenter_);
        }
        const ControllerFocusRow destinationRow = direction == UiDirection::Down && currentRow == ControllerFocusRow::Choices
            ? ControllerFocusRow::Actions
            : (direction == UiDirection::Up && currentRow == ControllerFocusRow::Actions ? ControllerFocusRow::Choices : ControllerFocusRow::None);
        if (destinationRow != ControllerFocusRow::None) {
            return applyControllerFocus(
                nearestControllerRowTarget(destinationRow, current->centerX),
                focusedId_,
                lastFocusCenterX_,
                lastFocusCenterY_,
                hasLastFocusCenter_);
        }
    }

    FocusTarget* bestTarget = nullptr;
    float bestScore = std::numeric_limits<float>::max();
    for (FocusTarget& candidate : g_focusTargets) {
        if (&candidate == current) {
            continue;
        }
        const float dx = candidate.centerX - current->centerX;
        const float dy = candidate.centerY - current->centerY;
        const float primary = (direction == UiDirection::Left || direction == UiDirection::Right) ? std::abs(dx) : std::abs(dy);
        const float secondary = (direction == UiDirection::Left || direction == UiDirection::Right) ? std::abs(dy) : std::abs(dx);
        const bool ahead = (direction == UiDirection::Left && dx < -1.0f) || (direction == UiDirection::Right && dx > 1.0f) ||
            (direction == UiDirection::Up && dy < -1.0f) || (direction == UiDirection::Down && dy > 1.0f);
        if (!ahead) {
            continue;
        }
        const float score = primary * 10.0f + secondary * 2.0f + (secondary * secondary) / std::max(1.0f, primary);
        if (score < bestScore) {
            bestScore = score;
            bestTarget = &candidate;
        }
    }
    return applyControllerFocus(bestTarget, focusedId_, lastFocusCenterX_, lastFocusCenterY_, hasLastFocusCenter_);
}

bool GameRmlUi::activateFocused()
{
    if (!initialized_ || !g_document) {
        return rr_rml_dom_activate() != 0;
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
        return rr_rml_dom_cancel() != 0;
    }
    if (!openModalId_.empty()) {
        const std::vector<ModalTemplate> modals = extractModals(panelHtml_);
        const ModalTemplate* modal = findModal(modals, openModalId_);
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
        return rr_rml_dom_scroll(amount) != 0;
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
    return initialized_ ? !openModalId_.empty() : rr_rml_dom_modal_open() != 0;
}

void GameRmlUi::setControllerPresentation(bool active, ControllerFamily family)
{
    rr_rml_dom_set_controller_presentation(active ? 1 : 0, static_cast<int>(family));
    if (controllerPresentationActive_ == active && controllerFamily_ == family) {
        return;
    }
    controllerPresentationActive_ = active;
    controllerFamily_ = family;
    if (initialized_) {
        rebuildDocument();
    }
}

void GameRmlUi::setControllerFocusVisible(bool visible)
{
    rr_rml_dom_set_controller_focus_visible(visible ? 1 : 0);
    if (controllerFocusVisible_ == visible) {
        return;
    }
    controllerFocusVisible_ = visible;
    if (initialized_) {
        rebuildDocument();
    }
}

void GameRmlUi::setControllerResumeBlocked(bool blocked, bool controllerConnected)
{
    rr_rml_dom_set_controller_resume_blocked(blocked ? 1 : 0, controllerConnected ? 1 : 0);
    if (controllerResumeBlocked_ == blocked && controllerResumeConnected_ == controllerConnected) {
        return;
    }
    controllerResumeBlocked_ = blocked;
    controllerResumeConnected_ = controllerConnected;
    if (initialized_) {
        rebuildDocument();
    }
}

std::string GameRmlUi::focusedId() const
{
    if (initialized_) {
        return focusedId_;
    }
    char buffer[256] = {};
    rr_rml_dom_focused_id(buffer, static_cast<int>(sizeof(buffer)));
    return buffer;
}

void GameRmlUi::openModal(const std::string& id)
{
    if (!initialized_) {
        rr_rml_dom_open_modal(id.c_str());
        return;
    }
    if (id.empty() || id == openModalId_) {
        return;
    }
    const std::vector<ModalTemplate> modals = extractModals(panelHtml_);
    if (!findModal(modals, id)) {
        return;
    }
    if (!openModalId_.empty()) {
        modalStack_.push_back(openModalId_);
        modalFocusStack_.push_back(focusedId_);
    } else {
        modalReturnFocusId_ = focusedId_;
    }
    openModalId_ = id;
    focusedId_.clear();
    hasLastFocusCenter_ = false;
    rebuildDocument();
}

void GameRmlUi::closeModal()
{
    if (!initialized_) {
        rr_rml_dom_close_modal();
        return;
    }
    std::string closeAction;
    if (modalStack_.empty()) {
        const std::vector<ModalTemplate> modals = extractModals(panelHtml_);
        if (const ModalTemplate* modal = findModal(modals, openModalId_)) {
            closeAction = modal->closeAction;
        }
    }
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
    rebuildDocument();
    if (!closeAction.empty() && actionHandler_) {
        actionHandler_(closeAction);
    }
}

void GameRmlUi::dismissHelp(const std::string& topic)
{
    rr_rml_dismiss_help_topic(topic.c_str());
    rebuildDocument();
}

void GameRmlUi::dispatchAction(const std::string& action)
{
    const bool closesModal = !openModalId_.empty();
    if (closesModal) {
        openModalId_.clear();
        modalStack_.clear();
        modalFocusStack_.clear();
        modalReturnFocusId_.clear();
    }
    if (actionHandler_) {
        actionHandler_(action);
    }
    if (closesModal) {
        rebuildDocument();
    }
}

void GameRmlUi::refresh()
{
    rebuildDocument();
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
    if (!it->helpDismiss.empty()) {
        dismissHelp(it->helpDismiss);
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
    uiBridge_.setPerformanceStats(nextHtml, visible);

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
        // Older documents created before the persistent overlay container was added
        // get repaired once. Normal statistics updates never rebuild the document.
        rebuildDocument();
        return;
    }
    if (htmlChanged) {
        performanceElement->SetInnerRML(performanceStatsHtml_);
    }
    if (visibilityChanged) {
        performanceElement->SetClass(
            "performance-hidden",
            !performanceStatsVisible_ || performanceStatsHtml_.empty());
    }
}

UiDiagnostics GameRmlUi::diagnostics() const
{
    return uiDiagnostics_;
}

void GameRmlUi::rebuildDocument()
{
    if (!initialized_ || !g_context) {
        return;
    }

    ++pendingDocumentRebuilds_;

    if (g_document) {
        g_context->UnloadDocument(g_document);
        g_document = nullptr;
        g_elementButtonBindings.clear();
        g_focusTargets.clear();
    }

    layoutViewportWidth_ = rr_rml_viewport_width();
    layoutViewportHeight_ = rr_rml_viewport_height();
    const std::string documentRml = buildDocumentRml(
        panelHtml_,
        openModalId_,
        controllerPresentationActive_,
        controllerFocusVisible_,
        controllerResumeBlocked_,
        controllerResumeConnected_,
        controllerFamily_,
        performanceStatsHtml_,
        performanceStatsVisible_);
    rr_rml_set_modal_open(openModalId_.empty() ? 0 : 1);
    g_document = g_context->LoadDocumentFromMemory(documentRml, "rocket://panel.rml");
    if (g_document) {
        bindLoadedButtons(documentRml);
        g_document->AddEventListener(Rml::EventId::Change, &g_settingsEventListener);
        g_document->Show();
        g_context->Update();
        collectFocusTargets(!openModalId_.empty());
        if (controllerPresentationActive_ && !g_focusTargets.empty()) {
            FocusTarget* target = findFocusTarget(focusedId_);
            if (!target && hasLastFocusCenter_) {
                target = nearestFocusTarget(lastFocusCenterX_, lastFocusCenterY_);
            }
            if (!target) {
                target = defaultFocusTarget();
            }
            applyControllerFocus(target, focusedId_, lastFocusCenterX_, lastFocusCenterY_, hasLastFocusCenter_);
        }
    } else {
        g_elementButtonBindings.clear();
        g_focusTargets.clear();
        Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to load Rocket Rogue RmlUi document.");
    }
}

void GameRmlUi::shutdown()
{
    if (!initialized_) {
        return;
    }
    if (g_context) {
        if (g_document) {
            g_context->UnloadDocument(g_document);
            g_document = nullptr;
        }
        Rml::RemoveContext("rocket-ui");
        g_context = nullptr;
    }
    Rml::Shutdown();
    renderHost_.shutdown();
    g_elementButtonBindings.clear();
    g_focusTargets.clear();
    rr_rml_set_enabled(0);
    pendingDocumentRebuilds_ = 0;
    uiDiagnostics_ = {};
    initialized_ = false;
    if (g_preferences == &preferences_) g_preferences = nullptr;
    if (g_host == &host_) g_host = nullptr;
    if (g_uiBridge == &uiBridge_) g_uiBridge = nullptr;
}

} // namespace rocket
