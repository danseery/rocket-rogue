#include "core/Content.h"
#include "core/ContentIds.h"
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
        std::abort();
    }
}

void allActLevelContractsResolve()
{
    require(miningActName(MiningAct::ActOne) == "Act 1" && miningActName(MiningAct::ActThree) == "Act 3",
        "mining act labels should use consistent player-facing numbering");
    require(miningProgressionBandName(MiningProgressionBand::Learn) == "Learn"
            && miningProgressionBandName(MiningProgressionBand::Mastery) == "Mastery",
        "mining band labels should use the contract names");
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
            require(!rules.complication.empty() && !rules.tutorialCallout.empty(), "every act/level should resolve player-facing progression copy");
            require(miningMaterialAllowed(rules, MiningCellMaterial::CommonOre), "every mining arena should permit common ore");
            require(miningRoomFeatureAllowed(rules, MiningCellFeature::MainTunnel), "every mining arena should permit a main route");

            if (act == MiningAct::ActOne) {
                require(rules.maxActiveEnemies == 0, "Act 1 should never permit active enemies");
                require(!miningMaterialAllowed(rules, MiningCellMaterial::ExoticVein), "Act 1 should never permit exotic mineral veins");
                require(!miningEnemyAllowed(rules, MiningEnemyType::Ant), "Act 1 should have no enemy roster");
                require(rules.enemyHealthScale == 0.0 && rules.enemyDamageScale == 0.0, "Act 1 should not expose combat scaling");
                require(rules.mechanics.fogAndScanner == (difficulty >= 2), "Act 1 scanner gate should match its level table");
                require(rules.mechanics.oxygenAndFuel == (difficulty >= 2), "Act 1 endurance gate should match its level table");
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
    require(!actOneLevelOne.mechanics.fogAndScanner, "Act 1 level 1 should isolate movement and drilling");
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
    require(legacySave.has_value(), "legacy save should still deserialize");
    for (const MiningFirstClearProgress& item : legacySave->miningFirstClearProgress) {
        require(item.rareBanked == 0 && item.exoticBanked == 0, "legacy saves should default to no fulfilled rich guarantees");
    }
    require(legacySave->mining.arenaMetadata.rulesVersion == 0, "missing legacy arena metadata should remain detectable for restore migration");

    const ContentCatalog catalog = createDefaultContent();
    SaveData activeLegacy;
    activeLegacy.seed = 321;
    activeLegacy.chapter = GameChapter::RedFrontier;
    activeLegacy.screen = Screen::Mining;
    activeLegacy.surfaceExpedition.active = true;
    activeLegacy.surfaceExpedition.destinationId = content::destination::mars;
    activeLegacy.surfaceExpedition.depth = 2;
    activeLegacy.mining.active = true;
    activeLegacy.mining.destinationId = content::destination::mars;
    activeLegacy.mining.terrain.cells.resize(
        static_cast<std::size_t>(activeLegacy.mining.terrain.width * activeLegacy.mining.terrain.height));

    GameState restoredState = createNewGame(catalog, 999);
    restoreSaveData(restoredState, catalog, activeLegacy);
    require(restoredState.run.mining.active, "legacy arena migration should preserve valid serialized terrain");
    require(restoredState.run.mining.arenaMetadata.act == MiningAct::ActOne
            && restoredState.run.mining.arenaMetadata.difficulty == 6,
        "legacy active arena metadata should derive from campaign chapter and surface depth");
    require(restoredState.run.mining.arenaMetadata.rulesVersion == miningArenaRulesVersion,
        "legacy active arena migration should stamp the current rules version without rerolling");
    require(restoredState.run.mining.terrain.cells.size()
            == static_cast<std::size_t>(activeLegacy.mining.terrain.width * activeLegacy.mining.terrain.height),
        "legacy active arena metadata migration should not reroll serialized terrain");
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
    require(actOneEight.fixedStoryGate == MiningGateType::HazardCocoon && actOneEight.maximumGateLocks == 1,
        "Act 1 level 8 should anchor the one-lock Hazard Cocoon story site");
    require(actTwoTwo.fixedStoryGate == MiningGateType::EnemySealedChamber,
        "Act 2 level 2 should anchor the enemy-sealed story site");
    require(miningGateAllowed(actTwoFour, MiningGateType::ShieldCorridor),
        "Act 2 level 4 should permit shield corridor sites after ranged enemies are taught");
    require(actTwoSeven.fixedStoryGate == MiningGateType::CompoundStoryVault && actTwoSeven.maximumGateLocks == 2,
        "Act 2 pressure should combine exactly two previously taught locks");
    require(actThreeOne.fixedStoryGate == MiningGateType::BurrowBreach && actThreeOne.maximumGateLocks == 3,
        "Act 3 should introduce the burrow gate and support three-lock capstones");
    require(!miningGateAllowed(actTwoSeven, MiningGateType::BurrowBreach),
        "Act 2 must never leak the Act 3 Mammal gate");

    const MiningArenaRules illegalOverride = resolveMiningArenaRules({
        MiningAct::ActOne, 8, 99, true, MiningGateType::BurrowBreach
    });
    require(selectMiningGateType(illegalOverride) == MiningGateType::None,
        "Arena Lab must reject an override that violates the Act roster");

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
    MiningStorySiteProgress* firstSite = ensureMiningStorySite(meta, content::destination::outerPlanets, actOneEight);
    MiningStorySiteProgress* sameSite = ensureMiningStorySite(meta, content::destination::outerPlanets, actOneEight);
    require(firstSite != nullptr && sameSite != nullptr && firstSite->seed == sameSite->seed && firstSite->artifactId == sameSite->artifactId,
        "story sites should retain deterministic seed and artifact identity until completion");
    creditExtractedMiningStoryArtifacts(meta, {{"wrong", content::destination::outerPlanets, false, ArtifactKind::Story}});
    require(!meta.miningStorySites.front().completed, "unrelated recovered artifacts must not complete a story site");
    ArtifactRecord recovered;
    recovered.id = meta.miningStorySites.front().artifactId;
    recovered.kind = ArtifactKind::Story;
    creditExtractedMiningStoryArtifacts(meta, {recovered});
    require(meta.miningStorySites.front().completed, "only the banked site artifact should complete persistent story progress");

    const ContentCatalog catalog = createDefaultContent();
    auto prepareSurface = [](GameState& state, std::string_view destinationId) {
        state.run.surfaceExpedition = {};
        state.run.surfaceExpedition.active = true;
        state.run.surfaceExpedition.destinationId = std::string(destinationId);
        state.run.surfaceExpedition.sharedFuel = 4;
        state.run.surfaceExpedition.sharedFuelCapacity = 4;
        state.run.surfaceExpedition.miningSitePrepared = true;
    };

    GameState hazardState = createNewGame(catalog, 501);
    prepareSurface(hazardState, content::destination::outerPlanets);
    const MiningArenaRequest hazardRequest {MiningAct::ActOne, 8, 0xCAFE, true, MiningGateType::HazardCocoon};
    require(startMiningRun(hazardState, catalog, hazardRequest, false).applied, "Hazard Cocoon debug arena should start");
    require(hazardState.run.mining.gate.type == MiningGateType::HazardCocoon
            && hazardState.run.mining.gate.shellTilesRemaining == 8,
        "Hazard Cocoon should stamp eight marked, deterministic shell tiles");
    require(hazardState.run.mining.gate.derivedStateDirty,
        "new gate runtime should require one derived-state reconciliation");
    hazardState.run.mining.droneX = hazardState.run.mining.artifact.x;
    hazardState.run.mining.droneY = hazardState.run.mining.artifact.y;
    toggleMiningTether(hazardState);
    require(!hazardState.run.mining.artifact.tethered, "a locked cocoon must reject tether bypass");
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
    prepareSurface(surveyState, content::destination::outerPlanets);
    const MiningArenaRequest surveyRequest {MiningAct::ActOne, 8, 0x5151, true, MiningGateType::SurveyTriangulation};
    require(startMiningRun(surveyState, catalog, surveyRequest, false).applied, "Survey Triangulation debug arena should start");
    require(surveyState.run.mining.gate.markers.size() == 3, "triangulation should stamp three distinct scanner origins");
    updateMiningRun(surveyState, catalog, 0.01);
    require(!surveyState.run.mining.gate.derivedStateDirty,
        "initial triangulation reconciliation should clean the derived cache");
    for (const MiningGateMarker marker : surveyState.run.mining.gate.markers) {
        surveyState.run.mining.droneX = marker.x;
        surveyState.run.mining.droneY = marker.y;
        pulseMiningScanner(surveyState, catalog);
    }
    require(surveyState.run.mining.gate.derivedStateDirty,
        "activating a triangulation marker should invalidate gate-derived state");
    updateMiningRun(surveyState, catalog, 0.01);
    require(surveyState.run.mining.gate.surveyComplete && surveyState.run.mining.gate.state == MiningGateState::Open,
        "a no-drone rig should solve triangulation by repositioning to every marker");
    require(!surveyState.run.mining.gate.derivedStateDirty,
        "triangulation should resolve and clean its cache in the same simulation tick");

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
    save.miningStorySites = meta.miningStorySites;
    const std::optional<SaveData> gateRoundTrip = deserializeSaveData(serializeSaveData(save));
    require(gateRoundTrip.has_value()
            && gateRoundTrip->mining.gate.type == MiningGateType::HazardCocoon
            && gateRoundTrip->mining.gate.derivedStateDirty
            && gateRoundTrip->miningStorySites.front().artifactId == meta.miningStorySites.front().artifactId,
        "active gate state and persistent story identity should survive save/load while transient derived state reloads dirty");
}

} // namespace

int main()
{
    allActLevelContractsResolve();
    campaignMappingMatchesChapterPace();
    deterministicSeedsAndRewardProgressAreStable();
    progressionSaveFieldsRoundTripAndLegacyDefault();
    miningGateContractsAndRuntimeAreDeterministic();
    std::cout << "rocket_mining_progression_tests passed\n";
    return 0;
}
