#include "game/GameRmlUi.h"
#include "game/GamePanel.h"
#include "game/IRmlRenderHost.h"
#include "core/ExpeditionSystem.h"
#include "core/MiningSystem.h"
#include "core/GameUi.h"
#include "core/ResearchSystem.h"
#include "core/ScenarioSystem.h"
#include "core/SolarProgression.h"
#include "core/UiViewportLayout.h"
#include "core/ArtifactProgression.h"
#include "core/ContentIds.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/RenderInterface.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <limits>
#include <memory>
#include <set>
#include <string_view>
#include <vector>
#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

namespace {
class Preferences final : public rocket::IPreferenceStore {
public:
    rocket::AppPreferences load() override { return value; }
    bool store(const rocket::AppPreferences& next) override { value = next; return true; }
    std::string lastError() const override { return {}; }
    rocket::AppPreferences value;
};
class Host final : public rocket::IPlatformHost {
public:
    double monotonicSeconds() const override { return now; }
    rocket::ViewportMetrics viewportMetrics() override { return viewport; }
    bool focused() const override { return true; }
    bool visible() const override { return true; }
    bool fullscreenAvailable() const override { return false; }
    bool fullscreen() const override { return false; }
    bool setFullscreen(bool) override { return false; }
    void log(rocket::PlatformLogLevel, std::string_view) override {}
    bool haptic(double, double, double) override { return false; }
    double now = 1.0;
    rocket::ViewportMetrics viewport;
};
class Bridge final : public rocket::IUiBridge {
public:
    void setUiHostContext(const rocket::UiHostContext&) override {}
    void setRmlUiEnabled(bool) override {}
    void setModalOpen(bool) override {}
    void setControllerPresentation(bool, rocket::ControllerFamily) override {}
    void setControllerFocusVisible(bool) override {}
    void setControllerResumeBlocked(bool, bool) override {}
    void preferencesChanged(const rocket::AppPreferences&) override {}
};
class Renderer final : public Rml::RenderInterface {
public:
    struct GeometryBounds {
        float left = std::numeric_limits<float>::max();
        float top = std::numeric_limits<float>::max();
        float right = std::numeric_limits<float>::lowest();
        float bottom = std::numeric_limits<float>::lowest();
    };
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int>) override
    {
        GeometryBounds bounds;
        for (const auto& vertex : vertices) {
            bounds.left = std::min(bounds.left, vertex.position.x);
            bounds.top = std::min(bounds.top, vertex.position.y);
            bounds.right = std::max(bounds.right, vertex.position.x);
            bounds.bottom = std::max(bounds.bottom, vertex.position.y);
        }
        const auto handle = next++;
        geometryBounds.emplace(handle, bounds);
        return handle;
    }
    void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle) override
    {
        ++renderedGeometry;
        const auto found = geometryBounds.find(handle);
        assert(found != geometryBounds.end());
        const auto& bounds = found->second;
        float left = std::max(bounds.left + translation.x, static_cast<float>(rootClip.left));
        float top = std::max(bounds.top + translation.y, static_cast<float>(rootClip.top));
        float right = std::min(bounds.right + translation.x, static_cast<float>(rootClip.right));
        float bottom = std::min(bounds.bottom + translation.y, static_cast<float>(rootClip.bottom));
        if (scissorEnabled && scissor.Valid()) {
            left = std::max(left, static_cast<float>(scissor.Left()));
            top = std::max(top, static_cast<float>(scissor.Top()));
            right = std::min(right, static_cast<float>(scissor.Right()));
            bottom = std::min(bottom, static_cast<float>(scissor.Bottom()));
        }
        if (right > left && bottom > top) ++unclippedGeometry;
    }
    void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override { geometryBounds.erase(handle); }
    Rml::TextureHandle LoadTexture(Rml::Vector2i& size, const Rml::String&) override { size = {1, 1}; return next++; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return next++; }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool enabled) override { scissorEnabled = enabled; }
    void SetScissorRegion(Rml::Rectanglei region) override { scissor = region; }
    std::map<Rml::CompiledGeometryHandle, GeometryBounds> geometryBounds;
    rocket::RmlRenderClip rootClip;
    Rml::Rectanglei scissor = Rml::Rectanglei::MakeInvalid();
    bool scissorEnabled = false;
    int renderedGeometry = 0;
    int unclippedGeometry = 0;
    std::uintptr_t next = 1;
};
class RenderHost final : public rocket::IRmlRenderHost {
public:
    bool initialize() override { return true; }
    Rml::RenderInterface& renderInterface() override { return renderer; }
    void setViewport(const rocket::RmlRenderViewport&) override {}
    void setRootClip(const rocket::RmlRenderClip& clip) override { renderer.rootClip = clip; }
    bool beginFrame() override { renderer.renderedGeometry = renderer.unclippedGeometry = 0; return true; }
    void endFrame() override {}
    rocket::UiDiagnostics diagnostics() const override { return {}; }
    void shutdown() override {}
    Renderer renderer;
};
std::string assetRoot()
{
    auto path = std::filesystem::path(__FILE__).parent_path().parent_path();
    if (std::filesystem::exists(path / "assets/ui/panel.rml")) return path.string();
    path = std::filesystem::current_path();
    for (int level = 0; level < 6; ++level, path = path.parent_path()) {
        if (std::filesystem::exists(path / "assets/ui/panel.rml")) return path.string();
    }
    assert(false);
    return {};
}
std::string button(std::string_view id, std::string_view extra = {})
{
    return "<button data-ui-focus-id=\"" + std::string(id) + "\" data-rr-action=\"" + std::string(id)
        + "\" " + std::string(extra) + "><span class=\"rr-button-label\">" + std::string(id) + "</span></button>";
}
rocket::PanelDocumentPresentation panel(std::string markup)
{
    rocket::PanelDocumentPresentation result;
    result.templateKind = rocket::PanelTemplateKind::Workspace;
    result.contentMarkup = "<div style=\"width: 600px;\">" + markup + "</div>";
    return result;
}
Rml::ElementDocument* document() { return Rml::GetContext("rocket-ui")->GetDocument(0); }
Rml::Element* soleFocus(const rocket::GameRmlUi& ui)
{
    Rml::GetContext("rocket-ui")->Update();
    Rml::ElementList focused;
    document()->GetElementsByClassName(focused, "rr-controller-focus");
    if (focused.size() != 1) std::cerr << "Focus count " << focused.size() << " for " << ui.focusedId() << "\n";
    assert(focused.size() == 1);
    const auto explicitId = focused.front()->GetAttribute<Rml::String>("data-ui-focus-id", "");
    assert(explicitId.empty() || explicitId == ui.focusedId());
    const auto background = focused.front()->GetProperty<Rml::Colourb>("background-color");
    if (background.red != 0xa5 || background.green != 0xec || background.blue != 0xf5)
        std::cerr << "Focus color " << static_cast<int>(background.red) << ',' << static_cast<int>(background.green)
            << ',' << static_cast<int>(background.blue) << " for " << ui.focusedId() << "\n";
    assert(background.red == 0xa5 && background.green == 0xec && background.blue == 0xf5);
    return focused.front();
}
std::set<std::string> reachable(rocket::GameRmlUi& ui, std::string initial)
{
    std::set<std::string> visited;
    std::vector<std::string> pending {initial};
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        if (!visited.insert(id).second) continue;
        for (const auto direction : {rocket::UiDirection::Up, rocket::UiDirection::Down,
                 rocket::UiDirection::Left, rocket::UiDirection::Right}) {
            ui.requestFocus(id);
            ui.navigate(direction);
            soleFocus(ui);
            if (!visited.contains(ui.focusedId())) pending.push_back(ui.focusedId());
        }
    }
    return visited;
}

void focusPass(int width, int height)
{
    Preferences preferences;
    Host host;
    host.viewport = {width, height, width, height, 1.0F};
    Bridge bridge;
    RenderHost renderer;
    rocket::GameRmlUi ui(preferences, host, bridge, renderer, assetRoot());
    std::string action;
    assert(ui.initialize([&](const std::string& value) { action = value; }));

    auto presentation = panel(button("scan", "data-ui-default-focus=\"1\"") + button("land") + button("resume"));
    ui.setPanelPresentation(presentation);
    ui.setControllerPresentation(true, rocket::ControllerFamily::Xbox);
    ui.setControllerFocusVisible(true);
    assert(ui.focusedId() == "scan");
    auto* focused = soleFocus(ui);
    assert(focused->QuerySelector(".rr-controller-confirm-label")->GetInnerRML() == "A");
    assert(ui.activateFocused() && action == "scan");
    const auto originalSize = focused->GetBox().GetSize(Rml::BoxArea::Border);
    ui.setControllerConfirmCancelSwapped(true);
    ui.render();
    focused = soleFocus(ui);
    assert(focused->QuerySelector(".rr-controller-confirm-label")->GetInnerRML() == "B");
    assert(focused->GetBox().GetSize(Rml::BoxArea::Border) == originalSize);
    ui.setControllerPresentation(true, rocket::ControllerFamily::PlayStation);
    assert(soleFocus(ui)->QuerySelector(".rr-controller-confirm-label")->GetInnerRML() == "Circle");
    // Mining buttons clip overflow and use a tall line-height. The confirm
    // badge must stay entirely inside the control, including its text.
    ui.setPanelPresentation(panel("<div class=\"mining-command-dock\"><div class=\"system-actions\">"
        + button("repair", "class=\"rr-text-button\" data-ui-default-focus=\"1\"") + "</div></div>"));
    ui.render();
    auto* miningButton = soleFocus(ui);
    auto* badge = miningButton->QuerySelector(".rr-controller-confirm-glyph");
    const auto buttonTop = miningButton->GetAbsoluteOffset(Rml::BoxArea::Border).y;
    const auto badgeTop = badge->GetAbsoluteOffset(Rml::BoxArea::Border).y;
    assert(badgeTop >= buttonTop);
    assert(badgeTop + badge->GetBox().GetSize(Rml::BoxArea::Border).y <=
        buttonTop + miningButton->GetBox().GetSize(Rml::BoxArea::Border).y);
    assert(badge->QuerySelector(".rr-controller-confirm-label")->GetInnerRML() == "Circle");
    auto* badgeText = badge->GetChild(0)->GetChild(0);
    assert(badgeText);
    const auto& lines = static_cast<Rml::ElementText*>(badgeText)->GetLines();
    assert(lines.size() == 1);
    assert(lines.front().width > 0);
    assert(badge->GetBox().GetSize(Rml::BoxArea::Content).x >= lines.front().width);

    // Dock route controls share the compact action row, whose outer panel clips
    // overflow. Their Circle badge must remain a complete, readable chip.
    ui.setPanelPresentation(panel("<div class=\"expedition-home\"><div class=\"expedition-dock-route-actions\">"
        "<button class=\"dock-depart\" data-ui-focus-id=\"depart\" data-rr-action=\"depart\" data-ui-default-focus=\"1\">"
        "<span class=\"rr-button-label\">DEPART FOR Titan</span></button>"
        "<button class=\"dock-waypoint\" data-ui-focus-id=\"waypoint\" data-rr-action=\"map\">"
        "<span class=\"rr-button-label\">Change waypoint</span></button></div></div>"));
    for (const auto* id : {"depart", "waypoint"}) {
        ui.requestFocus(id);
        ui.render();
        auto* routeButton = soleFocus(ui);
        auto* routeBadge = routeButton->QuerySelector(".rr-controller-confirm-glyph");
        assert(routeBadge);
        const auto routeButtonTop = routeButton->GetAbsoluteOffset(Rml::BoxArea::Border).y;
        const auto routeBadgeTop = routeBadge->GetAbsoluteOffset(Rml::BoxArea::Border).y;
        assert(routeBadgeTop >= routeButtonTop);
        assert(routeBadgeTop + routeBadge->GetBox().GetSize(Rml::BoxArea::Border).y <=
            routeButtonTop + routeButton->GetBox().GetSize(Rml::BoxArea::Border).y);
        assert(routeBadge->QuerySelector(".rr-controller-confirm-label")->GetInnerRML() == "Circle");
        auto* routeBadgeText = routeBadge->GetChild(0)->GetChild(0);
        assert(routeBadgeText);
        const auto& routeLines = static_cast<Rml::ElementText*>(routeBadgeText)->GetLines();
        assert(routeLines.size() == 1 && routeLines.front().width > 0);
        assert(routeBadge->GetBox().GetSize(Rml::BoxArea::Content).x >= routeLines.front().width);
    }
    ui.setPanelPresentation(presentation);
    ui.requestFocus("land");
    presentation.contentMarkup += "<p>Telemetry update</p>";
    ui.setPanelPresentation(presentation);
    assert(ui.focusedId() == "land");
    soleFocus(ui);
    ui.requestFocus("future");
    assert(ui.focusedId() == "land");
    soleFocus(ui);
    presentation.contentMarkup += button("future");
    ui.setPanelPresentation(presentation);
    assert(ui.focusedId() == "future");
    soleFocus(ui);

    // When the selected action retires, use the new semantic default, not a
    // nearby unrelated button. Metadata distinguishes a fresh drill hold.
    ui.requestFocus("scan");
    presentation = panel("<p>Scanning</p>" + button("resume", "data-ui-default-focus=\"1\""));
    ui.setPanelPresentation(presentation);
    assert(ui.focusedId() == "resume");
    presentation = panel(button("drill", "id=\"drill_control\" data-ui-default-focus=\"1\" data-ui-activation=\"continuous\"")
        + button("land") + button("resume"));
    ui.setPanelPresentation(presentation);
    assert(ui.focusedId() == "drill");
    assert(ui.focusedControllerAction().kind == rocket::ControllerActivationKind::ContinuousHold);
    assert(soleFocus(ui)->QuerySelector(".rr-controller-confirm-label")->GetInnerRML() == "Hold Circle");
    assert(reachable(ui, "drill") == std::set<std::string>({"drill", "land", "resume"}));
    ui.requestFocus("resume");
    presentation.contentMarkup += "<p>Drill telemetry</p>";
    ui.setPanelPresentation(presentation);
    assert(ui.focusedId() == "resume");

    presentation.modals.push_back({"confirm", "Confirm", button("cancel", "data-ui-default-focus=\"1\"")
        + button("reset", "data-ui-activation=\"hold\" data-ui-hold-seconds=\"0.75\"")
        + "<button data-ui-focus-id=\"nested\" data-ui-modal=\"details\">Details</button>"});
    presentation.modals.push_back({"details", "Details", button("details_ok", "data-ui-default-focus=\"1\"")});
    ui.setPanelPresentation(presentation);
    ui.requestFocus("land");
    ui.openModal("confirm");
    assert(ui.focusedId() == "cancel");
    assert(soleFocus(ui)->Closest("#rr-modal"));
    assert(!reachable(ui, "cancel").contains("land"));
    ui.requestFocus("reset");
    assert(ui.focusedControllerAction().kind == rocket::ControllerActivationKind::HoldToConfirm);
    assert(ui.focusedControllerAction().holdSeconds == 0.75);
    ui.requestFocus("nested");
    ui.openModal("details");
    assert(ui.focusedId() == "details_ok");
    assert(ui.cancelChildModal() && ui.focusedId() == "nested");
    assert(!ui.cancelChildModal() && ui.modalOpen());
    assert(ui.cancel() && ui.focusedId() == "land");
    soleFocus(ui);

    // Settings remain selected at option boundaries, skip disabled options,
    // and still let vertical movement reach checkbox and footer actions.
    presentation = panel("<select id=\"setting\" data-ui-focus-id=\"setting\" data-ui-default-focus=\"1\">"
        "<option value=\"first\" selected=\"1\">First</option><option value=\"disabled\" disabled=\"1\">Disabled</option>"
        "<option value=\"last\">Last</option></select>" + button("adjacent")
        + "<div><input type=\"checkbox\" data-ui-focus-id=\"checkbox\" name=\"test\" style=\"width: 20px; height: 20px;\" /></div>"
        + "<div>" + button("footer") + "</div>");
    ui.setPanelPresentation(presentation);
    ui.requestFocus("setting");
    assert(!ui.navigate(rocket::UiDirection::Left));
    assert(ui.focusedId() == "setting");
    assert(ui.navigate(rocket::UiDirection::Right));
    assert(dynamic_cast<Rml::ElementFormControlSelect*>(document()->GetElementById("setting"))->GetValue() == "last");
    assert(!ui.navigate(rocket::UiDirection::Right));
    assert(ui.focusedId() == "setting");
    ui.requestFocus("checkbox");
    if (ui.focusedId() != "checkbox") {
        Rml::ElementList inputs;
        document()->QuerySelectorAll(inputs, "input");
        for (auto* input : inputs) std::cerr << "checkbox visible=" << input->IsVisible(true) << " size="
            << input->GetBox().GetSize(Rml::BoxArea::Border).x << "," << input->GetBox().GetSize(Rml::BoxArea::Border).y
            << " type=" << input->GetAttribute<Rml::String>("type", "") << " id=" << input->GetAttribute<Rml::String>("data-ui-focus-id", "") << "\n";
    }
    auto* checkbox = soleFocus(ui);
    assert(!checkbox->HasAttribute("checked"));
    assert(ui.activateFocused());
    assert(checkbox->HasAttribute("checked"));

    // A regular card grid is reachable from its default, without wrapping;
    // hidden/disabled controls do not create traps or ghost focus.
    presentation = panel("<div>" + button("a", "data-ui-default-focus=\"1\"") + button("b") + "</div><div>"
        + button("c") + button("d") + "</div><div>" + button("footer") + "</div>"
        + button("disabled", "disabled=\"1\"") + button("hidden", "style=\"display: none;\""));
    ui.setPanelPresentation(presentation);
    const auto gridReachable = reachable(ui, "a");
    if (gridReachable != std::set<std::string>({"a", "b", "c", "d", "footer"})) {
        for (const auto& id : gridReachable) std::cerr << "Grid visited " << id << " @" << width << "\n";
    }
    assert(gridReachable == std::set<std::string>({"a", "b", "c", "d", "footer"}));
    ui.requestFocus("a");
    assert(!ui.navigate(rocket::UiDirection::Left));
    ui.requestFocus("d");
    host.viewport.logicalWidth = 900;
    host.viewport.drawableWidth = 900;
    ui.render();
    assert(ui.focusedId() == "d");
    soleFocus(ui);

    // Explicit cancellation cannot activate an invisible default behind the
    // player's back; a new UI entry re-establishes and displays that default.
    assert(ui.cancel());
    assert(ui.focusedControllerAction().id.empty());
    action.clear();
    assert(!ui.activateFocused() && action.empty());
    ui.setControllerFocusVisible(false);
    ui.setControllerFocusVisible(true);
    assert(ui.focusedId() == "a");
    soleFocus(ui);
    ui.shutdown();
}

void auditRenderedGraph(rocket::GameRmlUi& ui, const std::string& label)
{
    Rml::ElementList controls;
    auto* root = document()->GetElementById(ui.modalOpen() ? "rr-modal" : "rr-panel");
    root->QuerySelectorAll(controls, "button, select, input");
    if (!ui.modalOpen()) {
        Rml::ElementList overlayControls;
        document()->GetElementById("rr-scene-overlay-host")->QuerySelectorAll(overlayControls, "button, select, input");
        controls.insert(controls.end(), overlayControls.begin(), overlayControls.end());
    }
    std::set<std::string> expected;
    std::map<std::string, int> seen;
    int enabledDefaults = 0;
    for (auto* control : controls) {
        if (!control->IsVisible(true) || control->HasAttribute("disabled")
            || control->HasAttribute("data-ui-focus-skip") || control->GetAttribute<int>("tabindex", 0) < 0) continue;
        const auto size = control->GetBox().GetSize(Rml::BoxArea::Border);
        if (size.x <= 1 || size.y <= 1) continue;
        if (control->HasAttribute("data-ui-default-focus")) ++enabledDefaults;
        std::string id = control->GetAttribute<Rml::String>("data-ui-focus-id", "");
        if (id.empty() && control->HasAttribute("data-rr-action")) id = "action:" + control->GetAttribute<Rml::String>("data-rr-action", "");
        if (id.empty() && control->HasAttribute("data-ui-modal")) id = "modal:" + control->GetAttribute<Rml::String>("data-ui-modal", "");
        if (id.empty() && control->HasAttribute("data-ui-close-modal")) id = "modal:close";
        if (id.empty()) {
            const std::pair<const char*, const char*> settings[] {
                {"data-resolution-select", "resolution"}, {"data-frame-limit-select", "frame_limit"},
                {"data-game-speed-select", "game_speed"}, {"data-keyboard-drill-mode-select", "keyboard_drill_mode"},
                {"data-controller-prompt-select", "controller_prompt"}, {"data-controller-deadzone-select", "controller_deadzone"},
                {"data-controller-invert-toggle", "invertFlightY"}, {"data-controller-swap-toggle", "swapConfirmCancel"},
                {"data-controller-vibration-toggle", "vibrationEnabled"}};
            for (auto [attribute, key] : settings) if (control->HasAttribute(attribute)) id = "setting:" + std::string(key);
        }
        if (id.empty() && !control->GetId().empty()) id = "control:" + control->GetId();
        if (id.empty()) std::cerr << label << ": unidentified enabled " << control->GetTagName() << "\n";
        assert(!id.empty());
        const int duplicate = seen[id]++;
        if (duplicate) std::cerr << label << ": duplicate stable focus id " << id << "\n";
        assert(duplicate == 0);
        expected.insert(id);
    }
    if (enabledDefaults > 1) std::cerr << label << ": multiple enabled defaults " << enabledDefaults << "\n";
    assert(enabledDefaults <= 1);
    if (expected.empty()) return;
    assert(!ui.focusedId().empty());
    soleFocus(ui);
    const auto visited = reachable(ui, ui.focusedId());
    for (const auto& id : expected) {
        if (!visited.contains(id)) std::cerr << label << ": unreachable " << id << "\n";
    }
    assert(std::includes(visited.begin(), visited.end(), expected.begin(), expected.end()));
    if (label.find("mining") != std::string::npos) {
        for (const auto& id : expected) {
            ui.requestFocus(id);
            ui.render();
            auto* glyph = soleFocus(ui)->QuerySelector(".rr-controller-confirm-glyph");
            if (!glyph) continue;
            auto* text = static_cast<Rml::ElementText*>(glyph->GetChild(0)->GetChild(0));
            assert(text && !text->GetLines().empty());
            const auto width = glyph->GetBox().GetSize(Rml::BoxArea::Content).x;
            const float baseline = text->GetAbsoluteOffset().y + text->GetLines().front().position.y;
            const float bottom = glyph->GetAbsoluteOffset(Rml::BoxArea::Border).y + glyph->GetBox().GetSize(Rml::BoxArea::Border).y;
            const float textLeft = text->GetAbsoluteOffset().x + text->GetLines().front().position.x;
            const float left = glyph->GetAbsoluteOffset(Rml::BoxArea::Content).x;
            assert(width >= text->GetLines().front().width);
            assert(baseline <= bottom);
            assert(textLeft >= left && textLeft + text->GetLines().front().width <= left + width + 1);
        }
    }
}

void assertActionLabelFits(Rml::Element* root, const std::string& action, const std::string& text)
{
    // Scenario action ids contain a pipe separator. Match the attribute value
    // directly instead of embedding it in a CSS selector, whose parser treats
    // that character as a selector token and can fail to find the button.
    Rml::ElementList controls;
    root->QuerySelectorAll(controls, "button[data-rr-action]");
    auto* control = static_cast<Rml::Element*>(nullptr);
    for (auto* candidate : controls) {
        const auto value = candidate->GetAttribute<Rml::String>("data-rr-action", "");
        const bool scenario = action.rfind("scenario_action:", 0) == 0;
        if ((scenario && value.rfind("scenario_action:", 0) == 0) ||
            (!scenario && value == action)) {
            control = candidate;
            break;
        }
    }
    assert(control);
    auto* label = control->QuerySelector(".rr-button-label");
    const float available = control->GetBox().GetSize(Rml::BoxArea::Content).x;
    const int required = Rml::ElementUtilities::GetStringWidth(label ? label : control, text);
    if (required > available + 1) std::cerr << action << ": label requires " << required << "px, has " << available << "px\n";
    assert(required <= available + 1);
}

void generatedPanelPass(int width, int height)
{
    Preferences preferences;
    Host host;
    host.viewport = {width, height, width, height, 1.0F};
    Bridge bridge;
    RenderHost renderer;
    rocket::GameRmlUi ui(preferences, host, bridge, renderer, assetRoot());
    assert(ui.initialize([](const std::string&) {}));
    ui.setControllerPresentation(true, rocket::ControllerFamily::SteamDeck);
    ui.setControllerFocusVisible(true);
    const auto catalog = rocket::createDefaultContent();
    auto state = std::make_unique<rocket::GameState>(rocket::createNewGame(catalog, 0xC047ULL));
    state->run.destinationIndex = 2;
    rocket::startSurfaceExpedition(*state, catalog);
    state->meta.unlockKeys.push_back(rocket::content::unlock::droneBay);
    state->meta.unlockKeys.push_back(rocket::content::unlock::droneSupportSuite);
    state->meta.droneBaySlots = 2;
    state->meta.ownedDroneIds.push_back(rocket::content::drone::miningDrone);
    rocket::ensureDroneBayState(*state, catalog);
    rocket::ui::briefings::acknowledge(state->meta.acknowledgedActivityBriefingIds, rocket::ui::briefings::miniDrones);
    rocket::Random rng(0xC047ULL);
    const auto launch = rocket::prepareLaunch(*state, catalog, rng);
    rocket::PanelRenderContext context {*state, catalog, launch, launch};
    context.firstTimeIntroductionsEnabled = false;
    context.incomingMessageDeliveryAllowed = false;
    for (const auto screen : {rocket::Screen::Hangar, rocket::Screen::DroneOps,
             rocket::Screen::Research, rocket::Screen::SurfaceUpgrade, rocket::Screen::Navigation}) {
        state->screen = screen;
        auto presentation = rocket::buildGamePanelPresentation(context);
        ui.setPanelPresentation(presentation);
        const auto label = "screen " + std::to_string(static_cast<int>(screen)) + " @" + std::to_string(width);
        auditRenderedGraph(ui, label);
        if (screen == rocket::Screen::DroneOps)
            assertActionLabelFits(document()->GetElementById("rr-panel"), "drone_view:expand", "Expand bay");
        if (!presentation.missionTrackerMarkup.empty()) {
            assert(presentation.contentMarkup.find("rr-mission-tracker") == std::string::npos);
            auto* panelButton = document()->GetElementById("rr-panel")->QuerySelector("button[data-rr-action]");
            assert(panelButton);
            const auto withMission = panelButton->GetAbsoluteOffset();
            auto* overlay = document()->GetElementById("rr-scene-overlay-host");
            if (overlay->GetElementById("rr-mission-tracker-toggle")) {
                assert(!overlay->GetElementById("rr-mission-tracker"));
                if (screen == rocket::Screen::SurfaceUpgrade) {
                    auto* tab = overlay->GetElementById("rr-mission-tracker-toggle");
                    auto* hero = document()->GetElementById("rr-panel")->QuerySelector(".level-up-stamp");
                    assert(hero);
                    const auto tabLeft = tab->GetAbsoluteOffset(Rml::BoxArea::Border).x;
                    const auto tabWidth = tab->GetBox().GetSize(Rml::BoxArea::Border).x;
                    const auto heroLeft = hero->GetAbsoluteOffset(Rml::BoxArea::Border).x;
                    assert(tabLeft + tabWidth + 8 <= heroLeft);
                }
                ui.dispatchAction("ui:toggle_mission_tracker");
                assert(overlay->GetElementById("rr-mission-tracker"));
                ui.dispatchAction("ui:toggle_mission_tracker");
                assert(overlay->GetElementById("rr-mission-tracker-toggle"));
            } else {
                assert(overlay->GetElementById("rr-mission-tracker"));
            }
            presentation.missionTrackerMarkup.clear();
            ui.setPanelPresentation(presentation);
            ui.render();
            panelButton = document()->GetElementById("rr-panel")->QuerySelector("button[data-rr-action]");
            assert(panelButton);
            const auto withoutMission = panelButton->GetAbsoluteOffset();
            assert(std::abs(withMission.x - withoutMission.x) <= 1.0F);
            assert(std::abs(withMission.y - withoutMission.y) <= 1.0F);
            ui.setPanelPresentation(rocket::buildGamePanelPresentation(context));
        }
        if (screen == rocket::Screen::Hangar) {
            for (const auto& modal : presentation.modals) {
                ui.openModal(modal.id);
                auditRenderedGraph(ui, modal.id + " @" + std::to_string(width));
                ui.closeModal();
            }
        }
    }
    // The live solar map has different world choices from the legacy route map.
    *state = rocket::createNewGame(catalog, 0xC048ULL);
    assert(rocket::initializeLiveExpedition(*state, catalog));
    state->screen = rocket::Screen::Hangar;
    auto presentation = rocket::buildGamePanelPresentation(context);
    ui.setPanelPresentation(presentation);
    ui.openModal("map");
    auditRenderedGraph(ui, "solar map @" + std::to_string(width));
    ui.closeModal();
    context.titleScreenActive = true;
    ui.setPanelPresentation(rocket::buildGamePanelPresentation(context));
    auditRenderedGraph(ui, "title @" + std::to_string(width));
    context.titleScreenActive = false;
    const auto audit = [&](const std::string& label) {
        ui.setPanelPresentation(rocket::buildGamePanelPresentation(context));
        auditRenderedGraph(ui, label + " @" + std::to_string(width));
    };
    *state = rocket::createNewGame(catalog, 0xC049ULL);
    state->screen = rocket::Screen::Flight;
    context.flightArmed = false;
    audit("preflight");
    context.preflightReady = false;
    audit("preflight blocked");
    context.preflightReady = true;
    context.flightArmed = true;
    rocket::FlightRunState flight;
    flight.active = flight.physicalFlight = true;
    flight.destinationId = "moon";
    flight.selectedThrottle = 0;
    flight.mode = rocket::FlightMode::Travel;
    context.launchFlight = &flight;
    audit("manual flight");
    flight.mode = rocket::FlightMode::Orbit;
    flight.orbit.captured = flight.orbit.loopQualifies = true;
    rocket::OrbitalWorkState work;
    context.orbitalWork = &work;
    context.orbitalInsideZone = true;
    audit("orbit scan");
    work.phase = rocket::OrbitalWorkPhase::Surveying;
    audit("orbit scanning");
    work.phase = rocket::OrbitalWorkPhase::LaserReady;
    work.surveyComplete = true;
    audit("orbit drill");
    context.orbitalLaserComplete = true;
    audit("orbit shaft ready");
    work.phase = rocket::OrbitalWorkPhase::Inactive;
    context.orbitalLandingEligible = true;
    const auto returningPanel = rocket::buildGamePanelPresentation(context);
    assert(returningPanel.contentMarkup.find("land_from_orbit") != std::string::npos);
    assert(returningPanel.contentMarkup.find("resume_orbital_flight") == std::string::npos);
    audit("revisited shaft while piloting");
    context.orbitalInsideZone = false;
    context.orbitalLandingEligible = false;
    const auto outsidePanel = rocket::buildGamePanelPresentation(context);
    assert(outsidePanel.contentMarkup.find("land_from_orbit") == std::string::npos);
    assert(outsidePanel.contentMarkup.find("Ready to land") == std::string::npos);
    assert(outsidePanel.contentMarkup.find("resume_orbital_flight") == std::string::npos);
    audit("revisited shaft outside wedge");
    context.orbitalInsideZone = true;
    work.phase = rocket::OrbitalWorkPhase::LaserReady;
    context.orbitalLaserComplete = false;
    context.orbitalLaserBlocked = true;
    audit("orbit tools blocked");
    context.orbitalInsideZone = false;
    audit("orbit outside zone");
    work.phase = rocket::OrbitalWorkPhase::LandingAlignment;
    audit("orbit alignment");
    context.orbitalWork = nullptr;
    context.orbitalLaserBlocked = false;
    flight.mode = rocket::FlightMode::Landing;
    flight.landing.siteBound = true;
    audit("manual descent");
    const auto auditRestoredSurfaceTitle = [&] {
        context.titleScreenActive = true;
        context.hasSavedGame = true;
        const auto title = rocket::buildGamePanelPresentation(context);
        assert(title.metadata.overlay == rocket::PanelOverlayKind::None);
        assert(title.metadata.variant == "title");
        assert(title.templateKind != rocket::PanelTemplateKind::Mining);
        ui.setPanelPresentation(title);
        auditRenderedGraph(ui, "restored surface title @" + std::to_string(width));
        context.titleScreenActive = false;
        const auto resumed = rocket::buildGamePanelPresentation(context);
        assert(resumed.metadata.overlay == rocket::PanelOverlayKind::MiningExperience);
        assert(resumed.templateKind == rocket::PanelTemplateKind::Mining);
    };
    auditRestoredSurfaceTitle();
    flight.landing.departureActive = true;
    audit("manual ascent");
    auditRestoredSurfaceTitle();
    flight.landing.departureActive = false;
    context.surfaceArrivalActive = true;
    context.surfaceArrivalPhase = 3;
    context.surfaceArrivalLandingCommitted = true;
    audit("deployment choices");
    auditRestoredSurfaceTitle();
    context.surfaceArrivalLandingCommitted = false;
    audit("undeployed takeoff");
    context.surfaceArrivalPhase = 4;
    audit("deployment animation");
    auditRestoredSurfaceTitle();
    context.surfaceArrivalActive = false;
    context.launchFlight = nullptr;

    *state = rocket::createNewGame(catalog, 0xC04AULL);
    state->run.destinationIndex = 2;
    rocket::startSurfaceExpedition(*state, catalog);
    state->run.planetaryExpedition.miningSitePrepared = true;
    assert(rocket::startMiningRun(*state, catalog).applied);
    state->run.mining.droneX = state->run.mining.returnZoneX;
    state->run.mining.droneY = state->run.mining.returnZoneY;
    state->run.mining.drillIntegrity = 0;
    state->run.mining.drillBreakNotified = true;
    state->run.mining.droneHealth = .5;
    state->run.mining.stowedMaterials.common = state->run.mining.stowedCargo = 10;
    audit("mining service");
    state->run.mining.droneX += 8;
    audit("mining field");
    state->run.mining.failurePending = true;
    state->run.mining.failureMessage = "Drone health lost. Emergency recall fired.";
    context.miningFailureModalReady = true;
    audit("mining failure");

    *state = rocket::createNewGame(catalog, 0xC04BULL);
    state->screen = rocket::Screen::Results;
    state->lastOutcome.type = rocket::LaunchResultType::SafeEject;
    state->lastOutcome.recoveryMethod = rocket::RecoveryMethod::ReturnHome;
    state->lastOutcome.ejectMultiplier = 1.1;
    state->lastOutcome.crashMultiplier = 1.5;
    audit("safe return results");
    state->lastOutcome.type = rocket::LaunchResultType::MissionComplete;
    audit("mission results");
    for (const auto story : {rocket::StoryBriefingId::CampaignIntroduction, rocket::StoryBriefingId::StraylightDiscovery,
             rocket::StoryBriefingId::StraylightApproach, rocket::StoryBriefingId::ActOneComplete}) {
        state->screen = rocket::Screen::StoryBriefing;
        state->storyBriefing.pending = story;
        audit("story " + std::to_string(static_cast<int>(story)));
    }
    *state = rocket::createNewGame(catalog, 0xC04CULL);
    state->meta.launchLessons.stage = rocket::LaunchTrainingStage::Complete;
    state->run.credits = 1000;
    rocket::generateModuleOffers(*state, catalog, rng);
    state->screen = rocket::Screen::Upgrade;
    context.selectedRefitOfferIndex = 1;
    audit("refit choices");
    rocket::awardExpeditionExperience(*state, 75, rocket::Screen::SurfaceExpedition);
    rocket::generateRunUpgradeOffers(*state, catalog, rng);
    state->screen = rocket::Screen::SurfaceUpgrade;
    audit("upgrade draft");

    // The physical Io commission is an explicit action, not a side effect of
    // viewing its transmission. Acknowledged old saves retain a visible path.
    *state = rocket::createNewGame(catalog, 0xC04DULL);
    state->run.destinationIndex = 2;
    rocket::startSurfaceExpedition(*state, catalog);
    state->run.planetaryExpedition.miningSitePrepared = true;
    assert(rocket::startMiningRun(*state, catalog).applied);
    state->run.expedition.travelInitialized = state->run.expedition.active = true;
    state->run.expedition.location.bodyId = state->run.mining.bodyId = "io";
    state->meta.unlockKeys.push_back(rocket::content::unlock::routeJupiter);
    rocket::ensureScenarioInstances(*state, catalog);
    const auto* io = rocket::solarMissionForBody(catalog, "io");
    assert(io);
    const auto commission = rocket::solarMissionAcceptanceForBody(*state, catalog, "io");
    assert(commission.available);
    const auto commissionAction = rocket::ui::actions::scenarioAction(
        commission.scenarioId, commission.stepId, static_cast<int>(commission.action));
    state->incomingMessages.acknowledgedMessages.push_back(io->briefingMessageId);
    state->incomingMessages.acknowledgedOccurrences.push_back("campaign.solar.io.briefing");
    state->run.mining.droneX = state->run.mining.returnZoneX + 8;
    state->run.mining.droneY = state->run.mining.returnZoneY;
    auto ioPanel = rocket::buildGamePanelPresentation(context);
    assert(ioPanel.contentMarkup.find("data-hazard-support=\"uncommissioned\"") == std::string::npos);
    assert(ioPanel.contentMarkup.find("mining-command-dock") == std::string::npos);
    assert(ioPanel.missionTrackerMarkup.find(commissionAction) != std::string::npos);
    audit("Io mining commission in mission tracker");
    assertActionLabelFits(document()->GetElementById("rr-mission-tracker"), commissionAction, "Commission Hazard Drone");
    state->screen = rocket::Screen::DroneOps;
    const int hazardIndex = static_cast<int>(std::find_if(catalog.miniDrones.begin(), catalog.miniDrones.end(),
        [](const auto& drone) { return drone.id == rocket::content::drone::hazardDrone; }) - catalog.miniDrones.begin());
    context.droneSelection = {0, hazardIndex, true, false};
    ui.setPanelPresentation(rocket::buildGamePanelPresentation(context));
    ui.requestFocus("action:" + commissionAction);
    assert(ui.focusedId() == "action:" + commissionAction);
    auditRenderedGraph(ui, "Io Drone Ops commission @" + std::to_string(width));
    auto* previewScroll = document()->QuerySelector(".drone-preview-scroll");
    std::string longEffects;
    for (int row = 0; row < 30; ++row) longEffects += "<p>Selected graft and synergy consequence.</p>";
    previewScroll->SetInnerRML(longEffects);
    Rml::GetContext("rocket-ui")->Update();
    ui.requestFocus("action:" + commissionAction);
    assert(ui.scroll(1.0F));
    assert(previewScroll->GetScrollTop() > 0);
    ui.requestFocus("drone-preview-content");
    assert(!ui.activateFocused());
    assert(ui.navigate(rocket::UiDirection::Left));
    assert(ui.focusedId() == "action:select_drone:" + std::to_string(hazardIndex));
    assert(rocket::acceptSolarMission(*state, catalog, *io).accepted);
    assert(rocket::equippedMiniDroneCount(*state, rocket::content::drone::hazardDrone) == 1);
    assert(!state->run.mining.miniDrones.empty());
    {
        // Read the required rank from authored mission data, not the body or
        // merely the presence of a Hazard drone in the loadout.
        auto rankCatalog = catalog;
        for (auto& site : rankCatalog.miningSites)
            for (auto& layer : site.cocoon.layers) layer.requiredHazardMark = 2;
        auto rankState = std::make_unique<rocket::GameState>(*state);
        rocket::PanelRenderContext rankContext {*rankState, rankCatalog, launch, launch};
        auto requirement = rocket::buildGamePanelPresentation(rankContext).contentMarkup;
        assert(requirement.find("Hazard Mk II required / Higher Mk needed") != std::string::npos);
        rankState->run.expedition.progression.runDroneRanks = {{rocket::content::drone::hazardDrone, 2}};
        requirement = rocket::buildGamePanelPresentation(rankContext).contentMarkup;
        assert(requirement.find("Hazard Mk II required / Ready") != std::string::npos);
        rankState->meta.equippedDroneIds.clear();
        requirement = rocket::buildGamePanelPresentation(rankContext).contentMarkup;
        assert(requirement.find("Hazard Mk II required / Not equipped") != std::string::npos);
    }
    while (!state->incomingMessages.pending.empty())
        assert(rocket::acknowledgeIncomingMessage(state->incomingMessages, state->incomingMessages.pending.front().id));
    // Old recall state, remote workers and cargo must not disable Drone Ops.
    context.droneSelection = {0, hazardIndex, false, false};
    state->meta.equippedDroneIds.clear();
    state->run.mining.miniDrones.clear();
    state->run.mining.droneLoadoutRecallActive = true;
    ioPanel = rocket::buildGamePanelPresentation(context);
    assert(ioPanel.contentMarkup.find("drone-preview") != std::string::npos);
    assert(ioPanel.contentMarkup.find("Commission Hazard Drone") == std::string::npos);
    audit("Io owned unassigned away from service");
    const auto assertAssignmentReady = [&] {
        Rml::ElementList actions;
        document()->GetElementById("rr-panel")->QuerySelectorAll(actions, "button[data-rr-action]");
        const auto owned = std::find_if(catalog.miniDrones.begin(), catalog.miniDrones.end(),
            [](const auto& drone) { return drone.id == rocket::content::drone::hazardDrone; });
        const auto ownedAction = std::string("assign_drone_slot:0:") + std::to_string(owned-catalog.miniDrones.begin());
        bool found = false;
        for (auto* action : actions) {
            const auto id = action->GetAttribute<Rml::String>("data-rr-action", "");
            if (id == ownedAction) { found = true; assert(!action->HasAttribute("disabled")); }
        }
        assert(found);
        assert(!document()->GetElementById("rr-panel")->QuerySelector("button[data-rr-action=\"mining_wait_for_drones\"]"));
    };
    assertAssignmentReady();
    state->run.mining.droneX = state->run.mining.returnZoneX;
    rocket::MiningMiniDroneAgent hauling;
    hauling.haulMaterials.common = 3;
    state->run.mining.miniDrones.push_back(hauling);
    audit("Io service outstanding drones");
    assertAssignmentReady();
    ui.requestFocus("action:select_drone:" + std::to_string(hazardIndex));
    assert(ui.navigate(rocket::UiDirection::Right));
    assert(soleFocus(ui)->Closest(".drone-preview-actions"));
    assert(ui.navigate(rocket::UiDirection::Left));
    assert(soleFocus(ui)->Closest(".drone-picker-list"));
    auto& deployedWorker = state->run.mining.miniDrones.back();
    deployedWorker.haulMaterials = {};
    deployedWorker.x = state->run.mining.returnZoneX + 8;
    deployedWorker.y = state->run.mining.returnZoneY;
    deployedWorker.behavior = rocket::MiningMiniDroneBehavior::Working;
    assert(rocket::miningDroneRecoveryStatus(state->run.mining).outstandingDrones == 0);
    assert(rocket::miningDroneRecoveryStatus(state->run.mining, true).outstandingDrones > 0);
    audit("Io service empty deployed worker");
    assertAssignmentReady();
    deployedWorker.x = state->run.mining.returnZoneX;
    deployedWorker.behavior = rocket::MiningMiniDroneBehavior::Docked;
    assert(rocket::miningDroneRecoveryStatus(state->run.mining, true).outstandingDrones == 0);
    state->meta.droneBaySlots = 2;
    state->meta.materials = {1000, 1000, 1000};
    state->run.mining.droneLoadoutRecallActive = false;
    audit("Io service assignment ready");
    const auto hazard = std::find_if(catalog.miniDrones.begin(), catalog.miniDrones.end(), [](const auto& drone) {
        return drone.id == rocket::content::drone::hazardDrone;
    });
    assert(hazard != catalog.miniDrones.end());
    const auto hazardAction = std::string("assign_drone_slot:0:") + std::to_string(hazard-catalog.miniDrones.begin());
    auto* assign = document()->GetElementById("rr-panel")->QuerySelector("button[data-rr-action=\"" + hazardAction + "\"]");
    assert(assign && !assign->HasAttribute("disabled"));
    state->meta.equippedDroneIds.push_back(rocket::content::drone::hazardDrone);
    state->screen = rocket::Screen::Mining;
    ioPanel = rocket::buildGamePanelPresentation(context);
    assert(ioPanel.contentMarkup.find("data-hazard-support=") == std::string::npos);
    audit("Io assigned mining support");

    // Commission prompts bind to the authored acceptance label, while unrelated
    // transmissions retain their acknowledgement and physical location gate.
    *state = rocket::createNewGame(catalog, 0xC04EULL);
    state->screen = rocket::Screen::Flight;
    state->run.expedition.travelInitialized = state->run.expedition.active = true;
    state->run.expedition.location.bodyId = "io";
    state->run.flight.active = state->run.flight.physicalFlight = true;
    state->run.flight.destinationId = io->environmentId;
    state->run.flight.mode = rocket::FlightMode::Orbit;
    state->run.flight.selectedThrottle = 0;
    state->run.flight.orbit.captured = state->run.flight.orbit.loopQualifies = true;
    context.launchFlight = &state->run.flight;
    work = {};
    work.phase = rocket::OrbitalWorkPhase::LaserReady;
    work.surveyComplete = true;
    context.orbitalWork = &work;
    context.orbitalInsideZone = true;
    state->meta.unlockKeys.push_back(rocket::content::unlock::routeJupiter);
    rocket::ensureScenarioInstances(*state, catalog);
    state->incomingMessages.pending.push_back({"io-briefing", io->briefingMessageId, "default"});
    context.incomingMessageDeliveryAllowed = true;
    ioPanel = rocket::buildGamePanelPresentation(context);
    const auto incoming = std::find_if(ioPanel.modals.begin(), ioPanel.modals.end(), [](const auto& modal) { return modal.id == "incoming_message"; });
    assert(incoming != ioPanel.modals.end());
    assert(incoming->bodyMarkup.find("Commission Hazard Drone") != std::string::npos);
    assert(incoming->bodyMarkup.find("ack_incoming_message:io-briefing") != std::string::npos);
    audit("Io authored commission transmission");
    ui.setPanelPresentation(ioPanel);
    ui.openModal("incoming_message");
    assertActionLabelFits(document()->GetElementById("rr-modal"), "ack_incoming_message:io-briefing", "Commission Hazard Drone");
    ui.closeModal();
    state->incomingMessages.pending.clear();
    context.incomingMessageDeliveryAllowed = false;
    audit("Io live flight commission");
    state->run.expedition.location.bodyId = "jupiter";
    ioPanel = rocket::buildGamePanelPresentation(context);
    assert(ioPanel.contentMarkup.find("data-hazard-support=") == std::string::npos);
    ui.shutdown();
}
void recoveredDockKeepsSubmittingVisibleGeometry(int width, int height, float density)
{
    Preferences preferences;
    Host host;
    host.viewport = {width, height, static_cast<int>(width * density), static_cast<int>(height * density), density};
    Bridge bridge;
    RenderHost renderer;
    rocket::GameRmlUi ui(preferences, host, bridge, renderer, assetRoot());
    assert(ui.initialize([](const std::string&) {}));
    // Use pointer presentation: a controller prompt or modal must not be
    // required to expand the recovered dock's root clip.
    ui.setControllerPresentation(false, rocket::ControllerFamily::Generic);
    ui.setControllerFocusVisible(false);
    const auto catalog = rocket::createDefaultContent();
    auto state = std::make_unique<rocket::GameState>(rocket::createNewGame(catalog, 0xD0C5ULL));
    assert(rocket::initializeLiveExpedition(*state, catalog));
    auto& expedition = state->run.expedition;
    expedition.active = expedition.openingInitialized = true;
    expedition.departureCount = 2;
    expedition.location = {"solar", "", rocket::CoordinateFrame::System, {}, {}, 0.0, ""};
    expedition.course.targetBodyId = "io";
    state->screen = rocket::Screen::Flight;
    auto launch = rocket::expeditionFlightModel(*state, catalog);
    state->run.flight = rocket::beginLaunchFlight(launch, rocket::expeditionEnvironment(*state, catalog));
    state->run.flight.mode = rocket::FlightMode::Travel;
    state->run.flight.positionX = 22;
    state->run.flight.positionY = 6;
    rocket::captureSystemLocation(expedition.location, state->run.flight);
    rocket::PanelRenderContext context {*state, catalog, launch, launch};
    context.launchFlight = &state->run.flight;
    context.firstTimeIntroductionsEnabled = false;
    context.incomingMessageDeliveryAllowed = false;
    ui.setPanelPresentation(rocket::buildGamePanelPresentation(context));
    for (int frame = 0; frame < 3; ++frame) ui.render();
    state->run.flight.active = false;
    state->run.flight.hullRemaining = 0;
    state->run.flight.phase = rocket::FlightPhase::Impact;
    state->run.flight.failureCause = rocket::LaunchFailureCause::HullBreach;
    ui.setPanelPresentation(rocket::buildGamePanelPresentation(context));
    ui.render();

    assert(rocket::recoverExpedition(*state, rocket::solarSystemDefinition()) == rocket::ExpeditionResult::Applied);
    assert(state->screen == rocket::Screen::Hangar && rocket::operationalHomeDocked(expedition));
    launch = rocket::expeditionFlightModel(*state, catalog);
    context.flightArmed = false;
    const auto dock = rocket::buildGamePanelPresentation(context);
    assert(dock.metadata.layoutMode == rocket::PanelLayoutMode::Fullscreen);
    assert(dock.metadata.overlay == rocket::PanelOverlayKind::None);
    assert(!dock.runtime.gameplayInputHelper);
    ui.setPanelPresentation(dock);
    const auto assertDockDraws = [&] {
        host.now += 1.0 / 60.0;
        ui.render();
        assert(!ui.modalOpen());
        const auto& output = renderer.renderer;
        assert(output.rootClip.left == 0 && output.rootClip.top == 0);
        assert(output.rootClip.right == width && output.rootClip.bottom == height);
        assert(output.renderedGeometry > 10 && output.unclippedGeometry > 10);
        auto* depart = document()->QuerySelector("button[data-rr-action=\"expedition:depart\"]");
        assert(depart && depart->IsVisible());
        const auto position = depart->GetAbsoluteOffset(Rml::BoxArea::Border);
        const auto size = depart->GetBox().GetSize(Rml::BoxArea::Border);
        assert(size.x > 0 && size.y > 0 && position.x < width && position.y < height);
        assert(position.x + size.x > 0 && position.y + size.y > 0);
    };
    assertDockDraws();
    const int firstUnclippedGeometry = renderer.renderer.unclippedGeometry;
    for (int frame = 0; frame < 120; ++frame) {
        assertDockDraws();
        assert(renderer.renderer.unclippedGeometry == firstUnclippedGeometry);
    }
    ui.openModal("map");
    ui.render();
    assert(ui.modalOpen() && renderer.renderer.unclippedGeometry > 10);
    ui.closeModal();
    for (int frame = 0; frame < 30; ++frame) assertDockDraws();
    // This proves persistent RmlUi submissions/clip validity, not actual GL
    // pixels. A WebGL cache/context failure still requires live browser QA.
    ui.shutdown();
}
void dockMissionRoutePass(int width, int height)
{
    Preferences preferences;
    Host host;
    host.viewport = {width, height, width, height, 1.0F};
    Bridge bridge;
    RenderHost renderer;
    rocket::GameRmlUi ui(preferences, host, bridge, renderer, assetRoot());
    std::string action;
    assert(ui.initialize([&](const std::string& value) { action = value; }));
    ui.setControllerPresentation(true, rocket::ControllerFamily::PlayStation);
    ui.setControllerFocusVisible(true);
    const auto catalog = rocket::createDefaultContent();
    auto state = std::make_unique<rocket::GameState>(rocket::createNewGame(catalog, 0xD0CCULL));
    assert(rocket::initializeLiveExpedition(*state, catalog));
    assert(rocket::performScenarioAction(*state, catalog,
        rocket::content::scenario::lunarProspector, "briefing",
        rocket::ScenarioActionKind::AcknowledgeBriefing).applied);
    assert(rocket::recordScenarioEvent(*state, catalog,
        {rocket::ScenarioEventKind::SafeMaterialDelivered,
         rocket::content::scenario::lunarProspector, "delivery", "moon", "common", 20, 0}));
    assert(rocket::recordScenarioEvent(*state, catalog,
        {rocket::ScenarioEventKind::ProtectedObjectiveExtracted,
         rocket::content::scenario::lunarProspector, "anomaly", "moon",
         rocket::content::miningSite::lunarAnomalyCrevice, 1, 0}));
    for (auto& battery : state->run.expedition.batteries)
        if (battery.id == "moon") battery.owner = rocket::BatteryOwner::Ship;
    rocket::reconcileArtifactCustody(*state, catalog);
    rocket::bankMissionArtifacts(*state, catalog);
    state->run.expedition.progression.pendingRunUpgradeChoices = 0;
    state->run.expedition.progression.runUpgradeOfferPending = false;
    state->screen = rocket::Screen::Hangar;
    state->incomingMessages = {};
    auto launch = rocket::expeditionFlightModel(*state, catalog);
    rocket::PanelRenderContext context {*state, catalog, launch, launch};
    context.firstTimeIntroductionsEnabled = false;
    const auto ready = rocket::buildGamePanelPresentation(context);
    const auto notice = std::find_if(ready.modals.begin(), ready.modals.end(), [](const auto& modal) {
        return modal.id == "solar_mission_claim";
    });
    assert(notice != ready.modals.end());
    ui.setPanelPresentation(ready);
    ui.render();
    auto* claim = document()->QuerySelector(".dock-mission-claim");
    auto* route = document()->QuerySelector(".expedition-dock-route-actions");
    auto* depart = route ? route->QuerySelector(".dock-depart") : nullptr;
    auto* banner = document()->GetElementById("rr-incoming-banner");
    assert(claim && depart && route->IsClassSet("is-secondary"));
    assert(ui.focusedId() == "action:" + notice->closeAction);
    assert(!depart->HasAttribute("data-ui-default-focus"));
    const auto claimPosition = claim->GetAbsoluteOffset(Rml::BoxArea::Border);
    const auto claimSize = claim->GetBox().GetSize(Rml::BoxArea::Border);
    const auto departPosition = depart->GetAbsoluteOffset(Rml::BoxArea::Border);
    const auto departSize = depart->GetBox().GetSize(Rml::BoxArea::Border);
    assert(departPosition.y - (claimPosition.y + claimSize.y) >= 20);
    assert(claimSize.y >= 56 && departSize.y < claimSize.y);
    assert(claim->GetProperty<float>("font-size") > depart->GetProperty<float>("font-size"));
    assert(banner && banner->GetInnerRML().find("Claim Mining Drone") != std::string::npos);
    assert(!banner->QuerySelector(".modal-actions")->IsVisible(true));
    assert(!ui.modalOpen());
    assert(ui.activateFocused() && action == notice->closeAction);
    // Navigation remains reachable, with the existing bright focus chip.
    ui.requestFocus("action:expedition:depart");
    soleFocus(ui);
    ui.requestFocus("action:expedition:map");
    assert(ui.activateFocused() && action == "expedition:map");
    // Classic messages retain their original explicit completion control.
    preferences.value.incomingNoticesAsModals = true;
    ui.setPanelPresentation(ready);
    ui.render();
    assert(ui.modalOpen());
    auto* modal = document()->GetElementById("rr-modal");
    assert(modal && modal->QuerySelector(".modal-actions button")->IsVisible(true));
    preferences.value.incomingNoticesAsModals = false;
    ui.setPanelPresentation(ready);
    ui.render();
    // Completing the mission returns departure to primary status. Nothing
    // accepts a mission or grants rewards merely by showing this layout.
    assert(rocket::performScenarioAction(*state, catalog,
        rocket::content::scenario::lunarProspector, "anomaly",
        rocket::ScenarioActionKind::ClaimReward).applied);
    ui.setPanelPresentation(rocket::buildGamePanelPresentation(context));
    ui.render();
    route = document()->QuerySelector(".expedition-dock-route-actions");
    assert(route && !route->IsClassSet("is-secondary"));
    assert(document()->QuerySelector(".dock-mission-claim") == nullptr);
    depart = route->QuerySelector(".dock-depart");
    assert(depart->HasAttribute("data-ui-default-focus"));
    assert(depart->GetBox().GetSize(Rml::BoxArea::Border).y >= 56);
    ui.shutdown();
}

void beaconSidebarPass(int width, int height)
{
    Preferences preferences;
    Host host;
    host.viewport = {width, height, width, height, 1.0F};
    Bridge bridge;
    RenderHost renderer;
    rocket::GameRmlUi ui(preferences, host, bridge, renderer, assetRoot());
    std::string action;
    assert(ui.initialize([&](const std::string& value) { action = value; }));
    ui.setControllerPresentation(true, rocket::ControllerFamily::Xbox);
    const auto catalog = rocket::createDefaultContent();
    auto state = std::make_unique<rocket::GameState>(rocket::createNewGame(catalog, 77));
    assert(rocket::initializeLiveExpedition(*state, catalog));
    state->screen = rocket::Screen::Hangar;
    state->meta.straylightStage = rocket::StraylightStage::RetrieveBeacons;
    state->incomingMessages = {};
    auto& expedition = state->run.expedition;
    for (auto& beacon : expedition.batteries) beacon.owner = rocket::BatteryOwner::EarthStorage;
    const auto launch = rocket::expeditionFlightModel(*state, catalog);
    rocket::PanelRenderContext context {*state, catalog, launch, launch};
    context.firstTimeIntroductionsEnabled = false;
    context.incomingMessageDeliveryAllowed = false;
    const auto presentation = rocket::buildGamePanelPresentation(context);
    assert(presentation.contentMarkup.find("BEACON RECOVERY") == std::string::npos);
    assert(presentation.missionSidebarMarkup.find("expedition:straylight:collect") != std::string::npos);
    ui.setPanelPresentation(presentation);
    ui.render();
    auto* panel = document()->GetElementById("rr-panel");
    auto* depart = panel->QuerySelector("button[data-rr-action=\"expedition:depart\"]");
    assert(depart);
    auto dockPosition = depart->GetAbsoluteOffset();
    const auto checkRail = [&] {
        auto* recovery = document()->GetElementById("rr-beacon-recovery");
        auto* dock = panel->QuerySelector(".expedition-home");
        assert(recovery && dock && recovery->IsVisible(true));
        const auto position = recovery->GetAbsoluteOffset(Rml::BoxArea::Border);
        const auto size = recovery->GetBox().GetSize(Rml::BoxArea::Border);
        const auto dockLeft = dock->GetAbsoluteOffset(Rml::BoxArea::Border).x;
        assert(position.x >= 0 && position.y >= 0 && position.y + size.y <= height);
        assert(position.x + size.x + 8 <= dockLeft);
        Rml::ElementList rows;
        recovery->GetElementsByClassName(rows, "beacon-row");
        assert(rows.size() == 6);
        for (auto* row : rows) {
            const auto rowPosition = row->GetAbsoluteOffset(Rml::BoxArea::Border);
            const auto rowSize = row->GetBox().GetSize(Rml::BoxArea::Border);
            if (rowPosition.y < position.y || rowPosition.y + rowSize.y > position.y + size.y)
                std::cerr << "Recovery card " << position.y << " + " << size.y << "; row " << rowPosition.y << " + " << rowSize.y << '\n';
            assert(rowPosition.y >= position.y && rowPosition.y + rowSize.y <= position.y + size.y);
        }
        assert(panel->QuerySelector("button[data-rr-action=\"expedition:depart\"]")->GetAbsoluteOffset() == dockPosition);
        return recovery;
    };
    auto* recovery = checkRail();
    assertActionLabelFits(recovery, "expedition:straylight:collect", "Collect beacons");
    ui.requestFocus("action:expedition:straylight:collect");
    assert(ui.activateFocused() && action == "expedition:straylight:collect");
    action.clear();
    ui.setControllerPresentation(false, rocket::ControllerFamily::Xbox);
    ui.render();
    auto* collect = document()->GetElementById("rr-beacon-recovery")->QuerySelector("button[data-rr-action]");
    const auto click = collect->GetAbsoluteOffset(Rml::BoxArea::Border);
    assert(ui.hitTest(static_cast<int>(click.x + 12), static_cast<int>(click.y + 12)));
    ui.mouseMove(static_cast<int>(click.x + 12), static_cast<int>(click.y + 12));
    ui.mouseDown(static_cast<int>(click.x + 12), static_cast<int>(click.y + 12), 0);
    ui.mouseUp(static_cast<int>(click.x + 12), static_cast<int>(click.y + 12), 0);
    ui.render();
    assert(action == "expedition:straylight:collect");
    for (const auto& beacon : expedition.batteries) assert(beacon.owner == rocket::BatteryOwner::EarthStorage);
    // Toggling the mission never moves dock controls or hides recovery actions.
    ui.dispatchAction("ui:toggle_mission_tracker");
    checkRail();
    ui.dispatchAction("ui:toggle_mission_tracker");
    checkRail();
    for (auto& beacon : expedition.batteries) beacon.owner = rocket::BatteryOwner::Ship;
    ui.setPanelPresentation(rocket::buildGamePanelPresentation(context));
    ui.render();
    dockPosition = panel->QuerySelector("button[data-rr-action=\"expedition:depart\"]")->GetAbsoluteOffset();
    recovery = checkRail();
    assert(!recovery->QuerySelector("button[data-rr-action=\"expedition:straylight:collect\"]"));
    assert(recovery->GetInnerRML().find("Aboard ship") != std::string::npos);
    auto withoutSidebar = rocket::buildGamePanelPresentation(context);
    withoutSidebar.missionSidebarMarkup.clear();
    ui.setPanelPresentation(withoutSidebar);
    ui.render();
    assert(!document()->GetElementById("rr-beacon-recovery"));
    assert(panel->QuerySelector("button[data-rr-action=\"expedition:depart\"]")->GetAbsoluteOffset() == dockPosition);
    ui.setPanelPresentation(rocket::buildGamePanelPresentation(context));
    ui.render();
    assert(document()->GetElementById("rr-beacon-recovery"));
    ui.openModal("map");
    ui.render();
    assert(ui.modalOpen() && !document()->GetElementById("rr-beacon-recovery"));
    ui.shutdown();
}

void landedShipServicesPass(int width, int height)
{
    Preferences preferences;
    Host host;
    host.viewport = {width, height, width, height, 1.0F};
    Bridge bridge;
    RenderHost renderer;
    rocket::GameRmlUi ui(preferences, host, bridge, renderer, assetRoot());
    std::string action;
    assert(ui.initialize([&](const std::string& value) { action = value; }));
    ui.setControllerPresentation(true, rocket::ControllerFamily::Xbox);
    ui.setControllerFocusVisible(true);
    const auto catalog = rocket::createDefaultContent();
    auto state = std::make_unique<rocket::GameState>(rocket::createNewGame(catalog, 11));
    state->screen = rocket::Screen::Flight;
    rocket::Random rng(11);
    const auto launch = rocket::prepareLaunch(*state, catalog, rng);
    rocket::FlightRunState flight;
    flight.physicalFlight = true;
    flight.mode = rocket::FlightMode::Landing;
    flight.landing.siteBound = true;
    rocket::PanelRenderContext context {*state, catalog, launch, launch};
    context.launchFlight = &flight;
    context.flightArmed = true;
    context.surfaceArrivalActive = true;
    context.surfaceArrivalPhase = 3;
    context.surfaceArrivalLandingCommitted = true;
    context.firstTimeIntroductionsEnabled = false;
    context.incomingMessageDeliveryAllowed = false;
    rocket::SceneInteractionAnchors anchors;
    anchors.ship = {true, width * .65F, height * .50F};
    anchors.shipRadiusX = anchors.shipRadiusY = 160;
    ui.setInteractionAnchors(anchors);
    const auto show = [&] {
        auto presentation = rocket::buildGamePanelPresentation(context);
        assert(presentation.contentMarkup.find("surface-flight-actions") == std::string::npos);
        assert(presentation.contentMarkup.find("deploy_surface_team") == std::string::npos);
        assert(presentation.contentMarkup.find("depart_surface_undeployed") == std::string::npos);
        ui.setPanelPresentation(presentation);
        ui.render();
        return presentation;
    };
    show();
    auto* services = document()->GetElementById("rr-ship-services");
    assert(services && services->IsVisible(true));
    const auto position = services->GetAbsoluteOffset(Rml::BoxArea::Border);
    const auto size = services->GetBox().GetSize(Rml::BoxArea::Border);
    assert(size.x <= 258 && size.y < 150);
    assert(position.x > anchors.ship.x && position.y + size.y < anchors.ship.y);
    assert(position.x + size.x <= width && position.y >= 0);
    assert(ui.focusedId() == "interaction:deploy_surface_team");
    assert(ui.activateFocused() && action == "deploy_surface_team");
    auto* takeoff = services->QuerySelector("button[data-rr-action=\"depart_surface_undeployed\"]");
    assert(takeoff && takeoff->GetInnerRML().find("Hold B") != std::string::npos);
    ui.setControllerPresentation(true, rocket::ControllerFamily::PlayStation);
    assert(document()->GetElementById("rr-ship-services")->GetInnerRML().find("Hold ○") != std::string::npos);
    ui.setControllerConfirmCancelSwapped(true);
    services = document()->GetElementById("rr-ship-services");
    assert(services->GetInnerRML().find("Hold ×") != std::string::npos);
    assert(services->QuerySelector("button[data-rr-action=\"deploy_surface_team\"]")
        ->QuerySelector(".controller-key")->GetInnerRML() == "○");
    ui.setControllerPresentation(true, rocket::ControllerFamily::SteamDeck);
    ui.setControllerConfirmCancelSwapped(false);
    services = document()->GetElementById("rr-ship-services");
    assert(services->GetInnerRML().find("Hold B") != std::string::npos);
    ui.setControllerPresentation(false, rocket::ControllerFamily::SteamDeck);
    ui.render();
    services = document()->GetElementById("rr-ship-services");
    assert(services->QuerySelector(".keyboard-key")->GetInnerRML() == "Space");
    takeoff = services->QuerySelector("button[data-rr-action=\"depart_surface_undeployed\"]");
    const auto click = takeoff->GetAbsoluteOffset(Rml::BoxArea::Border);
    ui.mouseMove(static_cast<int>(click.x + 12), static_cast<int>(click.y + 12));
    ui.mouseDown(static_cast<int>(click.x + 12), static_cast<int>(click.y + 12), 0);
    ui.mouseUp(static_cast<int>(click.x + 12), static_cast<int>(click.y + 12), 0);
    ui.render();
    assert(action == "depart_surface_undeployed");
    // The same menu handles a failed deployment site, the touchdown queue,
    // reload, and subsequent deployment/ascent transitions.
    context.surfaceArrivalLandingCommitted = false;
    show();
    assert(ui.focusedId() == "interaction:depart_surface_undeployed");
    services = document()->GetElementById("rr-ship-services");
    assert(!services->QuerySelector("button[data-rr-action=\"deploy_surface_team\"]"));
    context.surfaceArrivalLandingCommitted = true;
    context.surfaceArrivalPhase = 2;
    context.surfaceArrivalDeployQueued = true;
    show();
    services = document()->GetElementById("rr-ship-services");
    assert(services->GetInnerRML().find("Deployment queued") != std::string::npos);
    assert(!services->QuerySelector("button[data-rr-action=\"depart_surface_undeployed\"]"));
    for (int phase : {4, 5}) {
        context.surfaceArrivalPhase = phase;
        assert(show().interactionMarkup.empty());
        assert(!document()->GetElementById("rr-ship-services"));
    }
    context.surfaceArrivalPhase = 3;
    context.titleScreenActive = true;
    assert(rocket::buildGamePanelPresentation(context).interactionMarkup.empty());
    ui.shutdown();
}

void contextualOverlayPass(int width, int height)
{
    Preferences preferences;
    Host host;
    host.viewport = {width, height, width, height, 1.0F};
    Bridge bridge;
    RenderHost renderer;
    rocket::GameRmlUi ui(preferences, host, bridge, renderer, assetRoot());
    std::string action;
    assert(ui.initialize([&](const std::string& value) { action = value; }));
    ui.setControllerPresentation(true, rocket::ControllerFamily::Xbox);
    auto presentation = panel(button("base"));
    presentation.metadata.screen = rocket::Screen::Mining;
    presentation.metadata.surface = rocket::PanelSurfaceKind::Mining;
    presentation.interactionMarkup =
        "<div id=\"rr-context-interaction\" class=\"context-interaction is-mining is-ready\">"
        "<button class=\"interaction-action\" data-rr-action=\"mining_tether\" "
        "data-ui-focus-id=\"interaction:mining_tether\"><span class=\"interaction-key controller-key\">"
        "{{controller_north}} {{controller_lb}}</span><span class=\"interaction-label\">Tether artifact</span></button></div>";
    ui.setPanelPresentation(presentation);
    rocket::SceneInteractionAnchors anchors;
    anchors.target = {true, static_cast<float>(width - 12), static_cast<float>(height - 24)};
    ui.setInteractionAnchors(anchors);
    ui.render();
    auto* prompt = document()->GetElementById("rr-context-interaction");
    assert(prompt && prompt->IsVisible(true));
    const auto position = prompt->GetAbsoluteOffset(Rml::BoxArea::Border);
    const auto size = prompt->GetBox().GetSize(Rml::BoxArea::Border);
    assert(position.x >= 0 && position.y >= 0 && position.x + size.x <= width && position.y + size.y <= height);
    assert(size.x <= 184 && size.y <= 38);
    assert(position.x + size.x < anchors.target.x);
    assert(prompt->GetInnerRML().find("Y") != std::string::npos);
    ui.setControllerPresentation(true, rocket::ControllerFamily::PlayStation);
    prompt = document()->GetElementById("rr-context-interaction");
    assert(prompt && prompt->GetInnerRML().find("△ L1") != std::string::npos);
    ui.setControllerPresentation(true, rocket::ControllerFamily::SteamDeck);
    prompt = document()->GetElementById("rr-context-interaction");
    assert(prompt && prompt->GetInnerRML().find("Y L1") != std::string::npos);
    ui.render();
    const auto click = prompt->QuerySelector("button")->GetAbsoluteOffset(Rml::BoxArea::Border);
    ui.mouseMove(static_cast<int>(click.x + 12), static_cast<int>(click.y + 12));
    ui.mouseDown(static_cast<int>(click.x + 12), static_cast<int>(click.y + 12), 0);
    ui.mouseUp(static_cast<int>(click.x + 12), static_cast<int>(click.y + 12), 0);
    ui.render();
    assert(action == "mining_tether");
    anchors.target = {};
    ui.setInteractionAnchors(anchors);
    ui.render();
    assert(!prompt->IsVisible(true));
    anchors.target = {true, static_cast<float>(width / 2), static_cast<float>(height / 2), 48, 48};
    anchors.player = {true, anchors.target.x - 70, anchors.target.y - 10, 36, 36};
    ui.setInteractionAnchors(anchors);
    ui.render();
    prompt = document()->GetElementById("rr-context-interaction");
    const auto centerPosition = prompt->GetAbsoluteOffset(Rml::BoxArea::Border);
    assert(centerPosition.x >= anchors.target.x + 30);
    const auto assertClearOfAction = [&] {
        const auto p = prompt->GetAbsoluteOffset(Rml::BoxArea::Border);
        const auto s = prompt->GetBox().GetSize(Rml::BoxArea::Border);
        const auto scene = rocket::resolveUiViewportLayout(width, height, rocket::UiSurfaceKind::Mining).sceneRect;
        assert(p.x >= scene.x && p.y >= scene.y);
        assert(p.x + s.x <= scene.x + scene.width && p.y + s.y <= scene.y + scene.height);
        for (const auto actor : {anchors.player, anchors.target}) {
            assert(p.x + s.x <= actor.x - actor.halfWidth || p.x >= actor.x + actor.halfWidth
                || p.y + s.y <= actor.y - actor.halfHeight || p.y >= actor.y + actor.halfHeight);
        }
    };
    assertClearOfAction();
    // EVA crosses the tethered rig: slide to the other side without passing
    // through either sprite or the tether between them.
    anchors.player.x = anchors.target.x + 70;
    ui.setInteractionAnchors(anchors);
    host.now += 1.0 / 60.0;
    ui.render();
    const auto slidingPrompt = prompt->GetAbsoluteOffset(Rml::BoxArea::Border);
    assert(slidingPrompt.x < centerPosition.x && slidingPrompt.x + size.x > anchors.target.x);
    assertClearOfAction();
    for (int frame = 0; frame < 30; ++frame) {
        host.now += 1.0 / 60.0;
        ui.render();
        assertClearOfAction();
    }
    assert(prompt->GetAbsoluteOffset(Rml::BoxArea::Border).x + size.x < anchors.target.x);
    anchors.player.x = anchors.target.x - 3;
    ui.setInteractionAnchors(anchors);
    host.now += 0.05;
    ui.render();
    assert(prompt->GetAbsoluteOffset(Rml::BoxArea::Border).x + size.x < anchors.target.x);
    assertClearOfAction();
    // Clamping or rapid camera movement must not put the helper on top of the
    // actor. When there is no room above, place it below the work area.
    const auto scene = rocket::resolveUiViewportLayout(width, height, rocket::UiSurfaceKind::Mining).sceneRect;
    anchors.target = {true, static_cast<float>(scene.x + 50), static_cast<float>(scene.y + 50), 48, 48};
    anchors.player = {true, anchors.target.x + 40, anchors.target.y, 36, 36};
    ui.setInteractionAnchors(anchors);
    host.now += 1.0 / 60.0;
    ui.render();
    assertClearOfAction();
    assert(prompt->GetAbsoluteOffset(Rml::BoxArea::Border).y >= anchors.target.y + anchors.target.halfHeight);
    // Stacked player and payload positions during towing remain unobstructed.
    anchors.player.x = anchors.target.x;
    anchors.player.y = anchors.target.y;
    ui.setInteractionAnchors(anchors);
    host.now += 1.0 / 60.0;
    ui.render();
    assertClearOfAction();
    presentation.interactionMarkup =
        "<section id=\"rr-ship-services\" class=\"context-ship-services\">"
        "<strong>SHIP SERVICES</strong><div class=\"context-ship-actions\">"
        "<button class=\"interaction-action\" data-rr-action=\"drone_ops\">Drone Ops</button>"
        "<button class=\"interaction-action is-depart\" "
        "data-rr-action=\"mining_depart\" data-ui-focus-id=\"interaction:mining_depart\">"
        "Depart planet</button></div></section>";
    ui.setPanelPresentation(presentation);
    anchors.target = {};
    anchors.ship = {true, static_cast<float>(width - 12), static_cast<float>(height - 12)};
    ui.setInteractionAnchors(anchors);
    ui.render();
    auto* services = document()->GetElementById("rr-ship-services");
    assert(services && services->IsVisible(true));
    const auto shipPosition = services->GetAbsoluteOffset(Rml::BoxArea::Border);
    const auto shipSize = services->GetBox().GetSize(Rml::BoxArea::Border);
    assert(shipPosition.x >= 0 && shipPosition.y >= 0
        && shipPosition.x + shipSize.x <= width && shipPosition.y + shipSize.y <= height);
    anchors.ship = {};
    ui.setInteractionAnchors(anchors);
    ui.render();
    anchors.ship = {true, width * 0.5F, height * 0.35F};
    anchors.player = {true, anchors.ship.x - 70, anchors.ship.y + 40};
    anchors.shipRadiusX = anchors.shipRadiusY = 120;
    ui.setInteractionAnchors(anchors);
    ui.render();
    services = document()->GetElementById("rr-ship-services");
    const auto rightPosition = services->GetAbsoluteOffset(Rml::BoxArea::Border);
    const auto compactSize = services->GetBox().GetSize(Rml::BoxArea::Border);
    assert(rightPosition.x > anchors.ship.x);
    assert(rightPosition.y + compactSize.y < anchors.ship.y);
    // Crossing the ship slides the list rather than snapping. Small movements
    // directly beneath its center do not repeatedly swap sides.
    anchors.player.x = anchors.ship.x + 70;
    ui.setInteractionAnchors(anchors);
    host.now += 1.0 / 60.0;
    ui.render();
    const auto slidingPosition = services->GetAbsoluteOffset(Rml::BoxArea::Border);
    assert(slidingPosition.x < rightPosition.x && slidingPosition.x + compactSize.x > anchors.ship.x);
    for (int frame = 0; frame < 30; ++frame) {
        host.now += 1.0 / 60.0;
        ui.render();
    }
    const auto leftPosition = services->GetAbsoluteOffset(Rml::BoxArea::Border);
    assert(leftPosition.x + compactSize.x < anchors.ship.x);
    assert(leftPosition.y + compactSize.y < anchors.ship.y);
    anchors.player.x = anchors.ship.x - 3;
    ui.setInteractionAnchors(anchors);
    host.now += 0.05;
    ui.render();
    assert(services->GetAbsoluteOffset(Rml::BoxArea::Border).x + compactSize.x < anchors.ship.x);
    action.clear();
    const auto departClick = services->QuerySelector("button[data-rr-action=\"mining_depart\"]")->GetAbsoluteOffset(Rml::BoxArea::Border);
    ui.mouseMove(static_cast<int>(departClick.x + 12), static_cast<int>(departClick.y + 12));
    ui.mouseDown(static_cast<int>(departClick.x + 12), static_cast<int>(departClick.y + 12), 0);
    ui.mouseUp(static_cast<int>(departClick.x + 12), static_cast<int>(departClick.y + 12), 0);
    ui.render();
    assert(action == "mining_depart");
    ui.setInteractionAnchors({});
    ui.render();
    assert(!document()->GetElementById("rr-ship-services")->IsVisible(true));
    ui.shutdown();
}

} // namespace

void pauseSettingsPass(int width, int height)
{
    Preferences preferences;
    Host host;
    host.viewport = {width, height, width, height, 1.0F};
    Bridge bridge;
    RenderHost renderer;
    rocket::GameRmlUi ui(preferences, host, bridge, renderer, assetRoot());
    assert(ui.initialize([](const std::string&) {}));
    ui.setControllerPresentation(true, rocket::ControllerFamily::Xbox);
    ui.setControllerFocusVisible(true);
    const auto catalog = rocket::createDefaultContent();
    auto state = std::make_unique<rocket::GameState>(rocket::createNewGame(catalog, 0xA117ULL));
    state->screen = rocket::Screen::Hangar;
    rocket::Random rng(0xA117ULL);
    const auto launch = rocket::prepareLaunch(*state, catalog, rng);
    rocket::PanelRenderContext context {*state, catalog, launch, launch};
    ui.setPanelPresentation(rocket::buildGamePanelPresentation(context));
    ui.openModal("system_menu");
    ui.render();
    auto* modal = document()->GetElementById("rr-modal");
    assert(modal && modal->IsClassSet("modal-system_menu"));
    const auto pauseSize = modal->GetBox().GetSize(Rml::BoxArea::Border);
    const auto pausePosition = modal->GetAbsoluteOffset(Rml::BoxArea::Border);
    assert(pauseSize.x <= 420 && pauseSize.y <= 250);
    assert(pausePosition.x >= 0 && pausePosition.y >= 0);
    assert(pausePosition.x + pauseSize.x <= width && pausePosition.y + pauseSize.y <= height);
    assert(ui.focusedId() == "system:resume");
    assert(reachable(ui, "system:resume") == std::set<std::string>({
        "system:resume", "modal:controls", "modal:settings", "modal:map", "modal:inventory"}));

    ui.requestFocus("modal:settings");
    assert(ui.activateFocused());
    ui.render();
    assert(ui.focusedId() == "settings-tab:display");
    auto* display = document()->QuerySelector("[data-settings-page=display]");
    auto* controls = document()->QuerySelector("[data-settings-page=controls]");
    assert(display && controls && display->IsVisible(true) && !controls->IsVisible(true));
    auto* footer = document()->QuerySelector(".settings-footer");
    modal = document()->GetElementById("rr-modal");
    assert(footer && modal);
    const auto footerPosition = footer->GetAbsoluteOffset(Rml::BoxArea::Border);
    const auto footerSize = footer->GetBox().GetSize(Rml::BoxArea::Border);
    const auto modalPosition = modal->GetAbsoluteOffset(Rml::BoxArea::Border);
    const auto modalSize = modal->GetBox().GetSize(Rml::BoxArea::Border);
    assert(footerPosition.y + footerSize.y <= modalPosition.y + modalSize.y);
    const auto displayReachable = reachable(ui, "settings-tab:display");
    for (const std::string_view id : {"setting:resolution", "setting:fullscreen", "setting:frame_limit",
             "modal:developer_options", "modal:reset_save_confirm"})
        assert(displayReachable.contains(std::string(id)));

    ui.requestFocus("settings-tab:controls");
    assert(ui.activateFocused());
    ui.render();
    assert(ui.focusedId() == "settings-tab:controls");
    display = document()->QuerySelector("[data-settings-page=display]");
    controls = document()->QuerySelector("[data-settings-page=controls]");
    assert(controls->IsVisible(true) && !display->IsVisible(true));
    const auto controlsReachable = reachable(ui, "settings-tab:controls");
    for (const std::string_view id : {"setting:keyboard_drill_mode", "setting:controller_prompt",
             "setting:controller_deadzone", "setting:controller_invert", "setting:controller_swap",
             "setting:controller_vibration", "modal:developer_options"})
        assert(controlsReachable.contains(std::string(id)));
    ui.requestFocus("setting:controller_deadzone");
    assert(ui.focusedId() == "setting:controller_deadzone");
    ui.requestFocus("modal:developer_options");
    assert(ui.activateFocused());
    ui.render();
    assert(document()->GetElementById("rr-modal")->IsClassSet("modal-developer_options"));
    assert(ui.cancel() && ui.focusedId() == "modal:developer_options");
    assert(ui.cancel() && ui.focusedId() == "modal:settings");
    ui.requestFocus("modal:map");
    assert(ui.activateFocused());
    assert(ui.cancel() && ui.focusedId() == "modal:map");
}

void incomingBannerPass(int width, int height)
{
    Preferences preferences;
    assert(!preferences.value.incomingNoticesAsModals);
    Host host;
    host.viewport = {width, height, width, height, 1.0F};
    Bridge bridge;
    RenderHost renderer;
    rocket::GameRmlUi ui(preferences, host, bridge, renderer, assetRoot());
    std::string action;
    assert(ui.initialize([&](const std::string& value) { action = value; }));
    auto presentation = panel("<p>Flight controls remain visible</p>");
    presentation.modals.push_back({"incoming_message", "INCOMING MESSAGE",
        "<section class=\"incoming-message modal-body\"><div class=\"incoming-message-layout\">"
        "<div class=\"incoming-message-portrait\"></div><div class=\"incoming-message-copy\">"
        "<h2>Mission Control</h2><h3>Mars mission complete</h3><p>Your second drone bay is online.</p>"
        "</div></div><div class=\"modal-actions action-row\">"
        "<button data-rr-action=\"ack_incoming_message:test\" data-ui-focus-id=\"action:ack_incoming_message:test\">Understood</button>"
        "</div></section>", "ack_incoming_message:test", true, false, false,
        rocket::ModalTone::Neutral, true});
    ui.setPanelPresentation(presentation);
    ui.render();
    assert(!ui.modalOpen());
    auto* banner = document()->GetElementById("rr-incoming-banner");
    assert(banner && banner->IsVisible());
    const auto position = banner->GetAbsoluteOffset(Rml::BoxArea::Border);
    const auto size = banner->GetBox().GetSize(Rml::BoxArea::Border);
    assert(size.x == 744 && size.y >= 125 && size.y <= 126);
    assert(std::abs(position.x + size.x * .5F - width * .5F) <= 2);
    assert(position.y > height * 0.6F && height - (position.y + size.y) <= 24);
    auto* acknowledgement = banner->QuerySelector("button[data-rr-action=\"ack_incoming_message:test\"]");
    assert(acknowledgement && acknowledgement->IsVisible());
    const auto buttonPosition = acknowledgement->GetAbsoluteOffset(Rml::BoxArea::Border);
    const auto buttonSize = acknowledgement->GetBox().GetSize(Rml::BoxArea::Border);
    assert(buttonSize.x > 40 && buttonSize.y > 20 && buttonPosition.y + buttonSize.y <= height);
    const auto assertNoMessageScroll = [&](Rml::Element* card) {
        auto* copy = card->QuerySelector(".incoming-message-copy");
        assert(copy && copy->GetScrollHeight() <= copy->GetClientHeight() + 1);
        const auto cardPosition = card->GetAbsoluteOffset(Rml::BoxArea::Border);
        const auto cardSize = card->GetBox().GetSize(Rml::BoxArea::Border);
        const auto copyPosition = copy->GetAbsoluteOffset(Rml::BoxArea::Border);
        const auto copySize = copy->GetBox().GetSize(Rml::BoxArea::Border);
        assert(cardPosition.y >= 0 && cardPosition.y + cardSize.y <= height);
        assert(copyPosition.y >= cardPosition.y && copyPosition.y + copySize.y <= cardPosition.y + cardSize.y);
    };
    assertNoMessageScroll(banner);
    ui.requestFocus("action:ack_incoming_message:test");
    assert(ui.activateFocused() && action == "ack_incoming_message:test");
    preferences.value.incomingNoticesAsModals = true;
    ui.setPanelPresentation(presentation);
    ui.render();
    assert(ui.modalOpen() && document()->GetElementById("rr-incoming-banner") == nullptr);
    assert(document()->GetElementById("rr-modal")->IsClassSet("modal-incoming_message"));
    preferences.value.incomingNoticesAsModals = false;
    const auto catalog = rocket::createDefaultContent();
    auto state = std::make_unique<rocket::GameState>(rocket::createNewGame(catalog, 0xA11CEULL));
    // Match the save-free debug preview's prelaunch state.
    state->screen = rocket::Screen::Hangar;
    assert(rocket::enqueueIncomingMessage(state->incomingMessages, catalog,
        {"preview.incoming", "earth_dock_intro", "services"}));
    const auto launch = rocket::expeditionFlightModel(*state, catalog);
    rocket::PanelRenderContext context {*state, catalog, launch, launch};
    const auto dock = rocket::buildGamePanelPresentation(context);
    const auto actualNotice = std::find_if(dock.modals.begin(), dock.modals.end(), [](const auto& modal) {
        return modal.id == "incoming_message";
    });
    assert(actualNotice != dock.modals.end() && actualNotice->bannerEligible);
    ui.setPanelPresentation(dock);
    ui.render();
    auto* dockBanner = document()->GetElementById("rr-incoming-banner");
    assert(!ui.modalOpen() && dockBanner);
    const auto dockPosition = dockBanner->GetAbsoluteOffset(Rml::BoxArea::Border);
    const auto dockSize = dockBanner->GetBox().GetSize(Rml::BoxArea::Border);
    assert(std::abs(dockPosition.x + dockSize.x * .5F - width * .5F) <= 2);
    assert(dockPosition.y >= 0 && height - (dockPosition.y + dockSize.y) <= 24);
    assertNoMessageScroll(dockBanner);

    // The fresh campaign's Moon briefing is delivered before reaching the
    // Moon. Local acceptance eligibility must not force the classic popup.
    *state = rocket::createNewGame(catalog, 0xA11CEULL);
    state->incomingMessages = {};
    state->screen = rocket::Screen::Flight;
    state->run.expedition.travelInitialized = state->run.expedition.active = true;
    state->run.expedition.location.bodyId = "earth";
    const auto* moon = rocket::solarMissionForBody(catalog, "moon");
    assert(moon && !rocket::solarMissionAcceptanceForBody(*state, catalog, "moon").available);
    assert(rocket::enqueueIncomingMessage(state->incomingMessages, catalog,
        {"test.moon.departure", moon->briefingMessageId, "default"}));
    const auto moonPanel = rocket::buildGamePanelPresentation(context);
    const auto moonNotice = std::find_if(moonPanel.modals.begin(), moonPanel.modals.end(), [](const auto& modal) {
        return modal.id == "incoming_message";
    });
    assert(moonNotice != moonPanel.modals.end() && moonNotice->bannerEligible);
    assert(moonNotice->bodyMarkup.find("Understood") != std::string::npos);
    preferences.value.incomingNoticesAsModals = true;
    ui.setPanelPresentation(moonPanel);
    ui.render();
    assert(ui.modalOpen());
    preferences.value.incomingNoticesAsModals = false;
    ui.setPanelPresentation(moonPanel);
    ui.render();
    auto* moonBanner = document()->GetElementById("rr-incoming-banner");
    assert(!ui.modalOpen() && moonBanner);
    const auto moonPosition = moonBanner->GetAbsoluteOffset(Rml::BoxArea::Border);
    const auto moonSize = moonBanner->GetBox().GetSize(Rml::BoxArea::Border);
    assert(std::abs(moonPosition.x + moonSize.x * .5F - width * .5F) <= 2);
    assert(height - (moonPosition.y + moonSize.y) <= 24);
    assertActionLabelFits(moonBanner, "ack_incoming_message:test.moon.departure", "Understood");
    assertNoMessageScroll(moonBanner);
    ui.requestFocus("action:ack_incoming_message:test.moon.departure");
    action.clear();
    assert(ui.activateFocused() && action == "ack_incoming_message:test.moon.departure");

    state->run.expedition.location.bodyId = "moon";
    assert(rocket::solarMissionAcceptanceForBody(*state, catalog, "moon").available);
    ui.setPanelPresentation(rocket::buildGamePanelPresentation(context));
    ui.render();
    moonBanner = document()->GetElementById("rr-incoming-banner");
    assert(!ui.modalOpen() && moonBanner);
    assertActionLabelFits(moonBanner, "ack_incoming_message:test.moon.departure", "Accept Contract");
    assertNoMessageScroll(moonBanner);

    // The authored Mars briefing includes the bedrock lesson from the reported
    // screenshot. Show the entire message, not merely a hidden scrollbar.
    state->incomingMessages = {};
    state->run.expedition.location.bodyId = "mars";
    state->meta.unlockKeys.push_back(rocket::content::unlock::routeMars);
    rocket::ensureScenarioInstances(*state, catalog);
    const auto* mars = rocket::solarMissionForBody(catalog, "mars");
    assert(mars && rocket::solarMissionAcceptanceForBody(*state, catalog, "mars").available);
    assert(rocket::enqueueIncomingMessage(state->incomingMessages, catalog,
        {"test.mars.briefing", mars->briefingMessageId, "default"}));
    ui.setPanelPresentation(rocket::buildGamePanelPresentation(context));
    ui.render();
    auto* marsBanner = document()->GetElementById("rr-incoming-banner");
    assert(marsBanner && marsBanner->GetInnerRML().find("bedrock shelf") != std::string::npos);
    assertNoMessageScroll(marsBanner);
    assertActionLabelFits(marsBanner, "ack_incoming_message:test.mars.briefing", "Accept Contract");

    state->incomingMessages = {};
    state->screen = rocket::Screen::Flight;
    state->run.expedition.travelInitialized = state->run.expedition.active = true;
    state->run.expedition.location = {"solar", "io", rocket::CoordinateFrame::Body,
        {}, {}, 0, ""};
    state->meta.unlockKeys.push_back(rocket::content::unlock::routeJupiter);
    rocket::ensureScenarioInstances(*state, catalog);
    const auto* io = rocket::solarMissionForBody(catalog, "io");
    assert(io && rocket::solarMissionAcceptanceForBody(*state, catalog, "io").available);
    assert(rocket::enqueueIncomingMessage(state->incomingMessages, catalog,
        {"test.io.briefing", io->briefingMessageId, "default"}));
    const auto ioPanel = rocket::buildGamePanelPresentation(context);
    const auto ioNotice = std::find_if(ioPanel.modals.begin(), ioPanel.modals.end(), [](const auto& modal) {
        return modal.id == "incoming_message";
    });
    assert(ioNotice != ioPanel.modals.end() && ioNotice->bannerEligible);
    preferences.value.incomingNoticesAsModals = true;
    ui.setPanelPresentation(ioPanel);
    ui.render();
    assert(ui.modalOpen() && document()->GetElementById("rr-incoming-banner") == nullptr);
    preferences.value.incomingNoticesAsModals = false;
    ui.setPanelPresentation(ioPanel);
    ui.render();
    auto* ioBanner = document()->GetElementById("rr-incoming-banner");
    assert(!ui.modalOpen() && ioBanner);
    const auto ioPosition = ioBanner->GetAbsoluteOffset(Rml::BoxArea::Border);
    const auto ioSize = ioBanner->GetBox().GetSize(Rml::BoxArea::Border);
    assert(std::abs(ioPosition.x + ioSize.x * .5F - width * .5F) <= 2);
    assert(height - (ioPosition.y + ioSize.y) <= 24);
    assertNoMessageScroll(ioBanner);
    auto* commission = ioBanner->QuerySelector("button[data-rr-action=\"ack_incoming_message:test.io.briefing\"]");
    assert(commission && commission->IsVisible());
    const auto commissionPosition = commission->GetAbsoluteOffset(Rml::BoxArea::Border);
    const auto commissionSize = commission->GetBox().GetSize(Rml::BoxArea::Border);
    assert(commissionPosition.x >= ioPosition.x &&
        commissionPosition.x + commissionSize.x <= ioPosition.x + ioSize.x &&
        commissionPosition.y + commissionSize.y <= ioPosition.y + ioSize.y);
    action.clear();
    ui.requestFocus("action:ack_incoming_message:test.io.briefing");
    assert(ui.activateFocused() && action == "ack_incoming_message:test.io.briefing");
    ui.shutdown();
}

int main(int argc, char** argv)
{
#if defined(_MSC_VER)
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    if (argc > 1 && std::string_view(argv[1]) == "--generated-panels-only") {
        generatedPanelPass(800, 600);
        generatedPanelPass(1280, 800);
        generatedPanelPass(1600, 900);
        std::cout << "Generated panel layout tests passed\n";
        return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "--incoming-banner-only") {
        incomingBannerPass(1280, 800);
        incomingBannerPass(1600, 900);
        std::cout << "Incoming banner layout tests passed\n";
        return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "--context-interactions-only") {
        landedShipServicesPass(1280, 800);
        landedShipServicesPass(1600, 900);
        contextualOverlayPass(1280, 800);
        contextualOverlayPass(1600, 900);
        std::cout << "Contextual interaction layout tests passed\n";
        return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "--dock-mission-route-only") {
        dockMissionRoutePass(1280, 800);
        dockMissionRoutePass(1600, 900);
        std::cout << "Dock mission route layout tests passed\n";
        return 0;
    }
    if (argc > 1 && std::string_view(argv[1]) == "--beacon-sidebar-only") {
        beaconSidebarPass(1280, 800);
        beaconSidebarPass(1600, 900);
        std::cout << "Beacon recovery sidebar tests passed\n";
        return 0;
    }
    recoveredDockKeepsSubmittingVisibleGeometry(800, 600, 1.0F);
    recoveredDockKeepsSubmittingVisibleGeometry(1422, 800, 1.125F);
    recoveredDockKeepsSubmittingVisibleGeometry(1600, 900, 1.0F);
    focusPass(800, 600);
    focusPass(1280, 800);
    focusPass(1600, 900);
    contextualOverlayPass(1280, 800);
    contextualOverlayPass(1600, 900);
    landedShipServicesPass(1280, 800);
    landedShipServicesPass(1600, 900);
    dockMissionRoutePass(1280, 800);
    dockMissionRoutePass(1600, 900);
    beaconSidebarPass(1280, 800);
    beaconSidebarPass(1600, 900);
    incomingBannerPass(1280, 800);
    incomingBannerPass(1600, 900);
    generatedPanelPass(800, 600);
    generatedPanelPass(1280, 800);
    generatedPanelPass(1600, 900);
    pauseSettingsPass(1280, 720);
    pauseSettingsPass(1280, 800);
    pauseSettingsPass(1920, 1080);
    std::cout << "Controller rendered focus tests passed\n";
}
