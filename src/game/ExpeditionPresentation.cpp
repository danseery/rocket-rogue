#include "game/ExpeditionPresentation.h"
#include "core/ExpeditionSystem.h"
#include "core/GameUi.h"
#include "core/ResearchSystem.h"
#include "core/ScenarioSystem.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace rocket {
namespace {
std::string esc(std::string_view text) {
    std::string result;
    for (char c : text) switch (c) {
        case '&': result += "&amp;"; break; case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break; case '"': result += "&quot;"; break; default: result += c;
    }
    return result;
}
std::string num(double value) { std::ostringstream s; s << std::fixed << std::setprecision(1) << value; return s.str(); }
// Both Rml render hosts support positioned geometry; avoid depending on CSS
// transforms for navigation lines, which must remain accurate on either host.
void mapLine(std::ostream& out, std::string_view style, double ax, double ay, double bx, double by) {
    const int steps = std::max(1, static_cast<int>(std::ceil(std::hypot(bx-ax,by-ay)/2.0)));
    for (int i=0;i<=steps;++i) {
        const double x=ax+(bx-ax)*i/steps, y=ay+(by-ay)*i/steps;
        if (x<0 || x>704 || y<0 || y>350) continue;
        out << "<div class=\"" << style << "\" style=\"left:" << x << "dp;top:" << y << "dp;\"></div>";
    }
}
std::string button(std::string_view label, std::string_view id, bool enabled = true) {
    return "<button type=\"button\" data-rr-action=\"" + esc(id) + "\"" +
        (enabled ? " data-ui-focus-id=\"action:" + esc(id) + "\"" : " class=\"disabled\" disabled") + ">" + esc(label) + "</button>";
}
void replaceModal(PanelDocumentPresentation& panel, ModalPresentation value) {
    std::erase_if(panel.modals, [&](const auto& item) { return item.id == value.id; });
    panel.modals.push_back(std::move(value));
}
std::string benefit(const ShipModule& m) {
    if (m.surfaceDepthUpgradeKind == SurfaceDepthUpgradeKind::SurveyArray) return "Survey one additional terrain layer.";
    if (m.surfaceDepthUpgradeKind == SurfaceDepthUpgradeKind::BoreSystem) return "Drill one additional surveyed layer.";
    switch (m.launchUpgradeKind) {
    case LaunchUpgradeKind::FuelTanks: return num(launchFuelCapacityForRank(m.launchUpgradeRank)) + " ship fuel capacity.";
    case LaunchUpgradeKind::FlightControls: return "+10% physical thrust at the same burn rate.";
    case LaunchUpgradeKind::Cooling: return "Reduce powered heat and improve coast cooling.";
    case LaunchUpgradeKind::Hull: return num(tuning::launch::hullBaseIntegrity + m.launchUpgradeRank*tuning::launch::hullIntegrityPerRank) + " maximum hull integrity.";
    default: return "";
    }
}
}
void appendExpeditionPresentation(const PanelRenderContext& c, PanelDocumentPresentation& panel) {
    const auto& state = c.state;
    const auto& e = state.run.expedition;
    if (!e.travelInitialized || c.titleScreenActive) return;
    if (state.screen==Screen::Hangar && openingMissionRetryEligible(state) &&
        (e.decision.pendingId=="opening_retry" || (operationalHomeDocked(e) && !e.wrecks.empty()))) {
        panel.modals.clear();
        panel.contentMarkup.clear();
        // The saved retry decision owns this repeatable transmission. Its
        // explicit action retries the launch; the shared card only presents it.
        if (auto card=buildIncomingMessageCard(c,"opening_retry",openingRetryMessageVariant(state),"expedition:retry_opening"))
            panel.modals.push_back(std::move(*card));
        panel.templateKind=PanelTemplateKind::LegacyRaw;
        panel.metadata.legacyContentOwnsLaneGeometry=false;
        panel.runtime.responsiveViewport=true;
        return;
    }
    const auto& system = solarSystemDefinition();
    const auto& flight = state.run.flight;
    auto location = e.location;
    captureSystemLocation(location, flight);
    const auto absolute = convertSystemFrame(location, CoordinateFrame::System, "", system);
    const auto* target = systemBody(system, e.course.targetBodyId);
    const auto* region = systemBody(system, e.location.bodyId);
    const auto objective = scenarioObjectiveForDestination(state, c.catalog, expeditionEnvironment(state, c.catalog).id);
    const auto action = [&](std::string_view label, std::string_view suffix, bool enabled = true) { return button(label, "expedition:" + std::string(suffix), enabled); };
    std::ostringstream map;
    map << "<section class=\"expedition-map\"><p>" << (e.cruise.active ? "PAUSED / CRUISE WILL RESUME" : "PAUSED / MANUAL FLIGHT") << "</p><div class=\"solar-map-scroll\"><div class=\"solar-map\">";
    double minX = 0, maxX = 0, minY = 0, maxY = 0;
    for (const auto& b : system.bodies) {
        minX = std::min(minX,b.position.x); maxX = std::max(maxX,b.position.x);
        minY = std::min(minY,b.position.y); maxY = std::max(maxY,b.position.y);
    }
    const double mapScale = std::min(576.0/std::max(1.0,maxX-minX),222.0/std::max(1.0,maxY-minY));
    const auto x = [&](double v) { return 352.0+(v-(minX+maxX)*.5)*mapScale; };
    const auto y = [&](double v) { return 175.0-(v-(minY+maxY)*.5)*mapScale; };
    const auto diameter = [](const auto& b) { return std::clamp(systemBodyDisplayRadius(b)*64.0,16.0,64.0); };
    for (std::size_t i = 1; i < e.course.trajectory.size(); ++i) {
        const auto a = e.course.trajectory[i-1], b = e.course.trajectory[i];
        mapLine(map, "solar-course", x(a.x), y(a.y), x(b.x), y(b.y));
    }
    struct LabelBox { double x,y,w,h; };
    std::vector<LabelBox> labels;
    for (const auto& b : system.bodies) {
        const double size = diameter(b);
        labels.push_back({x(b.position.x)-size*.5,y(b.position.y)-size*.5,size,size});
    }
    for (const auto& b : system.bodies) {
        const bool hazard = std::find(e.course.intersectedHazards.begin(), e.course.intersectedHazards.end(), b.id) != e.course.intersectedHazards.end();
        const double width = 18 + b.name.size()*8.0;
        LabelBox label{x(b.position.x)-12, y(b.position.y)-30, width, 26};
        for (int attempt=0;attempt<32;++attempt) {
            const double angle = (attempt%8)*.7853981633974483;
            const double radius = 25+(attempt/8)*28;
            label.x = std::clamp(x(b.position.x)+std::cos(angle)*radius-width*.5, 0.0, 704.0-width);
            label.y = std::clamp(y(b.position.y)+std::sin(angle)*radius-13, 0.0, 324.0);
            if (std::none_of(labels.begin(),labels.end(),[&](const auto& other) {
                return label.x<other.x+other.w+8 && label.x+label.w+8>other.x && label.y<other.y+other.h+8 && label.y+label.h+8>other.y;
            })) break;
        }
        labels.push_back(label);
        mapLine(map, "solar-leader", x(b.position.x), y(b.position.y), label.x+label.w*.5, label.y+13);
        const double size = diameter(b);
        const std::string art = b.kind == SystemBodyKind::Station ? "straylight-ark-damaged"
            : b.kind == SystemBodyKind::Moon ? "moon" : b.id;
        map << "<button class=\"solar-planet" << (b.id == e.course.targetBodyId ? " solar-planet-selected" : "")
            << "\" data-rr-action=\"expedition:preview:" << esc(b.id)
            << "\" data-ui-focus-id=\"planet:" << esc(b.id) << "\" style=\"left:" << x(b.position.x)-size*.5
            << "dp;top:" << y(b.position.y)-size*.5 << "dp;width:" << size << "dp;height:" << size << "dp;\">";
        if (b.kind == SystemBodyKind::Star) map << "<div class=\"solar-sun\"></div>";
        else map << "<img src=\"planets/" << art << ".png\" />";
        map << "</button>";
        map << "<div class=\"solar-body" << (b.id == e.course.targetBodyId ? " solar-selected" : "") << (hazard ? " solar-hazard" : "")
            << "\" style=\"left:" << label.x << "dp;top:" << label.y << "dp;width:" << width << "dp;\">" << action(b.name, "preview:" + b.id) << "</div>";
        if (b.dock) {
            const auto dock = systemDockPosition(b);
            map << "<div class=\"solar-dock\" style=\"left:" << x(dock.x) << "dp;top:" << y(dock.y) << "dp;\">+</div>";
        }
    }
    for (const auto& w : e.wrecks) {
        const auto p = convertSystemFrame(w.location, CoordinateFrame::System, "", system);
        map << "<div class=\"solar-wreck\" style=\"left:" << x(p.position.x) << "dp;top:" << y(p.position.y) << "dp;\">W" << w.id << "</div>";
    }
    map << "<div class=\"solar-ship\" style=\"left:" << x(absolute.position.x) << "dp;top:" << y(absolute.position.y) << "dp;\"></div></div></div><p>Green marker: ship / +: dock / W: wreck</p>";
    if (target) map << "<h3>" << esc(target->name) << "</h3><p>" << (target->dock ? "Rendezvous and dock" : target->siteId.empty() ? "Orbital exploration" : "Surface expedition")
        << (e.course.estimateValid ? " / Approach: " + num(e.course.approachFuel) + " fuel / Return margin: " + num(e.course.returnMargin) : " / Fuel estimate unavailable: inspect approach hazards") << "</p><p class=\"expedition-warning\">" << esc(target->hazard)
        << "</p><p>Fuel estimate assumes burn-and-coast piloting plus " << num(e.course.manualCaptureAllowance) << " fuel per manual approach. Line: unattended cruise forecast. Cruise does not brake or avoid collisions.</p>" << action("Plot " + target->name, "plot:" + target->id);
    for (const auto& site : e.sites) {
        const auto* body = systemBody(system, site.bodyId);
        map << "<p>Visited site: " << esc(body ? body->name : "Remote site") << " / excavation retained</p>";
    }
    map << "<p>Home: Earth orbital dock / Ship fuel: " << num(flight.fuelRemaining) << " / " << num(flight.fuelCapacity) << "</p><p>Carried: " << e.cargo.materials.common << " common, " << e.cargo.materials.rare << " rare, " << e.cargo.materials.exotic << " exotic / " << num(e.cargo.credits) << " unbanked credits</p>";
    for (const auto& w : e.wrecks) map << "<p>Wreck " << w.id << " - " << action("Recover cargo", "recover:" + std::to_string(w.id), canSalvageWreck(e, flight, system, w.id)) << "</p>";
    map << action("Resume", "close");
    if (e.active) map << action("Abandon ship", "abandon");
    map << "</section>";
    replaceModal(panel, {"map", "SOLAR SYSTEM", map.str(), "expedition:close", false, true, true, ModalTone::Neutral});
    replaceModal(panel, {"expedition_abandon", "ABANDON SHIP", "<p>Return in a replacement ship. Temporary XP and build end; carried salvage stays at a recoverable wreck.</p><div class=\"action-row\">" + action("Keep flying", "close") + action("Abandon and recover", "confirm_abandon") + "</div>", "expedition:close", false, true, true, ModalTone::Warning});
    if (state.screen == Screen::Hangar) {
        for (auto& item : panel.modals)
            if (item.id != "incoming_message") item.autoOpen = false;
        std::ostringstream home;
        home << "<section class=\"expedition-home\"><h2>" << esc(region ? region->name : "Home") << " / ORBITAL DOCK</h2><p>" << esc(state.statusLine)
            << "</p><p>Ship fuel " << num(flight.fuelRemaining) << " / " << num(flight.fuelCapacity) << " / Hull " << num(flight.hullRemaining) << " / Credits " << num(state.run.credits)
            << "</p><div class=\"action-row\">" << action("Plot course", "map") << action("Depart dock", "depart");
        if (operationalHomeDocked(e) && droneBayUnlocked(state))
            home << button("Drone Ops", ui::actions::droneOps);
        home << "</div>";
        if (operationalHomeDocked(e)) {
            home << "<h3>SHIPYARD</h3><div class=\"expedition-shipyard\">";
            for (const auto& m : c.catalog.modules) {
                const bool launch = m.launchUpgradeKind != LaunchUpgradeKind::None;
                if (!launch && m.surfaceDepthUpgradeKind == SurfaceDepthUpgradeKind::None) continue;
                const int rank = launch ? m.launchUpgradeRank : m.surfaceDepthUpgradeRank;
                const int installed = launch ? launchUpgradeRank(state, m.launchUpgradeKind) : surfaceDepthUpgradeRank(state, m.surfaceDepthUpgradeKind);
                if (rank != installed + 1) continue;
                const bool available = launch ? canInstallLaunchUpgrade(state, c.catalog, m.launchUpgradeKind) : canInstallSurfaceDepthUpgrade(state, c.catalog, m.surfaceDepthUpgradeKind);
                home << "<div class=\"shipyard-track\"><h4>" << esc(m.name) << "</h4><p>" << benefit(m) << "</p><p>" << moduleOfferCost(m) << " credits"
                    << (rank > batteryResearchRank(e) ? (rank == 2 ? " / research: 2 distinct batteries" : " / research: 4 distinct batteries") : "") << "</p>" << action("Install", "install:" + m.id, available) << "</div>";
            }
            home << "</div>";
        }
        home << "</section>";
        panel.contentMarkup = home.str();
        panel.templateKind = PanelTemplateKind::LegacyRaw;
        panel.metadata.legacyContentOwnsLaneGeometry = false;
        panel.runtime.responsiveViewport = true;
    }
    if (state.screen == Screen::Flight && !c.surfaceArrivalActive) {
        panel.contentMarkup += "<div class=\"expedition-flight-bar\"><p>" + esc(region ? region->name : "Solar space") +
        " / Target: " + esc(target ? target->name : "None") + " / " + (e.cruise.active ? "CRUISE ACTIVE" : "MANUAL") + "</p><p>" + esc(objective.available ? objective.goal : "Explore, mine, and return to Earth") + "</p>" +
        action(e.cruise.active ? "Cruise off [C / L3]" : "Cruise [C / L3]", "cruise", flight.active && flight.mode != FlightMode::Landing && !e.undockReady) +
        action("Dock", "dock", canDockExpedition(e, flight, system));
        if (flight.active && flight.mode != FlightMode::Landing) {
            auto position = e.location;
            captureSystemLocation(position, flight);
            position = convertSystemFrame(position, CoordinateFrame::System, "", system);
            for (const auto& body : system.bodies) {
                if (!body.dock) continue;
                const auto dock = systemDockPosition(body);
                const double range = std::hypot(position.position.x-dock.x,position.position.y-dock.y);
                if (range > expeditionDockRadius) continue;
                panel.contentMarkup += canDockExpedition(e,flight,system)
                    ? "<p>In range — press Dock.</p>"
                    : (range > expeditionDockRadius ? "<p>Approach the DOCK marker, not Earth's surface.</p>"
                                   : "<p>Slow down to enable Dock.</p>");
                break;
            }
        }
        for (const auto& wreck : e.wrecks) {
            if (!canSalvageWreck(e, flight, system, wreck.id, false)) continue;
            const bool ready = canSalvageWreck(e, flight, system, wreck.id);
            panel.contentMarkup += action("Salvage wreck " + std::to_string(wreck.id),
                "recover:" + std::to_string(wreck.id), ready);
            if (!ready) panel.contentMarkup += "<p>Wreck in range. Slow down and match its drift to salvage.</p>";
        }
        panel.contentMarkup += "</div>";
    }
    const auto& d = e.decision;
    if (!d.pendingId.empty() && !d.awaitingAscent && state.screen == Screen::Flight && flight.mode != FlightMode::Landing && c.incomingMessageDeliveryAllowed && e.progression.pendingRunUpgradeChoices == 0 &&
        std::none_of(panel.modals.begin(), panel.modals.end(), [](const auto& item) { return item.autoOpen; }))
        replaceModal(panel, {"expedition_decision", "NEXT COURSE", "<p>Objective secured. Return to bank salvage, or continue with your current fuel and build.</p><div class=\"action-row\">" +
            action("Plot Earth", "decision:home:" + d.pendingId) + action("Plot " + recommendedExpeditionLead(state, c.catalog), "decision:lead:" + d.pendingId) + action("Inspect map", "decision:map:" + d.pendingId) + "</div>", "expedition:decision:map:" + d.pendingId, true, false, false, ModalTone::Neutral});
}
} // namespace rocket
