#include "core/Content.h"
#include "core/ContentIds.h"
#include "core/ArtifactProgression.h"
#include "core/GameState.h"
#include "core/MiningProgression.h"
#include "core/MiningSystem.h"
#include "core/SaveData.h"
#include "core/SaveSchema.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
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
                require(rules.mechanics.fogAndScanner == (difficulty >= 2), "Act 1 scanner gate should match its level table");
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
    require(!actOneLevelOne.mechanics.fogAndScanner && actOneLevelOne.mechanics.oxygenAndFuel,
        "Act 1 level 1 should teach movement, drilling, fuel, and oxygen without general scanner fog");
    require(actOneLevelTwo.mechanics.fogAndScanner && actOneLevelTwo.mechanics.oxygenAndFuel, "Act 1 level 2 should introduce scanner and endurance resources");
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

void progressionArtifactPlacementAdvancesThreePerDepth()
{
    ContentCatalog catalog = createDefaultContent();
    const auto addArtifactScenario = [&](std::string id, std::string destinationId) {
        ScenarioDefinition scenario;
        scenario.id = std::move(id);
        scenario.destinationId = std::move(destinationId);
        ScenarioStepDefinition step;
        step.id = "artifact";
        step.completionEvent = ScenarioEventKind::ArtifactRecovered;
        step.eventOriginId = scenario.destinationId;
        scenario.steps.push_back(std::move(step));
        catalog.scenarios.push_back(std::move(scenario));
    };
    addArtifactScenario("uranus_artifact_test", content::destination::uranus);
    addArtifactScenario("neptune_artifact_test", content::destination::neptune);

    GameState state = createNewGame(catalog, 0xA471FAC7ULL);
    const Destination* jupiter = catalog.findDestination(content::destination::jupiter);
    require(jupiter != nullptr, "Jupiter should exist for progression artifact placement tests");

    const ProgressionArtifactPlacement first = resolveProgressionArtifactPlacement(
        state, catalog, *jupiter, 1, "first_artifact");
    require(first.ordinal == 0 && first.targetDepth == 1 && first.withinDepthSlot == 0 &&
            first.horizontalOffset == 0 && first.verticalOffset == 10,
        "the first progression artifact should be centered ten cells below the depth-one entry");

    state.meta.artifacts.push_back({"random_mars_bonus", content::destination::mars});
    require(resolveProgressionArtifactPlacement(state, catalog, *jupiter, 1, "first_artifact").ordinal == 0,
        "an incidental artifact from a non-authored destination must not advance progression placement");

    state.meta.artifacts.push_back({"jupiter_progression", content::destination::jupiter});
    const ProgressionArtifactPlacement secondEasy = resolveProgressionArtifactPlacement(
        state, catalog, *jupiter, 1, "second_artifact");
    const ProgressionArtifactPlacement secondHard = resolveProgressionArtifactPlacement(
        state, catalog, *jupiter, 10, "second_artifact");
    require(secondEasy.targetDepth == 1 && secondEasy.withinDepthSlot == 1 &&
            std::abs(secondEasy.horizontalOffset) <= 10 && secondEasy.verticalOffset >= 1 &&
            secondEasy.verticalOffset <= 10 && secondEasy.manhattanDistance >= 11 &&
            secondEasy.manhattanDistance <= 14,
        "the second artifact should stay on depth one inside the first displacement band");
    require(secondHard.manhattanDistance >= secondEasy.manhattanDistance,
        "raising mining difficulty must not make the same progression slot easier");

    state.meta.artifacts.push_back({"jupiter_duplicate", content::destination::jupiter});
    require(resolveProgressionArtifactPlacement(state, catalog, *jupiter, 5, "second_artifact").ordinal == 1,
        "duplicate artifacts from one authored destination must count only once");

    state.meta.artifacts.push_back({"saturn_progression", content::destination::saturn});
    const ProgressionArtifactPlacement third = resolveProgressionArtifactPlacement(
        state, catalog, *jupiter, 5, "third_artifact");
    require(third.targetDepth == 1 && third.withinDepthSlot == 2 &&
            third.manhattanDistance >= 15 && third.manhattanDistance <= 20 &&
            std::abs(third.horizontalOffset) <= 10 && third.verticalOffset <= 10,
        "the third artifact should remain on depth one inside the harder displacement band");

    state.meta.artifacts.push_back({"uranus_progression", content::destination::uranus});
    const ProgressionArtifactPlacement fourth = resolveProgressionArtifactPlacement(
        state, catalog, *jupiter, 10, "fourth_artifact");
    require(fourth.ordinal == 3 && fourth.targetDepth == 2 && fourth.withinDepthSlot == 0 &&
            fourth.horizontalOffset == 0 && fourth.verticalOffset == 10,
        "the fourth progression artifact should reset to the centered depth-two layout");
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

} // namespace

int main()
{
    allActLevelContractsResolve();
    campaignMappingMatchesChapterPace();
    deterministicSeedsAndRewardProgressAreStable();
    enemyThemesFollowProgressionAndRemainDeterministic();
    progressionSaveFieldsRoundTripAndLegacyDefault();
    miningGateContractsAndRuntimeAreDeterministic();
    progressionArtifactPlacementAdvancesThreePerDepth();
    authoredArtifactLayerIsPrebuiltAtResolvedDepth();
    thermalSiteRulesAreContentDriven();
    layeredCocoonsHonorAuthoredRevealPolicies();
    tetherTargetingPrioritizesArtifactsAndKeepsEvaTow();
    evaTetherFollowsAndRecoversAtShip();
    std::cout << "rocket_mining_progression_tests passed\n";
    return 0;
}
