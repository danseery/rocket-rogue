#include "core/LunarDiscoveryMessages.h"
#include "core/Content.h"
#include "core/ContentIds.h"
#include "core/GameState.h"
#include "core/IncomingMessages.h"
#include "core/SaveData.h"
#include "core/ScenarioSystem.h"
#include "core/SolarProgression.h"
#include "game/GamePanel.h"
#include <algorithm>
#include <stdexcept>

void incomingMessageTests() {
    using namespace rocket;
    const auto check = [](bool condition, const char *message) {
        if (!condition)
            throw std::runtime_error(message);
    };
    auto catalog = createDefaultContent();
    for (const auto& drone : catalog.miniDrones) {
        const std::string id = "drone_arrival_" + drone.id;
        check(incomingMessage(catalog, id) != nullptr, "Every drone type has a shared first-arrival introduction");
        IncomingMessageState arrivals;
        check(enqueueIncomingMessage(arrivals, catalog, {id, id, "default"}), "First arrival queues an introduction");
        check(acknowledgeIncomingMessage(arrivals, id).has_value(), "Arrival can be acknowledged");
        IncomingMessageState savedArrivals;
        check(deserializeIncomingMessages(serializeIncomingMessages(arrivals), savedArrivals) &&
            !enqueueIncomingMessage(savedArrivals, catalog, {id + ".again", id, "default"}),
            "Reassignment and save/load must not replay first-arrival introductions");
    }
    {
        IncomingMessageState belt;
        check(enqueueIncomingMessage(belt,catalog,{"campaign.asteroid_belt_intro","asteroid_belt_intro","default"}),
            "First belt entry must queue its tutorial");
        IncomingMessageState reloaded;
        check(deserializeIncomingMessages(serializeIncomingMessages(belt),reloaded) && reloaded.pending.size()==1,
            "Reload before acknowledgement must retain the belt tutorial");
        check(acknowledgeIncomingMessage(reloaded,"campaign.asteroid_belt_intro").has_value(),
            "Belt tutorial acknowledgement must succeed");
        check(deserializeIncomingMessages(serializeIncomingMessages(reloaded),belt) &&
            !enqueueIncomingMessage(belt,catalog,{"return.belt","asteroid_belt_intro","default"}),
            "Belt tutorial must not replay on return or after reload");
        const auto* message = incomingMessage(catalog,"asteroid_belt_intro");
        check(message && message->variants.front().body.find("Flight Controls")!=std::string::npos &&
            message->variants.front().body.find("Hull Plating")!=std::string::npos,
            "Belt tutorial must name the actual ship upgrades");
    }
    catalog.messageSpeakers.push_back({"engineer", "An Engineer With A Long Display Name", "ENGINEERING",
                                       "portraits/mission-control-fennec.png"});
    catalog.incomingMessages.push_back({"repair_report",
                                        "engineer",
                                        "Repair report",
                                        "Received",
                                        false,
                                        {{"default", "Repairs are complete.", {}}}});
    check(validateIncomingMessages(catalog), "Multiple speakers and repeatable messages must validate");
    const auto shipFullMessage = incomingMessage(catalog, "ship_full_tip");
    check(shipFullMessage != nullptr && shipFullMessage->concerned &&
              shipFullMessage->title == "Easy there, space squirrel" &&
              shipFullMessage->variants.front().body.find("Hoarding is frowned upon") != std::string::npos,
          "Ship overflow must use the concerned anti-hoarding warning");
    const auto moonReturnMessage = incomingMessage(catalog, "moon_mission_complete");
    check(moonReturnMessage != nullptr && moonReturnMessage->campaignOnce &&
              moonReturnMessage->context == MessageDeliveryContext::Any &&
              moonReturnMessage->variants.front().body.find("Mars, Mercury, and Venus are now charted") != std::string::npos,
          "Moon completion guidance must report its reward and newly charted worlds");
    auto invalid = catalog;
    invalid.incomingMessages.back().speakerId = "missing";
    check(!validateIncomingMessages(invalid), "Missing speaker references must fail validation");
    IncomingMessageState queue;
    check(enqueueIncomingMessage(queue, catalog, {"scan.1", "lunar_scan", "default"}),
          "First occurrence must enqueue");
    check(!enqueueIncomingMessage(queue, catalog, {"scan.2", "lunar_scan", "default"}),
          "Campaign-once pending messages must deduplicate");
    check(!enqueueIncomingMessage(queue, catalog, {"bad", "lunar_scan", "missing"}),
          "Missing variants must not enqueue");
    check(enqueueIncomingMessage(queue, catalog, {"repair.1", "repair_report", "default"}),
          "Second speaker must enqueue");
    check(!acknowledgeIncomingMessage(queue, "repair.1"),
          "Cannot acknowledge an occurrence behind the visible head");
    const auto ack = acknowledgeIncomingMessage(queue, "scan.1");
    check(ack && ack->messageId == "lunar_scan", "Acknowledgement must return its typed message identity");
    check(!enqueueIncomingMessage(queue, catalog, {"scan.3", "lunar_scan", "default"}),
          "Campaign-once acknowledgement must survive new occurrence IDs");
    check(acknowledgeIncomingMessage(queue, "repair.1").has_value(), "Repeatable message must acknowledge");
    check(!enqueueIncomingMessage(queue, catalog, {"repair.1", "repair_report", "default"}),
          "Acknowledged occurrences cannot replay");
    check(enqueueIncomingMessage(queue, catalog, {"repair.2", "repair_report", "default"}),
          "A new repeatable occurrence must work");
    IncomingMessageState restored;
    check(deserializeIncomingMessages(serializeIncomingMessages(queue), restored), "Queue must deserialize");
    check(serializeIncomingMessages(restored) == serializeIncomingMessages(queue),
          "Message state must round trip exactly");
    check(!deserializeIncomingMessages("999999", restored), "Malformed counts must be rejected");
    auto game = createNewGame(catalog, 991);
    game.incomingMessages = queue;
    const auto save = deserializeSaveData(serializeSaveData(captureSaveData(game)));
    check(save && save->version == 23, "v23 save with messages must load");
    restoreSaveData(game, catalog, *save);
    check(serializeIncomingMessages(game.incomingMessages) == serializeIncomingMessages(queue),
          "Full save must retain queue and acknowledgements");

    game.screen = Screen::Mining;
    auto &mining = game.run.mining;
    mining.active = true;
    mining.miningSiteDefinitionId = content::miningSite::lunarAnomalyCrevice;
    mining.artifact.present = true;
    mining.artifact.state = MiningArtifactState::Embedded;
    game.incomingMessages = {};
    check(reconcileLunarMessages(game, catalog), "Eligible old save must queue scanner instruction");
    check(!reconcileLunarMessages(game, catalog), "Repeated reconciliation must be idempotent");
    mining.artifact.revealed = true;
    mining.operatorMode = MiningOperatorMode::Jetpack;
    check(reconcileLunarMessages(game, catalog) && game.incomingMessages.pending.size() == 1 &&
              game.incomingMessages.pending.front().variantId == "eva",
          "Early discovery replaces stale scan instruction with EVA variant");
    PreparedLaunch prepared;
    PanelRenderContext context{game, catalog, prepared, prepared};
    {
        const auto* contact = incomingMessage(catalog, "triton_mission_complete");
        check(contact && contact->speakerId == "straylight_ai", "Post-Triton contact belongs to the ship AI");
        check(incomingMessage(catalog, "triton_mission_briefing")->speakerId == "mission_control_fennec",
            "Normal mission briefing retains Mission Control");
        const auto card = buildIncomingMessageCard(context, "triton_mission_complete", "default", "acknowledge");
        check(card && card->bodyMarkup.find("incoming-unknown-signal") != std::string::npos &&
            card->bodyMarkup.find("Unknown") != std::string::npos &&
            card->bodyMarkup.find("<img") == std::string::npos,
            "Ship AI uses the outlined Unknown signal without a fox portrait");
    }
    context.firstTimeIntroductionsEnabled = false;
    auto panel = buildGamePanelPresentation(context);
    const auto find = [](const auto &p) {
        return std::find_if(p.modals.begin(), p.modals.end(),
                            [](const auto &modal) { return modal.id == "incoming_message"; });
    };
    check(find(panel) != panel.modals.end(),
          "Eligible message must render through shared modal presentation");
    check(!find(panel)->dismissible && !find(panel)->showClose && find(panel)->autoOpen,
          "Message needs explicit acknowledgement");
    check(find(panel)->bodyMarkup.find("Your suit can fit") != std::string::npos,
          "Renderer must consume the selected content variant");
    mining.scannerPulseSeconds = 0.2;
    panel = buildGamePanelPresentation(context);
    check(find(panel) == panel.modals.end(), "Discovery animation must finish before the card opens");
    mining.scannerPulseSeconds = 0;
    game.run.expedition.progression.pendingRunUpgradeChoices = 1;
    panel = buildGamePanelPresentation(context);
    check(find(panel) == panel.modals.end(), "XP selections must take priority");
    game.run.expedition.progression.pendingRunUpgradeChoices = 0;
    mining.failurePending = true;
    panel = buildGamePanelPresentation(context);
    check(find(panel) == panel.modals.end(), "Failure resolution must take priority");
    mining.failurePending = false;
    mining.artifact.state = MiningArtifactState::Delivered;
    check(reconcileLunarMessages(game, catalog) && game.incomingMessages.pending.empty(),
          "Delivered old-save artifacts must suppress obsolete instructions");
    game.incomingMessages = queue;
    game.screen = Screen::Hangar;
    panel = buildGamePanelPresentation(context);
    check(find(panel) != panel.modals.end() &&
              find(panel)->bodyMarkup.find("An Engineer With A Long Display Name") != std::string::npos,
          "Other speakers and contexts must use the identical card");

    auto campaign = createNewGame(catalog, 992);
    campaign.run.expedition.travelInitialized = true;
    campaign.run.expedition.active = true;
    campaign.run.expedition.location.bodyId = "moon";
    campaign.screen = Screen::Flight;
    campaign.run.flight.mode = FlightMode::Orbit;
    check(reconcileSolarMissionMessages(campaign, catalog), "First approach should queue the mission briefing");
    const auto completeMission = [&](std::string_view bodyId) {
        const auto* mission = solarMissionForBody(catalog, bodyId);
        check(mission != nullptr, "Mission fixture must exist");
        const auto* scenario = catalog.findScenario(mission->scenarioId);
        check(scenario != nullptr, "Mission scenario must exist");
        for (const auto& step : scenario->steps) {
            if (step.mandatoryBriefing)
                check(performScenarioAction(campaign, catalog, scenario->id, step.id,
                    ScenarioActionKind::AcknowledgeBriefing).applied, "Mission briefing must acknowledge");
            if (step.completionEvent == ScenarioEventKind::ManualAction)
                check(performScenarioAction(campaign, catalog, scenario->id, step.id,
                    ScenarioActionKind::BeginActivity).applied, "Mission setup action must apply");
            else if (step.completionEvent != ScenarioEventKind::None)
                check(recordScenarioEvent(campaign, catalog,
                    {step.completionEvent, scenario->id, step.id, step.eventOriginId,
                     step.eventTargetId, step.requiredProgress, step.requiredGrade}),
                    "Mission objective event must apply");
            if (!step.claimRequired) continue;
            auto* instance = findScenarioInstance(campaign.meta, scenario->id);
            auto* progress = findScenarioStepProgress(*instance, step.id);
            progress->failureSeen = true;
            progress->failureAcknowledged = false;
            const auto ready = scenarioObjectivePresentation(campaign, catalog, scenario->id, step.id);
            check(ready.state == ScenarioStepState::ReadyToClaim &&
                      ready.action == ScenarioActionKind::ClaimReward && !ready.firstFailurePending,
                  "An earned reward must supersede an old failure acknowledgement");
            const auto claimed = performScenarioAction(campaign, catalog, scenario->id, step.id,
                ScenarioActionKind::ClaimReward);
            check(claimed.applied && claimed.transition.kind == ScenarioTransitionKind::None,
                  "Claim must grant rewards in place");
            check(!performScenarioAction(campaign, catalog, scenario->id, step.id,
                      ScenarioActionKind::ClaimReward).applied &&
                      !performScenarioAction(campaign, catalog, scenario->id, step.id,
                      ScenarioActionKind::BeginActivity).applied &&
                      !performScenarioAction(campaign, catalog, scenario->id, step.id,
                      ScenarioActionKind::AcknowledgeFailure).applied,
                  "A completed mission must reject duplicate claims and stale activity or failure actions");
        }
        const auto complete = solarMissionObjectiveForBody(campaign, catalog, bodyId);
        check(complete.state == ScenarioStepState::Complete && complete.action == ScenarioActionKind::None &&
                  complete.detail == "MISSION COMPLETE" && !complete.mandatoryBriefing &&
                  !complete.firstFailurePending,
              "A claimed mission must explicitly show completion without replaying its briefing or failure");
    };
    completeMission("moon");
    campaign.screen = Screen::Mining;
    check(reconcileSolarMissionMessages(campaign, catalog) &&
              campaign.incomingMessages.pending.size() == 1 &&
              campaign.incomingMessages.pending.front().messageId == "drone_arrival_mining_drone",
          "Claiming during mining retires the briefing and introduces the newly assigned Prospector immediately");
    check(acknowledgeIncomingMessage(campaign.incomingMessages,
        campaign.incomingMessages.pending.front().id).has_value(), "Acknowledge first Prospector arrival");
    campaign.screen = Screen::Hangar;
    campaign.run.expedition.active = false;
    campaign.run.expedition.location.bodyId = "earth";
    campaign.run.expedition.location.siteId = "earth.dock";
    campaign.run.expedition.course.targetBodyId = "mars";
    campaign.run.expedition.coursePlayerSelected = true;
    for (const auto* staleMessage : {"lunar_scan", "lunar_recovery", "rig_full_tip", "ship_full_tip"})
        check(enqueueIncomingMessage(campaign.incomingMessages, catalog,
                  {std::string("lost_ship.") + staleMessage, staleMessage, "default"}),
              "Loss fixture should contain pending mining guidance");
    check(reconcileSolarMissionMessages(campaign, catalog) &&
              campaign.incomingMessages.pending.size() == 1 &&
              campaign.incomingMessages.pending.front().messageId == "moon_mission_complete",
          "Respawn must retire abandoned mining instructions and deliver the earned completion once");
    check(campaign.run.expedition.course.targetBodyId == "mars" &&
              !reconcileSolarMissionMessages(campaign, catalog),
          "Completion reconciliation must preserve deliberate waypoints and remain idempotent");
    check(acknowledgeIncomingMessage(campaign.incomingMessages,
              campaign.incomingMessages.pending.front().id).has_value(), "Completion must acknowledge");
    const auto savedCampaign = deserializeSaveData(serializeSaveData(captureSaveData(campaign)));
    check(savedCampaign.has_value(), "Mission boundary save must deserialize");
    restoreSaveData(campaign, catalog, *savedCampaign);
    check(!reconcileSolarMissionMessages(campaign, catalog) && campaign.incomingMessages.pending.empty(),
          "Reloading at the dock must not replay acknowledged completion");

    completeMission("mars");
    completeMission("mercury");
    campaign.run.destinationIndex = static_cast<int>(std::distance(catalog.destinations.begin(),
        std::find_if(catalog.destinations.begin(), catalog.destinations.end(),
            [](const Destination& destination) { return destination.id == "mars"; })));
    campaign.run.expedition.location.bodyId = "earth";
    campaign.run.expedition.location.siteId = "earth.dock";
    check(nextSolarMission(campaign, catalog)->bodyId == "io" &&
              campaignNextStep(campaign, catalog).destinationId == "io",
          "After Mars, dock guidance must recommend Io independently of stale geology or the selected waypoint");
    check(reconcileSolarMissionMessages(campaign, catalog) && campaign.incomingMessages.pending.size() == 2,
          "Both main and optional mission completions must survive returning or respawning away from their body");
    while (!campaign.incomingMessages.pending.empty())
        check(acknowledgeIncomingMessage(campaign.incomingMessages,
                  campaign.incomingMessages.pending.front().id).has_value(), "Pending completions must acknowledge");
    campaign.screen = Screen::Flight;
    campaign.run.expedition.active = true;
    campaign.run.expedition.location.bodyId = "mars";
    campaign.run.expedition.location.siteId.clear();
    check(!reconcileSolarMissionMessages(campaign, catalog) && campaign.incomingMessages.pending.empty() &&
              campaign.run.expedition.course.targetBodyId == "mars",
          "A deliberate revisit must not reopen completed mission instructions or replace its waypoint");

    auto badMissionCatalog = catalog;
    badMissionCatalog.solarMissions.front().completionMessageId = "missing_completion";
    check(!validateSolarMissionCatalog(badMissionCatalog),
          "Campaign guardrails must reject missing mission completion messages");
    badMissionCatalog = catalog;
    auto* badScenario = const_cast<ScenarioDefinition*>(badMissionCatalog.findScenario(
        badMissionCatalog.solarMissions.front().scenarioId));
    auto* badClaim = const_cast<ScenarioStepDefinition*>(findScenarioStepDefinition(
        *badScenario, badMissionCatalog.solarMissions.front().claimStepId));
    badClaim->transition.kind = ScenarioTransitionKind::OpenScreen;
    badClaim->transition.screen = Screen::Hangar;
    check(!validateSolarMissionCatalog(badMissionCatalog),
          "Campaign guardrails must reject mission claims that redirect into a legacy menu");
}
