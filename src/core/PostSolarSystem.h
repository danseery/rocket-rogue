#pragma once

#include "core/GameTypes.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace rocket {

struct PostSolarGeologyProfile {
    std::string_view id;
    std::string_view name;
    int atlasRow = 0;
    MiningElementalAffinity hazardBias = MiningElementalAffinity::None;
};

inline constexpr int postSolarGeologyFrameCount = 19;
inline constexpr int postSolarGeologyProfileCount = 32;
inline constexpr int postSolarSystemGeneratorVersion = 1;

std::span<const PostSolarGeologyProfile> postSolarGeologyCatalog() noexcept;
const PostSolarGeologyProfile* findPostSolarGeology(std::string_view id) noexcept;
int postSolarGeologyRow(std::string_view id) noexcept;

PostSolarSystemRoster generatePostSolarSystemRoster(
    std::string_view systemId,
    std::uint64_t campaignSeed);

PostSolarSystemRoster& ensurePostSolarSystemRoster(
    MetaProgress& meta,
    std::string_view systemId,
    std::uint64_t campaignSeed);

const PostSolarSystemRoster* findPostSolarSystemRoster(
    const MetaProgress& meta,
    std::string_view systemId) noexcept;

const PostSolarBodyProfile* findPostSolarBody(
    const PostSolarSystemRoster& roster,
    std::string_view bodyId) noexcept;

const PostSolarBodyProfile* primaryPostSolarBody(
    const PostSolarSystemRoster& roster) noexcept;

std::string_view postSolarSystemForDestination(std::string_view destinationId) noexcept;

} // namespace rocket
