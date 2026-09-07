#include "core/LunarDiscoveryMessages.h"
#include "core/Content.h"
#include "core/ContentIds.h"
#include <algorithm>
namespace rocket {
bool reconcileLunarMessages(GameState &state, const ContentCatalog &catalog) {
    const auto &mining = state.run.mining;
    if (!mining.active || mining.miningSiteDefinitionId != content::miningSite::lunarAnomalyCrevice ||
        !mining.artifact.present)
        return false;
    auto &messages = state.incomingMessages;
    const bool delivered = mining.artifact.state == MiningArtifactState::Delivered;
    const bool revealed = mining.artifact.revealed || mining.artifact.state == MiningArtifactState::Loose;
    bool changed = false;
    // Superseded instructions are retired without granting mission acknowledgement/rewards.
    const auto oldSize = messages.pending.size();
    std::erase_if(messages.pending, [&](const auto &item) {
        return (item.messageId == "lunar_scan" && (revealed || delivered)) ||
               (item.messageId == "lunar_recovery" && delivered);
    });
    changed = oldSize != messages.pending.size();
    if (delivered)
        return changed;
    const std::string id = revealed ? "lunar_recovery" : "lunar_scan";
    const std::string variant =
        revealed && mining.operatorMode == MiningOperatorMode::Jetpack ? "eva" : "default";
    for (auto &pending : messages.pending) {
        if (pending.messageId == id && pending.variantId != variant) {
            pending.variantId = variant;
            changed = true;
        }
    }
    return enqueueIncomingMessage(messages, catalog, {id, id, variant}) || changed;
}

} // namespace rocket
