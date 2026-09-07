#pragma once
#include "core/GameTypes.h"
#include <optional>
namespace rocket
{
// Persistent location, cargo and registry wire payload. Expedition progression
// retains the existing XP/graft wire fields in SaveData; derived course previews
// are rebuilt from the saved target and current resources.
std::string serializeExpedition(const PersistentExpeditionState &);
std::optional<PersistentExpeditionState> deserializeExpedition(std::string_view);
} // namespace rocket
