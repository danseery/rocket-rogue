#include "core/MiningSystem.h"
#include "core/ContentIds.h"
#include "core/GameFormat.h"
#include "core/GameText.h"
#include "core/Tuning.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <utility>

namespace rocket {

namespace {

constexpr double kPi = 3.14159265358979323846;

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

MiningCell makeCell(MiningCellMaterial material, int depthZone)
{
    const double toughness = miningMaterialToughness(material, depthZone);
    return {material, toughness, toughness, material == MiningCellMaterial::Empty, material == MiningCellMaterial::HazardPocket};
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

MiningEnemyType hostileEnemyTypeForLane(const Destination& destination, int lane)
{
    if (destination.tier >= 5 && lane % 5 == 4) {
        return MiningEnemyType::Mammal;
    }
    switch (lane % 4) {
    case 0:
        return MiningEnemyType::Ant;
    case 1:
        return MiningEnemyType::Flying;
    case 2:
        return MiningEnemyType::Beetle;
    default:
        return MiningEnemyType::Elemental;
    }
}

MiningElementalAffinity elementalAffinityForLane(const Destination& destination, SurfaceSiteProfile profile, int depthZone, int lane)
{
    if (destination.id == content::destination::outerPlanets) {
        return MiningElementalAffinity::Cryo;
    }
    if (destination.id == content::destination::nearbyStar) {
        if (profile == SurfaceSiteProfile::FractureField) {
            return MiningElementalAffinity::Toxic;
        }
        return depthZone % 2 == 0 ? MiningElementalAffinity::Thermal : MiningElementalAffinity::Radiation;
    }
    if (destination.id == content::destination::nearbyGalaxy) {
        return lane % 2 == 0 ? MiningElementalAffinity::Radiation : MiningElementalAffinity::Toxic;
    }
    switch ((destination.tier + depthZone + lane + static_cast<int>(profile)) % 4) {
    case 0:
        return MiningElementalAffinity::Thermal;
    case 1:
        return MiningElementalAffinity::Cryo;
    case 2:
        return MiningElementalAffinity::Radiation;
    default:
        return MiningElementalAffinity::Toxic;
    }
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

void applyHostileTunnelNetwork(MiningTerrain& terrain, const GameState& state, const Destination& destination, SurfaceSiteProfile profile)
{
    if (!hostileSystemActive(state) || !destinationAllowsEnemyEncounters(destination)) {
        return;
    }

    const std::uint64_t seed = hashCombine(hashCombine(state.seed, hashString(destination.id)), static_cast<std::uint64_t>(terrain.depthZone + 31));
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

    const int branchCount = std::clamp(3 + destination.tier + terrain.depthZone, 4, 8);
    for (int branch = 0; branch < branchCount; ++branch) {
        const double rowT = (static_cast<double>(branch) + 1.0) / (static_cast<double>(branchCount) + 1.0);
        const int branchY = std::clamp(6 + static_cast<int>(rowT * static_cast<double>(terrain.height - 11)), 6, terrain.height - 6);
        const int side = unitHash(seed, branch, branchY, terrain.depthZone, 131) < 0.5 ? -1 : 1;
        const int startX = std::clamp(terrain.width / 2 + static_cast<int>(std::round((unitHash(seed, branch, branchY, terrain.depthZone, 137) - 0.5) * 8.0)), 4, terrain.width - 5);
        const int length = 9 + static_cast<int>(unitHash(seed, branch, branchY, terrain.depthZone, 149) * 13.0);
        const int endX = std::clamp(startX + side * length, 4, terrain.width - 5);
        const int endY = std::clamp(branchY + static_cast<int>(std::round((unitHash(seed, branch, branchY, terrain.depthZone, 151) - 0.45) * 8.0)), 6, terrain.height - 6);
        const MiningEnemyType enemy = hostileEnemyTypeForLane(destination, branch + terrain.depthZone);
        const bool mammalBurrow = enemy == MiningEnemyType::Mammal;
        const MiningCellFeature branchFeature = mammalBurrow ? MiningCellFeature::OrganicBurrow : MiningCellFeature::BranchTunnel;
        const int branchRadius = mammalBurrow ? 2 : 1;
        carveLine(terrain, startX, branchY, endX, branchY, branchRadius, terrain.depthZone, branchFeature, mammalBurrow ? enemy : MiningEnemyType::None);
        carveLine(terrain, endX, branchY, endX, endY, branchRadius, terrain.depthZone, branchFeature, mammalBurrow ? enemy : MiningEnemyType::None);

        const int encounterX = std::clamp((startX + endX) / 2, 3, terrain.width - 4);
        carveTunnelDisk(terrain, encounterX, branchY, mammalBurrow ? 3 : 2, terrain.depthZone, mammalBurrow ? MiningCellFeature::OrganicBurrow : MiningCellFeature::EncounterZone, enemy);

        const double roomRoll = unitHash(seed, branch, endY, terrain.depthZone, 173);
        MiningCellFeature room = MiningCellFeature::TreasureVault;
        MiningCellMaterial reward = MiningCellMaterial::RareOre;
        if (mammalBurrow) {
            room = MiningCellFeature::BossChamber;
            reward = terrain.depthZone >= 1 || destination.tier >= 5 ? MiningCellMaterial::ArtifactCache : MiningCellMaterial::ExoticVein;
        } else if (roomRoll > 0.72) {
            room = MiningCellFeature::MinibossLair;
            reward = terrain.depthZone >= 2 || destination.tier >= 5 ? MiningCellMaterial::ExoticVein : MiningCellMaterial::RareOre;
        } else if (roomRoll < 0.26 || profile == SurfaceSiteProfile::FractureField) {
            room = MiningCellFeature::HiveNest;
            reward = MiningCellMaterial::CommonOre;
        } else if (profile == SurfaceSiteProfile::OreShelf) {
            reward = MiningCellMaterial::RareOre;
        }
        const int roomHalfWidth = room == MiningCellFeature::BossChamber ? 5 : 3 + (room == MiningCellFeature::MinibossLair ? 1 : 0);
        const int roomHalfHeight = room == MiningCellFeature::BossChamber ? 3 : 2;
        carveRoom(terrain, endX, endY, roomHalfWidth, roomHalfHeight, terrain.depthZone, room, enemy, reward);
    }
}

MiningCellMaterial generatedMaterial(
    const GameState& state,
    const Destination& destination,
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
    const std::uint64_t seed = hashCombine(state.seed, hashString(destination.id));
    const double pocket = unitHash(seed, x / 3, y / 3, depthZone, 11);
    const double fleck = unitHash(seed, x, y, depthZone, 23);
    const double artifactRoll = unitHash(seed, x / 4, y / 4, depthZone, 37);
    const double hazardRoll = unitHash(seed, x / 4, y / 4, depthZone, 53);

    const double siteOreBonus = profile == SurfaceSiteProfile::OreShelf ? 0.05 : 0.0;
    const double fractureArtifactBonus = profile == SurfaceSiteProfile::FractureField ? 0.025 : 0.0;
    const double tierBonus = static_cast<double>(destination.tier) * 0.012;

    if (depthZone >= 1 && artifactRoll > 0.986 - fractureArtifactBonus - tierBonus && y > height / 2) {
        return MiningCellMaterial::ArtifactCache;
    }
    if (hazardRoll > 0.978 - depth * 0.010 && y > 8) {
        return MiningCellMaterial::HazardPocket;
    }
    if (depthZone >= 2 && pocket > 0.962 - tierBonus && y > 12) {
        return MiningCellMaterial::ExoticVein;
    }
    if (pocket > 0.925 - depth * 0.020 - siteOreBonus - tierBonus) {
        return MiningCellMaterial::RareOre;
    }
    if (pocket > 0.745 - siteOreBonus - depth * 0.030 || fleck > 0.940) {
        return MiningCellMaterial::CommonOre;
    }
    if (fleck < 0.22 + depth * 0.08) {
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
                cell->revealed = true;
            }
        }
    }
}

bool softMiningMaterial(MiningCellMaterial material)
{
    return material == MiningCellMaterial::Regolith || material == MiningCellMaterial::HazardPocket;
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
    mining.contactBounceCooldown = tuning::mining::hardTerrainBounceCooldownSeconds;
}

bool canOccupy(const MiningTerrain& terrain, double x, double y)
{
    const int cellX = static_cast<int>(std::floor(x));
    const int cellY = static_cast<int>(std::floor(y));
    const MiningCell* cell = miningCellAt(terrain, cellX, cellY);
    return cell != nullptr && !miningMaterialSolid(cell->material);
}

bool drillableCell(const MiningCell* cell)
{
    return cell != nullptr && miningMaterialSolid(cell->material) && cell->material != MiningCellMaterial::Bedrock;
}

void addBrokenCellReward(GameState& state, const MiningDrillStats& stats, MiningCellMaterial material);

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

bool applyDrillDamage(GameState& state, const MiningDrillStats& stats, int x, int y, double dt)
{
    MiningRunState& mining = state.run.mining;
    MiningCell* target = miningCellAt(mining.terrain, x, y);
    if (!drillableCell(target)) {
        return false;
    }

    const MiningCellMaterial material = target->material;
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
        const MiningCellMaterial brokenMaterial = target->material;
        *target = makeCell(MiningCellMaterial::Empty, mining.depthZone);
        target->revealed = true;
        mining.cellsBroken += 1;
        addBrokenCellReward(state, stats, brokenMaterial);
        revealAround(mining, static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5, 1.35);
        markDirty(mining.terrain, x, y);
        return true;
    }
    return false;
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

    const double footprintLength = tuning::mining::drillRangeCells + 0.95;
    const double baseHalfWidth = 0.95;
    const double tipHalfWidth = 0.24;
    const double originX = mining.droneX + dirX * 0.25;
    const double originY = mining.droneY + dirY * 0.25;
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
        maxHeatDelta = std::max(maxHeatDelta, drillHeatDelta(cell->material, stats, contactDt));
        maxIntegrityExposure = std::max(maxIntegrityExposure, contactDt);
        brokeAny = applyDrillDamage(state, stats, contact.x, contact.y, contactDt) || brokeAny;
    }

    applyDrillSystemLoad(mining, stats, maxHeatDelta, maxIntegrityExposure);
    mining.contactIntensity = std::max(mining.contactIntensity, touchedHardMaterial ? 0.82 : 0.35);
    mining.recoilX = -dirX;
    mining.recoilY = -dirY;
    if (touchedHardMaterial && !touchedSoftMaterial) {
        triggerHardContactBounce(mining, dirX, dirY, stats.hardRockBounceRelief);
    }
    return brokeAny || !cells.empty();
}

void setAimDirection(MiningRunState& mining, double dirX, double dirY)
{
    const double length = std::sqrt(dirX * dirX + dirY * dirY);
    if (length < 0.0001) {
        return;
    }

    const double step = (kPi * 2.0) / static_cast<double>(std::max(1, tuning::mining::drillAimDirections));
    const double snapped = std::round(std::atan2(dirY, dirX) / step) * step;
    mining.aimDirX = std::cos(snapped);
    mining.aimDirY = std::sin(snapped);
    mining.aimX = mining.droneX + mining.aimDirX * tuning::mining::drillRangeCells;
    mining.aimY = mining.droneY + mining.aimDirY * tuning::mining::drillRangeCells;
}

bool findNearbyDrillTarget(const MiningRunState& mining, double dirX, double dirY, int& targetX, int& targetY, double& tipX, double& tipY)
{
    const double reach = tuning::mining::drillRangeCells + 0.55;
    const int minX = std::max(0, static_cast<int>(std::floor(mining.droneX - reach)));
    const int maxX = std::min(mining.terrain.width - 1, static_cast<int>(std::ceil(mining.droneX + reach)));
    const int minY = std::max(0, static_cast<int>(std::floor(mining.droneY - reach)));
    const int maxY = std::min(mining.terrain.height - 1, static_cast<int>(std::ceil(mining.droneY + reach)));
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
            const double vx = cellX - mining.droneX;
            const double vy = cellY - mining.droneY;
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

            const double overshoot = std::max(0.0, along - tuning::mining::drillRangeCells);
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

void addBrokenCellReward(GameState& state, const MiningDrillStats& stats, MiningCellMaterial material)
{
    MiningRunState& mining = state.run.mining;
    MaterialInventory gain;
    switch (material) {
    case MiningCellMaterial::Regolith:
        if (mining.cellsBroken % 4 == 0) {
            gain.common = 1;
        }
        break;
    case MiningCellMaterial::CommonOre:
        gain.common = 1;
        break;
    case MiningCellMaterial::RareOre:
        gain.rare = 1;
        if (unitHash(state.seed, mining.cellsBroken, mining.depthZone, 0, 71) < stats.rareYieldChance) {
            gain.rare += 1;
        }
        break;
    case MiningCellMaterial::ExoticVein:
        gain.exotic = 1;
        break;
    case MiningCellMaterial::ArtifactCache:
        mining.temporaryArtifacts.push_back({artifactId(mining), mining.destinationId, false});
        mining.cargo += tuning::mining::artifactCargo;
        break;
    case MiningCellMaterial::HazardPocket:
        mining.hazardDelta += tuning::mining::hazardPocketRisk;
        mining.drillIntegrity = std::max(0.0, mining.drillIntegrity - tuning::mining::hazardPocketIntegrityDamage);
        break;
    default:
        break;
    }

    if (gain.common > 0 && unitHash(state.seed, mining.cellsBroken, mining.depthZone, 0, 83) < stats.oreYieldChance) {
        gain.common += 1;
    }
    if (gain.rare > 0 && unitHash(state.seed, mining.cellsBroken, mining.depthZone, 1, 83) < stats.oreYieldChance * 0.75) {
        gain.rare += 1;
    }
    if (gain.exotic > 0 && unitHash(state.seed, mining.cellsBroken, mining.depthZone, 2, 83) < stats.oreYieldChance * 0.45) {
        gain.exotic += 1;
    }

    addMiningMaterials(mining.temporaryMaterials, gain);
    mining.cargo += materialCargo(gain);
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

void spawnMiningEnemies(MiningRunState& mining, const Destination& destination)
{
    bool minibossSpawned = false;
    bool bossSpawned = false;
    for (int y = 0; y < mining.terrain.height && static_cast<int>(mining.enemies.size()) < tuning::mining::maxActiveEnemies; ++y) {
        for (int x = 0; x < mining.terrain.width && static_cast<int>(mining.enemies.size()) < tuning::mining::maxActiveEnemies; ++x) {
            const MiningCell* cell = miningCellAt(mining.terrain, x, y);
            if (cell == nullptr || cell->enemy == MiningEnemyType::None || miningMaterialSolid(cell->material)) {
                continue;
            }
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
                ? elementalAffinityForLane(destination, mining.siteProfile, mining.depthZone, x + y)
                : MiningElementalAffinity::None;
            mining.enemies.push_back(makeMiningEnemy(cell->enemy, cell->feature, affinity, static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5));
            minibossSpawned = minibossSpawned || cell->feature == MiningCellFeature::MinibossLair;
            bossSpawned = bossSpawned || cell->feature == MiningCellFeature::BossChamber;
        }
    }
}

bool enemyIgnoresTerrain(MiningEnemyType type)
{
    return type == MiningEnemyType::Flying;
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
    if (!drillableCell(target)) {
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

    *target = makeCell(MiningCellMaterial::Empty, mining.depthZone);
    target->feature = MiningCellFeature::OrganicBurrow;
    target->enemy = MiningEnemyType::Mammal;
    target->revealed = true;
    markDirty(mining.terrain, x, y);
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
    addMiningMaterials(mining.temporaryMaterials, gain);
    mining.cargo += materialCargo(gain);
}

double applyDefenseDamage(GameState& state, MiningEnemy& enemy, double rawDamage)
{
    if (!enemy.active || rawDamage <= 0.0) {
        return 0.0;
    }
    const double appliedDamage = rawDamage * std::clamp(1.0 - enemy.armor, 0.20, 1.0);
    enemy.health = std::max(0.0, enemy.health - appliedDamage);
    if (enemy.health <= 0.0 && enemy.active) {
        enemy.active = false;
        state.run.mining.enemiesDefeated += 1;
        addEnemyDefeatReward(state, enemy);
    }
    return appliedDamage;
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
        mining.drillIntegrity = std::max(0.0, mining.drillIntegrity - toxicDamage);
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
    mining.movementSlowSeconds = std::max(0.0, mining.movementSlowSeconds - dt);
    if (mining.movementSlowSeconds <= 0.0) {
        mining.movementSlowScale = 1.0;
    }
    if (mining.enemies.empty()) {
        return;
    }

    const MiniDroneLoadoutEffects drones = miniDroneLoadoutEffects(state, catalog);
    const double defenseDamage = tuning::mining::baseDefenseDamagePerSecond + drones.sentryDamagePerSecond;
    const double incomingRelief = std::clamp(drones.enemyDamageRelief + drones.enemyEncounterRelief * 0.75, 0.0, 0.70);
    const double shieldRelief = std::clamp(incomingRelief + drones.environmentalShieldRelief, 0.0, 0.82);
    const double areaRangeSq = tuning::mining::areaControlRangeCells * tuning::mining::areaControlRangeCells;
    int nearestIndex = -1;
    double nearestDistanceSq = tuning::mining::defenseRangeCells * tuning::mining::defenseRangeCells;

    for (std::size_t i = 0; i < mining.enemies.size(); ++i) {
        MiningEnemy& enemy = mining.enemies[i];
        if (!enemy.active) {
            continue;
        }
        const double dx = mining.droneX - enemy.x;
        const double dy = mining.droneY - enemy.y;
        const double distanceSq = dx * dx + dy * dy;
        if (distanceSq < nearestDistanceSq) {
            nearestDistanceSq = distanceSq;
            nearestIndex = static_cast<int>(i);
        }
    }

    if (nearestIndex >= 0 && defenseDamage > 0.0) {
        MiningEnemy& target = mining.enemies[static_cast<std::size_t>(nearestIndex)];
        const double appliedDamage = applyDefenseDamage(state, target, defenseDamage * dt);
        mining.defenseDamageDealt += appliedDamage;
    }

    if (drones.areaControlDamagePerSecond > 0.0) {
        for (MiningEnemy& enemy : mining.enemies) {
            if (!enemy.active) {
                continue;
            }
            const double dx = mining.droneX - enemy.x;
            const double dy = mining.droneY - enemy.y;
            if (dx * dx + dy * dy > areaRangeSq) {
                continue;
            }
            const double appliedDamage = applyDefenseDamage(state, enemy, drones.areaControlDamagePerSecond * dt);
            mining.defenseDamageDealt += appliedDamage;
            mining.areaControlDamageDealt += appliedDamage;
        }
    }

    for (MiningEnemy& enemy : mining.enemies) {
        if (!enemy.active) {
            continue;
        }
        const double dx = mining.droneX - enemy.x;
        const double dy = mining.droneY - enemy.y;
        const double distance = std::max(0.001, std::sqrt(dx * dx + dy * dy));
        const double dirX = dx / distance;
        const double dirY = dy / distance;
        const auto [moveDirX, moveDirY] = enemyMoveDirection(mining, enemy, dirX, dirY);
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

        const double attackRadius = std::max(tuning::mining::enemyContactRadiusCells, enemy.effectRadius);
        if (distance <= attackRadius) {
            const double rawDamage = enemy.damagePerSecond * tuning::mining::enemyDamageScale * dt;
            const double damage = rawDamage * (1.0 - shieldRelief);
            mining.drillIntegrity = std::max(0.0, mining.drillIntegrity - damage);
            mining.enemyDamageTaken += damage;
            mining.environmentalShieldAbsorbed += rawDamage - damage;
            mining.contactIntensity = std::max(mining.contactIntensity, 0.65);
            applyElementalContact(state, enemy, shieldRelief, dt);
            if (drones.reactiveArmorDamagePerSecond > 0.0) {
                const double appliedDamage = applyDefenseDamage(state, enemy, drones.reactiveArmorDamagePerSecond * dt);
                mining.defenseDamageDealt += appliedDamage;
                mining.reactiveArmorDamageDealt += appliedDamage;
            }
        }
    }
}

void refreshTargetCell(MiningRunState& mining)
{
    double dx = mining.aimDirX;
    double dy = mining.aimDirY;
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

    const double maxTipX = std::clamp(
        mining.droneX + dx * tuning::mining::drillRangeCells,
        0.0,
        static_cast<double>(mining.terrain.width - 1));
    const double maxTipY = std::clamp(
        mining.droneY + dy * tuning::mining::drillRangeCells,
        0.0,
        static_cast<double>(mining.terrain.height - 1));

    int targetX = std::clamp(static_cast<int>(std::floor(maxTipX)), 0, mining.terrain.width - 1);
    int targetY = std::clamp(static_cast<int>(std::floor(maxTipY)), 0, mining.terrain.height - 1);
    double tipX = maxTipX;
    double tipY = maxTipY;
    const int droneCellX = std::clamp(static_cast<int>(std::floor(mining.droneX)), 0, mining.terrain.width - 1);
    const int droneCellY = std::clamp(static_cast<int>(std::floor(mining.droneY)), 0, mining.terrain.height - 1);
    constexpr double step = 0.10;
    bool foundSolidOnRay = false;
    for (double distance = step; distance <= tuning::mining::drillRangeCells + 0.0001; distance += step) {
        const double probeX = std::clamp(
            mining.droneX + dx * distance,
            0.0,
            static_cast<double>(mining.terrain.width - 1));
        const double probeY = std::clamp(
            mining.droneY + dy * distance,
            0.0,
            static_cast<double>(mining.terrain.height - 1));
        const int probeCellX = std::clamp(static_cast<int>(std::floor(probeX)), 0, mining.terrain.width - 1);
        const int probeCellY = std::clamp(static_cast<int>(std::floor(probeY)), 0, mining.terrain.height - 1);
        if (probeCellX == droneCellX && probeCellY == droneCellY) {
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

void advanceDepthZone(GameState& state, const ContentCatalog& catalog)
{
    MiningRunState& mining = state.run.mining;
    const Destination* destination = catalog.findDestination(mining.destinationId);
    if (destination == nullptr) {
        return;
    }
    mining.depthZone += 1;
    mining.hazardDelta += tuning::mining::depthHazardRisk;
    const MiningDrillStats stats = miningDrillStats(state, catalog);
    mining.terrain = generateMiningTerrain(state, *destination, mining.siteProfile, mining.depthZone, stats.terrainWidth, stats.terrainHeight);
    mining.enemies.clear();
    spawnMiningEnemies(mining, *destination);
    mining.droneX = static_cast<double>(mining.terrain.width) * 0.5;
    mining.droneY = 4.0;
    mining.aimX = mining.droneX;
    mining.aimY = mining.droneY + 1.0;
    mining.aimDirX = 0.0;
    mining.aimDirY = 1.0;
    revealAround(mining, mining.droneX, mining.droneY, tuning::mining::passiveLightRadius);
    refreshTargetCell(mining);
}

void triggerMiningFailure(GameState& state, std::string message)
{
    MiningRunState& mining = state.run.mining;
    mining.failurePending = true;
    mining.failureSeconds = 0.0;
    mining.drilling = false;
    mining.drillIntegrity = std::max(0.0, mining.drillIntegrity);
    mining.oxygenSeconds = std::max(0.0, mining.oxygenSeconds);
    mining.contactIntensity = 1.0;
    mining.scannerPulseSeconds = 0.9;
    mining.failureMessage = std::move(message);
    state.statusLine = mining.failureMessage;
}

} // namespace

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
    const double depthScale = 1.0 + static_cast<double>(std::max(0, depthZone)) * 0.22;
    switch (material) {
    case MiningCellMaterial::Empty:
        return 0.0;
    case MiningCellMaterial::Regolith:
        return tuning::mining::regolithToughness * depthScale;
    case MiningCellMaterial::HardRock:
        return tuning::mining::hardRockToughness * depthScale;
    case MiningCellMaterial::CommonOre:
        return tuning::mining::commonOreToughness * depthScale;
    case MiningCellMaterial::RareOre:
        return tuning::mining::rareOreToughness * depthScale;
    case MiningCellMaterial::ExoticVein:
        return tuning::mining::exoticVeinToughness * depthScale;
    case MiningCellMaterial::ArtifactCache:
        return tuning::mining::artifactCacheToughness * depthScale;
    case MiningCellMaterial::HazardPocket:
        return tuning::mining::regolithToughness * depthScale;
    case MiningCellMaterial::Bedrock:
        return tuning::mining::bedrockToughness;
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

    stats.power += miningPower * 0.75;
    stats.oreYieldChance += miningYield * 0.09;
    stats.heatRiseScale = std::clamp(1.0 - miningCooling * 0.075, 0.62, 1.0);
    stats.heatCoolingPerSecond += miningCooling * 0.030;
    stats.integrityRelief += miningDurability * 0.075;
    stats.terrainWidth = std::clamp(tuning::mining::terrainWidth + static_cast<int>(std::round(miningWidth * 4.0)), 48, 84);
    stats.terrainHeight = std::clamp(tuning::mining::terrainHeight + static_cast<int>(std::round(miningDepth * 5.0)), 32, 58);

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
    return stats;
}

MiningTerrain generateMiningTerrain(const GameState& state, const Destination& destination, SurfaceSiteProfile profile, int depthZone, int width, int height)
{
    MiningTerrain terrain;
    terrain.width = std::max(8, width);
    terrain.height = std::max(8, height);
    terrain.depthZone = std::max(0, depthZone);
    terrain.cells.reserve(static_cast<std::size_t>(terrain.width * terrain.height));
    for (int y = 0; y < terrain.height; ++y) {
        for (int x = 0; x < terrain.width; ++x) {
            terrain.cells.push_back(makeCell(generatedMaterial(state, destination, profile, x, y, terrain.depthZone, terrain.width, terrain.height), terrain.depthZone));
        }
    }
    applyHostileTunnelNetwork(terrain, state, destination, profile);
    const int chunksX = (terrain.width + tuning::mining::chunkSize - 1) / tuning::mining::chunkSize;
    const int chunksY = (terrain.height + tuning::mining::chunkSize - 1) / tuning::mining::chunkSize;
    terrain.dirtyChunks.assign(static_cast<std::size_t>(chunksX * chunksY), 1);
    return terrain;
}

void applySurfaceProspects(MiningTerrain& terrain, const SurfaceExpeditionState& expedition)
{
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
    stampProspect(MiningCellMaterial::CommonOre, expedition.prospectMaterials.common, shallowBand, 2, MiningCellFeature::BranchTunnel);
    stampProspect(MiningCellMaterial::RareOre, expedition.prospectMaterials.rare, midBand, 2, MiningCellFeature::TreasureVault);
    stampProspect(MiningCellMaterial::ExoticVein, expedition.prospectMaterials.exotic, deepBand, 2, MiningCellFeature::TreasureVault);
    stampProspect(MiningCellMaterial::ArtifactCache, expedition.prospectArtifacts, deepBand + 2, 2, MiningCellFeature::TreasureVault);
}

SurfaceActionOutcome startMiningRun(GameState& state, const ContentCatalog& catalog)
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

    expedition.sharedFuel = std::max(0, expedition.sharedFuel - 1);
    expedition.miningRunUsed = true;
    outcome.applied = true;
    outcome.fuelDelta = -1;
    outcome.message = text::fuel::miningStartedStatus(arkKnown);

    MiningRunState mining;
    mining.active = true;
    mining.destinationId = expedition.destinationId;
    mining.siteProfile = expedition.siteProfile;
    mining.depthZone = expedition.depth;
    const MiningDrillStats stats = miningDrillStats(state, catalog);
    mining.oxygenSeconds = stats.oxygenSeconds;
    mining.fuelBurnSeconds = 0.0;
    mining.fuelSpent = 1;
    mining.terrain = generateMiningTerrain(state, *destination, expedition.siteProfile, mining.depthZone, stats.terrainWidth, stats.terrainHeight);
    applySurfaceProspects(mining.terrain, expedition);
    expedition.prospectMaterials = {};
    expedition.prospectArtifacts = 0;
    spawnMiningEnemies(mining, *destination);
    mining.droneX = static_cast<double>(mining.terrain.width) * 0.5;
    mining.droneY = 4.0;
    mining.aimX = mining.droneX;
    mining.aimY = mining.droneY + 1.0;
    mining.aimDirX = 0.0;
    mining.aimDirY = 1.0;
    revealAround(mining, mining.droneX, mining.droneY, tuning::mining::passiveLightRadius);
    refreshTargetCell(mining);
    state.run.mining = std::move(mining);
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
    mining.moveX = std::clamp(xAxis, -1.0, 1.0);
    mining.moveY = std::clamp(yAxis, -1.0, 1.0);
}

void setMiningAim(GameState& state, double normalizedX, double normalizedY)
{
    MiningRunState& mining = state.run.mining;
    if (!mining.active) {
        return;
    }
    const double candidateX = std::clamp(normalizedX, 0.0, 1.0) * static_cast<double>(std::max(1, mining.terrain.width - 1));
    const double candidateY = std::clamp(normalizedY, 0.0, 1.0) * static_cast<double>(std::max(1, mining.terrain.height - 1));
    const double dirX = candidateX - mining.droneX;
    const double dirY = candidateY - mining.droneY;
    const double distance = std::sqrt(dirX * dirX + dirY * dirY);
    if (distance >= tuning::mining::drillAimDeadzoneCells) {
        setAimDirection(mining, dirX, dirY);
    }
    refreshTargetCell(mining);
}

void setMiningDrilling(GameState& state, bool drilling)
{
    if (state.run.mining.active) {
        state.run.mining.drilling = drilling;
    }
}

void pulseMiningScanner(GameState& state, const ContentCatalog& catalog)
{
    MiningRunState& mining = state.run.mining;
    if (!mining.active) {
        return;
    }
    revealAround(mining, mining.droneX, mining.droneY, miningDrillStats(state, catalog).scannerRadius);
    mining.scannerPulseSeconds = 0.9;
}

void updateMiningRun(GameState& state, const ContentCatalog& catalog, double deltaSeconds)
{
    MiningRunState& mining = state.run.mining;
    if (!mining.active) {
        return;
    }
    const double dt = std::clamp(deltaSeconds, 0.0, 0.08);
    if (mining.failurePending) {
        mining.elapsedSeconds += dt;
        mining.failureSeconds = std::min(1.5, mining.failureSeconds + dt);
        mining.contactIntensity = 1.0;
        mining.scannerPulseSeconds = std::max(mining.scannerPulseSeconds, 0.35);
        mining.drilling = false;
        return;
    }
    const MiningDrillStats stats = miningDrillStats(state, catalog);
    mining.elapsedSeconds += dt;
    mining.oxygenSeconds = std::max(0.0, mining.oxygenSeconds - dt);
    applyMiningEnemyCombat(state, catalog, dt);
    mining.fuelBurnSeconds += dt;
    if (mining.oxygenSeconds > 0.0) {
        while (mining.fuelBurnSeconds >= tuning::mining::fuelSecondsPerUnit) {
            if (state.run.surfaceExpedition.sharedFuel <= 0) {
                triggerMiningFailure(state, text::fuel::miningFailedStatus(arkDiscovered(state)));
                return;
            }
            state.run.surfaceExpedition.sharedFuel = std::max(0, state.run.surfaceExpedition.sharedFuel - 1);
            mining.fuelSpent += 1;
            mining.fuelBurnSeconds -= tuning::mining::fuelSecondsPerUnit;
        }
    }
    const int passiveTarget = static_cast<int>(std::floor(mining.elapsedSeconds * stats.passiveDroneMiningRate));
    while (mining.passiveDroneYield < passiveTarget) {
        mining.temporaryMaterials.common += 1;
        mining.cargo += 1;
        mining.passiveDroneYield += 1;
    }
    mining.contactIntensity = std::max(0.0, mining.contactIntensity - dt * 5.5);
    mining.scannerPulseSeconds = std::max(0.0, mining.scannerPulseSeconds - dt);
    updateContactBounce(mining, dt);

    const double moveLength = std::sqrt(mining.moveX * mining.moveX + mining.moveY * mining.moveY);
    if (moveLength > 0.01) {
        const double step = stats.speed * std::clamp(mining.movementSlowScale, 0.40, 1.0) * dt;
        const double dirX = mining.moveX / std::max(1.0, moveLength);
        const double dirY = mining.moveY / std::max(1.0, moveLength);
        const double dx = dirX * step;
        const double dy = dirY * step;
        const double nextX = std::clamp(mining.droneX + dx, 1.0, static_cast<double>(mining.terrain.width - 2));
        const double nextY = std::clamp(mining.droneY + dy, 1.0, static_cast<double>(mining.terrain.height - 2));

        auto tryMoveAxis = [&](double& position, double proposed, double other, bool xAxis) {
            const int cellX = std::clamp(static_cast<int>(std::floor(xAxis ? proposed : other)), 0, mining.terrain.width - 1);
            const int cellY = std::clamp(static_cast<int>(std::floor(xAxis ? other : proposed)), 0, mining.terrain.height - 1);
            MiningCell* obstacle = miningCellAt(mining.terrain, cellX, cellY);
            if (obstacle == nullptr) {
                return;
            }
            if (!miningMaterialSolid(obstacle->material)) {
                position = proposed;
                return;
            }
            if (mining.drilling && obstacle->material != MiningCellMaterial::Bedrock) {
                setAimDirection(mining, dirX, dirY);
                const double resistance = miningContactResistance(obstacle->material);
                mining.contactIntensity = std::max(mining.contactIntensity, softMiningMaterial(obstacle->material) ? 0.45 : 1.0);
                applyDrillFootprintDamage(
                    state,
                    stats,
                    dirX,
                    dirY,
                    dt * (softMiningMaterial(obstacle->material) ? 1.18 : tuning::mining::contactDrillPowerScale));
                const bool clearedCell = !miningMaterialSolid(obstacle->material);
                if (resistance > 0.0) {
                    mining.recoilX = -dirX;
                    mining.recoilY = -dirY;
                    const double limited = clearedCell
                        ? proposed
                        : contactLimitedPosition(position, position + (proposed - position) * resistance, xAxis ? cellX : cellY);
                    position = std::clamp(limited, 1.0, xAxis ? static_cast<double>(mining.terrain.width - 2) : static_cast<double>(mining.terrain.height - 2));
                } else {
                    triggerHardContactBounce(mining, dirX, dirY, stats.hardRockBounceRelief);
                }
            }
        };

        if (canOccupy(mining.terrain, nextX, mining.droneY)) {
            mining.droneX = nextX;
        } else {
            tryMoveAxis(mining.droneX, nextX, mining.droneY, true);
        }
        if (canOccupy(mining.terrain, mining.droneX, nextY)) {
            mining.droneY = nextY;
        } else {
            tryMoveAxis(mining.droneY, nextY, mining.droneX, false);
        }
        mining.aimX = mining.droneX + mining.aimDirX * tuning::mining::drillRangeCells;
        mining.aimY = mining.droneY + mining.aimDirY * tuning::mining::drillRangeCells;
        revealAround(mining, mining.droneX, mining.droneY, tuning::mining::passiveLightRadius);
    }

    if (mining.droneY > static_cast<double>(mining.terrain.height - 3) && canOccupy(mining.terrain, mining.droneX, mining.droneY + 0.8)) {
        advanceDepthZone(state, catalog);
    }

    refreshTargetCell(mining);
    const bool drillTouchesTerrain = mining.drilling && !drillFootprintCells(mining, mining.aimDirX, mining.aimDirY).empty();
    if (drillTouchesTerrain) {
        applyDrillFootprintDamage(state, stats, mining.aimDirX, mining.aimDirY, dt);
    } else {
        mining.drillHeat = std::max(0.0, mining.drillHeat - stats.heatCoolingPerSecond * dt);
    }
    mining.drillHeat = std::clamp(mining.drillHeat, 0.0, 1.0);
    mining.hazardDelta = std::clamp(mining.hazardDelta, 0.0, tuning::mining::maxMiningHazardDelta);
    refreshTargetCell(mining);

    if (mining.oxygenSeconds <= 0.0 || mining.drillIntegrity <= 0.0) {
        triggerMiningFailure(
            state,
            mining.drillIntegrity <= 0.0
                ? std::string(text::status::miningDrillFailed)
                : std::string(text::status::miningOxygenFailed));
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

    outcome.applied = true;
    outcome.message = abort
        ? (mining.failureMessage.empty() ? std::string("Mining drone recalled under pressure.") : mining.failureMessage)
        : std::string("Mining payload stowed for extraction.");
    outcome.materialDelta = mining.temporaryMaterials;
    outcome.artifactFound = !mining.temporaryArtifacts.empty();
    outcome.cargoDelta = mining.cargo;
    outcome.hazardDelta = mining.hazardDelta + static_cast<double>(std::max(0, mining.cargo)) * tuning::mining::cargoExtractionRiskScale - miningDrillStats(state, catalog).extractionRiskRelief;

    addMiningMaterials(expedition.temporaryMaterials, mining.temporaryMaterials);
    expedition.temporaryArtifacts.insert(expedition.temporaryArtifacts.end(), mining.temporaryArtifacts.begin(), mining.temporaryArtifacts.end());
    expedition.cargo += mining.cargo;
    expedition.depth = std::max(expedition.depth, mining.depthZone);
    expedition.hazard = std::clamp(expedition.hazard + std::max(0.0, outcome.hazardDelta), 0.0, 1.0);
    outcome.extractionRiskDelta = std::max(0.0, outcome.hazardDelta);

    appendSurfaceLog(expedition, surfaceActionSummary(outcome));
    mining = {};
    state.screen = Screen::SurfaceExpedition;
    return outcome;
}

} // namespace rocket
