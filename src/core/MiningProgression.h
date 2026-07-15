#pragma once

#include "core/GameTypes.h"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace rocket {

inline constexpr int miningArenaRulesVersion = 1;

struct MiningCampaignProgression {
    bool miningAvailable = false;
    MiningAct act = MiningAct::ActOne;
    int minimumDifficulty = 1;
    int maximumDifficulty = 1;
    int difficulty = 1;
};

MiningProgressionBand miningProgressionBandForDifficulty(int difficulty);
std::string_view miningActName(MiningAct act);
std::string_view miningProgressionBandName(MiningProgressionBand band);
MiningArenaRules resolveMiningArenaRules(const MiningArenaRequest& request);

MiningCampaignProgression resolveCampaignMiningProgression(
    GameChapter chapter,
    std::string_view destinationId,
    int surfaceDepth,
    int completedHostileSorties);

std::uint64_t deriveMiningArenaSeed(
    std::uint64_t campaignSeed,
    std::string_view destinationId,
    int landingOrdinal,
    int surfaceDepth);

MiningArenaRequest campaignMiningArenaRequest(
    GameChapter chapter,
    std::string_view destinationId,
    int surfaceDepth,
    int completedHostileSorties,
    std::uint64_t campaignSeed,
    int landingOrdinal);

MiningRewardBudget effectiveMiningRewardBudget(const MiningArenaRules& rules, bool firstClearFulfilled);

std::size_t miningFirstClearProgressIndex(MiningAct act, MiningProgressionBand band);
const MiningFirstClearProgress& miningFirstClearProgress(
    const MetaProgress& meta,
    MiningAct act,
    MiningProgressionBand band);
bool miningFirstClearFulfilled(const MetaProgress& meta, const MiningArenaRules& rules);
void creditBankedMiningFirstClearRewards(
    MetaProgress& meta,
    const MiningArenaRules& rules,
    int rareBanked,
    int exoticBanked);

bool miningMaterialAllowed(const MiningArenaRules& rules, MiningCellMaterial material);
bool miningEnemyAllowed(const MiningArenaRules& rules, MiningEnemyType enemy);
bool miningAffinityAllowed(const MiningArenaRules& rules, MiningElementalAffinity affinity);
bool miningRoomFeatureAllowed(const MiningArenaRules& rules, MiningCellFeature feature);

} // namespace rocket
