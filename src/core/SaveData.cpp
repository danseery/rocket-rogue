#include "core/SaveData.h"
#include "core/ContentIds.h"
#include "core/GameText.h"
#include "core/SaveSchema.h"

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

std::string joinInts(const std::vector<int>& values, char delimiter)
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

template <typename Value>
void writeField(std::ostringstream& out, std::string_view key, const Value& value)
{
    out << key << save_schema::keyValueDelimiter << value << "\n";
}

std::string serializeCrew(const std::vector<Astronaut>& crew)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < crew.size(); ++i) {
        if (i > 0) {
            out << save_schema::crewRecordDelimiter;
        }
        out << crew[i].id
            << save_schema::crewFieldDelimiter << crew[i].training
            << save_schema::crewFieldDelimiter << crew[i].stress
            << save_schema::crewFieldDelimiter << statusToInt(crew[i].status);
    }
    return out.str();
}

std::vector<Astronaut> parseCrew(std::string_view text)
{
    std::vector<Astronaut> crew;
    for (const std::string& record : split(text, save_schema::crewRecordDelimiter)) {
        const std::vector<std::string> fields = split(record, save_schema::crewFieldDelimiter);
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

std::vector<int> parseInts(std::string_view text)
{
    std::vector<int> values;
    for (const std::string& item : split(text, save_schema::listDelimiter)) {
        values.push_back(parseInt(item, 0));
    }
    return values;
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
    save.offerRerollsThisExpedition = state.run.offerRerollsThisExpedition;
    save.repairOpsThisExpedition = state.run.repairOpsThisExpedition;
    save.trainingOpsThisExpedition = state.run.trainingOpsThisExpedition;
    save.restOpsThisExpedition = state.run.restOpsThisExpedition;
    save.inventoryModuleIds = state.run.inventoryModuleIds;
    save.equippedModuleIds = state.run.equippedModuleIds;
    save.crewUpgradeIds = state.run.crewUpgradeIds;
    save.unlockKeys = state.meta.unlockKeys;
    save.blueprintProgress = state.meta.blueprintProgress;
    save.furthestTier = state.meta.furthestTier;
    save.shipsLost = state.meta.shipsLost;
    save.astronautsLost = state.meta.astronautsLost;
    save.destinationAttempts = state.meta.destinationAttempts;
    save.destinationSuccesses = state.meta.destinationSuccesses;
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
    state.run.offerRerollsThisExpedition = std::max(0, save.offerRerollsThisExpedition);
    state.run.repairOpsThisExpedition = std::max(0, save.repairOpsThisExpedition);
    state.run.trainingOpsThisExpedition = std::max(0, save.trainingOpsThisExpedition);
    state.run.restOpsThisExpedition = std::max(0, save.restOpsThisExpedition);
    state.run.inventoryModuleIds = save.inventoryModuleIds.empty() ? state.run.inventoryModuleIds : save.inventoryModuleIds;
    state.run.equippedModuleIds = save.equippedModuleIds.empty() ? state.run.equippedModuleIds : save.equippedModuleIds;
    state.run.crewUpgradeIds = save.crewUpgradeIds;
    state.meta.unlockKeys = save.unlockKeys.empty() ? std::vector<std::string>{content::unlock::starter} : save.unlockKeys;
    state.meta.blueprintProgress = save.blueprintProgress;
    state.meta.furthestTier = save.furthestTier;
    state.meta.shipsLost = save.shipsLost;
    state.meta.astronautsLost = save.astronautsLost;
    state.meta.destinationAttempts = save.destinationAttempts;
    state.meta.destinationSuccesses = save.destinationSuccesses;
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
                recruit.name = text::isReplacementId(savedAstronaut.id) ? std::string(text::panel::messages::replacementCadet) : savedAstronaut.id;
                recruit.background = std::string(text::panel::messages::restoredCrewBackground);
                recruit.trait = std::string(text::panel::messages::generatedRecruitTrait);
                state.run.crew.push_back(recruit);
            }
        }
    }
    syncLaunchConfig(state, catalog);
}

std::string serializeSaveData(const SaveData& save)
{
    std::ostringstream out;
    out << save_schema::header << "\n";
    writeField(out, save_schema::field::version, save.version);
    writeField(out, save_schema::field::seed, save.seed);
    writeField(out, save_schema::field::credits, save.credits);
    writeField(out, save_schema::field::destinationIndex, save.destinationIndex);
    writeField(out, save_schema::field::frontierReadiness, save.frontierReadiness);
    writeField(out, save_schema::field::shipDamage, save.shipDamage);
    writeField(out, save_schema::field::frameId, save.frameId);
    writeField(out, save_schema::field::offerRerolls, save.offerRerollsThisExpedition);
    writeField(out, save_schema::field::repairOps, save.repairOpsThisExpedition);
    writeField(out, save_schema::field::trainingOps, save.trainingOpsThisExpedition);
    writeField(out, save_schema::field::restOps, save.restOpsThisExpedition);
    writeField(out, save_schema::field::inventory, join(save.inventoryModuleIds, save_schema::listDelimiter));
    writeField(out, save_schema::field::equipped, join(save.equippedModuleIds, save_schema::listDelimiter));
    writeField(out, save_schema::field::crewUpgrades, join(save.crewUpgradeIds, save_schema::listDelimiter));
    writeField(out, save_schema::field::unlocks, join(save.unlockKeys, save_schema::listDelimiter));
    writeField(out, save_schema::field::blueprints, save.blueprintProgress);
    writeField(out, save_schema::field::furthestTier, save.furthestTier);
    writeField(out, save_schema::field::shipsLost, save.shipsLost);
    writeField(out, save_schema::field::astronautsLost, save.astronautsLost);
    writeField(out, save_schema::field::destinationAttempts, joinInts(save.destinationAttempts, save_schema::listDelimiter));
    writeField(out, save_schema::field::destinationSuccesses, joinInts(save.destinationSuccesses, save_schema::listDelimiter));
    writeField(out, save_schema::field::memorials, join(save.memorials, save_schema::textListDelimiter));
    writeField(out, save_schema::field::famousLaunches, join(save.famousLaunches, save_schema::textListDelimiter));
    writeField(out, save_schema::field::crew, serializeCrew(save.crew));
    return out.str();
}

std::optional<SaveData> deserializeSaveData(std::string_view text)
{
    const std::size_t firstLineEnd = text.find('\n');
    const std::string_view headerLine = firstLineEnd == std::string_view::npos ? text : text.substr(0, firstLineEnd);
    if (headerLine != save_schema::header) {
        return std::nullopt;
    }

    SaveData save;
    for (std::string_view line : split(text, '\n')) {
        const std::size_t equals = line.find(save_schema::keyValueDelimiter);
        if (equals == std::string_view::npos) {
            continue;
        }

        const std::string_view key = line.substr(0, equals);
        const std::string_view value = line.substr(equals + 1);

        if (key == save_schema::field::version) {
            save.version = parseInt(value, save.version);
        } else if (key == save_schema::field::seed) {
            save.seed = parseU64(value, save.seed);
        } else if (key == save_schema::field::credits) {
            save.credits = parseDouble(value, save.credits);
        } else if (key == save_schema::field::destinationIndex) {
            save.destinationIndex = parseInt(value, save.destinationIndex);
        } else if (key == save_schema::field::frontierReadiness) {
            save.frontierReadiness = parseInt(value, save.frontierReadiness);
        } else if (key == save_schema::field::shipDamage) {
            save.shipDamage = parseInt(value, save.shipDamage);
        } else if (key == save_schema::field::frameId) {
            save.frameId = std::string(value);
        } else if (key == save_schema::field::offerRerolls) {
            save.offerRerollsThisExpedition = parseInt(value, save.offerRerollsThisExpedition);
        } else if (key == save_schema::field::repairOps) {
            save.repairOpsThisExpedition = parseInt(value, save.repairOpsThisExpedition);
        } else if (key == save_schema::field::trainingOps) {
            save.trainingOpsThisExpedition = parseInt(value, save.trainingOpsThisExpedition);
        } else if (key == save_schema::field::restOps) {
            save.restOpsThisExpedition = parseInt(value, save.restOpsThisExpedition);
        } else if (key == save_schema::field::inventory) {
            save.inventoryModuleIds = split(value, save_schema::listDelimiter);
        } else if (key == save_schema::field::equipped) {
            save.equippedModuleIds = split(value, save_schema::listDelimiter);
        } else if (key == save_schema::field::crewUpgrades) {
            save.crewUpgradeIds = split(value, save_schema::listDelimiter);
        } else if (key == save_schema::field::unlocks) {
            save.unlockKeys = split(value, save_schema::listDelimiter);
        } else if (key == save_schema::field::blueprints) {
            save.blueprintProgress = parseInt(value, save.blueprintProgress);
        } else if (key == save_schema::field::furthestTier) {
            save.furthestTier = parseInt(value, save.furthestTier);
        } else if (key == save_schema::field::shipsLost) {
            save.shipsLost = parseInt(value, save.shipsLost);
        } else if (key == save_schema::field::astronautsLost) {
            save.astronautsLost = parseInt(value, save.astronautsLost);
        } else if (key == save_schema::field::destinationAttempts) {
            save.destinationAttempts = parseInts(value);
        } else if (key == save_schema::field::destinationSuccesses) {
            save.destinationSuccesses = parseInts(value);
        } else if (key == save_schema::field::memorials) {
            save.memorials = split(value, save_schema::textListDelimiter);
        } else if (key == save_schema::field::famousLaunches) {
            save.famousLaunches = split(value, save_schema::textListDelimiter);
        } else if (key == save_schema::field::crew) {
            save.crew = parseCrew(value);
        }
    }

    return save;
}

} // namespace rocket
