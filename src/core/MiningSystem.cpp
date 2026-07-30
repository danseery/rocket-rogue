#include "core/MiningSystem.h"
#include "core/ContentIds.h"
#include "core/GameFormat.h"
#include "core/GameText.h"
#include "core/MiniDroneCoordination.h"
#include "core/MiningProgression.h"
#include "core/Tuning.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace rocket {

MiningTerrain generateMiningTerrainForRules(
    const GameState& state,
    const Destination& destination,
    SurfaceSiteProfile profile,
    int depthZone,
    int width,
    int height,
    const MiningArenaRules& rules,
    MiningSiteBiome biome = MiningSiteBiome::Default);

void applyMiningTerrainToughnessScale(MiningTerrain& terrain, double scale);
void normalizeRichTerrainDeposits(
    MiningTerrain& terrain,
    const MiningArenaRules& rules,
    const MiningRewardBudget& budget,
    int reservedRareGuarantees,
    int reservedExoticGuarantees);

namespace {

constexpr double kPi = 3.14159265358979323846;

void markMiningGateDerivedStateDirty(MiningRunState& mining)
{
    mining.gate.derivedStateDirty = true;
}

std::uint64_t hashCombine(std::uint64_t value, std::uint64_t mix)
{
    value ^= mix + 0x9E3779B97F4A7C15ULL + (value << 6U) + (value >> 2U);
    value ^= value >> 30U;
    value *= 0xBF58476D1CE4E5B9ULL;
    value ^= value >> 27U;
    value *= 0x94D049BB133111EBULL;
    value ^= value >> 31U;
    return value;
}

std::uint64_t hashString(std::string_view text)
{
    std::uint64_t value = 1469598103934665603ULL;
    for (const char c : text) {
        value ^= static_cast<unsigned char>(c);
        value *= 1099511628211ULL;
    }
    return value;
}

double unitHash(std::uint64_t seed, int x, int y, int depthZone, std::uint64_t lane)
{
    std::uint64_t value = seed;
    value = hashCombine(value, static_cast<std::uint64_t>(x + 1009));
    value = hashCombine(value, static_cast<std::uint64_t>(y + 2003));
    value = hashCombine(value, static_cast<std::uint64_t>(depthZone + 4099));
    value = hashCombine(value, lane);
    return static_cast<double>(value & 0xFFFFFFULL) / static_cast<double>(0x1000000ULL);
}

double miningShipStartX(const MiningRunState& mining)
{
    const double leftClearance = tuning::mining::returnZoneRadiusCells + 0.5;
    const double rightClearance = std::max(leftClearance, static_cast<double>(mining.terrain.width) - leftClearance);
    return std::clamp(
        static_cast<double>(mining.terrain.width) * tuning::mining::returnZoneHorizontalFraction,
        leftClearance,
        rightClearance);
}

int miningReturnShaftLeftX(const MiningTerrain& terrain)
{
    return std::clamp(terrain.width / 2 - 1, 1, std::max(1, terrain.width - 3));
}

bool miningReturnShaftContains(const MiningTerrain& terrain, int x, int y)
{
    const int leftX = miningReturnShaftLeftX(terrain);
    return y >= 0 && y < terrain.height - 1 && x >= leftX && x <= leftX + 1;
}

bool operatorControlled(const MiningRunState& mining)
{
    return mining.operatorMode == MiningOperatorMode::Jetpack && mining.operatorPresent;
}

double controlledActorX(const MiningRunState& mining)
{
    return operatorControlled(mining) ? mining.operatorX : mining.droneX;
}

double controlledActorY(const MiningRunState& mining)
{
    return operatorControlled(mining) ? mining.operatorY : mining.droneY;
}

double controlledActorVelocityX(const MiningRunState& mining)
{
    return operatorControlled(mining) ? mining.operatorVelocityX : mining.rigVelocityX;
}

double controlledActorVelocityY(const MiningRunState& mining)
{
    return operatorControlled(mining) ? mining.operatorVelocityY : mining.rigVelocityY;
}

double controlledAimX(const MiningRunState& mining)
{
    return operatorControlled(mining) ? mining.operatorAimDirX : mining.aimDirX;
}

double controlledAimY(const MiningRunState& mining)
{
    return operatorControlled(mining) ? mining.operatorAimDirY : mining.aimDirY;
}

double controlledDrillRange(const MiningRunState& mining)
{
    return operatorControlled(mining)
        ? tuning::mining::operatorDrillRangeCells
        : tuning::mining::drillRangeCells;
}

bool controlledActorAtReturnZone(const MiningRunState& mining)
{
    if (!mining.active || mining.depthZone != mining.entryDepthZone) {
        return false;
    }
    const double dx = controlledActorX(mining) - mining.returnZoneX;
    const double dy = controlledActorY(mining) - mining.returnZoneY;
    return dx * dx + dy * dy <=
        tuning::mining::returnZoneRadiusCells * tuning::mining::returnZoneRadiusCells;
}

bool rigAtReturnZone(const MiningRunState& mining)
{
    if (!mining.active || mining.rigDepthZone != mining.entryDepthZone) {
        return false;
    }
    const double dx = mining.droneX - mining.returnZoneX;
    const double dy = mining.droneY - mining.returnZoneY;
    return dx * dx + dy * dy <=
        tuning::mining::returnZoneRadiusCells * tuning::mining::returnZoneRadiusCells;
}

bool operatorAtReturnZone(const MiningRunState& mining)
{
    if (!mining.active || !mining.operatorPresent ||
        mining.depthZone != mining.entryDepthZone) {
        return false;
    }
    const double dx = mining.operatorX - mining.returnZoneX;
    const double dy = mining.operatorY - mining.returnZoneY;
    return dx * dx + dy * dy <=
        tuning::mining::returnZoneRadiusCells * tuning::mining::returnZoneRadiusCells;
}

bool miningExtractionReady(const MiningRunState& mining)
{
    if (mining.rigDisabled) {
        return operatorAtReturnZone(mining);
    }
    if (operatorControlled(mining)) {
        return rigAtReturnZone(mining) && operatorAtReturnZone(mining);
    }
    return rigAtReturnZone(mining);
}

void applyControlledActorDamage(MiningRunState& mining, double damage)
{
    const double clampedDamage = std::max(0.0, damage);
    if (operatorControlled(mining)) {
        mining.operatorIntegrity =
            std::max(0.0, mining.operatorIntegrity - clampedDamage);
    } else {
        mining.droneHealth =
            std::max(0.0, mining.droneHealth - clampedDamage);
    }
}

bool hasUnlockKey(const MetaProgress& meta, std::string_view key)
{
    return hasUnlock(meta, key);
}

bool traitIs(const GameState& state, std::string_view trait)
{
    const Astronaut* astronaut = activeAstronaut(state);
    return astronaut != nullptr && astronaut->trait == trait;
}

int activeTraining(const GameState& state)
{
    const Astronaut* astronaut = activeAstronaut(state);
    return astronaut == nullptr ? 0 : effectiveTrainingLevel(*astronaut);
}

int materialCargo(const MaterialInventory& materials)
{
    return std::max(0, materials.common) * tuning::mining::commonCargo
        + std::max(0, materials.rare) * tuning::mining::rareCargo
        + std::max(0, materials.exotic) * tuning::mining::exoticCargo;
}

void addMiningMaterials(MaterialInventory& owned, const MaterialInventory& delta)
{
    owned.common += delta.common;
    owned.rare += delta.rare;
    owned.exotic += delta.exotic;
}

void recordMiningPickup(
    MiningRunState& mining,
    MiningPickupKind kind,
    int amount,
    double x,
    double y)
{
    if (amount <= 0) {
        return;
    }
    constexpr std::size_t maxEvents = 64;
    mining.pickupEvents.push_back({++mining.pickupEventSequence, kind, amount, x, y});
    if (mining.pickupEvents.size() > maxEvents) {
        mining.pickupEvents.erase(
            mining.pickupEvents.begin(),
            mining.pickupEvents.begin() + static_cast<std::ptrdiff_t>(mining.pickupEvents.size() - maxEvents));
    }
}

void recordMiningMaterialPickup(MiningRunState& mining, const MaterialInventory& gain, double x, double y)
{
    recordMiningPickup(mining, MiningPickupKind::CommonOre, gain.common, x, y);
    recordMiningPickup(mining, MiningPickupKind::RareOre, gain.rare, x, y);
    recordMiningPickup(mining, MiningPickupKind::ExoticOre, gain.exotic, x, y);
}

MiningArenaRequest miningArenaRequest(const MiningArenaMetadata& metadata)
{
    return {
        metadata.act,
        std::clamp(metadata.difficulty, 1, 10),
        metadata.seed,
        metadata.gateOverrideEnabled,
        metadata.gateType
    };
}

MiningArenaRules activeMiningArenaRules(const MiningRunState& mining)
{
    if (!mining.miningSiteDefinitionId.empty() ||
        mining.miningSiteBiome != MiningSiteBiome::Default) {
        MiningSiteDefinition runtimeSite;
        runtimeSite.arena = miningArenaRequest(mining.arenaMetadata);
        runtimeSite.biome = mining.miningSiteBiome;
        runtimeSite.gateType = mining.arenaMetadata.gateType;
        return resolveMiningSiteArenaRules(runtimeSite.arena, runtimeSite);
    }
    return resolveMiningArenaRules(miningArenaRequest(mining.arenaMetadata));
}

bool hasLayeredCocoon(const MiningRunState& mining)
{
    return mining.gate.active && !mining.gate.cocoonLayers.empty();
}

bool validCocoonLayer(const MiningRunState& mining, int layer)
{
    return layer >= 0 &&
        layer < static_cast<int>(mining.gate.cocoonLayers.size());
}

bool cocoonLayerRevealed(const MiningRunState& mining, int layer)
{
    return validCocoonLayer(mining, layer) &&
        mining.gate.cocoonLayers[static_cast<std::size_t>(layer)].revealed;
}

bool cocoonLayerCompleted(const MiningRunState& mining, int layer)
{
    return validCocoonLayer(mining, layer) &&
        mining.gate.cocoonLayers[static_cast<std::size_t>(layer)].completed;
}

bool cocoonLayerPrerequisitesComplete(const MiningRunState& mining, int layer)
{
    if (!validCocoonLayer(mining, layer)) {
        return false;
    }
    for (int previous = 0; previous < layer; ++previous) {
        if (!cocoonLayerCompleted(mining, previous)) {
            return false;
        }
    }
    return true;
}

int earliestIncompleteCocoonLayer(const MiningRunState& mining)
{
    for (int layer = 0; layer < static_cast<int>(mining.gate.cocoonLayers.size()); ++layer) {
        if (!mining.gate.cocoonLayers[static_cast<std::size_t>(layer)].completed) {
            return layer;
        }
    }
    return -1;
}

bool cocoonComplete(const MiningRunState& mining)
{
    return hasLayeredCocoon(mining) && earliestIncompleteCocoonLayer(mining) < 0;
}

bool hiddenProtectedObjectiveAt(const MiningRunState& mining, int x, int y)
{
    if (!hasLayeredCocoon(mining) || cocoonComplete(mining) ||
        mining.gate.protectedObjective.kind != ProtectedObjectiveKind::Artifact ||
        !mining.artifact.present ||
        mining.artifact.state != MiningArtifactState::Embedded) {
        return false;
    }
    return static_cast<int>(std::floor(mining.artifact.x)) == x &&
        static_cast<int>(std::floor(mining.artifact.y)) == y;
}

bool cocoonCellVisible(const MiningRunState& mining, const MiningCell& cell)
{
    return cell.cocoonLayer < 0 || cocoonLayerRevealed(mining, cell.cocoonLayer);
}

int cocoonRequiredHazardMark(const MiningRunState& mining, const MiningCell& cell)
{
    const int affinityMark = tuning::mining::hazardDroneRequiredMark(cell.hazardAffinity);
    if (!validCocoonLayer(mining, cell.cocoonLayer)) {
        return affinityMark;
    }
    return std::max(
        affinityMark,
        mining.gate.cocoonLayers[static_cast<std::size_t>(cell.cocoonLayer)].requiredHazardMark);
}

std::uint64_t activeMiningArenaSeed(const MiningRunState& mining)
{
    return mining.arenaMetadata.seed;
}

MaterialInventory claimMiningRichRewardBudget(MiningRunState& mining, MaterialInventory requested)
{
    requested.common = std::max(0, requested.common);
    requested.rare = std::max(0, requested.rare);
    requested.exotic = std::max(0, requested.exotic);

    const int rareRemaining = std::max(0, mining.rewardBudget.rareCap - mining.richRewardsAwarded.rare);
    const int exoticRemaining = std::max(0, mining.rewardBudget.exoticCap - mining.richRewardsAwarded.exotic);
    const int rareGranted = std::min(requested.rare, rareRemaining);
    const int exoticGranted = std::min(requested.exotic, exoticRemaining);
    const int overflow = (requested.rare - rareGranted) + (requested.exotic - exoticGranted);

    requested.common += overflow;
    requested.rare = rareGranted;
    requested.exotic = exoticGranted;
    mining.richRewardsAwarded.rare += rareGranted;
    mining.richRewardsAwarded.exotic += exoticGranted;
    return requested;
}

void appendSurfaceLog(SurfaceExpeditionState& expedition, std::string entry)
{
    if (entry.empty()) {
        return;
    }
    expedition.logEntries.push_back(std::move(entry));
    const int overflow = static_cast<int>(expedition.logEntries.size()) - tuning::research::surfaceLogEntryLimit;
    if (overflow > 0) {
        expedition.logEntries.erase(expedition.logEntries.begin(), expedition.logEntries.begin() + overflow);
    }
}

int chunkIndexForCell(const MiningTerrain& terrain, int x, int y)
{
    const int chunksX = std::max(1, (terrain.width + tuning::mining::chunkSize - 1) / tuning::mining::chunkSize);
    const int chunkX = std::clamp(x / tuning::mining::chunkSize, 0, chunksX - 1);
    const int chunkY = std::clamp(y / tuning::mining::chunkSize, 0, std::max(1, (terrain.height + tuning::mining::chunkSize - 1) / tuning::mining::chunkSize) - 1);
    return chunkY * chunksX + chunkX;
}

void markDirty(MiningTerrain& terrain, int x, int y)
{
    const int index = chunkIndexForCell(terrain, x, y);
    if (index >= 0 && index < static_cast<int>(terrain.dirtyChunks.size())) {
        terrain.dirtyChunks[static_cast<std::size_t>(index)] = 1;
    }
}

void revealCocoonLayer(MiningRunState& mining, int layer)
{
    if (!validCocoonLayer(mining, layer)) {
        return;
    }
    MiningCocoonLayerProgress& progress =
        mining.gate.cocoonLayers[static_cast<std::size_t>(layer)];
    bool changed = !progress.revealed;
    progress.revealed = true;
    for (int y = 0; y < mining.terrain.height; ++y) {
        for (int x = 0; x < mining.terrain.width; ++x) {
            MiningCell* cell = miningCellAt(mining.terrain, x, y);
            if (cell == nullptr || cell->cocoonLayer != layer || cell->revealed) {
                continue;
            }
            cell->revealed = true;
            markDirty(mining.terrain, x, y);
            changed = true;
        }
    }
    if (changed) {
        markMiningGateDerivedStateDirty(mining);
    }
}

void revealProtectedObjective(MiningRunState& mining)
{
    if (mining.gate.protectedObjective.kind != ProtectedObjectiveKind::Artifact ||
        !mining.artifact.present ||
        mining.artifact.state == MiningArtifactState::Delivered ||
        mining.artifact.state == MiningArtifactState::Destroyed) {
        return;
    }

    mining.artifact.revealed = true;
    const int artifactX = std::clamp(
        static_cast<int>(std::floor(mining.artifact.x)),
        0,
        std::max(0, mining.terrain.width - 1));
    const int artifactY = std::clamp(
        static_cast<int>(std::floor(mining.artifact.y)),
        0,
        std::max(0, mining.terrain.height - 1));
    if (MiningCell* artifactCell = miningCellAt(mining.terrain, artifactX, artifactY)) {
        artifactCell->revealed = true;
        markDirty(mining.terrain, artifactX, artifactY);
    }
}

void maybeRevealCocoonLayerAt(MiningRunState& mining, const MiningCell& cell)
{
    if (!hasLayeredCocoon(mining) || cell.cocoonLayer < 0 ||
        cell.cocoonLayer != mining.gate.activeCocoonLayer ||
        cocoonLayerRevealed(mining, cell.cocoonLayer)) {
        return;
    }
    const MiningCocoonLayerProgress& progress = mining.gate.cocoonLayers[
        static_cast<std::size_t>(cell.cocoonLayer)];
    if (progress.revealPolicy != MiningCocoonRevealPolicy::OnAnyCellDiscovered ||
        !cocoonLayerPrerequisitesComplete(mining, cell.cocoonLayer)) {
        return;
    }
    // The active layer's discovery trigger is intentionally evaluated here,
    // where every player-visible reveal source converges: line of sight,
    // scanner pulses, and Survey Drone pulses. The full layer appears at
    // once, while future layers remain hidden.
    revealCocoonLayer(mining, cell.cocoonLayer);
}

MiningCell makeCell(
    MiningCellMaterial material,
    int depthZone,
    MiningElementalAffinity hazardAffinity = MiningElementalAffinity::None)
{
    const double toughness = miningMaterialToughness(material, depthZone);
    MiningCell cell {
        material,
        toughness,
        toughness,
        material == MiningCellMaterial::Empty,
        material == MiningCellMaterial::HazardPocket
    };
    cell.hazardAffinity = material == MiningCellMaterial::HazardPocket
        ? hazardAffinity
        : MiningElementalAffinity::None;
    return cell;
}

int featurePriority(MiningCellFeature feature)
{
    switch (feature) {
    case MiningCellFeature::None:
        return 0;
    case MiningCellFeature::MainTunnel:
        return 1;
    case MiningCellFeature::BranchTunnel:
        return 2;
    case MiningCellFeature::EncounterZone:
        return 3;
    case MiningCellFeature::TreasureVault:
        return 4;
    case MiningCellFeature::HiveNest:
        return 5;
    case MiningCellFeature::MinibossLair:
        return 6;
    case MiningCellFeature::OrganicBurrow:
        return 7;
    case MiningCellFeature::BossChamber:
        return 8;
    }
    return 0;
}

MiningEnemyType hostileEnemyTypeForLane(const MiningArenaRules& rules, int lane)
{
    std::array<MiningEnemyType, 5> candidates {};
    std::size_t count = 0;
    for (const MiningEnemyType type : {
             MiningEnemyType::Ant,
             MiningEnemyType::Flying,
             MiningEnemyType::Beetle,
             MiningEnemyType::Elemental,
             MiningEnemyType::Mammal}) {
        if (miningEnemyAllowed(rules, type)) {
            candidates[count++] = type;
        }
    }
    if (count == 0) {
        return MiningEnemyType::None;
    }
    return candidates[static_cast<std::size_t>(std::max(0, lane)) % count];
}

MiningElementalAffinity elementalAffinityForLane(const MiningArenaRules& rules, SurfaceSiteProfile profile, int lane)
{
    std::array<MiningElementalAffinity, 4> candidates {};
    std::size_t count = 0;
    for (const MiningElementalAffinity affinity : {
             MiningElementalAffinity::Thermal,
             MiningElementalAffinity::Cryo,
             MiningElementalAffinity::Toxic,
             MiningElementalAffinity::Radiation}) {
        if (miningAffinityAllowed(rules, affinity)) {
            candidates[count++] = affinity;
        }
    }
    if (count == 0) {
        return MiningElementalAffinity::None;
    }
    const int offset = rules.request.difficulty + static_cast<int>(profile);
    return candidates[static_cast<std::size_t>(std::max(0, lane + offset)) % count];
}

std::vector<MiningCellFeature> allowedSpecialRooms(const MiningArenaRules& rules)
{
    std::vector<MiningCellFeature> result;
    for (const MiningCellFeature feature : {
             MiningCellFeature::TreasureVault,
             MiningCellFeature::HiveNest,
             MiningCellFeature::MinibossLair,
             MiningCellFeature::BossChamber}) {
        if (miningRoomFeatureAllowed(rules, feature)) {
            result.push_back(feature);
        }
    }
    return result;
}

void stampMiningCell(
    MiningTerrain& terrain,
    int x,
    int y,
    int depthZone,
    MiningCellFeature feature,
    MiningEnemyType enemy = MiningEnemyType::None,
    MiningCellMaterial rewardMaterial = MiningCellMaterial::Empty)
{
    MiningCell* cell = miningCellAt(terrain, x, y);
    if (cell == nullptr || cell->material == MiningCellMaterial::Bedrock) {
        return;
    }

    const bool placeReward = rewardMaterial != MiningCellMaterial::Empty;
    if (placeReward) {
        *cell = makeCell(rewardMaterial, depthZone);
        cell->revealed = false;
    } else {
        *cell = makeCell(MiningCellMaterial::Empty, depthZone);
        cell->revealed = true;
    }
    if (featurePriority(feature) >= featurePriority(cell->feature)) {
        cell->feature = feature;
    }
    if (enemy != MiningEnemyType::None) {
        cell->enemy = enemy;
    }
}

void carveTunnelDisk(
    MiningTerrain& terrain,
    int centerX,
    int centerY,
    int radius,
    int depthZone,
    MiningCellFeature feature,
    MiningEnemyType enemy = MiningEnemyType::None)
{
    for (int y = centerY - radius; y <= centerY + radius; ++y) {
        for (int x = centerX - radius; x <= centerX + radius; ++x) {
            const int dx = x - centerX;
            const int dy = y - centerY;
            if (dx * dx + dy * dy > radius * radius) {
                continue;
            }
            stampMiningCell(terrain, x, y, depthZone, feature, enemy);
        }
    }
}

void carveRoom(
    MiningTerrain& terrain,
    int centerX,
    int centerY,
    int halfWidth,
    int halfHeight,
    int depthZone,
    MiningCellFeature feature,
    MiningEnemyType enemy,
    MiningCellMaterial rewardMaterial)
{
    for (int y = centerY - halfHeight; y <= centerY + halfHeight; ++y) {
        for (int x = centerX - halfWidth; x <= centerX + halfWidth; ++x) {
            const bool border = x == centerX - halfWidth || x == centerX + halfWidth || y == centerY - halfHeight || y == centerY + halfHeight;
            const bool rewardTile = !border && rewardMaterial != MiningCellMaterial::Empty && ((x + y + depthZone) % 3 == 0);
            stampMiningCell(terrain, x, y, depthZone, feature, enemy, rewardTile ? rewardMaterial : MiningCellMaterial::Empty);
        }
    }
}

void carveLine(
    MiningTerrain& terrain,
    int startX,
    int startY,
    int endX,
    int endY,
    int radius,
    int depthZone,
    MiningCellFeature feature,
    MiningEnemyType enemy = MiningEnemyType::None)
{
    const int steps = std::max(std::abs(endX - startX), std::abs(endY - startY));
    for (int step = 0; step <= steps; ++step) {
        const double t = steps <= 0 ? 0.0 : static_cast<double>(step) / static_cast<double>(steps);
        const int x = static_cast<int>(std::round(static_cast<double>(startX) + static_cast<double>(endX - startX) * t));
        const int y = static_cast<int>(std::round(static_cast<double>(startY) + static_cast<double>(endY - startY) * t));
        carveTunnelDisk(terrain, x, y, radius, depthZone, feature, enemy);
    }
}

void applyHostileTunnelNetwork(
    MiningTerrain& terrain,
    const GameState& state,
    const Destination& destination,
    SurfaceSiteProfile profile,
    const MiningArenaRules& rules)
{
    if (rules.maxActiveEnemies <= 0 || hostileEnemyTypeForLane(rules, 0) == MiningEnemyType::None) {
        return;
    }
    (void)profile;

    const std::uint64_t seed = hashCombine(
        hashCombine(rules.request.seed == 0 ? state.seed : rules.request.seed, hashString(destination.id)),
        static_cast<std::uint64_t>(terrain.depthZone + 31));
    int x = terrain.width / 2;
    int y = 4;
    const int bottom = std::max(terrain.height / 2, terrain.height - 7);
    carveLine(terrain, x, 3, x, y, 1, terrain.depthZone, MiningCellFeature::MainTunnel);
    while (y < bottom) {
        const int nextY = std::min(bottom, y + 3 + static_cast<int>(unitHash(seed, x, y, terrain.depthZone, 101) * 4.0));
        const int drift = static_cast<int>(std::round(unitHash(seed, x, y, terrain.depthZone, 113) * 4.0)) - 2;
        const int nextX = std::clamp(x + drift, 4, terrain.width - 5);
        carveLine(terrain, x, y, nextX, nextY, 1, terrain.depthZone, MiningCellFeature::MainTunnel);
        x = nextX;
        y = nextY;
    }

    const int branchCount = std::clamp(2 + rules.maxActiveEnemies / 2, 3, 8);
    const std::vector<MiningCellFeature> specialRooms = allowedSpecialRooms(rules);
    for (int branch = 0; branch < branchCount; ++branch) {
        const double rowT = (static_cast<double>(branch) + 1.0) / (static_cast<double>(branchCount) + 1.0);
        const int branchY = std::clamp(6 + static_cast<int>(rowT * static_cast<double>(terrain.height - 11)), 6, terrain.height - 6);
        const int side = unitHash(seed, branch, branchY, terrain.depthZone, 131) < 0.5 ? -1 : 1;
        const int startX = std::clamp(terrain.width / 2 + static_cast<int>(std::round((unitHash(seed, branch, branchY, terrain.depthZone, 137) - 0.5) * 8.0)), 4, terrain.width - 5);
        const int length = 9 + static_cast<int>(unitHash(seed, branch, branchY, terrain.depthZone, 149) * 13.0);
        const int endX = std::clamp(startX + side * length, 4, terrain.width - 5);
        const int endY = std::clamp(branchY + static_cast<int>(std::round((unitHash(seed, branch, branchY, terrain.depthZone, 151) - 0.45) * 8.0)), 6, terrain.height - 6);
        const MiningEnemyType enemy = hostileEnemyTypeForLane(rules, branch + terrain.depthZone);
        const bool mammalBurrow = enemy == MiningEnemyType::Mammal && miningRoomFeatureAllowed(rules, MiningCellFeature::OrganicBurrow);
        const MiningCellFeature branchFeature = mammalBurrow ? MiningCellFeature::OrganicBurrow : MiningCellFeature::BranchTunnel;
        const int branchRadius = mammalBurrow ? 2 : 1;
        carveLine(terrain, startX, branchY, endX, branchY, branchRadius, terrain.depthZone, branchFeature, mammalBurrow ? enemy : MiningEnemyType::None);
        carveLine(terrain, endX, branchY, endX, endY, branchRadius, terrain.depthZone, branchFeature, mammalBurrow ? enemy : MiningEnemyType::None);

        const int encounterX = std::clamp((startX + endX) / 2, 3, terrain.width - 4);
        const MiningCellFeature encounterFeature = mammalBurrow ? MiningCellFeature::OrganicBurrow : MiningCellFeature::EncounterZone;
        if (miningRoomFeatureAllowed(rules, encounterFeature)) {
            carveTunnelDisk(terrain, encounterX, branchY, mammalBurrow ? 3 : 2, terrain.depthZone, encounterFeature, enemy);
        }

        if (specialRooms.empty()) {
            continue;
        }
        const double roomRoll = unitHash(seed, branch, endY, terrain.depthZone, 173);
        MiningCellFeature room = specialRooms[
            static_cast<std::size_t>(roomRoll * static_cast<double>(specialRooms.size())) % specialRooms.size()];
        if (mammalBurrow && miningRoomFeatureAllowed(rules, MiningCellFeature::BossChamber) && branch % 2 == 0) {
            room = MiningCellFeature::BossChamber;
        }
        MiningCellMaterial reward = room == MiningCellFeature::HiveNest
            ? MiningCellMaterial::CommonOre
            : MiningCellMaterial::RareOre;
        if (room == MiningCellFeature::BossChamber && miningMaterialAllowed(rules, MiningCellMaterial::ExoticVein)) {
            reward = MiningCellMaterial::ExoticVein;
        } else if (!miningMaterialAllowed(rules, reward)) {
            reward = MiningCellMaterial::CommonOre;
        }
        const int roomHalfWidth = room == MiningCellFeature::BossChamber ? 5 : 3 + (room == MiningCellFeature::MinibossLair ? 1 : 0);
        const int roomHalfHeight = room == MiningCellFeature::BossChamber ? 3 : 2;
        carveRoom(terrain, endX, endY, roomHalfWidth, roomHalfHeight, terrain.depthZone, room, enemy, reward);
    }
}

MiningCellMaterial generatedMaterial(
    std::uint64_t arenaSeed,
    const Destination& destination,
    const MiningArenaRules& rules,
    MiningSiteBiome biome,
    SurfaceSiteProfile profile,
    int x,
    int y,
    int depthZone,
    int width,
    int height)
{
    if (x <= 0 || x >= width - 1 || y >= height - 1) {
        return MiningCellMaterial::Bedrock;
    }
    if (y < 4) {
        return MiningCellMaterial::Empty;
    }
    if (std::abs(x - width / 2) <= 1 && y < 8) {
        return MiningCellMaterial::Empty;
    }

    const double depth = static_cast<double>(y + depthZone * height) / static_cast<double>(height);
    const std::uint64_t seed = hashCombine(arenaSeed, hashString(destination.id));
    const double pocket = unitHash(seed, x / 3, y / 3, depthZone, 11);
    const double fleck = unitHash(seed, x, y, depthZone, 23);
    const double artifactRoll = unitHash(seed, x / 4, y / 4, depthZone, 37);
    const double hazardRoll = unitHash(seed, x / 4, y / 4, depthZone, 53);

    const double siteOreBonus = profile == SurfaceSiteProfile::OreShelf ? 0.05 : 0.0;
    const double fractureArtifactBonus = profile == SurfaceSiteProfile::FractureField ? 0.025 : 0.0;
    const double tierBonus = static_cast<double>(destination.tier) * 0.012;

    if (biome == MiningSiteBiome::ThermalLava) {
        const double lavaPocket = unitHash(seed, x / 3, y / 3, depthZone, 0x494F4C415641ULL);
        const double lavaFleck = unitHash(seed, x, y, depthZone, 0x494F5345414DULL);
        return y > 7 && (lavaPocket > 0.70 || lavaFleck > 0.965)
            ? MiningCellMaterial::HazardPocket
            : MiningCellMaterial::Regolith;
    }

    if (depthZone >= 1 && artifactRoll > 0.986 - fractureArtifactBonus - tierBonus && y > height / 2) {
        const MiningCellMaterial rich = depthZone >= 2 ? MiningCellMaterial::ExoticVein : MiningCellMaterial::RareOre;
        if (miningMaterialAllowed(rules, rich)) {
            return rich;
        }
    }
    if (rules.mechanics.environmentalHazards &&
        miningMaterialAllowed(rules, MiningCellMaterial::HazardPocket) &&
        hazardRoll > 0.978 - depth * 0.010 && y > 8) {
        return MiningCellMaterial::HazardPocket;
    }
    if (miningMaterialAllowed(rules, MiningCellMaterial::ExoticVein) &&
        depthZone >= 2 && pocket > 0.962 - tierBonus && y > 12) {
        return MiningCellMaterial::ExoticVein;
    }
    if (miningMaterialAllowed(rules, MiningCellMaterial::RareOre) &&
        pocket > 0.925 - depth * 0.020 - siteOreBonus - tierBonus) {
        return MiningCellMaterial::RareOre;
    }
    if (pocket > 0.745 - siteOreBonus - depth * 0.030 || fleck > 0.940) {
        return MiningCellMaterial::CommonOre;
    }
    if (miningMaterialAllowed(rules, MiningCellMaterial::HardRock) && fleck < 0.22 + depth * 0.08) {
        return MiningCellMaterial::HardRock;
    }
    return MiningCellMaterial::Regolith;
}

void revealAround(MiningRunState& mining, double centerX, double centerY, double radius)
{
    const int minX = std::max(0, static_cast<int>(std::floor(centerX - radius)));
    const int maxX = std::min(mining.terrain.width - 1, static_cast<int>(std::ceil(centerX + radius)));
    const int minY = std::max(0, static_cast<int>(std::floor(centerY - radius)));
    const int maxY = std::min(mining.terrain.height - 1, static_cast<int>(std::ceil(centerY + radius)));
    const double radiusSq = radius * radius;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const double dx = static_cast<double>(x) + 0.5 - centerX;
            const double dy = static_cast<double>(y) + 0.5 - centerY;
            if (dx * dx + dy * dy > radiusSq) {
                continue;
            }
            if (MiningCell* cell = miningCellAt(mining.terrain, x, y)) {
                if (hiddenProtectedObjectiveAt(mining, x, y)) {
                    continue;
                }
                if (!cocoonCellVisible(mining, *cell)) {
                    maybeRevealCocoonLayerAt(mining, *cell);
                }
                if (!cocoonCellVisible(mining, *cell)) {
                    continue;
                }
                cell->revealed = true;
            }
        }
    }
    if (mining.artifact.present &&
        (mining.artifact.state == MiningArtifactState::Embedded ||
         mining.artifact.state == MiningArtifactState::Loose) &&
        (!hasLayeredCocoon(mining) || cocoonComplete(mining))) {
        const double dx = mining.artifact.x - centerX;
        const double dy = mining.artifact.y - centerY;
        if (dx * dx + dy * dy <= radiusSq) {
            mining.artifact.revealed = true;
        }
    }
}

bool softMiningMaterial(MiningCellMaterial material)
{
    return material == MiningCellMaterial::Regolith ||
        material == MiningCellMaterial::HazardPocket ||
        material == MiningCellMaterial::FuelPocket ||
        material == MiningCellMaterial::OxygenPocket;
}

double miningContactResistance(MiningCellMaterial material)
{
    if (material == MiningCellMaterial::Regolith) {
        return tuning::mining::softTerrainMoveScale;
    }
    if (material == MiningCellMaterial::HazardPocket) {
        return tuning::mining::softTerrainMoveScale * 0.65;
    }
    return 0.0;
}

double contactLimitedPosition(double current, double proposed, int obstacleCell)
{
    constexpr double contactPadding = 0.018;
    if (proposed > current) {
        return std::min(proposed, static_cast<double>(obstacleCell) - contactPadding);
    }
    if (proposed < current) {
        return std::max(proposed, static_cast<double>(obstacleCell + 1) + contactPadding);
    }
    return current;
}

void updateContactBounce(MiningRunState& mining, double dt)
{
    mining.contactBounceCooldown = std::max(0.0, mining.contactBounceCooldown - dt);
    mining.contactSpeedRecovery = std::min(
        1.0,
        mining.contactSpeedRecovery + dt / tuning::mining::postContactSpeedRecoverySeconds);
    mining.contactBounceVelocity -= mining.contactBounce * tuning::mining::contactBounceSpring * dt;
    mining.contactBounceVelocity *= std::pow(tuning::mining::contactBounceDamping, dt * 60.0);
    mining.contactBounce += mining.contactBounceVelocity * dt;
    mining.contactBounce = std::clamp(mining.contactBounce, 0.0, tuning::mining::contactBounceMaxCells);
    if (mining.contactBounce <= 0.0001 && mining.contactBounceVelocity < 0.0) {
        mining.contactBounce = 0.0;
        mining.contactBounceVelocity = 0.0;
    }
}

void triggerHardContactBounce(MiningRunState& mining, double dirX, double dirY, double bounceRelief)
{
    mining.recoilX = -dirX;
    mining.recoilY = -dirY;
    if (mining.contactBounceCooldown > 0.0) {
        return;
    }
    const double reliefScale = std::clamp(1.0 - bounceRelief, 0.35, 1.0);
    mining.contactBounceVelocity += tuning::mining::hardTerrainBounceImpulse * reliefScale * (1.0 + mining.contactIntensity * 0.35);
    mining.contactSpeedRecovery = std::min(mining.contactSpeedRecovery, std::clamp(bounceRelief, 0.0, 0.55));
    mining.contactBounceCooldown = tuning::mining::hardTerrainBounceCooldownSeconds;
}

bool canOccupy(const MiningTerrain& terrain, double x, double y)
{
    const int cellX = static_cast<int>(std::floor(x));
    const int cellY = static_cast<int>(std::floor(y));
    const MiningCell* cell = miningCellAt(terrain, cellX, cellY);
    return cell != nullptr &&
        !miningMaterialSolid(cell->material) &&
        !cell->suitOnlyPassage;
}

bool canOccupyActor(
    const MiningTerrain& terrain,
    double x,
    double y,
    double colliderRadius,
    bool allowSuitOnlyPassage)
{
    constexpr std::array<std::pair<double, double>, 9> samples {{
        {0.0, 0.0},
        {1.0, 0.0},
        {-1.0, 0.0},
        {0.0, 1.0},
        {0.0, -1.0},
        {0.70710678118, 0.70710678118},
        {-0.70710678118, 0.70710678118},
        {0.70710678118, -0.70710678118},
        {-0.70710678118, -0.70710678118}
    }};
    for (const auto& [sampleX, sampleY] : samples) {
        const int cellX = static_cast<int>(std::floor(x + sampleX * colliderRadius));
        const int cellY = static_cast<int>(std::floor(y + sampleY * colliderRadius));
        const MiningCell* cell = miningCellAt(terrain, cellX, cellY);
        if (cell == nullptr || miningMaterialSolid(cell->material) ||
            (cell->suitOnlyPassage && !allowSuitOnlyPassage)) {
            return false;
        }
    }
    return true;
}

bool drillableCell(const MiningCell* cell)
{
    return cell != nullptr && miningMaterialSolid(cell->material) && cell->material != MiningCellMaterial::Bedrock;
}

MaterialInventory brokenCellReward(
    GameState& state,
    const MiningDrillStats& stats,
    MiningCellMaterial material,
    bool includeYieldBonuses);
void damageMiningArtifact(MiningRunState& mining, double damage);
void releaseEmbeddedArtifact(MiningRunState& mining);

double drillHeatDelta(MiningCellMaterial material, const MiningDrillStats& stats, double dt)
{
    return (tuning::mining::heatRisePerSecond +
        (material == MiningCellMaterial::HardRock ? tuning::mining::heatHardRockBonus : 0.0)) * stats.heatRiseScale * dt;
}

void applyDrillSystemLoad(MiningRunState& mining, const MiningDrillStats& stats, double heatDelta, double exposureSeconds)
{
    mining.drillHeat += heatDelta;
    if (mining.drillHeat > tuning::mining::heatDamageThreshold) {
        mining.drillIntegrity = std::max(
            0.0,
            mining.drillIntegrity -
                std::max(0.0, 1.0 - stats.integrityRelief) *
                    tuning::mining::overheatIntegrityDamagePerSecond *
                    exposureSeconds);
    }
}

void spawnLooseMaterialChunks(
    MiningRunState& mining,
    const MaterialInventory& gain,
    double x,
    double y)
{
    const auto append = [&](MiningCellMaterial material, int count, int cargoValue, int laneOffset) {
        for (int index = 0; index < std::max(0, count); ++index) {
            const double angle = static_cast<double>(
                (mining.combatSequence + laneOffset + index * 5) % 16) * (kPi * 2.0 / 16.0);
            MiningLooseChunk chunk;
            chunk.material = material;
            chunk.x = x + std::cos(angle) * 0.16;
            chunk.y = y + std::sin(angle) * 0.16;
            chunk.velocityX = std::cos(angle) * 0.35;
            chunk.velocityY = std::sin(angle) * 0.35;
            chunk.cargoValue = cargoValue;
            mining.looseChunks.push_back(chunk);
        }
    };
    append(MiningCellMaterial::CommonOre, gain.common, tuning::mining::commonCargo, 1);
    append(MiningCellMaterial::RareOre, gain.rare, tuning::mining::rareCargo, 7);
    append(MiningCellMaterial::ExoticVein, gain.exotic, tuning::mining::exoticCargo, 13);
}

bool applyMiningPocketReward(
    GameState& state,
    const MiningDrillStats& stats,
    MiningCellMaterial material,
    double x,
    double y)
{
    MiningRunState& mining = state.run.mining;
    if (material == MiningCellMaterial::FuelPocket) {
        SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
        const int before = expedition.sharedFuel;
        expedition.sharedFuel = std::min(
            std::max(0, expedition.sharedFuelCapacity),
            std::max(0, expedition.sharedFuel) + 1);
        if (expedition.sharedFuel > before) {
            recordMiningPickup(mining, MiningPickupKind::Fuel, 1, x, y);
            return true;
        }
        return false;
    }
    if (material == MiningCellMaterial::OxygenPocket) {
        const double capacity = std::max(0.0, stats.oxygenSeconds);
        const double before = mining.oxygenSeconds;
        mining.oxygenSeconds = std::min(
            capacity,
            std::max(0.0, mining.oxygenSeconds) + tuning::mining::oxygenPocketRestoreSeconds);
        if (mining.oxygenSeconds > before + 0.0001) {
            recordMiningPickup(mining, MiningPickupKind::Oxygen, 1, x, y);
            return true;
        }
    }
    return false;
}

bool completeBrokenMiningCell(
    GameState& state,
    const MiningDrillStats& stats,
    int x,
    int y,
    MaterialInventory* miniDroneHaul = nullptr)
{
    MiningRunState& mining = state.run.mining;
    MiningCell* target = miningCellAt(mining.terrain, x, y);
    if (!drillableCell(target)) {
        return false;
    }

    const MiningCellMaterial brokenMaterial = target->material;
    const bool gateAssociated = target->gateAssociated || target->cocoonLayer >= 0;
    *target = makeCell(MiningCellMaterial::Empty, mining.depthZone);
    target->revealed = true;
    mining.cellsBroken += 1;
    if (brokenMaterial == MiningCellMaterial::ArtifactCache) {
        releaseEmbeddedArtifact(mining);
    } else if (brokenMaterial == MiningCellMaterial::FuelPocket ||
               brokenMaterial == MiningCellMaterial::OxygenPocket) {
        (void)applyMiningPocketReward(
            state,
            stats,
            brokenMaterial,
            static_cast<double>(x) + 0.5,
            static_cast<double>(y) + 0.5);
    } else {
        const MaterialInventory gain = brokenCellReward(
            state,
            stats,
            brokenMaterial,
            miniDroneHaul == nullptr);
        if (miniDroneHaul != nullptr) {
            addMiningMaterials(*miniDroneHaul, gain);
            recordMiningMaterialPickup(
                mining,
                gain,
                static_cast<double>(x) + 0.5,
                static_cast<double>(y) + 0.5);
        } else if (operatorControlled(mining)) {
            spawnLooseMaterialChunks(
                mining,
                gain,
                static_cast<double>(x) + 0.5,
                static_cast<double>(y) + 0.5);
        } else {
            addMiningMaterials(mining.temporaryMaterials, gain);
            mining.cargo += materialCargo(gain);
            recordMiningMaterialPickup(
                mining,
                gain,
                static_cast<double>(x) + 0.5,
                static_cast<double>(y) + 0.5);
        }
    }
    revealAround(mining, static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5, 1.35);
    markDirty(mining.terrain, x, y);
    if (gateAssociated) {
        markMiningGateDerivedStateDirty(mining);
    }
    return true;
}

bool applyDrillDamage(GameState& state, const MiningDrillStats& stats, int x, int y, double dt)
{
    MiningRunState& mining = state.run.mining;
    MiningCell* target = miningCellAt(mining.terrain, x, y);
    if (!drillableCell(target) || !target->revealed ||
        !cocoonCellVisible(mining, *target)) {
        return false;
    }

    if (target->cocoonLayer >= 0 &&
        target->material == MiningCellMaterial::HazardPocket) {
        state.statusLine = "This protected layer is drill-proof. Treat the revealed marked tiles with the required Hazard Drone.";
        return false;
    }
    if (target->material == MiningCellMaterial::ArtifactCache && mining.gate.active &&
        (mining.gate.state == MiningGateState::Locked || mining.gate.state == MiningGateState::InProgress) &&
        (mining.gate.type == MiningGateType::HazardCocoon ||
         mining.gate.type == MiningGateType::EnemySealedChamber ||
         mining.gate.type == MiningGateType::SurveyTriangulation ||
         mining.gate.type == MiningGateType::CompoundVault)) {
        target->revealed = true;
        state.statusLine = std::string(miningGateName(mining.gate.type)) + " remains locked. Complete the site requirement first.";
        return false;
    }

    const MiningCellMaterial material = target->material;
    if (material == MiningCellMaterial::ArtifactCache) {
        damageMiningArtifact(mining, tuning::mining::artifactDrillDamagePerSecond * dt);
    }
    double drillPower = stats.power;
    if (!softMiningMaterial(material)) {
        drillPower *= tuning::mining::denseMaterialDrillPowerScale;
    }
    if (mining.drillHeat >= tuning::mining::heatSlowThreshold) {
        drillPower *= tuning::mining::overheatedDrillSlow;
    }
    target->remainingToughness = std::max(0.0, target->remainingToughness - drillPower * dt);
    target->revealed = true;
    markDirty(mining.terrain, x, y);
    if (target->remainingToughness <= 0.0) {
        return completeBrokenMiningCell(state, stats, x, y);
    }
    return false;
}

struct MiniDroneHomePoint {
    double x = 0.0;
    double y = 0.0;
};

double miniDroneRoleSpread(int roleIndex)
{
    if (roleIndex <= 0) {
        return 0.0;
    }
    const int lane = (roleIndex + 1) / 2;
    return static_cast<double>(lane) * tuning::mining::miniDroneSameRoleSpacingCells * (roleIndex % 2 == 1 ? 1.0 : -1.0);
}

MiniDroneHomePoint miniDroneHomePoint(const MiningRunState& mining, const MiningMiniDroneAgent& agent)
{
    const MiniDroneCoordinationPoint orbit = miniDroneOrbitPoint(mining, agent);
    return {orbit.x, orbit.y};
}

MiniDroneHomePoint miniDroneShipDockPoint(const MiningRunState& mining, const MiningMiniDroneAgent& agent)
{
    const double spread = miniDroneRoleSpread(agent.roleIndex) *
        tuning::mining::resourceDroneDockSpacingCells / tuning::mining::miniDroneSameRoleSpacingCells;
    const double roleLane = agent.role == MiniDroneRole::Mining ? -0.55 : 0.55;
    return {mining.returnZoneX + roleLane + spread, mining.returnZoneY};
}

double miniDroneDistanceSquared(const MiningMiniDroneAgent& agent, double x, double y)
{
    const double dx = x - agent.x;
    const double dy = y - agent.y;
    return dx * dx + dy * dy;
}

enum class MiniDroneArrivalStyle {
    Precise,
    SmoothFormation,
    DeliberateSurvey
};

bool moveMiniDroneToward(
    MiningMiniDroneAgent& agent,
    const MiningRunState& mining,
    double targetX,
    double targetY,
    double speed,
    double dt,
    MiniDroneArrivalStyle arrivalStyle = MiniDroneArrivalStyle::Precise)
{
    const double dx = targetX - agent.x;
    const double dy = targetY - agent.y;
    const double distance = std::sqrt(dx * dx + dy * dy);
    const double currentSpeed = std::sqrt(agent.velocityX * agent.velocityX + agent.velocityY * agent.velocityY);
    const bool preciseArrival = arrivalStyle != MiniDroneArrivalStyle::SmoothFormation;
    if (preciseArrival &&
        distance <= tuning::mining::miniDroneHomeRadiusCells &&
        currentSpeed <= tuning::mining::miniDroneStopSpeedCellsPerSecond) {
        agent.x = std::clamp(targetX, 0.5, static_cast<double>(mining.terrain.width) - 0.5);
        agent.y = std::clamp(targetY, 0.5, static_cast<double>(mining.terrain.height) - 0.5);
        agent.velocityX = 0.0;
        agent.velocityY = 0.0;
        return true;
    }
    const double safeDistance = std::max(0.0001, distance);
    const double speedScale = std::clamp(distance / tuning::mining::miniDroneBrakeRadiusCells, 0.0, 1.0);
    const double desiredSpeed = std::max(0.0, speed) * speedScale;
    const double desiredVelocityX = dx / safeDistance * desiredSpeed;
    const double desiredVelocityY = dy / safeDistance * desiredSpeed;
    const double responsePerSecond = arrivalStyle == MiniDroneArrivalStyle::DeliberateSurvey
        ? tuning::mining::surveyDroneVelocityResponsePerSecond
        : (arrivalStyle == MiniDroneArrivalStyle::SmoothFormation
                ? tuning::mining::miniDroneFormationResponsePerSecond
                : tuning::mining::miniDroneVelocityResponsePerSecond);
    const double response = 1.0 - std::exp(-responsePerSecond * dt);
    agent.velocityX += (desiredVelocityX - agent.velocityX) * response;
    agent.velocityY += (desiredVelocityY - agent.velocityY) * response;
    double nextX = std::clamp(agent.x + agent.velocityX * dt, 0.5, static_cast<double>(mining.terrain.width) - 0.5);
    double nextY = std::clamp(agent.y + agent.velocityY * dt, 0.5, static_cast<double>(mining.terrain.height) - 0.5);
    if (!canOccupyActor(
            mining.terrain,
            nextX,
            nextY,
            tuning::mining::miniDroneColliderRadiusCells,
            true)) {
        if (canOccupyActor(
                mining.terrain,
                nextX,
                agent.y,
                tuning::mining::miniDroneColliderRadiusCells,
                true)) {
            nextY = agent.y;
            agent.velocityY = 0.0;
        } else if (canOccupyActor(
                       mining.terrain,
                       agent.x,
                       nextY,
                       tuning::mining::miniDroneColliderRadiusCells,
                       true)) {
            nextX = agent.x;
            agent.velocityX = 0.0;
        } else {
            nextX = agent.x;
            nextY = agent.y;
            agent.velocityX *= -0.20;
            agent.velocityY *= -0.20;
        }
    }
    const double remainingX = targetX - nextX;
    const double remainingY = targetY - nextY;
    if (preciseArrival && dx * remainingX + dy * remainingY <= 0.0) {
        agent.x = std::clamp(targetX, 0.5, static_cast<double>(mining.terrain.width) - 0.5);
        agent.y = std::clamp(targetY, 0.5, static_cast<double>(mining.terrain.height) - 0.5);
        agent.velocityX = 0.0;
        agent.velocityY = 0.0;
        return true;
    }
    agent.x = nextX;
    agent.y = nextY;
    const double remainingDistance = std::sqrt(remainingX * remainingX + remainingY * remainingY);
    return remainingDistance <= tuning::mining::miniDroneHomeRadiusCells &&
        (arrivalStyle == MiniDroneArrivalStyle::SmoothFormation ||
            std::sqrt(agent.velocityX * agent.velocityX + agent.velocityY * agent.velocityY) <=
                tuning::mining::miniDroneStopSpeedCellsPerSecond);
}

bool moveHazardDroneDirect(
    MiningMiniDroneAgent& agent,
    const MiningRunState& mining,
    double targetX,
    double targetY,
    double speed,
    double dt,
    MiniDroneArrivalStyle arrivalStyle = MiniDroneArrivalStyle::Precise)
{
    const double dx = targetX - agent.x;
    const double dy = targetY - agent.y;
    const double distance = std::sqrt(dx * dx + dy * dy);
    const double currentSpeed = std::sqrt(
        agent.velocityX * agent.velocityX +
        agent.velocityY * agent.velocityY);
    const bool preciseArrival =
        arrivalStyle != MiniDroneArrivalStyle::SmoothFormation;
    if (preciseArrival &&
        distance <= tuning::mining::miniDroneHomeRadiusCells &&
        currentSpeed <= tuning::mining::miniDroneStopSpeedCellsPerSecond) {
        agent.x = std::clamp(
            targetX,
            0.5,
            static_cast<double>(mining.terrain.width) - 0.5);
        agent.y = std::clamp(
            targetY,
            0.5,
            static_cast<double>(mining.terrain.height) - 0.5);
        agent.velocityX = 0.0;
        agent.velocityY = 0.0;
        return true;
    }

    const double safeDistance = std::max(0.0001, distance);
    const double speedScale = std::clamp(
        distance / tuning::mining::miniDroneBrakeRadiusCells,
        0.0,
        1.0);
    const double desiredSpeed = std::max(0.0, speed) * speedScale;
    const double desiredVelocityX = dx / safeDistance * desiredSpeed;
    const double desiredVelocityY = dy / safeDistance * desiredSpeed;
    const double responsePerSecond =
        arrivalStyle == MiniDroneArrivalStyle::SmoothFormation
        ? tuning::mining::miniDroneFormationResponsePerSecond
        : tuning::mining::miniDroneVelocityResponsePerSecond;
    const double response = 1.0 - std::exp(-responsePerSecond * dt);
    agent.velocityX += (desiredVelocityX - agent.velocityX) * response;
    agent.velocityY += (desiredVelocityY - agent.velocityY) * response;

    const double nextX = std::clamp(
        agent.x + agent.velocityX * dt,
        0.5,
        static_cast<double>(mining.terrain.width) - 0.5);
    const double nextY = std::clamp(
        agent.y + agent.velocityY * dt,
        0.5,
        static_cast<double>(mining.terrain.height) - 0.5);
    const double remainingX = targetX - nextX;
    const double remainingY = targetY - nextY;
    if (preciseArrival && dx * remainingX + dy * remainingY <= 0.0) {
        agent.x = std::clamp(
            targetX,
            0.5,
            static_cast<double>(mining.terrain.width) - 0.5);
        agent.y = std::clamp(
            targetY,
            0.5,
            static_cast<double>(mining.terrain.height) - 0.5);
        agent.velocityX = 0.0;
        agent.velocityY = 0.0;
        return true;
    }

    agent.x = nextX;
    agent.y = nextY;
    const double remainingDistance =
        std::sqrt(remainingX * remainingX + remainingY * remainingY);
    return remainingDistance <= tuning::mining::miniDroneHomeRadiusCells &&
        (arrivalStyle == MiniDroneArrivalStyle::SmoothFormation ||
            std::sqrt(
                agent.velocityX * agent.velocityX +
                agent.velocityY * agent.velocityY) <=
                tuning::mining::miniDroneStopSpeedCellsPerSecond);
}

bool moveMiniDroneTowardTask(
    MiningMiniDroneAgent& agent,
    const MiningRunState& mining,
    int targetCellX,
    int targetCellY,
    double workRangeCells,
    double speed,
    double dt)
{
    const std::optional<MiniDroneCoordinationPoint> waypoint =
        miniDroneTaskNavigationWaypoint(
            mining,
            agent,
            targetCellX,
            targetCellY,
            workRangeCells);
    if (!waypoint.has_value()) {
        return false;
    }
    const MiningCell* occupiedCell = miningCellAt(
        mining.terrain,
        static_cast<int>(std::floor(agent.x)),
        static_cast<int>(std::floor(agent.y)));
    if (occupiedCell != nullptr && miningMaterialSolid(occupiedCell->material)) {
        // Old straight-line movement could strand a drone inside the cell that
        // was just converted. Move it to the verified adjacent recovery cell.
        agent.x = waypoint->x;
        agent.y = waypoint->y;
        agent.velocityX = 0.0;
        agent.velocityY = 0.0;
        return true;
    }
    moveMiniDroneToward(agent, mining, waypoint->x, waypoint->y, speed, dt);
    return true;
}

bool moveMiniDroneTowardOpenPoint(
    MiningMiniDroneAgent& agent,
    const MiningRunState& mining,
    double targetX,
    double targetY,
    double speed,
    double dt,
    MiniDroneArrivalStyle arrivalStyle = MiniDroneArrivalStyle::Precise)
{
    const int agentCellX = static_cast<int>(std::floor(agent.x));
    const int agentCellY = static_cast<int>(std::floor(agent.y));
    const int targetCellX = static_cast<int>(std::floor(targetX));
    const int targetCellY = static_cast<int>(std::floor(targetY));
    if (agentCellX == targetCellX && agentCellY == targetCellY) {
        return moveMiniDroneToward(
            agent, mining, targetX, targetY, speed, dt, arrivalStyle);
    }
    const std::optional<MiniDroneCoordinationPoint> waypoint =
        miniDroneTaskNavigationWaypoint(
            mining,
            agent,
            targetCellX,
            targetCellY,
            0.0);
    if (!waypoint.has_value()) {
        return false;
    }
    const MiningCell* occupiedCell = miningCellAt(
        mining.terrain,
        static_cast<int>(std::floor(agent.x)),
        static_cast<int>(std::floor(agent.y)));
    if (occupiedCell != nullptr && miningMaterialSolid(occupiedCell->material)) {
        agent.x = waypoint->x;
        agent.y = waypoint->y;
        agent.velocityX = 0.0;
        agent.velocityY = 0.0;
        return false;
    }
    // Reaching an intermediate waypoint is not reaching home. The next frame
    // advances to the following cell until the actual destination cell is hit.
    moveMiniDroneToward(
        agent,
        mining,
        waypoint->x,
        waypoint->y,
        speed,
        dt,
        MiniDroneArrivalStyle::Precise);
    return false;
}

void slowMiniDroneAtTask(MiningMiniDroneAgent& agent, const MiningRunState& mining, double dt)
{
    const double damping = std::exp(-tuning::mining::miniDroneTaskStopDampingPerSecond * dt);
    agent.velocityX *= damping;
    agent.velocityY *= damping;
    agent.x = std::clamp(agent.x + agent.velocityX * dt, 0.5, static_cast<double>(mining.terrain.width) - 0.5);
    agent.y = std::clamp(agent.y + agent.velocityY * dt, 0.5, static_cast<double>(mining.terrain.height) - 0.5);
    if (std::sqrt(agent.velocityX * agent.velocityX + agent.velocityY * agent.velocityY) <= tuning::mining::miniDroneStopSpeedCellsPerSecond) {
        agent.velocityX = 0.0;
        agent.velocityY = 0.0;
    }
}

void keepMiniDroneOutsideRigPerimeter(
    MiningMiniDroneAgent& agent,
    const MiningRunState& mining,
    double fallbackX,
    double fallbackY)
{
    const MiniDroneAnchorFrame anchor = resolveMiniDroneAnchor(mining, agent.anchorTarget);
    if (!anchor.valid) {
        return;
    }
    double dx = agent.x - anchor.x;
    double dy = agent.y - anchor.y;
    double distance = std::sqrt(dx * dx + dy * dy);
    if (distance >= tuning::mining::attackDroneRigClearanceCells) {
        return;
    }
    if (distance <= 0.0001) {
        dx = fallbackX - anchor.x;
        dy = fallbackY - anchor.y;
        distance = std::max(0.0001, std::sqrt(dx * dx + dy * dy));
    }
    const double normalX = dx / distance;
    const double normalY = dy / distance;
    agent.x = std::clamp(
        anchor.x + normalX * tuning::mining::attackDroneRigClearanceCells,
        0.5,
        static_cast<double>(mining.terrain.width) - 0.5);
    agent.y = std::clamp(
        anchor.y + normalY * tuning::mining::attackDroneRigClearanceCells,
        0.5,
        static_cast<double>(mining.terrain.height) - 0.5);
    const double inwardSpeed = agent.velocityX * normalX + agent.velocityY * normalY;
    if (inwardSpeed < 0.0) {
        agent.velocityX -= normalX * inwardSpeed;
        agent.velocityY -= normalY * inwardSpeed;
    }
}

int miniDroneHaulChunkCount(const MiningMiniDroneAgent& agent)
{
    return std::max(0, agent.haulMaterials.common) +
        std::max(0, agent.haulMaterials.rare) +
        std::max(0, agent.haulMaterials.exotic);
}

bool loadOneNearbyLooseChunk(
    MiningRunState& mining,
    MiningMiniDroneAgent& agent,
    double collectionRadius)
{
    const double collectionRangeSq = collectionRadius * collectionRadius;
    MiningLooseChunk* closestChunk = nullptr;
    double closestDistanceSq = collectionRangeSq;
    for (MiningLooseChunk& chunk : mining.looseChunks) {
        if (!chunk.active) {
            continue;
        }
        const double dx = chunk.x - agent.x;
        const double dy = chunk.y - agent.y;
        const double distanceSq = dx * dx + dy * dy;
        if (distanceSq <= closestDistanceSq) {
            closestDistanceSq = distanceSq;
            closestChunk = &chunk;
        }
    }
    if (closestChunk == nullptr) {
        return false;
    }

    closestChunk->active = false;
    switch (closestChunk->material) {
    case MiningCellMaterial::ExoticVein:
        ++agent.haulMaterials.exotic;
        recordMiningPickup(mining, MiningPickupKind::ExoticOre, 1, closestChunk->x, closestChunk->y);
        break;
    case MiningCellMaterial::RareOre:
        ++agent.haulMaterials.rare;
        recordMiningPickup(mining, MiningPickupKind::RareOre, 1, closestChunk->x, closestChunk->y);
        break;
    default:
        ++agent.haulMaterials.common;
        recordMiningPickup(mining, MiningPickupKind::CommonOre, 1, closestChunk->x, closestChunk->y);
        break;
    }
    return true;
}

bool resourceDroneCanAccessRigCargo(
    const MiningRunState& mining,
    const MiningMiniDroneAgent& agent)
{
    if (mining.rigDisabled || mining.rigDepthZone != mining.depthZone ||
        (mining.temporaryMaterials.common <= 0 &&
            mining.temporaryMaterials.rare <= 0 &&
            mining.temporaryMaterials.exotic <= 0)) {
        return false;
    }
    const double collectionRadius =
        tuning::mining::resourceDroneCollectionRadiusCells +
        tuning::mining::resourceDroneCollectionExitToleranceCells;
    return miniDroneDistanceSquared(agent, mining.droneX, mining.droneY) <=
        collectionRadius * collectionRadius;
}

bool resourceDroneHasCollectibleMaterial(
    const MiningRunState& mining,
    const MiningMiniDroneAgent& agent)
{
    const double collectionRangeSq =
        tuning::mining::resourceDroneCollectionRadiusCells *
        tuning::mining::resourceDroneCollectionRadiusCells;
    const bool nearbyLooseChunk = std::any_of(
        mining.looseChunks.begin(),
        mining.looseChunks.end(),
        [&](const MiningLooseChunk& chunk) {
            if (!chunk.active) {
                return false;
            }
            const double dx = chunk.x - agent.x;
            const double dy = chunk.y - agent.y;
            return dx * dx + dy * dy <= collectionRangeSq;
        });
    return nearbyLooseChunk || resourceDroneCanAccessRigCargo(mining, agent);
}

double resourceDroneTransferInterval(const MiningMiniDroneAgent& agent)
{
    const int upgrades = std::max(0, agent.upgradeLevel - 1);
    return tuning::mining::resourceDroneTransferSeconds /
        (1.0 + static_cast<double>(upgrades) * tuning::mining::resourceDroneUpgradeRateBonus);
}

bool loadOneResourceChunk(MiningRunState& mining, MiningMiniDroneAgent& agent)
{
    if (loadOneNearbyLooseChunk(
            mining,
            agent,
            tuning::mining::resourceDroneCollectionRadiusCells)) {
        return true;
    }
    if (!resourceDroneCanAccessRigCargo(mining, agent)) {
        return false;
    }
    if (mining.temporaryMaterials.exotic > 0) {
        --mining.temporaryMaterials.exotic;
        ++agent.haulMaterials.exotic;
        mining.cargo = std::max(0, mining.cargo - tuning::mining::exoticCargo);
        return true;
    }
    if (mining.temporaryMaterials.rare > 0) {
        --mining.temporaryMaterials.rare;
        ++agent.haulMaterials.rare;
        mining.cargo = std::max(0, mining.cargo - tuning::mining::rareCargo);
        return true;
    }
    if (mining.temporaryMaterials.common > 0) {
        --mining.temporaryMaterials.common;
        ++agent.haulMaterials.common;
        mining.cargo = std::max(0, mining.cargo - tuning::mining::commonCargo);
        return true;
    }
    return false;
}

bool unloadOneMiniDroneChunk(MiningRunState& mining, MiningMiniDroneAgent& agent)
{
    if (agent.haulMaterials.common > 0) {
        --agent.haulMaterials.common;
        ++mining.stowedMaterials.common;
        mining.stowedCargo += tuning::mining::commonCargo;
        return true;
    }
    if (agent.haulMaterials.rare > 0) {
        --agent.haulMaterials.rare;
        ++mining.stowedMaterials.rare;
        mining.stowedCargo += tuning::mining::rareCargo;
        return true;
    }
    if (agent.haulMaterials.exotic > 0) {
        --agent.haulMaterials.exotic;
        ++mining.stowedMaterials.exotic;
        mining.stowedCargo += tuning::mining::exoticCargo;
        return true;
    }
    return false;
}

bool miniDroneTargetEnemyValid(const MiningRunState& mining, const MiningMiniDroneAgent& agent)
{
    return agent.targetEnemyIndex >= 0 && agent.targetEnemyIndex < static_cast<int>(mining.enemies.size()) &&
        mining.enemies[static_cast<std::size_t>(agent.targetEnemyIndex)].active;
}

MiningCellMaterial refinedHazardMaterial(MiningElementalAffinity affinity)
{
    switch (affinity) {
    case MiningElementalAffinity::Thermal:
    case MiningElementalAffinity::Cryo:
    case MiningElementalAffinity::None:
        return MiningCellMaterial::CommonOre;
    case MiningElementalAffinity::Toxic:
        return MiningCellMaterial::RareOre;
    case MiningElementalAffinity::Radiation:
        return MiningCellMaterial::ExoticVein;
    }
    return MiningCellMaterial::CommonOre;
}

int completeHazardTreatment(
    GameState& state,
    const MiningMiniDroneAgent& agent,
    const HazardDroneCoordinator& coordinator)
{
    MiningRunState& mining = state.run.mining;
    const MiningArenaRules arenaRules = activeMiningArenaRules(mining);
    const bool thermalOnly =
        mining.miningSiteBiome == MiningSiteBiome::ThermalLava;
    struct Candidate {
        int x = 0;
        int y = 0;
        int requiredMark = 1;
        int distance = 0;
    };
    std::vector<Candidate> candidates;
    for (int y = agent.targetCellY - 1; y <= agent.targetCellY + 1; ++y) {
        for (int x = agent.targetCellX - 1; x <= agent.targetCellX + 1; ++x) {
            MiningCell* cell = miningCellAt(mining.terrain, x, y);
            if (cell == nullptr || !cell->revealed || !cell->hazard ||
                cell->material != MiningCellMaterial::HazardPocket ||
                cocoonRequiredHazardMark(mining, *cell) > agent.upgradeLevel ||
                coordinator.reservedByOther(x, y, agent)) {
                continue;
            }
            candidates.push_back({
                x,
                y,
                cocoonRequiredHazardMark(mining, *cell),
                std::abs(x - agent.targetCellX) + std::abs(y - agent.targetCellY)
            });
        }
    }
    std::sort(candidates.begin(), candidates.end(), [&](const Candidate& lhs, const Candidate& rhs) {
        const bool lhsTarget = lhs.x == agent.targetCellX && lhs.y == agent.targetCellY;
        const bool rhsTarget = rhs.x == agent.targetCellX && rhs.y == agent.targetCellY;
        if (lhsTarget != rhsTarget) {
            return lhsTarget;
        }
        if (lhs.requiredMark != rhs.requiredMark) {
            return lhs.requiredMark > rhs.requiredMark;
        }
        if (lhs.distance != rhs.distance) {
            return lhs.distance < rhs.distance;
        }
        return lhs.y == rhs.y ? lhs.x < rhs.x : lhs.y < rhs.y;
    });

    const int batchSize = std::min(
        tuning::mining::hazardDroneBatchSize(agent.upgradeLevel),
        static_cast<int>(candidates.size()));
    int refinedCount = 0;
    for (int index = 0; index < batchSize; ++index) {
        const Candidate& candidate = candidates[static_cast<std::size_t>(index)];
        MiningCell* cell = miningCellAt(mining.terrain, candidate.x, candidate.y);
        if (cell == nullptr || cell->material != MiningCellMaterial::HazardPocket) {
            continue;
        }
        const MiningElementalAffinity affinity = cell->hazardAffinity;
        const MiningCellFeature feature = cell->feature;
        const MiningEnemyType enemy = cell->enemy;
        const bool cocoonCell = cell->cocoonLayer >= 0;
        const bool refined = (thermalOnly && affinity == MiningElementalAffinity::Thermal) ||
            cocoonCell ||
            unitHash(
                activeMiningArenaSeed(mining),
                candidate.x,
                candidate.y,
                mining.depthZone,
                0x48415A415244ULL + static_cast<std::uint64_t>(agent.upgradeLevel)) <
                tuning::mining::hazardDroneRefinementChance(agent.upgradeLevel);
        MiningCellMaterial refinedMaterial = refined ? refinedHazardMaterial(affinity) : MiningCellMaterial::Regolith;
        if (!miningMaterialAllowed(arenaRules, refinedMaterial)) {
            refinedMaterial = MiningCellMaterial::CommonOre;
        }
        const bool gateAssociated = cell->gateAssociated;
        const int cocoonLayer = cell->cocoonLayer;
        *cell = makeCell(refinedMaterial, mining.depthZone);
        cell->revealed = true;
        cell->feature = feature;
        cell->enemy = enemy;
        cell->gateAssociated = gateAssociated;
        cell->cocoonLayer = cocoonLayer;
        markDirty(mining.terrain, candidate.x, candidate.y);
        if (gateAssociated) {
            markMiningGateDerivedStateDirty(mining);
        }
        refinedCount += refined ? 1 : 0;
    }
    if (batchSize > 0) {
        state.statusLine = thermalOnly
            ? "Hazard Drone cooled " + std::to_string(batchSize) + " thermal " +
                (batchSize == 1 ? "seam into gray Common Ore." : "seams into gray Common Ore.")
            : (refinedCount > 0
            ? "Hazard Drone stabilized " + std::to_string(batchSize) + " tiles and refined " + std::to_string(refinedCount) +
                (refinedCount == 1 ? " valuable tile." : " valuable tiles.")
            : "Hazard Drone stabilized " + std::to_string(batchSize) + " hazardous tile" + (batchSize == 1 ? "." : "s."));
    }
    return batchSize;
}

void advanceHazardDroneTreatments(
    GameState& state,
    HazardDroneCoordinator& coordinator,
    const MiniDroneLoadoutEffects& loadoutEffects,
    double dt)
{
    for (const auto& [targetX, targetY] : coordinator.assignedTargets()) {
        std::vector<MiningMiniDroneAgent*> workers =
            coordinator.assignedWorkers(targetX, targetY);
        if (workers.empty()) {
            continue;
        }
        const int activeWorkers = static_cast<int>(std::count_if(
            workers.begin(),
            workers.end(),
            [](const MiningMiniDroneAgent* worker) {
                return worker != nullptr &&
                    worker->behavior == MiningMiniDroneBehavior::Working;
            }));
        if (activeWorkers <= 0) {
            continue;
        }

        MiningMiniDroneAgent* leader = workers.front();
        if (leader == nullptr) {
            continue;
        }
        double sharedProgress = 0.0;
        for (const MiningMiniDroneAgent* worker : workers) {
            if (worker != nullptr) {
                sharedProgress = std::max(
                    sharedProgress,
                    worker->taskProgressSeconds);
            }
        }
        sharedProgress += dt * static_cast<double>(activeWorkers);
        for (MiningMiniDroneAgent* worker : workers) {
            if (worker != nullptr) {
                worker->taskProgressSeconds = sharedProgress;
            }
        }

        const double treatmentSeconds =
            tuning::mining::hazardDroneTreatmentSeconds(leader->upgradeLevel) /
            (1.0 + loadoutEffects.hazardTreatmentRateBonus);
        if (sharedProgress < treatmentSeconds) {
            continue;
        }
        completeHazardTreatment(state, *leader, coordinator);
        coordinator.releaseTargetAssignments(targetX, targetY);
    }
}

void ensureMiningMiniDroneAgents(GameState& state, const ContentCatalog& catalog)
{
    MiningRunState& mining = state.run.mining;
    std::vector<std::pair<MiniDroneRole, int>> expected;
    expected.reserve(state.meta.equippedDroneIds.size());
    for (const std::string& droneId : state.meta.equippedDroneIds) {
        const MiniDrone* drone = catalog.findMiniDrone(droneId);
        if (drone != nullptr && isMiniDroneUnlocked(state.meta, *drone)) {
            expected.push_back({drone->role, miniDroneUpgradeLevel(state, drone->id)});
        }
    }
    bool matches = mining.miniDrones.size() == expected.size();
    for (std::size_t i = 0; matches && i < expected.size(); ++i) {
        matches = mining.miniDrones[i].role == expected[i].first && mining.miniDrones[i].upgradeLevel == expected[i].second;
    }
    if (matches) {
        return;
    }

    mining.miniDrones.clear();
    int roleIndices[6] = {};
    for (const auto& [role, upgradeLevel] : expected) {
        MiningMiniDroneAgent agent;
        agent.role = role;
        agent.roleIndex = roleIndices[static_cast<int>(role)]++;
        agent.anchorTarget = MiningAnchorTarget::ControlledActor;
        agent.stableFormationSlot = agent.roleIndex;
        agent.orbitPhaseRadians =
            static_cast<double>(static_cast<int>(role)) * (kPi / 9.0);
        agent.upgradeLevel = std::clamp(upgradeLevel, 1, 3);
        agent.behavior = MiningMiniDroneBehavior::Following;
        mining.miniDrones.push_back(agent);
    }
    for (MiningMiniDroneAgent& agent : mining.miniDrones) {
        const MiniDroneHomePoint home = miniDroneHomePoint(mining, agent);
        agent.x = home.x;
        agent.y = home.y;
        if (agent.role == MiniDroneRole::Defense) {
            const MiniDroneAnchorFrame anchor = resolveMiniDroneAnchor(mining, agent.anchorTarget);
            agent.defenseAngleRadians = std::atan2(home.y - anchor.y, home.x - anchor.x);
            agent.defenseAngleInitialized = true;
        }
    }
}

void updateMiningMiniDroneAgents(GameState& state, const ContentCatalog& catalog, const MiningDrillStats& stats, double dt)
{
    ensureMiningMiniDroneAgents(state, catalog);
    MiningRunState& mining = state.run.mining;
    for (MiningMiniDroneAgent& agent : mining.miniDrones) {
        agent.orbitPhaseRadians = std::fmod(
            agent.orbitPhaseRadians + tuning::mining::miniDroneOrbitRadiansPerSecond * dt,
            kPi * 2.0);
        agent.actionCooldownSeconds = std::max(0.0, agent.actionCooldownSeconds - dt);
        agent.surveyPulseSeconds = std::max(0.0, agent.surveyPulseSeconds - dt);
    }
    MiningDroneCoordinator miningCoordinator(mining);
    miningCoordinator.synchronizeAssignments();
    HazardDroneCoordinator hazardCoordinator(mining);
    hazardCoordinator.synchronizeAssignments();
    hazardCoordinator.assignAvailableDrones();
    AttackDroneCoordinator attackCoordinator(mining);
    attackCoordinator.synchronizeAssignments();
    DefenseDroneCoordinator defenseCoordinator(mining);
    defenseCoordinator.synchronizeAssignments();
    defenseCoordinator.advanceFormation(dt);
    SurveyDroneCoordinator surveyCoordinator(mining);
    surveyCoordinator.synchronizeAssignments();
    const MiniDroneLoadoutEffects loadoutEffects = miniDroneLoadoutEffects(state, catalog);

    for (MiningMiniDroneAgent& agent : mining.miniDrones) {
        const MiniDroneAnchorFrame anchor = resolveMiniDroneAnchor(mining, agent.anchorTarget);
        const double anchorDistance = anchor.valid
            ? std::hypot(agent.x - anchor.x, agent.y - anchor.y)
            : 0.0;
        const double returnSpeed = anchorDistance > tuning::mining::miniDroneCatchUpDistanceCells
            ? tuning::mining::miniDroneCatchUpSpeedCellsPerSecond
            : tuning::mining::miniDroneReturnSpeedCellsPerSecond;
        const bool hardRecall =
            anchor.valid &&
            anchorDistance > tuning::mining::miningDroneLeashRadiusCells &&
            agent.role != MiniDroneRole::Hazard;
        if (hardRecall) {
            agent.targetCellX = -1;
            agent.targetCellY = -1;
            agent.targetEnemyIndex = -1;
            agent.taskProgressSeconds = 0.0;
            agent.finishTargetBeforeReturn = false;
            agent.behavior = MiningMiniDroneBehavior::Returning;
        }
        const MiniDroneHomePoint home = miniDroneHomePoint(mining, agent);
        if (hardRecall) {
            moveMiniDroneTowardOpenPoint(
                agent,
                mining,
                home.x,
                home.y,
                returnSpeed,
                dt,
                MiniDroneArrivalStyle::SmoothFormation);
            continue;
        }
        switch (agent.role) {
        case MiniDroneRole::Mining: {
            const double mamaDistanceSq = miniDroneDistanceSquared(agent, anchor.x, anchor.y);
            const double leashSq = tuning::mining::miningDroneLeashRadiusCells * tuning::mining::miningDroneLeashRadiusCells;
            const int capacity = tuning::mining::miningDroneCapacityChunks(agent.upgradeLevel);
            int carriedChunks = miniDroneHaulChunkCount(agent);
            const MiniDroneHomePoint dock = miniDroneShipDockPoint(mining, agent);
            const bool deliveryAllowed = controlledActorAtReturnZone(mining);

            if (carriedChunks >= capacity && miningCoordinator.hasAssignment(agent)) {
                miningCoordinator.releaseAssignment(agent);
                agent.finishTargetBeforeReturn = false;
                agent.behavior = MiningMiniDroneBehavior::Traveling;
            }

            const bool deliveryInProgress = carriedChunks > 0 &&
                !miningCoordinator.hasAssignment(agent) &&
                (agent.behavior == MiningMiniDroneBehavior::Traveling ||
                    agent.behavior == MiningMiniDroneBehavior::Docked);
            if ((carriedChunks >= capacity || deliveryInProgress) && !deliveryAllowed) {
                miningCoordinator.releaseAssignment(agent);
                agent.behavior = MiningMiniDroneBehavior::Returning;
                moveMiniDroneToward(
                    agent,
                    mining,
                    home.x,
                    home.y,
                    returnSpeed,
                    dt,
                    MiniDroneArrivalStyle::SmoothFormation);
                break;
            }
            if (carriedChunks >= capacity || deliveryInProgress) {
                if (agent.behavior != MiningMiniDroneBehavior::Docked) {
                    agent.behavior = MiningMiniDroneBehavior::Traveling;
                    if (moveMiniDroneToward(
                            agent,
                            mining,
                            dock.x,
                            dock.y,
                            tuning::mining::miniDroneTravelSpeedCellsPerSecond,
                            dt)) {
                        agent.behavior = MiningMiniDroneBehavior::Docked;
                        agent.actionCooldownSeconds = std::max(
                            agent.actionCooldownSeconds,
                            tuning::mining::miningDroneDropoffSeconds);
                    }
                    break;
                }
                slowMiniDroneAtTask(agent, mining, dt);
                if (agent.actionCooldownSeconds <= 0.0 && unloadOneMiniDroneChunk(mining, agent)) {
                    agent.actionCooldownSeconds = tuning::mining::miningDroneDropoffSeconds;
                    carriedChunks = miniDroneHaulChunkCount(agent);
                }
                if (carriedChunks <= 0) {
                    agent.behavior = MiningMiniDroneBehavior::Returning;
                    agent.actionCooldownSeconds = 0.0;
                }
                break;
            }

            if (carriedChunks < capacity &&
                agent.actionCooldownSeconds <= 0.0 &&
                loadOneNearbyLooseChunk(
                    mining,
                    agent,
                    tuning::mining::miningDroneWorkRangeCells)) {
                carriedChunks = miniDroneHaulChunkCount(agent);
                agent.actionCooldownSeconds = 0.15;
                agent.behavior = MiningMiniDroneBehavior::Returning;
                if (carriedChunks >= capacity) {
                    miningCoordinator.releaseAssignment(agent);
                }
                break;
            }

            if (miningCoordinator.hasAssignment(agent)) {
                if (mamaDistanceSq > leashSq) {
                    agent.finishTargetBeforeReturn = true;
                }
                const double targetX = static_cast<double>(agent.targetCellX) + 0.5;
                const double targetY = static_cast<double>(agent.targetCellY) + 0.5;
                const double workRangeSq = tuning::mining::miningDroneWorkRangeCells * tuning::mining::miningDroneWorkRangeCells;
                if (miniDroneDistanceSquared(agent, targetX, targetY) > workRangeSq) {
                    agent.behavior = MiningMiniDroneBehavior::Traveling;
                    if (!moveMiniDroneTowardTask(
                            agent,
                            mining,
                            agent.targetCellX,
                            agent.targetCellY,
                            tuning::mining::miningDroneWorkRangeCells,
                            tuning::mining::miniDroneTravelSpeedCellsPerSecond,
                            dt)) {
                        miningCoordinator.releaseAssignment(agent);
                        agent.behavior = MiningMiniDroneBehavior::Returning;
                    }
                } else {
                    slowMiniDroneAtTask(agent, mining, dt);
                    agent.behavior = MiningMiniDroneBehavior::Working;
                    const MiningCell* target = miningCellAt(mining.terrain, agent.targetCellX, agent.targetCellY);
                    const MiningCellMaterial targetMaterial = target != nullptr
                        ? target->material
                        : MiningCellMaterial::Empty;
                    agent.taskProgressSeconds += dt;
                    if (agent.taskProgressSeconds >=
                            tuning::mining::miningDroneWorkSeconds(agent.upgradeLevel, targetMaterial) &&
                        completeBrokenMiningCell(
                            state,
                            stats,
                            agent.targetCellX,
                            agent.targetCellY,
                            &agent.haulMaterials)) {
                        miningCoordinator.releaseAssignment(agent);
                        carriedChunks = miniDroneHaulChunkCount(agent);
                        if (carriedChunks >= capacity) {
                            agent.finishTargetBeforeReturn = false;
                            agent.behavior = MiningMiniDroneBehavior::Traveling;
                        } else if (agent.finishTargetBeforeReturn || mamaDistanceSq > leashSq) {
                            agent.behavior = MiningMiniDroneBehavior::Returning;
                        } else {
                            agent.behavior = MiningMiniDroneBehavior::Following;
                        }
                    }
                }
                break;
            }

            miningCoordinator.releaseAssignment(agent);
            const double reacquireSq = tuning::mining::miningDroneReacquireRadiusCells * tuning::mining::miningDroneReacquireRadiusCells;
            if (agent.finishTargetBeforeReturn || mamaDistanceSq > reacquireSq) {
                agent.behavior = MiningMiniDroneBehavior::Returning;
                if (moveMiniDroneToward(agent, mining, home.x, home.y, returnSpeed, dt)) {
                    agent.finishTargetBeforeReturn = false;
                    agent.behavior = MiningMiniDroneBehavior::Following;
                }
            } else if (agent.actionCooldownSeconds > 0.0) {
                agent.behavior = MiningMiniDroneBehavior::Following;
                moveMiniDroneToward(agent, mining, home.x, home.y, returnSpeed, dt);
            } else if (!miningCoordinator.acquireAssignment(agent)) {
                if (carriedChunks > 0) {
                    agent.behavior = MiningMiniDroneBehavior::Traveling;
                } else {
                    agent.actionCooldownSeconds = 0.45;
                    agent.behavior = MiningMiniDroneBehavior::Following;
                    moveMiniDroneToward(agent, mining, home.x, home.y, returnSpeed, dt);
                }
            }
            break;
        }
        case MiniDroneRole::Attack:
            if (attackCoordinator.hasAssignment(agent) || attackCoordinator.acquireAssignment(agent)) {
                const MiniDroneCoordinationPoint formation = attackCoordinator.formationPoint(agent);
                agent.behavior = MiningMiniDroneBehavior::Engaging;
                if (!moveMiniDroneToward(agent, mining, formation.x, formation.y, tuning::mining::miniDroneTravelSpeedCellsPerSecond, dt)) {
                    break;
                }
                slowMiniDroneAtTask(agent, mining, dt);
            } else {
                agent.behavior = MiningMiniDroneBehavior::Returning;
                const bool arrivedHome = moveMiniDroneToward(
                    agent,
                    mining,
                    home.x,
                    home.y,
                    returnSpeed,
                    dt,
                    MiniDroneArrivalStyle::SmoothFormation);
                keepMiniDroneOutsideRigPerimeter(agent, mining, home.x, home.y);
                if (arrivedHome) {
                    agent.behavior = MiningMiniDroneBehavior::Following;
                }
            }
            break;
        case MiniDroneRole::Defense: {
            const MiniDroneCoordinationPoint guard = defenseCoordinator.formationPoint(agent);
            moveMiniDroneToward(
                agent,
                mining,
                guard.x,
                guard.y,
                returnSpeed,
                dt,
                MiniDroneArrivalStyle::SmoothFormation);
            break;
        }
        case MiniDroneRole::Resource: {
            const double transferInterval = resourceDroneTransferInterval(agent);
            const MiniDroneHomePoint dock = miniDroneShipDockPoint(mining, agent);
            int carriedChunks = miniDroneHaulChunkCount(agent);
            const bool shouldDeliver = controlledActorAtReturnZone(mining) && carriedChunks > 0 &&
                (agent.behavior == MiningMiniDroneBehavior::Traveling ||
                    agent.behavior == MiningMiniDroneBehavior::Docked ||
                    carriedChunks >= tuning::mining::resourceDroneCapacityChunks ||
                    !resourceDroneHasCollectibleMaterial(mining, agent));
            if (shouldDeliver) {
                if (agent.behavior != MiningMiniDroneBehavior::Docked) {
                    agent.behavior = MiningMiniDroneBehavior::Traveling;
                    if (moveMiniDroneToward(agent, mining, dock.x, dock.y, tuning::mining::miniDroneTravelSpeedCellsPerSecond, dt)) {
                        agent.behavior = MiningMiniDroneBehavior::Docked;
                        agent.actionCooldownSeconds = std::max(agent.actionCooldownSeconds, transferInterval);
                    }
                    break;
                }
                slowMiniDroneAtTask(agent, mining, dt);
                if (agent.actionCooldownSeconds <= 0.0 && unloadOneMiniDroneChunk(mining, agent)) {
                    agent.actionCooldownSeconds = transferInterval;
                    carriedChunks = miniDroneHaulChunkCount(agent);
                }
                if (carriedChunks <= 0) {
                    agent.behavior = MiningMiniDroneBehavior::Returning;
                    agent.actionCooldownSeconds = 0.0;
                }
                break;
            }

            const MiningMiniDroneBehavior previousBehavior = agent.behavior;
            moveMiniDroneToward(
                agent,
                mining,
                home.x,
                home.y,
                returnSpeed,
                dt,
                MiniDroneArrivalStyle::SmoothFormation);
            const double collectionTolerance = previousBehavior == MiningMiniDroneBehavior::Working
                ? tuning::mining::resourceDroneCollectionExitToleranceCells
                : tuning::mining::resourceDroneCollectionEnterToleranceCells;
            const bool inCollectionPosition = miniDroneDistanceSquared(agent, home.x, home.y) <=
                collectionTolerance * collectionTolerance;
            if (!resourceDroneHasCollectibleMaterial(mining, agent) ||
                carriedChunks >= tuning::mining::resourceDroneCapacityChunks) {
                agent.behavior = MiningMiniDroneBehavior::Following;
                break;
            }
            if (!inCollectionPosition) {
                agent.behavior = MiningMiniDroneBehavior::Returning;
                break;
            }
            if (previousBehavior != MiningMiniDroneBehavior::Working) {
                agent.actionCooldownSeconds = std::max(agent.actionCooldownSeconds, transferInterval);
            }
            agent.behavior = MiningMiniDroneBehavior::Working;
            if (agent.actionCooldownSeconds <= 0.0 && loadOneResourceChunk(mining, agent)) {
                agent.actionCooldownSeconds = transferInterval;
                carriedChunks = miniDroneHaulChunkCount(agent);
                if (carriedChunks >= tuning::mining::resourceDroneCapacityChunks ||
                    !resourceDroneHasCollectibleMaterial(mining, agent)) {
                    agent.behavior = MiningMiniDroneBehavior::Traveling;
                }
            }
            break;
        }
        case MiniDroneRole::Survey: {
            if (agent.actionCooldownSeconds > 0.0 || agent.behavior == MiningMiniDroneBehavior::Returning) {
                agent.behavior = MiningMiniDroneBehavior::Returning;
                const bool atHome = moveMiniDroneToward(
                    agent,
                    mining,
                    home.x,
                    home.y,
                    returnSpeed,
                    dt,
                    MiniDroneArrivalStyle::DeliberateSurvey);
                if (!atHome || agent.actionCooldownSeconds > 0.0) {
                    break;
                }
                agent.behavior = MiningMiniDroneBehavior::Following;
            }
            if (surveyCoordinator.hasAssignment(agent) || surveyCoordinator.acquireAssignment(agent)) {
                const double targetX = static_cast<double>(agent.targetCellX) + 0.5;
                const double targetY = static_cast<double>(agent.targetCellY) + 0.5;
                const double arrivalSq = tuning::mining::surveyDroneScanArrivalRadiusCells *
                    tuning::mining::surveyDroneScanArrivalRadiusCells;
                if (miniDroneDistanceSquared(agent, targetX, targetY) > arrivalSq) {
                    agent.behavior = MiningMiniDroneBehavior::Traveling;
                    moveMiniDroneToward(
                        agent,
                        mining,
                        targetX,
                        targetY,
                        tuning::mining::surveyDroneTravelSpeedCellsPerSecond,
                        dt,
                        MiniDroneArrivalStyle::DeliberateSurvey);
                    break;
                }
                slowMiniDroneAtTask(agent, mining, dt);
                agent.behavior = MiningMiniDroneBehavior::Scouting;
                agent.taskProgressSeconds += dt;
                if (agent.taskProgressSeconds < tuning::mining::surveyDroneScanDwellSeconds) {
                    break;
                }
                revealAround(mining, agent.x, agent.y, tuning::mining::surveyDroneScanRadiusCells);
                agent.surveyPulseSeconds = tuning::mining::surveyDronePulseSeconds;
                surveyCoordinator.releaseAssignment(agent);
                agent.actionCooldownSeconds = tuning::mining::surveyDroneRechargeSeconds;
                break;
            }
            agent.behavior = MiningMiniDroneBehavior::Returning;
            if (anchor.valid &&
                miniDroneTaskPathLength(
                    mining,
                    agent,
                    static_cast<int>(std::floor(anchor.x)),
                    static_cast<int>(std::floor(anchor.y)),
                    0.0) < 0) {
                agent.x = anchor.x;
                agent.y = anchor.y;
                agent.velocityX = 0.0;
                agent.velocityY = 0.0;
                agent.behavior = MiningMiniDroneBehavior::Following;
                break;
            }
            if (moveMiniDroneTowardOpenPoint(
                    agent,
                    mining,
                    home.x,
                    home.y,
                    returnSpeed,
                    dt,
                    MiniDroneArrivalStyle::DeliberateSurvey)) {
                agent.behavior = MiningMiniDroneBehavior::Scouting;
                revealAround(mining, agent.x, agent.y, tuning::mining::surveyDroneScanRadiusCells);
                agent.surveyPulseSeconds = tuning::mining::surveyDronePulseSeconds;
                agent.actionCooldownSeconds = tuning::mining::surveyDroneRechargeSeconds;
            }
            break;
        }
        case MiniDroneRole::Hazard: {
            if (hazardCoordinator.hasAssignment(agent)) {
                const MiniDroneCoordinationPoint approach =
                    hazardCoordinator.treatmentApproachPoint(agent);
                if (!moveHazardDroneDirect(
                        agent,
                        mining,
                        approach.x,
                        approach.y,
                        tuning::mining::miniDroneTravelSpeedCellsPerSecond,
                        dt)) {
                    agent.behavior = MiningMiniDroneBehavior::Traveling;
                    break;
                }
                slowMiniDroneAtTask(agent, mining, dt);
                agent.behavior = MiningMiniDroneBehavior::Working;
                break;
            }
            agent.behavior = MiningMiniDroneBehavior::Returning;
            if (moveHazardDroneDirect(
                    agent,
                    mining,
                    home.x,
                    home.y,
                    returnSpeed,
                    dt,
                    MiniDroneArrivalStyle::SmoothFormation)) {
                agent.behavior = MiningMiniDroneBehavior::Following;
            }
            break;
        }
        }
    }

    advanceHazardDroneTreatments(
        state,
        hazardCoordinator,
        loadoutEffects,
        dt);

    const double separation = tuning::mining::miniDroneSeparationRadiusCells;
    for (std::size_t lhsIndex = 0; lhsIndex < mining.miniDrones.size(); ++lhsIndex) {
        for (std::size_t rhsIndex = lhsIndex + 1; rhsIndex < mining.miniDrones.size(); ++rhsIndex) {
            MiningMiniDroneAgent& lhs = mining.miniDrones[lhsIndex];
            MiningMiniDroneAgent& rhs = mining.miniDrones[rhsIndex];
            double dx = rhs.x - lhs.x;
            double dy = rhs.y - lhs.y;
            double distance = std::hypot(dx, dy);
            if (distance >= separation) {
                continue;
            }
            if (distance <= 0.0001) {
                const double angle = static_cast<double>(lhsIndex + rhsIndex + 1) * 1.61803398875;
                dx = std::cos(angle);
                dy = std::sin(angle);
                distance = 1.0;
            }
            const double push = (separation - distance) * 0.5;
            const double normalX = dx / distance;
            const double normalY = dy / distance;
            const double lhsX = lhs.x - normalX * push;
            const double lhsY = lhs.y - normalY * push;
            const double rhsX = rhs.x + normalX * push;
            const double rhsY = rhs.y + normalY * push;
            if (lhs.role == MiniDroneRole::Hazard ||
                canOccupyActor(
                    mining.terrain,
                    lhsX,
                    lhsY,
                    tuning::mining::miniDroneColliderRadiusCells,
                    true)) {
                lhs.x = lhsX;
                lhs.y = lhsY;
            }
            if (rhs.role == MiniDroneRole::Hazard ||
                canOccupyActor(
                    mining.terrain,
                    rhsX,
                    rhsY,
                    tuning::mining::miniDroneColliderRadiusCells,
                    true)) {
                rhs.x = rhsX;
                rhs.y = rhsY;
            }
        }
    }
    for (MiningMiniDroneAgent& agent : mining.miniDrones) {
        if (agent.role != MiniDroneRole::Attack) {
            continue;
        }
        const MiniDroneHomePoint home = miniDroneHomePoint(mining, agent);
        keepMiniDroneOutsideRigPerimeter(agent, mining, home.x, home.y);
    }
}

struct DrillFootprintCell {
    int x = 0;
    int y = 0;
    double powerScale = 1.0;
};

std::vector<DrillFootprintCell> drillFootprintCells(const MiningRunState& mining, double dirX, double dirY)
{
    const double length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length < 0.001) {
        return {};
    }
    dirX /= length;
    dirY /= length;

    const bool suit = operatorControlled(mining);
    const double footprintLength = suit
        ? tuning::mining::operatorDrillRangeCells
        : tuning::mining::drillRangeCells + 0.95;
    const double baseHalfWidth = suit ? 0.30 : 0.95;
    const double tipHalfWidth = suit ? 0.16 : 0.24;
    const double originX = controlledActorX(mining) + dirX * (suit ? 0.12 : 0.25);
    const double originY = controlledActorY(mining) + dirY * (suit ? 0.12 : 0.25);
    const int minX = std::max(0, static_cast<int>(std::floor(originX - footprintLength - baseHalfWidth)));
    const int maxX = std::min(mining.terrain.width - 1, static_cast<int>(std::ceil(originX + footprintLength + baseHalfWidth)));
    const int minY = std::max(0, static_cast<int>(std::floor(originY - footprintLength - baseHalfWidth)));
    const int maxY = std::min(mining.terrain.height - 1, static_cast<int>(std::ceil(originY + footprintLength + baseHalfWidth)));

    std::vector<DrillFootprintCell> cells;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const MiningCell* cell = miningCellAt(mining.terrain, x, y);
            if (!drillableCell(cell)) {
                continue;
            }

            const double cellX = static_cast<double>(x) + 0.5;
            const double cellY = static_cast<double>(y) + 0.5;
            const double vx = cellX - originX;
            const double vy = cellY - originY;
            const double along = vx * dirX + vy * dirY;
            if (along < -0.10 || along > footprintLength) {
                continue;
            }

            const double cross = std::abs(vx * dirY - vy * dirX);
            const double t = std::clamp(along / footprintLength, 0.0, 1.0);
            const double halfWidth = baseHalfWidth + (tipHalfWidth - baseHalfWidth) * t + 0.18;
            if (cross > halfWidth) {
                continue;
            }

            const double centerBias = 1.0 - std::clamp(cross / std::max(0.001, halfWidth), 0.0, 1.0);
            cells.push_back({x, y, 0.48 + centerBias * 0.52});
        }
    }

    std::sort(cells.begin(), cells.end(), [&](const DrillFootprintCell& lhs, const DrillFootprintCell& rhs) {
        const double lhsX = static_cast<double>(lhs.x) + 0.5 - originX;
        const double lhsY = static_cast<double>(lhs.y) + 0.5 - originY;
        const double rhsX = static_cast<double>(rhs.x) + 0.5 - originX;
        const double rhsY = static_cast<double>(rhs.y) + 0.5 - originY;
        const double lhsAlong = lhsX * dirX + lhsY * dirY;
        const double rhsAlong = rhsX * dirX + rhsY * dirY;
        if (std::abs(lhsAlong - rhsAlong) > 0.001) {
            return lhsAlong < rhsAlong;
        }
        const double lhsCross = std::abs(lhsX * dirY - lhsY * dirX);
        const double rhsCross = std::abs(rhsX * dirY - rhsY * dirX);
        return lhsCross < rhsCross;
    });
    return cells;
}

bool applyDrillFootprintDamage(GameState& state, const MiningDrillStats& stats, double dirX, double dirY, double dt)
{
    MiningRunState& mining = state.run.mining;
    const MiningDrillStats& actorStats = stats;
    const MiningArenaRules arenaRules = activeMiningArenaRules(mining);
    const std::vector<DrillFootprintCell> cells = drillFootprintCells(mining, dirX, dirY);
    if (cells.empty()) {
        return false;
    }

    bool touchedHardMaterial = false;
    bool touchedSoftMaterial = false;
    bool brokeAny = false;
    double maxHeatDelta = 0.0;
    double maxIntegrityExposure = 0.0;
    for (const DrillFootprintCell& contact : cells) {
        const MiningCell* cell = miningCellAt(mining.terrain, contact.x, contact.y);
        if (cell == nullptr) {
            continue;
        }
        const double contactDt = dt * contact.powerScale;
        touchedSoftMaterial = touchedSoftMaterial || softMiningMaterial(cell->material);
        touchedHardMaterial = touchedHardMaterial || !softMiningMaterial(cell->material);
        double heatDelta = arenaRules.mechanics.drillHeat ? drillHeatDelta(cell->material, actorStats, contactDt) : 0.0;
        maxHeatDelta = std::max(maxHeatDelta, heatDelta);
        if (arenaRules.mechanics.drillIntegrity) {
            maxIntegrityExposure = std::max(maxIntegrityExposure, contactDt);
        }
        brokeAny = applyDrillDamage(state, actorStats, contact.x, contact.y, contactDt) || brokeAny;
    }

    if (arenaRules.mechanics.drillHeat || arenaRules.mechanics.drillIntegrity) {
        applyDrillSystemLoad(mining, actorStats, maxHeatDelta, maxIntegrityExposure);
    }
    mining.contactIntensity = std::max(mining.contactIntensity, touchedHardMaterial ? 0.82 : 0.35);
    mining.recoilX = -dirX;
    mining.recoilY = -dirY;
    if (arenaRules.mechanics.contactRebound && touchedHardMaterial && !touchedSoftMaterial) {
        triggerHardContactBounce(mining, dirX, dirY, stats.hardRockBounceRelief);
    }
    return brokeAny || !cells.empty();
}

struct EnvironmentalHazardExposure {
    double thermal = 0.0;
    double cryo = 0.0;
    double toxic = 0.0;
    double radiation = 0.0;

    bool active() const
    {
        return thermal > 0.0 || cryo > 0.0 || toxic > 0.0 || radiation > 0.0;
    }
};

void recordEnvironmentalHazardExposure(
    EnvironmentalHazardExposure& exposure,
    MiningElementalAffinity affinity,
    double seconds)
{
    switch (affinity) {
    case MiningElementalAffinity::Thermal:
        exposure.thermal = std::max(exposure.thermal, seconds);
        break;
    case MiningElementalAffinity::Cryo:
        exposure.cryo = std::max(exposure.cryo, seconds);
        break;
    case MiningElementalAffinity::Toxic:
        exposure.toxic = std::max(exposure.toxic, seconds);
        break;
    case MiningElementalAffinity::Radiation:
        exposure.radiation = std::max(exposure.radiation, seconds);
        break;
    case MiningElementalAffinity::None:
        break;
    }
}

MiningElementalAffinity applyEnvironmentalHazardExposure(
    GameState& state,
    const ContentCatalog& catalog,
    const MiningArenaRules& arenaRules,
    bool drillTouchesTerrain,
    double dt)
{
    MiningRunState& mining = state.run.mining;
    if (!arenaRules.mechanics.environmentalHazards) {
        return MiningElementalAffinity::None;
    }

    EnvironmentalHazardExposure exposure;
    auto recordCell = [&](const MiningCell* cell, double seconds) {
        if (cell != nullptr && cell->material == MiningCellMaterial::HazardPocket && cell->hazard) {
            recordEnvironmentalHazardExposure(exposure, cell->hazardAffinity, seconds);
        }
    };

    if (drillTouchesTerrain) {
        for (const DrillFootprintCell& contact : drillFootprintCells(mining, mining.aimDirX, mining.aimDirY)) {
            recordCell(miningCellAt(mining.terrain, contact.x, contact.y), dt * contact.powerScale);
        }
    }

    const double actorX = controlledActorX(mining);
    const double actorY = controlledActorY(mining);
    const double radius = tuning::mining::hazardPocketExposureRadiusCells;
    const int minX = std::max(0, static_cast<int>(std::floor(actorX - radius)));
    const int maxX = std::min(mining.terrain.width - 1, static_cast<int>(std::floor(actorX + radius)));
    const int minY = std::max(0, static_cast<int>(std::floor(actorY - radius)));
    const int maxY = std::min(mining.terrain.height - 1, static_cast<int>(std::floor(actorY + radius)));
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const MiningCell* cell = miningCellAt(mining.terrain, x, y);
            if (cell == nullptr || cell->material != MiningCellMaterial::HazardPocket || !cell->hazard) {
                continue;
            }
            const double dx = static_cast<double>(x) + 0.5 - actorX;
            const double dy = static_cast<double>(y) + 0.5 - actorY;
            const double distance = std::sqrt(dx * dx + dy * dy);
            if (distance > radius) {
                continue;
            }
            const double proximityScale = std::max(
                tuning::mining::hazardPocketPeripheralExposureFloor,
                1.0 - (1.0 - tuning::mining::hazardPocketPeripheralExposureFloor) * distance / radius);
            recordCell(cell, dt * proximityScale);
        }
    }

    if (!exposure.active()) {
        return MiningElementalAffinity::None;
    }

    const MiniDroneLoadoutEffects loadout = miniDroneLoadoutEffects(state, catalog);
    const double shieldRelief = std::clamp(loadout.environmentalShieldRelief, 0.0, 0.80);
    const double exposureScale = 1.0 - shieldRelief;
    MiningElementalAffinity activeAffinity = MiningElementalAffinity::None;
    if (exposure.thermal > 0.0) {
        const double thermalSeconds = exposure.thermal * exposureScale;
        const double thermalDamage = tuning::mining::elementalThermalHullDamagePerSecond * thermalSeconds;
        mining.drillHeat = std::min(1.0, mining.drillHeat + tuning::mining::elementalHeatRisePerSecond * thermalSeconds);
        applyControlledActorDamage(mining, thermalDamage);
        mining.environmentalShieldAbsorbed += tuning::mining::elementalThermalHullDamagePerSecond * exposure.thermal - thermalDamage;
        mining.contactIntensity = std::max(mining.contactIntensity, 0.72);
        activeAffinity = MiningElementalAffinity::Thermal;
    }
    if (exposure.cryo > 0.0) {
        mining.movementSlowSeconds = std::max(
            mining.movementSlowSeconds,
            tuning::mining::elementalCryoSlowDurationSeconds * exposureScale);
        mining.movementSlowScale = std::min(
            mining.movementSlowScale,
            tuning::mining::elementalCryoSlowScale + shieldRelief * 0.24);
        activeAffinity = activeAffinity == MiningElementalAffinity::None ? MiningElementalAffinity::Cryo : activeAffinity;
    }
    if (exposure.toxic > 0.0) {
        mining.drillIntegrity = std::max(
            0.0,
            mining.drillIntegrity - tuning::mining::elementalToxicIntegrityDamagePerSecond * exposure.toxic * exposureScale);
        activeAffinity = activeAffinity == MiningElementalAffinity::None ? MiningElementalAffinity::Toxic : activeAffinity;
    }
    if (exposure.radiation > 0.0) {
        mining.hazardDelta += tuning::mining::elementalRadiationHazardPerSecond * exposure.radiation * exposureScale;
        activeAffinity = activeAffinity == MiningElementalAffinity::None ? MiningElementalAffinity::Radiation : activeAffinity;
    }
    return activeAffinity;
}

void setAimDirection(MiningRunState& mining, double dirX, double dirY)
{
    const double length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length < 0.0001) {
        return;
    }

    if (operatorControlled(mining)) {
        mining.operatorAimDirX = dirX / length;
        mining.operatorAimDirY = dirY / length;
        mining.aimDirX = mining.operatorAimDirX;
        mining.aimDirY = mining.operatorAimDirY;
    } else {
        const double step = (kPi * 2.0) / static_cast<double>(std::max(1, tuning::mining::drillAimDirections));
        const double snapped = std::round(std::atan2(dirY, dirX) / step) * step;
        mining.aimDirX = std::cos(snapped);
        mining.aimDirY = std::sin(snapped);
    }
    mining.aimX = controlledActorX(mining) + controlledAimX(mining) * controlledDrillRange(mining);
    mining.aimY = controlledActorY(mining) + controlledAimY(mining) * controlledDrillRange(mining);
}

bool findNearbyDrillTarget(const MiningRunState& mining, double dirX, double dirY, int& targetX, int& targetY, double& tipX, double& tipY)
{
    const double reach = controlledDrillRange(mining) + (operatorControlled(mining) ? 0.10 : 0.55);
    const double actorX = controlledActorX(mining);
    const double actorY = controlledActorY(mining);
    const int minX = std::max(0, static_cast<int>(std::floor(actorX - reach)));
    const int maxX = std::min(mining.terrain.width - 1, static_cast<int>(std::ceil(actorX + reach)));
    const int minY = std::max(0, static_cast<int>(std::floor(actorY - reach)));
    const int maxY = std::min(mining.terrain.height - 1, static_cast<int>(std::ceil(actorY + reach)));
    double bestScore = 999.0;
    bool found = false;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const MiningCell* cell = miningCellAt(mining.terrain, x, y);
            if (!drillableCell(cell)) {
                continue;
            }

            const double cellX = static_cast<double>(x) + 0.5;
            const double cellY = static_cast<double>(y) + 0.5;
            const double vx = cellX - actorX;
            const double vy = cellY - actorY;
            const double distance = std::sqrt(vx * vx + vy * vy);
            if (distance < 0.25 || distance > reach) {
                continue;
            }

            const double along = vx * dirX + vy * dirY;
            if (along < 0.10 || along > reach) {
                continue;
            }

            const double cross = std::abs(vx * dirY - vy * dirX);
            if (cross > 1.10) {
                continue;
            }

            const double overshoot = std::max(0.0, along - controlledDrillRange(mining));
            const double score = cross + overshoot * 0.65 + distance * 0.03;
            if (score < bestScore) {
                bestScore = score;
                targetX = x;
                targetY = y;
                tipX = static_cast<double>(x) + 0.5 - dirX * 0.48;
                tipY = static_cast<double>(y) + 0.5 - dirY * 0.48;
                found = true;
            }
        }
    }

    return found;
}

std::string artifactId(const MiningRunState& mining)
{
    std::ostringstream out;
    out << mining.destinationId << "_mining_artifact_" << mining.depthZone << "_" << mining.temporaryArtifacts.size();
    return out.str();
}

bool recoverableArtifactState(MiningArtifactState state)
{
    return state == MiningArtifactState::Embedded || state == MiningArtifactState::Loose;
}

ArtifactKind rollMiningArtifactKind(const GameState& state, const Destination& destination, int depthZone)
{
    if (state.meta.ark.condition == ArkCondition::DamagedStranded) {
        const std::uint64_t seed = hashCombine(state.seed, hashString(destination.id));
        if (unitHash(seed, destination.tier, depthZone, 0, 211) < 0.42) {
            return ArtifactKind::Story;
        }
    }
    return ArtifactKind::Boost;
}

ArtifactRewardType rollMiningArtifactReward(const GameState& state, const Destination& destination, ArtifactKind kind, int depthZone)
{
    if (kind == ArtifactKind::Story) {
        return ArtifactRewardType::None;
    }
    const std::uint64_t seed = hashCombine(state.seed, hashString(destination.id));
    const double roll = unitHash(seed, destination.tier, depthZone, 0, 223);
    if (!arkDiscovered(state) && roll < 0.67) {
        return ArtifactRewardType::Credits;
    }
    if (roll < 0.34) {
        return ArtifactRewardType::Credits;
    }
    if (roll < 0.67) {
        return ArtifactRewardType::ArkFuel;
    }
    return ArtifactRewardType::BlueprintInsight;
}

double miningArtifactSpawnChance(const GameState& state, const Destination& destination, SurfaceSiteProfile profile, int depthZone)
{
    (void)state;
    const double depthBonus = std::clamp(static_cast<double>(std::max(0, depthZone)) * 0.035, 0.0, 0.10);
    const double tierBonus = std::clamp(static_cast<double>(destination.tier) * 0.018, 0.0, 0.10);
    const double siteBonus = profile == SurfaceSiteProfile::FractureField ? 0.08 : 0.0;
    return std::min(tuning::mining::artifactMaxSpawnChance, tuning::mining::artifactBaseSpawnChance + depthBonus + tierBonus + siteBonus);
}

bool placeMiningArtifact(GameState& state, MiningRunState& mining, const Destination& destination, bool forced, bool revealed)
{
    if (mining.artifact.present) {
        return true;
    }
    const std::uint64_t seed = hashCombine(state.seed, hashString(destination.id));
    if (!forced && unitHash(seed, destination.tier, mining.depthZone, 0, 197) > miningArtifactSpawnChance(state, destination, mining.siteProfile, mining.depthZone)) {
        return false;
    }

    const int minY = std::max(6, mining.terrain.height / 2);
    const int maxY = std::max(minY, mining.terrain.height - 4);
    auto stampArtifact = [&](int x, int y) {
        MiningCell* cell = miningCellAt(mining.terrain, x, y);
        if (cell == nullptr ||
            cell->material == MiningCellMaterial::Bedrock ||
            miningReturnShaftContains(mining.terrain, x, y) ||
            (!forced && cell->material == MiningCellMaterial::Empty)) {
            return false;
        }
        *cell = makeCell(MiningCellMaterial::ArtifactCache, mining.depthZone);
        cell->revealed = revealed;
        cell->feature = MiningCellFeature::BranchTunnel;
        markDirty(mining.terrain, x, y);

        const ArtifactKind kind = rollMiningArtifactKind(state, destination, mining.depthZone);
        mining.artifact = {};
        mining.artifact.present = true;
        mining.artifact.id = artifactId(mining);
        mining.artifact.kind = kind;
        mining.artifact.rewardType = rollMiningArtifactReward(state, destination, kind, mining.depthZone);
        mining.artifact.state = MiningArtifactState::Embedded;
        mining.artifact.x = static_cast<double>(x) + 0.5;
        mining.artifact.y = static_cast<double>(y) + 0.5;
        mining.artifact.maxHealth = tuning::mining::artifactMaxHealth;
        mining.artifact.health = mining.artifact.maxHealth;
        mining.artifact.embedStrength = 1.0;
        mining.artifact.revealed = revealed;
        return true;
    };

    for (int attempt = 0; attempt < 180; ++attempt) {
        const int x = std::clamp(
            2 + static_cast<int>(unitHash(seed, attempt, mining.depthZone, 0, 229) * static_cast<double>(std::max(1, mining.terrain.width - 4))),
            2,
            std::max(2, mining.terrain.width - 3));
        const int y = std::clamp(
            minY + static_cast<int>(unitHash(seed, attempt, mining.depthZone, 1, 233) * static_cast<double>(std::max(1, maxY - minY + 1))),
            minY,
            maxY);
        if (stampArtifact(x, y)) {
            return true;
        }
    }

    if (forced) {
        for (int y = minY; y <= maxY; ++y) {
            for (int x = 2; x <= std::max(2, mining.terrain.width - 3); ++x) {
                if (stampArtifact(x, y)) {
                    return true;
                }
            }
        }
    }
    return false;
}

void damageMiningArtifact(MiningRunState& mining, double damage)
{
    MiningArtifactObject& artifact = mining.artifact;
    if (!artifact.present || !recoverableArtifactState(artifact.state) || damage <= 0.0) {
        return;
    }
    artifact.health = std::max(0.0, artifact.health - damage);
    if (mining.gate.active && mining.gate.compatibilityCritical && mining.gate.fragileArtifact) {
        artifact.health = std::max(artifact.health, artifact.maxHealth * 0.10);
    }
    artifact.revealed = true;
    if (artifact.health <= 0.0) {
        artifact.state = MiningArtifactState::Destroyed;
        artifact.tethered = false;
        artifact.velocityX = 0.0;
        artifact.velocityY = 0.0;
    }
}

void releaseEmbeddedArtifact(MiningRunState& mining)
{
    MiningArtifactObject& artifact = mining.artifact;
    if (!artifact.present || artifact.state != MiningArtifactState::Embedded) {
        return;
    }
    artifact.state = MiningArtifactState::Loose;
    const int cellX = std::clamp(static_cast<int>(std::floor(artifact.x)), 0, mining.terrain.width - 1);
    const int cellY = std::clamp(static_cast<int>(std::floor(artifact.y)), 0, mining.terrain.height - 1);
    if (MiningCell* cell = miningCellAt(mining.terrain, cellX, cellY)) {
        if (cell->material == MiningCellMaterial::ArtifactCache) {
            *cell = makeCell(MiningCellMaterial::Empty, mining.depthZone);
            cell->revealed = true;
            markDirty(mining.terrain, cellX, cellY);
        }
    }
}

ArtifactRecord artifactRecordForObject(const MiningArtifactObject& artifact, std::string_view destinationId)
{
    ArtifactRecord record;
    record.id = artifact.id;
    record.originDestinationId = std::string(destinationId);
    record.identified = false;
    record.kind = artifact.kind;
    record.rewardType = artifact.rewardType;
    record.condition = artifact.maxHealth <= 0.0 ? 0.0 : std::clamp(artifact.health / artifact.maxHealth, 0.0, 1.0);
    record.rewardApplied = false;
    return record;
}

MaterialInventory brokenCellReward(
    GameState& state,
    const MiningDrillStats& stats,
    MiningCellMaterial material,
    bool includeYieldBonuses)
{
    MiningRunState& mining = state.run.mining;
    MaterialInventory gain;
    switch (material) {
    case MiningCellMaterial::Regolith:
        break;
    case MiningCellMaterial::CommonOre:
        gain.common = 1;
        break;
    case MiningCellMaterial::RareOre:
        gain.rare = 1;
        if (includeYieldBonuses &&
            unitHash(activeMiningArenaSeed(mining), mining.cellsBroken, mining.depthZone, 0, 71) < stats.rareYieldChance) {
            gain.rare += 1;
        }
        break;
    case MiningCellMaterial::ExoticVein:
        gain.exotic = 1;
        break;
    case MiningCellMaterial::ArtifactCache:
        break;
    case MiningCellMaterial::HazardPocket:
        break;
    case MiningCellMaterial::FuelPocket:
    case MiningCellMaterial::OxygenPocket:
        break;
    default:
        break;
    }

    if (includeYieldBonuses && gain.common > 0 &&
        unitHash(activeMiningArenaSeed(mining), mining.cellsBroken, mining.depthZone, 0, 83) < stats.oreYieldChance) {
        gain.common += 1;
    }
    if (includeYieldBonuses && gain.rare > 0 &&
        unitHash(activeMiningArenaSeed(mining), mining.cellsBroken, mining.depthZone, 1, 83) < stats.oreYieldChance * 0.75) {
        gain.rare += 1;
    }
    if (includeYieldBonuses && gain.exotic > 0 &&
        unitHash(activeMiningArenaSeed(mining), mining.cellsBroken, mining.depthZone, 2, 83) < stats.oreYieldChance * 0.45) {
        gain.exotic += 1;
    }
    return claimMiningRichRewardBudget(mining, gain);
}

MiningEnemy makeMiningEnemy(MiningEnemyType type, MiningCellFeature sourceFeature, MiningElementalAffinity affinity, double x, double y)
{
    MiningEnemy enemy;
    enemy.type = type;
    enemy.sourceFeature = sourceFeature;
    enemy.affinity = type == MiningEnemyType::Elemental ? affinity : MiningElementalAffinity::None;
    enemy.x = x;
    enemy.y = y;
    switch (type) {
    case MiningEnemyType::Ant:
        enemy.maxHealth = 5.0;
        enemy.speed = 2.0;
        enemy.damagePerSecond = 0.62;
        enemy.armor = 0.0;
        break;
    case MiningEnemyType::Flying:
        enemy.maxHealth = 4.0;
        enemy.speed = 3.1;
        enemy.damagePerSecond = 0.48;
        enemy.armor = 0.0;
        break;
    case MiningEnemyType::Beetle:
        enemy.maxHealth = 10.0;
        enemy.speed = 1.15;
        enemy.damagePerSecond = 0.82;
        enemy.armor = 0.45;
        break;
    case MiningEnemyType::Elemental:
        enemy.maxHealth = 8.0;
        enemy.speed = 1.65;
        enemy.damagePerSecond = 0.58;
        enemy.armor = 0.18;
        enemy.effectRadius = tuning::mining::enemyElementalRadiusCells;
        break;
    case MiningEnemyType::Mammal:
        enemy.maxHealth = 15.0;
        enemy.speed = 1.45;
        enemy.damagePerSecond = 0.95;
        enemy.armor = 0.28;
        break;
    case MiningEnemyType::Spawner:
        enemy.maxHealth = 30.0;
        enemy.speed = 0.0;
        enemy.damagePerSecond = 0.0;
        enemy.armor = tuning::mining::enemySpawnerArmor;
        break;
    case MiningEnemyType::None:
        enemy.active = false;
        break;
    }

    if (enemy.type == MiningEnemyType::Elemental) {
        switch (enemy.affinity) {
        case MiningElementalAffinity::Thermal:
            enemy.damagePerSecond += 0.14;
            break;
        case MiningElementalAffinity::Cryo:
            enemy.speed *= 0.82;
            enemy.effectRadius += 0.35;
            break;
        case MiningElementalAffinity::Radiation:
            enemy.effectRadius += 0.25;
            enemy.armor = std::max(0.0, enemy.armor - 0.06);
            break;
        case MiningElementalAffinity::Toxic:
            enemy.damagePerSecond += 0.08;
            enemy.armor += 0.08;
            break;
        case MiningElementalAffinity::None:
            break;
        }
    }

    if (sourceFeature == MiningCellFeature::BossChamber) {
        enemy.maxHealth *= tuning::mining::bossHealthScale;
        enemy.damagePerSecond *= 1.35;
        enemy.armor += 0.10;
    } else if (sourceFeature == MiningCellFeature::MinibossLair) {
        enemy.maxHealth *= tuning::mining::minibossHealthScale;
        enemy.damagePerSecond *= 1.25;
    } else if (sourceFeature == MiningCellFeature::TreasureVault || sourceFeature == MiningCellFeature::HiveNest) {
        enemy.maxHealth *= tuning::mining::roomEnemyHealthScale;
    }
    enemy.health = enemy.maxHealth;
    return enemy;
}

MiningEnemy makeMiningEnemyForRules(
    MiningEnemyType type,
    MiningCellFeature sourceFeature,
    MiningElementalAffinity affinity,
    double x,
    double y,
    const MiningArenaRules& rules)
{
    MiningEnemy enemy = makeMiningEnemy(type, sourceFeature, affinity, x, y);
    if (!enemy.active || type == MiningEnemyType::None) {
        return enemy;
    }
    enemy.maxHealth *= std::max(0.0, rules.enemyHealthScale);
    enemy.health = enemy.maxHealth;
    enemy.damagePerSecond *= std::max(0.0, rules.enemyDamageScale);
    return enemy;
}

bool shouldSpawnEnemyAt(const MiningTerrain& terrain, int x, int y, MiningCellFeature feature)
{
    switch (feature) {
    case MiningCellFeature::EncounterZone:
        return ((x * 3 + y * 5 + terrain.depthZone) % 13) == 0;
    case MiningCellFeature::TreasureVault:
    case MiningCellFeature::HiveNest:
        return ((x * 5 + y * 7 + terrain.depthZone) % 19) == 0;
    case MiningCellFeature::MinibossLair:
        return ((x + y + terrain.depthZone) % 11) == 0;
    case MiningCellFeature::OrganicBurrow:
        return ((x * 7 + y * 3 + terrain.depthZone) % 23) == 0;
    case MiningCellFeature::BossChamber:
        return ((x + y + terrain.depthZone) % 7) == 0;
    default:
        return false;
    }
}

void spawnMiningEnemies(MiningRunState& mining, const Destination& destination, const MiningArenaRules& rules)
{
    if (rules.maxActiveEnemies <= 0) {
        return;
    }

    struct EnemySpawnCandidate {
        int x = 0;
        int y = 0;
        const MiningCell* cell = nullptr;
    };

    std::vector<EnemySpawnCandidate> candidates;
    for (int y = 0; y < mining.terrain.height; ++y) {
        for (int x = 0; x < mining.terrain.width; ++x) {
            const MiningCell* cell = miningCellAt(mining.terrain, x, y);
            if (cell == nullptr || cell->enemy == MiningEnemyType::None || miningMaterialSolid(cell->material) ||
                !miningEnemyAllowed(rules, cell->enemy) || !miningRoomFeatureAllowed(rules, cell->feature)) {
                continue;
            }
            candidates.push_back({x, y, cell});
        }
    }

    int spawnersPlaced = 0;
    if (rules.maxSpawners > 0 && miningEnemyAllowed(rules, MiningEnemyType::Spawner)) {
        for (const EnemySpawnCandidate& candidate : candidates) {
            if (spawnersPlaced >= rules.maxSpawners || static_cast<int>(mining.enemies.size()) >= rules.maxActiveEnemies) {
                break;
            }
            if (candidate.cell->feature != MiningCellFeature::HiveNest &&
                candidate.cell->feature != MiningCellFeature::MinibossLair &&
                candidate.cell->feature != MiningCellFeature::BossChamber) {
                continue;
            }
            const MiningElementalAffinity affinity = candidate.cell->enemy == MiningEnemyType::Elemental
                ? elementalAffinityForLane(rules, mining.siteProfile, candidate.x + candidate.y)
                : MiningElementalAffinity::None;
            MiningEnemy spawner = makeMiningEnemyForRules(
                MiningEnemyType::Spawner,
                candidate.cell->feature,
                MiningElementalAffinity::None,
                static_cast<double>(candidate.x) + 0.5,
                static_cast<double>(candidate.y) + 0.5,
                rules);
            spawner.spawn.enemyType = candidate.cell->enemy;
            spawner.spawn.affinity = affinity;
            spawner.spawn.maxSpawns = std::clamp(2 + rules.request.difficulty / 3, 2, 5);
            spawner.spawn.intervalSeconds = std::clamp(8.0 - static_cast<double>(rules.request.difficulty) * 0.35, 4.5, 7.5);
            spawner.spawn.cooldownSeconds = spawner.spawn.intervalSeconds;
            mining.enemies.push_back(std::move(spawner));
            ++spawnersPlaced;
        }
    }

    bool minibossSpawned = false;
    bool bossSpawned = false;
    for (const EnemySpawnCandidate& candidate : candidates) {
        if (static_cast<int>(mining.enemies.size()) >= rules.maxActiveEnemies) {
            break;
        }
        const int x = candidate.x;
        const int y = candidate.y;
        const MiningCell* cell = candidate.cell;
            if (cell->feature == MiningCellFeature::MinibossLair && minibossSpawned) {
                continue;
            }
            if (cell->feature == MiningCellFeature::BossChamber && bossSpawned) {
                continue;
            }
            if (!shouldSpawnEnemyAt(mining.terrain, x, y, cell->feature)) {
                continue;
            }
            const MiningElementalAffinity affinity = cell->enemy == MiningEnemyType::Elemental
                ? elementalAffinityForLane(rules, mining.siteProfile, x + y)
                : MiningElementalAffinity::None;
            mining.enemies.push_back(makeMiningEnemyForRules(
                cell->enemy,
                cell->feature,
                affinity,
                static_cast<double>(x) + 0.5,
                static_cast<double>(y) + 0.5,
                rules));
            minibossSpawned = minibossSpawned || cell->feature == MiningCellFeature::MinibossLair;
            bossSpawned = bossSpawned || cell->feature == MiningCellFeature::BossChamber;
    }
    (void)destination;
}

void relocateMiningArtifact(MiningRunState& mining, int x, int y, bool revealed, MiningCellFeature feature)
{
    if (mining.artifact.present) {
        const int oldX = std::clamp(static_cast<int>(std::floor(mining.artifact.x)), 0, mining.terrain.width - 1);
        const int oldY = std::clamp(static_cast<int>(std::floor(mining.artifact.y)), 0, mining.terrain.height - 1);
        if (MiningCell* oldCell = miningCellAt(mining.terrain, oldX, oldY);
            oldCell != nullptr && oldCell->material == MiningCellMaterial::ArtifactCache) {
            *oldCell = makeCell(MiningCellMaterial::Empty, mining.depthZone);
            oldCell->revealed = true;
            markDirty(mining.terrain, oldX, oldY);
        }
    }
    MiningCell* anchor = miningCellAt(mining.terrain, x, y);
    if (anchor == nullptr) {
        return;
    }
    *anchor = makeCell(MiningCellMaterial::ArtifactCache, mining.depthZone);
    anchor->feature = feature;
    anchor->revealed = revealed;
    anchor->gateAssociated = true;
    markDirty(mining.terrain, x, y);
    mining.artifact.present = true;
    mining.artifact.state = MiningArtifactState::Embedded;
    mining.artifact.x = static_cast<double>(x) + 0.5;
    mining.artifact.y = static_cast<double>(y) + 0.5;
    mining.artifact.velocityX = 0.0;
    mining.artifact.velocityY = 0.0;
    mining.artifact.maxHealth = tuning::mining::artifactMaxHealth;
    mining.artifact.health = mining.artifact.maxHealth;
    mining.artifact.embedStrength = 1.0;
    mining.artifact.tethered = false;
    mining.artifact.revealed = revealed;
}

MiningCocoonDefinition makeLegacyLayeredCocoonDefinition(
    const MiningGateDefinition& definition)
{
    MiningCocoonDefinition cocoon;
    cocoon.id = "legacy_layered_hazard_gate";
    cocoon.version = 1;
    cocoon.protectedObjective = {ProtectedObjectiveKind::Artifact, "legacy_protected_artifact"};
    cocoon.layers = {
        {
            "layer_1",
            "LAYER 1",
            {{0, -2}, {2, 0}, {0, 2}, {-2, 0}},
            MiningCocoonRevealPolicy::OnAnyCellDiscovered,
            MiningCocoonCompletionRule::TreatAndExcavate,
            definition.hazardAffinity,
            definition.requiredHazardMark
        },
        {
            "layer_2",
            "LAYER 2",
            {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}},
            MiningCocoonRevealPolicy::AfterPreviousLayerCompleted,
            MiningCocoonCompletionRule::TreatAndExcavate,
            definition.hazardAffinity,
            definition.requiredHazardMark
        }
    };
    return cocoon;
}

void stampGateHazardShell(
    MiningRunState& mining,
    const MiningGateDefinition& definition,
    int anchorX,
    int anchorY,
    const MiningCocoonDefinition* cocoon)
{
    const auto stamp = [&](int dx,
                           int dy,
                           MiningElementalAffinity affinity,
                           bool revealed,
                           int cocoonLayer) {
        MiningCell* cell = miningCellAt(mining.terrain, anchorX + dx, anchorY + dy);
        if (cell == nullptr) {
            return false;
        }
        *cell = makeCell(MiningCellMaterial::HazardPocket, mining.depthZone);
        cell->hazard = true;
        cell->hazardAffinity = affinity;
        cell->feature = MiningCellFeature::BranchTunnel;
        cell->revealed = revealed;
        cell->gateAssociated = true;
        cell->cocoonLayer = cocoonLayer;
        markDirty(mining.terrain, anchorX + dx, anchorY + dy);
        ++mining.gate.shellTilesTotal;
        return true;
    };

    if (cocoon != nullptr && !cocoon->layers.empty()) {
        mining.gate.cocoonDefinitionId = cocoon->id;
        mining.gate.cocoonDefinitionVersion = cocoon->version;
        mining.gate.protectedObjective = cocoon->protectedObjective;
        mining.gate.cocoonLayers.clear();
        mining.gate.cocoonLayers.reserve(cocoon->layers.size());
        for (std::size_t index = 0; index < cocoon->layers.size(); ++index) {
            const MiningCocoonLayerDefinition& layer = cocoon->layers[index];
            MiningCocoonLayerProgress progress;
            progress.id = layer.id;
            progress.label = layer.label;
            progress.requiredHazardMark = std::max(1, layer.requiredHazardMark);
            progress.completionRule = layer.completionRule;
            progress.revealPolicy = layer.revealPolicy;
            progress.revealed = layer.revealPolicy == MiningCocoonRevealPolicy::Immediately ||
                (index == 0 &&
                 layer.revealPolicy == MiningCocoonRevealPolicy::AfterPreviousLayerCompleted);
            for (const MiningCocoonOffset& offset : layer.offsets) {
                if (stamp(
                        offset.x,
                        offset.y,
                        layer.hazardAffinity,
                        progress.revealed,
                        static_cast<int>(index))) {
                    ++progress.total;
                    ++progress.remaining;
                }
            }
            progress.completed = progress.total == 0;
            mining.gate.cocoonLayers.push_back(std::move(progress));
        }
        mining.gate.activeCocoonLayer = earliestIncompleteCocoonLayer(mining);
        for (int layer = 0;
             layer < static_cast<int>(mining.gate.cocoonLayers.size());
             ++layer) {
            if (cocoonLayerRevealed(mining, layer)) {
                revealCocoonLayer(mining, layer);
            }
        }
        if (!mining.gate.cocoonLayers.empty()) {
            const MiningCocoonLayerProgress& outer = mining.gate.cocoonLayers.front();
            mining.gate.outerShellTilesTotal = outer.total;
            mining.gate.outerShellTilesRemaining = outer.remaining;
        }
        if (mining.gate.cocoonLayers.size() > 1) {
            const MiningCocoonLayerProgress& inner = mining.gate.cocoonLayers[1];
            mining.gate.innerShellTilesTotal = inner.total;
            mining.gate.innerShellTilesRemaining = inner.remaining;
        }
        mining.gate.shellTilesRemaining = std::accumulate(
            mining.gate.cocoonLayers.begin(),
            mining.gate.cocoonLayers.end(),
            0,
            [](int total, const MiningCocoonLayerProgress& layer) {
                return total + std::max(0, layer.remaining);
            });
        return;
    }

    constexpr std::array<std::pair<int, int>, 8> offsets {{
        {-1, -1}, {0, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {0, 1}, {1, 1}
    }};
    for (const auto& [dx, dy] : offsets) {
        (void)stamp(dx, dy, definition.hazardAffinity, true, -1);
    }
    mining.gate.outerShellTilesTotal = mining.gate.shellTilesTotal;
    mining.gate.outerShellTilesRemaining = mining.gate.shellTilesTotal;
    mining.gate.shellTilesRemaining = mining.gate.shellTilesTotal;
}

void stampGateEncounter(MiningRunState& mining, const MiningArenaRules& rules, int anchorX, int anchorY, bool rangedOnly)
{
    mining.enemies.clear();
    const int enemyCount = std::max(1, std::min(rules.maxActiveEnemies, rangedOnly ? 3 : 2 + rules.request.difficulty / 4));
    for (int index = 0; index < enemyCount; ++index) {
        MiningEnemyType type = MiningEnemyType::Ant;
        if (rangedOnly && miningEnemyAllowed(rules, MiningEnemyType::Flying)) {
            type = MiningEnemyType::Flying;
        } else if (rules.request.difficulty >= 7 && miningEnemyAllowed(rules, MiningEnemyType::Elemental)) {
            type = MiningEnemyType::Elemental;
        } else if (rules.request.difficulty >= 5 && miningEnemyAllowed(rules, MiningEnemyType::Beetle)) {
            type = MiningEnemyType::Beetle;
        }
        const MiningElementalAffinity affinity = type == MiningEnemyType::Elemental
            ? (rules.request.act == MiningAct::ActThree ? MiningElementalAffinity::Radiation : MiningElementalAffinity::Thermal)
            : MiningElementalAffinity::None;
        MiningEnemy enemy = makeMiningEnemyForRules(
            type,
            rangedOnly ? MiningCellFeature::TreasureVault : MiningCellFeature::EncounterZone,
            affinity,
            static_cast<double>(anchorX - 4 + index * 2) + 0.5,
            static_cast<double>(anchorY - 2 + index % 2) + 0.5,
            rules);
        enemy.gateAssociated = true;
        mining.enemies.push_back(std::move(enemy));
    }
    if (rules.maxSpawners > 0 && miningEnemyAllowed(rules, MiningEnemyType::Spawner) &&
        static_cast<int>(mining.enemies.size()) < rules.maxActiveEnemies) {
        MiningEnemy spawner = makeMiningEnemyForRules(
            MiningEnemyType::Spawner,
            MiningCellFeature::HiveNest,
            MiningElementalAffinity::None,
            static_cast<double>(anchorX) + 3.5,
            static_cast<double>(anchorY) - 1.5,
            rules);
        spawner.gateAssociated = true;
        spawner.spawn.enemyType = miningEnemyAllowed(rules, MiningEnemyType::Elemental) ? MiningEnemyType::Elemental : MiningEnemyType::Ant;
        spawner.spawn.affinity = spawner.spawn.enemyType == MiningEnemyType::Elemental
            ? MiningElementalAffinity::Thermal
            : MiningElementalAffinity::None;
        spawner.spawn.maxSpawns = 3;
        spawner.spawn.intervalSeconds = 5.5;
        spawner.spawn.cooldownSeconds = 5.5;
        mining.enemies.push_back(std::move(spawner));
    }
    mining.gate.assignedEnemiesRemaining = static_cast<int>(std::count_if(mining.enemies.begin(), mining.enemies.end(), [](const MiningEnemy& enemy) {
        return enemy.gateAssociated && enemy.active;
    }));
}

void setupMiningGate(
    MiningRunState& mining,
    const MiningArenaRules& rules,
    MiningSiteProgress* compatibilitySite,
    const MiningSiteDefinition* siteDefinition)
{
    const MiningGateType type = siteDefinition != nullptr
        ? siteDefinition->gateType
        : (compatibilitySite != nullptr
            ? compatibilitySite->gateType
            : selectMiningGateType(rules));
    if (type == MiningGateType::None || !miningGateAllowed(rules, type)) {
        return;
    }
    // Authored sites carry their own objective and reward references. The
    // compatibility marker remains only for migrated pre-scenario gates.
    const bool compatibilityCritical = compatibilitySite != nullptr;
    const MiningGateDefinition definition = resolveMiningGateDefinition(
        rules,
        type,
        compatibilityCritical);
    std::optional<MiningCocoonDefinition> legacyCocoon;
    const MiningCocoonDefinition* cocoon = nullptr;
    if (siteDefinition != nullptr && !siteDefinition->cocoon.layers.empty()) {
        cocoon = &siteDefinition->cocoon;
    } else if (compatibilitySite != nullptr && type == MiningGateType::HazardCocoon) {
        // Compatibility only: old campaign saves did not carry a reusable
        // cocoon definition. New authored content always supplies one.
        legacyCocoon = makeLegacyLayeredCocoonDefinition(definition);
        cocoon = &*legacyCocoon;
    }
    if (!mining.artifact.present) {
        mining.artifact = {};
        mining.artifact.present = true;
        mining.artifact.id = artifactId(mining);
        mining.artifact.kind = ArtifactKind::Boost;
        mining.artifact.rewardType = ArtifactRewardType::BlueprintInsight;
    }

    mining.gate = {};
    mining.gate.active = true;
    mining.gate.type = type;
    mining.gate.state = MiningGateState::InProgress;
    mining.gate.compatibilityCritical = compatibilityCritical;
    mining.gate.discovered = true;
    mining.gate.siteId = siteDefinition != nullptr
        ? siteDefinition->id
        : (compatibilitySite != nullptr
        ? compatibilitySite->siteId
        : mining.destinationId + "_optional_gate_" + std::to_string(static_cast<int>(type)));
    mining.gate.artifactId = compatibilitySite != nullptr
        ? compatibilitySite->artifactId
        : mining.artifact.id;
    if (cocoon != nullptr && cocoon->protectedObjective.kind == ProtectedObjectiveKind::Artifact &&
        !cocoon->protectedObjective.id.empty()) {
        // The protected-objective reference is the stable payload identity.
        // Scenario reward dispatch can therefore match it without knowing the
        // destination, site layout, or campaign beat that exposed it.
        mining.gate.artifactId = cocoon->protectedObjective.id;
    }
    mining.gate.hazardAffinity = definition.hazardAffinity;
    mining.gate.requiredHazardMark = definition.requiredHazardMark;
    mining.gate.requiredSurveyOrigins = definition.requiredSurveyOrigins;
    mining.gate.fragileArtifact = definition.fragileArtifact;
    mining.gate.heavyTow = definition.heavyTow;
    mining.gate.endurancePlacement = definition.endurancePlacement;
    mining.gate.shieldCorridor = definition.shieldCorridor;
    mining.gate.burrowBreach = definition.burrowBreach;
    mining.gate.hazardTreatmentComplete = !definition.requiresHazardTreatment;
    mining.gate.enemyClearanceComplete = !definition.requiresEnemyClearance;
    mining.gate.surveyComplete = !definition.requiresSurveyTriangulation && !definition.burrowBreach;

    const int anchorX = std::clamp(
        definition.endurancePlacement ? mining.terrain.width * 4 / 5 : mining.terrain.width * 2 / 3,
        8,
        mining.terrain.width - 6);
    const int anchorY = std::clamp(
        definition.endurancePlacement ? mining.terrain.height - 6 : mining.terrain.height * 2 / 3,
        9,
        mining.terrain.height - 5);
    mining.gate.anchorX = static_cast<double>(anchorX) + 0.5;
    mining.gate.anchorY = static_cast<double>(anchorY) + 0.5;
    const MiningCellFeature gateFeature = miningRoomFeatureAllowed(rules, MiningCellFeature::TreasureVault)
        ? MiningCellFeature::TreasureVault
        : MiningCellFeature::BranchTunnel;
    relocateMiningArtifact(
        mining,
        anchorX,
        anchorY,
        cocoon == nullptr,
        gateFeature);
    mining.artifact.id = mining.gate.artifactId;
    if (cocoon != nullptr &&
        cocoon->protectedObjective.kind == ProtectedObjectiveKind::Artifact) {
        // Scenario rewards own the outcome. The artifact adapter only owns
        // protection, visibility, tethering, and safe extraction.
        mining.artifact.kind = ArtifactKind::Boost;
        mining.artifact.rewardType = ArtifactRewardType::None;
    } else if (compatibilityCritical) {
        if (legacyCocoon.has_value()) {
            mining.artifact.kind = ArtifactKind::Boost;
            mining.artifact.rewardType = ArtifactRewardType::DroneUpgradeCredit;
        } else {
            mining.artifact.kind = ArtifactKind::Story;
            mining.artifact.rewardType = ArtifactRewardType::None;
        }
    }

    if (definition.requiresHazardTreatment) {
        stampGateHazardShell(mining, definition, anchorX, anchorY, cocoon);
        mining.gate.state = MiningGateState::Locked;
    }
    if (definition.requiresEnemyClearance || definition.shieldCorridor) {
        stampGateEncounter(mining, rules, anchorX, anchorY, definition.shieldCorridor);
        mining.gate.state = definition.requiresEnemyClearance ? MiningGateState::Locked : MiningGateState::InProgress;
    }
    if (definition.requiresSurveyTriangulation) {
        const double markerRadius = std::max(4.0, static_cast<double>(mining.terrain.width) * 0.10);
        mining.gate.markers = {
            {mining.gate.anchorX - markerRadius, mining.gate.anchorY - 2.0, false},
            {mining.gate.anchorX + markerRadius, mining.gate.anchorY - 1.0, false},
            {mining.gate.anchorX, mining.gate.anchorY + markerRadius * 0.55, false}
        };
        mining.gate.state = MiningGateState::Locked;
    }
    if (definition.burrowBreach) {
        mining.enemies.clear();
        const int wallX = anchorX - 4;
        for (int dy = -2; dy <= 2; ++dy) {
            MiningCell* wall = miningCellAt(mining.terrain, wallX, anchorY + dy);
            if (wall == nullptr) {
                continue;
            }
            *wall = makeCell(MiningCellMaterial::Bedrock, mining.depthZone);
            wall->remainingToughness = wall->maxToughness = 1.8;
            wall->feature = MiningCellFeature::OrganicBurrow;
            wall->enemy = MiningEnemyType::Mammal;
            wall->revealed = true;
            wall->gateAssociated = true;
            markDirty(mining.terrain, wallX, anchorY + dy);
        }
        MiningEnemy mammal = makeMiningEnemyForRules(
            MiningEnemyType::Mammal,
            MiningCellFeature::OrganicBurrow,
            MiningElementalAffinity::None,
            static_cast<double>(wallX - 2) + 0.5,
            static_cast<double>(anchorY) + 0.5,
            rules);
        mammal.gateAssociated = true;
        mining.enemies.push_back(std::move(mammal));
        mining.gate.state = MiningGateState::InProgress;
    }

    if (compatibilitySite != nullptr) {
        compatibilitySite->discovered = true;
    }
}

bool gateHasHardLock(const MiningGateRuntime& gate)
{
    return gate.active && (
        !gate.hazardTreatmentComplete ||
        !gate.enemyClearanceComplete ||
        !gate.surveyComplete);
}

void updateMiningGate(GameState& state, const MiningArenaRules& rules)
{
    MiningRunState& mining = state.run.mining;
    MiningGateRuntime& gate = mining.gate;
    if (!gate.active || gate.state == MiningGateState::Completed) {
        return;
    }

    if (!gate.derivedStateDirty) {
        if (mining.artifact.state == MiningArtifactState::Delivered) {
            gate.state = MiningGateState::Completed;
        }
        return;
    }
    gate.derivedStateDirty = false;

    if (hasLayeredCocoon(mining)) {
        const int previousActiveLayer = gate.activeCocoonLayer;
        std::vector<bool> layerHasRevealedCell(gate.cocoonLayers.size(), false);
        for (int layer = 0;
             layer < static_cast<int>(gate.cocoonLayers.size());
             ++layer) {
            MiningCocoonLayerProgress& progress =
                gate.cocoonLayers[static_cast<std::size_t>(layer)];
            int observed = 0;
            int remaining = 0;
            for (const MiningCell& cell : mining.terrain.cells) {
                if (cell.cocoonLayer != layer) {
                    continue;
                }
                ++observed;
                layerHasRevealedCell[static_cast<std::size_t>(layer)] =
                    layerHasRevealedCell[static_cast<std::size_t>(layer)] || cell.revealed;
                const bool requiresExcavation =
                    progress.completionRule == MiningCocoonCompletionRule::TreatAndExcavate;
                const bool unresolved = requiresExcavation
                    ? cell.material != MiningCellMaterial::Empty
                    : cell.material == MiningCellMaterial::HazardPocket;
                if (unresolved) {
                    ++remaining;
                }
            }
            progress.total = std::max(progress.total, observed);
            progress.remaining = remaining;
            progress.completed = progress.total == 0 || remaining == 0;
        }
        gate.activeCocoonLayer = earliestIncompleteCocoonLayer(mining);
        bool exposedActiveLayer = false;
        for (int layer = 0;
             layer < static_cast<int>(gate.cocoonLayers.size());
             ++layer) {
            MiningCocoonLayerProgress& progress = gate.cocoonLayers[
                static_cast<std::size_t>(layer)];
            if (progress.revealed) {
                continue;
            }
            const bool prerequisitesComplete = cocoonLayerPrerequisitesComplete(mining, layer);
            const bool shouldReveal =
                progress.revealPolicy == MiningCocoonRevealPolicy::Immediately ||
                (progress.revealPolicy == MiningCocoonRevealPolicy::AfterPreviousLayerCompleted &&
                 prerequisitesComplete) ||
                (progress.revealPolicy == MiningCocoonRevealPolicy::OnAnyCellDiscovered &&
                 prerequisitesComplete &&
                 layerHasRevealedCell[static_cast<std::size_t>(layer)]);
            if (!shouldReveal) {
                continue;
            }
            revealCocoonLayer(mining, layer);
            exposedActiveLayer = exposedActiveLayer || layer == gate.activeCocoonLayer;
        }
        if (previousActiveLayer >= 0 &&
            previousActiveLayer != gate.activeCocoonLayer &&
            gate.activeCocoonLayer >= 0 &&
            exposedActiveLayer) {
            const MiningCocoonLayerProgress& next = gate.cocoonLayers[
                static_cast<std::size_t>(gate.activeCocoonLayer)];
            state.statusLine = next.label.empty()
                ? "The next protected layer is exposed."
                : next.label + " exposed.";
        }
        if (gate.activeCocoonLayer < 0) {
            revealProtectedObjective(mining);
        }
        gate.shellTilesRemaining = std::accumulate(
            gate.cocoonLayers.begin(),
            gate.cocoonLayers.end(),
            0,
            [](int total, const MiningCocoonLayerProgress& layer) {
                return total + std::max(0, layer.remaining);
            });
        gate.outerShellTilesTotal = !gate.cocoonLayers.empty()
            ? gate.cocoonLayers.front().total
            : 0;
        gate.outerShellTilesRemaining = !gate.cocoonLayers.empty()
            ? gate.cocoonLayers.front().remaining
            : 0;
        gate.innerShellTilesTotal = gate.cocoonLayers.size() > 1
            ? gate.cocoonLayers[1].total
            : 0;
        gate.innerShellTilesRemaining = gate.cocoonLayers.size() > 1
            ? gate.cocoonLayers[1].remaining
            : 0;
        gate.hazardTreatmentComplete = gate.activeCocoonLayer < 0;
    } else {
        gate.shellTilesRemaining = static_cast<int>(std::count_if(mining.terrain.cells.begin(), mining.terrain.cells.end(), [](const MiningCell& cell) {
            return cell.gateAssociated && cell.material == MiningCellMaterial::HazardPocket;
        }));
        gate.outerShellTilesRemaining = gate.shellTilesRemaining;
        gate.hazardTreatmentComplete = gate.shellTilesRemaining == 0;
    }
    gate.assignedEnemiesRemaining = static_cast<int>(std::count_if(mining.enemies.begin(), mining.enemies.end(), [](const MiningEnemy& enemy) {
        return enemy.gateAssociated && enemy.active;
    }));
    gate.enemyClearanceComplete = gate.assignedEnemiesRemaining == 0;
    gate.surveyOriginsCompleted = static_cast<int>(std::count_if(gate.markers.begin(), gate.markers.end(), [](const MiningGateMarker& marker) {
        return marker.activated;
    }));
    if (gate.requiredSurveyOrigins > 0) {
        gate.surveyComplete = gate.surveyOriginsCompleted >= gate.requiredSurveyOrigins;
    }

    if (gate.burrowBreach) {
        const bool anyWall = std::any_of(mining.terrain.cells.begin(), mining.terrain.cells.end(), [](const MiningCell& cell) {
            return cell.gateAssociated && cell.material == MiningCellMaterial::Bedrock;
        });
        gate.burrowBreached = !anyWall || gate.surveyComplete;
        const bool hasBurrower = std::any_of(mining.enemies.begin(), mining.enemies.end(), [](const MiningEnemy& enemy) {
            return enemy.gateAssociated && enemy.type == MiningEnemyType::Mammal && enemy.active;
        });
        if (!gate.burrowBreached && !hasBurrower && miningEnemyAllowed(rules, MiningEnemyType::Mammal)) {
            MiningEnemy replacement = makeMiningEnemyForRules(
                MiningEnemyType::Mammal,
                MiningCellFeature::OrganicBurrow,
                MiningElementalAffinity::None,
                gate.anchorX - 6.0,
                gate.anchorY,
                rules);
            replacement.gateAssociated = true;
            const auto reusable = std::find_if(mining.enemies.begin(), mining.enemies.end(), [](const MiningEnemy& enemy) {
                return enemy.gateAssociated && enemy.type == MiningEnemyType::Mammal && !enemy.active;
            });
            if (reusable != mining.enemies.end()) {
                *reusable = std::move(replacement);
            } else {
                mining.enemies.push_back(std::move(replacement));
            }
            markMiningGateDerivedStateDirty(mining);
        }
        if (gate.burrowBreached) {
            gate.surveyComplete = true;
        }
    }

    const bool unlocked = !gateHasHardLock(gate);
    if (unlocked && (gate.state == MiningGateState::Locked || gate.state == MiningGateState::InProgress) &&
        (gate.type == MiningGateType::HazardCocoon || gate.type == MiningGateType::EnemySealedChamber ||
         gate.type == MiningGateType::SurveyTriangulation || gate.type == MiningGateType::BurrowBreach ||
         gate.type == MiningGateType::CompoundVault)) {
        gate.state = MiningGateState::Open;
        if (!gate.completionNotified) {
            state.statusLine = std::string(miningGateName(gate.type)) + " opened. The artifact can now be extracted.";
            gate.completionNotified = true;
        }
    }
    if (mining.artifact.state == MiningArtifactState::Delivered) {
        gate.state = MiningGateState::Completed;
    }
}

bool enemyIgnoresTerrain(MiningEnemyType type)
{
    return type == MiningEnemyType::Flying;
}

bool enemyUsesRangedAttack(MiningEnemyType type)
{
    return type == MiningEnemyType::Flying || type == MiningEnemyType::Elemental;
}

bool enemyUsesMeleeAttack(MiningEnemyType type)
{
    return type == MiningEnemyType::Ant || type == MiningEnemyType::Beetle || type == MiningEnemyType::Mammal;
}

void updateMiningEnemySpawners(MiningRunState& mining, const MiningArenaRules& rules, double dt)
{
    int activeEnemyCount = static_cast<int>(std::count_if(mining.enemies.begin(), mining.enemies.end(), [](const MiningEnemy& enemy) {
        return enemy.active;
    }));
    const std::size_t existingEnemyCount = mining.enemies.size();
    std::vector<MiningEnemy> spawnedEnemies;

    for (std::size_t i = 0; i < existingEnemyCount; ++i) {
        MiningEnemy& spawner = mining.enemies[i];
        MiningEnemySpawnSpec& spec = spawner.spawn;
        if (!spawner.active || spawner.type != MiningEnemyType::Spawner ||
            spec.enemyType == MiningEnemyType::None || spec.enemyType == MiningEnemyType::Spawner ||
            spec.maxSpawns <= 0 || spec.spawned >= spec.maxSpawns) {
            continue;
        }

        const double interval = std::max(0.01, spec.intervalSeconds);
        spec.cooldownSeconds -= std::max(0.0, dt);
        while (spec.cooldownSeconds <= 0.0 && spec.spawned < spec.maxSpawns &&
            activeEnemyCount < rules.maxActiveEnemies) {
            const double angle = static_cast<double>(spec.spawned % 8) * (kPi * 0.25);
            const double radius = tuning::mining::enemySpawnerSpawnRadiusCells;
            double spawnX = std::clamp(spawner.x + std::cos(angle) * radius, 1.0, static_cast<double>(mining.terrain.width - 2));
            double spawnY = std::clamp(spawner.y + std::sin(angle) * radius, 1.0, static_cast<double>(mining.terrain.height - 2));
            if (!enemyIgnoresTerrain(spec.enemyType) && !canOccupy(mining.terrain, spawnX, spawnY)) {
                spawnX = spawner.x;
                spawnY = spawner.y;
            }

            MiningEnemy spawned = makeMiningEnemyForRules(
                spec.enemyType,
                spawner.sourceFeature,
                spec.affinity,
                spawnX,
                spawnY,
                rules);
            spawned.gateAssociated = spawner.gateAssociated;
            spawnedEnemies.push_back(std::move(spawned));
            spec.spawned += 1;
            activeEnemyCount += 1;
            spec.cooldownSeconds += interval;
        }
    }

    const bool spawnedGateEnemy = std::any_of(spawnedEnemies.begin(), spawnedEnemies.end(), [](const MiningEnemy& enemy) {
        return enemy.gateAssociated;
    });
    mining.enemies.insert(mining.enemies.end(), spawnedEnemies.begin(), spawnedEnemies.end());
    if (spawnedGateEnemy) {
        markMiningGateDerivedStateDirty(mining);
    }
}

double deterministicCombatRoll(const MiningRunState& mining, const MiningEnemy& enemy, int salt)
{
    std::uint64_t value = hashCombine(0xC0FFEEULL, static_cast<std::uint64_t>(mining.combatSequence + 1));
    value = hashCombine(value, static_cast<std::uint64_t>(static_cast<int>(enemy.type) + 11));
    value = hashCombine(value, static_cast<std::uint64_t>(static_cast<int>(enemy.affinity) + 23));
    value = hashCombine(value, static_cast<std::uint64_t>(std::max(0, static_cast<int>(std::round(enemy.x * 31.0)))));
    value = hashCombine(value, static_cast<std::uint64_t>(std::max(0, static_cast<int>(std::round(enemy.y * 37.0)))));
    value = hashCombine(value, static_cast<std::uint64_t>(salt));
    return static_cast<double>(value % 10000ULL) / 10000.0;
}

bool deterministicCombatCrit(MiningRunState& mining, const MiningEnemy& enemy, double chance, int salt)
{
    const bool critical = deterministicCombatRoll(mining, enemy, salt) < chance;
    mining.combatSequence += 1;
    return critical;
}

void trimMiningCombatVisuals(MiningRunState& mining)
{
    if (mining.combatProjectiles.size() > static_cast<std::size_t>(tuning::mining::maxCombatProjectiles)) {
        mining.combatProjectiles.erase(
            mining.combatProjectiles.begin(),
            mining.combatProjectiles.begin() + static_cast<std::ptrdiff_t>(mining.combatProjectiles.size() - tuning::mining::maxCombatProjectiles));
    }
    if (mining.damageNumbers.size() > static_cast<std::size_t>(tuning::mining::maxDamageNumbers)) {
        mining.damageNumbers.erase(
            mining.damageNumbers.begin(),
            mining.damageNumbers.begin() + static_cast<std::ptrdiff_t>(mining.damageNumbers.size() - tuning::mining::maxDamageNumbers));
    }
}

void advanceMiningCombatVisuals(MiningRunState& mining, double dt)
{
    for (MiningProjectileVisual& projectile : mining.combatProjectiles) {
        projectile.age += dt;
    }
    for (MiningDamageNumber& number : mining.damageNumbers) {
        number.age += dt;
    }
    mining.combatProjectiles.erase(
        std::remove_if(mining.combatProjectiles.begin(), mining.combatProjectiles.end(), [](const MiningProjectileVisual& projectile) {
            return projectile.age >= projectile.lifetime;
        }),
        mining.combatProjectiles.end());
    mining.damageNumbers.erase(
        std::remove_if(mining.damageNumbers.begin(), mining.damageNumbers.end(), [](const MiningDamageNumber& number) {
            return number.age >= number.lifetime;
        }),
        mining.damageNumbers.end());
}

void pushMiningProjectile(
    MiningRunState& mining,
    double startX,
    double startY,
    double endX,
    double endY,
    MiningCombatTeam team,
    MiningEnemyType sourceType,
    MiningElementalAffinity affinity,
    bool critical)
{
    mining.combatProjectiles.push_back({
        startX,
        startY,
        endX,
        endY,
        0.0,
        tuning::mining::projectileLifetimeSeconds,
        team,
        sourceType,
        affinity,
        critical
    });
    trimMiningCombatVisuals(mining);
}

void pushMiningDamageNumber(
    MiningRunState& mining,
    double x,
    double y,
    double amount,
    MiningCombatTeam team,
    bool critical,
    bool rigDamage)
{
    if (amount <= 0.0) {
        return;
    }
    for (MiningDamageNumber& number : mining.damageNumbers) {
        const double dx = number.x - x;
        const double dy = number.y - y;
        if (number.kind == MiningCombatTextKind::Damage && number.team == team && number.critical == critical && number.rigDamage == rigDamage && number.age < 0.18 && dx * dx + dy * dy < 0.36) {
            number.amount += amount;
            number.age = 0.0;
            return;
        }
    }
    mining.damageNumbers.push_back({
        x,
        y,
        amount,
        0.0,
        tuning::mining::damageNumberLifetimeSeconds,
        team,
        MiningCombatTextKind::Damage,
        critical,
        rigDamage
    });
    trimMiningCombatVisuals(mining);
}

void pushMiningCombatPopup(
    MiningRunState& mining,
    double x,
    double y,
    double amount,
    MiningCombatTextKind kind)
{
    if (amount <= 0.0) {
        return;
    }
    mining.damageNumbers.push_back({
        x,
        y,
        amount,
        0.0,
        tuning::mining::damageNumberLifetimeSeconds * 1.15,
        MiningCombatTeam::Allied,
        kind,
        false,
        false
    });
    trimMiningCombatVisuals(mining);
}

std::pair<double, double> enemyMoveDirection(const MiningRunState& mining, const MiningEnemy& enemy, double dirX, double dirY)
{
    if (enemy.type != MiningEnemyType::Flying) {
        return {dirX, dirY};
    }
    const double wave = std::sin(
        mining.elapsedSeconds * tuning::mining::flyingDartFrequency +
        enemy.x * 1.73 +
        enemy.y * 0.91);
    const double dartX = dirX - dirY * wave * tuning::mining::flyingDartStrength;
    const double dartY = dirY + dirX * wave * tuning::mining::flyingDartStrength;
    const double length = std::max(0.001, std::sqrt(dartX * dartX + dartY * dartY));
    return {dartX / length, dartY / length};
}

bool applyMammalBurrow(MiningRunState& mining, int x, int y, double dt)
{
    MiningCell* target = miningCellAt(mining.terrain, x, y);
    if (!drillableCell(target) && !(target != nullptr && target->gateAssociated && target->material == MiningCellMaterial::Bedrock)) {
        return false;
    }

    target->revealed = true;
    target->feature = MiningCellFeature::OrganicBurrow;
    target->enemy = MiningEnemyType::Mammal;
    target->remainingToughness = std::max(0.0, target->remainingToughness - tuning::mining::mammalBurrowPower * dt);
    markDirty(mining.terrain, x, y);
    if (target->remainingToughness > 0.0) {
        return false;
    }

    const bool gateAssociated = target->gateAssociated;
    *target = makeCell(MiningCellMaterial::Empty, mining.depthZone);
    target->feature = MiningCellFeature::OrganicBurrow;
    target->enemy = MiningEnemyType::Mammal;
    target->revealed = true;
    target->gateAssociated = gateAssociated;
    markDirty(mining.terrain, x, y);
    if (gateAssociated) {
        markMiningGateDerivedStateDirty(mining);
    }
    return true;
}

void addEnemyDefeatReward(GameState& state, const MiningEnemy& enemy)
{
    MiningRunState& mining = state.run.mining;
    MaterialInventory gain;
    switch (enemy.type) {
    case MiningEnemyType::Ant:
    case MiningEnemyType::Flying:
        gain.common = 1;
        break;
    case MiningEnemyType::Beetle:
        gain.common = 1;
        gain.rare = 1;
        break;
    case MiningEnemyType::Elemental:
        gain.rare = 1;
        if (enemy.sourceFeature == MiningCellFeature::MinibossLair) {
            gain.exotic = 1;
        }
        break;
    case MiningEnemyType::Mammal:
        gain.rare = 2;
        gain.exotic = 1;
        break;
    case MiningEnemyType::Spawner:
        gain.common = 1;
        gain.rare = 1;
        break;
    case MiningEnemyType::None:
        break;
    }
    if (enemy.sourceFeature == MiningCellFeature::TreasureVault) {
        gain.rare += 1;
    } else if (enemy.sourceFeature == MiningCellFeature::MinibossLair) {
        gain.rare += 2;
        gain.exotic += 1;
    } else if (enemy.sourceFeature == MiningCellFeature::HiveNest) {
        gain.common += 2;
    } else if (enemy.sourceFeature == MiningCellFeature::BossChamber) {
        gain.rare += 3;
        gain.exotic += 2;
        state.meta.blueprintProgress += 2;
    }
    gain = claimMiningRichRewardBudget(mining, gain);
    if (operatorControlled(mining)) {
        spawnLooseMaterialChunks(mining, gain, enemy.x, enemy.y);
    } else {
        addMiningMaterials(mining.temporaryMaterials, gain);
        mining.cargo += materialCargo(gain);
    }
    pushMiningCombatPopup(mining, enemy.x, enemy.y, 1.0, MiningCombatTextKind::Defeat);
    if (gain.common > 0) {
        pushMiningCombatPopup(mining, enemy.x + 0.18, enemy.y + 0.20, static_cast<double>(gain.common), MiningCombatTextKind::CommonReward);
    }
    if (gain.rare > 0) {
        pushMiningCombatPopup(mining, enemy.x - 0.18, enemy.y + 0.36, static_cast<double>(gain.rare), MiningCombatTextKind::RareReward);
    }
    if (gain.exotic > 0) {
        pushMiningCombatPopup(mining, enemy.x, enemy.y + 0.52, static_cast<double>(gain.exotic), MiningCombatTextKind::ExoticReward);
    }
}

double applyDefenseDamage(GameState& state, MiningEnemy& enemy, double rawDamage, bool critical = false, bool emitNumber = false)
{
    if (!enemy.active || rawDamage <= 0.0) {
        return 0.0;
    }
    const double appliedDamage = rawDamage * (critical ? tuning::mining::alliedCritMultiplier : 1.0) * std::clamp(1.0 - enemy.armor, 0.20, 1.0);
    enemy.health = std::max(0.0, enemy.health - appliedDamage);
    if (emitNumber) {
        pushMiningDamageNumber(state.run.mining, enemy.x, enemy.y, appliedDamage, MiningCombatTeam::Allied, critical, false);
    }
    if (enemy.health <= 0.0 && enemy.active) {
        enemy.active = false;
        if (enemy.gateAssociated) {
            markMiningGateDerivedStateDirty(state.run.mining);
        }
        state.run.mining.enemiesDefeated += 1;
        addEnemyDefeatReward(state, enemy);
    }
    return appliedDamage;
}

void updateMiningOperatorSidearm(
    GameState& state,
    double dt)
{
    MiningRunState& mining = state.run.mining;
    mining.operatorFireCooldownSeconds =
        std::max(0.0, mining.operatorFireCooldownSeconds - dt);
    mining.operatorFirePulseSeconds =
        std::max(0.0, mining.operatorFirePulseSeconds - dt);
    if (!operatorControlled(mining) || !mining.firing ||
        mining.operatorFireCooldownSeconds > 0.0 ||
        mining.operatorIntegrity <= 0.0) {
        return;
    }

    const double directionLength = std::max(
        0.0001,
        std::hypot(mining.operatorAimDirX, mining.operatorAimDirY));
    const double directionX = mining.operatorAimDirX / directionLength;
    const double directionY = mining.operatorAimDirY / directionLength;
    const double startX = mining.operatorX + directionX * 0.28;
    const double startY = mining.operatorY + directionY * 0.28;
    double hitDistance = tuning::mining::operatorSidearmRangeCells;
    int hitCellX = -1;
    int hitCellY = -1;
    for (double distance = 0.15;
         distance <= tuning::mining::operatorSidearmRangeCells;
         distance += 0.10) {
        const double probeX = startX + directionX * distance;
        const double probeY = startY + directionY * distance;
        const int cellX = static_cast<int>(std::floor(probeX));
        const int cellY = static_cast<int>(std::floor(probeY));
        const MiningCell* cell = miningCellAt(mining.terrain, cellX, cellY);
        if (cell == nullptr || miningMaterialSolid(cell->material)) {
            hitDistance = distance;
            hitCellX = cellX;
            hitCellY = cellY;
            break;
        }
    }

    MiningEnemy* hitEnemy = nullptr;
    for (MiningEnemy& enemy : mining.enemies) {
        if (!enemy.active) {
            continue;
        }
        const double offsetX = enemy.x - startX;
        const double offsetY = enemy.y - startY;
        const double along = offsetX * directionX + offsetY * directionY;
        if (along < 0.0 || along > hitDistance) {
            continue;
        }
        const double perpendicular = std::abs(offsetX * directionY - offsetY * directionX);
        if (perpendicular <= std::max(0.28, enemy.effectRadius * 0.35)) {
            hitDistance = along;
            hitEnemy = &enemy;
            hitCellX = -1;
            hitCellY = -1;
        }
    }

    const double endX = startX + directionX * hitDistance;
    const double endY = startY + directionY * hitDistance;
    pushMiningProjectile(
        mining,
        startX,
        startY,
        endX,
        endY,
        MiningCombatTeam::Allied,
        MiningEnemyType::None,
        MiningElementalAffinity::None,
        false);
    if (hitEnemy != nullptr) {
        (void)applyDefenseDamage(
            state,
            *hitEnemy,
            tuning::mining::operatorSidearmDamage,
            false,
            true);
    } else if (hitCellX >= 0 && hitCellY >= 0) {
        const MiningCell* hitCell =
            miningCellAt(mining.terrain, hitCellX, hitCellY);
        if (hitCell != nullptr &&
            hitCell->material != MiningCellMaterial::ArtifactCache) {
            const MiningDrillStats terrainStats =
                miningOperatorDrillStats();
            (void)applyDrillDamage(
                state,
                terrainStats,
                hitCellX,
                hitCellY,
                tuning::mining::operatorSidearmIntervalSeconds *
                    tuning::mining::operatorSidearmTerrainOutputScale);
        }
    }
    mining.operatorFireCooldownSeconds =
        tuning::mining::operatorSidearmIntervalSeconds;
    mining.operatorFirePulseSeconds = 0.12;
}

void applyElementalContact(GameState& state, const MiningEnemy& enemy, double shieldRelief, double dt)
{
    MiningRunState& mining = state.run.mining;
    if (enemy.type != MiningEnemyType::Elemental || enemy.affinity == MiningElementalAffinity::None) {
        return;
    }
    const double exposureScale = std::clamp(1.0 - shieldRelief, 0.20, 1.0);
    mining.elementalExposureSeconds += dt;
    switch (enemy.affinity) {
    case MiningElementalAffinity::Thermal:
        mining.drillHeat = std::min(1.0, mining.drillHeat + tuning::mining::elementalHeatRisePerSecond * exposureScale * dt);
        break;
    case MiningElementalAffinity::Cryo:
        mining.movementSlowSeconds = std::max(mining.movementSlowSeconds, tuning::mining::elementalCryoSlowDurationSeconds * exposureScale);
        mining.movementSlowScale = std::min(mining.movementSlowScale, tuning::mining::elementalCryoSlowScale + shieldRelief * 0.24);
        break;
    case MiningElementalAffinity::Radiation:
        mining.hazardDelta += tuning::mining::elementalRadiationHazardPerSecond * exposureScale * dt;
        break;
    case MiningElementalAffinity::Toxic: {
        const double toxicDamage = tuning::mining::elementalToxicIntegrityDamagePerSecond * exposureScale * dt;
        applyControlledActorDamage(mining, toxicDamage);
        mining.enemyDamageTaken += toxicDamage;
        break;
    }
    case MiningElementalAffinity::None:
        break;
    }
}

void applyMiningEnemyCombat(GameState& state, const ContentCatalog& catalog, double dt)
{
    MiningRunState& mining = state.run.mining;
    const double actorX = controlledActorX(mining);
    const double actorY = controlledActorY(mining);
    const bool rigTarget = !operatorControlled(mining);
    const MiningArenaRules rules = activeMiningArenaRules(mining);
    advanceMiningCombatVisuals(mining, dt);
    mining.movementSlowSeconds = std::max(0.0, mining.movementSlowSeconds - dt);
    if (mining.movementSlowSeconds <= 0.0) {
        mining.movementSlowScale = 1.0;
    }
    mining.alliedFireCooldownSeconds = std::max(0.0, mining.alliedFireCooldownSeconds - dt);
    mining.areaControlPulseCooldownSeconds = std::max(0.0, mining.areaControlPulseCooldownSeconds - dt);
    updateMiningEnemySpawners(mining, rules, dt);
    if (mining.enemies.empty()) {
        return;
    }

    DefenseDroneCoordinator defenseCoordinator(mining);
    defenseCoordinator.synchronizeAssignments();
    const MiniDroneLoadoutEffects drones = miniDroneLoadoutEffects(state, catalog);
    const double defenseDamage = tuning::mining::baseDefenseDamagePerSecond + drones.sentryDamagePerSecond;
    const double incomingRelief = std::clamp(drones.enemyDamageRelief + drones.enemyEncounterRelief * 0.75, 0.0, 0.70);
    const double shieldRelief = std::clamp(incomingRelief + drones.environmentalShieldRelief, 0.0, 0.82);
    const double areaRangeSq = tuning::mining::areaControlRangeCells * tuning::mining::areaControlRangeCells;
    const double alliedCritChance = std::clamp(tuning::mining::alliedCritChance + drones.alliedCritChanceBonus, 0.0, tuning::mining::alliedCritChanceMaximum);
    const double alliedShotInterval = tuning::mining::alliedShotIntervalSeconds / (1.0 + std::clamp(drones.alliedFireRateBonus, 0.0, tuning::mining::alliedFireRateBonusMaximum));
    const int sentryShots = 1 + std::clamp(drones.sentryVolleyBonus, 0, tuning::mining::alliedSentryVolleyMaximum);
    std::vector<std::pair<double, int>> sentryTargets;
    const double defenseRangeSq = tuning::mining::defenseRangeCells * tuning::mining::defenseRangeCells;

    for (std::size_t i = 0; i < mining.enemies.size(); ++i) {
        MiningEnemy& enemy = mining.enemies[i];
        if (!enemy.active) {
            continue;
        }
        const double dx = actorX - enemy.x;
        const double dy = actorY - enemy.y;
        const double distanceSq = dx * dx + dy * dy;
        if (distanceSq <= defenseRangeSq) {
            sentryTargets.push_back({distanceSq, static_cast<int>(i)});
        }
    }
    std::sort(sentryTargets.begin(), sentryTargets.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.first < rhs.first;
    });

    std::vector<MiningMiniDroneAgent*> attackDrones;
    for (MiningMiniDroneAgent& agent : mining.miniDrones) {
        if (agent.role == MiniDroneRole::Attack) {
            attackDrones.push_back(&agent);
        }
    }
    if (!attackDrones.empty() && defenseDamage > 0.0) {
        const double damagePerShot = defenseDamage * alliedShotInterval / static_cast<double>(attackDrones.size());
        for (std::size_t attackIndex = 0; attackIndex < attackDrones.size(); ++attackIndex) {
            MiningMiniDroneAgent& agent = *attackDrones[attackIndex];
            if (agent.actionCooldownSeconds > 0.0 || !miniDroneTargetEnemyValid(mining, agent)) {
                continue;
            }
            MiningEnemy& primary = mining.enemies[static_cast<std::size_t>(agent.targetEnemyIndex)];
            if (miniDroneDistanceSquared(agent, primary.x, primary.y) > defenseRangeSq) {
                continue;
            }
            for (int shot = 0; shot < sentryShots; ++shot) {
                int targetIndex = agent.targetEnemyIndex;
                if (!mining.enemies[static_cast<std::size_t>(targetIndex)].active) {
                    const auto extra = std::find_if(sentryTargets.begin(), sentryTargets.end(), [&](const auto& candidate) {
                        return candidate.second != agent.targetEnemyIndex && mining.enemies[static_cast<std::size_t>(candidate.second)].active;
                    });
                    if (extra != sentryTargets.end()) {
                        targetIndex = extra->second;
                    }
                }
                MiningEnemy& target = mining.enemies[static_cast<std::size_t>(targetIndex)];
                if (!target.active) {
                    continue;
                }
                const bool critical = deterministicCombatCrit(
                    mining,
                    target,
                    alliedCritChance,
                    101 + static_cast<int>(attackIndex) * 31 + shot * 17);
                const double targetDx = target.x - agent.x;
                const double targetDy = target.y - agent.y;
                const double targetDistance = std::max(0.001, std::sqrt(targetDx * targetDx + targetDy * targetDy));
                const double forwardX = targetDx / targetDistance;
                const double forwardY = targetDy / targetDistance;
                const double sideX = -forwardY;
                const double sideY = forwardX;
                const double muzzleSide = (mining.combatSequence + static_cast<int>(attackIndex) + shot) % 2 == 0 ? -1.0 : 1.0;
                pushMiningProjectile(
                    mining,
                    agent.x + forwardX * 0.54 + sideX * muzzleSide * 0.30,
                    agent.y + forwardY * 0.54 + sideY * muzzleSide * 0.30,
                    target.x,
                    target.y,
                    MiningCombatTeam::Allied,
                    target.type,
                    target.affinity,
                    critical);
                const double appliedDamage = applyDefenseDamage(state, target, damagePerShot, critical, true);
                mining.defenseDamageDealt += appliedDamage;
            }
            agent.actionCooldownSeconds = alliedShotInterval;
            if (!miniDroneTargetEnemyValid(mining, agent)) {
                agent.targetEnemyIndex = -1;
                agent.behavior = MiningMiniDroneBehavior::Returning;
            }
        }
    } else if (!sentryTargets.empty() && defenseDamage > 0.0 && mining.alliedFireCooldownSeconds <= 0.0) {
        const int shots = std::min(sentryShots, static_cast<int>(sentryTargets.size()));
        for (int shot = 0; shot < shots; ++shot) {
            MiningEnemy& target = mining.enemies[static_cast<std::size_t>(sentryTargets[static_cast<std::size_t>(shot)].second)];
            if (!target.active) {
                continue;
            }
            const bool critical = deterministicCombatCrit(mining, target, alliedCritChance, 101 + shot * 17);
            pushMiningProjectile(
                mining,
                actorX,
                actorY,
                target.x,
                target.y,
                MiningCombatTeam::Allied,
                target.type,
                target.affinity,
                critical);
            const double appliedDamage = applyDefenseDamage(state, target, defenseDamage * alliedShotInterval, critical, true);
            mining.defenseDamageDealt += appliedDamage;
        }
        mining.alliedFireCooldownSeconds = alliedShotInterval;
    }

    if (drones.areaControlDamagePerSecond > 0.0 && mining.areaControlPulseCooldownSeconds <= 0.0) {
        for (MiningEnemy& enemy : mining.enemies) {
            if (!enemy.active) {
                continue;
            }
            const double dx = actorX - enemy.x;
            const double dy = actorY - enemy.y;
            if (dx * dx + dy * dy > areaRangeSq) {
                continue;
            }
            const bool critical = deterministicCombatCrit(mining, enemy, alliedCritChance * 0.55, 211);
            const double appliedDamage = applyDefenseDamage(state, enemy, drones.areaControlDamagePerSecond * tuning::mining::areaControlPulseSeconds, critical, true);
            mining.defenseDamageDealt += appliedDamage;
            mining.areaControlDamageDealt += appliedDamage;
        }
        mining.areaControlPulseCooldownSeconds = tuning::mining::areaControlPulseSeconds;
    }

    for (MiningEnemy& enemy : mining.enemies) {
        if (!enemy.active) {
            continue;
        }
        enemy.attackCooldownSeconds = std::max(0.0, enemy.attackCooldownSeconds - dt);
        const double dx = actorX - enemy.x;
        const double dy = actorY - enemy.y;
        const double distance = std::max(0.001, std::sqrt(dx * dx + dy * dy));
        const double dirX = dx / distance;
        const double dirY = dy / distance;
        double desiredDirX = dirX;
        double desiredDirY = dirY;
        if (enemyUsesRangedAttack(enemy.type)) {
            if (distance < tuning::mining::enemyRangedStandoffCells) {
                desiredDirX = -dirX;
                desiredDirY = -dirY;
            } else if (distance <= tuning::mining::enemyRangedStandoffCells + 0.60) {
                const double wave = std::sin(mining.elapsedSeconds * 4.7 + enemy.x * 0.41 + enemy.y * 0.73);
                desiredDirX = -dirY * (wave >= 0.0 ? 1.0 : -1.0);
                desiredDirY = dirX * (wave >= 0.0 ? 1.0 : -1.0);
            }
        }
        const auto [moveDirX, moveDirY] = enemyMoveDirection(mining, enemy, desiredDirX, desiredDirY);
        const double areaSlow = distance * distance <= areaRangeSq ? drones.enemySlow : 0.0;
        const double speedScale = std::clamp(1.0 - areaSlow, 0.40, 1.0);
        enemy.velocityX = moveDirX * enemy.speed * speedScale;
        enemy.velocityY = moveDirY * enemy.speed * speedScale;
        const double nextX = enemy.x + enemy.velocityX * dt;
        const double nextY = enemy.y + enemy.velocityY * dt;
        if (enemyIgnoresTerrain(enemy.type) || canOccupy(mining.terrain, nextX, nextY)) {
            enemy.x = std::clamp(nextX, 1.0, static_cast<double>(mining.terrain.width - 2));
            enemy.y = std::clamp(nextY, 1.0, static_cast<double>(mining.terrain.height - 2));
        } else if (enemy.type == MiningEnemyType::Mammal &&
            applyMammalBurrow(
                mining,
                std::clamp(static_cast<int>(std::floor(nextX)), 0, mining.terrain.width - 1),
                std::clamp(static_cast<int>(std::floor(nextY)), 0, mining.terrain.height - 1),
                dt) &&
            canOccupy(mining.terrain, nextX, nextY)) {
            enemy.x = std::clamp(nextX, 1.0, static_cast<double>(mining.terrain.width - 2));
            enemy.y = std::clamp(nextY, 1.0, static_cast<double>(mining.terrain.height - 2));
        } else if (enemyIgnoresTerrain(enemy.type) || canOccupy(mining.terrain, nextX, enemy.y)) {
            enemy.x = std::clamp(nextX, 1.0, static_cast<double>(mining.terrain.width - 2));
        } else if (enemy.type == MiningEnemyType::Mammal &&
            applyMammalBurrow(
                mining,
                std::clamp(static_cast<int>(std::floor(nextX)), 0, mining.terrain.width - 1),
                std::clamp(static_cast<int>(std::floor(enemy.y)), 0, mining.terrain.height - 1),
                dt) &&
            canOccupy(mining.terrain, nextX, enemy.y)) {
            enemy.x = std::clamp(nextX, 1.0, static_cast<double>(mining.terrain.width - 2));
        } else if (enemyIgnoresTerrain(enemy.type) || canOccupy(mining.terrain, enemy.x, nextY)) {
            enemy.y = std::clamp(nextY, 1.0, static_cast<double>(mining.terrain.height - 2));
        } else if (enemy.type == MiningEnemyType::Mammal &&
            applyMammalBurrow(
                mining,
                std::clamp(static_cast<int>(std::floor(enemy.x)), 0, mining.terrain.width - 1),
                std::clamp(static_cast<int>(std::floor(nextY)), 0, mining.terrain.height - 1),
                dt) &&
            canOccupy(mining.terrain, enemy.x, nextY)) {
            enemy.y = std::clamp(nextY, 1.0, static_cast<double>(mining.terrain.height - 2));
        }

        if (enemy.type == MiningEnemyType::Elemental && distance <= std::max(0.0, enemy.effectRadius)) {
            applyElementalContact(state, enemy, shieldRelief, dt);
        }

        if (enemyUsesMeleeAttack(enemy.type) && distance <= tuning::mining::enemyContactRadiusCells && enemy.attackCooldownSeconds <= 0.0) {
            const bool critical = deterministicCombatCrit(mining, enemy, tuning::mining::enemyCritChance, 307);
            const double rawDamage = enemy.damagePerSecond * tuning::mining::enemyDamageScale * tuning::mining::enemyMeleeAttackIntervalSeconds * (critical ? tuning::mining::enemyCritMultiplier : 1.0);
            const DefenseShieldImpact shieldImpact = defenseCoordinator.absorbIncomingDamage(enemy.x, enemy.y, rawDamage);
            const double damage = shieldImpact.remainingDamage * (1.0 - shieldRelief);
            applyControlledActorDamage(mining, damage);
            mining.enemyDamageTaken += damage;
            mining.environmentalShieldAbsorbed += shieldImpact.absorbedDamage +
                std::max(0.0, shieldImpact.remainingDamage - damage);
            mining.contactIntensity = std::max(
                mining.contactIntensity,
                shieldImpact.absorbedDamage > 0.0 ? 0.38 : 0.65);
            if (damage > 0.00001) {
                pushMiningDamageNumber(mining, actorX, actorY, damage * 100.0, MiningCombatTeam::Enemy, critical, rigTarget);
            }
            enemy.attackCooldownSeconds = tuning::mining::enemyMeleeAttackIntervalSeconds;
            if (drones.reactiveArmorDamagePerSecond > 0.0) {
                const double appliedDamage = applyDefenseDamage(state, enemy, drones.reactiveArmorDamagePerSecond * tuning::mining::enemyMeleeAttackIntervalSeconds, false, true);
                mining.defenseDamageDealt += appliedDamage;
                mining.reactiveArmorDamageDealt += appliedDamage;
            }
        } else if (enemyUsesRangedAttack(enemy.type) && distance <= tuning::mining::enemyRangedAttackRangeCells && enemy.attackCooldownSeconds <= 0.0) {
            const bool critical = deterministicCombatCrit(mining, enemy, tuning::mining::enemyCritChance, 401);
            const double rawDamage = enemy.damagePerSecond * tuning::mining::enemyDamageScale * tuning::mining::enemyRangedAttackIntervalSeconds * (critical ? tuning::mining::enemyCritMultiplier : 1.0);
            const DefenseShieldImpact shieldImpact = defenseCoordinator.absorbIncomingDamage(enemy.x, enemy.y, rawDamage);
            const double damage = shieldImpact.remainingDamage * (1.0 - shieldRelief);
            pushMiningProjectile(
                mining,
                enemy.x,
                enemy.y,
                shieldImpact.impactX,
                shieldImpact.impactY,
                MiningCombatTeam::Enemy,
                enemy.type,
                enemy.affinity,
                critical);
            applyControlledActorDamage(mining, damage);
            mining.enemyDamageTaken += damage;
            mining.environmentalShieldAbsorbed += shieldImpact.absorbedDamage +
                std::max(0.0, shieldImpact.remainingDamage - damage);
            mining.contactIntensity = std::max(
                mining.contactIntensity,
                shieldImpact.absorbedDamage > 0.0 ? 0.30 : 0.50);
            if (damage > 0.00001) {
                pushMiningDamageNumber(mining, actorX, actorY, damage * 100.0, MiningCombatTeam::Enemy, critical, rigTarget);
            }
            enemy.attackCooldownSeconds = tuning::mining::enemyRangedAttackIntervalSeconds;
        }
    }
}

void refreshTargetCell(MiningRunState& mining)
{
    double dx = controlledAimX(mining);
    double dy = controlledAimY(mining);
    double length = std::sqrt(dx * dx + dy * dy);
    if (length < 0.05) {
        dx = mining.moveX;
        dy = mining.moveY;
        length = std::sqrt(dx * dx + dy * dy);
    }
    if (length < 0.05) {
        dx = 0.0;
        dy = 1.0;
        length = 1.0;
    }
    dx /= length;
    dy /= length;

    const double actorX = controlledActorX(mining);
    const double actorY = controlledActorY(mining);
    const double drillRange = controlledDrillRange(mining);
    const double maxTipX = std::clamp(
        actorX + dx * drillRange,
        0.0,
        static_cast<double>(mining.terrain.width - 1));
    const double maxTipY = std::clamp(
        actorY + dy * drillRange,
        0.0,
        static_cast<double>(mining.terrain.height - 1));

    int targetX = std::clamp(static_cast<int>(std::floor(maxTipX)), 0, mining.terrain.width - 1);
    int targetY = std::clamp(static_cast<int>(std::floor(maxTipY)), 0, mining.terrain.height - 1);
    double tipX = maxTipX;
    double tipY = maxTipY;
    const int actorCellX = std::clamp(static_cast<int>(std::floor(actorX)), 0, mining.terrain.width - 1);
    const int actorCellY = std::clamp(static_cast<int>(std::floor(actorY)), 0, mining.terrain.height - 1);
    constexpr double step = 0.10;
    bool foundSolidOnRay = false;
    for (double distance = step; distance <= drillRange + 0.0001; distance += step) {
        const double probeX = std::clamp(
            actorX + dx * distance,
            0.0,
            static_cast<double>(mining.terrain.width - 1));
        const double probeY = std::clamp(
            actorY + dy * distance,
            0.0,
            static_cast<double>(mining.terrain.height - 1));
        const int probeCellX = std::clamp(static_cast<int>(std::floor(probeX)), 0, mining.terrain.width - 1);
        const int probeCellY = std::clamp(static_cast<int>(std::floor(probeY)), 0, mining.terrain.height - 1);
        if (probeCellX == actorCellX && probeCellY == actorCellY) {
            continue;
        }
        const MiningCell* probe = miningCellAt(mining.terrain, probeCellX, probeCellY);
        if (probe != nullptr && miningMaterialSolid(probe->material)) {
            targetX = probeCellX;
            targetY = probeCellY;
            tipX = probeX;
            tipY = probeY;
            foundSolidOnRay = true;
            break;
        }
    }

    const MiningCell* directTarget = miningCellAt(mining.terrain, targetX, targetY);
    if (!foundSolidOnRay && !drillableCell(directTarget)) {
        (void)findNearbyDrillTarget(mining, dx, dy, targetX, targetY, tipX, tipY);
    }

    mining.targetCellX = targetX;
    mining.targetCellY = targetY;
    mining.targetTipX = tipX;
    mining.targetTipY = tipY;
    if (const MiningCell* cell = miningCellAt(mining.terrain, targetX, targetY)) {
        mining.targetMaterial = cell->material;
        mining.targetRemainingToughness = cell->remainingToughness;
        mining.targetMaxToughness = cell->maxToughness;
    }
}

void storeActiveDepthLayer(MiningRunState& mining)
{
    mining.depthLayers.erase(
        std::remove_if(
            mining.depthLayers.begin(),
            mining.depthLayers.end(),
            [&](const MiningDepthLayerState& layer) { return layer.depthZone == mining.depthZone; }),
        mining.depthLayers.end());
    MiningDepthLayerState layer;
    layer.depthZone = mining.depthZone;
    layer.terrain = std::move(mining.terrain);
    layer.enemies = std::move(mining.enemies);
    layer.artifact = std::move(mining.artifact);
    layer.looseChunks = std::move(mining.looseChunks);
    layer.gate = std::move(mining.gate);
    layer.downwardTransitionX = mining.downwardTransitionX;
    layer.hasDownwardTransition = mining.hasDownwardTransition;
    mining.depthLayers.push_back(std::move(layer));
}

bool restoreDepthLayer(MiningRunState& mining, int depthZone)
{
    const auto found = std::find_if(
        mining.depthLayers.begin(),
        mining.depthLayers.end(),
        [depthZone](const MiningDepthLayerState& layer) { return layer.depthZone == depthZone; });
    if (found == mining.depthLayers.end()) {
        return false;
    }
    mining.depthZone = found->depthZone;
    mining.terrain = std::move(found->terrain);
    mining.enemies = std::move(found->enemies);
    mining.artifact = std::move(found->artifact);
    mining.looseChunks = std::move(found->looseChunks);
    mining.gate = std::move(found->gate);
    mining.downwardTransitionX = found->downwardTransitionX;
    mining.hasDownwardTransition = found->hasDownwardTransition;
    mining.depthLayers.erase(found);
    std::fill(
        mining.terrain.dirtyChunks.begin(),
        mining.terrain.dirtyChunks.end(),
        static_cast<std::uint8_t>(1));
    return true;
}

void resetMiningFollowersAfterDepthTransition(MiningRunState& mining)
{
    transferMiniDroneSwarmAnchor(
        mining,
        mining.operatorMode,
        mining.operatorMode,
        true);
}

void carveMiningReturnShaft(MiningTerrain& terrain)
{
    const int leftX = miningReturnShaftLeftX(terrain);
    for (int y = 0; y < std::max(0, terrain.height - 1); ++y) {
        for (int x = leftX; x <= leftX + 1; ++x) {
            if (MiningCell* cell = miningCellAt(terrain, x, y)) {
                if (cell->material == MiningCellMaterial::ArtifactCache ||
                    cell->gateAssociated ||
                    cell->cocoonLayer >= 0) {
                    continue;
                }
                *cell = makeCell(MiningCellMaterial::Empty, terrain.depthZone);
                cell->feature = MiningCellFeature::MainTunnel;
                markDirty(terrain, x, y);
            }
        }
    }
}

void applySuitOnlyPassageNetwork(
    MiningTerrain& terrain,
    std::uint64_t seed,
    int depthZone)
{
    if (terrain.width < 16 || terrain.height < 14) {
        return;
    }

    const double laneHash = unitHash(seed, terrain.width, terrain.height, depthZone, 0xE7A5ULL);
    const int targetY = std::clamp(
        terrain.height / 3 +
            static_cast<int>(laneHash * static_cast<double>(std::max(1, terrain.height / 3))),
        5,
        terrain.height - 6);
    int entranceX = terrain.width / 2;
    int bestDistance = terrain.width;
    for (int x = 3; x < terrain.width - 3; ++x) {
        const MiningCell* cell = miningCellAt(terrain, x, targetY);
        if (cell == nullptr || miningMaterialSolid(cell->material)) {
            continue;
        }
        const int centerDistance = std::abs(x - terrain.width / 2);
        if (centerDistance < bestDistance) {
            entranceX = x;
            bestDistance = centerDistance;
        }
    }

    if (bestDistance == terrain.width) {
        if (MiningCell* entrance = miningCellAt(terrain, entranceX, targetY)) {
            *entrance = makeCell(MiningCellMaterial::Empty, depthZone);
            entrance->feature = MiningCellFeature::MainTunnel;
        }
    }

    const bool carveRight =
        unitHash(seed, entranceX, targetY, depthZone, 0x5A17ULL) >= 0.5;
    const int direction = carveRight ? 1 : -1;
    const int passageLength = std::clamp(terrain.width / 9, 4, 7);
    const int pocketCenterX = std::clamp(
        entranceX + direction * passageLength,
        3,
        terrain.width - 4);

    auto carveSuitCell = [&](int x, int y) {
        MiningCell* cell = miningCellAt(terrain, x, y);
        if (cell == nullptr) {
            return;
        }
        *cell = makeCell(MiningCellMaterial::Empty, depthZone);
        cell->revealed = false;
        cell->feature = MiningCellFeature::MainTunnel;
        cell->suitOnlyPassage = true;
    };

    for (int step = 1; step <= passageLength; ++step) {
        carveSuitCell(
            std::clamp(entranceX + direction * step, 2, terrain.width - 3),
            targetY);
    }
    for (int y = targetY - 1; y <= targetY + 1; ++y) {
        for (int x = pocketCenterX - 1; x <= pocketCenterX + 1; ++x) {
            carveSuitCell(x, y);
        }
    }
}

void transitionDepthZone(GameState& state, const ContentCatalog& catalog, int direction)
{
    MiningRunState& mining = state.run.mining;
    const Destination* destination = catalog.findDestination(mining.destinationId);
    if (destination == nullptr || direction == 0) {
        return;
    }
    const int targetDepth = mining.depthZone + (direction > 0 ? 1 : -1);
    if (targetDepth < mining.entryDepthZone) {
        return;
    }

    const bool suitTravels = operatorControlled(mining);
    const bool tetheredRigTravels = suitTravels && mining.operatorRigTethered &&
        !mining.rigDisabled && mining.rigDepthZone == mining.depthZone;
    const double transitionX = controlledActorX(mining);
    MiningArtifactObject travelingArtifact;
    const MiningCell* artifactCell = mining.artifact.present
        ? miningCellAt(
              mining.terrain,
              static_cast<int>(std::floor(mining.artifact.x)),
              static_cast<int>(std::floor(mining.artifact.y)))
        : nullptr;
    const bool artifactTravels =
        mining.artifact.present &&
        mining.artifact.tethered &&
        (artifactCell == nullptr || !artifactCell->suitOnlyPassage);
    if (artifactTravels) {
        travelingArtifact = std::move(mining.artifact);
        mining.artifact = {};
    } else if (mining.artifact.present && mining.artifact.tethered) {
        mining.artifact.tethered = false;
        state.statusLine =
            "The artifact cannot pass through the suit-only aperture; tether released.";
    }
    if (direction > 0) {
        mining.downwardTransitionX = transitionX;
        mining.hasDownwardTransition = true;
        // Only a layer the player has left behind becomes a return route. A
        // fresh deployment depth remains ordinary mining terrain.
        carveMiningReturnShaft(mining.terrain);
    }
    storeActiveDepthLayer(mining);

    const bool revisiting = restoreDepthLayer(mining, targetDepth);
    const MiningDrillStats stats = miningDrillStats(state, catalog);
    const MiningArenaRules arenaRules = activeMiningArenaRules(mining);
    if (!revisiting) {
        mining.depthZone = targetDepth;
        mining.terrain = generateMiningTerrainForRules(
            state,
            *destination,
            mining.siteProfile,
            mining.depthZone,
            stats.terrainWidth,
            stats.terrainHeight,
            arenaRules,
            mining.miningSiteBiome);
        normalizeRichTerrainDeposits(mining.terrain, arenaRules, mining.rewardBudget, 0, 0);
        applyMiningTerrainToughnessScale(mining.terrain, arenaRules.terrainToughnessScale);
        mining.enemies.clear();
        spawnMiningEnemies(mining, *destination, arenaRules);
        mining.artifact = {};
        mining.looseChunks.clear();
        mining.gate = {};
        mining.downwardTransitionX = 0.0;
        mining.hasDownwardTransition = false;
        if (direction > 0) {
            mining.hazardDelta += tuning::mining::depthHazardRisk;
            mining.deepestDepthZone = std::max(mining.deepestDepthZone, mining.depthZone);
        }
    }

    if (direction < 0) {
        // A new layer generated while climbing is already behind the deeper
        // mining level, so it receives the clear route up to the ship.
        carveMiningReturnShaft(mining.terrain);
    }

    double arrivalX = 0.0;
    double arrivalY = 0.0;
    if (direction > 0) {
        arrivalX = static_cast<double>(mining.terrain.width) * 0.5;
        arrivalY = 4.0;
    } else {
        arrivalX = mining.hasDownwardTransition
            ? std::clamp(mining.downwardTransitionX, 1.0, static_cast<double>(mining.terrain.width - 2))
            : static_cast<double>(mining.terrain.width) * 0.5;
        arrivalY = std::max(2.0, static_cast<double>(mining.terrain.height) - 3.5);
    }
    if (suitTravels) {
        mining.operatorX = arrivalX;
        mining.operatorY = arrivalY;
        if (tetheredRigTravels) {
            mining.droneX = arrivalX;
            mining.droneY = std::clamp(
                arrivalY + (direction > 0 ? -0.9 : 0.9),
                1.0,
                static_cast<double>(mining.terrain.height - 2));
            mining.rigVelocityX = 0.0;
            mining.rigVelocityY = 0.0;
            mining.rigDepthZone = mining.depthZone;
        }
    } else {
        mining.droneX = arrivalX;
        mining.droneY = arrivalY;
        mining.rigDepthZone = mining.depthZone;
    }
    if (artifactTravels) {
        mining.artifact = std::move(travelingArtifact);
        mining.artifact.x = arrivalX;
        mining.artifact.y = std::clamp(
            arrivalY + (direction > 0 ? -1.5 : 1.5),
            1.0,
            static_cast<double>(mining.terrain.height - 2));
        mining.artifact.velocityX = 0.0;
        mining.artifact.velocityY = 0.0;
    }
    mining.aimX = arrivalX;
    mining.aimY = arrivalY + (direction > 0 ? 1.0 : -1.0);
    mining.aimDirX = 0.0;
    mining.aimDirY = direction > 0 ? 1.0 : -1.0;
    if (suitTravels) {
        mining.operatorAimDirX = mining.aimDirX;
        mining.operatorAimDirY = mining.aimDirY;
    } else {
        mining.hullDirX = 0.0;
        mining.hullDirY = mining.aimDirY;
    }
    mining.depthTransitionCooldownSeconds = 0.65;
    resetMiningFollowersAfterDepthTransition(mining);
    if (mining.depthZone == mining.entryDepthZone) {
        revealAround(mining, mining.returnZoneX, mining.returnZoneY, tuning::mining::passiveLightRadius);
    }
    revealAround(mining, arrivalX, arrivalY, tuning::mining::passiveLightRadius);
    markMiningGateDerivedStateDirty(mining);
    refreshTargetCell(mining);
    const int levelsFromShip = std::max(0, mining.depthZone);
    if (direction > 0) {
        state.statusLine = "Descended to depth +" + std::to_string(levelsFromShip) +
            ". Ship is " + std::to_string(levelsFromShip) + (levelsFromShip == 1 ? " level" : " levels") + " above.";
    } else if (levelsFromShip == 0) {
        state.statusLine = "Surface reached. Return to the ship.";
    } else {
        state.statusLine = "Ascended to depth +" + std::to_string(levelsFromShip) +
            ". Ship is " + std::to_string(levelsFromShip) + (levelsFromShip == 1 ? " level" : " levels") + " above.";
    }
}

void approachVelocity(
    double& velocityX,
    double& velocityY,
    double targetVelocityX,
    double targetVelocityY,
    double maximumDelta)
{
    const double deltaX = targetVelocityX - velocityX;
    const double deltaY = targetVelocityY - velocityY;
    const double deltaLength = std::hypot(deltaX, deltaY);
    if (deltaLength <= maximumDelta || deltaLength <= 0.0001) {
        velocityX = targetVelocityX;
        velocityY = targetVelocityY;
        return;
    }
    velocityX += deltaX / deltaLength * maximumDelta;
    velocityY += deltaY / deltaLength * maximumDelta;
}

void clampActorSpeed(double& velocityX, double& velocityY, double maximumSpeed)
{
    const double speed = std::hypot(velocityX, velocityY);
    if (speed <= maximumSpeed || speed <= 0.0001) {
        return;
    }
    velocityX = velocityX / speed * maximumSpeed;
    velocityY = velocityY / speed * maximumSpeed;
}

void simulateMiningActorMotion(
    GameState& state,
    const MiningDrillStats& drillStats,
    const MiningLoadStats& loadStats,
    const MiningArenaRules& arenaRules,
    bool suit,
    bool acceptsInput,
    double dt)
{
    MiningRunState& mining = state.run.mining;
    double& positionX = suit ? mining.operatorX : mining.droneX;
    double& positionY = suit ? mining.operatorY : mining.droneY;
    double& velocityX = suit ? mining.operatorVelocityX : mining.rigVelocityX;
    double& velocityY = suit ? mining.operatorVelocityY : mining.rigVelocityY;
    const double colliderRadius = suit
        ? tuning::mining::operatorColliderRadiusCells
        : tuning::mining::rigColliderRadiusCells;
    const bool allowSuitOnlyPassage = suit;

    velocityX += mining.gravityDirectionX * mining.gravityStrength * dt;
    velocityY += mining.gravityDirectionY * mining.gravityStrength * dt;

    if (!suit && mining.rigTethered && !mining.rigDisabled) {
        // The ship winch follows the return shaft: on a deeper layer it pulls
        // the rig toward that layer's upper boundary, then the normal depth
        // transition carries the tether through the previously carved route.
        const double targetX = mining.returnZoneX;
        const double targetY = mining.depthZone == mining.entryDepthZone
            ? mining.returnZoneY
            : 1.5;
        const double tetherDx = targetX - positionX;
        const double tetherDy = targetY - positionY;
        const double tetherDistance = std::hypot(tetherDx, tetherDy);
        if (tetherDistance > tuning::mining::rigTetherRestLengthCells) {
            const double pull = std::min(
                tuning::mining::rigTetherPullAccelerationCellsPerSecondSquared,
                (tetherDistance - tuning::mining::rigTetherRestLengthCells) * 2.4);
            velocityX += tetherDx / tetherDistance * pull * dt;
            velocityY += tetherDy / tetherDistance * pull * dt;
            const double damping = std::clamp(1.0 - tuning::mining::rigTetherDamping * dt, 0.0, 1.0);
            velocityX *= damping;
            velocityY *= damping;
        }
    }

    if (!suit && mining.operatorRigTethered && operatorControlled(mining) &&
        !mining.rigDisabled && mining.rigDepthZone == mining.depthZone) {
        const double tetherDx = mining.operatorX - positionX;
        const double tetherDy = mining.operatorY - positionY;
        const double tetherDistance = std::hypot(tetherDx, tetherDy);
        if (tetherDistance > tuning::mining::operatorRigTetherRestLengthCells) {
            const double extension = tetherDistance - tuning::mining::operatorRigTetherRestLengthCells;
            const double pull = std::min(
                tuning::mining::rigTetherPullAccelerationCellsPerSecondSquared,
                extension * tuning::mining::operatorRigTetherSpring);
            velocityX += tetherDx / tetherDistance * pull * dt;
            velocityY += tetherDy / tetherDistance * pull * dt;
            const double damping = std::clamp(
                1.0 - tuning::mining::operatorRigTetherDamping * dt,
                0.0,
                1.0);
            velocityX *= damping;
            velocityY *= damping;
        }
    }

    const double recovery = std::clamp(mining.contactSpeedRecovery, 0.0, 1.0);
    const double easedRecovery = recovery * recovery * (3.0 - 2.0 * recovery);
    const double contactSpeedScale =
        tuning::mining::postContactMinSpeedScale +
        (1.0 - tuning::mining::postContactMinSpeedScale) * easedRecovery;
    const double baseMaximumSpeed = suit
        ? tuning::mining::operatorSpeedCellsPerSecond
        : drillStats.speed;
    const double maximumSpeed = std::max(
        0.25,
        baseMaximumSpeed *
            std::clamp(mining.movementSlowScale, 0.40, 1.0) *
            loadStats.speedMultiplier *
            contactSpeedScale);
    const double acceleration = suit
        ? tuning::mining::operatorAccelerationCellsPerSecondSquared
        : tuning::mining::rigAccelerationCellsPerSecondSquared;
    const double braking = suit
        ? tuning::mining::operatorBrakingCellsPerSecondSquared
        : tuning::mining::rigBrakingCellsPerSecondSquared;

    const double inputLength = acceptsInput
        ? std::hypot(mining.moveX, mining.moveY)
        : 0.0;
    if (inputLength > 0.01) {
        const double normalizedX = mining.moveX / std::max(1.0, inputLength);
        const double normalizedY = mining.moveY / std::max(1.0, inputLength);
        const double analogScale = std::clamp(inputLength, 0.0, 1.0);
        const double targetVelocityX = normalizedX * maximumSpeed * analogScale;
        const double targetVelocityY = normalizedY * maximumSpeed * analogScale;
        const double alongInput = velocityX * normalizedX + velocityY * normalizedY;
        const bool reversing = alongInput < -0.01;
        const double rate = reversing ? braking : acceleration;
        approachVelocity(
            velocityX,
            velocityY,
            targetVelocityX,
            targetVelocityY,
            rate * dt);
    }
    clampActorSpeed(velocityX, velocityY, maximumSpeed);

    const double minimumX = 1.0 + colliderRadius;
    const double maximumX =
        static_cast<double>(mining.terrain.width - 1) - colliderRadius;
    const double minimumY = 1.0 + colliderRadius;
    const double maximumY =
        static_cast<double>(mining.terrain.height - 1) - colliderRadius;
    const double nextX = std::clamp(positionX + velocityX * dt, minimumX, maximumX);
    const double nextY = std::clamp(positionY + velocityY * dt, minimumY, maximumY);

    if (canOccupyActor(
            mining.terrain,
            nextX,
            positionY,
            colliderRadius,
            allowSuitOnlyPassage)) {
        positionX = nextX;
    } else {
        velocityX = 0.0;
        mining.contactIntensity = std::max(mining.contactIntensity, 0.45);
    }
    if (canOccupyActor(
            mining.terrain,
            positionX,
            nextY,
            colliderRadius,
            allowSuitOnlyPassage)) {
        positionY = nextY;
    } else {
        velocityY = 0.0;
        mining.contactIntensity = std::max(mining.contactIntensity, 0.45);
    }

    if (acceptsInput && inputLength > 0.01 && !suit) {
        mining.hullDirX = mining.aimDirX;
        mining.hullDirY = mining.aimDirY;
    }
    if (acceptsInput) {
        mining.aimX =
            positionX + controlledAimX(mining) * controlledDrillRange(mining);
        mining.aimY =
            positionY + controlledAimY(mining) * controlledDrillRange(mining);
        revealAround(
            mining,
            positionX,
            positionY,
            tuning::mining::passiveLightRadius);
    }

    if (arenaRules.mechanics.contactRebound &&
        mining.contactIntensity > 0.50 &&
        inputLength > 0.01) {
        const double dirX = mining.moveX / std::max(1.0, inputLength);
        const double dirY = mining.moveY / std::max(1.0, inputLength);
        mining.recoilX = -dirX;
        mining.recoilY = -dirY;
    }
}

void updateMiningLooseChunks(MiningRunState& mining, double dt)
{
    constexpr double chunkCollider = 0.10;
    constexpr double rigCollectionRadius = 0.70;
    const bool rigSharesLayer =
        !mining.rigDisabled && mining.rigDepthZone == mining.depthZone;
    for (MiningLooseChunk& chunk : mining.looseChunks) {
        if (!chunk.active) {
            continue;
        }
        chunk.velocityX += mining.gravityDirectionX * mining.gravityStrength * dt;
        chunk.velocityY += mining.gravityDirectionY * mining.gravityStrength * dt;
        const double damping = std::pow(0.985, dt * 60.0);
        chunk.velocityX *= damping;
        chunk.velocityY *= damping;
        clampActorSpeed(chunk.velocityX, chunk.velocityY, 5.0);

        const double nextX = std::clamp(
            chunk.x + chunk.velocityX * dt,
            1.0,
            static_cast<double>(mining.terrain.width - 2));
        const double nextY = std::clamp(
            chunk.y + chunk.velocityY * dt,
            1.0,
            static_cast<double>(mining.terrain.height - 2));
        if (canOccupyActor(
                mining.terrain,
                nextX,
                chunk.y,
                chunkCollider,
                true)) {
            chunk.x = nextX;
        } else {
            chunk.velocityX *= -0.22;
        }
        if (canOccupyActor(
                mining.terrain,
                chunk.x,
                nextY,
                chunkCollider,
                true)) {
            chunk.y = nextY;
        } else {
            chunk.velocityY *= -0.22;
        }

        if (!rigSharesLayer) {
            continue;
        }
        const double dx = chunk.x - mining.droneX;
        const double dy = chunk.y - mining.droneY;
        if (dx * dx + dy * dy >
            rigCollectionRadius * rigCollectionRadius) {
            continue;
        }
        switch (chunk.material) {
        case MiningCellMaterial::ExoticVein:
            ++mining.temporaryMaterials.exotic;
            recordMiningPickup(mining, MiningPickupKind::ExoticOre, 1, chunk.x, chunk.y);
            break;
        case MiningCellMaterial::RareOre:
            ++mining.temporaryMaterials.rare;
            recordMiningPickup(mining, MiningPickupKind::RareOre, 1, chunk.x, chunk.y);
            break;
        default:
            ++mining.temporaryMaterials.common;
            recordMiningPickup(mining, MiningPickupKind::CommonOre, 1, chunk.x, chunk.y);
            break;
        }
        mining.cargo += std::max(0, chunk.cargoValue);
        chunk.active = false;
    }

    mining.looseChunks.erase(
        std::remove_if(
            mining.looseChunks.begin(),
            mining.looseChunks.end(),
            [](const MiningLooseChunk& chunk) { return !chunk.active; }),
        mining.looseChunks.end());
}

void triggerMiningFailure(GameState& state, std::string message);

bool emergencyEjectFromRig(GameState& state)
{
    MiningRunState& mining = state.run.mining;
    if (!mining.active ||
        mining.rigDisabled ||
        operatorControlled(mining) ||
        mining.droneHealth > 0.0) {
        return false;
    }

    double exitX = mining.droneX;
    double exitY = mining.droneY;
    bool found = false;
    const int rings = std::max(
        1,
        static_cast<int>(std::ceil(
            tuning::mining::operatorSafeExitSearchRadiusCells / 0.35)));
    for (int ring = 1; ring <= rings && !found; ++ring) {
        const double radius = std::min(
            tuning::mining::operatorSafeExitSearchRadiusCells,
            static_cast<double>(ring) * 0.35);
        for (int lane = 0; lane < 24; ++lane) {
            const double angle =
                static_cast<double>(lane) * (kPi * 2.0 / 24.0);
            const double candidateX =
                mining.droneX + std::cos(angle) * radius;
            const double candidateY =
                mining.droneY + std::sin(angle) * radius;
            if (canOccupyActor(
                    mining.terrain,
                    candidateX,
                    candidateY,
                    tuning::mining::operatorColliderRadiusCells,
                    true)) {
                exitX = candidateX;
                exitY = candidateY;
                found = true;
                break;
            }
        }
    }
    if (!found &&
        !canOccupyActor(
            mining.terrain,
            exitX,
            exitY,
            tuning::mining::operatorColliderRadiusCells,
            true)) {
        triggerMiningFailure(
            state,
            "Rig destroyed with no safe EVA cell. Operator recovery failed.");
        return false;
    }

    const MiningOperatorMode previous = mining.operatorMode;
    mining.rigDisabled = true;
    mining.rigTethered = false;
    mining.operatorRigTethered = false;
    mining.droneHealth = 0.0;
    mining.rigVelocityX = 0.0;
    mining.rigVelocityY = 0.0;
    mining.operatorMode = MiningOperatorMode::Jetpack;
    mining.operatorPresent = true;
    mining.operatorX = exitX;
    mining.operatorY = exitY;
    mining.operatorVelocityX = 0.0;
    mining.operatorVelocityY = 0.0;
    mining.operatorAimDirX = mining.hullDirX;
    mining.operatorAimDirY = mining.hullDirY;
    mining.moveX = 0.0;
    mining.moveY = 0.0;
    mining.drilling = false;
    mining.firing = false;
    transferMiniDroneSwarmAnchor(mining, previous, mining.operatorMode);
    revealAround(
        mining,
        mining.operatorX,
        mining.operatorY,
        tuning::mining::passiveLightRadius);
    refreshTargetCell(mining);
    state.statusLine =
        "Rig destroyed. Emergency EVA active; reach the shuttle for recovery.";
    return true;
}

void triggerMiningFailure(GameState& state, std::string message)
{
    MiningRunState& mining = state.run.mining;
    mining.failurePending = true;
    mining.failureSeconds = 0.0;
    mining.drilling = false;
    mining.firing = false;
    mining.moveX = 0.0;
    mining.moveY = 0.0;
    mining.rigTethered = false;
    mining.operatorRigTethered = false;
    mining.artifact.tethered = false;
    for (MiningMiniDroneAgent& agent : mining.miniDrones) {
        agent.velocityX = 0.0;
        agent.velocityY = 0.0;
    }
    mining.drillIntegrity = std::max(0.0, mining.drillIntegrity);
    mining.oxygenSeconds = std::max(0.0, mining.oxygenSeconds);
    mining.contactIntensity = 1.0;
    mining.scannerPulseSeconds = 0.9;
    mining.failureMessage = std::move(message);
    state.statusLine = mining.failureMessage;
}

} // namespace

MiningEnemy createMiningEnemy(MiningEnemyType type, MiningCellFeature sourceFeature, double x, double y, MiningElementalAffinity affinity)
{
    return makeMiningEnemy(type, sourceFeature, affinity, x, y);
}

MiningEnemy createMiningEnemySpawner(
    double x,
    double y,
    double health,
    MiningEnemyType spawnType,
    int maxSpawns,
    double spawnIntervalSeconds,
    MiningElementalAffinity affinity)
{
    MiningEnemy spawner = makeMiningEnemy(
        MiningEnemyType::Spawner,
        MiningCellFeature::HiveNest,
        MiningElementalAffinity::None,
        x,
        y);
    spawner.maxHealth = std::max(0.001, health);
    spawner.health = spawner.maxHealth;
    spawner.spawn.enemyType = spawnType == MiningEnemyType::Spawner ? MiningEnemyType::None : spawnType;
    spawner.spawn.affinity = spawnType == MiningEnemyType::Elemental ? affinity : MiningElementalAffinity::None;
    spawner.spawn.maxSpawns = std::max(0, maxSpawns);
    spawner.spawn.intervalSeconds = std::max(0.01, spawnIntervalSeconds);
    spawner.spawn.cooldownSeconds = spawner.spawn.intervalSeconds;
    return spawner;
}

std::string_view miningMaterialName(MiningCellMaterial material)
{
    switch (material) {
    case MiningCellMaterial::Empty:
        return "Open tunnel";
    case MiningCellMaterial::Regolith:
        return "Regolith";
    case MiningCellMaterial::HardRock:
        return "Hard rock";
    case MiningCellMaterial::CommonOre:
        return "Common ore";
    case MiningCellMaterial::RareOre:
        return "Rare ore";
    case MiningCellMaterial::ExoticVein:
        return "Exotic vein";
    case MiningCellMaterial::ArtifactCache:
        return "Artifact cache";
    case MiningCellMaterial::HazardPocket:
        return "Pressure pocket";
    case MiningCellMaterial::Bedrock:
        return "Bedrock";
    case MiningCellMaterial::FuelPocket:
        return "Fuel pocket";
    case MiningCellMaterial::OxygenPocket:
        return "Oxygen pocket";
    }
    return "Unknown";
}

std::string_view miningCellFeatureName(MiningCellFeature feature)
{
    switch (feature) {
    case MiningCellFeature::None:
        return "Natural strata";
    case MiningCellFeature::MainTunnel:
        return "Pre-dug tunnel";
    case MiningCellFeature::BranchTunnel:
        return "Branch tunnel";
    case MiningCellFeature::EncounterZone:
        return "Enemy encounter zone";
    case MiningCellFeature::TreasureVault:
        return "Treasure vault";
    case MiningCellFeature::MinibossLair:
        return "Miniboss lair";
    case MiningCellFeature::HiveNest:
        return "Hive nest";
    case MiningCellFeature::OrganicBurrow:
        return "Organic burrow";
    case MiningCellFeature::BossChamber:
        return "Boss chamber";
    }
    return "Unknown feature";
}

std::string_view miningEnemyTypeName(MiningEnemyType enemy)
{
    switch (enemy) {
    case MiningEnemyType::None:
        return "None";
    case MiningEnemyType::Ant:
        return "Ant-like creatures";
    case MiningEnemyType::Flying:
        return "Flying creatures";
    case MiningEnemyType::Beetle:
        return "Beetle-like creatures";
    case MiningEnemyType::Elemental:
        return "Elemental monsters";
    case MiningEnemyType::Mammal:
        return "Burrowing mammals";
    case MiningEnemyType::Spawner:
        return "Enemy spawner";
    }
    return "Unknown enemy";
}

std::string_view miningElementalAffinityName(MiningElementalAffinity affinity)
{
    switch (affinity) {
    case MiningElementalAffinity::None:
        return "No affinity";
    case MiningElementalAffinity::Thermal:
        return "Thermal";
    case MiningElementalAffinity::Cryo:
        return "Cryo";
    case MiningElementalAffinity::Radiation:
        return "Radiation";
    case MiningElementalAffinity::Toxic:
        return "Toxic";
    }
    return "Unknown affinity";
}

bool miningMaterialSolid(MiningCellMaterial material)
{
    return material != MiningCellMaterial::Empty;
}

double miningMaterialToughness(MiningCellMaterial material, int depthZone)
{
    (void)depthZone;
    switch (material) {
    case MiningCellMaterial::Empty:
        return 0.0;
    case MiningCellMaterial::Regolith:
        return tuning::mining::regolithToughness;
    case MiningCellMaterial::HardRock:
        return tuning::mining::hardRockToughness;
    case MiningCellMaterial::CommonOre:
        return tuning::mining::commonOreToughness;
    case MiningCellMaterial::RareOre:
        return tuning::mining::rareOreToughness;
    case MiningCellMaterial::ExoticVein:
        return tuning::mining::exoticVeinToughness;
    case MiningCellMaterial::ArtifactCache:
        return tuning::mining::artifactCacheToughness;
    case MiningCellMaterial::HazardPocket:
        return tuning::mining::regolithToughness;
    case MiningCellMaterial::Bedrock:
        return tuning::mining::bedrockToughness;
    case MiningCellMaterial::FuelPocket:
    case MiningCellMaterial::OxygenPocket:
        return tuning::mining::regolithToughness;
    }
    return 0.0;
}

MiningCell* miningCellAt(MiningTerrain& terrain, int x, int y)
{
    if (x < 0 || y < 0 || x >= terrain.width || y >= terrain.height) {
        return nullptr;
    }
    const std::size_t index = static_cast<std::size_t>(y * terrain.width + x);
    if (index >= terrain.cells.size()) {
        return nullptr;
    }
    return &terrain.cells[index];
}

const MiningCell* miningCellAt(const MiningTerrain& terrain, int x, int y)
{
    if (x < 0 || y < 0 || x >= terrain.width || y >= terrain.height) {
        return nullptr;
    }
    const std::size_t index = static_cast<std::size_t>(y * terrain.width + x);
    if (index >= terrain.cells.size()) {
        return nullptr;
    }
    return &terrain.cells[index];
}

MiningDrillStats miningDrillStats(const GameState& state, const ContentCatalog& catalog)
{
    MiningDrillStats stats;
    stats.power = tuning::mining::baseDrillPower + static_cast<double>(activeTraining(state)) * tuning::mining::trainingDrillPowerScale;
    stats.speed = tuning::mining::droneSpeedCellsPerSecond;
    stats.scannerRadius = tuning::mining::scannerRevealRadius;
    stats.oxygenSeconds = tuning::mining::oxygenSeconds;
    stats.heatCoolingPerSecond = tuning::mining::heatCoolingPerSecond;
    stats.terrainWidth = tuning::mining::terrainWidth;
    stats.terrainHeight = tuning::mining::terrainHeight;

    const ModuleStats shipStats = aggregateShipStats(state, catalog);
    const double miningPower = std::max(0.0, shipStats.miningPower);
    const double miningYield = std::max(0.0, shipStats.miningYield);
    const double miningCooling = std::max(0.0, shipStats.miningCooling);
    const double miningDurability = std::max(0.0, shipStats.miningDurability);
    const double miningWidth = std::max(0.0, shipStats.miningWidth);
    const double miningDepth = std::max(0.0, shipStats.miningDepth);
    const double miningStorage = std::max(0.0, shipStats.miningStorage);
    const double miningEngineEfficiency = std::max(0.0, shipStats.miningEngineEfficiency);

    stats.power += miningPower * 0.75;
    stats.oreYieldChance += miningYield * 0.09;
    stats.heatRiseScale = std::clamp(1.0 - miningCooling * 0.075, 0.62, 1.0);
    stats.heatCoolingPerSecond += miningCooling * 0.030;
    stats.integrityRelief += miningDurability * 0.075;
    stats.terrainWidth = std::clamp(tuning::mining::terrainWidth + static_cast<int>(std::round(miningWidth * 4.0)), 48, 84);
    stats.terrainHeight = std::clamp(tuning::mining::terrainHeight + static_cast<int>(std::round(miningDepth * 5.0)), 32, 58);
    stats.storage += miningStorage;
    stats.engineEfficiency += miningEngineEfficiency;

    if (hasUnlockKey(state.meta, content::unlock::surfaceDrills)) {
        stats.power += tuning::mining::surfaceDrillPowerBonus;
    }
    if (hasUnlockKey(state.meta, content::unlock::surfaceProbes)) {
        stats.scannerRadius += tuning::mining::scannerProbeBonus;
    }
    if (traitIs(state, tuning::traits::beastMode)) {
        stats.oxygenSeconds += tuning::mining::capybaraOxygenBonusSeconds;
    }
    if (traitIs(state, tuning::traits::hardReboot)) {
        stats.integrityRelief += tuning::mining::beaverIntegrityRelief;
    }
    if (traitIs(state, tuning::traits::outtaHere)) {
        stats.extractionRiskRelief += tuning::mining::foxExtractionRiskRelief;
    }
    if (traitIs(state, tuning::traits::deepFocus)) {
        stats.power += tuning::mining::prairieDogDrillBonus;
    }
    if (traitIs(state, tuning::traits::rummageSale)) {
        stats.rareYieldChance += tuning::mining::squirrelRareYieldChance;
    }
    if (traitIs(state, tuning::traits::phaseShift)) {
        stats.speed += tuning::mining::chipmunkSpeedBonus;
    }
    const SurfaceUpgradeEffects surfaceUpgrades = surfaceUpgradeEffects(state, catalog);
    stats.power += surfaceUpgrades.drillPower * 0.75;
    stats.oreYieldChance += surfaceUpgrades.oreYieldChance;
    stats.scannerRadius += surfaceUpgrades.scannerRadius;
    stats.speed += surfaceUpgrades.droneSpeed;
    stats.oxygenSeconds += surfaceUpgrades.oxygenSeconds;
    stats.heatRiseScale = std::clamp(stats.heatRiseScale - surfaceUpgrades.drillCooling * 0.060, 0.50, 1.0);
    stats.heatCoolingPerSecond += surfaceUpgrades.drillCooling * 0.025;
    stats.integrityRelief += surfaceUpgrades.drillDurability * 0.070;
    stats.hardRockBounceRelief += surfaceUpgrades.hardRockBounceRelief;
    stats.extractionRiskRelief += surfaceUpgrades.extractionRiskRelief;
    stats.storage += surfaceUpgrades.droneStorage;
    stats.engineEfficiency += surfaceUpgrades.droneEngineEfficiency;
    stats.artifactTowEfficiency += surfaceUpgrades.artifactTowEfficiency;

    const MiniDroneLoadoutEffects drones = miniDroneLoadoutEffects(state, catalog);
    stats.passiveDroneMiningRate += drones.passiveMiningRate;
    stats.oxygenSeconds += drones.oxygenSeconds;
    stats.scannerRadius += drones.scannerRadius;
    stats.integrityRelief += drones.drillIntegrityRelief;
    stats.hardRockBounceRelief += drones.hardRockBounceRelief;
    stats.extractionRiskRelief += drones.extractionRiskRelief;
    stats.hardRockBounceRelief += miningDurability * 0.035;

    stats.oreYieldChance = std::clamp(stats.oreYieldChance, 0.0, 0.36);
    stats.rareYieldChance = std::clamp(stats.rareYieldChance, 0.0, 0.48);
    stats.integrityRelief = std::clamp(stats.integrityRelief, 0.0, 0.70);
    stats.passiveDroneMiningRate = std::clamp(stats.passiveDroneMiningRate, 0.0, 0.40);
    stats.hardRockBounceRelief = std::clamp(stats.hardRockBounceRelief, 0.0, 0.55);
    stats.storage = std::max(0.0, stats.storage);
    stats.engineEfficiency = std::clamp(stats.engineEfficiency, 0.0, 0.75);
    stats.artifactTowEfficiency = std::clamp(stats.artifactTowEfficiency, 0.0, 0.80);
    stats.oxygenSeconds = std::clamp(stats.oxygenSeconds, 0.0, tuning::mining::maximumOxygenSeconds);
    stats.heatCoolingPerSecond *= tuning::mining::heatCoolingMultiplier;
    return stats;
}

MiningDrillStats miningOperatorDrillStats()
{
    MiningDrillStats stats;
    stats.power =
        tuning::mining::baseDrillPower *
        tuning::mining::operatorDrillPowerScale;
    stats.speed = tuning::mining::operatorSpeedCellsPerSecond;
    stats.scannerRadius = tuning::mining::scannerRevealRadius;
    stats.oxygenSeconds = tuning::mining::oxygenSeconds;
    stats.heatCoolingPerSecond =
        tuning::mining::heatCoolingPerSecond *
        tuning::mining::heatCoolingMultiplier;
    stats.terrainWidth = tuning::mining::terrainWidth;
    stats.terrainHeight = tuning::mining::terrainHeight;
    return stats;
}

MiningCapabilityProfile miningCapabilityProfile(const GameState& state, const ContentCatalog& catalog)
{
    MiningCapabilityProfile profile;
    const MiningDrillStats drill = miningDrillStats(state, catalog);
    const MiniDroneLoadoutEffects loadout = miniDroneLoadoutEffects(state, catalog);
    profile.oxygenSeconds = drill.oxygenSeconds;
    profile.scannerRadius = drill.scannerRadius;
    profile.artifactTowEfficiency = drill.artifactTowEfficiency;
    profile.drillControl = std::clamp(
        drill.hardRockBounceRelief + drill.integrityRelief + (1.0 - drill.heatRiseScale),
        0.0,
        2.0);
    profile.combatDamagePerSecond = loadout.sentryDamagePerSecond + loadout.areaControlDamagePerSecond + loadout.reactiveArmorDamagePerSecond;
    profile.damageRelief = std::clamp(loadout.enemyDamageRelief + loadout.environmentalShieldRelief, 0.0, 0.95);
    for (const std::string& droneId : state.meta.equippedDroneIds) {
        const MiniDrone* drone = catalog.findMiniDrone(droneId);
        if (drone == nullptr || !isMiniDroneUnlocked(state.meta, *drone)) {
            continue;
        }
        const std::size_t role = static_cast<std::size_t>(drone->role);
        if (role < profile.roleMarks.size()) {
            profile.roleMarks[role] = std::max(profile.roleMarks[role], miniDroneUpgradeLevel(state, droneId));
        }
    }
    return profile;
}

bool miningCapabilityReadyForGate(const MiningCapabilityProfile& profile, const MiningGateDefinition& gate)
{
    const auto mark = [&](MiniDroneRole role) {
        return profile.roleMarks[static_cast<std::size_t>(role)];
    };
    if (gate.requiresHazardTreatment && mark(MiniDroneRole::Hazard) < gate.requiredHazardMark) {
        return false;
    }
    if (gate.requiresEnemyClearance && profile.combatDamagePerSecond <= 0.0 && profile.damageRelief <= 0.0) {
        return false;
    }
    if (gate.requiresSurveyTriangulation && mark(MiniDroneRole::Survey) <= 0 && profile.scannerRadius < 3.0) {
        return false;
    }
    if (gate.heavyTow && mark(MiniDroneRole::Resource) <= 0 && profile.artifactTowEfficiency <= 0.0) {
        return false;
    }
    if (gate.endurancePlacement && mark(MiniDroneRole::Resource) <= 0 && profile.oxygenSeconds <= tuning::mining::oxygenSeconds) {
        return false;
    }
    if (gate.shieldCorridor && mark(MiniDroneRole::Defense) <= 0 && mark(MiniDroneRole::Attack) <= 0) {
        return false;
    }
    return true;
}

std::string miningGateCapabilityStatus(const MiningCapabilityProfile& profile, const MiningGateDefinition& gate)
{
    if (gate.type == MiningGateType::None) {
        return "No artifact-site lock forecast.";
    }
    return std::string(miningCapabilityReadyForGate(profile, gate) ? "READY - " : "MISSING DIRECT KEY - ")
        + std::string(gate.requiredCapability)
        + ". Alternatives: " + std::string(gate.alternatives);
}

int miningCarriedCargo(const MiningRunState& mining)
{
    return std::max(0, mining.cargo);
}

int miningBankedCargo(const MiningRunState& mining)
{
    return std::max(0, mining.stowedCargo);
}

namespace {

int proportionalMiningRepairCost(double integrity, int fullDamageCost)
{
    const double missing = std::clamp(1.0 - integrity, 0.0, 1.0);
    if (missing <= tuning::mining::repairDamageEpsilon) {
        return 0;
    }
    return std::max(1, static_cast<int>(std::ceil(missing * static_cast<double>(fullDamageCost) - 0.000001)));
}

bool spendMiningRepairMaterials(MiningRunState& mining, int commonCost)
{
    if (commonCost <= 0 || mining.stowedMaterials.common < commonCost) {
        return false;
    }
    mining.stowedMaterials.common -= commonCost;
    mining.stowedCargo = std::max(0, mining.stowedCargo - commonCost * tuning::mining::commonCargo);
    return true;
}

} // namespace

int miningDrillRepairCost(const MiningRunState& mining)
{
    return proportionalMiningRepairCost(mining.drillIntegrity, tuning::mining::drillRepairCommonAtFullDamage);
}

int miningDroneRepairCost(const MiningRunState& mining)
{
    return proportionalMiningRepairCost(mining.droneHealth, tuning::mining::droneRepairCommonAtFullDamage);
}

bool repairMiningDrill(GameState& state)
{
    MiningRunState& mining = state.run.mining;
    const MiningArenaRules arenaRules = activeMiningArenaRules(mining);
    const int cost = miningDrillRepairCost(mining);
    if (!mining.active || !arenaRules.mechanics.fieldRepairs || !miningAtReturnZone(mining) || !spendMiningRepairMaterials(mining, cost)) {
        return false;
    }
    mining.drillIntegrity = 1.0;
    mining.drillBreakNotified = false;
    return true;
}

bool repairMiningDrone(GameState& state)
{
    MiningRunState& mining = state.run.mining;
    const MiningArenaRules arenaRules = activeMiningArenaRules(mining);
    const int cost = miningDroneRepairCost(mining);
    if (!mining.active ||
        mining.rigDisabled ||
        !arenaRules.mechanics.fieldRepairs ||
        !rigAtReturnZone(mining) ||
        !spendMiningRepairMaterials(mining, cost)) {
        return false;
    }
    mining.droneHealth = 1.0;
    return true;
}

bool repairMiningOperator(GameState& state)
{
    MiningRunState& mining = state.run.mining;
    if (!mining.active || !controlledActorAtReturnZone(mining) ||
        mining.operatorIntegrity >= 1.0 ||
        !spendMiningRepairMaterials(
            mining,
            static_cast<int>(tuning::mining::operatorIntegrityRepairCommonCost))) {
        return false;
    }
    mining.operatorIntegrity = 1.0;
    return true;
}

bool miningAtReturnZone(const MiningRunState& mining)
{
    return controlledActorAtReturnZone(mining);
}

MiningLoadStats miningLoadStats(const GameState& state, const ContentCatalog& catalog)
{
    MiningLoadStats load;
    const MiningRunState& mining = state.run.mining;
    const MiningDrillStats stats = miningDrillStats(state, catalog);
    const bool suit = operatorControlled(mining);
    const double heavyTowScale = mining.gate.active && mining.gate.heavyTow ? 1.80 : 1.0;
    const double towWeight = (mining.artifact.present &&
                                 mining.artifact.tethered &&
                                 mining.artifact.state != MiningArtifactState::Delivered &&
                                 mining.artifact.state != MiningArtifactState::Destroyed)
        ? tuning::mining::tetheredArtifactCargoWeight * heavyTowScale *
            (suit ? 1.0 : std::clamp(1.0 - stats.artifactTowEfficiency, 0.20, 1.0))
        : 0.0;
    load.currentLoad = (suit ? 0.0 : static_cast<double>(miningCarriedCargo(mining))) + towWeight;
    load.freeBuffer = suit
        ? tuning::mining::operatorArtifactFreeBuffer
        : tuning::mining::baseCarryBufferCargo + stats.storage;
    const MiningArenaRules arenaRules = activeMiningArenaRules(mining);
    if (!arenaRules.mechanics.cargoDrag) {
        return load;
    }
    load.burden = std::max(0.0, load.currentLoad - load.freeBuffer);
    const double penaltyScale = suit
        ? 1.0
        : std::clamp(1.0 - stats.engineEfficiency, 0.20, 1.0);
    load.speedMultiplier = std::clamp(
        1.0 - load.burden * tuning::mining::loadSpeedPenaltyPerCargo * penaltyScale,
        suit
            ? tuning::mining::operatorArtifactMinimumSpeedMultiplier
            : tuning::mining::minLoadedSpeedMultiplier,
        1.0);
    load.fuelConsumptionMultiplier = std::clamp(
        1.0 + load.burden * tuning::mining::loadFuelPenaltyPerCargo * penaltyScale,
        1.0,
        tuning::mining::maxLoadedFuelMultiplier);
    return load;
}

bool bankMiningPayloadAtShip(GameState& state, const ContentCatalog& catalog)
{
    MiningRunState& mining = state.run.mining;
    if (mining.rigDisabled || !rigAtReturnZone(mining)) {
        return false;
    }
    const bool hasPayload = mining.cargo > 0 ||
        mining.temporaryMaterials.common > 0 ||
        mining.temporaryMaterials.rare > 0 ||
        mining.temporaryMaterials.exotic > 0 ||
        !mining.temporaryArtifacts.empty();
    if (!hasPayload) {
        return false;
    }

    addMiningMaterials(mining.stowedMaterials, mining.temporaryMaterials);
    mining.temporaryMaterials = {};
    mining.stowedArtifacts.insert(mining.stowedArtifacts.end(), mining.temporaryArtifacts.begin(), mining.temporaryArtifacts.end());
    mining.temporaryArtifacts.clear();
    mining.stowedCargo += std::max(0, mining.cargo);
    mining.cargo = 0;
    mining.oxygenSeconds = miningDrillStats(state, catalog).oxygenSeconds;
    mining.oxygenSeconds = std::max(
        mining.oxygenSeconds,
        std::max(0.0, mining.siteBaselineOxygenSeconds));
    mining.oxygenDepletedNotified = false;
    return true;
}

bool bankPhysicallyDeliveredArtifactsAtShip(MiningRunState& mining)
{
    if (mining.temporaryArtifacts.empty()) {
        return false;
    }
    const int artifactCount =
        static_cast<int>(mining.temporaryArtifacts.size());
    mining.stowedArtifacts.insert(
        mining.stowedArtifacts.end(),
        mining.temporaryArtifacts.begin(),
        mining.temporaryArtifacts.end());
    mining.temporaryArtifacts.clear();
    const int deliveredCargo = std::min(
        std::max(0, mining.cargo),
        artifactCount * tuning::mining::artifactCargo);
    mining.stowedCargo += deliveredCargo;
    mining.cargo = std::max(0, mining.cargo - deliveredCargo);
    return true;
}

void applyMiningTerrainToughnessScale(MiningTerrain& terrain, double scale)
{
    const double clampedScale = std::max(0.01, scale);
    for (MiningCell& cell : terrain.cells) {
        if (cell.material == MiningCellMaterial::Empty || cell.material == MiningCellMaterial::Bedrock) {
            continue;
        }
        const double toughness = miningMaterialToughness(cell.material, 0) * clampedScale;
        cell.maxToughness = toughness;
        cell.remainingToughness = toughness;
    }
}

MiningTerrain generateMiningTerrainForRules(
    const GameState& state,
    const Destination& destination,
    SurfaceSiteProfile profile,
    int depthZone,
    int width,
    int height,
    const MiningArenaRules& rules,
    MiningSiteBiome biome)
{
    MiningTerrain terrain;
    terrain.width = std::max(8, width);
    terrain.height = std::max(8, height);
    terrain.depthZone = std::max(0, depthZone);
    terrain.cells.reserve(static_cast<std::size_t>(terrain.width * terrain.height));
    for (int y = 0; y < terrain.height; ++y) {
        for (int x = 0; x < terrain.width; ++x) {
            const MiningCellMaterial material = generatedMaterial(
                rules.request.seed,
                destination,
                rules,
                biome,
                profile,
                x,
                y,
                terrain.depthZone,
                terrain.width,
                terrain.height);
            const MiningElementalAffinity affinity = material == MiningCellMaterial::HazardPocket
                ? elementalAffinityForLane(rules, profile, x / 4)
                : MiningElementalAffinity::None;
            terrain.cells.push_back(makeCell(material, terrain.depthZone, affinity));
        }
    }
    applyHostileTunnelNetwork(terrain, state, destination, profile, rules);
    applySuitOnlyPassageNetwork(
        terrain,
        rules.request.seed,
        terrain.depthZone);
    const int chunksX = (terrain.width + tuning::mining::chunkSize - 1) / tuning::mining::chunkSize;
    const int chunksY = (terrain.height + tuning::mining::chunkSize - 1) / tuning::mining::chunkSize;
    terrain.dirtyChunks.assign(static_cast<std::size_t>(chunksX * chunksY), 1);
    return terrain;
}

MiningTerrain generateMiningTerrain(const GameState& state, const Destination& destination, SurfaceSiteProfile profile, int depthZone, int width, int height)
{
    const MiningCampaignProgression progression = resolveCampaignMiningProgression(
        state.meta.chapter,
        destination.id,
        depthZone,
        0);
    const MiningArenaRequest request {
        progression.act,
        progression.difficulty,
        deriveMiningArenaSeed(state.seed, destination.id, 0, depthZone)
    };
    const MiningArenaRules rules = resolveMiningArenaRules(request);
    MiningTerrain terrain = generateMiningTerrainForRules(
        state,
        destination,
        profile,
        depthZone,
        width,
        height,
        rules);
    normalizeRichTerrainDeposits(terrain, rules, rules.rewardBudget, 0, 0);
    applyMiningTerrainToughnessScale(terrain, rules.terrainToughnessScale);
    return terrain;
}

void applySurfaceProspects(
    MiningTerrain& terrain,
    const SurfaceExpeditionState& expedition,
    const MiningArenaRules& rules,
    const MiningRewardBudget& budget,
    MiningSiteBiome biome = MiningSiteBiome::Default)
{
    if (biome == MiningSiteBiome::ThermalLava) {
        return;
    }
    std::vector<std::pair<int, int>> occupied;
    auto alreadyOccupied = [&](int x, int y) {
        return std::find(occupied.begin(), occupied.end(), std::pair<int, int> {x, y}) != occupied.end();
    };
    auto stampProspect = [&](MiningCellMaterial material, int count, int yBase, int yStride, MiningCellFeature feature) {
        const int clampedCount = std::clamp(count, 0, 10);
        int placed = 0;
        for (int i = 0; i < clampedCount; ++i) {
            for (int attempt = 0; attempt < 14; ++attempt) {
                const int lane = i + attempt;
                const int side = lane % 2 == 0 ? -1 : 1;
                const int offset = side * (2 + (lane / 2) * 3);
                const int x = std::clamp(terrain.width / 2 + offset, 2, terrain.width - 3);
                const int y = std::clamp(yBase + (lane % 4) * yStride + (lane / 5), 5, terrain.height - 3);
                if (alreadyOccupied(x, y)) {
                    continue;
                }
                stampMiningCell(terrain, x, y, terrain.depthZone, feature, MiningEnemyType::None, material);
                if (MiningCell* cell = miningCellAt(terrain, x, y)) {
                    cell->revealed = true;
                }
                markDirty(terrain, x, y);
                occupied.push_back({x, y});
                ++placed;
                break;
            }
        }
        (void)placed;
    };

    const int shallowBand = std::max(7, terrain.height / 5);
    const int midBand = std::max(10, terrain.height / 2);
    const int deepBand = std::max(12, (terrain.height * 2) / 3);
    const int deniedRare = std::max(0, expedition.prospectMaterials.rare - budget.rareCap);
    const int deniedExotic = std::max(0, expedition.prospectMaterials.exotic - budget.exoticCap);
    const MiningCellFeature branchFeature = miningRoomFeatureAllowed(rules, MiningCellFeature::BranchTunnel)
        ? MiningCellFeature::BranchTunnel
        : MiningCellFeature::MainTunnel;
    stampProspect(
        MiningCellMaterial::CommonOre,
        expedition.prospectMaterials.common + deniedRare + deniedExotic,
        shallowBand,
        2,
        branchFeature);
    if (miningMaterialAllowed(rules, MiningCellMaterial::RareOre)) {
        stampProspect(MiningCellMaterial::RareOre, std::min(expedition.prospectMaterials.rare, budget.rareCap), midBand, 2, branchFeature);
    }
    if (miningMaterialAllowed(rules, MiningCellMaterial::ExoticVein)) {
        stampProspect(MiningCellMaterial::ExoticVein, std::min(expedition.prospectMaterials.exotic, budget.exoticCap), deepBand, 2, branchFeature);
    }
}

void normalizeRichTerrainDeposits(
    MiningTerrain& terrain,
    const MiningArenaRules& rules,
    const MiningRewardBudget& budget,
    int reservedRareGuarantees,
    int reservedExoticGuarantees)
{
    int rareKept = 0;
    int exoticKept = 0;
    const int rareLimit = std::max(0, budget.rareCap - reservedRareGuarantees);
    const int exoticLimit = std::max(0, budget.exoticCap - reservedExoticGuarantees);
    for (MiningCell& cell : terrain.cells) {
        if (cell.material == MiningCellMaterial::RareOre) {
            const bool keep = miningMaterialAllowed(rules, cell.material) && rareKept < rareLimit;
            if (keep) {
                ++rareKept;
            } else {
                const MiningCellFeature feature = cell.feature;
                const MiningEnemyType enemy = cell.enemy;
                cell = makeCell(MiningCellMaterial::CommonOre, terrain.depthZone);
                cell.feature = feature;
                cell.enemy = enemy;
            }
        } else if (cell.material == MiningCellMaterial::ExoticVein) {
            const bool keep = miningMaterialAllowed(rules, cell.material) && exoticKept < exoticLimit;
            if (keep) {
                ++exoticKept;
            } else {
                const MiningCellFeature feature = cell.feature;
                const MiningEnemyType enemy = cell.enemy;
                cell = makeCell(MiningCellMaterial::CommonOre, terrain.depthZone);
                cell.feature = feature;
                cell.enemy = enemy;
            }
        }
    }
}

void stampGuaranteedRichDeposits(
    MiningTerrain& terrain,
    const MiningArenaRules& rules,
    int rareGuarantees,
    int exoticGuarantees)
{
    auto stampGuarantees = [&](MiningCellMaterial material, int count, int laneOffset) {
        if (!miningMaterialAllowed(rules, material)) {
            return;
        }
        for (int index = 0; index < count; ++index) {
            const int lane = laneOffset + index;
            const int side = unitHash(rules.request.seed, lane, rules.request.difficulty, 0, 0x47554152414E5445ULL) < 0.5 ? -1 : 1;
            const int x = std::clamp(terrain.width / 2 + side * (2 + lane % 3), 2, terrain.width - 3);
            const int y = std::clamp(7 + lane * 2, 5, terrain.height - 3);
            const MiningCellFeature feature = miningRoomFeatureAllowed(rules, MiningCellFeature::BranchTunnel)
                ? MiningCellFeature::BranchTunnel
                : MiningCellFeature::MainTunnel;
            stampMiningCell(terrain, x, y, terrain.depthZone, feature, MiningEnemyType::None, material);
            if (MiningCell* cell = miningCellAt(terrain, x, y)) {
                cell->revealed = true;
            }
            markDirty(terrain, x, y);
        }
    };

    stampGuarantees(MiningCellMaterial::RareOre, rareGuarantees, 0);
    stampGuarantees(MiningCellMaterial::ExoticVein, exoticGuarantees, rareGuarantees + 2);
}

void stampMiningSupplyPockets(
    MiningRunState& mining,
    const MiningArenaRules& rules,
    MiningSiteBiome biome = MiningSiteBiome::Default)
{
    if (biome == MiningSiteBiome::ThermalLava ||
        !rules.mechanics.oxygenAndFuel ||
        !miningMaterialAllowed(rules, MiningCellMaterial::FuelPocket) ||
        !miningMaterialAllowed(rules, MiningCellMaterial::OxygenPocket)) {
        return;
    }

    const auto stamp = [&](MiningCellMaterial material, std::uint64_t salt) {
        for (int attempt = 0; attempt < 96; ++attempt) {
            const int x = 2 + static_cast<int>(unitHash(
                rules.request.seed,
                attempt,
                mining.depthZone,
                0,
                salt) * static_cast<double>(std::max(1, mining.terrain.width - 4)));
            const int y = std::clamp(
                7 + static_cast<int>(unitHash(
                    rules.request.seed,
                    attempt,
                    mining.depthZone,
                    1,
                    salt) * static_cast<double>(std::max(1, mining.terrain.height - 10))),
                5,
                std::max(5, mining.terrain.height - 3));
            MiningCell* cell = miningCellAt(mining.terrain, x, y);
            if (cell == nullptr || cell->material != MiningCellMaterial::Regolith || cell->gateAssociated ||
                miningReturnShaftContains(mining.terrain, x, y)) {
                continue;
            }
            const MiningCellFeature feature = cell->feature;
            *cell = makeCell(material, mining.depthZone);
            cell->feature = feature;
            const double toughness = miningMaterialToughness(material, mining.depthZone) * rules.terrainToughnessScale;
            cell->maxToughness = toughness;
            cell->remainingToughness = toughness;
            markDirty(mining.terrain, x, y);
            return;
        }
    };

    stamp(MiningCellMaterial::FuelPocket, 0x4655454C504F434BULL);
    stamp(MiningCellMaterial::OxygenPocket, 0x4F585947454E504FULL);
}

int miningDestinationHistoryValue(
    const std::vector<int>& history,
    const ContentCatalog& catalog,
    std::string_view destinationId)
{
    for (std::size_t index = 0; index < catalog.destinations.size(); ++index) {
        if (catalog.destinations[index].id == destinationId) {
            return index < history.size() ? std::max(0, history[index]) : 0;
        }
    }
    return 0;
}

SurfaceActionOutcome startMiningRun(GameState& state, const ContentCatalog& catalog)
{
    const SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    if (!expedition.active) {
        return {};
    }
    const int completedHostileSorties = miningDestinationHistoryValue(
        state.meta.destinationSuccesses,
        catalog,
        expedition.destinationId);
    const int landingOrdinal = miningDestinationHistoryValue(
        state.meta.destinationLandings,
        catalog,
        expedition.destinationId);
    const MiningArenaRequest request = campaignMiningArenaRequest(
        state.meta.chapter,
        expedition.destinationId,
        expedition.depth,
        completedHostileSorties,
        state.seed,
        landingOrdinal);
    return startMiningRun(state, catalog, request, true);
}

SurfaceActionOutcome startMiningRun(
    GameState& state,
    const ContentCatalog& catalog,
    const MiningArenaRequest& requestedArena,
    bool progressionCreditEligible)
{
    SurfaceActionOutcome outcome;
    SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    if (!expedition.active) {
        return outcome;
    }
    if (expedition.miningRunUsed) {
        outcome.message = "Mining run already used for this surface loop.";
        return outcome;
    }
    const bool arkKnown = arkDiscovered(state);
    if (expedition.sharedFuel <= 0) {
        outcome.message = text::fuel::miningBlockedStatus(arkKnown);
        return outcome;
    }
    const Destination* destination = catalog.findDestination(expedition.destinationId);
    if (destination == nullptr) {
        return outcome;
    }
    const MiningSiteDefinition* siteDefinition = nullptr;
    if (!expedition.pendingMiningSiteDefinitionId.empty()) {
        siteDefinition = catalog.findMiningSite(expedition.pendingMiningSiteDefinitionId);
        if (siteDefinition == nullptr) {
            outcome.message = "The requested mining site definition is unavailable.";
            return outcome;
        }
    }

    expedition.sharedFuel = std::max(0, expedition.sharedFuel - 1);
    expedition.miningRunUsed = true;
    outcome.applied = true;
    outcome.fuelDelta = -1;
    outcome.message = text::fuel::miningStartedStatus(arkKnown);

    MiningArenaRequest request = requestedArena;
    if (siteDefinition != nullptr) {
        request = siteDefinition->arena;
        if (request.seed == 0) {
            request.seed = requestedArena.seed;
        }
        if (siteDefinition->gateType != MiningGateType::None) {
            request.gateOverrideEnabled = true;
            request.gateOverride = siteDefinition->gateType;
        }
    }
    MiningSiteProgress* compatibilitySite = nullptr;
    if (siteDefinition == nullptr && progressionCreditEligible && !request.gateOverrideEnabled) {
        // Imported saves can resume their old fixed gate. New runs never
        // synthesize a compatibility record from campaign or destination data.
        compatibilitySite = pendingCompatibilityMiningSite(
            state.meta,
            expedition.destinationId);
        if (compatibilitySite != nullptr) {
            request.act = compatibilitySite->act;
            request.difficulty = compatibilitySite->difficulty;
            request.seed = compatibilitySite->seed;
            request.gateOverrideEnabled = true;
            request.gateOverride = compatibilitySite->gateType;
        }
    }
    const MiningArenaRules arenaRules = siteDefinition != nullptr
        ? resolveMiningSiteArenaRules(request, *siteDefinition)
        : resolveMiningArenaRules(request);
    const bool firstClearFulfilled = progressionCreditEligible && miningFirstClearFulfilled(state.meta, arenaRules);
    const MiningRewardBudget rewardBudget = effectiveMiningRewardBudget(arenaRules, firstClearFulfilled);
    const MiningFirstClearProgress& firstClear = miningFirstClearProgress(state.meta, arenaRules.request.act, arenaRules.band);
    const int rareGuarantees = progressionCreditEligible
        ? std::max(0, arenaRules.rewardBudget.rareGuarantee - firstClear.rareBanked)
        : arenaRules.rewardBudget.rareGuarantee;
    const int exoticGuarantees = progressionCreditEligible
        ? std::max(0, arenaRules.rewardBudget.exoticGuarantee - firstClear.exoticBanked)
        : arenaRules.rewardBudget.exoticGuarantee;

    MiningRunState mining;
    mining.active = true;
    mining.arenaMetadata = {
        arenaRules.request.act,
        arenaRules.request.difficulty,
        arenaRules.request.seed,
        miningArenaRulesVersion,
        siteDefinition != nullptr
            ? siteDefinition->gateType
            : (compatibilitySite != nullptr
                ? compatibilitySite->gateType
                : selectMiningGateType(arenaRules)),
        true
    };
    mining.rewardBudget = rewardBudget;
    mining.progressionCreditEligible = progressionCreditEligible;
    mining.destinationId = expedition.destinationId;
    mining.scenarioId = expedition.pendingScenarioId;
    mining.scenarioStepId = expedition.pendingScenarioStepId;
    mining.miningSiteDefinitionId = expedition.pendingMiningSiteDefinitionId;
    mining.miningSiteBiome = siteDefinition != nullptr
        ? siteDefinition->biome
        : MiningSiteBiome::Default;
    mining.siteBaselineOxygenSeconds = siteDefinition != nullptr
        ? std::max(0.0, siteDefinition->baselineOxygenSeconds)
        : 0.0;
    mining.siteProfile = expedition.siteProfile;
    mining.depthZone = expedition.depth;
    mining.entryDepthZone = 0;
    mining.deepestDepthZone = mining.depthZone;
    const MiningDrillStats stats = miningDrillStats(state, catalog);
    mining.oxygenSeconds = stats.oxygenSeconds;
    mining.fuelCycleProgress = 0.0;
    mining.fuelSpent = 1;
    mining.terrain = generateMiningTerrainForRules(
        state,
        *destination,
        expedition.siteProfile,
        mining.depthZone,
        stats.terrainWidth,
        stats.terrainHeight,
        arenaRules,
        siteDefinition != nullptr ? siteDefinition->biome : MiningSiteBiome::Default);
    applySurfaceProspects(
        mining.terrain,
        expedition,
        arenaRules,
        rewardBudget,
        siteDefinition != nullptr ? siteDefinition->biome : MiningSiteBiome::Default);
    normalizeRichTerrainDeposits(
        mining.terrain,
        arenaRules,
        rewardBudget,
        rareGuarantees,
        exoticGuarantees);
    stampGuaranteedRichDeposits(mining.terrain, arenaRules, rareGuarantees, exoticGuarantees);
    applyMiningTerrainToughnessScale(mining.terrain, arenaRules.terrainToughnessScale);
    const bool forcedArtifact = expedition.prospectArtifacts > 0;
    if ((arenaRules.mechanics.artifactRecovery || forcedArtifact) &&
        mining.arenaMetadata.gateType == MiningGateType::None) {
        placeMiningArtifact(state, mining, *destination, forcedArtifact, forcedArtifact);
    }
    expedition.prospectMaterials = {};
    expedition.prospectArtifacts = 0;
    expedition.depthProspects.clear();
    spawnMiningEnemies(mining, *destination, arenaRules);
    setupMiningGate(mining, arenaRules, compatibilitySite, siteDefinition);
    if (forcedArtifact && !mining.artifact.present) {
        placeMiningArtifact(state, mining, *destination, true, true);
    }
    if (forcedArtifact && mining.artifact.present) {
        mining.artifact.revealed = true;
        const int artifactX = static_cast<int>(std::floor(mining.artifact.x));
        const int artifactY = static_cast<int>(std::floor(mining.artifact.y));
        if (MiningCell* artifactCell = miningCellAt(mining.terrain, artifactX, artifactY)) {
            artifactCell->revealed = true;
            markDirty(mining.terrain, artifactX, artifactY);
        }
    }
    stampMiningSupplyPockets(
        mining,
        arenaRules,
        siteDefinition != nullptr ? siteDefinition->biome : MiningSiteBiome::Default);
    mining.oxygenSeconds = std::max(
        mining.oxygenSeconds,
        mining.siteBaselineOxygenSeconds);
    mining.droneX = static_cast<double>(mining.terrain.width) * 0.5;
    mining.droneY = 4.0;
    mining.rigDepthZone = mining.depthZone;
    mining.operatorMode = MiningOperatorMode::Rig;
    mining.operatorPresent = false;
    mining.operatorX = mining.droneX;
    mining.operatorY = mining.droneY;
    mining.operatorAimDirX = 0.0;
    mining.operatorAimDirY = 1.0;
    const double gravityLength = std::max(
        0.0001,
        std::hypot(destination->gravityDirectionX, destination->gravityDirectionY));
    mining.gravityDirectionX = destination->gravityDirectionX / gravityLength;
    mining.gravityDirectionY = destination->gravityDirectionY / gravityLength;
    mining.gravityStrength =
        tuning::mining::baseGravityCellsPerSecondSquared *
        std::max(0.0, destination->gravityScale);
    mining.returnZoneX = miningShipStartX(mining);
    mining.returnZoneY = mining.droneY;
    mining.aimX = mining.droneX;
    mining.aimY = mining.droneY + 1.0;
    mining.aimDirX = 0.0;
    mining.aimDirY = 1.0;
    mining.hullDirX = 0.0;
    mining.hullDirY = 1.0;
    revealAround(mining, mining.returnZoneX, mining.returnZoneY, tuning::mining::passiveLightRadius);
    revealAround(mining, mining.droneX, mining.droneY, tuning::mining::passiveLightRadius);
    if (!arenaRules.mechanics.fogAndScanner) {
        for (MiningCell& cell : mining.terrain.cells) {
            cell.revealed = true;
        }
    }
    refreshTargetCell(mining);
    state.run.mining = std::move(mining);
    ensureMiningMiniDroneAgents(state, catalog);
    state.screen = Screen::Mining;
    appendSurfaceLog(expedition, text::fuel::miningLog(arkKnown));
    return outcome;
}

void setMiningMove(GameState& state, double xAxis, double yAxis)
{
    MiningRunState& mining = state.run.mining;
    if (!mining.active) {
        return;
    }
    const MiningArenaRules arenaRules = activeMiningArenaRules(mining);
    if (!arenaRules.mechanics.movement) {
        mining.moveX = 0.0;
        mining.moveY = 0.0;
        return;
    }
    mining.moveX = std::clamp(xAxis, -1.0, 1.0);
    mining.moveY = std::clamp(yAxis, -1.0, 1.0);
    const double moveLength = std::sqrt(mining.moveX * mining.moveX + mining.moveY * mining.moveY);
    if (moveLength > 0.01 && !operatorControlled(mining)) {
        setAimDirection(mining, mining.moveX, mining.moveY);
        mining.hullDirX = mining.aimDirX;
        mining.hullDirY = mining.aimDirY;
        refreshTargetCell(mining);
    }
}

void setMiningAim(GameState& state, double normalizedX, double normalizedY)
{
    MiningRunState& mining = state.run.mining;
    if (!mining.active) {
        return;
    }
    if (operatorControlled(mining)) {
        const double length = std::hypot(normalizedX, normalizedY);
        if (length > 0.12) {
            setAimDirection(mining, normalizedX / length, normalizedY / length);
        }
    }
    refreshTargetCell(mining);
}

void setMiningDrilling(GameState& state, bool drilling)
{
    if (state.run.mining.active) {
        const MiningArenaRules arenaRules =
            activeMiningArenaRules(state.run.mining);
        state.run.mining.drilling = arenaRules.mechanics.drilling && drilling
            && state.run.mining.drillIntegrity > 0.0
            && !state.run.mining.drillThermalLock
            && state.run.mining.drillHeat < 1.0
            && !state.run.mining.failurePending;
    }
}

void setMiningFire(GameState& state, bool firing)
{
    MiningRunState& mining = state.run.mining;
    mining.firing = mining.active && operatorControlled(mining) &&
        !mining.failurePending && mining.operatorIntegrity > 0.0 && firing;
}

void setMiningOperatorToggleProgress(GameState& state, double progress)
{
    MiningRunState& mining = state.run.mining;
    mining.operatorToggleProgress = mining.active && !mining.failurePending
        ? std::clamp(progress, 0.0, 1.0)
        : 0.0;
}

bool toggleMiningOperator(GameState& state)
{
    MiningRunState& mining = state.run.mining;
    if (!mining.active || mining.failurePending) {
        return false;
    }

    mining.operatorToggleProgress = 0.0;
    if (operatorControlled(mining)) {
        if (mining.rigDisabled || mining.depthZone != mining.rigDepthZone) {
            state.statusLine = mining.rigDisabled
                ? "The disabled rig cannot be re-entered."
                : "Return to the rig's depth before boarding.";
            return false;
        }
        const double distance = std::hypot(
            mining.operatorX - mining.droneX,
            mining.operatorY - mining.droneY);
        if (distance > tuning::mining::operatorEntryDistanceCells) {
            state.statusLine = "Move within 1.25 cells of the rig to enter.";
            return false;
        }
        const MiningOperatorMode previous = mining.operatorMode;
        mining.operatorMode = MiningOperatorMode::Rig;
        mining.operatorRigTethered = false;
        mining.operatorPresent = false;
        mining.operatorVelocityX = 0.0;
        mining.operatorVelocityY = 0.0;
        mining.firing = false;
        mining.drilling = false;
        transferMiniDroneSwarmAnchor(mining, previous, mining.operatorMode);
        state.statusLine = "Operator secured in the mining rig.";
        refreshTargetCell(mining);
        return true;
    }

    const double baseAngle = std::atan2(mining.hullDirY, mining.hullDirX);
    bool found = false;
    double exitX = mining.droneX;
    double exitY = mining.droneY;
    const int exitRings = std::max(
        1,
        static_cast<int>(std::ceil(
            (tuning::mining::operatorSafeExitSearchRadiusCells - 0.72) /
                0.48)) +
            1);
    for (int ring = 0; ring < exitRings && !found; ++ring) {
        const double radius = 0.72 + static_cast<double>(ring) * 0.48;
        if (radius >
            tuning::mining::operatorSafeExitSearchRadiusCells + 0.001) {
            break;
        }
        for (int lane = 0; lane < 16; ++lane) {
            const double angle = baseAngle + kPi +
                static_cast<double>((lane + 1) / 2) * (kPi / 8.0) *
                    (lane % 2 == 0 ? 1.0 : -1.0);
            const double candidateX = mining.droneX + std::cos(angle) * radius;
            const double candidateY = mining.droneY + std::sin(angle) * radius;
            if (canOccupyActor(
                    mining.terrain,
                    candidateX,
                    candidateY,
                    tuning::mining::operatorColliderRadiusCells,
                    true)) {
                exitX = candidateX;
                exitY = candidateY;
                found = true;
                break;
            }
        }
    }
    if (!found) {
        state.statusLine = "No safe adjacent position for EVA.";
        return false;
    }

    const MiningOperatorMode previous = mining.operatorMode;
    mining.rigTethered = false;
    mining.operatorRigTethered = false;
    mining.operatorMode = MiningOperatorMode::Jetpack;
    mining.operatorPresent = true;
    mining.operatorX = exitX;
    mining.operatorY = exitY;
    mining.operatorVelocityX = mining.rigVelocityX;
    mining.operatorVelocityY = mining.rigVelocityY;
    mining.operatorAimDirX = mining.hullDirX;
    mining.operatorAimDirY = mining.hullDirY;
    mining.drilling = false;
    mining.firing = false;
    transferMiniDroneSwarmAnchor(mining, previous, mining.operatorMode);
    state.statusLine = "EVA active. Support Drones are anchored to the operator.";
    revealAround(mining, mining.operatorX, mining.operatorY, tuning::mining::passiveLightRadius);
    refreshTargetCell(mining);
    return true;
}

void toggleMiningTether(GameState& state)
{
    MiningRunState& mining = state.run.mining;
    MiningArtifactObject& artifact = mining.artifact;
    const MiningArenaRules arenaRules = activeMiningArenaRules(mining);
    if (!mining.active) {
        return;
    }
    const bool artifactRecoverable = arenaRules.mechanics.artifactTethering && artifact.present &&
        artifact.state != MiningArtifactState::Delivered &&
        artifact.state != MiningArtifactState::Destroyed;
    const double artifactDistance = artifactRecoverable
        ? std::hypot(artifact.x - controlledActorX(mining), artifact.y - controlledActorY(mining))
        : 0.0;
    const bool rigAvailable = !mining.rigDisabled && mining.rigDepthZone == mining.depthZone;
    const bool operatorTowMode = operatorControlled(mining) && rigAvailable;
    const double rigDistance = rigAvailable
        ? std::hypot(mining.droneX - controlledActorX(mining), mining.droneY - controlledActorY(mining))
        : 0.0;
    if (artifact.tethered) {
        const double speed = std::hypot(artifact.velocityX, artifact.velocityY);
        if (speed > tuning::mining::artifactDropDamageThreshold) {
            damageMiningArtifact(
                mining,
                (speed - tuning::mining::artifactDropDamageThreshold) *
                    tuning::mining::artifactImpactDamageScale);
        }
        artifact.tethered = false;
        return;
    }
    if (mining.operatorRigTethered) {
        mining.operatorRigTethered = false;
        return;
    }
    if (mining.rigTethered) {
        mining.rigTethered = false;
        return;
    }

    const bool artifactInRange = artifactRecoverable &&
        (artifact.revealed || artifact.state == MiningArtifactState::Loose) &&
        artifactDistance <= tuning::mining::artifactTetherRangeCells;
    const bool operatorRigInRange = operatorTowMode &&
        rigDistance <= tuning::mining::operatorRigTetherRangeCells;
    // EVA has one physical tow line. Choose the nearer valid target so
    // stepping out beside the rig and pressing T reliably grabs the rig even
    // when an exposed artifact also sits inside the wider tether radius.
    if (operatorRigInRange && (!artifactInRange || rigDistance <= artifactDistance)) {
        mining.operatorRigTethered = true;
        return;
    }
    if (!artifactInRange) {
        if (rigAvailable && !operatorTowMode) {
            mining.rigTethered = true;
        }
        return;
    }

    const bool payloadAlreadyReleased =
        artifact.state == MiningArtifactState::Loose;
    if (gateHasHardLock(mining.gate) && !payloadAlreadyReleased) {
        state.statusLine = std::string(miningGateName(mining.gate.type)) + " is still locked. Complete its marked requirements before tethering.";
        artifact.tethered = false;
        return;
    }
    artifact.tethered = true;
    artifact.revealed = true;
}

void pulseMiningScanner(GameState& state, const ContentCatalog& catalog)
{
    MiningRunState& mining = state.run.mining;
    if (!mining.active) {
        return;
    }
    const MiningArenaRules arenaRules = activeMiningArenaRules(mining);
    if (!arenaRules.mechanics.fogAndScanner) {
        return;
    }
    ensureMiningMiniDroneAgents(state, catalog);
    const int revealedBefore = static_cast<int>(std::count_if(mining.terrain.cells.begin(), mining.terrain.cells.end(), [](const MiningCell& cell) {
        return cell.revealed;
    }));
    const bool artifactRevealedBefore = mining.artifact.revealed;
    const double scannerRadius = operatorControlled(mining)
        ? tuning::mining::scannerRevealRadius
        : miningDrillStats(state, catalog).scannerRadius;
    const double originX = controlledActorX(mining);
    const double originY = controlledActorY(mining);
    revealAround(mining, originX, originY, scannerRadius);
    bool gateStateChanged = false;
    auto activateGateMarkers = [&](double originX, double originY, double radius) {
        for (MiningGateMarker& marker : mining.gate.markers) {
            if (marker.activated) {
                continue;
            }
            const double dx = marker.x - originX;
            const double dy = marker.y - originY;
            if (dx * dx + dy * dy <= radius * radius) {
                marker.activated = true;
                gateStateChanged = true;
            }
        }
    };
    activateGateMarkers(originX, originY, scannerRadius);
    bool surveyDronePresent = false;
    for (const MiningMiniDroneAgent& agent : mining.miniDrones) {
        if (agent.role == MiniDroneRole::Survey) {
            surveyDronePresent = true;
            revealAround(mining, agent.x, agent.y, scannerRadius);
            activateGateMarkers(agent.x, agent.y, scannerRadius * 1.45);
        }
    }
    bool contextualGateMessage = false;
    if (mining.gate.active && mining.gate.burrowBreach && surveyDronePresent) {
        gateStateChanged = gateStateChanged || !mining.gate.surveyComplete;
        mining.gate.surveyComplete = true;
        state.statusLine = "Survey Drone mapped the long natural route around the marked burrow wall.";
        contextualGateMessage = true;
    } else if (!mining.gate.markers.empty()) {
        const int completed = static_cast<int>(std::count_if(mining.gate.markers.begin(), mining.gate.markers.end(), [](const MiningGateMarker& marker) {
            return marker.activated;
        }));
        state.statusLine = "Triangulation markers: " + std::to_string(completed) + "/" + std::to_string(mining.gate.requiredSurveyOrigins) + ". Reposition for a distinct origin.";
        contextualGateMessage = true;
    }
    if (gateStateChanged) {
        markMiningGateDerivedStateDirty(mining);
    }
    const int revealedAfter = static_cast<int>(std::count_if(mining.terrain.cells.begin(), mining.terrain.cells.end(), [](const MiningCell& cell) {
        return cell.revealed;
    }));
    const int terrainRevealed = std::max(0, revealedAfter - revealedBefore);
    const int signalsRevealed = !artifactRevealedBefore && mining.artifact.revealed ? 1 : 0;
    const std::string revealReport = "Scanner revealed " + std::to_string(terrainRevealed) + " terrain cells and "
        + std::to_string(signalsRevealed) + (signalsRevealed == 1 ? " signal." : " signals.");
    state.statusLine = contextualGateMessage ? state.statusLine + " " + revealReport : revealReport;
    mining.scannerPulseSeconds = 0.9;
}

void updateMiningArtifact(GameState& state, double dt)
{
    MiningRunState& mining = state.run.mining;
    MiningArtifactObject& artifact = mining.artifact;
    if (!artifact.present || artifact.state == MiningArtifactState::Delivered || artifact.state == MiningArtifactState::Destroyed) {
        return;
    }

    const double actorX = controlledActorX(mining);
    const double actorY = controlledActorY(mining);
    const double dx = actorX - artifact.x;
    const double dy = actorY - artifact.y;
    const double distance = std::sqrt(dx * dx + dy * dy);
    if (artifact.tethered && distance > tuning::mining::artifactTetherRangeCells * 1.75) {
        artifact.tethered = false;
    }

    if (artifact.state == MiningArtifactState::Embedded) {
        if (artifact.tethered) {
            const double tension = std::max(0.0, distance - tuning::mining::artifactTetherRestLengthCells);
            artifact.embedStrength = std::max(0.0, artifact.embedStrength - tension * tuning::mining::artifactTetherPullPerSecond * dt);
            artifact.velocityX = dx / std::max(0.001, distance) * tension * 0.22;
            artifact.velocityY = dy / std::max(0.001, distance) * tension * 0.22;
            if (artifact.embedStrength <= 0.0) {
                releaseEmbeddedArtifact(mining);
            }
        }
        return;
    }

    if (artifact.state != MiningArtifactState::Loose) {
        return;
    }

    artifact.velocityX += mining.gravityDirectionX * mining.gravityStrength * dt;
    artifact.velocityY += mining.gravityDirectionY * mining.gravityStrength * dt;
    if (artifact.tethered && distance > 0.001) {
        const double extension = std::max(0.0, distance - tuning::mining::artifactTetherRestLengthCells);
        const double towScale = mining.gate.active && mining.gate.heavyTow ? 0.48 : 1.0;
        const double pullX = dx / distance * extension * tuning::mining::artifactTetherSpring * towScale;
        const double pullY = dy / distance * extension * tuning::mining::artifactTetherSpring * towScale;
        artifact.velocityX += (pullX - artifact.velocityX * tuning::mining::artifactTetherDamping) * dt;
        artifact.velocityY += (pullY - artifact.velocityY * tuning::mining::artifactTetherDamping) * dt;
        const double transferScale = std::min(4.0, std::hypot(pullX, pullY) * 0.10);
        const double forceLength = std::max(0.001, std::hypot(pullX, pullY));
        const double actorForceX = -pullX / forceLength * transferScale * dt;
        const double actorForceY = -pullY / forceLength * transferScale * dt;
        if (operatorControlled(mining)) {
            mining.operatorVelocityX += actorForceX;
            mining.operatorVelocityY += actorForceY;
        } else {
            mining.rigVelocityX += actorForceX;
            mining.rigVelocityY += actorForceY;
        }
    } else {
        artifact.velocityX *= std::max(0.0, 1.0 - dt * 1.8);
        artifact.velocityY *= std::max(0.0, 1.0 - dt * 1.8);
    }

    auto solidAt = [&](double x, double y) {
        const int cellX = std::clamp(static_cast<int>(std::floor(x)), 0, mining.terrain.width - 1);
        const int cellY = std::clamp(static_cast<int>(std::floor(y)), 0, mining.terrain.height - 1);
        const MiningCell* cell = miningCellAt(mining.terrain, cellX, cellY);
        return cell != nullptr &&
            (miningMaterialSolid(cell->material) || cell->suitOnlyPassage);
    };
    auto impactDamage = [&]() {
        const double speed = std::sqrt(artifact.velocityX * artifact.velocityX + artifact.velocityY * artifact.velocityY);
        if (speed > tuning::mining::artifactImpactDamageThreshold) {
            damageMiningArtifact(mining, (speed - tuning::mining::artifactImpactDamageThreshold) * tuning::mining::artifactImpactDamageScale);
        }
    };

    const double nextX = std::clamp(artifact.x + artifact.velocityX * dt, 1.0, static_cast<double>(mining.terrain.width - 2));
    if (!solidAt(nextX, artifact.y)) {
        artifact.x = nextX;
    } else {
        impactDamage();
        artifact.velocityX *= -0.46;
    }

    const double nextY = std::clamp(artifact.y + artifact.velocityY * dt, 1.0, static_cast<double>(mining.terrain.height - 2));
    if (!solidAt(artifact.x, nextY)) {
        artifact.y = nextY;
    } else {
        impactDamage();
        artifact.velocityY *= -0.46;
    }

    const double bayX = mining.returnZoneX;
    const double bayY = mining.returnZoneY;
    const double bayDx = artifact.x - bayX;
    const double bayDy = artifact.y - bayY;
    if (mining.depthZone == mining.entryDepthZone &&
        artifact.tethered &&
        bayDx * bayDx + bayDy * bayDy <=
            tuning::mining::artifactDeliveryRadiusCells *
                tuning::mining::artifactDeliveryRadiusCells) {
        artifact.state = MiningArtifactState::Delivered;
        artifact.tethered = false;
        artifact.velocityX = 0.0;
        artifact.velocityY = 0.0;
        mining.temporaryArtifacts.push_back(artifactRecordForObject(artifact, mining.destinationId));
        mining.cargo += tuning::mining::artifactCargo;
    }
}

void updateMiningRun(GameState& state, const ContentCatalog& catalog, double deltaSeconds)
{
    MiningRunState& mining = state.run.mining;
    if (!mining.active) {
        return;
    }
    // Keep older active saves aligned with the current visual ship lane.
    mining.returnZoneX = miningShipStartX(mining);
    const double dt = std::clamp(deltaSeconds, 0.0, 0.08);
    mining.depthTransitionCooldownSeconds = std::max(0.0, mining.depthTransitionCooldownSeconds - dt);
    if (mining.failurePending) {
        advanceMiningCombatVisuals(mining, dt);
        mining.elapsedSeconds += dt;
        mining.failureSeconds = std::min(1.5, mining.failureSeconds + dt);
        mining.contactIntensity = 1.0;
        mining.scannerPulseSeconds = std::max(mining.scannerPulseSeconds, 0.35);
        mining.drilling = false;
        return;
    }
    const MiningDrillStats stats = miningDrillStats(state, catalog);
    const MiningArenaRules arenaRules = activeMiningArenaRules(mining);
    MiningLoadStats loadStats = miningLoadStats(state, catalog);
    auto finishAtReturnZone = [&]() {
        if (!miningExtractionReady(mining)) {
            return false;
        }
        const SurfaceActionOutcome outcome = finishMiningRun(state, catalog, false);
        if (!outcome.message.empty()) {
            state.statusLine = outcome.message;
        }
        return outcome.applied;
    };
    mining.elapsedSeconds += dt;
    if (arenaRules.mechanics.oxygenAndFuel) {
        mining.oxygenSeconds = std::max(0.0, mining.oxygenSeconds - dt);
    }
    if (arenaRules.mechanics.oxygenAndFuel && mining.oxygenSeconds <= 0.0) {
        if (finishAtReturnZone()) {
            return;
        }
        applyControlledActorDamage(
            mining,
            tuning::mining::oxygenDroneDamagePerSecond * dt);
        mining.contactIntensity = std::max(mining.contactIntensity, 0.45);
        if (!mining.oxygenDepletedNotified) {
            mining.oxygenDepletedNotified = true;
            state.statusLine = std::string(text::status::miningOxygenFailed);
        }
    }
    if (mining.droneHealth <= 0.0 && !operatorControlled(mining)) {
        (void)emergencyEjectFromRig(state);
    }
    if (operatorControlled(mining) && mining.operatorIntegrity <= 0.0) {
        triggerMiningFailure(
            state,
            "Suit integrity lost. Tether released; operator recovery failed.");
        return;
    }
    loadStats = miningLoadStats(state, catalog);
    updateMiningMiniDroneAgents(state, catalog, stats, dt);
    updateMiningOperatorSidearm(state, dt);
    applyMiningEnemyCombat(state, catalog, dt);
    if (mining.droneHealth <= 0.0 && !operatorControlled(mining)) {
        (void)emergencyEjectFromRig(state);
    }
    if (operatorControlled(mining) && mining.operatorIntegrity <= 0.0) {
        triggerMiningFailure(
            state,
            "Suit integrity lost. Tether released; operator recovery failed.");
        return;
    }
    loadStats = miningLoadStats(state, catalog);
    if (arenaRules.mechanics.oxygenAndFuel) {
        mining.fuelCycleProgress +=
            dt * tuning::mining::fuelCycleProgressPerSecond * loadStats.fuelConsumptionMultiplier;
    }
    if (arenaRules.mechanics.oxygenAndFuel && mining.oxygenSeconds > 0.0) {
        while (mining.fuelCycleProgress >= 1.0) {
            if (state.run.surfaceExpedition.sharedFuel <= 0) {
                if (finishAtReturnZone()) {
                    return;
                }
                triggerMiningFailure(state, text::fuel::miningFailedStatus(arkDiscovered(state)));
                return;
            }
            state.run.surfaceExpedition.sharedFuel = std::max(0, state.run.surfaceExpedition.sharedFuel - 1);
            mining.fuelSpent += 1;
            mining.fuelCycleProgress -= 1.0;
        }
    }
    mining.contactIntensity = std::max(0.0, mining.contactIntensity - dt * 5.5);
    mining.scannerPulseSeconds = std::max(0.0, mining.scannerPulseSeconds - dt);
    updateContactBounce(mining, dt);
    if (mining.drillIntegrity <= 0.0) {
        mining.drilling = false;
    }

    const bool suitActive = operatorControlled(mining);
    const MiningDrillStats activeDrillStats =
        suitActive ? miningOperatorDrillStats() : stats;
    if (suitActive) {
        if (!mining.rigDisabled && mining.rigDepthZone == mining.depthZone) {
            MiningLoadStats parkedRigLoad;
            simulateMiningActorMotion(
                state,
                stats,
                parkedRigLoad,
                arenaRules,
                false,
                false,
                dt);
        }
        simulateMiningActorMotion(
            state,
            stats,
            loadStats,
            arenaRules,
            true,
            true,
            dt);
    } else if (!mining.rigDisabled) {
        simulateMiningActorMotion(
            state,
            stats,
            loadStats,
            arenaRules,
            false,
            true,
            dt);
        mining.rigDepthZone = mining.depthZone;
    }
    updateMiningLooseChunks(mining, dt);

    const double activeX = controlledActorX(mining);
    const double activeY = controlledActorY(mining);
    const double activeCollider = suitActive
        ? tuning::mining::operatorColliderRadiusCells
        : tuning::mining::rigColliderRadiusCells;
    if (mining.depthTransitionCooldownSeconds <= 0.0 &&
        mining.depthZone > mining.entryDepthZone &&
        activeY < 3.0 &&
        canOccupyActor(
            mining.terrain,
            activeX,
            activeY - 0.8,
            activeCollider,
            suitActive)) {
        transitionDepthZone(state, catalog, -1);
    } else if (mining.depthTransitionCooldownSeconds <= 0.0 &&
        activeY > static_cast<double>(mining.terrain.height - 3) &&
        canOccupyActor(
            mining.terrain,
            activeX,
            activeY + 0.8,
            activeCollider,
            suitActive)) {
        transitionDepthZone(state, catalog, 1);
    }

    refreshTargetCell(mining);
    const double drillDirectionX = controlledAimX(mining);
    const double drillDirectionY = controlledAimY(mining);
    const bool drillTouchesTerrain =
        mining.drilling &&
        mining.drillIntegrity > 0.0 &&
        !drillFootprintCells(
             mining,
             drillDirectionX,
             drillDirectionY)
             .empty();
    if (drillTouchesTerrain) {
        applyDrillFootprintDamage(
            state,
            activeDrillStats,
            drillDirectionX,
            drillDirectionY,
            dt);
    } else {
        const bool externalThermalLoad = std::any_of(mining.enemies.begin(), mining.enemies.end(), [&](const MiningEnemy& enemy) {
            if (!enemy.active || enemy.type != MiningEnemyType::Elemental || enemy.affinity != MiningElementalAffinity::Thermal) {
                return false;
            }
            const double dx = controlledActorX(mining) - enemy.x;
            const double dy = controlledActorY(mining) - enemy.y;
            return dx * dx + dy * dy <= enemy.effectRadius * enemy.effectRadius;
        });
        if (arenaRules.mechanics.drillHeat && !externalThermalLoad) {
            mining.drillHeat = std::max(
                0.0,
                mining.drillHeat -
                    activeDrillStats.heatCoolingPerSecond * dt);
        }
    }
    const MiningElementalAffinity activeHazard = applyEnvironmentalHazardExposure(
        state,
        catalog,
        arenaRules,
        drillTouchesTerrain,
        dt);
    mining.drillHeat = std::clamp(mining.drillHeat, 0.0, 1.0);
    bool thermalStateChanged = false;
    if (mining.drillHeat >= 1.0 && !mining.drillThermalLock) {
        mining.drillThermalLock = true;
        mining.drilling = false;
        thermalStateChanged = true;
    } else if (mining.drillThermalLock && mining.drillHeat <= 0.60) {
        mining.drillThermalLock = false;
        thermalStateChanged = true;
    }
    mining.hazardDelta = std::clamp(mining.hazardDelta, 0.0, tuning::mining::maxMiningHazardDelta);
    updateMiningGate(state, arenaRules);
    updateMiningArtifact(state, dt);
    if (bankMiningPayloadAtShip(state, catalog)) {
        state.statusLine = std::string(text::status::miningStowed);
    } else if (!miningAtReturnZone(mining) && miningCarriedCargo(mining) > 0) {
        state.statusLine = std::string(text::status::miningReturnToShip);
    }
    if (activeHazard != MiningElementalAffinity::None && mining.oxygenSeconds > 0.0) {
        switch (activeHazard) {
        case MiningElementalAffinity::Thermal:
            state.statusLine = std::string(text::status::miningThermalHazard);
            break;
        case MiningElementalAffinity::Cryo:
            state.statusLine = std::string(text::status::miningCryoHazard);
            break;
        case MiningElementalAffinity::Toxic:
            state.statusLine = std::string(text::status::miningToxicHazard);
            break;
        case MiningElementalAffinity::Radiation:
            state.statusLine = std::string(text::status::miningRadiationHazard);
            break;
        case MiningElementalAffinity::None:
            break;
        }
    }
    if (thermalStateChanged) {
        state.statusLine = mining.drillThermalLock
            ? "Drill thermal cutoff engaged at 100%. Cooling required before restart."
            : "Drill cooled below 60%. Thermal lock cleared; drilling available.";
    }
    refreshTargetCell(mining);

    if (mining.drillIntegrity <= 0.0 && !mining.drillBreakNotified) {
        mining.drillBreakNotified = true;
        mining.drilling = false;
        state.statusLine = std::string(text::status::miningDrillFailed);
    }
    if (operatorControlled(mining) && mining.operatorIntegrity <= 0.0) {
        triggerMiningFailure(
            state,
            "Suit integrity lost. Tether released; operator recovery failed.");
        return;
    }
    if (mining.droneHealth <= 0.0 &&
        !mining.rigDisabled &&
        !operatorControlled(mining)) {
        (void)emergencyEjectFromRig(state);
    }
}

SurfaceActionOutcome finishMiningRun(GameState& state, const ContentCatalog& catalog, bool abort)
{
    SurfaceActionOutcome outcome;
    MiningRunState& mining = state.run.mining;
    SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    if (!mining.active || !expedition.active) {
        return outcome;
    }

    if (!abort && !miningExtractionReady(mining)) {
        outcome.message = mining.rigDisabled
            ? "Reach the shuttle in the EVA suit to complete emergency recovery."
            : (operatorControlled(mining)
                    ? "Return both the operator and functioning rig to the shuttle."
                    : std::string(text::status::miningReturnToShip));
        return outcome;
    }
    if (!abort) {
        bankMiningPayloadAtShip(state, catalog);
        bankPhysicallyDeliveredArtifactsAtShip(mining);
    }

    outcome.applied = true;
    outcome.message = abort
        ? (mining.failureMessage.empty() ? std::string(text::status::miningAborted) : mining.failureMessage)
        : (mining.rigDisabled
                ? std::string("Operator recovered. Physically delivered artifact secured; rig cargo remains with the wreck.")
                : std::string("Banked payload loaded for surface extraction."));
    outcome.materialDelta = mining.stowedMaterials;
    outcome.artifactFound = !mining.stowedArtifacts.empty();
    outcome.cargoDelta = mining.stowedCargo;
    const double recallPenalty = abort ? tuning::mining::emergencyRecallHazardPenalty : 0.0;
    const double payloadHazard = std::max(
        0.0,
        mining.hazardDelta +
            static_cast<double>(std::max(0, mining.stowedCargo)) * tuning::mining::cargoExtractionRiskScale -
            miningDrillStats(state, catalog).extractionRiskRelief);
    outcome.hazardDelta = payloadHazard + recallPenalty;
    const bool emergencyRecall = abort;
    const bool hadDroneUpgrades = !state.run.surfaceUpgradeIds.empty();

    addMiningMaterials(expedition.temporaryMaterials, mining.stowedMaterials);
    expedition.bankedMiningArenaValid = true;
    expedition.bankedMiningProgressionEligible = mining.progressionCreditEligible;
    expedition.bankedMiningArenaMetadata = mining.arenaMetadata;
    expedition.bankedMiningMaterials = mining.stowedMaterials;
    expedition.temporaryArtifacts.insert(expedition.temporaryArtifacts.end(), mining.stowedArtifacts.begin(), mining.stowedArtifacts.end());
    expedition.cargo += mining.stowedCargo;
    expedition.depth = std::max(expedition.depth, mining.deepestDepthZone);
    expedition.hazard = std::clamp(expedition.hazard + std::max(0.0, outcome.hazardDelta), 0.0, 1.0);
    outcome.extractionRiskDelta = std::max(0.0, outcome.hazardDelta);
    if (emergencyRecall) {
        state.run.surfaceUpgradeIds.clear();
        if (hadDroneUpgrades) {
            appendSurfaceLog(expedition, "Emergency recall cleared temporary field upgrades.");
        }
    }

    appendSurfaceLog(expedition, surfaceActionSummary(outcome));
    mining = {};
    state.screen = Screen::SurfaceExpedition;
    return outcome;
}

} // namespace rocket
