#include "core/MissionGuidance.h"
#include "core/ArtifactProgression.h"
#include "core/ScenarioSystem.h"
#include "core/SolarProgression.h"
#include "core/StraylightSequence.h"
#include "core/Tuning.h"
#include "core/GameUi.h"
#include <algorithm>

namespace rocket {
std::string missionSectorName(std::string_view id) {
    const auto* zone = planetLandingZone(id);
    return zone ? "Sector " + std::to_string(zone->sectorIndex + 1) : "mission sector";
}
std::string firstMoonMissionInstructions(const GameState& s, const ContentCatalog&) {
    return "Land in " + missionSectorName(artifactSectorForBody(s, "solar", "moon")) +
        ", marked MISSION LANDING SITE. Deliver " + std::to_string(tuning::research::prospectorCommonOreGoal) +
        " Common Ore to your ship, then use the surface scanner to locate the artifact. Recover it and bring it aboard.";
}
MissionView missionView(const GameState& s, const ContentCatalog& catalog, std::string_view id,
    const FlightRunState* liveFlight, bool surveyed) {
    MissionView v;
    const auto& e = s.run.expedition;
    if (id == "straylight") {
        const auto objective = straylightObjective(s);
        if (!objective) return v;
        v.available = true; v.id = "straylight"; v.location = "STRAYLIGHT";
        v.title = "Awaken the Ark"; v.stepId = std::to_string(static_cast<int>(s.meta.straylightStage));
        v.instruction = objective->title; v.purpose = objective->detail; v.targetId = objective->targetId;
        v.kind = objective->kind; v.wreckId = objective->wreckId;
        using Stage = StraylightStage;
        const auto stage = s.meta.straylightStage;
        if (stage == Stage::FirstContact) v.instruction = "Accept the beacon retrieval mission";
        if (stage == Stage::Online) v.instruction = "Coordinate evacuation";
        if (stage == Stage::EvacuationBriefing) v.instruction = "Complete boarding";
        if (stage == Stage::Boarded) v.instruction = "Secure the Ark";
        if (stage == Stage::Secured) v.instruction = "Depart for Aaru Vale";
        if (stage == Stage::Arrived) v.instruction = "Acknowledge arrival at Aaru Vale";
        v.complete = stage == Stage::Complete;
        if (v.complete) v.instruction = "Arrived at Aaru Vale";
        if (stage >= Stage::RetrieveBeacons) {
            const auto installed = std::count_if(e.batteries.begin(), e.batteries.end(), [](const auto& b) { return b.owner == BatteryOwner::ArkSlot; });
            v.progress = {"Beacons installed " + std::to_string(installed) + "/6"};
            v.requirements = {{"Install all six beacons", installed == 6}, {"Bring Straylight online", stage >= Stage::Awakening},
                {"Coordinate evacuation", stage >= Stage::EvacuationBriefing}, {"Complete boarding", stage >= Stage::Boarded},
                {"Secure the Ark", stage >= Stage::Secured}, {"Depart for Aaru Vale", stage >= Stage::Arrived}};
        } else v.requirements = {{"Dock with Straylight and learn its purpose", stage >= Stage::FirstContact}};
        return v;
    }
    const auto* m = solarMissionForBody(catalog, id);
    if (!m) return v;
    const BeaconBatteryState* battery = nullptr;
    for (const auto& b : e.batteries) if (b.id == m->bodyId) battery = &b;
    const bool physicallyRecovered = battery && battery->owner != BatteryOwner::Site;
    if (!physicallyRecovered && (!solarMissionAvailable(s, *m) || !solarBodyRevealed(s, catalog, m->bodyId))) return v;
    const auto* body = systemBody(solarSystemDefinition(), m->bodyId);
    const auto* def = catalog.findScenario(m->scenarioId);
    if (!body || !def) return v;
    v.available = true; v.id = m->bodyId; v.targetId = m->bodyId; v.location = body->name;
    v.title = "Ore and artifact recovery"; v.optional = m->optional;
    v.complete = solarMissionClaimed(s, catalog, *m); v.artifactId = m->artifactId;
    v.sectorId = artifactSectorForBody(s, e.location.systemId.empty() ? "solar" : e.location.systemId, m->bodyId);
    v.purpose = m->bodyId == "moon" ? firstMoonMissionInstructions(s, catalog) : "Complete the local mission and bring its artifact aboard your ship.";
    int delivered = 0, required = 0;
    bool prerequisites = true;
    for (const auto& step : def->steps) {
        const auto p = scenarioObjectivePresentation(s, catalog, m->scenarioId, step.id);
        if (step.id == m->claimStepId) { v.reward = p.rewardPreview; continue; }
        const bool done = p.state == ScenarioStepState::Complete || p.state == ScenarioStepState::ReadyToClaim;
        if (step.id != m->acceptanceStepId) {
            const auto requirement = step.completionEvent == ScenarioEventKind::SafeMaterialDelivered && step.eventTargetId == "common"
                ? "Deliver " + std::to_string(p.required) + " Common Ore to your ship"
                : p.goal.empty() ? p.detail : p.goal;
            v.requirements.push_back({requirement, done});
            prerequisites &= done;
        }
        if (step.completionEvent == ScenarioEventKind::SafeMaterialDelivered && step.eventTargetId == "common") {
            required = p.required; delivered = p.current;
        }
    }
    if (!required) v.title = "Artifact recovery";
    const auto claim = scenarioObjectivePresentation(s, catalog, m->scenarioId, m->claimStepId);
    bool aboard = battery && battery->owner == BatteryOwner::Ship;
    const bool banked = battery && (battery->owner == BatteryOwner::EarthStorage || battery->owner == BatteryOwner::ArkSlot);
    const bool wreck = battery && battery->owner == BatteryOwner::Wreck;
    bool revealed = false, exposed = false, tethered = false;
    const auto inspectArtifact = [&](const MiningArtifactObject& a) {
        if (!a.present) return;
        revealed |= a.revealed; exposed |= a.state == MiningArtifactState::Loose;
        tethered |= a.tethered; aboard |= a.state == MiningArtifactState::Delivered;
    };
    const auto inspect = [&](const MiningRunState& mining) {
        inspectArtifact(mining.artifact);
        for (const auto& layer : mining.depthLayers) inspectArtifact(layer.artifact);
    };
    // Physical ownership takes precedence over historical site delivery snapshots.
    if (!battery || battery->owner == BatteryOwner::Site) {
        if (s.run.mining.bodyId == m->bodyId) inspect(s.run.mining);
        for (const auto& site : e.sites) if (site.bodyId == m->bodyId) inspect(site.mining);
    }
    v.sectorKnown = (e.location.bodyId == m->bodyId && surveyed) ||
        std::any_of(e.sites.begin(), e.sites.end(), [&](const auto& site) { return site.bodyId == m->bodyId && site.orbital.surveyComplete; });
    v.artifactLocated = revealed || aboard || banked || wreck;
    if (required) v.progress.push_back("Ore delivered " + std::to_string(std::min(delivered, required)) + "/" + std::to_string(required));
    const std::string artifactState = banked ? "banked" : wreck ? "in wreck" : aboard ? "aboard ship" : tethered ? "tethered - return to ship" : exposed ? "exposed" : revealed ? "located" : !prerequisites ? (required ? "after ore delivery" : "after mission requirements") : "not recovered";
    v.progress.push_back("Artifact: " + artifactState);
    v.requirements.push_back({"Recover the artifact and bring it aboard", aboard || banked});
    v.requirements.push_back({"Claim mission reward", v.complete});
    const auto set = [&](std::string step, std::string instruction) { v.stepId = std::move(step); v.instruction = std::move(instruction); };
    if (wreck) {
        v.kind = CampaignObjectiveKind::RecoverArtifact; v.wreckId = battery->wreckId;
        v.targetId = "wreck:" + std::to_string(v.wreckId);
        set("wreck", "Recover " + body->name + " artifact from Wreck " + std::to_string(v.wreckId));
        if (!courseWreck(e, v.targetId)) { v.kind = CampaignObjectiveKind::RecoveryUnavailable; v.targetId.clear(); v.instruction = "Artifact recovery unavailable - wreck missing"; }
        return v;
    }
    if (claim.state == ScenarioStepState::ReadyToClaim && !v.complete) {
        set("claim", "Claim your " + body->name + " mission reward");
        v.action = ui::actions::scenarioAction(m->scenarioId, m->claimStepId, static_cast<int>(ScenarioActionKind::ClaimReward));
        return v;
    }
    if (aboard && !banked) {
        v.kind = CampaignObjectiveKind::SecureArtifact; v.targetId = "earth";
        set("bank", "Return to Earth to bank the " + body->name + " artifact"); return v;
    }
    if (v.complete) { set("complete", "Mission complete"); return v; }
    const auto& f = liveFlight ? *liveFlight : s.run.flight;
    if (e.location.bodyId != m->bodyId) { set("travel", "Reach " + body->name + " and establish orbit"); return v; }
    if (s.screen == Screen::Mining && s.run.mining.active) {
        if (v.sectorKnown && !e.location.siteId.ends_with(v.sectorId))
            set("wrong_site", "Return to orbit and land in " + missionSectorName(v.sectorId));
        else if (required && delivered < required) set("ore", "Deliver Common Ore to your ship");
        else if (!prerequisites) {
            const auto objective = solarMissionObjectiveForBody(s, catalog, m->bodyId);
            set(objective.stepId, objective.goal.empty() ? objective.detail : objective.goal);
        } else if (!revealed) set("scan_artifact", "Pulse the surface scanner to locate the artifact");
        else if (tethered) set("carry", "Bring the tethered artifact aboard your ship");
        else if (m->bodyId == "moon") set("recover", "Exit the rig; clear the crevice, tether the artifact, and return it to the ship");
        else set("recover", "Excavate and tether the artifact, then bring it aboard");
        return v;
    }
    if (f.mode == FlightMode::Landing) { set("landing", f.landing.departureActive ? "Climb clear of the surface" : "Touch down and deploy the mining rig"); return v; }
    if (!f.orbit.captured) set("orbit", "Establish a safe orbit around " + body->name);
    else if (!v.sectorKnown) set("survey", "Scan a landing sector");
    else { set("land", "Land at the mission site in " + missionSectorName(v.sectorId)); }
    return v;
}
MissionView trackedMissionView(const GameState& s, const ContentCatalog& c, const FlightRunState* f, bool surveyed) {
    if (straylightObjective(s)) return missionView(s, c, "straylight", f, surveyed);
    if (!s.run.expedition.trackedMissionId.empty()) {
        auto v = missionView(s, c, s.run.expedition.trackedMissionId, f, surveyed);
        if (v.available && (!v.complete || v.kind == CampaignObjectiveKind::RecoverArtifact)) return v;
    }
    for (const auto& b : s.run.expedition.batteries) if (b.owner == BatteryOwner::Wreck || b.owner == BatteryOwner::Ship) {
        auto v = missionView(s, c, b.id, f, surveyed);
        if (v.available && (!v.complete || b.owner == BatteryOwner::Wreck)) return v;
    }
    if (const auto* m = nextSolarMission(s, c)) return missionView(s, c, m->bodyId, f, surveyed);
    return {};
}
std::vector<MissionView> missionLog(const GameState& s, const ContentCatalog& c) {
    std::vector<MissionView> views;
    for (const auto& m : c.solarMissions) { auto v = missionView(s, c, m.bodyId); if (v.available) views.push_back(std::move(v)); }
    auto ark = missionView(s, c, "straylight"); if (ark.available) views.push_back(std::move(ark));
    return views;
}
bool reconcileTrackedMission(GameState& s, const ContentCatalog& c) {
    const auto view = trackedMissionView(s, c);
    if (s.run.expedition.trackedMissionId == view.id) return false;
    s.run.expedition.trackedMissionId = view.id; return true;
}
}
