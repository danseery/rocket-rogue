#include "core/MissionGuidance.h"
#include "core/ArtifactProgression.h"
#include "core/ScenarioSystem.h"
#include "core/SolarProgression.h"
#include "core/StraylightSequence.h"
#include "core/Tuning.h"
#include "core/GameUi.h"
#include <algorithm>
#include <tuple>

namespace rocket {
int arrivalTutorialIndex(std::string_view body) { return body == "moon" ? 0 : body == "mars" ? 1 : -1; }
bool arrivalBriefingRequired(const GameState& s, std::string_view body) {
    const int i = arrivalTutorialIndex(body);
    return i >= 0 && s.run.expedition.arrivalTutorials[i].landed && !s.run.expedition.arrivalTutorials[i].acknowledged;
}
bool updateArrivalTutorial(GameState& s, const ContentCatalog&, const FlightRunState& f, bool surveyed, const OrbitalSiteProgress* orbital) {
    auto& e = s.run.expedition;
    const int i = arrivalTutorialIndex(e.location.bodyId);
    if (i < 0 || !e.travelInitialized) return false;
    auto& t = e.arrivalTutorials[i];
    const auto before = std::tuple(t.orbit, t.scanned, t.drilled);
    if (f.mode == FlightMode::Orbit && f.orbit.captured) t.orbit = true;
    const auto sector = artifactSectorForBody(s, "solar", e.location.bodyId);
    if (e.selectedOrbitZone == sector) {
        t.scanned |= surveyed;
        if (!t.landed && orbital) t.drilled |= orbital->laserComplete;
    }
    return before != std::tuple(t.orbit, t.scanned, t.drilled);
}
void recordTutorialTouchdown(GameState& s, const ContentCatalog&) {
    auto& e = s.run.expedition;
    const int i = arrivalTutorialIndex(e.location.bodyId);
    if (i < 0 || !e.location.siteId.ends_with(artifactSectorForBody(s, "solar", e.location.bodyId))) return;
    auto& t = e.arrivalTutorials[i];
    t.landed = true;
    if (i == 1 && !t.drilled) t.drillBypassed = true;
}
void migrateArrivalTutorials(GameState& s, const ContentCatalog& c) {
    auto& e = s.run.expedition;
    if (e.arrivalTutorialsLoaded) return;
    for (const auto body : {"moon", "mars"}) {
        auto& t = e.arrivalTutorials[arrivalTutorialIndex(body)];
        const auto* m = solarMissionForBody(c, body);
        if (!m) continue;
        bool visited = solarMissionClaimed(s, c, *m) || missionArtifact(s, m->scenarioId, m->claimStepId);
        for (std::size_t d = 0; d < c.destinations.size(); ++d)
            if (c.destinations[d].id == body && d < s.meta.destinationLandings.size())
                visited |= s.meta.destinationLandings[d] > 0;
        visited |= s.run.mining.bodyId == body && s.run.mining.active;
        visited |= e.location.bodyId == body && s.run.flight.phase == FlightPhase::Landed;
        for (const auto& step : c.findScenario(m->scenarioId)->steps)
            if (step.completionEvent == ScenarioEventKind::SafeMaterialDelivered)
                visited |= scenarioObjectivePresentation(s, c, m->scenarioId, step.id).current > 0;
        for (const auto& b : e.batteries) if (b.id == body && b.owner != BatteryOwner::Site) visited = true;
        for (const auto& site : e.sites) if (site.bodyId == body) {
            // Orbital previews also contain an active mining template. Only
            // actual surface play is evidence of a previous visit.
            visited |= site.mining.elapsedSeconds > 0.0 || site.surface.miningRunUsed;
            if (site.siteId.ends_with(artifactSectorForBody(s, "solar", body))) {
                t.scanned |= site.orbital.surveyComplete;
                t.drilled |= site.orbital.laserComplete;
            }
        }
        t.orbit = t.scanned || visited;
        t.landed = t.acknowledged = visited;
        t.drillBypassed = std::string_view(body) == "mars" && visited && !t.drilled;
    }
    e.arrivalTutorialsLoaded = true;
}
std::string missionSectorName(std::string_view id) {
    const auto* zone = planetLandingZone(id);
    return zone ? "Sector " + std::to_string(zone->sectorIndex + 1) : "mission sector";
}
std::string servicingDockName(std::string_view id) {
    if (id == "earth") return "Earth dock";
    if (id == "straylight") return "Straylight dock";
    return std::string(id) + " dock";
}
std::string firstMoonMissionInstructions(const GameState& s, const ContentCatalog&) {
    return "Land in " + missionSectorName(artifactSectorForBody(s, "solar", "moon")) +
        ", marked MISSION LANDING SITE. Establish Moon orbit, scan this sector, then land. Mission Control will brief you on recovery after touchdown.";
}
MissionView missionView(const GameState& s, const ContentCatalog& catalog, std::string_view id,
    const FlightRunState* liveFlight, bool surveyed) {
    MissionView v;
    const auto& e = s.run.expedition;
    if (id.starts_with("artifact:")) {
        for (const auto& a : e.artifacts) {
            if (id != "artifact:" + a.key) continue;
            v.available=true; v.id=id; v.title="Artifact recovery"; v.artifactId=a.artifact.id;
            v.location=a.artifact.originDestinationId; v.complete=a.completed; v.targetId=a.requiredDockId;
            const bool banked=a.owner==ArtifactCustody::Banked;
            const std::string dock = servicingDockName(a.requiredDockId);
            v.requirements={{"Collect Artifact",a.owner!=ArtifactCustody::Wreck},
                {"Complete Mission",a.completed}};
            v.trackerGoals={{"Collect Artifact",a.owner!=ArtifactCustody::Wreck,
                a.owner==ArtifactCustody::Wreck ? "Recover from Wreck " + std::to_string(a.wreckId) : "Aboard ship"},
                {"Complete Mission",a.completed,
                a.completed ? "Mission complete" : banked ? "At the " + dock : "Return to the " + dock}};
            if (a.owner==ArtifactCustody::Wreck) {
                v.kind=CampaignObjectiveKind::RecoverArtifact; v.wreckId=a.wreckId;
                v.targetId="wreck:"+std::to_string(a.wreckId); v.instruction="Collect Artifact from Wreck "+std::to_string(a.wreckId);
                if (!courseWreck(e,v.targetId)) {v.kind=CampaignObjectiveKind::RecoveryUnavailable;v.targetId.clear();v.instruction="Artifact collection unavailable - wreck missing";}
            } else if (!banked) {
                v.kind=CampaignObjectiveKind::SecureArtifact;v.instruction="Return to the "+dock+" to complete the mission";
                v.purpose="Aboard / unsecured. A crash transfers this artifact to your wreck.";
            } else {
                v.instruction=a.completed ? "Mission complete" : "Complete Mission at the "+dock;
                if (artifactHandInAvailable(s,a)) v.action=a.scenarioId.empty() ? "expedition:artifact_handin:"+a.key :
                    ui::actions::scenarioAction(a.scenarioId,a.stepId,static_cast<int>(ScenarioActionKind::ClaimReward));
            }
            return v;
        }
        return v;
    }
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
    const auto* custody = missionArtifact(s,m->scenarioId,m->claimStepId);
    const bool physicallyRecovered = custody || (battery && battery->owner != BatteryOwner::Site);
    if (!physicallyRecovered && (!solarMissionAvailable(s, *m) || !solarBodyRevealed(s, catalog, m->bodyId))) return v;
    const auto* body = systemBody(solarSystemDefinition(), m->bodyId);
    const auto* def = catalog.findScenario(m->scenarioId);
    if (!body || !def) return v;
    v.available = true; v.id = m->bodyId; v.targetId = m->bodyId; v.location = body->name;
    v.title = "Ore and artifact recovery"; v.optional = m->optional;
    v.complete = solarMissionClaimed(s, catalog, *m); v.artifactId = m->artifactId;
    v.sectorId = artifactSectorForBody(s, e.location.systemId.empty() ? "solar" : e.location.systemId, m->bodyId);
    const std::string dock = servicingDockName("earth");
    v.purpose = m->bodyId == "moon" ? firstMoonMissionInstructions(s, catalog) : "Collect the artifact, then complete the mission at the " + dock + ". Artifacts aboard are at risk until then.";
    if (m->bodyId == "mars") v.purpose = "Mars's artifact is underground. Scan the mission sector, hold Drill to prepare a shaft, then land and use the surface scanner to locate it. Return the ore and artifact to your ship, then complete the mission at Earth dock.";
    int delivered = 0, required = 0;
    bool prerequisites = true;
    for (const auto& step : def->steps) {
        const auto p = scenarioObjectivePresentation(s, catalog, m->scenarioId, step.id);
        if (step.id == m->claimStepId) { v.reward = p.rewardPreview; continue; }
        const bool done = p.state == ScenarioStepState::Complete || p.state == ScenarioStepState::ReadyToClaim;
        if (step.id != m->acceptanceStepId) {
            const auto requirement = step.completionEvent == ScenarioEventKind::SafeMaterialDelivered && step.eventTargetId == "common"
                ? "Collect Common Ore"
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
    bool aboard = custody ? custody->owner == ArtifactCustody::Ship : battery && battery->owner == BatteryOwner::Ship;
    const bool banked = custody ? custody->owner == ArtifactCustody::Banked : battery && (battery->owner == BatteryOwner::EarthStorage || battery->owner == BatteryOwner::ArkSlot);
    const bool wreck = custody ? custody->owner == ArtifactCustody::Wreck : battery && battery->owner == BatteryOwner::Wreck;
    const auto wreckId = custody ? custody->wreckId : battery ? battery->wreckId : 0;
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
    if (!custody && (!battery || battery->owner == BatteryOwner::Site)) {
        if (s.run.mining.bodyId == m->bodyId) inspect(s.run.mining);
        for (const auto& site : e.sites) if (site.bodyId == m->bodyId) inspect(site.mining);
    }
    const auto& f = liveFlight ? *liveFlight : s.run.flight;
    const auto& mining = s.run.mining;
    const bool surfaceBodyMatches = mining.bodyId == m->bodyId ||
        (mining.bodyId.empty() && mining.postSolarSystemId.empty() &&
            (mining.scenarioId == m->scenarioId ||
                (mining.scenarioId.empty() && mining.destinationId == m->bodyId)));
    const bool miningHere = s.screen == Screen::Mining && mining.active && surfaceBodyMatches;
    const bool atBody = miningHere || e.location.bodyId == m->bodyId;
    v.sectorKnown = (atBody && surveyed) ||
        std::any_of(e.sites.begin(), e.sites.end(), [&](const auto& site) { return site.bodyId == m->bodyId && site.orbital.surveyComplete; });
    v.artifactLocated = revealed || aboard || banked || wreck;
    if (required) v.progress.push_back("Common Ore collected " + std::to_string(std::min(delivered, required)) + "/" + std::to_string(required));
    const std::string artifactState = banked ? "secured at dock" : wreck ? "in wreck" : aboard ? "aboard ship" : tethered ? "return to ship" : exposed ? "collect artifact" : revealed ? "collect artifact" : !prerequisites ? (required ? "after Common Ore collection" : "after mission requirements") : "not collected";
    v.progress.push_back("Artifact: " + artifactState);
    v.requirements.push_back({"Collect Artifact", aboard || banked});
    v.requirements.push_back({"Complete Mission", v.complete});
    const bool orbitComplete = miningHere || v.sectorKnown || physicallyRecovered || delivered > 0 || v.complete ||
        (atBody && (f.orbit.captured || f.mode == FlightMode::Landing));
    if (required) v.trackerGoals.push_back({"Collect Common Ore " + std::to_string(std::min(delivered, required)) +
        "/" + std::to_string(required), delivered >= required, delivered >= required ? "Aboard ship" : "Return to ship"});
    // Special prerequisites remain contextual instructions/actions, not a
    // repeated navigation or commissioning checklist in the compact tracker.
    v.trackerGoals.push_back({"Collect Artifact", aboard || banked,
        wreck ? "Recover from Wreck " + std::to_string(wreckId) :
        banked || aboard ? "Aboard ship" : tethered ? "Return to ship" :
        exposed || revealed ? "Collect Artifact" :
        !prerequisites ? (required && delivered < required ? "After Common Ore collection" : "After mission requirements") : "Use the surface scanner"});
    v.trackerGoals.push_back({"Return to " + dock, banked || v.complete,
        v.complete ? "Mission complete" : banked ? "Ready to complete" : aboard ? "Complete the mission at the dock" : "Bring the artifact aboard first"});
    v.recoveryGoals = v.trackerGoals;
    const int tutorial = arrivalTutorialIndex(m->bodyId);
    if (tutorial >= 0) {
        const auto& t = e.arrivalTutorials[tutorial];
        const bool missionSurvey = (atBody && surveyed && e.selectedOrbitZone == v.sectorId) ||
            std::any_of(e.sites.begin(), e.sites.end(), [&](const auto& site) {
                return site.bodyId == m->bodyId && site.siteId.ends_with(v.sectorId) && site.orbital.surveyComplete;
            });
        // A scan elsewhere can reveal the mission bearing, but only a scan
        // of the actual mission sector completes the tutorial checklist.
        v.arrivalGoals = {{"Establish " + body->name + " orbit", t.orbit || (atBody && f.orbit.captured)},
            {"Scan landing site", t.scanned || missionSurvey, missionSectorName(v.sectorId)}};
        if (tutorial == 1) v.arrivalGoals.push_back({"Prepare shaft with orbital laser", t.drilled,
            t.drillBypassed ? "Bypassed - use surface tools" : "Hold Drill in the mission sector"});
        v.arrivalGoals.push_back({"Land", t.landed, missionSectorName(v.sectorId)});
        v.arrivalStage = !t.acknowledged && !miningHere && !physicallyRecovered && !v.complete && delivered == 0;
        if (v.arrivalStage) v.trackerGoals = v.arrivalGoals;
    }
    v.title = v.arrivalStage ? "Arrival" : "Recovery";
    if (v.arrivalStage) v.purpose = m->bodyId == "moon" ? firstMoonMissionInstructions(s, catalog)
        : "Establish Mars orbit, scan the mission sector, hold Drill to prepare a shaft, then land. Mars's artifact is underground. Mission Control will brief you on recovery after touchdown.";
    else if (tutorial >= 0) v.purpose = std::string(tutorial == 1 ? "Mars's artifact is underground. " : "") +
        "Collect Common Ore and return it to your ship, then locate and collect the artifact. Return with it to Earth dock and choose Complete Mission.";
    const auto set = [&](std::string step, std::string instruction) { v.stepId = std::move(step); v.instruction = std::move(instruction); };
    if (wreck) {
        v.kind = CampaignObjectiveKind::RecoverArtifact; v.wreckId = wreckId;
        v.targetId = "wreck:" + std::to_string(v.wreckId);
        set("wreck", "Collect " + body->name + " Artifact from Wreck " + std::to_string(v.wreckId));
        if (!courseWreck(e, v.targetId)) { v.kind = CampaignObjectiveKind::RecoveryUnavailable; v.targetId.clear(); v.instruction = "Artifact collection unavailable - wreck missing"; }
        return v;
    }
    if (claim.state == ScenarioStepState::ReadyToClaim && !v.complete) {
        v.targetId = "earth";
        set("claim", "Complete " + body->name + " mission at Earth dock");
        v.action = ui::actions::scenarioAction(m->scenarioId, m->claimStepId, static_cast<int>(ScenarioActionKind::ClaimReward));
        return v;
    }
    if (aboard && !banked) {
        v.kind = CampaignObjectiveKind::SecureArtifact; v.targetId = "earth";
        set("dock", "Return to the " + dock + " to complete the mission"); return v;
    }
    if (banked && !v.complete) {
        v.targetId = "earth"; set("handin", "Complete Mission at the " + dock); return v;
    }
    if (v.complete) { set("complete", "Mission complete"); return v; }
    if (v.arrivalStage && e.arrivalTutorials[tutorial].landed) {
        set("arrival_briefing", "Arrival complete - Continue to the recovery briefing"); return v;
    }
    if (!atBody) { set("travel", orbitComplete ? "Return to " + body->name : "Reach " + body->name + " and establish orbit"); return v; }
    if (miningHere) {
        if (v.sectorKnown && e.location.bodyId == m->bodyId && !e.location.siteId.empty() && !e.location.siteId.ends_with(v.sectorId))
            set("wrong_site", "Return to orbit and land in " + missionSectorName(v.sectorId));
        else if (required && delivered < required) set("ore", m->bodyId == "mars" ? "Collect Common Ore, then return to ship. The artifact is underground; use the surface scanner to locate it." : "Collect Common Ore, then return to ship");
        else if (!prerequisites) {
            const auto objective = solarMissionObjectiveForBody(s, catalog, m->bodyId);
            set(objective.stepId, objective.goal.empty() ? objective.detail : objective.goal);
        } else if (!revealed) set("scan_artifact", "Pulse the surface scanner to locate the artifact");
        else if (tethered) set("carry", "Return to ship with the artifact");
        else if (m->bodyId == "moon") set("recover", "Exit the rig; clear the crevice, then Collect Artifact");
        else set("recover", m->bodyId == "mars" ? "Excavate underground to the scanner signal, then Collect Artifact and return to ship" : "Excavate the site, then Collect Artifact");
        return v;
    }
    if (f.mode == FlightMode::Landing) {
        set("landing", f.landing.departureActive ? "Climb clear of the surface" :
            f.phase == FlightPhase::Landed ? "Deploy the mining rig" : "Touch down and deploy the mining rig");
        return v;
    }
    if (!f.orbit.captured) set("orbit", "Establish a safe orbit around " + body->name);
    else if (v.arrivalStage && e.selectedOrbitZone != v.sectorId)
        set("mission_sector", "Fly to " + missionSectorName(v.sectorId) + " and scan the landing site");
    else if (!v.sectorKnown || (v.arrivalStage && !v.arrivalGoals[1].complete)) set("survey", "Scan a landing sector");
    else if (m->bodyId == "mars") {
        const PersistentSiteState* site = nullptr;
        for (const auto& candidate : e.sites)
            if (candidate.bodyId == m->bodyId && candidate.siteId.ends_with(v.sectorId)) site = &candidate;
        if (e.selectedOrbitZone != v.sectorId) set("mission_sector", "Fly to " + missionSectorName(v.sectorId) + " to scan and prepare a shaft");
        else if (site && site->orbital.laserBlocked) set("land", "Protected terrain blocks the shaft. Land and use surface tools to reach the underground artifact");
        else if (site && site->orbital.laserComplete) set("land", "Shaft ready. Land and use the surface scanner to locate the underground artifact");
        else set("drill", "Hold Drill to prepare a shaft, then land. Mars's artifact is underground");
    } else { set("land", "Land at the mission site in " + missionSectorName(v.sectorId)); }
    return v;
}
MissionView trackedMissionView(const GameState& s, const ContentCatalog& c, const FlightRunState* f, bool surveyed) {
    for (const auto& m : c.solarMissions) {
        const auto* a=missionArtifact(s,m.scenarioId,m.claimStepId);
        if (a && !a->completed) return missionView(s,c,m.bodyId,f,surveyed);
    }
    for (const auto& a : s.run.expedition.artifacts)
        if (!a.completed) return missionView(s,c,"artifact:"+a.key,f,surveyed);
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
    for (const auto& a : s.run.expedition.artifacts) if (!a.key.starts_with("solar:"))
        views.push_back(missionView(s,c,"artifact:"+a.key));
    auto ark = missionView(s, c, "straylight"); if (ark.available) views.push_back(std::move(ark));
    return views;
}
bool reconcileTrackedMission(GameState& s, const ContentCatalog& c) {
    const auto view = trackedMissionView(s, c);
    if (s.run.expedition.trackedMissionId == view.id) return false;
    s.run.expedition.trackedMissionId = view.id; return true;
}
}
