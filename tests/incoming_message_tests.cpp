#include "core/LunarDiscoveryMessages.h"
#include "core/Content.h"
#include "core/ContentIds.h"
#include "core/GameState.h"
#include "core/IncomingMessages.h"
#include "core/SaveData.h"
#include "core/ScenarioSystem.h"
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
    catalog.messageSpeakers.push_back({"engineer", "An Engineer With A Long Display Name", "ENGINEERING",
                                       "portraits/mission-control-fennec.png"});
    catalog.incomingMessages.push_back({"repair_report",
                                        "engineer",
                                        "Repair report",
                                        "Received",
                                        false,
                                        {{"default", "Repairs are complete.", {}}}});
    check(validateIncomingMessages(catalog), "Multiple speakers and repeatable messages must validate");
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
    auto oldPayload = serializeSaveData(captureSaveData(game));
    const auto messageField = oldPayload.find("incomingMessages=");
    check(messageField != std::string::npos, "Current saves must include message state");
    oldPayload.erase(messageField, oldPayload.find('\n', messageField) - messageField + 1);
    const auto oldSave = deserializeSaveData(oldPayload);
    check(oldSave && oldSave->version == 21 && oldSave->incomingMessages.pending.empty(),
          "Existing v21 payloads without message fields must remain compatible");
    game.incomingMessages = queue;
    const auto save = deserializeSaveData(serializeSaveData(captureSaveData(game)));
    check(save.has_value(), "v21 save with messages must load");
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
}
