#include "game/ExpeditionPresentation.h"
#include "core/ArtifactProgression.h"
#include "core/ExpeditionSystem.h"
#include "core/MissionGuidance.h"
#include "core/StraylightSequence.h"
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
std::string bankedArtifactMarkup(const PersistentExpeditionState& e) {
    const bool ark = e.location.bodyId == "straylight";
    if (!ark && e.location.bodyId != "earth") return {};
    std::string names;
    int count = 0;
    for (const auto& b : e.batteries) {
        if (b.owner != (ark ? BatteryOwner::ArkSlot : BatteryOwner::EarthStorage)) continue;
        const auto* origin = systemBody(solarSystemDefinition(), b.id);
        if (count++) names += ", ";
        names += origin ? origin->name : b.id;
    }
    for (const auto& artifact : e.artifacts) {
        if (artifact.owner != ArtifactCustody::Banked ||
            artifact.requiredDockId != e.location.bodyId) continue;
        const auto* origin = systemBody(solarSystemDefinition(), artifact.artifact.originDestinationId);
        const std::string label = origin ? origin->name : artifact.artifact.originDestinationId;
        if (names.find(label) != std::string::npos) continue;
        if (count++) names += ", ";
        names += label;
    }
    if (!count) return {};
    return "<section class=\"artifact-bank-status\"><strong>" +
        std::string(count == 1 ? "MISSION ARTIFACT READY" : "MISSION ARTIFACTS READY") +
        "</strong><p>" + esc(names) + " / " + (ark ? "At Straylight dock" : "At Earth dock") +
        "</p></section>";
}
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
    using Stage = StraylightStage;
    const auto stage = state.meta.straylightStage;
    const auto beaconChecklist = [&]() {
        std::string text = "<section class=\"straylight-beacons\"><h3>BEACON RECOVERY</h3>";
        for (const auto& b : e.batteries) {
            const auto* origin = systemBody(solarSystemDefinition(), b.id);
            const std::string location = b.owner == BatteryOwner::EarthStorage ? "Earth Storage" :
                b.owner == BatteryOwner::Ship ? "Aboard your ship" : b.owner == BatteryOwner::ArkSlot ? "Installed in Straylight" :
                b.owner == BatteryOwner::Wreck ? "Wreck " + std::to_string(b.wreckId) : "At recovery site";
            text += "<p>" + std::string(b.owner == BatteryOwner::ArkSlot ? "[x] " : "[ ] ") +
                esc(origin ? origin->name : b.id) + " / " + location + "</p>";
        }
        return text + "</section>";
    };
    if (straylightOwnsPresentation(state)) {
        std::erase_if(panel.modals, [](const auto& modal) {
            return modal.id != "system_menu" && modal.id != "settings" &&
                modal.id != "controls" && modal.id != "reset_save_confirm";
        });
        for (auto& modal : panel.modals) if (modal.id == "system_menu") {
            modal.bodyMarkup = "<div class=\"modal-actions action-row system-menu-actions\">"
                "<button class=\"ok\" data-ui-close-modal=\"1\" data-controller-resume=\"1\" data-ui-focus-id=\"system:resume\" data-ui-default-focus=\"1\">Resume</button>"
                "<button class=\"ghost\" data-ui-modal=\"controls\" data-ui-focus-id=\"modal:controls\">Controls</button>"
                "<button class=\"ghost\" data-ui-modal=\"settings\" data-ui-focus-id=\"modal:settings\">Settings</button></div>";
        }
        panel.contentMarkup = "<section class=\"straylight-sequence\">";
        panel.templateKind = PanelTemplateKind::Takeover;
        panel.metadata.overlay = PanelOverlayKind::None;
        panel.metadata.screen = Screen::StoryBriefing;
        panel.metadata.visualFamily = PanelVisualFamily::Fullscreen;
        panel.metadata.layoutMode = PanelLayoutMode::Fullscreen;
        panel.metadata.surface = PanelSurfaceKind::None;
        panel.metadata.interaction = PanelInteractionMode::Takeover;
        panel.runtime = {};
        panel.metadata.legacyContentOwnsLaneGeometry = false;
        panel.runtime.responsiveViewport = true;
        const auto sequenceAction = [&](std::string_view label, std::string_view id, bool enabled = true) {
            return button(label, "expedition:straylight:" + std::string(id), enabled, "ok", true);
        };
        const auto transmission = [&](std::string_view message, std::string_view id) {
            if (auto card = buildIncomingMessageCard(c, message, "default", "expedition:straylight:" + std::string(id))) {
                card->autoOpen = true;
                panel.modals.push_back(std::move(*card));
            }
        };
        if (straylightCinematicDuration(stage) > 0) {
            panel.contentMarkup += sequenceAction("Skip animation", "skip");
        } else if (stage == Stage::Invitation) {
            transmission("triton_mission_complete", "invitation");
        } else if (stage == Stage::FirstContact) {
            transmission("straylight_beacons", "retrieve");
        } else if (stage == Stage::RetrieveBeacons) {
            const bool carried = std::any_of(e.batteries.begin(), e.batteries.end(), [](const auto& b) { return b.owner == BatteryOwner::Ship; });
            const bool installed = std::all_of(e.batteries.begin(), e.batteries.end(), [](const auto& b) { return b.owner == BatteryOwner::ArkSlot; });
            panel.contentMarkup += bankedArtifactMarkup(e) + beaconChecklist() + sequenceAction("Install carried beacons", "install", carried) +
                sequenceAction("Bring Straylight online", "prepare_online", installed) +
                button("Depart dock", "expedition:depart", true, "ghost");
        } else if (stage == Stage::ConfirmOnline) {
            panel.contentMarkup += "<h2>POINT OF NO RETURN</h2><p>Bringing Straylight online commits you to evacuation and departure. Solar exploration will end.</p>" +
                button("Not yet", "expedition:straylight:cancel_online", true, "ghost", true) +
                button("Bring Straylight online", "expedition:straylight:online", true, "warn");
        } else if (stage == Stage::Online) {
            transmission("straylight_online", "coordinate");
        } else if (stage == Stage::EvacuationBriefing) {
            transmission("straylight_evacuation", "boarding");
        } else if (stage == Stage::Arrived) {
            transmission("straylight_arrival", "arrived");
        } else if (stage == Stage::Boarded || stage == Stage::Secured) {
            panel.contentMarkup += stage == Stage::Boarded ? sequenceAction("Secure the Ark", "secure") : sequenceAction("Depart for Aaru Vale", "depart");
        } else if (stage == Stage::Complete) {
            panel.contentMarkup += "<h2>AARU VALE</h2><p>Straylight / Home dock</p><p>Evacuation complete. The Ark is safely holding in Aaru Vale.</p>" + bankedArtifactMarkup(e);
        }
        if (stage >= Stage::Online && stage <= Stage::Departing) {
            panel.contentMarkup += "<div class=\"straylight-checklist\"><p>" + std::string(stage > Stage::Online ? "[x]" : "[ ]") + " Coordinate evacuation</p><p>" +
                (stage >= Stage::Boarded ? "[x]" : "[ ]") + " Complete boarding</p><p>" +
                (stage >= Stage::Secured ? "[x]" : "[ ]") + " Secure the Ark</p><p>[ ] Depart for Aaru Vale</p></div>";
        }
        if (panel.contentMarkup == "<section class=\"straylight-sequence\">") panel.contentMarkup.clear();
        else panel.contentMarkup += "</section>";
        return;
    }
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
    const auto& flight = c.launchFlight ? *c.launchFlight : state.run.flight;
    auto location = e.location;
    captureSystemLocation(location, flight);
    const auto absolute = convertSystemFrame(location, CoordinateFrame::System, "", system);
    const auto recommendation = recommendedCampaignObjective(state,c.catalog);
    const auto selectedName = courseTargetName(e,system,e.course.targetBodyId);
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
            << placement->labelWidth << "dp;\">" << action(b.name + (recommendation.targetId==b.id ? " *" : ""), "preview:" + b.id, true, b.id == mapDefaultBody) << "</div>";
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
        if (placement && currentBody && e.course.targetBodyId=="wreck:"+std::to_string(w.id)) {
            if (const auto from=displayPlacement(currentBody->id))
                mapLine(map,"solar-course",from->x,from->y,placement->x+16,placement->y+15);
        }
        if (placement) map << "<div class=\"solar-wreck" << (wreckCarriesArtifact(e, w.id) ? " solar-wreck-artifact" : "")
            << "\" style=\"left:" << placement->x+16 << "dp;top:" << placement->y+15 << "dp;\">"
            << (wreckCarriesArtifact(e, w.id) ? "&#9670; " : "") << "W" << w.id
            << (recommendation.wreckId==w.id ? " *" : "") << "</div>";
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
                (mapTarget->id=="mercury" || mapTarget->id=="venus" ||
                 (solarMissionForBody(c.catalog,mapTarget->id) && solarMissionForBody(c.catalog,mapTarget->id)->optional)) ? "Optional exploration" :
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
        << "X / " << num(e.cargo.credits) << " UNSECURED</p>";
    map << "<section class=\"expedition-dock-departure\"><h3>RECOMMENDED OBJECTIVE *</h3><p>" << esc(recommendation.title)
        << "</p><p>" << esc(recommendation.detail) << "</p><p>SELECTED WAYPOINT: " << esc(selectedName)
        << (e.coursePlayerSelected ? " / MANUAL OVERRIDE" : " / FOLLOWING MISSION")
        << "</p>" << action("Follow mission","follow_mission",!recommendation.targetId.empty()) << "</section>";
    for (const auto& w : e.wrecks) map << "<p class=\"" << (wreckCarriesArtifact(e, w.id) ? "wreck-artifact-copy" : "") << "\">"
        << esc(wreckDisplayName(e, w.id)) << " - "
        << action("Set waypoint: " + wreckDisplayName(e, w.id),"plot:wreck:"+std::to_string(w.id))
        << action(wreckCarriesArtifact(e, w.id) ? "Collect artifact and salvage" : w.buildRecoverable ? "Recover upgrades and cargo" : "Recover cargo",
            "recover:" + std::to_string(w.id), canSalvageWreck(e, flight, system, w.id)) << "</p>";
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
        const std::string waypointName = selectedName;
        const SolarMissionDefinition* lastCompleted = nullptr;
        for (const auto& mission : c.catalog.solarMissions)
            if (!mission.optional && solarMissionClaimed(state, c.catalog, mission)) lastCompleted = &mission;
        const std::string departLabel = !e.course.targetBodyId.empty() && e.course.targetBodyId != e.location.bodyId
            ? "DEPART FOR " + selectedName : "DEPART DOCK";
        home << "<section class=\"expedition-home\"><h2>" << esc(region ? region->name : "Home") << " / ORBITAL DOCK</h2><p>" << esc(state.statusLine)
            << "</p>" << bankedArtifactMarkup(e) << "<p>Upgrades survive docking. Recover your wreck to reclaim lost upgrades.</p>"
            << "<section class=\"expedition-dock-status\">"
            << "<div class=\"dock-status-segment\"><span>SHIP FUEL</span><strong>" << num(flight.fuelRemaining) << " / " << num(flight.fuelCapacity) << "</strong></div>"
            << "<div class=\"dock-status-segment\"><span>HULL</span><strong>" << num(flight.hullRemaining) << "</strong></div>"
            << "<div class=\"dock-status-segment dock-status-credits\"><span>CREDITS</span><strong>" << num(state.run.credits) << "</strong></div>"
            << "</section><section class=\"expedition-dock-departure\">";
        if (lastCompleted) {
            const auto* completedBody = systemBody(system, lastCompleted->bodyId);
            home << "<p>MISSION COMPLETE: " << esc(completedBody ? completedBody->name : lastCompleted->bodyId) << "</p>";
        }
        bool handInDefault = false;
        const auto handIn = [&](const MissionArtifact& a) {
            if (!artifactHandInAvailable(state,a)) return;
            const auto* scenario = a.scenarioId.empty() ? nullptr : findScenarioInstance(state.meta,a.scenarioId);
            const auto* progress = scenario ? findScenarioStepProgress(*scenario,a.stepId) : nullptr;
            const std::string id = !a.scenarioId.empty() && progress
                ? ui::actions::scenarioAction(a.scenarioId,a.stepId,static_cast<int>(ScenarioActionKind::ClaimReward))
                : "expedition:artifact_handin:" + a.key;
            const auto* body = systemBody(system,a.artifact.originDestinationId);
            home << button("Complete Mission: " + (body ? body->name : a.artifact.originDestinationId),id,true,"ok",!handInDefault);
            handInDefault = true;
        };
        for (const auto& mission : c.catalog.solarMissions)
            if (const auto* a = missionArtifact(state,mission.scenarioId,mission.claimStepId)) handIn(*a);
        for (const auto& a : e.artifacts) if (!a.key.starts_with("solar:")) handIn(a);
        home << "<p>SELECTED WAYPOINT: " << esc(waypointName) << (e.coursePlayerSelected ? " / MANUAL OVERRIDE" : " / FOLLOWING MISSION")
            << "</p><div class=\"expedition-dock-route-actions\">"
            << button(departLabel, "expedition:depart", true, "dock-depart", !handInDefault)
            << button("Change waypoint", "expedition:map", true, "dock-waypoint")
            << "</div></section><div class=\"action-row\">";
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
    if (stage == Stage::RevealPending) {
        panel.contentMarkup += "<section class=\"straylight-contact\"><strong>CONTACT RESOLVED - STRAYLIGHT</strong><p>WAYPOINT SET / Depart when ready.</p></section>";
    }
    if (stage == Stage::RetrieveBeacons && atOperationalDock && e.location.bodyId == "earth") {
        const bool stored = std::any_of(e.batteries.begin(), e.batteries.end(), [](const auto& b) { return b.owner == BatteryOwner::EarthStorage; });
        panel.contentMarkup += beaconChecklist() + button("Collect stored beacons", "expedition:straylight:collect", stored, "ok");
    }
    const bool stableMissionContext = c.incomingMessageDeliveryAllowed && !c.miningExtractionActive &&
        e.progression.pendingRunUpgradeChoices == 0 &&
        ((state.screen == Screen::Mining && !state.run.mining.failurePending) ||
         (state.screen == Screen::Flight && !c.surfaceArrivalActive && flight.mode != FlightMode::Landing) || atOperationalDock);
    if (stableMissionContext) {
        for (const auto& mission : c.catalog.solarMissions) {
            if (mission.bodyId == "triton" && stage != Stage::Hidden) continue;
            const auto claim = solarMissionObjectiveForBody(state, c.catalog, mission.bodyId);
            if (claim.state != ScenarioStepState::ReadyToClaim || claim.action != ScenarioActionKind::ClaimReward) continue;
            // A recovered artifact is only claimable after it has reached the
            // servicing dock. Keep the mission prompt out of mining/flight
            // screens while the artifact is still aboard and at risk.
            const auto* artifact = missionArtifact(state, mission.scenarioId, mission.claimStepId);
            if (!artifact || !artifactHandInAvailable(state, *artifact)) continue;
            const auto* body = systemBody(system, mission.bodyId);
            const std::string title = (body ? body->name : mission.bodyId) + " mission ready";
            const std::string claimAction = ui::actions::scenarioAction(claim.scenarioId, claim.stepId, static_cast<int>(claim.action));
            const bool autoOpen = std::none_of(panel.modals.begin(), panel.modals.end(), [](const auto& item) { return item.autoOpen; });
            std::string copy = "Artifact secured at the servicing dock. Complete the mission to claim: " + claim.rewardPreview;
            if (mission.bodyId == "moon") {
                copy += " The Prospector mines revealed ore pockets while you explore. Manage it in Drone Ops.";
            }
            if (auto card = buildIncomingMessageCard(c, mission.briefingMessageId, "default", claimAction,
                    title, copy, "Complete Mission")) {
                card->id = "solar_mission_claim";
                card->autoOpen = autoOpen;
                replaceModal(panel, std::move(*card));
            }
            break;
        }
    }
    if (state.screen == Screen::Flight && !c.surfaceArrivalActive &&
        !(c.orbitalWork && c.orbitalWork->active())) {
        const bool earthDocking = earthDockingActive(flight);
        const bool dockInRange = expeditionDockInRange(e, flight, system);
        const bool dockReady = canDockExpedition(e, flight, system);
        const bool flightDefaultAvailable = panel.contentMarkup.find("data-ui-default-focus=") == std::string::npos;
        const std::string hazardMission = hazardDroneMissionMarkup(c);
        panel.contentMarkup += "<div class=\"expedition-flight-bar" +
        std::string(hazardMission.empty() ? "" : " expedition-flight-mission-flow") + "\"><p>" + esc(region ? region->name : "Solar space") +
        " / Target: " + esc(selectedName) + " / " + (e.cruise.active ?
            (e.cruise.cooling ? "CRUISE COOLING" : "CRUISE ACTIVE") : "MANUAL") + "</p>" +
        hazardMission +
        action(e.cruise.active ? "Cruise off [C / L3]" : "Cruise [C / L3]", "cruise", flight.active && flight.mode != FlightMode::Landing && flight.mode != FlightMode::Docking && !e.undockReady,
            flightDefaultAvailable && !dockReady) +
        (earthDocking ? "<div class=\"expedition-dock-action\"><strong>EARTH DOCK / " + esc(earthDockingGuidance(flight)) + "</strong></div>"
            : dockInRange && e.location.bodyId == "earth"
                ? "<div class=\"expedition-dock-action\"><strong>EARTH DOCK / MANEUVER ENGAGED</strong></div>"
            : dockInRange ? "<div class=\"expedition-dock-action\">" + button(e.course.targetBodyId == "straylight" ? "Dock with Straylight" : "DOCK", "expedition:dock", dockReady, "ok", flightDefaultAvailable && dockReady) + "</div>"
                          : action("Dock", "dock", false));
        for (const auto& wreck : e.wrecks) {
            if (!canSalvageWreck(e, flight, system, wreck.id, false)) continue;
            const bool ready = canSalvageWreck(e, flight, system, wreck.id);
            panel.contentMarkup += "<p class=\"" + std::string(wreckCarriesArtifact(e, wreck.id) ? "wreck-artifact-copy" : "") + "\">" +
                esc(wreckDisplayName(e, wreck.id)) + " / " +
                (ready ? "Ready to salvage. Artifacts and upgrades fit even with a full ore hold." : "Match the wreck's speed to salvage.") + "</p>" +
                action(wreckCarriesArtifact(e, wreck.id) ? "Salvage artifact / Wreck " + std::to_string(wreck.id) : "Salvage wreck " + std::to_string(wreck.id),
                    "recover:" + std::to_string(wreck.id), ready);
        }
        panel.contentMarkup += "</div>";
    }
    const auto& d = e.decision;
    if (!d.pendingId.empty() && !d.awaitingAscent && state.screen == Screen::Flight && flight.mode != FlightMode::Landing && c.incomingMessageDeliveryAllowed && e.progression.pendingRunUpgradeChoices == 0 &&
        std::none_of(panel.modals.begin(), panel.modals.end(), [](const auto& item) { return item.autoOpen; }))
        replaceModal(panel, {"expedition_decision", "NEXT COURSE", "<p>Objective secured. Return to the dock to secure salvage, or continue with your current fuel and build.</p><div class=\"action-row\">" +
            action("Set Earth waypoint", "decision:home:" + d.pendingId, true, true) + action("Set " + recommendedExpeditionLead(state, c.catalog) + " waypoint", "decision:lead:" + d.pendingId) + action("Inspect map", "decision:map:" + d.pendingId) + "</div>", "expedition:decision:map:" + d.pendingId, true, false, false, ModalTone::Neutral});
}

void appendMissionPresentation(const PanelRenderContext& c, PanelDocumentPresentation& panel) {
    const auto& s = c.state;
    const auto& e = s.run.expedition;
    if (!e.travelInitialized || c.titleScreenActive || c.sceneFadeToBlack > 0 || c.titleLaunchActive) return;
    const auto v = trackedMissionView(s, c.catalog, c.launchFlight, c.orbitalWork && c.orbitalWork->surveyComplete);
    const std::string tabs = "<div class=\"mission-tabs action-row\">" + button("Map", "expedition:map", !straylightCommitted(s)) + button("Missions", "expedition:missions") + "</div>";
    for (auto& modal : panel.modals) if (modal.id == "map") modal.bodyMarkup = tabs + modal.bodyMarkup;
    std::string log = tabs;
    std::string completed;
    int completedCount = 0;
    for (const auto& item : missionLog(s, c.catalog)) {
        std::string entry = "<section class=\"mission-log-entry\"><h3>" + esc(item.location + " / " + item.title) +
            (item.optional ? " (optional)" : "") + "</h3><p>" + esc(item.purpose) + "</p><ol>";
        const auto appendGoals = [&](const auto& goals) {
            for (const auto& row : goals)
                entry += "<li class=\"" + std::string(row.complete ? "mission-done" : "mission-pending") + "\">" +
                    (row.complete ? "[x] " : "[ ] ") + esc(row.text) +
                    (row.detail.empty() ? "" : " - " + esc(row.detail)) + "</li>";
        };
        if (!item.arrivalGoals.empty()) {
            entry += "</ol><h4>Arrival</h4><ol>";
            appendGoals(item.arrivalGoals);
            entry += "</ol><h4>Recovery</h4><ol>";
        }
        appendGoals(item.recoveryGoals.empty() ? item.requirements : item.recoveryGoals);
        entry += "</ol><p>" + esc(item.instruction) + "</p>";
        for (const auto& progress : item.progress) entry += "<p>" + esc(progress) + "</p>";
        if (!item.reward.empty()) entry += "<p class=\"mission-reward\">" + esc(item.reward) + "</p>";
        if (!item.complete) entry += button(item.id == v.id ? "Tracked mission" : "Track mission", "expedition:track:" + item.id,
            item.id != v.id && !straylightCommitted(s));
        entry += "</section>";
        if (item.complete) { completed += entry; ++completedCount; } else log += entry;
    }
    if (completedCount) {
        log += button((c.showCompletedMissions ? "Hide completed (" : "Show completed (") + std::to_string(completedCount) + ")", "expedition:mission_history");
        if (c.showCompletedMissions) log += completed;
    }
    log += button("Resume", "expedition:missions_close");
    replaceModal(panel, {"missions", "MISSIONS", log, "expedition:missions_close", false, true, true, ModalTone::Neutral});
    if (!v.available || straylightCinematicDuration(s.meta.straylightStage) > 0 ||
        std::any_of(panel.modals.begin(), panel.modals.end(), [](const auto& modal) { return modal.autoOpen; })) return;
    std::string tracker = "<section id=\"rr-mission-tracker\" class=\"mission-tracker" +
        std::string(c.missionChanged ? " mission-changed" : "") + "\"><strong>" + esc(v.location + " / " + v.title) +
        "</strong>";
    if (!v.trackerGoals.empty()) {
        tracker += "<div class=\"mission-goals\" role=\"list\">";
        for (const auto& goal : v.trackerGoals) {
            tracker += "<div role=\"listitem\" class=\"mission-goal " + std::string(goal.complete ? "mission-done" : "mission-pending") +
                "\"><span class=\"mission-checkbox\" aria-label=\"" + (goal.complete ? "Complete" : "Incomplete") + "\">" +
                (goal.complete ? "x" : "") + "</span><div class=\"mission-goal-copy\">" + esc(goal.text);
            if (!goal.detail.empty()) tracker += "<small>" + esc(goal.detail) + "</small>";
            tracker += "</div></div>";
        }
        tracker += "</div>";
        if (v.stepId != "ore" && v.stepId != "orbit" && v.stepId != "complete")
            tracker += "<p class=\"mission-next\">" + esc(v.instruction) + "</p>";
    } else {
        tracker += "<p class=\"mission-next\">" + esc(v.instruction) + "</p><p class=\"mission-progress\">";
        for (std::size_t i = 0; i < v.progress.size() && i < 2; ++i) tracker += (i ? " / " : "") + esc(v.progress[i]);
        tracker += "</p>";
    }
    tracker += "<div class=\"mission-tracker-actions\">" + button("Missions", "expedition:missions");
    if (e.coursePlayerSelected && e.course.targetBodyId != v.targetId && !v.targetId.empty())
        tracker += button("Return to mission", "expedition:follow_mission");
    tracker += "</div></section>";
    if (s.screen == Screen::Flight || s.screen == Screen::Mining) {
        panel.missionTrackerMarkup = tracker;
    } else if (s.screen == Screen::Hangar && panel.templateKind == PanelTemplateKind::LegacyRaw) {
        // The dock has a full-width shipyard/menu surface. Keep the mission
        // status in a sibling column so it cannot push the departure controls
        // or shipyard cards down the page.
        panel.contentMarkup =
            "<div class=\"expedition-dock-layout\"><aside class=\"expedition-mission-sidebar\">" +
            tracker + "</aside><div class=\"expedition-dock-main\">" +
            panel.contentMarkup + "</div></div>";
    } else {
        panel.contentMarkup = tracker + panel.contentMarkup;
    }
}
} // namespace rocket
