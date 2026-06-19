#pragma once

#include "core/GameState.h"

#include <optional>

namespace rocket {

struct SaveData {
    int version = 1;
    std::uint64_t seed = 0xC0DEC0FFEEULL;
    double credits = 100.0;
    int destinationIndex = 0;
    int frontierReadiness = 0;
    int shipDamage = 0;
    std::string frameId = "pathfinder";
    std::vector<std::string> inventoryModuleIds;
    std::vector<std::string> equippedModuleIds;
    std::vector<std::string> unlockKeys;
    int blueprintProgress = 0;
    int furthestTier = 0;
    int shipsLost = 0;
    int astronautsLost = 0;
    std::vector<std::string> memorials;
    std::vector<std::string> famousLaunches;
    std::vector<Astronaut> crew;
};

SaveData captureSaveData(const GameState& state);
void restoreSaveData(GameState& state, const ContentCatalog& catalog, const SaveData& save);

std::string serializeSaveData(const SaveData& save);
std::optional<SaveData> deserializeSaveData(std::string_view text);

} // namespace rocket
