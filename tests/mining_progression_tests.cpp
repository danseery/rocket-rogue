#include "core/Content.h"
#include "core/ContentIds.h"
#include "core/ArtifactProgression.h"
#include "core/GameState.h"
#include "core/MiningProgression.h"
#include "core/MiningSystem.h"
#include "core/SaveData.h"
#include "core/SaveSchema.h"
#include "core/ScenarioSystem.h"
#include "core/SolarProgression.h"
#include "core/ExpeditionSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <queue>
#include <string>
#include <tuple>

using namespace rocket;

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAILED: " << message << "\n";
        std::exit(3);
    }
}

void allActLevelContractsResolve()
{
    constexpr std::array<MiningRewardBudget, miningFirstClearProgressCount> expectedBudgets {{
        {0, 0, 0, 0}, {1, 0, 1, 0}, {1, 0, 2, 0}, {2, 0, 3, 0},
        {1, 0, 2, 0}, {2, 0, 3, 0}, {2, 0, 4, 1}, {3, 1, 5, 1},
        {3, 1, 5, 1}, {4, 1, 6, 2}, {5, 2, 7, 3}, {6, 3, 8, 4},
    }};
    constexpr std::array<int, 4> actTwoEnemyCaps {2, 4, 6, 8};
    constexpr std::array<int, 4> actThreeEnemyCaps {6, 8, 11, 14};
    constexpr std::array<int, miningFirstClearProgressCount> expectedReferenceSlots {0, 2, 3, 4, 3, 3, 4, 5, 5, 5, 6, 6};
    constexpr std::array<int, miningFirstClearProgressCount> expectedReferenceMarks {0, 1, 1, 2, 1, 1, 2, 2, 3, 3, 3, 3};

    for (int actNumber = 1; actNumber <= 3; ++actNumber) {
        double previousTerrainScale = 0.0;
        double previousHealthScale = 0.0;
        double previousDamageScale = 0.0;
        for (int difficulty = 1; difficulty <= 10; ++difficulty) {
            const MiningAct act = static_cast<MiningAct>(actNumber);
            const MiningArenaRules rules = resolveMiningArenaRules({act, difficulty, 42});
            const MiningProgressionBand band = miningProgressionBandForDifficulty(difficulty);
            const int contractIndex = (actNumber - 1) * 4 + static_cast<int>(band);

            require(rules.request.act == act && rules.request.difficulty == difficulty, "resolver should preserve valid act and difficulty");
            require(rules.band == band, "resolver should assign the expected difficulty band");
            require(rules.rewardBudget.rareGuarantee == expectedBudgets[contractIndex].rareGuarantee, "rare guarantee should match the progression table");
            require(rules.rewardBudget.exoticGuarantee == expectedBudgets[contractIndex].exoticGuarantee, "exotic guarantee should match the progression table");
            require(rules.rewardBudget.rareCap == expectedBudgets[contractIndex].rareCap, "rare cap should match the progression table");
            require(rules.rewardBudget.exoticCap == expectedBudgets[contractIndex].exoticCap, "exotic cap should match the progression table");
            require(rules.terrainToughnessScale > previousTerrainScale, "terrain pressure should increase at every level within an act");
            require(rules.referenceDrones.slots == expectedReferenceSlots[contractIndex], "reference drone slots should match the progression table");
            require(rules.referenceDrones.maximumMark == expectedReferenceMarks[contractIndex], "reference drone mark should match the progression table");
            require(rules.referenceDrones.roleCount <= static_cast<std::size_t>(rules.referenceDrones.slots), "reference roles should fit the expected bay");
            require(miningMaterialAllowed(rules, MiningCellMaterial::CommonOre), "every mining arena should permit common ore");
            require(miningRoomFeatureAllowed(rules, MiningCellFeature::MainTunnel), "every mining arena should permit a main route");

            if (act == MiningAct::ActOne) {
                require(rules.maxActiveEnemies == 0, "Act 1 should never permit active enemies");
                require(!miningMaterialAllowed(rules, MiningCellMaterial::ExoticVein), "Act 1 should never permit exotic mineral veins");
                require(!miningEnemyAllowed(rules, MiningEnemyType::Ant), "Act 1 should have no enemy roster");
                require(rules.enemyHealthScale == 0.0 && rules.enemyDamageScale == 0.0, "Act 1 should not expose combat scaling");
                require(rules.mechanics.fogAndScanner, "Act 1 should use local visibility and scanner pulses from its first level");
                require(rules.mechanics.oxygenAndFuel, "Act 1 endurance resources should be active from the first Moon expedition");
                require(miningMaterialAllowed(rules, MiningCellMaterial::HardRock) == (difficulty >= 3), "Act 1 Hard Rock gate should match its level table");
                require(rules.mechanics.drillHeat == (difficulty >= 4), "Act 1 heat gate should match its level table");
                require(rules.mechanics.cargoDrag == (difficulty >= 5), "Act 1 cargo gate should match its level table");
                require(rules.mechanics.environmentalHazards == (difficulty >= 7), "Act 1 hazard gate should match its level table");
                require(rules.mechanics.artifactRecovery == (difficulty >= 8), "Act 1 artifact gate should match its level table");
                require(miningAffinityAllowed(rules, MiningElementalAffinity::Toxic) == (difficulty >= 9), "Act 1 Toxic gate should match its level table");
            } else {
                require(rules.enemyHealthScale > previousHealthScale, "enemy health pressure should increase at every level within a combat act");
                require(rules.enemyDamageScale > previousDamageScale, "enemy damage pressure should increase at every level within a combat act");
                const int bandIndex = static_cast<int>(band);
                const int expectedCap = act == MiningAct::ActTwo ? actTwoEnemyCaps[bandIndex] : actThreeEnemyCaps[bandIndex];
                require(rules.maxActiveEnemies == expectedCap, "active enemy cap should match the act/band contract");
            }

            if (act == MiningAct::ActTwo) {
                require(!miningEnemyAllowed(rules, MiningEnemyType::Mammal), "Act 2 should not permit Mammals");
                require(!miningAffinityAllowed(rules, MiningElementalAffinity::Radiation), "Act 2 should not permit Radiation");
                require(!miningRoomFeatureAllowed(rules, MiningCellFeature::BossChamber), "Act 2 should not permit boss chambers");
                require(miningEnemyAllowed(rules, MiningEnemyType::Ant), "Act 2 should always retain Ant melee contact");
                require(miningEnemyAllowed(rules, MiningEnemyType::Flying) == (difficulty >= 4), "Act 2 Flying gate should match its level table");
                require(miningEnemyAllowed(rules, MiningEnemyType::Beetle) == (difficulty >= 5), "Act 2 Beetle gate should match its level table");
                require(miningEnemyAllowed(rules, MiningEnemyType::Elemental) == (difficulty >= 7), "Act 2 Elemental gate should match its level table");
                require(miningMaterialAllowed(rules, MiningCellMaterial::ExoticVein) == (difficulty >= 7), "Act 2 Exotic gate should match its level table");
                require(miningAffinityAllowed(rules, MiningElementalAffinity::Toxic) == (difficulty >= 9), "Act 2 Toxic gate should match its level table");
                require(miningRoomFeatureAllowed(rules, MiningCellFeature::MinibossLair) == (difficulty >= 9), "Act 2 miniboss gate should match its level table");
                require(miningEnemyAllowed(rules, MiningEnemyType::Spawner) == (difficulty >= 10), "Act 2 spawner gate should match its level table");
            }

            if (act == MiningAct::ActThree) {
                require(miningEnemyAllowed(rules, MiningEnemyType::Mammal), "Act 3 should always permit Mammal burrowers");
                require(miningAffinityAllowed(rules, MiningElementalAffinity::Radiation) == (difficulty >= 2), "Act 3 Radiation gate should match its level table");
                require(miningEnemyAllowed(rules, MiningEnemyType::Spawner) == (difficulty >= 4), "Act 3 spawner gate should match its level table");
                require(miningRoomFeatureAllowed(rules, MiningCellFeature::MinibossLair) == (difficulty >= 4), "Act 3 miniboss gate should match its level table");
                require(miningRoomFeatureAllowed(rules, MiningCellFeature::BossChamber) == (difficulty >= 7), "Act 3 boss gate should match its level table");
            }

            previousTerrainScale = rules.terrainToughnessScale;
            previousHealthScale = rules.enemyHealthScale;
            previousDamageScale = rules.enemyDamageScale;
        }
    }

    const MiningArenaRules actOneLevelOne = resolveMiningArenaRules({MiningAct::ActOne, 1, 1});
    const MiningArenaRules actOneLevelTwo = resolveMiningArenaRules({MiningAct::ActOne, 2, 1});
    const MiningArenaRules actOneLevelThree = resolveMiningArenaRules({MiningAct::ActOne, 3, 1});
    require(actOneLevelOne.mechanics.fogAndScanner && actOneLevelOne.mechanics.oxygenAndFuel,
        "Act 1 level 1 should teach scanner visibility alongside movement, drilling, fuel, and oxygen");
    require(actOneLevelTwo.mechanics.fogAndScanner && actOneLevelTwo.mechanics.oxygenAndFuel,
        "Act 1 level 2 should retain scanner and endurance resources");
    require(miningMaterialAllowed(actOneLevelThree, MiningCellMaterial::HardRock), "Act 1 level 3 should introduce hard rock");

    const MiningArenaRules actTwoLevelOne = resolveMiningArenaRules({MiningAct::ActTwo, 1, 1});
    const MiningArenaRules actTwoLevelFour = resolveMiningArenaRules({MiningAct::ActTwo, 4, 1});
    const MiningArenaRules actTwoLevelFive = resolveMiningArenaRules({MiningAct::ActTwo, 5, 1});
    const MiningArenaRules actTwoLevelTen = resolveMiningArenaRules({MiningAct::ActTwo, 10, 1});
    require(miningEnemyAllowed(actTwoLevelOne, MiningEnemyType::Ant), "Act 2 should open with Ant melee enemies");
    require(!miningEnemyAllowed(actTwoLevelOne, MiningEnemyType::Flying), "Act 2 should not open with ranged enemies");
    require(miningEnemyAllowed(actTwoLevelFour, MiningEnemyType::Flying), "Act 2 level 4 should introduce Flying enemies");
    require(miningEnemyAllowed(actTwoLevelFive, MiningEnemyType::Beetle), "Act 2 level 5 should introduce Beetles");
    require(miningEnemyAllowed(actTwoLevelTen, MiningEnemyType::Spawner) && actTwoLevelTen.maxSpawners == 1, "Act 2 level 10 should introduce one spawner");

    const MiningArenaRules actThreeLevelOne = resolveMiningArenaRules({MiningAct::ActThree, 1, 1});
    const MiningArenaRules actThreeLevelTwo = resolveMiningArenaRules({MiningAct::ActThree, 2, 1});
    const MiningArenaRules actThreeLevelFour = resolveMiningArenaRules({MiningAct::ActThree, 4, 1});
    const MiningArenaRules actThreeLevelSeven = resolveMiningArenaRules({MiningAct::ActThree, 7, 1});
    const MiningArenaRules actThreeLevelNine = resolveMiningArenaRules({MiningAct::ActThree, 9, 1});
    require(miningEnemyAllowed(actThreeLevelOne, MiningEnemyType::Mammal), "Act 3 level 1 should introduce Mammals");
    require(!miningAffinityAllowed(actThreeLevelOne, MiningElementalAffinity::Radiation), "Mammals and Radiation should be introduced separately");
    require(!miningEnemyAllowed(actThreeLevelOne, MiningEnemyType::Spawner)
            && !miningRoomFeatureAllowed(actThreeLevelOne, MiningCellFeature::MinibossLair),
        "Act 3 Learn should defer spawners and minibosses until the Combine band");
    require(miningAffinityAllowed(actThreeLevelTwo, MiningElementalAffinity::Radiation), "Act 3 level 2 should introduce Radiation");
    require(miningEnemyAllowed(actThreeLevelFour, MiningEnemyType::Spawner)
            && miningRoomFeatureAllowed(actThreeLevelFour, MiningCellFeature::MinibossLair),
        "Act 3 level 4 should restore spawner and miniboss pressure");
    require(miningRoomFeatureAllowed(actThreeLevelSeven, MiningCellFeature::BossChamber), "Act 3 level 7 should introduce boss chambers");
    require(actThreeLevelNine.maxSpawners == 2, "Act 3 mastery should permit multiple spawners");
}

void campaignMappingMatchesChapterPace()
{
    struct Expected {
        GameChapter chapter;
        MiningAct act;
        int minimum;
        int maximum;
    };
    constexpr std::array<Expected, 9> expected {{
        {GameChapter::LunarProgram, MiningAct::ActOne, 1, 3},
        {GameChapter::RedFrontier, MiningAct::ActOne, 4, 6},
        {GameChapter::Breakthrough, MiningAct::ActOne, 7, 8},
        {GameChapter::Straylight, MiningAct::ActOne, 9, 10},
        {GameChapter::Arkfall, MiningAct::ActTwo, 1, 3},
        {GameChapter::LastCampfire, MiningAct::ActTwo, 4, 10},
        {GameChapter::VoidCompass, MiningAct::ActThree, 1, 4},
        {GameChapter::Ouroboros, MiningAct::ActThree, 5, 8},
        {GameChapter::Ascent, MiningAct::ActThree, 9, 10},
    }};

    require(!resolveCampaignMiningProgression(GameChapter::ProvingGround, content::destination::earthOrbit, 0, 0).miningAvailable,
        "Chapter 1 should not allow mining");
    for (const Expected& item : expected) {
        const MiningCampaignProgression low = resolveCampaignMiningProgression(item.chapter, content::destination::moon, 0, 1);
        const MiningCampaignProgression high = resolveCampaignMiningProgression(item.chapter, content::destination::moon, 20, 1);
        require(low.miningAvailable && low.act == item.act, "campaign chapter should map to its mining act");
        require(low.minimumDifficulty == item.minimum && low.maximumDifficulty == item.maximum, "campaign chapter should publish its difficulty range");
        const int expectedHigh = item.chapter == GameChapter::LastCampfire ? 8 : item.maximum;
        require(low.difficulty == item.minimum && high.difficulty == expectedHigh, "surface depth should advance and clamp within its chapter allowance");
    }

    const MiningCampaignProgression firstKhepri = resolveCampaignMiningProgression(
        GameChapter::LastCampfire, content::destination::nearbyStar, 0, 1);
    const MiningCampaignProgression thirdKhepriDeep = resolveCampaignMiningProgression(
        GameChapter::LastCampfire, content::destination::nearbyStar, 4, 3);
    require(firstKhepri.difficulty == 4, "Chapter 7 first hostile success should begin at level 4");
    require(thirdKhepriDeep.minimumDifficulty == 6 && thirdKhepriDeep.difficulty == 10,
        "Chapter 7 third hostile success plus four depth steps should reach level 10");
}

void deterministicSeedsAndRewardProgressAreStable()
{
    const std::uint64_t baseline = deriveMiningArenaSeed(1234, content::destination::mars, 2, 3);
    require(baseline == deriveMiningArenaSeed(1234, content::destination::mars, 2, 3), "identical arena seed inputs should reproduce exactly");
    require(baseline != deriveMiningArenaSeed(1235, content::destination::mars, 2, 3), "campaign seed should affect arena seed");
    require(baseline != deriveMiningArenaSeed(1234, content::destination::moon, 2, 3), "destination should affect arena seed");
    require(baseline != deriveMiningArenaSeed(1234, content::destination::mars, 3, 3), "landing ordinal should affect arena seed");
    require(baseline != deriveMiningArenaSeed(1234, content::destination::mars, 2, 4), "surface depth should affect arena seed");

    MetaProgress meta;
    const MiningArenaRules rules = resolveMiningArenaRules({MiningAct::ActThree, 10, baseline});
    require(!miningFirstClearFulfilled(meta, rules), "unbanked rich guarantees should remain pending");
    creditBankedMiningFirstClearRewards(meta, rules, 4, 1);
    require(!miningFirstClearFulfilled(meta, rules), "partial banked guarantees should persist without completing the band");
    creditBankedMiningFirstClearRewards(meta, rules, 10, 10);
    require(miningFirstClearFulfilled(meta, rules), "banked rewards should complete the band at its guarantee caps");

    const MiningFirstClearProgress& progress = miningFirstClearProgress(meta, MiningAct::ActThree, MiningProgressionBand::Mastery);
    require(progress.rareBanked == 6 && progress.exoticBanked == 3, "first-clear progress should clamp to its guarantees");
    const MiningRewardBudget repeat = effectiveMiningRewardBudget(rules, true);
    require(repeat.rareGuarantee == 0 && repeat.exoticGuarantee == 0, "repeat arenas should have no first-clear guarantee");
    require(repeat.rareCap == 4 && repeat.exoticCap == 2, "repeat caps should halve with rare rounding up and exotic rounding down");
}

void enemyThemesFollowProgressionAndRemainDeterministic()
{
    require(miningEnemyThemeAffinity(MiningEnemyTheme::Neutral) == MiningElementalAffinity::None,
        "neutral enemy ecology should not add an affinity");
    require(miningEnemyThemeAffinity(MiningEnemyTheme::Lava) == MiningElementalAffinity::Thermal,
        "lava enemy ecology should map to the existing Thermal affinity");
    require(miningEnemyThemeAffinity(MiningEnemyTheme::Ice) == MiningElementalAffinity::Cryo,
        "ice enemy ecology should map to the existing Cryo affinity");
    require(miningEnemyThemeAffinity(MiningEnemyTheme::Radioactive) == MiningElementalAffinity::Radiation,
        "radioactive enemy ecology should map to the existing Radiation affinity");
    require(miningEnemyThemeAffinity(MiningEnemyTheme::Toxic) == MiningElementalAffinity::Toxic,
        "toxic enemy ecology should map to the existing Toxic affinity");

    const MiningArenaRules earlyCombat = resolveMiningArenaRules({MiningAct::ActTwo, 1, 0x1234});
    require(selectMiningEnemyTheme(earlyCombat, 0x1234) == MiningEnemyTheme::Neutral,
        "combat sites should remain neutral until Elementals enter the curriculum");

    const MiningArenaRules elementalCombat = resolveMiningArenaRules({MiningAct::ActTwo, 7, 0x5678});
    for (std::uint64_t seed = 0; seed < 64; ++seed) {
        const MiningEnemyTheme first = selectMiningEnemyTheme(elementalCombat, seed);
        const MiningEnemyTheme second = selectMiningEnemyTheme(elementalCombat, seed);
        require(first == second, "a mining site theme should be deterministic for its seed");
        require(first == MiningEnemyTheme::Lava || first == MiningEnemyTheme::Ice,
            "Act 2 elemental sites should select only currently legal Thermal or Cryo ecologies");
    }

    const MiningArenaRules toxicCombat = resolveMiningArenaRules({MiningAct::ActTwo, 9, 0x9abc});
    bool sawToxic = false;
    for (std::uint64_t seed = 0; seed < 128; ++seed) {
        sawToxic = sawToxic || selectMiningEnemyTheme(toxicCombat, seed) == MiningEnemyTheme::Toxic;
    }
    require(sawToxic, "Toxic ecology should enter deterministic site selection at its existing affinity gate");

    const MiningArenaRules radioactiveCombat = resolveMiningArenaRules({MiningAct::ActThree, 2, 0xdef0});
    bool sawRadioactive = false;
    for (std::uint64_t seed = 0; seed < 128; ++seed) {
        sawRadioactive = sawRadioactive || selectMiningEnemyTheme(radioactiveCombat, seed) == MiningEnemyTheme::Radioactive;
    }
    require(sawRadioactive, "Radioactive ecology should enter deterministic site selection with Act 3 Radiation");

    const ContentCatalog catalog = createDefaultContent();
    const MiningSiteDefinition* thermalSite = catalog.findMiningSite(content::miningSite::thermalLayeredRecovery);
    require(thermalSite != nullptr &&
            resolveMiningEnemyTheme(elementalCombat, thermalSite) == MiningEnemyTheme::Lava,
        "the authored Thermal Lava site should override generic ecology selection");

    MiningSiteProgress savedSite;
    savedSite.enemyTheme = MiningEnemyTheme::Toxic;
    require(resolveMiningEnemyTheme(elementalCombat, nullptr, &savedSite) == MiningEnemyTheme::Toxic,
        "a saved site ecology should remain fixed instead of rerolling on a later depth or reload");
}

void progressionSaveFieldsRoundTripAndLegacyDefault()
{
    SaveData save;
    save.mining.arenaMetadata = {MiningAct::ActThree, 8, 987654321ULL, miningArenaRulesVersion};
    save.miningFirstClearProgress[miningFirstClearProgressIndex(MiningAct::ActTwo, MiningProgressionBand::Pressure)] = {2, 0};
    save.miningFirstClearProgress[miningFirstClearProgressIndex(MiningAct::ActThree, MiningProgressionBand::Mastery)] = {6, 3};

    const std::string serialized = serializeSaveData(save);
    const std::optional<SaveData> restored = deserializeSaveData(serialized);
    require(restored.has_value(), "progression save should deserialize");
    require(restored->mining.arenaMetadata.act == MiningAct::ActThree, "arena act metadata should round trip");
    require(restored->mining.arenaMetadata.difficulty == 8, "arena difficulty metadata should round trip");
    require(restored->mining.arenaMetadata.seed == 987654321ULL, "arena seed metadata should round trip");
    require(restored->mining.arenaMetadata.rulesVersion == miningArenaRulesVersion, "arena rules version should round trip");
    require(restored->miningFirstClearProgress[miningFirstClearProgressIndex(MiningAct::ActThree, MiningProgressionBand::Mastery)].exoticBanked == 3,
        "first-clear progress should round trip");

    const std::string legacy = std::string(save_schema::header) + "\nversion=1\nseed=44\n";
    const std::optional<SaveData> legacySave = deserializeSaveData(legacy);
    require(!legacySave.has_value(), "pre-v16 saves must be rejected at the fresh-start boundary");

}

void miningGateContractsAndRuntimeAreDeterministic()
{
    const MiningArenaRules actOneSeven = resolveMiningArenaRules({MiningAct::ActOne, 7, 17});
    const MiningArenaRules actOneEight = resolveMiningArenaRules({MiningAct::ActOne, 8, 18});
    const MiningArenaRules actTwoTwo = resolveMiningArenaRules({MiningAct::ActTwo, 2, 22});
    const MiningArenaRules actTwoFour = resolveMiningArenaRules({MiningAct::ActTwo, 4, 24});
    const MiningArenaRules actTwoSeven = resolveMiningArenaRules({MiningAct::ActTwo, 7, 27});
    const MiningArenaRules actThreeOne = resolveMiningArenaRules({MiningAct::ActThree, 1, 31});
    require(selectMiningGateType(actOneSeven) == MiningGateType::None, "locks must not appear before their underlying Act 1 mechanics are taught");
    require(miningGateAllowed(actOneEight, MiningGateType::HazardCocoon)
            && miningGateAllowed(actOneEight, MiningGateType::SurveyTriangulation)
            && miningGateAllowed(actOneEight, MiningGateType::FragileExcavation),
        "Act 1 level 8 should introduce the three artifact recovery gates");
    require(actOneEight.maximumGateLocks == 1,
        "Act 1 gate composition should retain its one-lock limit");
    require(actTwoTwo.maximumGateLocks == 1,
        "Act 2 gate composition should retain its one-lock limit at its opening");
    require(miningGateAllowed(actTwoFour, MiningGateType::ShieldCorridor),
        "Act 2 level 4 should permit shield corridor sites after ranged enemies are taught");
    require(actTwoSeven.maximumGateLocks == 2,
        "Act 2 pressure should retain its two-lock cap");
    require(actThreeOne.maximumGateLocks == 3,
        "Act 3 should retain its three-lock cap");
    require(!miningGateAllowed(actTwoSeven, MiningGateType::BurrowBreach),
        "Act 2 must never leak the Act 3 Mammal gate");

    const MiningArenaRules illegalOverride = resolveMiningArenaRules({
        MiningAct::ActOne, 8, 99, true, MiningGateType::BurrowBreach
    });
    require(selectMiningGateType(illegalOverride) == MiningGateType::None,
        "Arena Lab must reject an override that violates the Act roster");
    MiningArenaRules noGateCandidates = actOneEight;
    noGateCandidates.allowedGateTypes.fill(false);
    require(selectMiningGateType(noGateCandidates) == MiningGateType::None,
        "an arena with no allowed gate must not synthesize a campaign site");

    const MiningGateDefinition cocoon = resolveMiningGateDefinition(actOneEight, MiningGateType::HazardCocoon, true);
    require(cocoon.requiresHazardTreatment && cocoon.requiredHazardMark == 1,
        "the first cocoon should be a hard Hazard Mk I lock");
    const MiningGateDefinition toxicCocoon = resolveMiningGateDefinition(
        resolveMiningArenaRules({MiningAct::ActTwo, 9, 29}), MiningGateType::HazardCocoon, false);
    require(toxicCocoon.hazardAffinity == MiningElementalAffinity::Toxic && toxicCocoon.requiredHazardMark == 2,
        "late Toxic cocoons should require Hazard Mk II");
    const MiningGateDefinition radiationCocoon = resolveMiningGateDefinition(
        resolveMiningArenaRules({MiningAct::ActThree, 9, 39}), MiningGateType::HazardCocoon, false);
    require(radiationCocoon.hazardAffinity == MiningElementalAffinity::Radiation && radiationCocoon.requiredHazardMark == 3,
        "Act 3 Radiation cocoons should require Hazard Mk III");

    MiningCapabilityProfile profile;
    require(!miningCapabilityReadyForGate(profile, cocoon), "a no-drone profile should fail the direct Hazard key forecast");
    profile.roleMarks[static_cast<std::size_t>(MiniDroneRole::Hazard)] = 1;
    require(miningCapabilityReadyForGate(profile, cocoon), "the matching Hazard mark should satisfy the direct key forecast");

    MetaProgress meta;
    require(pendingCompatibilityMiningSite(meta, content::destination::jupiter) == nullptr,
        "new progress should not synthesize a compatibility mining site");
    meta.miningSites.push_back({
        "legacy_fixed_gate",
        content::destination::jupiter,
        actOneEight.request.act,
        actOneEight.request.difficulty,
        actOneEight.request.seed,
        MiningGateType::HazardCocoon,
        "legacy_fixed_gate_artifact",
        false,
        false,
        true,
    });
    MiningSiteProgress* firstSite = pendingCompatibilityMiningSite(meta, content::destination::jupiter);
    require(firstSite != nullptr && firstSite->legacyMigrated,
        "only an imported legacy record should enter the compatibility mining-site path");
    creditExtractedCompatibilityMiningSiteArtifacts(meta, {{"wrong", content::destination::jupiter, false, ArtifactKind::Story}});
    require(!meta.miningSites.front().completed, "unrelated recovered artifacts must not complete a compatibility mining site");
    ArtifactRecord recovered;
    recovered.id = meta.miningSites.front().artifactId;
    recovered.kind = ArtifactKind::Story;
    creditExtractedCompatibilityMiningSiteArtifacts(meta, {recovered});
    require(meta.miningSites.front().completed, "only the banked site artifact should complete compatibility site progress");

    const ContentCatalog catalog = createDefaultContent();
    auto prepareSurface = [](GameState& state, std::string_view destinationId) {
        state.run.planetaryExpedition = {};
        state.run.planetaryExpedition.active = true;
        state.run.planetaryExpedition.destinationId = std::string(destinationId);
        state.run.planetaryExpedition.rigFuel = 4.0;
        state.run.planetaryExpedition.rigFuelCapacity = 4.0;
        state.run.planetaryExpedition.miningSitePrepared = true;
    };

    GameState hazardState = createNewGame(catalog, 501);
    // Exercise the generic cocoon contract without relying on a destination.
    prepareSurface(hazardState, content::destination::mars);
    const MiningArenaRequest hazardRequest {MiningAct::ActOne, 8, 0xCAFE, true, MiningGateType::HazardCocoon};
    require(startMiningRun(hazardState, catalog, hazardRequest, false).applied, "Hazard Cocoon debug arena should start");
    require(hazardState.run.mining.gate.type == MiningGateType::HazardCocoon
            && hazardState.run.mining.gate.shellTilesRemaining == 8,
        "Hazard Cocoon should stamp eight marked, deterministic shell tiles");
    require(hazardState.meta.miningSites.empty() && !hazardState.run.mining.gate.compatibilityCritical,
        "new debug sites should not create legacy mining-site progress");
    require(hazardState.run.mining.gate.derivedStateDirty,
        "new gate runtime should require one derived-state reconciliation");
    hazardState.run.mining.droneX = hazardState.run.mining.artifact.x;
    hazardState.run.mining.droneY = hazardState.run.mining.artifact.y;
    toggleMiningTether(hazardState);
    require(!hazardState.run.mining.artifact.tethered, "a locked cocoon must reject tether bypass");
    require(hazardState.run.mining.artifactTetherDeniedFlashSeconds > 0.0,
        "a nearby sealed artifact should acknowledge the rejected tether with a transient flash");
    for (MiningCell& cell : hazardState.run.mining.terrain.cells) {
        if (cell.gateAssociated && cell.material == MiningCellMaterial::HazardPocket) {
            cell.material = MiningCellMaterial::Regolith;
            cell.hazard = false;
        }
    }
    updateMiningRun(hazardState, catalog, 0.01);
    require(hazardState.run.mining.gate.state == MiningGateState::Open,
        "treating every shell tile should open the cocoon");
    require(!hazardState.run.mining.gate.derivedStateDirty,
        "end-of-tick gate reconciliation should leave the derived cache clean");
    updateMiningRun(hazardState, catalog, 0.01);
    require(!hazardState.run.mining.gate.derivedStateDirty,
        "a tick with no gate-affecting mutation should keep the derived cache clean");

    GameState enemyState = createNewGame(catalog, 502);
    prepareSurface(enemyState, content::destination::nearbyStar);
    const MiningArenaRequest enemyRequest {MiningAct::ActTwo, 2, 0xBEEF, true, MiningGateType::EnemySealedChamber};
    require(startMiningRun(enemyState, catalog, enemyRequest, false).applied, "Enemy-Sealed Chamber debug arena should start");
    for (const auto& layer : enemyState.run.mining.depthLayers) {
        if (layer.artifact.present) {
            const int depth = layer.depthZone;
            require(activateLandingLayer(enemyState.run.mining, depth), "enemy seal's rolled depth should activate");
            break;
        }
    }
    require(enemyState.run.mining.gate.assignedEnemiesRemaining > 0, "enemy seal should own a specific encounter group");
    for (MiningEnemy& enemy : enemyState.run.mining.enemies) {
        if (enemy.gateAssociated) enemy.active = false;
    }
    updateMiningRun(enemyState, catalog, 0.01);
    require(enemyState.run.mining.gate.state == MiningGateState::Open,
        "enemy seal should open only after its assigned encounter is cleared");

    GameState surveyState = createNewGame(catalog, 503);
    prepareSurface(surveyState, content::destination::mars);
    const MiningArenaRequest surveyRequest {MiningAct::ActOne, 8, 0x5151, true, MiningGateType::SurveyTriangulation};
    require(startMiningRun(surveyState, catalog, surveyRequest, false).applied, "Survey Triangulation debug arena should start");
    require(surveyState.run.mining.gate.markers.size() == 3, "triangulation should stamp three distinct scanner origins");
    require(!surveyState.run.mining.artifact.revealed,
        "triangulation should keep the artifact hidden until every signal is resolved");
    updateMiningRun(surveyState, catalog, 0.01);
    require(!surveyState.run.mining.gate.derivedStateDirty,
        "initial triangulation reconciliation should clean the derived cache");
    surveyState.run.mining.droneX = surveyState.run.mining.gate.markers.front().x;
    surveyState.run.mining.droneY = surveyState.run.mining.gate.markers.front().y;
    pulseMiningScanner(surveyState, catalog);
    require(surveyState.run.planetaryExpedition.scannerCooldownSeconds > 0.0,
        "triangulation calibration should preserve the shared scanner recharge");
    const int afterFirstPulse = surveyState.run.mining.gate.surveyOriginsCompleted;
    require(afterFirstPulse >= 1,
        "one real scanner pulse should resolve every hidden signal inside its coverage");
    const auto cooldownMarker = std::find_if(
        surveyState.run.mining.gate.markers.begin(),
        surveyState.run.mining.gate.markers.end(),
        [](const MiningGateMarker& marker) { return !marker.activated; });
    if (cooldownMarker != surveyState.run.mining.gate.markers.end()) {
        surveyState.run.mining.droneX = cooldownMarker->x;
        surveyState.run.mining.droneY = cooldownMarker->y;
        pulseMiningScanner(surveyState, catalog);
        require(surveyState.run.mining.gate.surveyOriginsCompleted == afterFirstPulse,
            "scanner input during cooldown must not calibrate a nearby hidden signal");
    }
    while (!surveyState.run.mining.gate.surveyComplete) {
        const auto marker = std::find_if(
            surveyState.run.mining.gate.markers.begin(),
            surveyState.run.mining.gate.markers.end(),
            [](const MiningGateMarker& candidate) { return !candidate.activated; });
        require(marker != surveyState.run.mining.gate.markers.end(),
            "an incomplete triangulation should retain an unresolved hidden signal");
        surveyState.run.planetaryExpedition.scannerCooldownSeconds = 0.0;
        surveyState.run.mining.droneX = marker->x;
        surveyState.run.mining.droneY = marker->y;
        pulseMiningScanner(surveyState, catalog);
    }
    require(surveyState.run.mining.gate.surveyComplete && surveyState.run.mining.gate.state == MiningGateState::Open,
        "the third resolved signal should open triangulation during that pulse");
    require(surveyState.run.mining.artifact.revealed,
        "completing triangulation should reveal the artifact during the same pulse");
    require(surveyState.statusLine == "TRIANGULATION COMPLETE — ARTIFACT EXPOSED.",
        "triangulation completion should emit the authored exposure message");
    require(!surveyState.run.mining.gate.derivedStateDirty,
        "triangulation should resolve and clean its cache in the same scanner pulse");

    GameState partialSurvey = createNewGame(catalog, 505);
    prepareSurface(partialSurvey, content::destination::mars);
    require(startMiningRun(partialSurvey, catalog, surveyRequest, false).applied,
        "partial triangulation save test should start");
    partialSurvey.run.mining.gate.markers.front().activated = true;
    partialSurvey.run.mining.gate.surveyOriginsCompleted = 1;
    partialSurvey.run.mining.gate.surveyComplete = false;
    partialSurvey.run.mining.artifact.revealed = true;
    const int partialArtifactX = static_cast<int>(std::floor(partialSurvey.run.mining.artifact.x));
    const int partialArtifactY = static_cast<int>(std::floor(partialSurvey.run.mining.artifact.y));
    if (MiningCell* cell = miningCellAt(partialSurvey.run.mining.terrain, partialArtifactX, partialArtifactY)) {
        cell->revealed = true;
    }
    const std::optional<SaveData> partialParsed = deserializeSaveData(
        serializeSaveData(captureSaveData(partialSurvey)));
    require(partialParsed.has_value(), "partial triangulation should serialize");
    GameState partialRestored = createNewGame(catalog, 1);
    restoreSaveData(partialRestored, catalog, *partialParsed);
    const int restoredSignals = static_cast<int>(std::count_if(
        partialRestored.run.mining.gate.markers.begin(),
        partialRestored.run.mining.gate.markers.end(),
        [](const MiningGateMarker& marker) { return marker.activated; }));
    require(restoredSignals == 1,
        "partial triangulation reload should preserve completed signal slices");
    require(!partialRestored.run.mining.artifact.revealed,
        "an incomplete v15 triangulation reload should force the artifact back to hidden");

    GameState burrowState = createNewGame(catalog, 504);
    prepareSurface(burrowState, content::destination::nearbyGalaxy);
    const MiningArenaRequest burrowRequest {MiningAct::ActThree, 1, 0xB0770, true, MiningGateType::BurrowBreach};
    require(startMiningRun(burrowState, catalog, burrowRequest, false).applied, "Burrow Breach debug arena should start");
    for (const auto& layer : burrowState.run.mining.depthLayers) {
        if (layer.artifact.present) {
            const int depth = layer.depthZone;
            require(activateLandingLayer(burrowState.run.mining, depth), "burrow seal's rolled depth should activate");
            break;
        }
    }
    const int markedBedrock = static_cast<int>(std::count_if(
        burrowState.run.mining.terrain.cells.begin(), burrowState.run.mining.terrain.cells.end(), [](const MiningCell& cell) {
            return cell.gateAssociated && cell.material == MiningCellMaterial::Bedrock;
        }));
    require(markedBedrock == 5, "Burrow Breach should stamp a marked five-tile wall");
    for (MiningEnemy& enemy : burrowState.run.mining.enemies) {
        if (enemy.gateAssociated && enemy.type == MiningEnemyType::Mammal) enemy.active = false;
    }
    updateMiningRun(burrowState, catalog, 0.01);
    require(std::any_of(burrowState.run.mining.enemies.begin(), burrowState.run.mining.enemies.end(), [](const MiningEnemy& enemy) {
        return enemy.gateAssociated && enemy.type == MiningEnemyType::Mammal && enemy.active;
    }), "an unopened breach should always replenish its assigned Mammal");
    require(burrowState.run.mining.gate.derivedStateDirty,
        "replenishing a gate-associated Mammal should invalidate enemy-derived state");
    updateMiningRun(burrowState, catalog, 0.01);
    require(!burrowState.run.mining.gate.derivedStateDirty,
        "the replenished encounter should reconcile once and remain clean without another mutation");

    for (const auto [type, act, difficulty] : std::array<std::tuple<MiningGateType, MiningAct, int>, 4> {{
             {MiningGateType::FragileExcavation, MiningAct::ActOne, 8},
             {MiningGateType::HeavyTow, MiningAct::ActOne, 9},
             {MiningGateType::EnduranceVault, MiningAct::ActOne, 9},
             {MiningGateType::ShieldCorridor, MiningAct::ActTwo, 4}
         }}) {
        const MiningArenaRules rules = resolveMiningArenaRules({act, difficulty, 77, true, type});
        const MiningGateDefinition definition = resolveMiningGateDefinition(rules, type, false);
        require(selectMiningGateType(rules) == type, "every documented soft gate should be directly replayable in Arena Lab");
        require(!definition.requiredCapability.empty() && !definition.alternatives.empty(),
            "every soft gate should publish a direct key and systemic alternatives");
    }

    SaveData save;
    save.mining = hazardState.run.mining;
    save.miningSites = meta.miningSites;
    const std::optional<SaveData> gateRoundTrip = deserializeSaveData(serializeSaveData(save));
    require(gateRoundTrip.has_value()
            && gateRoundTrip->mining.gate.type == MiningGateType::HazardCocoon
            && gateRoundTrip->mining.gate.derivedStateDirty
            && gateRoundTrip->miningSites.front().artifactId == meta.miningSites.front().artifactId,
        "active gate state and compatibility site identity should survive save/load while transient derived state reloads dirty");
}

void progressionArtifactPlacementDeepensByMissionStage()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 0xA471FAC7ULL);
    const std::array<int, 6> expectedMainDepths {1, 1, 2, 2, 3, 4};
    for (const SolarMissionDefinition& mission : catalog.solarMissions) {
        const Destination* environment = catalog.findDestination(mission.environmentId);
        require(environment != nullptr, "every solar mission must resolve its geology environment");
        state.run.planetaryExpedition.bodyId = mission.bodyId;
        const ProgressionArtifactPlacement placement = resolveProgressionArtifactPlacement(
            state, catalog, *environment, 5, mission.artifactId);
        require(placement.artifactId == mission.artifactId,
            "artifact placement must retain the physical world's unique artifact identity");
        require(placement.ordinal == (mission.optional ? 0 : mission.progressionOrdinal),
            "main artifact depth must follow mission stage while optional worlds stay at stage zero");
        const int expectedDepth = mission.optional
            ? 1
            : expectedMainDepths[static_cast<std::size_t>(mission.progressionOrdinal)];
        require(placement.targetDepth == expectedDepth,
            "main artifacts must follow the readable depth-one through depth-four mission staircase");
        if (mission.optional || mission.progressionOrdinal == 0) {
            const int expectedOffset = mission.optional ? 10 : 8;
            require(placement.horizontalOffset == 0 && placement.verticalOffset == expectedOffset,
                "the Moon and optional inner-world artifacts should use the centered introductory placement");
        } else {
            const int magnitude = std::abs(placement.horizontalOffset);
            require(magnitude >= mission.progressionOrdinal &&
                    magnitude <= mission.progressionOrdinal * 2,
                "lateral placement variation should widen predictably with main mission stage");
            require(placement.verticalOffset == 10 + (mission.progressionOrdinal % 2) * 4,
                "the later artifact sharing a depth layer should sit visibly below the earlier one");
        }
    }
}

GameState freshLunarAnomaly(const ContentCatalog& catalog)
{
    GameState state = createNewGame(catalog, 0x4C554E4152ULL);
    require(performScenarioAction(
                state,
                catalog,
                content::scenario::lunarProspector,
                "briefing",
                ScenarioActionKind::AcknowledgeBriefing)
                .applied,
        "the lunar fixture should accept its briefing");
    state.run.planetaryExpedition = {};
    state.run.planetaryExpedition.active = true;
    state.run.planetaryExpedition.destinationId = content::destination::moon;
    state.run.planetaryExpedition.bodyId = "moon";
    state.run.planetaryExpedition.rigFuel = 4.0;
    state.run.planetaryExpedition.rigFuelCapacity = 4.0;
    state.run.planetaryExpedition.miningSitePrepared = true;
    require(startMiningRun(state, catalog, {MiningAct::ActOne, 1, 0x4C554E4152ULL}, true).applied,
        "the lunar mining fixture should start");
    state.run.mining.droneX = state.run.mining.returnZoneX;
    state.run.mining.droneY = state.run.mining.returnZoneY;
    state.run.mining.temporaryMaterials.common = 20;
    state.run.mining.cargo = 20;
    require(bankMiningPayloadAtShip(state, catalog),
        "the lunar fixture should deliver the introductory ore contract");
    updateMiningRun(state, catalog, 0.01);
    require(state.run.mining.miningSiteDefinitionId == content::miningSite::lunarAnomalyCrevice &&
            state.run.mining.artifact.id == content::protectedObjective::lunarSignalArtifact,
        "the lunar fixture should generate the authored anomaly crevice");
    return state;
}

void lunarCreviceHasThreeVisibleCellsAndScansFromTheTop()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = freshLunarAnomaly(catalog);
    MiningRunState& mining = state.run.mining;
    const int artifactX = static_cast<int>(std::floor(mining.artifact.x));
    const int artifactY = static_cast<int>(std::floor(mining.artifact.y));
    int sealY = -1;
    int passageTop = mining.terrain.height;
    for (int y = 0; y < artifactY; ++y) {
        const MiningCell* cell = miningCellAt(mining.terrain, artifactX, y);
        if (cell == nullptr || !cell->suitOnlyPassage) continue;
        passageTop = std::min(passageTop, y);
        if (cell->material == MiningCellMaterial::Regolith) sealY = y;
    }
    require(sealY >= 0 && passageTop == sealY - 1,
        "the lunar crevice should retain one open framed cell above its drillable seal");
    require(artifactY - sealY == 3,
        "the rendered lunar approach should contain exactly three cells from the seal to the artifact");
    for (int y = sealY + 1; y < artifactY; ++y) {
        const MiningCell* cell = miningCellAt(mining.terrain, artifactX, y);
        require(cell != nullptr && cell->material == MiningCellMaterial::Empty && cell->suitOnlyPassage,
            "the compact lunar shaft should retain an unobstructed suit-only route");
    }

    mining.operatorMode = MiningOperatorMode::Jetpack;
    mining.operatorPresent = true;
    mining.operatorX = mining.artifact.x;
    mining.operatorY = static_cast<double>(passageTop) - 1.5;
    mining.artifact.revealed = false;
    if (MiningCell* artifactCell = miningCellAt(mining.terrain, artifactX, artifactY)) {
        artifactCell->revealed = false;
    }
    const int terrainProbeX = artifactX + 6;
    const int terrainProbeY = static_cast<int>(std::floor(mining.operatorY));
    MiningCell* terrainProbe = miningCellAt(mining.terrain, terrainProbeX, terrainProbeY);
    require(terrainProbe != nullptr, "the lunar scan regression needs an in-bounds terrain probe");
    terrainProbe->revealed = false;
    state.run.planetaryExpedition.scannerCooldownSeconds = 0.0;
    const MiningScannerResult result = pulseMiningScanner(state, catalog);
    require(result.pulsed && mining.artifact.revealed &&
            result.discoveredObjectiveId == content::protectedObjective::lunarSignalArtifact,
        "an EVA scanner pulse from the top of the compact shaft should detect the lunar artifact");
    require(!terrainProbe->revealed,
        "artifact coyote distance must not enlarge the normal terrain pulse radius");
}

void oldLunarCreviceSaveMigratesWithoutLosingTerrain()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState oldState = freshLunarAnomaly(catalog);
    MiningRunState& mining = oldState.run.mining;
    const int artifactX = static_cast<int>(std::floor(mining.artifact.x));
    const int compactArtifactY = static_cast<int>(std::floor(mining.artifact.y));
    const int oldArtifactY = compactArtifactY + 4;
    const int oldTop = oldArtifactY - 6;
    const int oldFloor = oldArtifactY + 2;

    const auto authoredCell = [&](MiningCellMaterial material, bool suitOnly = false) {
        MiningCell cell;
        cell.material = material;
        cell.maxToughness = material == MiningCellMaterial::Regolith ? 2.1 : 1.0;
        cell.remainingToughness = cell.maxToughness;
        cell.feature = MiningCellFeature::BranchTunnel;
        cell.suitOnlyPassage = suitOnly;
        return cell;
    };
    for (int y = oldTop - 2; y <= oldFloor; ++y) {
        for (int x = artifactX - 1; x <= artifactX + 1; ++x) {
            if (MiningCell* cell = miningCellAt(mining.terrain, x, y)) {
                *cell = authoredCell(MiningCellMaterial::Regolith);
            }
        }
    }
    for (int y = oldTop; y <= oldArtifactY + 1; ++y) {
        *miningCellAt(mining.terrain, artifactX - 1, y) = authoredCell(MiningCellMaterial::Bedrock);
        *miningCellAt(mining.terrain, artifactX + 1, y) = authoredCell(MiningCellMaterial::Bedrock);
        *miningCellAt(mining.terrain, artifactX, y) = authoredCell(MiningCellMaterial::Empty, true);
    }
    MiningCell& oldSeal = *miningCellAt(mining.terrain, artifactX, oldTop + 1);
    oldSeal = authoredCell(MiningCellMaterial::Regolith, true);
    oldSeal.remainingToughness = oldSeal.maxToughness * 0.5;
    oldSeal.revealed = true;
    MiningCell& oldArtifactCell = *miningCellAt(mining.terrain, artifactX, oldArtifactY);
    oldArtifactCell = authoredCell(MiningCellMaterial::ArtifactCache, true);
    oldArtifactCell.gateAssociated = true;
    *miningCellAt(mining.terrain, artifactX, oldFloor) = authoredCell(MiningCellMaterial::Bedrock);
    MiningCell& displacedOre = *miningCellAt(mining.terrain, artifactX - 1, oldTop - 2);
    displacedOre = authoredCell(MiningCellMaterial::CommonOre);
    displacedOre.remainingToughness = 0.75;
    MiningCell& unrelated = *miningCellAt(mining.terrain, artifactX + 4, oldTop);
    unrelated = authoredCell(MiningCellMaterial::RareOre);
    unrelated.remainingToughness = 0.42;

    mining.artifact.x = static_cast<double>(artifactX) + 0.5;
    mining.artifact.y = static_cast<double>(oldArtifactY) + 0.5;
    mining.artifact.health = 0.63;
    mining.artifact.revealed = false;
    mining.gate.anchorX = mining.artifact.x;
    mining.gate.anchorY = mining.artifact.y;
    mining.gate.cocoonDefinitionId.clear();
    mining.gate.cocoonDefinitionVersion = 0;

    const double oldSealRemaining = oldSeal.remainingToughness;
    const SaveData captured = captureSaveData(oldState);
    const std::optional<SaveData> parsed = deserializeSaveData(serializeSaveData(captured));
    require(parsed.has_value(), "the old lunar shaft fixture should serialize");
    GameState restored = createNewGame(catalog, 7);
    restoreSaveData(restored, catalog, *parsed);
    const MiningRunState& migrated = restored.run.mining;
    const int migratedArtifactY = static_cast<int>(std::floor(migrated.artifact.y));
    require(migratedArtifactY == compactArtifactY && migrated.gate.anchorY == migrated.artifact.y &&
            migrated.gate.cocoonDefinitionVersion == 2,
        "an unfinished public-build lunar shaft should migrate once to the compact layout");
    require(std::abs(migrated.artifact.health - 0.63) < 0.0001,
        "the lunar shaft repair should preserve artifact condition");
    const MiningCell* migratedSeal = miningCellAt(migrated.terrain, artifactX, oldTop - 1);
    require(migratedSeal != nullptr && migratedSeal->material == MiningCellMaterial::Regolith &&
            migratedSeal->revealed && std::abs(migratedSeal->remainingToughness - oldSealRemaining) < 0.0001,
        "the lunar shaft repair should preserve partial seal excavation and visibility");
    const MiningCell* preservedOre = miningCellAt(migrated.terrain, artifactX - 1, oldArtifactY);
    require(preservedOre != nullptr && preservedOre->material == MiningCellMaterial::CommonOre &&
            std::abs(preservedOre->remainingToughness - 0.75) < 0.0001,
        "terrain displaced by the compact shaft should be retained in the vacated tail");
    const MiningCell* preservedUnrelated = miningCellAt(migrated.terrain, artifactX + 4, oldTop);
    require(preservedUnrelated != nullptr && preservedUnrelated->material == MiningCellMaterial::RareOre &&
            std::abs(preservedUnrelated->remainingToughness - 0.42) < 0.0001,
        "the lunar shaft repair should not modify unrelated terrain");

    for (const MiningArtifactState protectedState : {
             MiningArtifactState::Loose,
             MiningArtifactState::Delivered,
             MiningArtifactState::Destroyed}) {
        MiningRunState excluded = oldState.run.mining;
        excluded.artifact.state = protectedState;
        excluded.artifact.tethered = protectedState == MiningArtifactState::Loose;
        migrateAdjacentCocoonTiles(excluded);
        require(static_cast<int>(std::floor(excluded.artifact.y)) == oldArtifactY &&
                excluded.gate.cocoonDefinitionVersion == 0,
            "the lunar save repair must not move recovered, tethered, or destroyed artifacts");
    }
}

void authoredArtifactLayerIsPrebuiltAtResolvedDepth()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 0x10A471FAC7ULL);
    state.run.planetaryExpedition = {};
    state.run.planetaryExpedition.active = true;
    state.run.planetaryExpedition.destinationId = content::destination::jupiter;
    state.run.planetaryExpedition.rigFuel = 4.0;
    state.run.planetaryExpedition.rigFuelCapacity = 4.0;
    state.run.planetaryExpedition.supply = 4;
    state.run.planetaryExpedition.pendingScenarioId = content::scenario::volcanicDescent;
    state.run.planetaryExpedition.pendingScenarioStepId = "recovery";
    state.run.planetaryExpedition.pendingMiningSiteDefinitionId =
        content::miningSite::thermalLayeredRecovery;

    require(startMiningRun(state, catalog).applied,
        "the authored Io artifact run should start");
    require(state.run.mining.depthZone == 0 && !state.run.mining.artifact.present,
        "starting above the resolved artifact depth should not duplicate the objective on the entry layer");
    const auto layer = std::find_if(
        state.run.mining.depthLayers.begin(),
        state.run.mining.depthLayers.end(),
        [](const MiningDepthLayerState& candidate) { return candidate.depthZone == 1; });
    require(layer != state.run.mining.depthLayers.end() && layer->artifact.present,
        "the first authored artifact should be prebuilt in the persisted depth-one cache");
    require(static_cast<int>(std::floor(layer->artifact.x)) == layer->terrain.width / 2 &&
            static_cast<int>(std::floor(layer->artifact.y)) == 14,
        "the first authored artifact should be dead center and ten cells below its layer entry");
    require(layer->gate.type == MiningGateType::HazardCocoon &&
            layer->gate.cocoonLayers.size() == 1,
        "the cached Io objective should retain its authored one-layer thermal seal");

    const SaveData captured = captureSaveData(state);
    const std::optional<SaveData> parsed = deserializeSaveData(serializeSaveData(captured));
    require(parsed.has_value(), "cached progression artifact layers should serialize");
    GameState restored = createNewGame(catalog, 2);
    restoreSaveData(restored, catalog, *parsed);
    const auto restoredLayer = std::find_if(
        restored.run.mining.depthLayers.begin(),
        restored.run.mining.depthLayers.end(),
        [](const MiningDepthLayerState& candidate) { return candidate.depthZone == 1; });
    require(restoredLayer != restored.run.mining.depthLayers.end() &&
            restoredLayer->artifact.x == layer->artifact.x &&
            restoredLayer->artifact.y == layer->artifact.y,
        "reload should preserve the cached authored artifact position without rerolling it");
}

void thermalSiteRulesAreContentDriven()
{
    const MiningArenaRequest request {MiningAct::ActOne, 7, 0x10A0ULL};
    MiningSiteDefinition thermalSite;
    thermalSite.id = "test_thermal_cocoon";
    thermalSite.arena = request;
    thermalSite.biome = MiningSiteBiome::ThermalLava;
    thermalSite.gateType = MiningGateType::HazardCocoon;

    const MiningArenaRules generic = resolveMiningArenaRules(request);
    const MiningArenaRules thermal = resolveMiningSiteArenaRules(request, thermalSite);

    require(selectMiningGateType(generic) == MiningGateType::None,
        "a pre-gate ordinary arena should not acquire an authored cocoon gate");
    require(selectMiningGateType(thermal) == MiningGateType::HazardCocoon
            && miningGateAllowed(thermal, MiningGateType::HazardCocoon),
        "a Thermal site should expose its authored cocoon gate");
    require(thermal.mechanics.environmentalHazards
            && thermal.mechanics.artifactRecovery
            && thermal.mechanics.artifactTethering,
        "a Thermal site should enable treatment and protected-objective mechanics");
    require(miningAffinityAllowed(thermal, MiningElementalAffinity::Thermal)
            && !miningAffinityAllowed(thermal, MiningElementalAffinity::Cryo)
            && !miningAffinityAllowed(thermal, MiningElementalAffinity::Toxic)
            && !miningAffinityAllowed(thermal, MiningElementalAffinity::Radiation),
        "a Thermal site should permit only its authored hazard affinity");
    require(thermal.rewardBudget.rareGuarantee == 0
            && thermal.rewardBudget.rareCap == 0
            && thermal.rewardBudget.exoticGuarantee == 0
            && thermal.rewardBudget.exoticCap == 0,
        "a Thermal site should not inject normal rich-deposit rewards");
    require(thermal.referenceDrones.roleCount == 1
            && thermal.referenceDrones.roles[0] == MiniDroneRole::Hazard
            && thermal.referenceDrones.maximumMark == 1,
        "a Thermal site should teach the required Hazard Drone Mk I");
}

void layeredCocoonsHonorAuthoredRevealPolicies()
{
    ContentCatalog catalog = createDefaultContent();
    MiningSiteDefinition site;
    site.id = "test_authored_three_layer_cocoon";
    site.version = 1;
    site.arena = {MiningAct::ActOne, 8, 0xC0C00AULL, true, MiningGateType::HazardCocoon};
    site.biome = MiningSiteBiome::ThermalLava;
    site.gateType = MiningGateType::HazardCocoon;
    site.cocoon.id = "test_three_layer_cocoon";
    site.cocoon.version = 1;
    site.cocoon.protectedObjective = {ProtectedObjectiveKind::Artifact, "test_protected_payload"};
    site.cocoon.layers = {
        {"outer", "OUTER", {{0, -3}, {3, 0}, {0, 3}, {-3, 0}},
            MiningCocoonRevealPolicy::OnAnyCellDiscovered,
            MiningCocoonCompletionRule::TreatAndExcavate,
            MiningElementalAffinity::Thermal, 1},
        {"middle", "MIDDLE", {{-2, -2}, {2, -2}, {2, 2}, {-2, 2}},
            MiningCocoonRevealPolicy::AfterPreviousLayerCompleted,
            MiningCocoonCompletionRule::TreatAndExcavate,
            MiningElementalAffinity::Thermal, 1},
        {"inner", "INNER", {{0, -1}, {1, 0}, {0, 1}, {-1, 0}},
            MiningCocoonRevealPolicy::OnAnyCellDiscovered,
            MiningCocoonCompletionRule::TreatAndExcavate,
            MiningElementalAffinity::Thermal, 1},
    };
    catalog.miningSites.push_back(site);

    GameState state = createNewGame(catalog, 0xC0C00BULL);
    state.run.planetaryExpedition.active = true;
    state.run.planetaryExpedition.destinationId = content::destination::mars;
    state.run.planetaryExpedition.rigFuel = 4.0;
    state.run.planetaryExpedition.rigFuelCapacity = 4.0;
    state.run.planetaryExpedition.miningSitePrepared = true;
    state.run.planetaryExpedition.pendingMiningSiteDefinitionId = site.id;
    require(startMiningRun(
                state,
                catalog,
                {MiningAct::ActOne, 8, 0xC0C00CULL, true, MiningGateType::HazardCocoon},
                false)
                .applied,
        "an authored mining site should start independently of its destination");

    MiningRunState& mining = state.run.mining;
    require(mining.gate.cocoonDefinitionId == site.cocoon.id &&
            mining.gate.cocoonLayers.size() == 3 &&
            !mining.gate.compatibilityCritical &&
            !mining.gate.cocoonLayers[0].revealed &&
            !mining.gate.cocoonLayers[1].revealed &&
            !mining.gate.cocoonLayers[2].revealed &&
            !mining.artifact.revealed,
        "an authored site should avoid legacy story state and begin with its payload and non-immediate layers hidden");

    mining.droneX = 2.0;
    mining.droneY = 4.0;
    pulseMiningScanner(state, catalog);
    require(mining.gate.cocoonLayers[0].revealed &&
            !mining.gate.cocoonLayers[1].revealed &&
            !mining.gate.cocoonLayers[2].revealed,
        "the first scanner pulse should map the declared outer objective seal even at a distance");
    require(state.statusLine.find("OBJECTIVE SIGNAL DETECTED") != std::string::npos,
        "the first objective pulse should explicitly report the mapped outer seal");

    auto clearLayer = [&](int layer) {
        for (MiningCell& cell : mining.terrain.cells) {
            if (cell.cocoonLayer == layer) {
                cell.material = MiningCellMaterial::Empty;
                cell.hazard = false;
                cell.revealed = true;
            }
        }
        mining.gate.derivedStateDirty = true;
        updateMiningRun(state, catalog, 0.01);
    };

    clearLayer(0);
    require(mining.gate.cocoonLayers[0].completed &&
            mining.gate.cocoonLayers[1].revealed &&
            !mining.gate.cocoonLayers[2].revealed,
        "an AfterPrevious layer should reveal only after its predecessor is treated and excavated");

    clearLayer(1);
    require(mining.gate.cocoonLayers[1].completed &&
            !mining.gate.cocoonLayers[2].revealed &&
            !mining.artifact.revealed,
        "a later OnAny layer should remain hidden until the player discovers it");

    pulseMiningScanner(state, catalog);
    require(!mining.gate.cocoonLayers[2].revealed,
        "an ordinary follow-up scan should wait for the shared scanner recharge");
    mining.droneX = mining.gate.anchorX;
    mining.droneY = mining.gate.anchorY;
    updateMiningRun(state, catalog, 4.0);
    pulseMiningScanner(state, catalog);
    require(mining.gate.cocoonLayers[2].revealed,
        "the active later OnAny layer should reveal atomically through the shared scanner path");

    clearLayer(2);
    require(mining.gate.hazardTreatmentComplete &&
            mining.gate.state == MiningGateState::Open &&
            mining.artifact.revealed,
        "the protected payload should reveal only after every authored layer is complete");
    mining.droneX = mining.artifact.x;
    mining.droneY = mining.artifact.y;
    toggleMiningTether(state);
    require(mining.artifact.tethered,
        "a revealed protected payload should become tetherable through the generic artifact adapter");
}

void tetherTargetingPrioritizesArtifactsAndKeepsEvaTow()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 0xA11CE);
    state.run.planetaryExpedition.active = true;
    state.run.planetaryExpedition.destinationId = content::destination::mars;
    state.run.planetaryExpedition.rigFuel = 4.0;
    state.run.planetaryExpedition.rigFuelCapacity = 4.0;
    state.run.planetaryExpedition.miningSitePrepared = true;
    require(startMiningRun(
                state,
                catalog,
                {MiningAct::ActOne, 1, 0xA11CE, true, MiningGateType::None},
                false)
                .applied,
        "rig tether test mining run should start");
    MiningRunState& mining = state.run.mining;
    mining.artifact = {};
    for (MiningCell& cell : mining.terrain.cells) {
        cell.material = MiningCellMaterial::Empty;
        cell.remainingToughness = 0.0;
        cell.maxToughness = 0.0;
        cell.suitOnlyPassage = false;
    }
    mining.droneX = mining.returnZoneX + 3.0;
    mining.droneY = mining.returnZoneY;
    mining.gravityStrength = 0.0;
    mining.rigVelocityX = 0.0;
    mining.rigVelocityY = 0.0;
    const double rigX = mining.droneX;
    const double rigY = mining.droneY;
    toggleMiningTether(state);
    require(!mining.rigTethered,
        "the player-controlled Mining Rig must never create the retired ship tether");
    updateMiningRun(state, catalog, 0.08);
    require(std::abs(mining.droneX - rigX) < 0.000001 && std::abs(mining.droneY - rigY) < 0.000001,
        "pressing tether without an artifact must not pull the Mining Rig toward the ship");

    // A revealed prospect is recoverable before the authored artifact tutorial
    // tier. Its stored center is one half-cell ahead of the actor coordinate.
    require(!resolveMiningArenaRules({MiningAct::ActOne, 1, 0xA11CE}).mechanics.artifactTethering,
        "the artifact targeting regression must cover the pre-tutorial tier");
    mining.artifact = {};
    mining.artifact.present = true;
    mining.artifact.revealed = true;
    mining.artifact.state = MiningArtifactState::Loose;
    mining.artifact.x = mining.droneX + 0.5;
    mining.artifact.y = mining.droneY + 0.5;
    toggleMiningTether(state);
    require(mining.artifact.tethered,
        "a revealed pre-tutorial artifact should tether from the Mining Rig");
    toggleMiningTether(state);

    // EVA may tow the same-depth rig, but a visually nearer artifact wins.
    mining.operatorMode = MiningOperatorMode::Jetpack;
    mining.operatorPresent = true;
    mining.operatorX = mining.returnZoneX + 10.0;
    mining.operatorY = mining.returnZoneY + 8.0;
    mining.droneX = mining.operatorX + 3.0;
    mining.droneY = mining.operatorY;
    mining.rigDepthZone = mining.depthZone;
    mining.artifact.x = mining.operatorX + 1.5;
    mining.artifact.y = mining.operatorY + 0.5;
    mining.artifact.tethered = false;
    mining.rigVelocityX = 0.0;
    mining.rigVelocityY = 0.0;
    toggleMiningTether(state);
    require(mining.artifact.tethered && !mining.operatorRigTethered,
        "the EVA player should tether the visually nearer artifact instead of the Mining Rig");
    toggleMiningTether(state);

    // The rig still wins when it is genuinely closer.
    mining.artifact.x = mining.operatorX + 5.5;
    mining.artifact.y = mining.operatorY + 0.5;
    toggleMiningTether(state);
    require(mining.operatorRigTethered && !mining.rigTethered,
        "the EVA player should tether a genuinely nearer same-depth Mining Rig");
    const double operatorTowBefore = std::hypot(
        mining.droneX - mining.operatorX,
        mining.droneY - mining.operatorY);
    updateMiningRun(state, catalog, 0.08);
    const double operatorTowAfter = std::hypot(
        mining.droneX - mining.operatorX,
        mining.droneY - mining.operatorY);
    require(operatorTowAfter < operatorTowBefore,
        "a jetpack tether should physically pull the Mining Rig toward the operator");
    toggleMiningTether(state);
    require(!mining.operatorRigTethered, "the shared tether input should release the EVA tow line");

    // An exact visual tie belongs to the artifact, preventing an overlapping
    // rig from stealing the recovery input.
    mining.droneX = mining.operatorX + 2.0;
    mining.droneY = mining.operatorY;
    mining.artifact.x = mining.operatorX + 2.5;
    mining.artifact.y = mining.operatorY + 0.5;
    toggleMiningTether(state);
    require(mining.artifact.tethered && !mining.operatorRigTethered,
        "an artifact should win an exact visual-distance tether tie");
    toggleMiningTether(state);

    mining.gate = {};
    mining.gate.active = true;
    mining.gate.type = MiningGateType::HazardCocoon;
    mining.artifact.state = MiningArtifactState::Embedded;
    mining.artifact.revealed = true;
    mining.artifact.x = mining.operatorX + 1.5;
    mining.artifact.y = mining.operatorY + 0.5;
    toggleMiningTether(state);
    require(!mining.artifact.tethered && !mining.operatorRigTethered &&
            state.statusLine.find("locked") != std::string::npos,
        "a nearest gate-locked artifact must explain its lock instead of falling through to the Mining Rig");
    require(mining.artifactTetherDeniedFlashSeconds > 0.0,
        "a nearby locked artifact should trigger the presentation denial flash");

    mining.artifact = {};
    mining.gate = {};
    mining.rigDepthZone = mining.depthZone + 1;
    toggleMiningTether(state);
    require(!mining.operatorRigTethered &&
            state.statusLine.find("Rig's depth") != std::string::npos,
        "an EVA player must not tether a Mining Rig on another depth");
}

void evaTetherFollowsAndRecoversAtShip()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 0x70A11E);
    state.run.planetaryExpedition.active = true;
    state.run.planetaryExpedition.destinationId = content::destination::mars;
    state.run.planetaryExpedition.rigFuel = 4.0;
    state.run.planetaryExpedition.rigFuelCapacity = 4.0;
    state.run.planetaryExpedition.miningSitePrepared = true;
    require(startMiningRun(
                state,
                catalog,
                {MiningAct::ActOne, 8, 0x70A11E, true, MiningGateType::None},
                false)
                .applied,
        "EVA surface towing test mining run should start");

    MiningRunState& mining = state.run.mining;
    mining.artifact = {};
    const double towX = std::min(
        mining.returnZoneX + 10.0,
        static_cast<double>(mining.terrain.width - 4));
    for (int y = 1; y <= 14; ++y) {
        for (int x = static_cast<int>(std::floor(towX)) - 2;
             x <= static_cast<int>(std::floor(towX)) + 2;
             ++x) {
            if (MiningCell* cell = miningCellAt(mining.terrain, x, y)) {
                cell->material = MiningCellMaterial::Empty;
                cell->suitOnlyPassage = false;
            }
        }
    }
    mining.operatorMode = MiningOperatorMode::Jetpack;
    mining.operatorPresent = true;
    mining.operatorX = towX;
    mining.operatorY = 6.0;
    mining.droneX = towX;
    mining.droneY = 10.5;
    mining.rigDepthZone = mining.depthZone;
    mining.operatorVelocityX = 0.0;
    mining.operatorVelocityY = 0.0;
    mining.rigVelocityX = 0.0;
    mining.rigVelocityY = 0.0;
    toggleMiningTether(state);
    require(mining.operatorRigTethered,
        "EVA should attach to a functioning same-layer rig before towing");

    setMiningMove(state, 0.0, -1.0);
    for (int frame = 0; frame < 35; ++frame) {
        updateMiningRun(state, catalog, 0.08);
    }
    setMiningMove(state, 0.0, 0.0);
    const double followedDistance = std::hypot(
        mining.droneX - mining.operatorX,
        mining.droneY - mining.operatorY);
    require(mining.droneY < 6.0 && followedDistance <= 3.0,
        "a vertically towed rig should follow the EVA operator instead of leaving a stretched line at the top boundary");

    mining.operatorX = mining.returnZoneX;
    mining.operatorY = mining.returnZoneY;
    mining.droneX = mining.returnZoneX + 5.0;
    mining.droneY = mining.returnZoneY;
    mining.rigDepthZone = mining.entryDepthZone;
    mining.operatorRigTethered = true;
    mining.cargo = 3;
    mining.temporaryMaterials.common = 3;
    const SurfaceActionOutcome recovery = finishMiningRun(state, catalog, false);
    require(recovery.applied && recovery.cargoDelta == 3 && recovery.materialDelta.common == 3,
        "Bank / Leave should dock a tethered same-layer Mining Rig and bank its payload");
}

void solarCampaignClaimsAdvanceToStraylight()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 0x522022ULL);
    ensureScenarioInstances(state, catalog);
    const std::array<std::string_view, 6> bodies {"moon", "mars", "io", "titan", "titania", "triton"};
    for (std::size_t index = 0; index < bodies.size(); ++index) {
        const SolarMissionDefinition* mission = nextSolarMission(state, catalog);
        require(mission && mission->bodyId == bodies[index], "main missions should follow physical-body order");
        const ScenarioDefinition* scenario = catalog.findScenario(mission->scenarioId);
        require(scenario != nullptr, "each solar mission should resolve its scenario");
        for (const ScenarioStepDefinition& step : scenario->steps) {
            if (step.mandatoryBriefing)
                require(performScenarioAction(state, catalog, scenario->id, step.id,
                    ScenarioActionKind::AcknowledgeBriefing).applied, "mission briefing should be explicit");
            if (step.completionEvent == ScenarioEventKind::ManualAction)
                require(performScenarioAction(state, catalog, scenario->id, step.id,
                    ScenarioActionKind::BeginActivity).applied, "mission setup action should be available");
            else if (step.completionEvent != ScenarioEventKind::None)
                require(recordScenarioEvent(state, catalog,
                    {step.completionEvent, scenario->id, step.id, step.eventOriginId,
                     step.eventTargetId, step.requiredProgress, step.requiredGrade}),
                    "mission event should advance its own objective");
            if (step.claimRequired)
                require(performScenarioAction(state, catalog, scenario->id, step.id,
                    ScenarioActionKind::ClaimReward).applied, "mission should accept one explicit claim");
        }
        require(solarMissionClaimed(state, catalog, *mission), "claimed mission should persist");
        state.screen = Screen::Flight;
        state.run.expedition.travelInitialized = true;
        state.run.expedition.location.bodyId = mission->bodyId;
        state.run.expedition.course.targetBodyId = mission->bodyId;
        state.run.expedition.coursePlayerSelected = false;
        state.run.flight.mode = FlightMode::Orbit;
        require(reconcileSolarMissionMessages(state, catalog),
            "claim and ascent should queue one completion message and recommend Earth");
        require(state.run.expedition.course.targetBodyId == "earth" &&
                !state.run.expedition.cruise.active,
            "main mission completion should mark Earth without activating Cruise");
        state.incomingMessages = {};
        if (index + 1 < bodies.size())
            require(solarBodyRevealed(state, catalog, bodies[index + 1]), "claim should reveal the next mission world");
    }
    require(arkDiscovered(state) && solarBodyRevealed(state, catalog, "straylight"),
        "the claimed Triton mission should reveal Straylight");
}

} // namespace

void drillFeedbackUsesTheCuttingFootprint()
{
    MiningRunState mining;
    mining.terrain.width = mining.terrain.height = 40;
    mining.terrain.cells.resize(1600);
    mining.droneX = mining.droneY = 20.0;
    MiningDrillStats stats;
    stats.sideCutterReach = 1.5;
    for (int heading = 0; heading < 8; ++heading) {
        const double angle = heading * 3.141592653589793 / 4.0;
        mining.hullDirX = std::cos(angle);
        mining.hullDirY = std::sin(angle);
        for (auto& cell : mining.terrain.cells) cell.material = MiningCellMaterial::HardRock;
        const auto contacts = miningDrillFootprintCells(mining, stats);
        for (int side : {-1, 1}) {
            const auto hit = std::find_if(contacts.begin(), contacts.end(),
                [side](const auto& cell) { return cell.cutter == side; });
            require(hit != contacts.end(), "both side cutters must report contact at every heading");
            for (auto& cell : mining.terrain.cells) cell.material = MiningCellMaterial::Empty;
            auto& rock = mining.terrain.cells[hit->y * 40 + hit->x];
            rock.material = MiningCellMaterial::HardRock;
            const auto sideOnly = miningDrillFootprintCells(mining, stats);
            require(sideOnly.size() == 1 && sideOnly.front().cutter == side,
                "side-only cutting must produce its own feedback contact");
            auto mainOnlyStats = stats;
            mainOnlyStats.sideCutterReach = 0.0;
            require(miningDrillFootprintCells(mining, mainOnlyStats).empty(),
                "side feedback must not depend on the main head touching rock");
            rock.material = MiningCellMaterial::Bedrock;
            require(miningDrillFootprintCells(mining, stats).empty(),
                "protected rock must not emit cutting feedback");
        }
    }
}

void blockedDrillFeedbackDistinguishesTerrainAndRespectsVisibility()
{
    MiningRunState mining;
    mining.active = mining.drilling = true;
    mining.rigFuel.current = 10;
    mining.terrain.width = mining.terrain.height = 40;
    mining.terrain.cells.resize(1600);
    mining.droneX = mining.droneY = 20.0;
    mining.aimDirX = mining.hullDirX = 0;
    mining.aimDirY = mining.hullDirY = 1;
    MiningDrillStats stats;
    for (auto& cell : mining.terrain.cells) { cell.material = MiningCellMaterial::HardRock; cell.revealed = true; }
    const auto contacts = miningDrillFootprintCells(mining, stats);
    require(!contacts.empty(), "feedback fixture needs real cutting contact");
    for (auto& cell : mining.terrain.cells) cell.material = MiningCellMaterial::Empty;
    auto& contact = mining.terrain.cells[contacts.front().y * 40 + contacts.front().x];
    contact.material = MiningCellMaterial::HardRock;
    require(miningDrillFeedback(mining, stats).kind == MiningDrillContactKind::HardRock, "hard rock remains drillable feedback");
    contact.material = MiningCellMaterial::Bedrock;
    require(miningDrillFootprintCells(mining, stats).empty() &&
        miningDrillFeedback(mining, stats).kind == MiningDrillContactKind::Bedrock,
        "bedrock produces blocked feedback without entering the damage footprint");
    contact.material = MiningCellMaterial::HazardPocket;
    require(miningDrillFeedback(mining, stats).kind == MiningDrillContactKind::Hazard, "ordinary hazards warn rather than pretend to be sealed");
    contact.cocoonLayer = 0;
    mining.gate.active = true;
    mining.gate.cocoonLayers.resize(1);
    mining.gate.cocoonLayers[0].revealed = true;
    require(miningDrillFeedback(mining, stats).kind == MiningDrillContactKind::ProtectedHazard, "protected hazards request treatment");
    mining.gate.cocoonLayers[0].revealed = false;
    require(miningDrillFeedback(mining, stats).kind == MiningDrillContactKind::None, "feedback must not reveal a hidden seal");
    contact.cocoonLayer = -1;
    contact.material = MiningCellMaterial::Bedrock;
    mining.drilling = false;
    require(miningDrillFeedback(mining, stats).kind == MiningDrillContactKind::None, "releasing drill clears blocked feedback");
    mining.drilling = true;
    mining.rigFuel.current = 0;
    require(miningDrillFeedback(mining, stats).kind == MiningDrillContactKind::None, "an unpowered rig cannot claim to be drilling");
}

void environmentalEncountersHaveSafeBypassesAndPersist()
{
    const auto catalog = createDefaultContent();
    for (const auto& mission : catalog.solarMissions) {
        if (mission.bodyId == "moon" || mission.bodyId == "io") continue;
        for (std::uint64_t seed = 1; seed <= 6; ++seed) {
            auto state = std::make_unique<GameState>(createNewGame(catalog, seed));
            initializeLiveExpedition(*state, catalog);
            for (const auto& other : catalog.solarMissions)
                if (!other.prerequisiteUnlockKey.empty()) state->meta.unlockKeys.push_back(other.prerequisiteUnlockKey);
            state->run.expedition.location.bodyId = mission.bodyId;
            state->run.expedition.location.siteId.clear();
            auto& expedition = state->run.planetaryExpedition;
            expedition = {};
            expedition.active = true;
            expedition.destinationId = mission.environmentId;
            expedition.bodyId = mission.bodyId;
            expedition.rigFuelCapacity = 50;
            state->screen = Screen::Mining;
            require(startMiningRun(*state, catalog, {MiningAct::ActOne, 8, seed}, true).applied,
                "every environmental mission must bind its encounter on direct entry");
            auto& mining = state->run.mining;
            const auto* site = catalog.findMiningSite(mining.miningSiteDefinitionId);
            require(site && !site->terrainPatches.empty(), "mission must use its content-owned terrain encounter");
            auto layer = std::find_if(mining.depthLayers.begin(), mining.depthLayers.end(),
                [](const auto& candidate) { return candidate.artifact.present; });
            require(layer != mining.depthLayers.end(), "encounter artifact must be cached at its campaign depth");
            auto& terrain = layer->terrain;
            const int ax = int(std::floor(layer->artifact.x)), ay = int(std::floor(layer->artifact.y));
            // A three-cell-wide route models rig/tow clearance. Treat every hazard
            // and hard-rock seam as blocked to verify the unequipped bypass too.
            const auto safe = [&](int x, int y) {
                if (x < ax-9 || x > ax+9 || y < ay-6 || y > ay+4) return false;
                for (int dy = -1; dy <= 1; ++dy) for (int dx = -1; dx <= 1; ++dx) {
                    const auto* cell = miningCellAt(terrain, x+dx, y+dy);
                    if (!cell || cell->suitOnlyPassage || cell->material == MiningCellMaterial::Bedrock ||
                        cell->material == MiningCellMaterial::HazardPocket || cell->material == MiningCellMaterial::HardRock) return false;
                }
                return true;
            };
            std::queue<std::pair<int,int>> pending;
            std::vector<bool> visited(terrain.cells.size(), false);
            require(safe(ax, ay), "artifact needs a safe staging area for towing");
            pending.push({ax, ay});
            visited[ay*terrain.width+ax] = true;
            bool reachedApproach = false;
            while (!pending.empty()) {
                const auto [x,y] = pending.front(); pending.pop();
                if (y == ay-6) { reachedApproach = true; break; }
                for (const auto [dx,dy] : std::array<std::pair<int,int>,4>{{{1,0},{-1,0},{0,1},{0,-1}}}) {
                    const int nx=x+dx, ny=y+dy;
                    if (!safe(nx,ny) || visited[ny*terrain.width+nx]) continue;
                    visited[ny*terrain.width+nx]=true; pending.push({nx,ny});
                }
            }
            if (!reachedApproach) std::cerr << "No bypass: " << mission.bodyId << " seed " << seed << '\n';
            require(reachedApproach, "environmental encounter must have a rig-width bypass without hazard upgrades");
            require(std::any_of(terrain.cells.begin(), terrain.cells.end(), [](const auto& cell) {
                return cell.material == MiningCellMaterial::Bedrock || cell.material == MiningCellMaterial::HazardPocket;
            }), "encounter must retain its obstacle");
            const int editedX=ax, editedY=ay-6;
            auto* edited = miningCellAt(terrain, editedX, editedY);
            edited->material = MiningCellMaterial::Empty;
            edited->remainingToughness = edited->maxToughness = 0;
            const auto saved = deserializeSaveData(serializeSaveData(captureSaveData(*state)));
            require(saved.has_value(), "encounter terrain and artifact ownership must serialize");
            auto restored = std::make_unique<GameState>(createNewGame(catalog, 999));
            restoreSaveData(*restored, catalog, *saved);
            const auto restoredLayer = std::find_if(restored->run.mining.depthLayers.begin(), restored->run.mining.depthLayers.end(),
                [](const auto& candidate) { return candidate.artifact.present; });
            require(restoredLayer != restored->run.mining.depthLayers.end() &&
                restoredLayer->artifact.x == layer->artifact.x && restoredLayer->artifact.y == layer->artifact.y &&
                miningCellAt(restoredLayer->terrain, editedX, editedY)->material == MiningCellMaterial::Empty,
                "reload must preserve excavation and artifact position instead of restamping the encounter");
        }
    }
}

int main()
{
    {
        auto state = std::make_unique<GameState>();
        auto& rig = state->run.mining;
        rig.active = true;
        rig.terrain.width = rig.terrain.height = 40;
        rig.terrain.cells.resize(1600);
        rig.droneX = rig.droneY = 20;
        for (double heading : {0.0, 1.5707963267948966, 3.141592653589793, -1.5707963267948966}) {
            rig.hullDirX = std::cos(heading); rig.hullDirY = std::sin(heading);
            setMiningRigPiloting(*state, 0, -1, 0);
            require(std::abs(rig.moveX + rig.hullDirX) < 1e-8 && std::abs(rig.moveY + rig.hullDirY) < 1e-8,
                "reverse thrust must not turn the rig around");
            require(std::abs(rig.aimDirX - rig.hullDirX) < 1e-8 && std::abs(rig.aimDirY - rig.hullDirY) < 1e-8,
                "neutral steering retains hull heading");
            setMiningRigPiloting(*state, 0, 0, 1);
            require(std::abs(rig.moveX + rig.hullDirY) < 1e-8 && std::abs(rig.moveY - rig.hullDirX) < 1e-8,
                "rig right strafe uses the clockwise perpendicular in Y-down coordinates");
            for (double turn : {-1.0, -0.2, 0.2, 1.0}) {
                setMiningRigPiloting(*state, turn, 0, 0);
                const double error = std::remainder(std::atan2(rig.aimDirY, rig.aimDirX) - heading, 6.283185307179586);
                require(std::abs(error - turn * 0.5) < 1e-8, "rig turning remains proportional without snapping to drill directions");
                require(rig.moveX == 0 && rig.moveY == 0, "rotation alone does not apply translation");
            }
        }
    }
    blockedDrillFeedbackDistinguishesTerrainAndRespectsVisibility();
    environmentalEncountersHaveSafeBypassesAndPersist();
    allActLevelContractsResolve();
    drillFeedbackUsesTheCuttingFootprint();
    campaignMappingMatchesChapterPace();
    deterministicSeedsAndRewardProgressAreStable();
    enemyThemesFollowProgressionAndRemainDeterministic();
    progressionSaveFieldsRoundTripAndLegacyDefault();
    miningGateContractsAndRuntimeAreDeterministic();
    progressionArtifactPlacementDeepensByMissionStage();
    lunarCreviceHasThreeVisibleCellsAndScansFromTheTop();
    oldLunarCreviceSaveMigratesWithoutLosingTerrain();
    solarCampaignClaimsAdvanceToStraylight();
    authoredArtifactLayerIsPrebuiltAtResolvedDepth();
    thermalSiteRulesAreContentDriven();
    layeredCocoonsHonorAuthoredRevealPolicies();
    tetherTargetingPrioritizesArtifactsAndKeepsEvaTow();
    evaTetherFollowsAndRecoversAtShip();
    std::cout << "rocket_mining_progression_tests passed\n";
    return 0;
}
