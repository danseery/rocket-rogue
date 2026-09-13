#include "game/ExpeditionPresentation.h"
#include "core/ExpeditionSystem.h"
#include "core/GameUi.h"
#include "core/MiningSystem.h"
#include "core/ResearchSystem.h"
#include "core/ScenarioSystem.h"
#include "core/SolarProgression.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <optional>
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
struct SolarMapPlacement {
    double x = 352, y = 175;
    double labelX = 320, labelY = 210, labelWidth = 64;
    double orbitX = 0, orbitY = 0;
};
std::optional<SolarMapPlacement> solarMapPlacement(std::string_view id) {
    if (id == "sun") return SolarMapPlacement{352,175,322,211,60};
    if (id == "mercury") return SolarMapPlacement{382,155,346,124,72,50,25};
    if (id == "venus") return SolarMapPlacement{282,160,247,181,70,78,39};
    if (id == "earth") return SolarMapPlacement{455,191,423,215,64,108,54};
    if (id == "moon") return SolarMapPlacement{485,177,472,146,58};
    if (id == "mars") return SolarMapPlacement{254,128,222,97,64,140,70};
    if (id == "jupiter") return SolarMapPlacement{502,129,465,88,74,176,88};
    if (id == "io") return SolarMapPlacement{538,146,528,165,44};
    if (id == "saturn") return SolarMapPlacement{166,125,129,84,74,212,106};
    if (id == "titan") return SolarMapPlacement{199,142,187,162,58};
    if (id == "uranus") return SolarMapPlacement{132,225,95,249,74,250,125};
    if (id == "titania") return SolarMapPlacement{165,239,151,260,68};
    if (id == "neptune") return SolarMapPlacement{596,247,555,270,82,288,144};
    if (id == "triton") return SolarMapPlacement{629,232,616,201,62};
    if (id == "straylight") return SolarMapPlacement{654,304,585,321,112};
    return std::nullopt;
}
double solarMapDiameter(const SystemBodyDefinition& body) {
    if (body.kind == SystemBodyKind::Star) return 60;
    if (body.id == "jupiter") return 48;
    if (body.id == "saturn") return 42;
    if (body.id == "uranus" || body.id == "neptune") return 36;
    if (body.id == "earth") return 32;
    if (body.id == "venus") return 28;
    if (body.id == "mars") return 25;
    if (body.kind == SystemBodyKind::Moon) return 18;
    if (body.kind == SystemBodyKind::Station) return 34;
    return 21;
}
std::string button(std::string_view label, std::string_view id, bool enabled = true, std::string_view cssClass = {},
    bool defaultFocus = false) {
    return "<button type=\"button\" data-rr-action=\"" + esc(id) + "\"" +
        (enabled ? (cssClass.empty() ? "" : " class=\"" + esc(cssClass) + "\"") +
                       " data-ui-focus-id=\"action:" + esc(id) + "\"" +
                       (defaultFocus ? " data-ui-default-focus=\"1\"" : "")
                 : " class=\"disabled\" disabled") + ">" + esc(label) + "</button>";
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
std::string solarMissionChecklist(
    const GameState& state, const ContentCatalog& catalog, std::string_view bodyId)
{
    const SolarMissionDefinition* mission = solarMissionForBody(catalog, bodyId);
    if (mission == nullptr || !solarMissionAvailable(state, *mission)) return {};
    if (solarMissionClaimed(state, catalog, *mission))
        return "<p class=\"solar-mission-checklist\">MISSION COMPLETE</p>";
    const ScenarioInstance* instance = findScenarioInstance(state.meta, mission->scenarioId);
    const ScenarioDefinition* definition = catalog.findScenario(mission->scenarioId);
    if (instance == nullptr || definition == nullptr) return {};
    bool recovered = false;
    std::ostringstream out;
    out << "<p class=\"solar-mission-checklist\">MISSION";
    for (const ScenarioStepDefinition& step : definition->steps) {
        if (step.id == "briefing") continue;
        const ScenarioStepProgress* progress = findScenarioStepProgress(*instance, step.id);
        if (progress == nullptr) continue;
        const bool done = progress->completed;
        if (step.id == mission->claimStepId) {
            recovered = done;
            continue;
        }
        out << " / " << (done ? "[x] " : "[ ] ") << esc(step.goalText.empty() ? step.title : step.goalText);
        if (step.requiredProgress > 1)
            out << " " << std::min(progress->progress, step.requiredProgress) << "/" << step.requiredProgress;
    }
    bool revealed = recovered;
    const auto artifactVisible = [&](const MiningRunState& mining) {
        return mining.bodyId == mission->bodyId &&
            (mining.artifact.revealed || mining.artifact.state == MiningArtifactState::Loose);
    };
    if (artifactVisible(state.run.mining)) revealed = true;
    for (const PersistentSiteState& site : state.run.expedition.sites)
        if (site.bodyId == mission->bodyId && artifactVisible(site.mining)) revealed = true;
    out << " / " << (revealed ? "[x] " : "[ ] ") << "Reveal artifact"
        << " / " << (recovered ? "[x] " : "[ ] ") << "Recover artifact</p>";
    return out.str();
}
}
std::string hazardDroneMissionMarkup(const PanelRenderContext& c, bool includeDroneOps, bool defaultFocus)
{
    const auto& state = c.state;
    const auto& expedition = state.run.expedition;
    const auto* mission = solarMissionForBody(c.catalog, expedition.location.bodyId);
    if (!expedition.travelInitialized || !mission || mission->bodyId != "io" ||
        !solarMissionAvailable(state, *mission) || solarMissionClaimed(state, c.catalog, *mission)) return {};

    const auto acceptance = solarMissionAcceptanceForBody(state, c.catalog, mission->bodyId);
    const auto hasHazard = [&](const std::vector<std::string>& ids) {
        return std::any_of(ids.begin(), ids.end(), [&](const std::string& id) {
            const auto found = std::find_if(c.catalog.miniDrones.begin(), c.catalog.miniDrones.end(),
                [&](const MiniDrone& drone) { return drone.id == id; });
            return found != c.catalog.miniDrones.end() && found->role == MiniDroneRole::Hazard;
        });
    };
    const bool owned = hasHazard(state.meta.ownedDroneIds);
    const bool assigned = hasHazard(state.meta.equippedDroneIds);
    std::string status, detail;
    if (acceptance.available) {
        status = "HAZARD SUPPORT // NOT COMMISSIONED";
        detail = acceptance.detail;
    } else if (assigned) {
        status = "HAZARD SUPPORT // ASSIGNED";
        detail = "Hazard Drone assigned to the active loadout.";
    } else if (owned) {
        status = "HAZARD SUPPORT // OWNED, NOT ASSIGNED";
        detail = "Assign the owned Hazard Drone in Drone Ops at ship service. Free a slot if the bay is full.";
    } else {
        status = "HAZARD SUPPORT // NO FRAME ABOARD";
        detail = "No Hazard Drone frame is aboard. Check available frames in Drone Ops.";
    }
    std::string result = "<section class=\"phase-advisory hazard-mission-strip\" data-hazard-support=\"" +
        std::string(acceptance.available ? "uncommissioned" : assigned ? "assigned" : owned ? "unassigned" : "missing") +
        "\"><strong>" + esc(status) + "</strong><p>" + esc(detail) + "</p>";
    if (acceptance.available || includeDroneOps) {
        result += "<div class=\"actions action-row hazard-mission-actions\">";
        if (acceptance.available) {
            result += button(acceptance.actionLabel,
                ui::actions::scenarioAction(acceptance.scenarioId, acceptance.stepId, static_cast<int>(acceptance.action)),
                true, "ok", defaultFocus);
        }
        if (includeDroneOps) result += button("Drone Ops", ui::actions::droneOps, true, "ghost", defaultFocus && !acceptance.available);
        result += "</div>";
    }
    return result + "</section>";
}
void appendExpeditionPresentation(const PanelRenderContext& c, PanelDocumentPresentation& panel) {
    const auto& state = c.state;
    const auto& e = state.run.expedition;
    if (!e.travelInitialized || c.titleScreenActive || c.sceneFadeToBlack > 0.0) return;
    if (state.screen==Screen::Flight && openingMissionRetryEligible(state) &&
        e.decision.pendingId=="opening_retry") {
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
    const CoursePlan& mapCourse = c.waypointPreviewCourse ? *c.waypointPreviewCourse : e.course;
    const auto* proposedMapTarget = systemBody(system, mapCourse.targetBodyId);
    const auto revealed = [&](const SystemBodyDefinition& body) {
        return solarBodyRevealed(state, c.catalog, body.id);
    };
    const auto* mapTarget = proposedMapTarget && revealed(*proposedMapTarget)
        ? proposedMapTarget : nullptr;
    const auto* region = systemBody(system, e.location.bodyId);
    const bool atOperationalDock = state.screen == Screen::Hangar && operationalHomeDocked(e);
    if (atOperationalDock) {
        // The live dock has one departure surface. Legacy launch, crew intake,
        // route gates and refit modals must never compete with it.
        std::erase_if(panel.modals, [](const auto& item) {
            return item.id != "settings" && item.id != "inventory" && item.id != "incoming_message" &&
                item.id != "system_menu" && item.id != "controls" && item.id != "reset_save_confirm";
        });
    }
    const auto objective = [&] {
        const auto solar = solarMissionObjectiveForBody(state, c.catalog, e.location.bodyId);
        return solar.available
            ? solar
            : scenarioObjectiveForDestination(state, c.catalog, expeditionEnvironment(state, c.catalog).id);
    }();
    const auto action = [&](std::string_view label, std::string_view suffix, bool enabled = true, bool defaultFocus = false) {
        return button(label, "expedition:" + std::string(suffix), enabled, {}, defaultFocus);
    };
    std::ostringstream map;
    map << "<section class=\"expedition-map\"><div class=\"solar-map-heading\"><span>SOLAR SYSTEM</span><strong>"
        << (atOperationalDock ? "DOCKED / CHOOSE NEXT DESTINATION" :
            e.cruise.active ? "PAUSED / CRUISE WILL RESUME" : "PAUSED / MANUAL FLIGHT")
        << "</strong></div><div class=\"solar-map-scroll\"><div class=\"solar-map\">";

    double outerRevealedOrbit = 108;
    for (const auto& b : system.bodies) {
        const auto placement = solarMapPlacement(b.id);
        if (placement && b.kind != SystemBodyKind::Moon && b.kind != SystemBodyKind::Station &&
            revealed(b))
            outerRevealedOrbit = std::max(outerRevealedOrbit, placement->orbitX);
    }
    const double chartScale = std::clamp(288.0/outerRevealedOrbit, 1.0, 2.1);
    const auto displayPlacement = [&](std::string_view id) -> std::optional<SolarMapPlacement> {
        auto placement = solarMapPlacement(id);
        const auto* body = systemBody(system, id);
        if (!placement || !body || body->kind == SystemBodyKind::Star || body->kind == SystemBodyKind::Station)
            return placement;
        const auto anchor = body->kind == SystemBodyKind::Moon ? solarMapPlacement(body->parentId) : placement;
        if (!anchor) return placement;
        const double dx = (anchor->x-352)*(chartScale-1);
        const double dy = (anchor->y-175)*(chartScale-1);
        placement->x += dx; placement->labelX += dx;
        placement->y += dy; placement->labelY += dy;
        placement->orbitX *= chartScale; placement->orbitY *= chartScale;
        return placement;
    };

    for (const auto& b : system.bodies) {
        const auto placement = displayPlacement(b.id);
        if (!placement || placement->orbitX <= 0 || !revealed(b) || b.kind == SystemBodyKind::Moon)
            continue;
        map << "<div class=\"solar-orbit\" style=\"left:" << 352-placement->orbitX << "dp;top:"
            << 175-placement->orbitY << "dp;width:" << placement->orbitX*2 << "dp;height:"
            << placement->orbitY*2 << "dp;\"></div>";
    }

    const SystemBodyDefinition* currentBody = nullptr;
    if (!e.location.bodyId.empty()) currentBody = systemBody(system, e.location.bodyId);
    if (!currentBody || !revealed(*currentBody)) {
        double nearestDistance = 1e12;
        for (const auto& b : system.bodies) {
            if (!revealed(b) || b.kind == SystemBodyKind::Station) continue;
            const double candidate = std::hypot(absolute.position.x-b.position.x, absolute.position.y-b.position.y);
            if (candidate < nearestDistance) { nearestDistance = candidate; currentBody = &b; }
        }
    }
    if (currentBody && mapTarget) {
        const auto from = displayPlacement(currentBody->id);
        const auto to = displayPlacement(mapTarget->id);
        if (from && to && currentBody->id != mapTarget->id)
            mapLine(map, "solar-course", from->x, from->y, to->x, to->y);
    }

    for (const auto& b : system.bodies) {
        if (b.kind != SystemBodyKind::Moon || !revealed(b)) continue;
        const auto child = displayPlacement(b.id);
        const auto parent = displayPlacement(b.parentId);
        const auto* parentBody = systemBody(system, b.parentId);
        if (child && parent && parentBody && revealed(*parentBody))
            mapLine(map, "solar-moon-link", parent->x, parent->y, child->x, child->y);
    }

    std::string mapDefaultBody = mapTarget ? mapTarget->id : std::string{};
    if (mapDefaultBody.empty()) {
        for (const auto& b : system.bodies) {
            if (revealed(b) && displayPlacement(b.id) && b.kind != SystemBodyKind::Star) {
                mapDefaultBody = b.id;
                break;
            }
        }
    }
    for (const auto& b : system.bodies) {
        if (!revealed(b)) continue;
        const auto placement = displayPlacement(b.id);
        if (!placement) continue;
        const bool hazard = std::find(mapCourse.intersectedHazards.begin(), mapCourse.intersectedHazards.end(), b.id) != mapCourse.intersectedHazards.end();
        const double size = solarMapDiameter(b);
        const std::string art = b.kind == SystemBodyKind::Station ? "straylight-ark-damaged"
            : b.kind == SystemBodyKind::Moon ? "moon" : b.id;
        map << "<button class=\"solar-planet" << (b.kind == SystemBodyKind::Star ? " solar-planet-sun" : "")
            << (b.id == mapCourse.targetBodyId ? " solar-planet-selected" : "")
            << "\" data-rr-action=\"expedition:preview:" << esc(b.id)
            << "\" data-ui-focus-id=\"planet:" << esc(b.id)
            << "\" data-ui-focus-skip=\"1\" tabindex=\"-1\" aria-label=\"" << esc(b.name)
            << "\" style=\"left:" << placement->x-size*.5
            << "dp;top:" << placement->y-size*.5 << "dp;width:" << size << "dp;height:" << size << "dp;\">";
        if (b.kind == SystemBodyKind::Star) map << "<div class=\"solar-sun\"></div>";
        else map << "<img src=\"planets/" << art << ".png\" />";
        map << "</button>";
        map << "<div class=\"solar-body" << (b.id == mapCourse.targetBodyId ? " solar-selected" : "") << (hazard ? " solar-hazard" : "")
            << (b.kind == SystemBodyKind::Moon ? " solar-moon-label" : "")
            << "\" style=\"left:" << placement->labelX << "dp;top:" << placement->labelY << "dp;width:"
            << placement->labelWidth << "dp;\">" << action(b.name, "preview:" + b.id, true, b.id == mapDefaultBody) << "</div>";
    }
    for (const auto& w : e.wrecks) {
        const auto p = convertSystemFrame(w.location, CoordinateFrame::System, "", system);
        const SystemBodyDefinition* nearest = nullptr;
        double nearestDistance = 1e12;
        for (const auto& b : system.bodies) {
            if (!revealed(b)) continue;
            const double candidate = std::hypot(p.position.x-b.position.x,p.position.y-b.position.y);
            if (candidate < nearestDistance) { nearestDistance = candidate; nearest = &b; }
        }
        const auto placement = nearest ? displayPlacement(nearest->id) : std::nullopt;
        if (placement) map << "<div class=\"solar-wreck\" style=\"left:" << placement->x+16
            << "dp;top:" << placement->y+15 << "dp;\">W" << w.id << "</div>";
    }
    map << "</div></div><p class=\"solar-map-help\">"
        << (atOperationalDock
            ? "Select a world, then confirm. Depart from the dock."
            : "Sets the marker and route forecast. Flight stays manual.")
        << "</p>";
    if (mapTarget) {
        const bool alreadyDockedHere = atOperationalDock && e.location.bodyId == mapTarget->id;
        const std::string mapTargetName = mapTarget->dock ? mapTarget->name + " Orbital Dock" : mapTarget->name;
        map << "<section class=\"solar-selection\"><div class=\"solar-selection-copy\"><h3>" << esc(mapTargetName)
            << "</h3><p>" << (alreadyDockedHere ? "Currently docked here. The planet surface is not landable; choose another revealed world" :
                mapTarget->dock ? "Rendezvous with the service dock; the planet surface is not landable" :
                mapTarget->siteId.empty() ? "Orbital exploration" : "Surface expedition")
            << (mapCourse.estimateValid ? " / Approach " + num(mapCourse.approachFuel) + " fuel / Return " + num(mapCourse.returnMargin) : " / Fuel estimate unavailable")
            << "</p><p class=\"expedition-warning\">" << esc(mapTarget->hazard) << "</p></div><div class=\"solar-selection-action\">"
            << action(alreadyDockedHere ? "Already docked at " + mapTarget->name : "Set waypoint: " + mapTargetName,
                "plot:" + mapTarget->id, !alreadyDockedHere) << "</div></section>";
    } else {
        map << "<section class=\"solar-selection\"><div class=\"solar-selection-copy\"><h3>SELECT DESTINATION</h3><p>Choose a revealed world to preview its route.</p></div></section>";
    }
    map << "<p class=\"solar-map-manifest\">FUEL " << num(flight.fuelRemaining) << " / " << num(flight.fuelCapacity)
        << " / CARGO " << e.cargo.materials.common << "C " << e.cargo.materials.rare << "R " << e.cargo.materials.exotic
        << "X / " << num(e.cargo.credits) << " UNBANKED</p>";
    for (const auto& w : e.wrecks) map << "<p>Wreck " << w.id << " - " << action(w.buildRecoverable ? "Recover upgrades and cargo" : "Recover cargo", "recover:" + std::to_string(w.id), canSalvageWreck(e, flight, system, w.id)) << "</p>";
    map << action(atOperationalDock ? "Back to dock" : "Resume flight", "close");
    if (e.active) map << action("Abandon ship", "abandon");
    map << "</section>";
    replaceModal(panel, {"map", "SET WAYPOINT", map.str(), "expedition:close", false, true, true, ModalTone::Neutral});
    if (e.active) replaceModal(panel, {"expedition_abandon", "ABANDON SHIP", "<p>Return to Earth in a replacement ship. Cargo and upgrades remain in your wreck.</p><div class=\"action-row\">" + action("Cancel", "close", true, true) + action("Abandon ship", "confirm_abandon") + "</div>", "expedition:close", false, true, true, ModalTone::Warning});
    if (!e.progression.pendingGraftConflicts.empty()) {
        const auto& conflict = e.progression.pendingGraftConflicts.front();
        const auto moduleName = [&](DroneModuleKind kind) {
            const auto found = std::find_if(c.catalog.droneModules.begin(), c.catalog.droneModules.end(),
                [&](const auto& module) { return module.kind == kind; });
            return found == c.catalog.droneModules.end() ? std::string("Unknown graft") : found->name;
        };
        const std::string copy = "<p>Slot " + std::to_string(conflict.equippedFrame + 1) +
            " has two grafts. Choose the installed module.</p><div class=\"action-row\">" +
            action("Keep " + moduleName(conflict.current.module), "graft_conflict:keep", true, true) +
            action("Install " + moduleName(conflict.recovered.module), "graft_conflict:recovered") + "</div>";
        replaceModal(panel, {"graft_conflict", "RECOVERED GRAFT", copy, {}, true, false, false, ModalTone::Neutral});
    }
    if (atOperationalDock) {
        std::ostringstream home;
        const std::string waypointName = target ? target->name : "None";
        const auto* nextMission = nextSolarMission(state, c.catalog);
        const auto* nextBody = nextMission ? systemBody(system, nextMission->bodyId) : nullptr;
        const SolarMissionDefinition* lastCompleted = nullptr;
        for (const auto& mission : c.catalog.solarMissions)
            if (!mission.optional && solarMissionClaimed(state, c.catalog, mission)) lastCompleted = &mission;
        const std::string departLabel = target && target->id != e.location.bodyId
            ? "DEPART FOR " + target->name : "DEPART DOCK";
        home << "<section class=\"expedition-home\"><h2>" << esc(region ? region->name : "Home") << " / ORBITAL DOCK</h2><p>" << esc(state.statusLine)
            << "</p><p>Upgrades survive docking. Recover your wreck to reclaim lost upgrades.</p>"
            << "<section class=\"expedition-dock-status\">"
            << "<div class=\"dock-status-segment\"><span>SHIP FUEL</span><strong>" << num(flight.fuelRemaining) << " / " << num(flight.fuelCapacity) << "</strong></div>"
            << "<div class=\"dock-status-segment\"><span>HULL</span><strong>" << num(flight.hullRemaining) << "</strong></div>"
            << "<div class=\"dock-status-segment dock-status-credits\"><span>CREDITS</span><strong>" << num(state.run.credits) << "</strong></div>"
            << "</section><section class=\"expedition-dock-departure\">";
        if (lastCompleted) {
            const auto* completedBody = systemBody(system, lastCompleted->bodyId);
            home << "<p>MISSION COMPLETE: " << esc(completedBody ? completedBody->name : lastCompleted->bodyId) << "</p>";
        }
        home << "<p>NEXT MISSION: "
            << esc(nextBody ? nextBody->name : (arkDiscovered(state) ? "Straylight" : "Complete"))
            << "</p><p>WAYPOINT: " << esc(waypointName)
            << "</p>" << action(departLabel, "depart", true, true) << "</section><div class=\"action-row\">" << action("Change waypoint", "map");
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
    const bool stableMissionContext = c.incomingMessageDeliveryAllowed && !c.miningExtractionActive &&
        e.progression.pendingRunUpgradeChoices == 0 &&
        ((state.screen == Screen::Mining && !state.run.mining.failurePending) ||
         (state.screen == Screen::Flight && !c.surfaceArrivalActive && flight.mode != FlightMode::Landing) || atOperationalDock);
    if (stableMissionContext) {
        for (const auto& mission : c.catalog.solarMissions) {
            const auto claim = solarMissionObjectiveForBody(state, c.catalog, mission.bodyId);
            if (claim.state != ScenarioStepState::ReadyToClaim || claim.action != ScenarioActionKind::ClaimReward) continue;
            const auto* body = systemBody(system, mission.bodyId);
            const std::string title = (body ? body->name : mission.bodyId) + " mission ready";
            const std::string claimAction = ui::actions::scenarioAction(claim.scenarioId, claim.stepId, static_cast<int>(claim.action));
            const bool autoOpen = std::none_of(panel.modals.begin(), panel.modals.end(), [](const auto& item) { return item.autoOpen; });
            std::string copy = "Objectives complete. " + claim.rewardPreview;
            if (mission.bodyId == "moon") {
                copy += " The Prospector mines revealed ore pockets while you explore. Manage it in Drone Ops.";
            }
            if (auto card = buildIncomingMessageCard(c, mission.briefingMessageId, "default", claimAction,
                    title, copy, "Claim mission reward")) {
                card->id = "solar_mission_claim";
                card->autoOpen = autoOpen;
                replaceModal(panel, std::move(*card));
            }
            break;
        }
    }
    if (state.screen == Screen::Flight && !c.surfaceArrivalActive) {
        const bool dockInRange = expeditionDockInRange(e, flight, system);
        const bool dockReady = canDockExpedition(e, flight, system);
        const bool flightDefaultAvailable = panel.contentMarkup.find("data-ui-default-focus=") == std::string::npos;
        const std::string hazardMission = hazardDroneMissionMarkup(c);
        panel.contentMarkup += "<div class=\"expedition-flight-bar" +
        std::string(hazardMission.empty() ? "" : " expedition-flight-mission-flow") + "\"><p>" + esc(region ? region->name : "Solar space") +
        " / Target: " + esc(target ? target->name : "None") + " / " + (e.cruise.active ?
            (e.cruise.cooling ? "CRUISE COOLING" : "CRUISE ACTIVE") : "MANUAL") + "</p><p>" + esc(objective.available ? objective.goal : "Explore, mine, and return to Earth") + "</p>" +
        solarMissionChecklist(state, c.catalog, e.location.bodyId) +
        hazardMission +
        action(e.cruise.active ? "Cruise off [C / L3]" : "Cruise [C / L3]", "cruise", flight.active && flight.mode != FlightMode::Landing && !e.undockReady,
            flightDefaultAvailable && !dockReady) +
        (dockInRange ? "<div class=\"expedition-dock-action\">" + button("DOCK", "expedition:dock", dockReady, "ok", flightDefaultAvailable && dockReady) + "</div>"
                     : action("Dock", "dock", false));
        if (flight.active && flight.mode != FlightMode::Landing) {
            auto position = e.location;
            captureSystemLocation(position, flight);
            position = convertSystemFrame(position, CoordinateFrame::System, "", system);
            for (const auto& body : system.bodies) {
                if (!body.dock) continue;
                const auto dock = systemDockPosition(body);
                const double range = std::hypot(position.position.x-dock.x,position.position.y-dock.y);
                if (range > expeditionDockRadius) continue;
                panel.contentMarkup += dockReady
                    ? "<p>In range — press Dock.</p>"
                    : "<p>In range — slow below 0.20 relative speed, then press Dock.</p>";
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
            action("Set Earth waypoint", "decision:home:" + d.pendingId, true, true) + action("Set " + recommendedExpeditionLead(state, c.catalog) + " waypoint", "decision:lead:" + d.pendingId) + action("Inspect map", "decision:map:" + d.pendingId) + "</div>", "expedition:decision:map:" + d.pendingId, true, false, false, ModalTone::Neutral});
}
} // namespace rocket
