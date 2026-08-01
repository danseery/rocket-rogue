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
        throw std::runtime_error(message);
    }
}

void prepareSurface(GameState& state, std::string destinationId)
{
    state.run.surfaceExpedition = {};
    state.run.surfaceExpedition.active = true;
    state.run.surfaceExpedition.destinationId = std::move(destinationId);
    state.run.surfaceExpedition.sharedFuel = 4;
    state.run.surfaceExpedition.sharedFuelCapacity = 4;
    state.run.surfaceExpedition.miningSitePrepared = true;
    state.screen = Screen::SurfaceExpedition;
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
    const int rareCollected = mining.temporaryMaterials.rare + mining.stowedMaterials.rare;
    const int commonCollected = mining.temporaryMaterials.common + mining.stowedMaterials.common;
    require(rareCollected == 1 && mining.richRewardsAwarded.rare == 1,
        "all mining reward paths should stop rare payouts at the shared cap");
    require(commonCollected >= 1, "rich payout overflow should retain common salvage value");
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
    state.run.surfaceUpgradeIds.assign(64, content::surfaceUpgrade::emergencyWinch);
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
    state.run.surfaceExpedition.sharedFuel = 1;
    state.run.surfaceExpedition.sharedFuelCapacity = 4;
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
    const int fuelBefore = state.run.surfaceExpedition.sharedFuel;
    drillTestCell(MiningCellMaterial::FuelPocket);
    require(state.run.surfaceExpedition.sharedFuel == fuelBefore + 1,
        "fuel pocket should restore one shared fuel immediately");
    require(!mining.pickupEvents.empty()
            && mining.pickupEvents.back().kind == MiningPickupKind::Fuel
            && mining.pickupEvents.back().amount == 1,
        "fuel pocket should emit one typed pickup event");

    mining.oxygenSeconds = 5.0;
    drillTestCell(MiningCellMaterial::OxygenPocket);
    require(mining.oxygenSeconds > 14.5 && mining.oxygenSeconds <= 15.0,
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
        state.run.surfaceExpedition.pendingScenarioId = content::scenario::volcanicDescent;
        state.run.surfaceExpedition.pendingScenarioStepId = "recovery";
        state.run.surfaceExpedition.pendingMiningSiteDefinitionId = recovery.miningSiteDefinitionId;
        state.run.surfaceExpedition.prospectMaterials = {
            .common = 5,
            .rare = 2,
            .exotic = 1
        };
        require(startMiningRun(state, catalog, request, true).applied,
            "Io story arena should start");
        return state;
    };

    GameState first = makeIoState(808);
    GameState second = makeIoState(909);
    MiningRunState& mining = first.run.mining;
    const MiningRunState& repeat = second.run.mining;
    require(mining.terrain.cells.size() == repeat.terrain.cells.size(),
        "matching Io arena requests should produce matching terrain sizes");
    require(mining.oxygenSeconds >= tuning::mining::ioArtifactOxygenSeconds,
        "the fixed Io artifact expedition should start with at least 60 seconds of oxygen");
    require(mining.artifact.present
            && mining.artifact.id == content::protectedObjective::ioMinorArtifact
            && mining.artifact.kind == ArtifactKind::Boost
            && mining.artifact.rewardType == ArtifactRewardType::None
            && mining.gate.protectedObjective.kind == ProtectedObjectiveKind::Artifact,
        "the generic protected-objective adapter should own Io payload visibility while the scenario owns its drone-credit reward");
    require(mining.gate.outerShellTilesTotal == 4
            && mining.gate.outerShellTilesRemaining == 4
            && mining.gate.innerShellTilesTotal == 4
            && mining.gate.innerShellTilesRemaining == 4,
        "the Io artifact should begin behind staged four-segment outer and inner seals");

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
    constexpr std::array<std::pair<int, int>, 4> innerOffsets {{
        {-1, -1}, {1, -1}, {1, 1}, {-1, 1}
    }};
    for (const auto& [dx, dy] : innerOffsets) {
        const MiningCell* inner = miningCellAt(
            mining.terrain,
            anchorX + dx,
            anchorY + dy);
        require(inner != nullptr && !inner->revealed,
            "the inner Io seal should remain hidden while the outer seal stands");
    }

    constexpr std::array<std::pair<int, int>, 4> outerOffsets {{
        {0, -2}, {2, 0}, {0, 2}, {-2, 0}
    }};
    for (const auto& [dx, dy] : outerOffsets) {
        MiningCell* outer = miningCellAt(
            mining.terrain,
            anchorX + dx,
            anchorY + dy);
        require(outer != nullptr, "outer Io seal segment should exist");
        *outer = {};
    }
    mining.gate.derivedStateDirty = true;
    updateMiningRun(first, catalog, 0.01);
    require(mining.gate.outerShellTilesRemaining == 0
            && mining.gate.innerShellTilesRemaining == 4,
        "clearing the outer seal should expose but not complete the inner seal");
    for (const auto& [dx, dy] : innerOffsets) {
        const MiningCell* inner = miningCellAt(
            mining.terrain,
            anchorX + dx,
            anchorY + dy);
        require(inner != nullptr && inner->revealed,
            "all four inner Io seal segments should reveal together");
    }
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
    state.run.surfaceExpedition.pendingScenarioId = content::scenario::volcanicDescent;
    state.run.surfaceExpedition.pendingScenarioStepId = "recovery";
    state.run.surfaceExpedition.pendingMiningSiteDefinitionId = recovery.miningSiteDefinitionId;
    state.meta.droneBaySlots = 1;
    state.meta.equippedDroneIds = {content::drone::hazardDrone};
    state.meta.droneUpgrades = {{content::drone::hazardDrone, 1}};
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
    *lava = {
        MiningCellMaterial::HazardPocket,
        1.0,
        1.0,
        true,
        true
    };
    lava->hazardAffinity = MiningElementalAffinity::Thermal;
    const MaterialInventory before = mining.temporaryMaterials;
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
}

} // namespace

int main()
{
    try {
        explicitArenaIsDeterministicAndBudgeted();
        richPayoutsShareOneLedger();
        firstClearCreditsOnlyExtractedMiningMaterials();
        rewardLedgerAndPendingCreditRoundTrip();
        oxygenCapacityHasAHardCeiling();
        regolithNeverProducesCommonOre();
        supplyPocketsAreDeterministicAndPayImmediately();
        ioTerrainAndArtifactSealAreDeterministic();
        ioLavaAlwaysCoolsIntoMineableCommonOre();
        std::cout << "Mining economy tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Mining economy test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
