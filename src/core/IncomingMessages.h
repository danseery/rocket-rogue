#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rocket {
struct MessageSpeaker {
    std::string id, name, channel, portrait;
    std::string concernedPortrait;
    bool unknownSignal = false;
};
enum class MessageHint { Scanner, ExitRig, Drill, Tether, FlightSteer, FlightThrust };
enum class MessageDeliveryContext { Any, Mining };
struct MessageVariant {
    std::string id, body;
    std::vector<MessageHint> hints;
};
struct IncomingMessageDefinition {
    std::string id, speakerId, title, acknowledgement;
    bool campaignOnce = true;
    std::vector<MessageVariant> variants;
    MessageDeliveryContext context = MessageDeliveryContext::Any;
    bool concerned = false;
};
struct IncomingMessageOccurrence {
    std::string id, messageId, variantId;
};
struct IncomingMessageState {
    std::vector<IncomingMessageOccurrence> pending;
    std::vector<std::string> acknowledgedOccurrences;
    std::vector<std::string> acknowledgedMessages;
};
struct MessageAcknowledgement {
    std::string occurrenceId, messageId;
};
struct ContentCatalog;
struct GameState;
const MessageSpeaker *messageSpeaker(const ContentCatalog &, std::string_view);
const IncomingMessageDefinition *incomingMessage(const ContentCatalog &, std::string_view);
const MessageVariant *messageVariant(const IncomingMessageDefinition &, std::string_view);
bool validateIncomingMessages(const ContentCatalog &, std::string *error = nullptr);
bool enqueueIncomingMessage(IncomingMessageState &, const ContentCatalog &, IncomingMessageOccurrence);
std::optional<MessageAcknowledgement> acknowledgeIncomingMessage(IncomingMessageState &, std::string_view);
std::string serializeIncomingMessages(const IncomingMessageState &);
bool deserializeIncomingMessages(std::string_view, IncomingMessageState &);
} // namespace rocket
