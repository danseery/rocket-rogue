#include "core/Content.h"
#include "core/ContentIds.h"
#include "core/GameState.h"
#include "core/MiningProgression.h"
#include "core/MiningSystem.h"
#include "core/ResearchSystem.h"
#include "core/SaveData.h"
#include "core/Tuning.h"

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
        const SurfaceActionOutcome outcome = extractSurfacePayload(candidate, rng);
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

} // namespace

int main()
{
    try {
        explicitArenaIsDeterministicAndBudgeted();
        richPayoutsShareOneLedger();
        firstClearCreditsOnlyExtractedMiningMaterials();
        rewardLedgerAndPendingCreditRoundTrip();
        oxygenCapacityHasAHardCeiling();
        std::cout << "Mining economy tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Mining economy test failure: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
