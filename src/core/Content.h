#pragma once

#include "core/GameTypes.h"

#include <string_view>

namespace rocket {

struct ContentCatalog {
    std::vector<ShipModule> modules;
    std::vector<ShipFrame> frames;
    std::vector<Astronaut> astronauts;
    std::vector<Destination> destinations;

    const ShipModule* findModule(std::string_view id) const;
    const ShipFrame* findFrame(std::string_view id) const;
    const Astronaut* findAstronaut(std::string_view id) const;
    const Destination* findDestination(std::string_view id) const;
};

ContentCatalog createDefaultContent();

bool hasUnlock(const MetaProgress& meta, std::string_view key);
bool isModuleUnlocked(const MetaProgress& meta, const ShipModule& module);
std::vector<const ShipModule*> unlockedModules(const ContentCatalog& catalog, const MetaProgress& meta);

} // namespace rocket

