#include "core/PostSolarSystem.h"

#include "core/ContentIds.h"
#include "core/Random.h"

#include <algorithm>
#include <array>
#include <string>

namespace rocket {
namespace {

constexpr std::array<PostSolarGeologyProfile, postSolarGeologyProfileCount> kGeologies {{
    {"island_basalt", "Island Basalt", 0, MiningElementalAffinity::Thermal},
    {"coral_limestone", "Coral Limestone", 1, MiningElementalAffinity::None},
    {"jade_sediment", "Jade Sediment", 2, MiningElementalAffinity::Toxic},
    {"cyan_fracture_basalt", "Cyan Fracture Basalt", 3, MiningElementalAffinity::Cryo},
    {"nickel_iron_crater", "Nickel-Iron Crater", 4, MiningElementalAffinity::Radiation},
    {"voidglass_breccia", "Voidglass Breccia", 5, MiningElementalAffinity::Cryo},
    {"violet_crystal", "Violet Crystal", 6, MiningElementalAffinity::Toxic},
    {"ammonia_ice", "Ammonia Ice", 7, MiningElementalAffinity::Cryo},
    {"pale_silicate", "Pale Silicate", 8, MiningElementalAffinity::None},
    {"sulfur_salt", "Sulfur Salt", 9, MiningElementalAffinity::Thermal},
    {"radiation_glass", "Radiation Glass", 10, MiningElementalAffinity::Radiation},
    {"toxic_tarstone", "Toxic Tarstone", 11, MiningElementalAffinity::Toxic},
    {"molten_obsidian", "Molten Obsidian", 12, MiningElementalAffinity::Thermal},
    {"black_scoria", "Black Scoria", 13, MiningElementalAffinity::Thermal},
    {"basalt_columns", "Basalt Columns", 14, MiningElementalAffinity::Thermal},
    {"water_ice", "Water Ice", 15, MiningElementalAffinity::Cryo},
    {"clathrate_ice", "Clathrate Ice", 16, MiningElementalAffinity::Cryo},
    {"cryovolcanic_slush", "Cryovolcanic Slush", 17, MiningElementalAffinity::Cryo},
    {"gold_regolith", "Gold Regolith", 18, MiningElementalAffinity::Radiation},
    {"salt_evaporite", "Salt Evaporite", 19, MiningElementalAffinity::None},
    {"desert_sandstone", "Desert Sandstone", 20, MiningElementalAffinity::Thermal},
    {"teal_xenobasalt", "Teal Xenobasalt", 21, MiningElementalAffinity::Toxic},
    {"opaline_crystal", "Opaline Crystal", 22, MiningElementalAffinity::Toxic},
    {"methane_ice", "Methane Ice", 23, MiningElementalAffinity::Cryo},
    {"ferric_breccia", "Ferric Breccia", 24, MiningElementalAffinity::Thermal},
    {"copper_oxide", "Copper Oxide", 25, MiningElementalAffinity::Radiation},
    {"carbonaceous_chondrite", "Carbonaceous Chondrite", 26, MiningElementalAffinity::None},
    {"rift_obsidian", "Rift Obsidian", 27, MiningElementalAffinity::Cryo},
    {"phase_glass", "Phase Glass", 28, MiningElementalAffinity::Toxic},
    {"cobalt_shard_ice", "Cobalt Shard Ice", 29, MiningElementalAffinity::Cryo},
    {"voidstone", "Voidstone", 30, MiningElementalAffinity::Radiation},
    {"resonant_gold", "Resonant Gold", 31, MiningElementalAffinity::Radiation},
}};

constexpr std::array<std::array<std::string_view, 3>, 9> kPortraitGeologies {{
    std::array<std::string_view, 3> {"island_basalt", "coral_limestone", "jade_sediment"},
    std::array<std::string_view, 3> {"cyan_fracture_basalt", "nickel_iron_crater", "voidglass_breccia"},
    std::array<std::string_view, 3> {"violet_crystal", "ammonia_ice", "pale_silicate"},
    std::array<std::string_view, 3> {"sulfur_salt", "radiation_glass", "toxic_tarstone"},
    std::array<std::string_view, 3> {"molten_obsidian", "black_scoria", "basalt_columns"},
    std::array<std::string_view, 3> {"water_ice", "clathrate_ice", "cryovolcanic_slush"},
    std::array<std::string_view, 3> {"gold_regolith", "salt_evaporite", "desert_sandstone"},
    std::array<std::string_view, 3> {"teal_xenobasalt", "opaline_crystal", "methane_ice"},
    std::array<std::string_view, 3> {"ferric_breccia", "copper_oxide", "carbonaceous_chondrite"},
}};

constexpr std::array<std::string_view, 5> kRiftGeologies {{
    "rift_obsidian", "phase_glass", "cobalt_shard_ice", "voidstone", "resonant_gold"
}};

std::uint64_t stableHash(std::string_view text)
{
    std::uint64_t value = 1469598103934665603ULL;
    for (const unsigned char character : text) {
        value ^= character;
        value *= 1099511628211ULL;
    }
    return value;
}

std::uint64_t systemSeed(std::uint64_t campaignSeed, std::string_view systemId)
{
    std::uint64_t value = campaignSeed ^ stableHash(systemId);
    value ^= static_cast<std::uint64_t>(postSolarSystemGeneratorVersion) * 0x9E3779B97F4A7C15ULL;
    return value == 0 ? 1 : value;
}

std::string romanNumeral(int value)
{
    static constexpr std::array<std::string_view, 12> numerals {{
        "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII"
    }};
    return value >= 1 && value <= static_cast<int>(numerals.size())
        ? std::string(numerals[static_cast<std::size_t>(value - 1)])
        : std::to_string(value);
}

std::string systemDisplayName(std::string_view systemId)
{
    if (systemId == content::postSolarSystem::aaruVale) return "Aaru Vale";
    if (systemId == content::postSolarSystem::khepriPrime) return "Khepri Prime";
    return "Rift";
}

MiningElementalAffinity geologyBias(std::string_view id)
{
    const PostSolarGeologyProfile* profile = findPostSolarGeology(id);
    return profile != nullptr ? profile->hazardBias : MiningElementalAffinity::None;
}

std::pair<std::string, std::string> chooseGeology(Random& random, int portrait, bool rift)
{
    if (rift) {
        const std::string surface(kRiftGeologies[static_cast<std::size_t>(random.rangeInt(0, 4))]);
        const bool alternateDeep = random.chance(0.55);
        const std::string deep = alternateDeep
            ? std::string(kRiftGeologies[static_cast<std::size_t>(random.rangeInt(0, 4))])
            : surface;
        return {surface, deep};
    }
    const std::size_t portraitIndex = static_cast<std::size_t>(std::clamp(portrait, 1, 9) - 1);
    const auto& candidates = kPortraitGeologies[portraitIndex];
    const int surfaceIndex = random.rangeInt(0, 2);
    const int deepIndex = random.chance(0.35) ? random.rangeInt(0, 2) : surfaceIndex;
    return {std::string(candidates[static_cast<std::size_t>(surfaceIndex)]),
            std::string(candidates[static_cast<std::size_t>(deepIndex)])};
}

PostSolarBodyProfile makeBody(
    Random& random,
    std::string_view systemId,
    int primaryOrdinal,
    int moonOrdinal,
    PostSolarBodyKind kind,
    int portrait,
    std::string parentId = {})
{
    const bool rift = systemId == content::postSolarSystem::riftBelt;
    const std::string baseId = std::string(systemId) + "_" + (rift ? "fragment_" : "body_") + std::to_string(primaryOrdinal);
    PostSolarBodyProfile body;
    body.id = moonOrdinal > 0 ? baseId + "_moon_" + std::to_string(moonOrdinal) : baseId;
    body.name = rift
        ? "Rift Fragment " + romanNumeral(primaryOrdinal)
        : systemDisplayName(systemId) + " " + romanNumeral(primaryOrdinal);
    if (moonOrdinal > 0) {
        body.name += "-" + std::string(1, static_cast<char>('a' + std::min(25, moonOrdinal - 1)));
    }
    body.parentId = std::move(parentId);
    body.kind = kind;
    body.visualArchetype = portrait;
    const auto [surface, deep] = chooseGeology(random, portrait, rift);
    body.surfaceGeologyId = surface;
    body.deepGeologyId = deep;
    body.hazardBias = geologyBias(surface);
    body.seed = random.nextU64();
    body.mineable = kind != PostSolarBodyKind::Giant;
    return body;
}

int chooseFrom(Random& random, std::span<const int> choices)
{
    return choices[static_cast<std::size_t>(random.rangeInt(0, static_cast<int>(choices.size()) - 1))];
}

} // namespace

std::span<const PostSolarGeologyProfile> postSolarGeologyCatalog() noexcept
{
    return kGeologies;
}

const PostSolarGeologyProfile* findPostSolarGeology(std::string_view id) noexcept
{
    const auto found = std::find_if(kGeologies.begin(), kGeologies.end(), [id](const auto& profile) {
        return profile.id == id;
    });
    return found != kGeologies.end() ? &*found : nullptr;
}

int postSolarGeologyRow(std::string_view id) noexcept
{
    const PostSolarGeologyProfile* profile = findPostSolarGeology(id);
    return profile != nullptr ? profile->atlasRow : -1;
}

PostSolarSystemRoster generatePostSolarSystemRoster(std::string_view systemId, std::uint64_t campaignSeed)
{
    PostSolarSystemRoster roster;
    roster.systemId = std::string(systemId);
    roster.generatorVersion = postSolarSystemGeneratorVersion;
    roster.seed = systemSeed(campaignSeed, systemId);
    Random random(roster.seed);

    const bool aaru = systemId == content::postSolarSystem::aaruVale;
    const bool khepri = systemId == content::postSolarSystem::khepriPrime;
    const bool rift = systemId == content::postSolarSystem::riftBelt;
    if (!aaru && !khepri && !rift) {
        return roster;
    }

    if (rift) {
        const int fragments = random.rangeInt(4, 8);
        constexpr std::array<int, 3> portraits {{2, 6, 9}};
        for (int ordinal = 1; ordinal <= fragments; ++ordinal) {
            roster.bodies.push_back(makeBody(
                random, systemId, ordinal, 0, PostSolarBodyKind::MinorBody,
                chooseFrom(random, portraits)));
        }
        roster.primaryBodyId = roster.bodies.front().id;
        return roster;
    }

    constexpr std::array<int, 4> aaruLandable {{1, 6, 7, 9}};
    constexpr std::array<int, 2> aaruGiants {{3, 8}};
    constexpr std::array<int, 5> khepriLandable {{2, 5, 6, 7, 9}};
    constexpr std::array<int, 3> khepriGiants {{3, 4, 8}};
    const int primaryCount = aaru ? random.rangeInt(4, 6) : random.rangeInt(3, 5);
    for (int ordinal = 1; ordinal <= primaryCount; ++ordinal) {
        // The first three bodies are always landable. Later slots may become
        // giants, guaranteeing a useful roster without treating a giant as a
        // mining surface.
        const bool giant = ordinal > 3 && random.chance(aaru ? 0.55 : 0.42);
        const int portrait = giant
            ? chooseFrom(random, aaru ? std::span<const int>(aaruGiants) : std::span<const int>(khepriGiants))
            : chooseFrom(random, aaru ? std::span<const int>(aaruLandable) : std::span<const int>(khepriLandable));
        roster.bodies.push_back(makeBody(
            random, systemId, ordinal, 0,
            giant ? PostSolarBodyKind::Giant : PostSolarBodyKind::Terrestrial,
            portrait));
    }

    const int targetMoonCount = aaru ? random.rangeInt(3, 7) : random.rangeInt(2, 8);
    constexpr std::array<int, 4> aaruMoons {{2, 6, 7, 9}};
    constexpr std::array<int, 5> khepriMoons {{2, 5, 6, 7, 9}};
    std::vector<int> moonsPerPrimary(static_cast<std::size_t>(primaryCount), 0);
    for (int moon = 0; moon < targetMoonCount; ++moon) {
        const int parentIndex = moon % primaryCount;
        const PostSolarBodyProfile parent = roster.bodies[static_cast<std::size_t>(parentIndex)];
        const int moonOrdinal = ++moonsPerPrimary[static_cast<std::size_t>(parentIndex)];
        const int portrait = chooseFrom(random, aaru
            ? std::span<const int>(aaruMoons)
            : std::span<const int>(khepriMoons));
        roster.bodies.push_back(makeBody(
            random, systemId, parentIndex + 1, moonOrdinal,
            PostSolarBodyKind::Moon, portrait, parent.id));
    }

    const auto primary = std::find_if(roster.bodies.begin(), roster.bodies.end(), [](const auto& body) {
        return body.mineable && body.parentId.empty();
    });
    if (primary != roster.bodies.end()) {
        roster.primaryBodyId = primary->id;
    }
    return roster;
}

PostSolarSystemRoster& ensurePostSolarSystemRoster(
    MetaProgress& meta,
    std::string_view systemId,
    std::uint64_t campaignSeed)
{
    const auto found = std::find_if(meta.postSolarSystemRosters.begin(), meta.postSolarSystemRosters.end(),
        [systemId](const auto& roster) { return roster.systemId == systemId; });
    if (found != meta.postSolarSystemRosters.end()) {
        return *found;
    }
    meta.postSolarSystemRosters.push_back(generatePostSolarSystemRoster(systemId, campaignSeed));
    return meta.postSolarSystemRosters.back();
}

const PostSolarSystemRoster* findPostSolarSystemRoster(const MetaProgress& meta, std::string_view systemId) noexcept
{
    const auto found = std::find_if(meta.postSolarSystemRosters.begin(), meta.postSolarSystemRosters.end(),
        [systemId](const auto& roster) { return roster.systemId == systemId; });
    return found != meta.postSolarSystemRosters.end() ? &*found : nullptr;
}

const PostSolarBodyProfile* findPostSolarBody(const PostSolarSystemRoster& roster, std::string_view bodyId) noexcept
{
    const auto found = std::find_if(roster.bodies.begin(), roster.bodies.end(),
        [bodyId](const auto& body) { return body.id == bodyId; });
    return found != roster.bodies.end() ? &*found : nullptr;
}

const PostSolarBodyProfile* primaryPostSolarBody(const PostSolarSystemRoster& roster) noexcept
{
    return findPostSolarBody(roster, roster.primaryBodyId);
}

std::string_view postSolarSystemForDestination(std::string_view destinationId) noexcept
{
    if (destinationId == content::destination::nearbyStar) {
        return content::postSolarSystem::khepriPrime;
    }
    if (destinationId == content::destination::nearbyGalaxy) {
        return content::postSolarSystem::riftBelt;
    }
    return {};
}

} // namespace rocket
