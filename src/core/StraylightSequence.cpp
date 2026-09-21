#include "core/StraylightSequence.h"
#include "core/ContentIds.h"
#include "core/GameState.h"
#include "core/PostSolarSystem.h"
#include <algorithm>

namespace rocket {
namespace {
using Stage = StraylightStage;
bool docked(const GameState& s, std::string_view body) {
    return s.run.expedition.location.bodyId == body && !s.run.expedition.location.siteId.empty() &&
        !s.run.flight.active;
}
void waypoint(GameState& s, std::string_view target) {
    auto& e = s.run.expedition;
    (void)plotSystemCourse(e, s.run.flight, solarSystemDefinition(), target);
    e.coursePlayerSelected = false;
    e.cruise = {};
}
bool allInstalled(const GameState& s) {
    return s.run.expedition.batteries.size() == 6 && std::all_of(s.run.expedition.batteries.begin(), s.run.expedition.batteries.end(),
        [](const auto& b) { return b.owner == BatteryOwner::ArkSlot; });
}
void arrive(GameState& s, const ContentCatalog& catalog) {
    auto& e = s.run.expedition;
    auto& ark = s.meta.ark;
    ark.firstJumpComplete = true;
    ark.condition = ArkCondition::DerelictOperable;
    s.meta.campaignMilestone = CampaignMilestone::FirstArkJumpComplete;
    const auto& roster = ensurePostSolarSystemRoster(s.meta, content::postSolarSystem::aaruVale, s.seed);
    const auto system = systemDefinitionForRoster(roster);
    const auto* home = systemBody(system, "straylight");
    const auto berth = systemDockPosition(*home);
    e.location = {system.id, "straylight", CoordinateFrame::Body,
        {berth.x-home->position.x, berth.y-home->position.y}, {}, 0, home->siteId};
    e.homeBodyId = "straylight";
    e.active = false;
    e.undockReady = false;
    e.course = {};
    e.cruise = {};
    s.run.flight = {};
    restoreSystemLocation(e.location, s.run.flight);
    s.run.flight.active = false;
    s.run.mining = {};
    s.run.planetaryExpedition = {};
    s.meta.navigation.currentSystemId = system.id;
    s.meta.navigation.arkLocationId = "straylight";
    s.meta.navigation.discoveredDestinationIds.clear();
    for (const auto& body : roster.bodies) s.meta.navigation.discoveredDestinationIds.push_back(body.id);
    s.meta.navigation.selectedDestinationId = roster.primaryBodyId;
    s.screen = Screen::Hangar;
    s.statusLine = "AARU VALE / Straylight safely docked. Evacuation complete.";
    syncChapterProgress(s, catalog);
}
}

double straylightCinematicDuration(StraylightStage stage) {
    switch (stage) {
    case Stage::Reveal: return 12;
    case Stage::Docking: return 10;
    case Stage::Awakening: return 15;
    case Stage::Boarding: return 10;
    case Stage::Departing: return 12;
    default: return 0;
    }
}
bool straylightCommitted(const GameState& s) {
    return s.meta.straylightStage >= Stage::Awakening &&
        !(s.meta.straylightStage == Stage::Complete && s.run.expedition.location.systemId != content::postSolarSystem::aaruVale);
}
bool straylightOwnsPresentation(const GameState& s) {
    const auto stage = s.meta.straylightStage;
    if (stage == Stage::Complete && s.run.expedition.location.systemId != content::postSolarSystem::aaruVale) return false;
    return (stage >= Stage::Reveal && stage != Stage::Approach && stage != Stage::RetrieveBeacons) ||
        (stage == Stage::RetrieveBeacons && docked(s, "straylight"));
}
bool revealStraylightOnDelivery(GameState& s, const ContentCatalog&) {
    if (s.meta.straylightStage != Stage::Hidden || !s.run.expedition.travelInitialized) return false;
    const auto& batteries = s.run.expedition.batteries;
    if (std::none_of(batteries.begin(), batteries.end(), [](const auto& b) {
        return b.id == "triton" && (b.owner == BatteryOwner::EarthStorage || b.owner == BatteryOwner::ArkSlot);
    })) return false;
    if (std::none_of(s.run.expedition.artifacts.begin(),s.run.expedition.artifacts.end(),[](const auto& a) {
        return a.artifact.originDestinationId == "triton" && a.completed;
    })) return false;
    s.meta.straylightStage = Stage::RevealPending;
    s.meta.campaignMilestone = CampaignMilestone::ArkDiscovered;
    s.meta.ark.condition = ArkCondition::DerelictOperable;
    s.run.expedition.straylightRevealed = true;
    s.meta.straylightDiscoveryAcknowledged = false;
    waypoint(s, "straylight");
    s.statusLine = "CONTACT RESOLVED - STRAYLIGHT / WAYPOINT SET";
    return true;
}
std::optional<CampaignObjective> straylightObjective(const GameState& s) {
    const auto stage = s.meta.straylightStage;
    if (stage == Stage::Hidden || (stage == Stage::Complete && !straylightCommitted(s))) return std::nullopt;
    if (stage < Stage::RetrieveBeacons)
        return CampaignObjective{CampaignObjectiveKind::Mission, "straylight", {}, "Approach Straylight",
            "Contact resolved beyond Neptune. Dock with the illuminated corridor.", 0};
    if (stage <= Stage::ConfirmOnline) {
        for (const auto& b : s.run.expedition.batteries)
            if (b.owner == BatteryOwner::EarthStorage)
                return CampaignObjective{CampaignObjectiveKind::Mission, "earth", {}, "Collect the stored beacons",
                    "Load the beacons at Earth, then return to Straylight.", 0};
        for (const auto& b : s.run.expedition.batteries) {
            if (b.owner == BatteryOwner::Wreck)
                return CampaignObjective{CampaignObjectiveKind::RecoverArtifact, "wreck:"+std::to_string(b.wreckId), {},
                    "Recover the missing beacon", "Salvage the marked wreck, then bring the beacons to Straylight.", b.wreckId};
            if (b.owner == BatteryOwner::Site)
                return CampaignObjective{CampaignObjectiveKind::Mission, b.id, {}, "Recover the remaining beacon",
                    "All six beacons must reach Straylight.", 0};
        }
        return CampaignObjective{CampaignObjectiveKind::Mission, "straylight", {},
            allInstalled(s) ? "Bring Straylight online" : "Bring the beacons to Straylight",
            allInstalled(s) ? "Six beacons installed. Activation commits you to evacuation and departure." : "Dock and install the carried beacons.", 0};
    }
    return CampaignObjective{CampaignObjectiveKind::Mission, {}, {}, "Straylight evacuation",
        "Complete boarding, secure the Ark, and depart for Aaru Vale.", 0};
}
bool reconcileStraylightSequence(GameState& s, const ContentCatalog& catalog) {
    bool changed = false;
    auto& stage = s.meta.straylightStage;
    if (stage == Stage::Hidden && s.run.expedition.travelInitialized) {
        if (s.meta.ark.firstJumpComplete) { stage = Stage::Complete; changed = true; }
        else if (s.run.expedition.arkActivated) { stage = Stage::Online; changed = true; }
        else if (s.run.expedition.straylightRevealed) {
            stage = docked(s, "straylight") ? Stage::FirstContact : Stage::RevealPending;
            waypoint(s, "straylight"); changed = true;
        } else changed = revealStraylightOnDelivery(s, catalog);
    }
    if (stage != Stage::Hidden && stage != Stage::Complete && s.storyBriefing.pending != StoryBriefingId::None) {
        s.storyBriefing = {};
        s.screen = docked(s,"earth") ? Screen::Hangar : s.run.mining.active ? Screen::Mining : Screen::Flight;
        changed = true;
    }
    return changed;
}
bool finishStraylightCinematic(GameState& s, const ContentCatalog& catalog) {
    auto& stage = s.meta.straylightStage;
    switch (stage) {
    case Stage::Reveal: stage = Stage::Invitation; break;
    case Stage::Docking: stage = Stage::FirstContact; break;
    case Stage::Awakening: stage = Stage::Online; break;
    case Stage::Boarding: stage = Stage::Boarded; break;
    case Stage::Departing: arrive(s, catalog); stage = Stage::Arrived; break;
    default: return false;
    }
    return true;
}
bool applyStraylightAction(GameState& s, const ContentCatalog& catalog, std::string_view action) {
    auto& stage = s.meta.straylightStage;
    auto& e = s.run.expedition;
    if (action == "skip") return finishStraylightCinematic(s, catalog);
    if (action == "invitation" && stage == Stage::Invitation) {
        stage = Stage::Approach;
        s.meta.straylightDiscoveryAcknowledged = true;
    } else if (action == "retrieve" && stage == Stage::FirstContact && docked(s,"straylight")) {
        stage = Stage::RetrieveBeacons; waypoint(s,"earth");
    } else if (action == "collect" && stage == Stage::RetrieveBeacons && docked(s,"earth")) {
        bool loaded = false;
        for (const auto& b : e.batteries)
            if (b.owner == BatteryOwner::EarthStorage) loaded |= loadEarthBattery(e,b.id) == ExpeditionResult::Applied;
        if (!loaded) return false;
        waypoint(s, straylightObjective(s)->targetId);
    } else if (action == "install" && stage == Stage::RetrieveBeacons && docked(s,"straylight")) {
        bool installed = false;
        for (const auto& b : e.batteries)
            if (b.owner == BatteryOwner::Ship) installed |= installArkBattery(e,b.id) == ExpeditionResult::Applied;
        if (!installed) return false;
    } else if (action == "prepare_online" && stage == Stage::RetrieveBeacons && docked(s,"straylight") && allInstalled(s)) {
        stage = Stage::ConfirmOnline;
    } else if (action == "cancel_online" && stage == Stage::ConfirmOnline) {
        stage = Stage::RetrieveBeacons;
    } else if (action == "online" && stage == Stage::ConfirmOnline && docked(s,"straylight") && allInstalled(s)) {
        if (activateStraylight(e) != ExpeditionResult::Applied) return false;
        stage = Stage::Awakening;
        e.active = false;
        e.undockReady = false;
        e.cruise = {}; e.course = {};
        s.run.flight.active = false;
    } else if (action == "coordinate" && stage == Stage::Online) {
        stage = Stage::EvacuationBriefing;
    } else if (action == "boarding" && stage == Stage::EvacuationBriefing) {
        stage = Stage::Boarding;
    } else if (action == "secure" && stage == Stage::Boarded) {
        stage = Stage::Secured;
    } else if (action == "depart" && stage == Stage::Secured) {
        stage = Stage::Departing;
    } else if (action == "arrived" && stage == Stage::Arrived) {
        stage = Stage::Complete;
    } else return false;
    return true;
}
}
