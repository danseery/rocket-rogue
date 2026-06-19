#include "core/SaveData.h"

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <sstream>

namespace rocket {

namespace {

std::vector<std::string> split(std::string_view text, char delimiter)
{
    std::vector<std::string> values;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(delimiter, start);
        const std::size_t count = end == std::string_view::npos ? text.size() - start : end - start;
        if (count > 0) {
            values.emplace_back(text.substr(start, count));
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return values;
}

std::string join(const std::vector<std::string>& values, char delimiter)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << delimiter;
        }
        out << values[i];
    }
    return out.str();
}

int statusToInt(CrewStatus status)
{
    switch (status) {
    case CrewStatus::Active:
        return 0;
    case CrewStatus::Injured:
        return 1;
    case CrewStatus::Dead:
        return 2;
    }
    return 0;
}

CrewStatus statusFromInt(int value)
{
    switch (value) {
    case 1:
        return CrewStatus::Injured;
    case 2:
        return CrewStatus::Dead;
    default:
        return CrewStatus::Active;
    }
}

int parseInt(std::string_view text, int fallback)
{
    int value = fallback;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

std::uint64_t parseU64(std::string_view text, std::uint64_t fallback)
{
    std::uint64_t value = fallback;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

double parseDouble(std::string_view text, double fallback)
{
    std::string copy(text);
    char* end = nullptr;
    const double value = std::strtod(copy.c_str(), &end);
    return end == copy.c_str() ? fallback : value;
}

std::string serializeCrew(const std::vector<Astronaut>& crew)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < crew.size(); ++i) {
        if (i > 0) {
            out << ';';
        }
        out << crew[i].id << ':' << crew[i].training << ':' << crew[i].stress << ':' << statusToInt(crew[i].status);
    }
    return out.str();
}

std::vector<Astronaut> parseCrew(std::string_view text)
{
    std::vector<Astronaut> crew;
    for (const std::string& record : split(text, ';')) {
        const std::vector<std::string> fields = split(record, ':');
        if (fields.empty()) {
            continue;
        }

        Astronaut astronaut;
        astronaut.id = fields[0];
        if (fields.size() > 1) {
            astronaut.training = parseInt(fields[1], 0);
        }
        if (fields.size() > 2) {
            astronaut.stress = parseInt(fields[2], 0);
        }
        if (fields.size() > 3) {
            astronaut.status = statusFromInt(parseInt(fields[3], 0));
        }
        crew.push_back(astronaut);
    }
    return crew;
}

} // namespace

SaveData captureSaveData(const GameState& state)
{
    SaveData save;
    save.seed = state.seed;
    save.credits = state.run.credits;
    save.destinationIndex = state.run.destinationIndex;
    save.frontierReadiness = state.run.frontierReadiness;
    save.shipDamage = state.run.shipDamage;
    save.frameId = state.run.frameId;
    save.inventoryModuleIds = state.run.inventoryModuleIds;
    save.equippedModuleIds = state.run.equippedModuleIds;
    save.unlockKeys = state.meta.unlockKeys;
    save.blueprintProgress = state.meta.blueprintProgress;
    save.furthestTier = state.meta.furthestTier;
    save.shipsLost = state.meta.shipsLost;
    save.astronautsLost = state.meta.astronautsLost;
    save.memorials = state.meta.memorials;
    save.famousLaunches = state.meta.famousLaunches;
    save.crew = state.run.crew;
    return save;
}

void restoreSaveData(GameState& state, const ContentCatalog& catalog, const SaveData& save)
{
    state.seed = save.seed;
    state.run.credits = save.credits;
    state.run.destinationIndex = std::clamp(save.destinationIndex, 0, static_cast<int>(catalog.destinations.size()) - 1);
    state.run.frontierReadiness = std::max(0, save.frontierReadiness);
    state.run.shipDamage = std::clamp(save.shipDamage, 0, 100);
    state.run.frameId = catalog.findFrame(save.frameId) == nullptr ? catalog.frames.front().id : save.frameId;
    state.run.inventoryModuleIds = save.inventoryModuleIds.empty() ? state.run.inventoryModuleIds : save.inventoryModuleIds;
    state.run.equippedModuleIds = save.equippedModuleIds.empty() ? state.run.equippedModuleIds : save.equippedModuleIds;
    state.meta.unlockKeys = save.unlockKeys.empty() ? std::vector<std::string>{"starter"} : save.unlockKeys;
    state.meta.blueprintProgress = save.blueprintProgress;
    state.meta.furthestTier = save.furthestTier;
    state.meta.shipsLost = save.shipsLost;
    state.meta.astronautsLost = save.astronautsLost;
    state.meta.memorials = save.memorials;
    state.meta.famousLaunches = save.famousLaunches;
    if (!save.crew.empty()) {
        for (auto& astronaut : state.run.crew) {
            const auto found = std::find_if(save.crew.begin(), save.crew.end(), [&](const Astronaut& savedAstronaut) {
                return savedAstronaut.id == astronaut.id;
            });
            if (found != save.crew.end()) {
                astronaut.training = found->training;
                astronaut.stress = found->stress;
                astronaut.status = found->status;
            }
        }

        for (const Astronaut& savedAstronaut : save.crew) {
            const auto found = std::find_if(state.run.crew.begin(), state.run.crew.end(), [&](const Astronaut& existing) {
                return existing.id == savedAstronaut.id;
            });
            if (found == state.run.crew.end()) {
                Astronaut recruit = savedAstronaut;
                recruit.name = savedAstronaut.id.find("replacement_") == 0 ? "Replacement Cadet" : savedAstronaut.id;
                recruit.background = "Restored crew record";
                recruit.trait = "Learns quickly";
                state.run.crew.push_back(recruit);
            }
        }
    }
    syncLaunchConfig(state, catalog);
}

std::string serializeSaveData(const SaveData& save)
{
    std::ostringstream out;
    out << "RR_SAVE_V1\n";
    out << "version=" << save.version << "\n";
    out << "seed=" << save.seed << "\n";
    out << "credits=" << save.credits << "\n";
    out << "destinationIndex=" << save.destinationIndex << "\n";
    out << "frontierReadiness=" << save.frontierReadiness << "\n";
    out << "shipDamage=" << save.shipDamage << "\n";
    out << "frameId=" << save.frameId << "\n";
    out << "inventory=" << join(save.inventoryModuleIds, ',') << "\n";
    out << "equipped=" << join(save.equippedModuleIds, ',') << "\n";
    out << "unlocks=" << join(save.unlockKeys, ',') << "\n";
    out << "blueprints=" << save.blueprintProgress << "\n";
    out << "furthestTier=" << save.furthestTier << "\n";
    out << "shipsLost=" << save.shipsLost << "\n";
    out << "astronautsLost=" << save.astronautsLost << "\n";
    out << "memorials=" << join(save.memorials, '|') << "\n";
    out << "famousLaunches=" << join(save.famousLaunches, '|') << "\n";
    out << "crew=" << serializeCrew(save.crew) << "\n";
    return out.str();
}

std::optional<SaveData> deserializeSaveData(std::string_view text)
{
    if (text.empty() || text.find("RR_SAVE_V1") != 0) {
        return std::nullopt;
    }

    SaveData save;
    for (std::string_view line : split(text, '\n')) {
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            continue;
        }

        const std::string_view key = line.substr(0, equals);
        const std::string_view value = line.substr(equals + 1);

        if (key == "version") {
            save.version = parseInt(value, save.version);
        } else if (key == "seed") {
            save.seed = parseU64(value, save.seed);
        } else if (key == "credits") {
            save.credits = parseDouble(value, save.credits);
        } else if (key == "destinationIndex") {
            save.destinationIndex = parseInt(value, save.destinationIndex);
        } else if (key == "frontierReadiness") {
            save.frontierReadiness = parseInt(value, save.frontierReadiness);
        } else if (key == "shipDamage") {
            save.shipDamage = parseInt(value, save.shipDamage);
        } else if (key == "frameId") {
            save.frameId = std::string(value);
        } else if (key == "inventory") {
            save.inventoryModuleIds = split(value, ',');
        } else if (key == "equipped") {
            save.equippedModuleIds = split(value, ',');
        } else if (key == "unlocks") {
            save.unlockKeys = split(value, ',');
        } else if (key == "blueprints") {
            save.blueprintProgress = parseInt(value, save.blueprintProgress);
        } else if (key == "furthestTier") {
            save.furthestTier = parseInt(value, save.furthestTier);
        } else if (key == "shipsLost") {
            save.shipsLost = parseInt(value, save.shipsLost);
        } else if (key == "astronautsLost") {
            save.astronautsLost = parseInt(value, save.astronautsLost);
        } else if (key == "memorials") {
            save.memorials = split(value, '|');
        } else if (key == "famousLaunches") {
            save.famousLaunches = split(value, '|');
        } else if (key == "crew") {
            save.crew = parseCrew(value);
        }
    }

    return save;
}

} // namespace rocket
