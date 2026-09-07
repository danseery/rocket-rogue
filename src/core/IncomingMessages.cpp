#include "core/IncomingMessages.h"
#include "core/Content.h"
#include "core/ContentIds.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <unordered_set>

namespace rocket {
namespace {
bool contains(const std::vector<std::string> &values, std::string_view id) {
    return std::find(values.begin(), values.end(), id) != values.end();
}
bool validId(std::string_view id) {
    return !id.empty() && id.size() <= 160 &&
           id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.:-") ==
               std::string_view::npos;
}
} // namespace
const MessageSpeaker *messageSpeaker(const ContentCatalog &catalog, std::string_view id) {
    for (const auto &speaker : catalog.messageSpeakers)
        if (speaker.id == id)
            return &speaker;
    return nullptr;
}
const IncomingMessageDefinition *incomingMessage(const ContentCatalog &catalog, std::string_view id) {
    for (const auto &message : catalog.incomingMessages)
        if (message.id == id)
            return &message;
    return nullptr;
}
const MessageVariant *messageVariant(const IncomingMessageDefinition &message, std::string_view id) {
    for (const auto &variant : message.variants)
        if (variant.id == id)
            return &variant;
    return nullptr;
}
bool validateIncomingMessages(const ContentCatalog &catalog, std::string *error) {
    const auto fail = [&](const char *why) {
        if (error)
            *error = why;
        return false;
    };
    std::unordered_set<std::string> ids;
    for (const auto &speaker : catalog.messageSpeakers) {
        if (!validId(speaker.id) || !ids.insert(speaker.id).second || speaker.name.empty() ||
            speaker.channel.empty() || speaker.portrait.empty())
            return fail("Invalid incoming-message speaker");
    }
    ids.clear();
    for (const auto &message : catalog.incomingMessages) {
        if (!validId(message.id) || !ids.insert(message.id).second ||
            !messageSpeaker(catalog, message.speakerId) || message.variants.empty() ||
            message.title.empty() || message.acknowledgement.empty())
            return fail("Invalid incoming-message definition");
        if (message.concerned && messageSpeaker(catalog, message.speakerId)->concernedPortrait.empty())
            return fail("Missing concerned incoming-message portrait");
        std::unordered_set<std::string> variants;
        for (const auto &variant : message.variants)
            if (!validId(variant.id) || !variants.insert(variant.id).second || variant.body.empty())
                return fail("Invalid incoming-message variant");
    }
    return true;
}
bool enqueueIncomingMessage(IncomingMessageState &state, const ContentCatalog &catalog,
                            IncomingMessageOccurrence occurrence) {
    const auto *definition = incomingMessage(catalog, occurrence.messageId);
    if (!definition || !validId(occurrence.id) || !messageVariant(*definition, occurrence.variantId) ||
        contains(state.acknowledgedOccurrences, occurrence.id) ||
        (definition->campaignOnce && contains(state.acknowledgedMessages, occurrence.messageId)))
        return false;
    for (const auto &pending : state.pending)
        if (pending.id == occurrence.id ||
            (definition->campaignOnce && pending.messageId == occurrence.messageId))
            return false;
    state.pending.push_back(std::move(occurrence));
    return true;
}
std::optional<MessageAcknowledgement> acknowledgeIncomingMessage(IncomingMessageState &state,
                                                                 std::string_view occurrenceId) {
    if (state.pending.empty() || state.pending.front().id != occurrenceId)
        return std::nullopt;
    const auto occurrence = state.pending.front();
    state.pending.erase(state.pending.begin());
    state.acknowledgedOccurrences.push_back(occurrence.id);
    if (!contains(state.acknowledgedMessages, occurrence.messageId))
        state.acknowledgedMessages.push_back(occurrence.messageId);
    return MessageAcknowledgement{occurrence.id, occurrence.messageId};
}
std::string serializeIncomingMessages(const IncomingMessageState &state) {
    std::ostringstream out;
    out << state.pending.size();
    for (const auto &item : state.pending)
        out << ' ' << std::quoted(item.id) << ' ' << std::quoted(item.messageId) << ' '
            << std::quoted(item.variantId);
    for (const auto *list : {&state.acknowledgedOccurrences, &state.acknowledgedMessages}) {
        out << ' ' << list->size();
        for (const auto &id : *list)
            out << ' ' << std::quoted(id);
    }
    return out.str();
}
bool deserializeIncomingMessages(std::string_view value, IncomingMessageState &state) {
    std::istringstream in{std::string(value)};
    IncomingMessageState parsed;
    std::size_t count = 0;
    if (!(in >> count) || count > 10000)
        return false;
    std::unordered_set<std::string> ids;
    for (std::size_t i = 0; i < count; ++i) {
        IncomingMessageOccurrence item;
        if (!(in >> std::quoted(item.id) >> std::quoted(item.messageId) >> std::quoted(item.variantId)) ||
            !validId(item.id) || !validId(item.messageId) || !validId(item.variantId) ||
            !ids.insert(item.id).second)
            return false;
        parsed.pending.push_back(std::move(item));
    }
    for (auto *list : {&parsed.acknowledgedOccurrences, &parsed.acknowledgedMessages}) {
        if (!(in >> count) || count > 10000)
            return false;
        ids.clear();
        for (std::size_t i = 0; i < count; ++i) {
            std::string id;
            if (!(in >> std::quoted(id)) || !validId(id) || !ids.insert(id).second)
                return false;
            list->push_back(std::move(id));
        }
    }
    in >> std::ws;
    if (!in.eof())
        return false;
    for (const auto &item : parsed.pending)
        if (contains(parsed.acknowledgedOccurrences, item.id))
            return false;
    state = std::move(parsed);
    return true;
}
} // namespace rocket
