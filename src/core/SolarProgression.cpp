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
namespace {

const ScenarioStepDefinition* missionAcceptanceStep(
    const ContentCatalog& catalog, const SolarMissionDefinition& mission)
{
    const ScenarioDefinition* scenario = catalog.findScenario(mission.scenarioId);
    const ScenarioStepDefinition* step = scenario == nullptr ? nullptr
        : findScenarioStepDefinition(*scenario, mission.acceptanceStepId);
    if (step == nullptr || !step->mandatoryBriefing || step->claimRequired ||
        step->action != mission.acceptanceAction || step->actionLabel.empty() ||
        step->transition.kind != ScenarioTransitionKind::None) return nullptr;
    const bool acknowledgement = mission.acceptanceAction == ScenarioActionKind::AcknowledgeBriefing &&
        step->completionEvent == ScenarioEventKind::None;
    const bool commissioning = mission.acceptanceAction == ScenarioActionKind::BeginActivity &&
        step->completionEvent == ScenarioEventKind::ManualAction &&
        step->miningSiteDefinitionId.empty() && step->activity == ScenarioActivityKind::None;
    return acknowledgement || commissioning ? step : nullptr;
}

} // namespace

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

bool solarMissionAccepted(
    const GameState& state, const ContentCatalog& catalog, const SolarMissionDefinition& mission)
{
    return missionAcceptanceStep(catalog, mission) != nullptr &&
        scenarioStepState(state, catalog, mission.scenarioId, mission.acceptanceStepId) ==
            ScenarioStepState::Complete;
}

ScenarioObjectivePresentation solarMissionAcceptanceForBody(
    const GameState& state, const ContentCatalog& catalog, std::string_view bodyId)
{
    const SolarMissionDefinition* mission = solarMissionForBody(catalog, bodyId);
    if (!state.run.expedition.travelInitialized || state.run.expedition.location.bodyId != bodyId ||
        mission == nullptr || !solarMissionAvailable(state, *mission) ||
        missionAcceptanceStep(catalog, *mission) == nullptr ||
        scenarioStepState(state, catalog, mission->scenarioId, mission->acceptanceStepId) !=
            ScenarioStepState::Active) return {};
    ScenarioObjectivePresentation presentation = scenarioObjectivePresentation(
        state, catalog, mission->scenarioId, mission->acceptanceStepId);
    presentation.action = mission->acceptanceAction;
    return presentation;
}

SolarMissionAcceptanceOutcome acceptSolarMission(
    GameState& state, const ContentCatalog& catalog, const SolarMissionDefinition& mission)
{
    if (missionAcceptanceStep(catalog, mission) == nullptr) {
        return {false, false, "Mission acceptance is unavailable."};
    }
    if (solarMissionAccepted(state, catalog, mission)) {
        return {true, false, "Mission already accepted."};
    }
    if (!state.run.expedition.travelInitialized ||
        state.run.expedition.location.bodyId != mission.bodyId ||
        !solarMissionAvailable(state, mission)) {
        return {false, false, "Reach the mission body before accepting this objective."};
    }
    ensureScenarioInstances(state, catalog);
    const ScenarioActionOutcome outcome = performScenarioAction(
        state, catalog, mission.scenarioId, mission.acceptanceStepId, mission.acceptanceAction,
        !state.run.mining.active);
    return {outcome.applied && solarMissionAccepted(state, catalog, mission),
        outcome.applied, outcome.message};
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
        if (missionAcceptanceStep(catalog, mission) == nullptr) {
            return fail("solar mission acceptance binding is invalid: " + mission.bodyId);
        }
        const bool hasClaimAction = claim->action == ScenarioActionKind::ClaimReward ||
            (claim->action == ScenarioActionKind::BeginActivity && !claim->miningSiteDefinitionId.empty());
        if (!claim->claimRequired || !hasClaimAction ||
            (claim->completionEvent != ScenarioEventKind::ArtifactRecovered &&
             claim->completionEvent != ScenarioEventKind::ProtectedObjectiveExtracted)) {
            return fail("solar mission has no explicit artifact claim action: " + mission.bodyId);
        }
        const auto* briefingMessage = incomingMessage(catalog, mission.briefingMessageId);
        const auto* completionMessage = incomingMessage(catalog, mission.completionMessageId);
        if (briefingMessage == nullptr || completionMessage == nullptr ||
            !briefingMessage->campaignOnce || !completionMessage->campaignOnce ||
            messageVariant(*briefingMessage, "default") == nullptr ||
            messageVariant(*completionMessage, "default") == nullptr ||
            mission.briefingMessageId == mission.completionMessageId) {
            return fail("solar mission guidance is missing: " + mission.bodyId);
        }
        if (claim->transition.kind != ScenarioTransitionKind::None) {
            return fail("solar mission claim must preserve the current gameplay screen: " + mission.bodyId);
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
    auto& expedition = state.run.expedition;
    if (!expedition.travelInitialized) return false;
    auto& messages = state.incomingMessages;
    const auto pendingCount = messages.pending.size();
    std::erase_if(messages.pending, [&](const IncomingMessageOccurrence& occurrence) {
        const auto* message = incomingMessage(catalog, occurrence.messageId);
        if (message == nullptr) return true;
        if (!state.run.mining.active && !state.run.planetaryExpedition.miningSitePrepared &&
            message != nullptr && message->context == MessageDeliveryContext::Mining) {
            return true;
        }
        return std::any_of(catalog.solarMissions.begin(), catalog.solarMissions.end(),
            [&](const SolarMissionDefinition& mission) {
                return occurrence.messageId == mission.briefingMessageId &&
                    solarMissionClaimed(state, catalog, mission);
            });
    });
    bool changed = messages.pending.size() != pendingCount;
    const bool inFlight = state.screen == Screen::Flight && state.run.flight.mode != FlightMode::Landing;
    const bool atDock = state.screen == Screen::Hangar && operationalHomeDocked(expedition);
    if (!inFlight && !atDock) return changed;

    // Mission ownership survives body transitions and ship loss. Deliver any
    // earned completion from that ledger even if the player reached a new
    // body or respawned at Earth before its first safe presentation point.
    const SolarMissionDefinition* newlyCompletedMain = nullptr;
    for (const auto& mission : catalog.solarMissions) {
        if (!solarMissionClaimed(state, catalog, mission)) continue;
        const bool queued = enqueueIncomingMessage(messages, catalog,
            {"campaign.solar." + mission.bodyId + ".complete", mission.completionMessageId, "default"});
        changed |= queued;
        if (queued && !mission.optional) newlyCompletedMain = &mission;
    }
    if (inFlight && newlyCompletedMain != nullptr) {
        const bool staleAutomaticCourse =
            !expedition.coursePlayerSelected &&
            (expedition.course.targetBodyId.empty() ||
             expedition.course.targetBodyId == newlyCompletedMain->bodyId);
        if (staleAutomaticCourse) {
            const ExpeditionResult result = plotSystemCourse(
                expedition, state.run.flight, solarSystemDefinition(), "earth");
            changed |= result == ExpeditionResult::Applied;
            expedition.coursePlayerSelected = false;
            expedition.cruise.active = false;
        }
    }
    const auto* mission = solarMissionForBody(catalog, expedition.location.bodyId);
    if (inFlight && mission != nullptr && solarMissionAvailable(state, *mission) &&
        !solarMissionClaimed(state, catalog, *mission)) {
        changed |= enqueueIncomingMessage(messages, catalog,
            {"campaign.solar." + mission->bodyId + ".briefing", mission->briefingMessageId, "default"});
    }
    return changed;
}

ScenarioObjectivePresentation solarMissionObjectiveForBody(
    const GameState& state, const ContentCatalog& catalog, std::string_view bodyId)
{
    const SolarMissionDefinition* mission = solarMissionForBody(catalog, bodyId);
    if (mission == nullptr || !solarMissionAvailable(state, *mission)) return {};
    const ScenarioInstance* instance = findScenarioInstance(state.meta, mission->scenarioId);
    const ScenarioDefinition* definition = catalog.findScenario(mission->scenarioId);
    if (instance == nullptr || definition == nullptr) return {};
    if (solarMissionClaimed(state, catalog, *mission)) {
        return scenarioObjectivePresentation(state, catalog, instance->id, mission->claimStepId);
    }
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
