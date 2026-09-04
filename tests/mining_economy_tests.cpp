#include "core/Content.h"
#include "core/ContentIds.h"
#include "core/GameState.h"
#include "core/MiningProgression.h"
#include "core/MiningSystem.h"
#include "core/ResearchSystem.h"
#include "core/SaveData.h"
#include "core/ScenarioSystem.h"
#include "core/Tuning.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace rocket;

void require(bool condition, const std::string& message)
{
    if (!condition) {
        std::cerr << "Mining economy test failure: " << message << '\n';
        throw std::runtime_error(message);
    }
}

void prepareSurface(GameState& state, std::string destinationId)
{
    state.run.planetaryExpedition = {};
    state.run.planetaryExpedition.active = true;
    state.run.planetaryExpedition.destinationId = std::move(destinationId);
    state.run.planetaryExpedition.rigFuel = 4.0;
    state.run.planetaryExpedition.rigFuelCapacity = 4.0;
    state.run.planetaryExpedition.miningSitePrepared = true;
    state.screen = Screen::SurfaceExpedition;
}

struct ExpeditionExperienceSnapshot {
    int level = 1;
    double progress = 0.0;
};

ExpeditionExperienceSnapshot snapshotExpeditionExperience(const GameState& state)
{
    return {
        state.run.planetaryExpedition.expeditionLevel,
        state.run.planetaryExpedition.expeditionExperience
    };
}

double expeditionExperienceEarnedSince(
    const ExpeditionExperienceSnapshot& before,
    const GameState& state)
{
    const PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    double earned = expedition.expeditionExperience - before.progress;
    for (int level = before.level; level < expedition.expeditionLevel; ++level) {
        earned += expeditionExperienceThreshold(level);
    }
    return earned;
}

int richCellCount(const MiningTerrain& terrain, MiningCellMaterial material)
{
    int count = 0;
    for (const MiningCell& cell : terrain.cells) {
        count += cell.material == material ? 1 : 0;
    }
    return count;
}

GameState extractWithSuccessfulSeed(const GameState& beforeExtraction)
{
    for (std::uint64_t seed = 1; seed < 256; ++seed) {
        GameState candidate = beforeExtraction;
        Random rng(seed);
        const SurfaceActionOutcome outcome = extractSurfacePayload(candidate);
        if (outcome.cargoRecovered) {
            return candidate;
        }
    }
    throw std::runtime_error("expected at least one deterministic successful surface extraction");
}

void miningExperienceAwardsMatchApprovedEconomy()
{
    require(
        miningMaterialExperience({1, 1, 1}) == 13,
        "Common, Rare, and Exotic ore should award 1, 3, and 9 XP");
    require(
        miningMaterialExperience({-4, 2, -1}) == 6,
        "negative material counts should never subtract expedition XP");

    require(
        miningHazardTreatmentExperience(MiningElementalAffinity::Thermal) == 1
            && miningHazardTreatmentExperience(MiningElementalAffinity::Cryo) == 1
            && miningHazardTreatmentExperience(MiningElementalAffinity::Toxic) == 3
            && miningHazardTreatmentExperience(MiningElementalAffinity::Radiation) == 9,
        "hazard conversion XP should follow the approved 1/3/9 rarity economy");

    MiningEnemy normal = createMiningEnemy(
        MiningEnemyType::Ant,
        MiningCellFeature::None,
        0.0,
        0.0);
    MiningEnemy elite = normal;
    elite.elite = true;
    MiningEnemy spawner = createMiningEnemySpawner(
        0.0,
        0.0,
        1.0,
        MiningEnemyType::Ant,
        1,
        1.0);
    MiningEnemy miniboss = normal;
    miniboss.sourceFeature = MiningCellFeature::MinibossLair;
    miniboss.elite = true;
    MiningEnemy boss = normal;
    boss.sourceFeature = MiningCellFeature::BossChamber;
    boss.elite = true;

    require(
        miningEnemyDefeatExperience(normal, 5) == 1
            && miningEnemyDefeatExperience(normal, 10) == 2,
        "normal enemy XP should use the approved difficulty scaling");
    require(
        miningEnemyDefeatExperience(elite, 5) == 4
            && miningEnemyDefeatExperience(elite, 10) == 5
            && miningEnemyDefeatExperience(spawner, 5) == 4
            && miningEnemyDefeatExperience(spawner, 10) == 5,
        "elite and spawner XP should share one exclusive combat class");
    require(
        miningEnemyDefeatExperience(miniboss, 5) == 10
            && miningEnemyDefeatExperience(miniboss, 10) == 12,
        "miniboss XP should not stack with the elite class");
    require(
        miningEnemyDefeatExperience(boss, 5) == 25
            && miningEnemyDefeatExperience(boss, 10) == 31,
        "boss XP should not stack with the elite class");

    boss.swarmAssociated = true;
    require(
        miningEnemyDefeatExperience(boss, 10) == 0,
        "individual swarm bodies, including the final elite, should award no combat XP");
    require(
        miningSwarmWaveExperience(1, 1)
                + miningSwarmWaveExperience(2, 1)
                + miningSwarmWaveExperience(3, 1)
            == 10
            && miningSwarmWaveExperience(1, 5)
                + miningSwarmWaveExperience(2, 5)
                + miningSwarmWaveExperience(3, 5)
            == 12
            && miningSwarmWaveExperience(1, 10)
                + miningSwarmWaveExperience(2, 10)
                + miningSwarmWaveExperience(3, 10)
            == 16,
        "completed swarm waves should award the approved 2/3/5 bases with difficulty scaling");
}

void explicitArenaIsDeterministicAndBudgeted()
{
    const ContentCatalog catalog = createDefaultContent();
    const MiningArenaRequest request {MiningAct::ActOne, 9, 0xA119ULL};

    GameState first = createNewGame(catalog, 101);
    prepareSurface(first, content::destination::mars);
    require(startMiningRun(first, catalog, request, true).applied, "explicit Act 1 arena should start");

    GameState second = createNewGame(catalog, 999);
    prepareSurface(second, content::destination::mars);
    require(startMiningRun(second, catalog, request, true).applied, "matching explicit arena should start");

    const MiningRunState& lhs = first.run.mining;
    const MiningRunState& rhs = second.run.mining;
    require(lhs.arenaMetadata.seed == request.seed, "active arena should retain the explicit seed");
    require(lhs.terrain.cells.size() == rhs.terrain.cells.size(), "matching requests should produce matching terrain sizes");
    for (std::size_t i = 0; i < lhs.terrain.cells.size(); ++i) {
        require(lhs.terrain.cells[i].material == rhs.terrain.cells[i].material, "matching request seeds should reproduce materials");
        require(lhs.terrain.cells[i].feature == rhs.terrain.cells[i].feature, "matching request seeds should reproduce room features");
        require(std::abs(lhs.terrain.cells[i].maxToughness - rhs.terrain.cells[i].maxToughness) < 0.000001,
            "matching request seeds should reproduce toughness");
    }

    require(richCellCount(lhs.terrain, MiningCellMaterial::RareOre) <= lhs.rewardBudget.rareCap,
        "terrain rare deposits should fit the arena-wide rare cap");
    require(richCellCount(lhs.terrain, MiningCellMaterial::ExoticVein) == 0,
        "Act 1 should never place exotic deposits");
    int revealedRare = 0;
    for (const MiningCell& cell : lhs.terrain.cells) {
        revealedRare += cell.material == MiningCellMaterial::RareOre && cell.revealed ? 1 : 0;
    }
    require(revealedRare >= lhs.rewardBudget.rareGuarantee,
        "unfulfilled first-clear guarantees should be placed as scanner-readable deposits");

    const MiningArenaRules rules = resolveMiningArenaRules(request);
    for (const MiningCell& cell : lhs.terrain.cells) {
        if (cell.material == MiningCellMaterial::Regolith) {
            require(std::abs(cell.maxToughness - tuning::mining::regolithToughness * rules.terrainToughnessScale) < 0.000001,
                "terrain toughness should use the act-level scale instead of raw depth scaling");
            break;
        }
    }
}

void richPayoutsShareOneLedger()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 202);
    prepareSurface(state, content::destination::mars);
    require(startMiningRun(state, catalog, {MiningAct::ActOne, 4, 0xB00BULL}, true).applied,
        "ledger arena should start");

    MiningRunState& mining = state.run.mining;
    mining.droneX = 32.0;
    mining.droneY = 4.0;
    setMiningMove(state, 1.0, 0.0);
    setMiningMove(state, 0.0, 0.0);
    setMiningDrilling(state, true);
    const MaterialInventory materialsBefore = mining.temporaryMaterials;
    const ExpeditionExperienceSnapshot experienceBefore =
        snapshotExpeditionExperience(state);

    auto breakRareCell = [&]() {
        MiningCell* cell = miningCellAt(mining.terrain, 33, 4);
        require(cell != nullptr, "ledger test cell should exist");
        *cell = {MiningCellMaterial::RareOre, 0.01, 0.01, true, false};
        for (int i = 0; i < 6 && cell->material != MiningCellMaterial::Empty; ++i) {
            updateMiningRun(state, catalog, 0.08);
        }
        require(cell->material == MiningCellMaterial::Empty, "test rare cell should be drilled");
    };

    breakRareCell();
    breakRareCell();
    int looseRare = 0;
    int looseCommon = 0;
    for (const MiningLooseObject& object : mining.looseObjects) {
        if (!object.active || object.kind != MiningLooseObjectKind::Material) {
            continue;
        }
        looseRare += object.material == MiningCellMaterial::RareOre ? 1 : 0;
        looseCommon += object.material == MiningCellMaterial::CommonOre ? 1 : 0;
    }
    require(looseRare == 1 && mining.richRewardsAwarded.rare == 1,
        "all mining reward paths should stop rare payouts at the shared cap");
    require(looseCommon >= 1, "rich payout overflow should retain common salvage value");
    require(mining.temporaryMaterials.common == materialsBefore.common &&
            mining.temporaryMaterials.rare == materialsBefore.rare,
        "fractured ore must remain physical until the rig intake reaches it");

    for (MiningLooseObject& object : mining.looseObjects) {
        if (object.active && object.kind == MiningLooseObjectKind::Material) {
            object.x = mining.droneX;
            object.y = mining.droneY;
            object.pickupDelaySeconds = 0.0;
        }
    }
    updateMiningRun(state, catalog, 0.08);
    const MaterialInventory acquired {
        mining.temporaryMaterials.common - materialsBefore.common,
        mining.temporaryMaterials.rare - materialsBefore.rare,
        mining.temporaryMaterials.exotic - materialsBefore.exotic
    };
    require(
        std::abs(
            expeditionExperienceEarnedSince(experienceBefore, state)
            - static_cast<double>(miningMaterialExperience(acquired)))
            < 0.000001,
        "physical ore should award XP exactly when it enters the rig manifest");
}

void lunarContractActivatesScannerLedEvaArtifactInSameRun()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 220);
    require(performScenarioAction(
                state,
                catalog,
                content::scenario::lunarProspector,
                "briefing",
                ScenarioActionKind::AcknowledgeBriefing)
                .applied,
        "the lunar contract briefing should be acknowledgeable");
    prepareSurface(state, content::destination::moon);
    state.run.planetaryExpedition.rigFuel = tuning::research::expeditionRigPackFuel;
    state.run.planetaryExpedition.rigFuelCapacity = tuning::research::expeditionRigPackFuel;
    require(startMiningRun(state, catalog, {MiningAct::ActOne, 1, 0x20ULL}, true).applied,
        "the first lunar mining run should start");

    MiningRunState& mining = state.run.mining;
    const MiningArenaRules firstMoonRules = resolveMiningArenaRules({
        mining.arenaMetadata.act,
        mining.arenaMetadata.difficulty,
        mining.arenaMetadata.seed});
    require(firstMoonRules.mechanics.oxygenAndFuel &&
            !firstMoonRules.mechanics.drillHeat &&
            std::abs(mining.rigFuel.capacity - tuning::research::expeditionRigPackFuel) < 0.000001,
        "the first Moon expedition should start with its three-fuel allotment and live oxygen, without heat");
    const int availableCommonOre = static_cast<int>(std::count_if(
        mining.terrain.cells.begin(),
        mining.terrain.cells.end(),
        [](const MiningCell& cell) { return cell.material == MiningCellMaterial::CommonOre; }));
    require(availableCommonOre >= tuning::research::prospectorCommonOreGoal,
        "the authored first Moon terrain should contain enough physical Common Ore for one intended haul");
    mining.droneX = mining.returnZoneX;
    mining.droneY = mining.returnZoneY;
    mining.temporaryMaterials.common = tuning::research::prospectorCommonOreGoal;
    mining.cargo = tuning::research::prospectorCommonOreGoal;
    require(bankMiningPayloadAtShip(state, catalog),
        "twenty lunar ore should transfer directly into contract allocation");
    require(state.meta.materials.common == 0,
        "lunar contract ore should not consume the starting twelve-mass ship hold");
    require(!hasUnlock(state.meta, content::unlock::routeMars),
        "ore delivery alone must not unlock Mars");

    updateMiningRun(state, catalog, 0.01);
    require(mining.miningSiteDefinitionId == content::miningSite::lunarAnomalyCrevice &&
            mining.gate.siteId == content::miningSite::lunarAnomalyCrevice &&
            mining.artifact.id == content::protectedObjective::lunarSignalArtifact,
        "the twentieth banked ore should activate the authored anomaly in the same mining run");
    int suitOnlyCells = 0;
    for (const MiningCell& cell : mining.terrain.cells) {
        suitOnlyCells += cell.suitOnlyPassage ? 1 : 0;
    }
    require(suitOnlyCells >= 6 && !mining.artifact.revealed,
        "the lunar artifact should begin behind a persistent suit-only crevice");
    pulseMiningScanner(state, catalog);
    require(mining.artifact.revealed,
        "one contextual scanner pulse should reveal the lunar artifact bearing");

    mining.depthZone = mining.entryDepthZone;
    mining.artifact.state = MiningArtifactState::Loose;
    mining.artifact.tethered = true;
    mining.artifact.x = mining.returnZoneX;
    mining.artifact.y = mining.returnZoneY;
    updateMiningRun(state, catalog, 0.01);
    require(mining.artifact.state == MiningArtifactState::Delivered &&
            mining.stowedArtifacts.size() == 1,
        "towing the anomaly into the ship capture field should secure exactly one physical artifact");
    require(scenarioStepState(
                state,
                catalog,
                content::scenario::lunarProspector,
                "anomaly") == ScenarioStepState::ReadyToClaim,
        "physical artifact recovery should advance the anomaly to an explicit claim");
    require(!hasUnlock(state.meta, content::unlock::routeMars),
        "recovering the artifact should not silently advance the story before its claim");
    require(performScenarioAction(
                state,
                catalog,
                content::scenario::lunarProspector,
                "anomaly",
                ScenarioActionKind::ClaimReward)
                .applied,
        "the recovered anomaly should expose a working explicit claim action");
    require(hasUnlock(state.meta, content::unlock::routeMars),
        "claiming the recovered lunar artifact should unlock the Mars route");

    const std::size_t artifactCount = mining.stowedArtifacts.size();
    updateMiningRun(state, catalog, 0.25);
    require(mining.stowedArtifacts.size() == artifactCount,
        "delivered artifacts must not duplicate on later mining updates");
}

void rigLoadBandsAndHardCapacityAreImmediate()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 221);
    prepareSurface(state, content::destination::moon);
    require(startMiningRun(state, catalog, {MiningAct::ActOne, 1, 0x21ULL}, true).applied,
        "the lunar load-band test should start a mining run");

    struct ExpectedBand {
        int mass;
        RigLoadBand band;
        double speed;
        double fuel;
    };
    const std::array<ExpectedBand, 5> expectations {{
        {5, RigLoadBand::Light, 1.00, 1.00},
        {6, RigLoadBand::Standard, 0.90, 1.15},
        {12, RigLoadBand::Laden, 0.72, 1.40},
        {18, RigLoadBand::Packrat, 0.55, 1.70},
        {24, RigLoadBand::Full, 0.50, 1.75},
    }};
    for (const ExpectedBand& expected : expectations) {
        state.run.mining.cargo = expected.mass;
        state.run.mining.temporaryMaterials = {expected.mass, 0, 0};
        const MiningLoadStats load = miningLoadStats(state, catalog);
        require(load.band == expected.band &&
                std::abs(load.speedMultiplier - expected.speed) < 0.000001 &&
                std::abs(load.fuelConsumptionMultiplier - expected.fuel) < 0.000001,
            "first-run rig load thresholds must immediately apply their published movement and fuel curve");
    }

    MiningRunState& mining = state.run.mining;
    MiningLooseObject overflow;
    overflow.persistentId = mining.nextLooseObjectId++;
    overflow.kind = MiningLooseObjectKind::Material;
    overflow.material = MiningCellMaterial::CommonOre;
    overflow.x = mining.droneX;
    overflow.y = mining.droneY;
    overflow.cargoValue = 1;
    overflow.mass = 1.0;
    overflow.active = true;
    mining.looseObjects.push_back(overflow);
    updateMiningRun(state, catalog, 0.08);
    require(mining.cargo == 24 &&
            !mining.looseObjects.empty() &&
            mining.looseObjects.front().active,
        "ore beside a full rig must remain a persistent physical object instead of being deleted or converted");
}

void supportDroneXpWaitsForAuthoritativeShipDelivery()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 252);
    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    state.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
    ensureDroneBayState(state, catalog);
    state.meta.droneBaySlots = 1;
    state.meta.equippedDroneIds = {content::drone::resourceDrone};
    prepareSurface(state, content::destination::mars);
    require(
        startMiningRun(
            state,
            catalog,
            {MiningAct::ActOne, 3, 0xD311ULL},
            false).applied,
        "Support Drone XP fixture should start");

    MiningRunState& mining = state.run.mining;
    mining.enemies.clear();
    mining.rigOxygen.current = 100.0;
    require(
        mining.miniDrones.size() == 1
            && mining.miniDrones.front().role == MiniDroneRole::Resource,
        "Support Drone XP fixture should create its Resource Drone agent");
    MiningMiniDroneAgent& resource = mining.miniDrones.front();
    resource.haulMaterials = {1, 1, 1};
    resource.uncreditedHaulMaterials = {1, 0, 1};
    resource.behavior = MiningMiniDroneBehavior::DeliveringToShip;
    resource.actionCooldownSeconds = 0.0;

    const ExpeditionExperienceSnapshot experienceBefore =
        snapshotExpeditionExperience(state);
    updateMiningRun(state, catalog, 0.01);
    require(
        resource.haulMaterials.common == 0
            && resource.haulMaterials.rare == 0
            && resource.haulMaterials.exotic == 0
            && resource.uncreditedHaulMaterials.common == 0
            && resource.uncreditedHaulMaterials.rare == 0
            && resource.uncreditedHaulMaterials.exotic == 0,
        "timed Ship delivery should unload the manifest and consume its XP provenance");
    require(
        std::abs(
            expeditionExperienceEarnedSince(experienceBefore, state)
            - 10.0)
            < 0.000001,
        "Support Drone delivery should credit only loose or drone-mined Common and Exotic ore, not rig-transferred Rare ore");

    resource.haulMaterials.rare = 1;
    resource.uncreditedHaulMaterials.rare = 1;
    mining.droneX = mining.returnZoneX;
    mining.droneY = mining.returnZoneY;
    const ExpeditionExperienceSnapshot beforeSafeRecall =
        snapshotExpeditionExperience(state);
    const SurfaceActionOutcome departure = finishMiningRun(state, catalog, false);
    require(
        departure.applied,
        "the astronaut should be able to depart without recalling a missing Support Drone");
    require(
        std::abs(expeditionExperienceEarnedSince(beforeSafeRecall, state)) < 0.000001 &&
            departure.materialLost.rare == 1,
        "departure must not teleport or credit Support Drone ore that did not physically unload");
}

void supportDroneRecallIsPhysicalAndExplicit()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 254);
    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    state.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
    ensureDroneBayState(state, catalog);
    state.meta.droneBaySlots = 1;
    state.meta.equippedDroneIds = {content::drone::resourceDrone};
    prepareSurface(state, content::destination::mars);
    require(
        startMiningRun(
            state,
            catalog,
            {MiningAct::ActOne, 3, 0xD312ULL},
            false).applied,
        "Support Drone recall fixture should start");

    MiningRunState& mining = state.run.mining;
    mining.enemies.clear();
    mining.droneX = mining.returnZoneX;
    mining.droneY = mining.returnZoneY;
    require(mining.miniDrones.size() == 1,
        "Support Drone recall fixture should create one physical drone");
    MiningMiniDroneAgent& resource = mining.miniDrones.front();
    resource.haulMaterials.common = 2;
    resource.uncreditedHaulMaterials.common = 2;
    resource.behavior = MiningMiniDroneBehavior::Working;

    const MiningDroneRecoveryStatus before = miningDroneRecoveryStatus(mining);
    require(before.outstandingDrones == 1 && before.outstandingCargoMass == 2 &&
            !before.recallInProgress,
        "service should identify the exact outstanding drone and cargo before recall");
    require(requestMiningDroneRecall(state) &&
            resource.behavior == MiningMiniDroneBehavior::DeliveringToShip &&
            resource.haulMaterials.common == 2,
        "Wait for Drones should start physical transit without remotely crediting cargo");

    const MiningDroneRecoveryStatus returning = miningDroneRecoveryStatus(mining);
    require(returning.outstandingDrones == 1 && returning.recallInProgress &&
            mining.stowedMaterials.common == 0,
        "recalled cargo should remain owned by its drone until timed delivery completes");
    resource.actionCooldownSeconds = 0.0;
    updateMiningRun(state, catalog, 0.01);
    require(mining.stowedMaterials.common == 2 &&
            miningDroneRecoveryStatus(mining).outstandingDrones == 0 &&
            !requestMiningDroneRecall(state),
        "ship contact should unload once and clear the outstanding recall state");
}

void looseEvaOreAwardsXpWhenTheRigCollectsIt()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 253);
    prepareSurface(state, content::destination::mars);
    require(
        startMiningRun(
            state,
            catalog,
            {MiningAct::ActOne, 3, 0xE7AULL},
            false).applied,
        "loose EVA ore XP fixture should start");

    MiningRunState& mining = state.run.mining;
    mining.enemies.clear();
    mining.rigOxygen.current = 100.0;
    MiningLooseObject chunk;
    chunk.material = MiningCellMaterial::ExoticVein;
    chunk.x = mining.droneX;
    chunk.y = mining.droneY;
    chunk.cargoValue = tuning::mining::exoticCargo;
    mining.looseObjects.push_back(chunk);
    const ExpeditionExperienceSnapshot experienceBefore =
        snapshotExpeditionExperience(state);
    require(
        std::abs(expeditionExperienceEarnedSince(experienceBefore, state))
            < 0.000001,
        "loose EVA ore should not award XP before collection");

    updateMiningRun(state, catalog, 0.01);
    require(
        mining.temporaryMaterials.exotic == 1
            && mining.looseObjects.empty(),
        "the rig should take ownership of the loose EVA ore");
    require(
        std::abs(
            expeditionExperienceEarnedSince(experienceBefore, state)
            - 9.0)
            < 0.000001,
        "loose Exotic EVA ore should award nine XP at rig collection");
}

void firstClearCreditsOnlyExtractedMiningMaterials()
{
    const ContentCatalog catalog = createDefaultContent();
    const MiningArenaRequest request {MiningAct::ActOne, 9, 0xC1EAULL};
    const MiningArenaRules rules = resolveMiningArenaRules(request);

    GameState state = createNewGame(catalog, 303);
    prepareSurface(state, content::destination::mars);
    require(startMiningRun(state, catalog, request, true).applied, "first-clear arena should start");
    state.run.mining.stowedMaterials.rare = rules.rewardBudget.rareGuarantee;
    state.run.mining.stowedCargo = rules.rewardBudget.rareGuarantee * tuning::mining::rareCargo;
    state.run.mining.droneX = state.run.mining.returnZoneX;
    state.run.mining.droneY = state.run.mining.returnZoneY;
    require(finishMiningRun(state, catalog, false).applied, "banked mining payload should return to Surface Ops");

    const MiningFirstClearProgress& before = miningFirstClearProgress(state.meta, rules.request.act, rules.band);
    require(before.rareBanked == 0, "returning to Surface Ops should not yet credit first-clear rewards");

    GameState extracted = extractWithSuccessfulSeed(state);
    const MiningFirstClearProgress& after = miningFirstClearProgress(extracted.meta, rules.request.act, rules.band);
    require(after.rareBanked == rules.rewardBudget.rareGuarantee,
        "successful surface extraction should credit the mining materials actually recovered");

    prepareSurface(extracted, content::destination::mars);
    require(startMiningRun(extracted, catalog, request, true).applied, "repeat arena should start");
    require(extracted.run.mining.rewardBudget.rareCap == (rules.rewardBudget.rareCap + 1) / 2,
        "fulfilled bands should use the reduced repeat rare cap");

    GameState debug = createNewGame(catalog, 404);
    prepareSurface(debug, content::destination::mars);
    require(startMiningRun(debug, catalog, request, false).applied, "non-crediting debug arena should start");
    debug.run.mining.stowedMaterials.rare = rules.rewardBudget.rareGuarantee;
    debug.run.mining.stowedCargo = rules.rewardBudget.rareGuarantee * tuning::mining::rareCargo;
    debug.run.mining.droneX = debug.run.mining.returnZoneX;
    debug.run.mining.droneY = debug.run.mining.returnZoneY;
    require(finishMiningRun(debug, catalog, false).applied, "debug payload should finish normally");
    debug = extractWithSuccessfulSeed(debug);
    require(miningFirstClearProgress(debug.meta, rules.request.act, rules.band).rareBanked == 0,
        "debug arenas should never write campaign first-clear progression");
}

void rewardLedgerAndPendingCreditRoundTrip()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 505);
    prepareSurface(state, content::destination::nearbyStar);
    require(startMiningRun(state, catalog, {MiningAct::ActTwo, 9, 0x5A5EULL}, false).applied,
        "save ledger arena should start");
    state.run.mining.richRewardsAwarded.rare = 3;
    state.run.mining.richRewardsAwarded.exotic = 1;

    const std::string serialized = serializeSaveData(captureSaveData(state));
    const auto parsed = deserializeSaveData(serialized);
    require(parsed.has_value(), "ledger save should deserialize");
    GameState restored = createNewGame(catalog, 1);
    restoreSaveData(restored, catalog, *parsed);
    require(restored.run.mining.rewardBudget.rareCap == state.run.mining.rewardBudget.rareCap,
        "active arena reward cap should round trip");
    require(restored.run.mining.richRewardsAwarded.rare == 3 && restored.run.mining.richRewardsAwarded.exotic == 1,
        "active arena committed rich rewards should round trip");
    require(!restored.run.mining.progressionCreditEligible,
        "debug progression eligibility should round trip");
}

void oxygenCapacityHasAHardCeiling()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 606);
    const MiningDrillStats baseline = miningDrillStats(state, catalog);
    state.run.planetaryExpedition.runRigUpgradeRanks.push_back(
        {content::surfaceUpgrade::emergencyWinch, 3});
    const MiningDrillStats winch = miningDrillStats(state, catalog);
    require(
        std::abs(winch.oxygenSeconds - baseline.oxygenSeconds) < 0.000001 &&
            std::abs(winch.artifactTowEfficiency - 0.75) < 0.000001,
        "Artifact Winch ranks should reduce artifact towing burden without changing mining oxygen");

    state.meta.unlockKeys.push_back(content::unlock::droneBay);
    state.meta.unlockKeys.push_back(content::unlock::droneSupportSuite);
    state.meta.droneBaySlots = 6;
    state.meta.ownedDroneIds.assign(6, content::drone::resourceDrone);
    state.meta.equippedDroneIds.assign(6, content::drone::resourceDrone);
    state.run.planetaryExpedition.runDroneRanks.push_back(
        {content::drone::resourceDrone, 3});
    const MiningDrillStats stats = miningDrillStats(state, catalog);
    require(stats.oxygenSeconds <= tuning::mining::maximumOxygenSeconds,
        "combined upgrades should never exceed the 120-second mining oxygen ceiling");
}

void regolithNeverProducesCommonOre()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 707);
    prepareSurface(state, content::destination::mars);
    require(startMiningRun(
                state,
                catalog,
                {MiningAct::ActOne, 3, 0xD17AULL},
                false)
            .applied,
        "regolith reward test arena should start");

    MiningRunState& mining = state.run.mining;
    mining.cellsBroken = 3;
    mining.droneX = 32.0;
    mining.droneY = 4.0;
    setMiningMove(state, 1.0, 0.0);
    setMiningMove(state, 0.0, 0.0);
    MiningCell* dirt = miningCellAt(mining.terrain, 33, 4);
    require(dirt != nullptr, "regolith reward test cell should exist");
    *dirt = {
        MiningCellMaterial::Regolith,
        0.01,
        0.01,
        true,
        false
    };
    const MaterialInventory before = mining.temporaryMaterials;
    setMiningDrilling(state, true);
    for (int tick = 0;
         tick < 8 && dirt->material != MiningCellMaterial::Empty;
         ++tick) {
        updateMiningRun(state, catalog, 0.08);
    }
    require(dirt->material == MiningCellMaterial::Empty,
        "regolith reward test cell should be drilled");
    require(mining.temporaryMaterials.common == before.common
            && mining.temporaryMaterials.rare == before.rare
            && mining.temporaryMaterials.exotic == before.exotic,
        "breaking the old fourth-cell threshold should still award nothing from regolith");
    require(mining.pickupEvents.empty(),
        "regolith should not create a resource pickup event");
}

void supplyPocketsAreDeterministicAndPayImmediately()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 708);
    prepareSurface(state, content::destination::mars);
    state.run.planetaryExpedition.rigFuel = 1.0;
    state.run.planetaryExpedition.rigFuelCapacity = 4.0;
    require(startMiningRun(state, catalog, {MiningAct::ActOne, 4, 0xF00DULL}, false).applied,
        "Mars supply-pocket arena should start");

    MiningRunState& mining = state.run.mining;
    const auto countMaterial = [&](MiningCellMaterial material) {
        return static_cast<int>(std::count_if(
            mining.terrain.cells.begin(),
            mining.terrain.cells.end(),
            [&](const MiningCell& cell) { return cell.material == material; }));
    };
    require(countMaterial(MiningCellMaterial::FuelPocket) == 1
            && countMaterial(MiningCellMaterial::OxygenPocket) == 1,
        "each eligible Mars arena should stamp exactly one fuel and oxygen pocket");
    const auto parsed = deserializeSaveData(serializeSaveData(captureSaveData(state)));
    require(parsed.has_value(), "supply-pocket arena save should deserialize");
    GameState restored = createNewGame(catalog, 710);
    restoreSaveData(restored, catalog, *parsed);
    require(std::count_if(
                restored.run.mining.terrain.cells.begin(),
                restored.run.mining.terrain.cells.end(),
                [](const MiningCell& cell) { return cell.material == MiningCellMaterial::FuelPocket; }) == 1
            && std::count_if(
                restored.run.mining.terrain.cells.begin(),
                restored.run.mining.terrain.cells.end(),
                [](const MiningCell& cell) { return cell.material == MiningCellMaterial::OxygenPocket; }) == 1,
        "appended supply-pocket material IDs should round trip through active saves");

    mining.droneX = 32.0;
    mining.droneY = 4.0;
    setMiningMove(state, 1.0, 0.0);
    setMiningMove(state, 0.0, 0.0);
    auto drillTestCell = [&](MiningCellMaterial material) {
        MiningCell* cell = miningCellAt(mining.terrain, 33, 4);
        require(cell != nullptr, "supply-pocket test cell should exist");
        *cell = {material, 0.01, 0.01, true, false};
        setMiningDrilling(state, true);
        for (int tick = 0; tick < 8 && cell->material != MiningCellMaterial::Empty; ++tick) {
            updateMiningRun(state, catalog, 0.08);
        }
        require(cell->material == MiningCellMaterial::Empty, "supply pocket should be drillable");
    };

    const MaterialInventory materialsBefore = mining.temporaryMaterials;
    const double fuelBefore = mining.rigFuel.current;
    drillTestCell(MiningCellMaterial::FuelPocket);
    auto fuelCell = std::find_if(
        mining.looseObjects.begin(), mining.looseObjects.end(),
        [](const MiningLooseObject& object) {
            return object.active && object.kind == MiningLooseObjectKind::FuelCell;
        });
    require(fuelCell != mining.looseObjects.end()
            && mining.rigFuel.current <= fuelBefore
            && mining.rigFuel.current > fuelBefore - 0.05,
        "a drilled fuel pocket should spawn a physical cell while drilling consumes only its normal powered demand");
    const double fuelBeforeContact = mining.rigFuel.current;
    fuelCell->x = mining.droneX;
    fuelCell->y = mining.droneY;
    fuelCell->pickupDelaySeconds = 0.0;
    updateMiningRun(state, catalog, 0.01);
    require(mining.rigFuel.current > fuelBeforeContact,
        "fuel should transfer only after physical contact with the rig");

    mining.rigOxygen.current = 5.0;
    drillTestCell(MiningCellMaterial::OxygenPocket);
    require(mining.rigOxygen.current > 14.5 && mining.rigOxygen.current <= 15.0,
        "oxygen pocket should restore ten seconds immediately");
    require(mining.pickupEvents.back().kind == MiningPickupKind::Oxygen
            && mining.pickupEvents.back().amount == 1,
        "oxygen pocket should emit one typed pickup event");
    require(mining.temporaryMaterials.common == materialsBefore.common
            && mining.temporaryMaterials.rare == materialsBefore.rare
            && mining.temporaryMaterials.exotic == materialsBefore.exotic
            && mining.cargo == 0,
        "supply pockets should not add material inventory or cargo");

    GameState moon = createNewGame(catalog, 709);
    prepareSurface(moon, content::destination::moon);
    require(startMiningRun(moon, catalog, {MiningAct::ActOne, 3, 0xC0DEULL}, false).applied,
        "Moon control arena should start");
    require(std::none_of(moon.run.mining.terrain.cells.begin(), moon.run.mining.terrain.cells.end(), [](const MiningCell& cell) {
        return cell.material == MiningCellMaterial::FuelPocket || cell.material == MiningCellMaterial::OxygenPocket;
    }), "Moon should remain free of supply pockets");
}

void ioTerrainAndArtifactSealAreDeterministic()
{
    const ContentCatalog catalog = createDefaultContent();
    const MiningArenaRequest request {
        MiningAct::ActOne,
        7,
        0x10A510A5ULL
    };
    auto makeIoState = [&](std::uint64_t stateSeed) {
        GameState state = createNewGame(catalog, stateSeed);
        state.meta.unlockKeys.push_back(content::unlock::routeJupiter);
        prepareSurface(state, content::destination::jupiter);
        const ScenarioActionOutcome commission = performScenarioAction(
            state,
            catalog,
            content::scenario::volcanicDescent,
            "commission",
            ScenarioActionKind::BeginActivity);
        require(commission.applied,
            "the generic Io scenario fixture should commission Hazard support through its authored action");
        const ScenarioActionOutcome recovery = performScenarioAction(
            state,
            catalog,
            content::scenario::volcanicDescent,
            "recovery",
            ScenarioActionKind::BeginActivity);
        require(recovery.applied && recovery.beginsActivity &&
                recovery.miningSiteDefinitionId == content::miningSite::thermalLayeredRecovery,
            "the generic Io recovery step should select its authored thermal mining site");
        // The progression curve authors the first artifact on depth +1. This
        // fixture starts on that surveyed layer so the remaining assertions
        // can exercise the protected-objective adapter directly.
        state.run.planetaryExpedition.depth = 1;
        state.run.planetaryExpedition.prospectMaterials = {
            .common = 5,
            .rare = 2,
            .exotic = 1
        };
        require(startMiningRun(state, catalog, request, true).applied,
            "Io story arena should start");
        // Keep this focused adapter fixture's synthetic ship bay on its only
        // loaded layer; depth-transition behavior is covered separately.
        state.run.mining.entryDepthZone = state.run.mining.depthZone;
        require(state.run.mining.scenarioId == content::scenario::volcanicDescent &&
                state.run.mining.scenarioStepId == "recovery" &&
                state.run.mining.miningSiteDefinitionId ==
                    content::miningSite::thermalLayeredRecovery,
            "campaign mining should resolve the active authored recovery site even when no UI pending binding survives");
        return state;
    };

    GameState first = makeIoState(808);
    GameState second = makeIoState(909);
    MiningRunState& mining = first.run.mining;
    const MiningRunState& repeat = second.run.mining;
    require(mining.terrain.cells.size() == repeat.terrain.cells.size(),
        "matching Io arena requests should produce matching terrain sizes");
    require(mining.rigOxygen.current >= tuning::mining::ioArtifactOxygenSeconds,
        "the fixed Io artifact expedition should start with at least 60 seconds of oxygen");
    require(mining.artifact.present
            && mining.artifact.id == content::protectedObjective::ioMinorArtifact
            && mining.artifact.kind == ArtifactKind::Boost
            && mining.artifact.rewardType == ArtifactRewardType::None
            && mining.gate.protectedObjective.kind == ProtectedObjectiveKind::Artifact,
        "the generic protected-objective adapter should own Io payload visibility while the scenario owns its drone-credit reward");
    require(mining.gate.outerShellTilesTotal == 4
            && mining.gate.outerShellTilesRemaining == 4
            && mining.gate.innerShellTilesTotal == 0
            && mining.gate.innerShellTilesRemaining == 0,
        "the introductory Io artifact should begin behind one four-segment thermal seal");
    require(static_cast<int>(std::floor(mining.gate.anchorX)) ==
                mining.terrain.width / 2
            && static_cast<int>(std::floor(mining.gate.anchorY)) ==
                static_cast<int>(std::floor(mining.returnZoneY)) + 10,
        "the first Io artifact should stay centered ten cells below the entry pad");

    int thermalLava = 0;
    for (std::size_t index = 0;
         index < mining.terrain.cells.size();
         ++index) {
        const MiningCell& cell = mining.terrain.cells[index];
        const MiningCell& repeated = repeat.terrain.cells[index];
        require(cell.material == repeated.material
                && cell.hazardAffinity == repeated.hazardAffinity,
            "matching Io arena requests should reproduce material and affinity placement");
        require(cell.material != MiningCellMaterial::CommonOre
                && cell.material != MiningCellMaterial::RareOre
                && cell.material != MiningCellMaterial::ExoticVein
                && cell.material != MiningCellMaterial::FuelPocket
                && cell.material != MiningCellMaterial::OxygenPocket,
            "Io should contain no directly generated ore deposits");
        if (cell.material == MiningCellMaterial::HazardPocket) {
            ++thermalLava;
            require(cell.hazardAffinity == MiningElementalAffinity::Thermal,
                "every Io lava pocket should use the Thermal affinity");
        }
    }
    require(thermalLava > mining.gate.shellTilesTotal,
        "Io should contain deterministic mineable lava beyond the story seal");

    const int anchorX = static_cast<int>(std::floor(mining.gate.anchorX));
    const int anchorY = static_cast<int>(std::floor(mining.gate.anchorY));
    constexpr std::array<std::pair<int, int>, 4> thermalSealOffsets {{
        {0, -2}, {2, 0}, {0, 2}, {-2, 0}
    }};
    const ExpeditionExperienceSnapshot experienceBeforeGateOpen =
        snapshotExpeditionExperience(first);
    for (const auto& [dx, dy] : thermalSealOffsets) {
        MiningCell* seal = miningCellAt(
            mining.terrain,
            anchorX + dx,
            anchorY + dy);
        require(seal != nullptr, "thermal Io seal segment should exist");
        *seal = {};
    }
    mining.gate.derivedStateDirty = true;
    updateMiningRun(first, catalog, 0.01);
    require(
        mining.gate.state == MiningGateState::Open,
        "clearing the introductory thermal seal should authoritatively open the gate");
    require(
        std::abs(
            expeditionExperienceEarnedSince(experienceBeforeGateOpen, first)
            - 10.0)
            < 0.000001,
        "opening the protected objective gate should award ten XP");
    mining.gate.derivedStateDirty = true;
    updateMiningRun(first, catalog, 0.01);
    require(
        std::abs(
            expeditionExperienceEarnedSince(experienceBeforeGateOpen, first)
            - 10.0)
            < 0.000001,
        "refreshing an already-open gate should not duplicate objective XP");

    // Crossing the ship bay is an immediate ship-manifest handoff, even when
    // the towing rig is still a few cells away. The later surface return must
    // carry that same protected objective into the authored recovery step.
    GameState recovered = makeIoState(1010);
    MiningRunState& recoveredMining = recovered.run.mining;
    recoveredMining.gate.state = MiningGateState::Open;
    recoveredMining.artifact.revealed = true;
    recoveredMining.artifact.state = MiningArtifactState::Loose;
    recoveredMining.artifact.tethered = true;
    recoveredMining.artifact.x = recoveredMining.returnZoneX;
    recoveredMining.artifact.y = recoveredMining.returnZoneY;
    recoveredMining.droneX = recoveredMining.returnZoneX + 5.0;
    recoveredMining.droneY = recoveredMining.returnZoneY;
    updateMiningRun(recovered, catalog, 0.01);
    require(recoveredMining.stowedArtifacts.size() == 1 &&
            recoveredMining.temporaryArtifacts.empty(),
        "an artifact captured by the ship bay must enter the Ship manifest before the rig docks");
    recoveredMining.droneX = recoveredMining.returnZoneX;
    recoveredMining.droneY = recoveredMining.returnZoneY;
    require(finishMiningRun(recovered, catalog, false).applied,
        "the rig should be able to leave after its artifact is secured aboard the ship");
    require(recovered.run.planetaryExpedition.temporaryArtifacts.size() == 1,
        "stowing and leaving must retain the delivered artifact for the return flight");
    const ScenarioObjectivePresentation pendingRecovery = scenarioObjectiveForDestination(
        recovered,
        catalog,
        content::destination::jupiter);
    require(pendingRecovery.returnPending &&
            pendingRecovery.scenarioId == content::scenario::volcanicDescent &&
            pendingRecovery.stepId == "recovery" &&
            pendingRecovery.current == 0 &&
            pendingRecovery.required == 1 &&
            pendingRecovery.action == ScenarioActionKind::None,
        "a protected artifact aboard the ship should replace the next objective with a pending-return recovery state");
    GameState legacyPending = recovered;
    legacyPending.run.planetaryExpedition.temporaryArtifacts.front().rewardApplied = true;
    const ScenarioObjectivePresentation repairedLegacyPending = scenarioObjectiveForDestination(
        legacyPending,
        catalog,
        content::destination::jupiter);
    require(repairedLegacyPending.returnPending &&
            repairedLegacyPending.action == ScenarioActionKind::None,
        "a previously ship-marked artifact must remain pending until its Earth return clears the manifest");
    const SurfaceActionOutcome recoveredPayload = extractSurfacePayload(recovered, catalog);
    const ScenarioStepState recoveryState = scenarioStepState(
        recovered,
        catalog,
        content::scenario::volcanicDescent,
        "recovery");
    require(recoveredPayload.artifactFound &&
            (recoveryState == ScenarioStepState::ReadyToClaim ||
             recoveryState == ScenarioStepState::Complete),
        "returning the ship-secured Io artifact must advance the recovery mission");
}

void proceduralArtifactGatesStayCenteredTenCellsBelowEntry()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 1213);
    state.meta.unlockKeys.push_back(content::unlock::routeJupiter);
    prepareSurface(state, content::destination::jupiter);
    const MiningArenaRequest request {
        MiningAct::ActOne,
        8,
        0xCE117EULL,
        true,
        MiningGateType::HazardCocoon
    };
    require(startMiningRun(state, catalog, request, false).applied,
        "procedural Jupiter artifact arena should start");

    MiningRunState& mining = state.run.mining;
    require(mining.gate.active && mining.artifact.present,
        "procedural Jupiter artifact arena should create an artifact gate");
    require(static_cast<int>(std::floor(mining.gate.anchorX)) == mining.terrain.width / 2 &&
            static_cast<int>(std::floor(mining.gate.anchorY)) ==
                static_cast<int>(std::floor(mining.returnZoneY)) + 10 &&
            static_cast<int>(std::floor(mining.artifact.x)) == mining.terrain.width / 2 &&
            static_cast<int>(std::floor(mining.artifact.y)) ==
                static_cast<int>(std::floor(mining.returnZoneY)) + 10,
        "new procedural artifact gates should stay ten cells below the entry pad, independent of rig staging");

}

void ioLavaAlwaysCoolsIntoMineableCommonOre()
{
    const ContentCatalog catalog = createDefaultContent();
    GameState state = createNewGame(catalog, 1001);
    state.meta.unlockKeys = {content::unlock::routeJupiter};
    prepareSurface(state, content::destination::jupiter);
    require(
        performScenarioAction(
            state,
            catalog,
            content::scenario::volcanicDescent,
            "commission",
            ScenarioActionKind::BeginActivity).applied,
        "the thermal treatment fixture should commission its Hazard frame through the generic scenario");
    const ScenarioActionOutcome recovery = performScenarioAction(
        state,
        catalog,
        content::scenario::volcanicDescent,
        "recovery",
        ScenarioActionKind::BeginActivity);
    require(recovery.applied && recovery.beginsActivity,
        "the thermal treatment fixture should start the authored recovery activity");
    state.run.planetaryExpedition.pendingScenarioId = content::scenario::volcanicDescent;
    state.run.planetaryExpedition.pendingScenarioStepId = "recovery";
    state.run.planetaryExpedition.pendingMiningSiteDefinitionId = recovery.miningSiteDefinitionId;
    state.meta.droneBaySlots = 1;
    state.meta.equippedDroneIds = {content::drone::hazardDrone};
    require(startMiningRun(
                state,
                catalog,
                {MiningAct::ActOne, 7, 0x1A7AULL},
                true)
            .applied,
        "Io lava treatment arena should start");

    MiningRunState& mining = state.run.mining;
    const int targetX = std::clamp(
        static_cast<int>(std::floor(mining.droneX)) + 1,
        1,
        mining.terrain.width - 2);
    const int targetY = std::clamp(
        static_cast<int>(std::floor(mining.droneY)) + 2,
        1,
        mining.terrain.height - 2);
    for (int y = 0; y < mining.terrain.height; ++y) {
        for (int x = 0; x < mining.terrain.width; ++x) {
            MiningCell* cell = miningCellAt(mining.terrain, x, y);
            if (cell != nullptr && cell->material == MiningCellMaterial::HazardPocket) {
                *cell = {
                    MiningCellMaterial::Regolith,
                    1.0,
                    1.0,
                    true,
                    false
                };
            }
        }
    }
    MiningCell* lava = miningCellAt(mining.terrain, targetX, targetY);
    require(lava != nullptr, "Io lava treatment test cell should exist");
    mining.gate = {};
    *lava = {
        MiningCellMaterial::HazardPocket,
        1.0,
        1.0,
        true,
        true
    };
    lava->hazardAffinity = MiningElementalAffinity::Thermal;
    const MaterialInventory before = mining.temporaryMaterials;
    const ExpeditionExperienceSnapshot experienceBefore =
        snapshotExpeditionExperience(state);
    for (int tick = 0;
         tick < 300 && lava->material == MiningCellMaterial::HazardPocket;
         ++tick) {
        updateMiningRun(state, catalog, 0.05);
    }
    require(lava->material == MiningCellMaterial::CommonOre
            && lava->revealed
            && !lava->hazard,
        "Io Thermal lava should deterministically cool into a mineable gray Common Ore cell");
    require(mining.temporaryMaterials.common == before.common,
        "Hazard treatment should create ore for drilling rather than award it immediately");
    require(
        std::abs(
            expeditionExperienceEarnedSince(experienceBefore, state)
            - 1.0)
            < 0.000001,
        "successful Thermal conversion should award one XP exactly once");
}

} // namespace

int main()
{
    try {
        miningExperienceAwardsMatchApprovedEconomy();
        explicitArenaIsDeterministicAndBudgeted();
        richPayoutsShareOneLedger();
        lunarContractActivatesScannerLedEvaArtifactInSameRun();
        rigLoadBandsAndHardCapacityAreImmediate();
        supportDroneXpWaitsForAuthoritativeShipDelivery();
        supportDroneRecallIsPhysicalAndExplicit();
        looseEvaOreAwardsXpWhenTheRigCollectsIt();
        firstClearCreditsOnlyExtractedMiningMaterials();
        rewardLedgerAndPendingCreditRoundTrip();
        oxygenCapacityHasAHardCeiling();
        regolithNeverProducesCommonOre();
        supplyPocketsAreDeterministicAndPayImmediately();
        proceduralArtifactGatesStayCenteredTenCellsBelowEntry();
        ioTerrainAndArtifactSealAreDeterministic();
        ioLavaAlwaysCoolsIntoMineableCommonOre();
        std::cout << "Mining economy tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Mining economy test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
