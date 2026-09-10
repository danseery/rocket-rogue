#include "core/SolarProgression.h"

#include "core/ContentIds.h"
#include "core/ResearchSystem.h"
#include "core/ScenarioSystem.h"
#include "core/IncomingMessages.h"
#include "core/SystemContent.h"
#include "core/ExpeditionSystem.h"

#include <algorithm>
#include <set>

namespace rocket {

const SolarMissionDefinition* solarMissionForBody(const ContentCatalog& catalog, std::string_view bodyId)
{
    return catalog.findSolarMission(bodyId);
}

bool solarMissionClaimed(const GameState& state, const ContentCatalog&, const SolarMissionDefinition& mission)
{
    const ScenarioInstance* instance = findScenarioInstance(state.meta, mission.scenarioId);
    const ScenarioStepProgress* step = instance == nullptr
        ? nullptr
        : findScenarioStepProgress(*instance, mission.claimStepId);
    return step != nullptr && step->claimed;
}

bool solarMissionAvailable(const GameState& state, const SolarMissionDefinition& mission)
{
    return mission.prerequisiteUnlockKey.empty() || hasUnlock(state.meta, mission.prerequisiteUnlockKey);
}

const SolarMissionDefinition* nextSolarMission(const GameState& state, const ContentCatalog& catalog)
{
    for (const SolarMissionDefinition& mission : catalog.solarMissions) {
        if (!mission.optional && solarMissionAvailable(state, mission) &&
            !solarMissionClaimed(state, catalog, mission)) {
            return &mission;
        }
    }
    return nullptr;
}

bool solarBodyRevealed(const GameState& state, const ContentCatalog& catalog, std::string_view bodyId)
{
    if (bodyId == "sun" || bodyId == "earth" || bodyId == "moon") return true;
    if (bodyId == "straylight") return arkDiscovered(state);
    if (const SolarMissionDefinition* mission = solarMissionForBody(catalog, bodyId)) {
        return solarMissionAvailable(state, *mission);
    }
    const auto childRevealed = [&](std::string_view childId) {
        const SolarMissionDefinition* child = solarMissionForBody(catalog, childId);
        return child != nullptr && solarMissionAvailable(state, *child);
    };
    if (bodyId == "jupiter") return childRevealed("io");
    if (bodyId == "saturn") return childRevealed("titan");
    if (bodyId == "uranus") return childRevealed("titania");
    if (bodyId == "neptune") return childRevealed("triton");
    return false;
}

bool validateSolarMissionCatalog(const ContentCatalog& catalog, std::string* error)
{
    const auto fail = [&](std::string message) {
        if (error != nullptr) *error = std::move(message);
        return false;
    };
    std::set<std::string> bodies, artifacts, batteries;
    std::vector<const SolarMissionDefinition*> mainMissions;
    int expectedOrdinal = 0;
    for (const SolarMissionDefinition& mission : catalog.solarMissions) {
        if (mission.bodyId.empty() || mission.environmentId.empty() || mission.scenarioId.empty() ||
            mission.claimStepId.empty() || mission.artifactId.empty()) {
            return fail("solar mission has a missing identity");
        }
        if (!bodies.insert(mission.bodyId).second || !artifacts.insert(mission.artifactId).second) {
            return fail("solar mission body or artifact is duplicated: " + mission.bodyId);
        }
        const SystemBodyDefinition* body = systemBody(solarSystemDefinition(), mission.bodyId);
        if (body == nullptr || body->siteId.empty() || body->kind == SystemBodyKind::Giant ||
            body->kind == SystemBodyKind::Star || body->kind == SystemBodyKind::Station) {
            return fail("solar mission artifact is not reachable on a landable body: " + mission.bodyId);
        }
        const ScenarioDefinition* scenario = catalog.findScenario(mission.scenarioId);
        const ScenarioStepDefinition* claim = scenario == nullptr
            ? nullptr : findScenarioStepDefinition(*scenario, mission.claimStepId);
        if (claim == nullptr) {
            return fail("solar mission claim step is unavailable: " + mission.bodyId);
        }
        const bool hasClaimAction = claim->action == ScenarioActionKind::ClaimReward ||
            (claim->action == ScenarioActionKind::BeginActivity && !claim->miningSiteDefinitionId.empty());
        if (!claim->claimRequired || !hasClaimAction ||
            (claim->completionEvent != ScenarioEventKind::ArtifactRecovered &&
             claim->completionEvent != ScenarioEventKind::ProtectedObjectiveExtracted)) {
            return fail("solar mission has no explicit artifact claim action: " + mission.bodyId);
        }
        if (mission.briefingMessageId.empty() || mission.completionMessageId.empty()) {
            return fail("solar mission guidance is missing: " + mission.bodyId);
        }
        if (!mission.optional) {
            if (mission.progressionOrdinal != expectedOrdinal++) {
                return fail("main solar mission order is discontinuous");
            }
            if (mission.batteryId.empty() || !batteries.insert(mission.batteryId).second) {
                return fail("main solar mission battery is missing or duplicated: " + mission.bodyId);
            }
            mainMissions.push_back(&mission);
        } else if (!mission.batteryId.empty()) {
            return fail("optional solar mission must not grant a battery: " + mission.bodyId);
        }
    }
    if (expectedOrdinal != 6) return fail("solar campaign must contain six main missions");
    for (std::size_t index = 0; index < mainMissions.size(); ++index) {
        const SolarMissionDefinition& mission = *mainMissions[index];
        const SolarMissionDefinition* previous = index == 0 ? nullptr : mainMissions[index - 1];
        const SolarMissionDefinition* next = index + 1 < mainMissions.size() ? mainMissions[index + 1] : nullptr;
        if ((previous == nullptr && !mission.prerequisiteUnlockKey.empty()) ||
            (previous != nullptr && mission.prerequisiteUnlockKey != previous->routeUnlockKey)) {
            return fail("main solar mission prerequisite is unreachable: " + mission.bodyId);
        }
        const std::string expectedNext = next == nullptr ? "straylight" : next->bodyId;
        if (mission.nextBodyId != expectedNext || mission.nextBodyId == mission.bodyId) {
            return fail("main solar mission recommendation is circular or missing: " + mission.bodyId);
        }
        if (next != nullptr && mission.routeUnlockKey.empty()) {
            return fail("main solar mission does not reveal its next route: " + mission.bodyId);
        }
    }
    return true;
}

bool reconcileSolarMissionMessages(GameState& state, const ContentCatalog& catalog)
{
    if (!state.run.expedition.travelInitialized || state.screen != Screen::Flight ||
        state.run.flight.mode == FlightMode::Landing) return false;
    const std::string& bodyId = state.run.expedition.location.bodyId;
    const SolarMissionDefinition* mission = solarMissionForBody(catalog, bodyId);
    if (mission == nullptr) {
        // A body-to-system handoff may happen before the completion message is
        // displayed. Keep the newest completed main mission as the stable
        // occurrence source so crossing that seam cannot lose guidance.
        for (auto it = catalog.solarMissions.rbegin(); it != catalog.solarMissions.rend(); ++it) {
            if (!it->optional && solarMissionClaimed(state, catalog, *it)) {
                mission = &*it;
                break;
            }
        }
    }
    if (mission == nullptr || !solarMissionAvailable(state, *mission)) return false;
    if (solarMissionClaimed(state, catalog, *mission)) {
        bool changed = enqueueIncomingMessage(state.incomingMessages, catalog,
            {"campaign.solar." + mission->bodyId + ".complete", mission->completionMessageId, "default"});
        const bool staleAutomaticCourse =
            !state.run.expedition.coursePlayerSelected &&
            (state.run.expedition.course.targetBodyId.empty() ||
             state.run.expedition.course.targetBodyId == mission->bodyId);
        if (!mission->optional && staleAutomaticCourse) {
            const ExpeditionResult result = plotSystemCourse(
                state.run.expedition, state.run.flight, solarSystemDefinition(), "earth");
            changed |= result == ExpeditionResult::Applied;
            state.run.expedition.coursePlayerSelected = false;
            state.run.expedition.cruise.active = false;
        }
        return changed;
    }
    return enqueueIncomingMessage(state.incomingMessages, catalog,
        {"campaign.solar." + mission->bodyId + ".briefing", mission->briefingMessageId, "default"});
}

ScenarioObjectivePresentation solarMissionObjectiveForBody(
    const GameState& state, const ContentCatalog& catalog, std::string_view bodyId)
{
    const SolarMissionDefinition* mission = solarMissionForBody(catalog, bodyId);
    if (mission == nullptr || !solarMissionAvailable(state, *mission)) return {};
    const ScenarioInstance* instance = findScenarioInstance(state.meta, mission->scenarioId);
    const ScenarioDefinition* definition = catalog.findScenario(mission->scenarioId);
    if (instance == nullptr || definition == nullptr) return {};
    ScenarioObjectivePresentation best;
    int bestRank = 100;
    for (const ScenarioStepDefinition& step : definition->steps) {
        ScenarioObjectivePresentation candidate =
            scenarioObjectivePresentation(state, catalog, instance->id, step.id);
        if (!candidate.available) continue;
        const int rank = candidate.returnPending ? -1
            : candidate.state == ScenarioStepState::ReadyToClaim ? 0
            : candidate.state == ScenarioStepState::Active ? 1
            : candidate.state == ScenarioStepState::Locked ? 2 : 3;
        if (rank < bestRank) {
            best = std::move(candidate);
            bestRank = rank;
        }
    }
    return best;
}

} // namespace rocket
