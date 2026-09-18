#include "game/GameRmlUi.h"
#include "game/GamePanel.h"
#include "game/IRmlRenderHost.h"
#include "core/ExpeditionSystem.h"
#include "core/MiningSystem.h"
#include "core/GameUi.h"
#include "core/ResearchSystem.h"
#include "core/ScenarioSystem.h"
#include "core/SolarProgression.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementUtilities.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/RenderInterface.h>
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <limits>
#include <memory>
#include <set>
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
    assert(focused->QuerySelector(".rr-controller-confirm-glyph")->GetInnerRML() == "A");
    assert(ui.activateFocused() && action == "scan");
    const auto originalSize = focused->GetBox().GetSize(Rml::BoxArea::Border);
    ui.setControllerConfirmCancelSwapped(true);
    ui.render();
    focused = soleFocus(ui);
    assert(focused->QuerySelector(".rr-controller-confirm-glyph")->GetInnerRML() == "B");
    assert(focused->GetBox().GetSize(Rml::BoxArea::Border) == originalSize);
    ui.setControllerPresentation(true, rocket::ControllerFamily::PlayStation);
    assert(soleFocus(ui)->QuerySelector(".rr-controller-confirm-glyph")->GetInnerRML() == "Circle");
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
    assert(soleFocus(ui)->QuerySelector(".rr-controller-confirm-glyph")->GetInnerRML() == "Hold Circle");
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
    assert(ioPanel.contentMarkup.find("data-hazard-support=\"uncommissioned\"") != std::string::npos);
    assert(ioPanel.contentMarkup.find("Commission Hazard Drone") != std::string::npos);
    assert(ioPanel.contentMarkup.find("data-rr-action=\"drone_ops\"") != std::string::npos);
    audit("Io mining persistent commission");
    assertActionLabelFits(document()->GetElementById("rr-panel"), commissionAction, "Commission Hazard Drone");
    auto* missionStrip = document()->QuerySelector(".mining-hazard-mission");
    auto* serviceDock = document()->QuerySelector(".mining-bottom-rail");
    assert(missionStrip && serviceDock);
    assert(missionStrip->GetAbsoluteOffset(Rml::BoxArea::Border).y + missionStrip->GetBox().GetSize(Rml::BoxArea::Border).y <=
        serviceDock->GetAbsoluteOffset(Rml::BoxArea::Border).y);
    state->screen = rocket::Screen::DroneOps;
    ui.setPanelPresentation(rocket::buildGamePanelPresentation(context));
    assert(ui.focusedId() == "action:" + commissionAction);
    auditRenderedGraph(ui, "Io Drone Ops commission @" + std::to_string(width));
    assert(rocket::acceptSolarMission(*state, catalog, *io).accepted);
    assert(rocket::equippedMiniDroneCount(*state, rocket::content::drone::hazardDrone) == 1);
    assert(!state->run.mining.miniDrones.empty());
    while (!state->incomingMessages.pending.empty())
        assert(rocket::acknowledgeIncomingMessage(state->incomingMessages, state->incomingMessages.pending.front().id));
    // Separately exercise an owned/unassigned frame while a loadout recall
    // is in progress; normal additive assignments no longer require recall.
    state->meta.equippedDroneIds.clear();
    state->run.mining.miniDrones.clear();
    state->run.mining.droneLoadoutRecallActive = true;
    ioPanel = rocket::buildGamePanelPresentation(context);
    assert(ioPanel.contentMarkup.find("data-hazard-support=\"unassigned\"") != std::string::npos);
    assert(ioPanel.contentMarkup.find("Commission Hazard Drone") == std::string::npos);
    audit("Io owned unassigned away from service");
    const auto assertAssignmentDisabled = [&] {
        Rml::ElementList actions;
        document()->GetElementById("rr-panel")->QuerySelectorAll(actions, "button[data-rr-action]");
        for (auto* action : actions) {
            const auto id = action->GetAttribute<Rml::String>("data-rr-action", "");
            if (id.starts_with("equip_drone:") || id.starts_with("unequip_drone_slot:")) assert(action->HasAttribute("disabled"));
        }
    };
    assertAssignmentDisabled();
    state->run.mining.droneX = state->run.mining.returnZoneX;
    rocket::MiningMiniDroneAgent hauling;
    hauling.haulMaterials.common = 3;
    state->run.mining.miniDrones.push_back(hauling);
    audit("Io service outstanding drones");
    assertAssignmentDisabled();
    assert(document()->GetElementById("rr-panel")->QuerySelector("button[data-rr-action=\"mining_wait_for_drones\"]"));
    ui.requestFocus("modal:surface");
    assert(ui.navigate(rocket::UiDirection::Down));
    assert(ui.focusedId() == "action:mining_wait_for_drones");
    assert(ui.navigate(rocket::UiDirection::Down));
    assert(soleFocus(ui)->Closest(".drone-controller-choice-row"));
    assert(ui.navigate(rocket::UiDirection::Up));
    assert(ui.focusedId() == "action:mining_wait_for_drones");
    assert(ui.navigate(rocket::UiDirection::Up));
    assert(soleFocus(ui)->Closest(".drone-workspace-actions"));
    auto& deployedWorker = state->run.mining.miniDrones.back();
    deployedWorker.haulMaterials = {};
    deployedWorker.x = state->run.mining.returnZoneX + 8;
    deployedWorker.y = state->run.mining.returnZoneY;
    deployedWorker.behavior = rocket::MiningMiniDroneBehavior::Working;
    assert(rocket::miningDroneRecoveryStatus(state->run.mining).outstandingDrones == 0);
    assert(rocket::miningDroneRecoveryStatus(state->run.mining, true).outstandingDrones > 0);
    audit("Io service empty deployed worker");
    assertAssignmentDisabled();
    assert(document()->GetElementById("rr-panel")->QuerySelector("button[data-rr-action=\"mining_wait_for_drones\"]"));
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
    const auto hazardAction = rocket::ui::actions::equipDrone(static_cast<int>(hazard - catalog.miniDrones.begin()));
    auto* assign = document()->GetElementById("rr-panel")->QuerySelector("button[data-rr-action=\"" + hazardAction + "\"]");
    assert(assign && !assign->HasAttribute("disabled"));
    state->meta.equippedDroneIds.push_back(rocket::content::drone::hazardDrone);
    state->screen = rocket::Screen::Mining;
    ioPanel = rocket::buildGamePanelPresentation(context);
    assert(ioPanel.contentMarkup.find("data-hazard-support=\"assigned\"") != std::string::npos);
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
} // namespace

int main()
{
#if defined(_MSC_VER)
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    recoveredDockKeepsSubmittingVisibleGeometry(800, 600, 1.0F);
    recoveredDockKeepsSubmittingVisibleGeometry(1422, 800, 1.125F);
    recoveredDockKeepsSubmittingVisibleGeometry(1600, 900, 1.0F);
    focusPass(800, 600);
    focusPass(1280, 800);
    focusPass(1600, 900);
    generatedPanelPass(800, 600);
    generatedPanelPass(1280, 800);
    generatedPanelPass(1600, 900);
    std::cout << "Controller rendered focus tests passed\n";
}
