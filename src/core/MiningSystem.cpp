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

void triggerHardContactBounce(MiningRunState& mining, double dirX, double dirY)
{
    mining.recoilX = -dirX;
    mining.recoilY = -dirY;
    if (mining.contactBounceCooldown > 0.0) {
        return;
    }
    mining.contactBounceVelocity += tuning::mining::hardTerrainBounceImpulse * (1.0 + mining.contactIntensity * 0.35);
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

bool applyDrillDamage(GameState& state, const MiningDrillStats& stats, int x, int y, double dt)
{
    MiningRunState& mining = state.run.mining;
    MiningCell* target = miningCellAt(mining.terrain, x, y);
    if (!drillableCell(target)) {
        return false;
    }

    const MiningCellMaterial material = target->material;
    double drillPower = stats.power;
    if (mining.drillHeat >= tuning::mining::heatSlowThreshold) {
        drillPower *= tuning::mining::overheatedDrillSlow;
    }
    target->remainingToughness = std::max(0.0, target->remainingToughness - drillPower * dt);
    target->revealed = true;
    mining.drillHeat += (tuning::mining::heatRisePerSecond +
        (material == MiningCellMaterial::HardRock ? tuning::mining::heatHardRockBonus : 0.0)) * stats.heatRiseScale * dt;
    if (mining.drillHeat > tuning::mining::heatDamageThreshold) {
        mining.drillIntegrity = std::max(
            0.0,
            mining.drillIntegrity - std::max(0.0, 1.0 - stats.integrityRelief) * tuning::mining::overheatIntegrityDamagePerSecond * dt);
    }
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
    mining.droneX = static_cast<double>(mining.terrain.width) * 0.5;
    mining.droneY = 4.0;
    mining.aimX = mining.droneX;
    mining.aimY = mining.droneY + 1.0;
    mining.aimDirX = 0.0;
    mining.aimDirY = 1.0;
    revealAround(mining, mining.droneX, mining.droneY, tuning::mining::passiveLightRadius);
    refreshTargetCell(mining);
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
    stats.oreYieldChance = std::clamp(stats.oreYieldChance, 0.0, 0.36);
    stats.rareYieldChance = std::clamp(stats.rareYieldChance, 0.0, 0.48);
    stats.integrityRelief = std::clamp(stats.integrityRelief, 0.0, 0.70);
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
    const int chunksX = (terrain.width + tuning::mining::chunkSize - 1) / tuning::mining::chunkSize;
    const int chunksY = (terrain.height + tuning::mining::chunkSize - 1) / tuning::mining::chunkSize;
    terrain.dirtyChunks.assign(static_cast<std::size_t>(chunksX * chunksY), 1);
    return terrain;
}

SurfaceActionOutcome startMiningRun(GameState& state, const ContentCatalog& catalog)
{
    SurfaceActionOutcome outcome;
    SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    if (!expedition.active || expedition.supply < tuning::research::mineSupplyCost) {
        return outcome;
    }
    const Destination* destination = catalog.findDestination(expedition.destinationId);
    if (destination == nullptr) {
        return outcome;
    }

    expedition.supply -= tuning::research::mineSupplyCost;
    outcome.applied = true;
    outcome.supplyDelta = -tuning::research::mineSupplyCost;
    outcome.message = "Mining drone deployed.";

    MiningRunState mining;
    mining.active = true;
    mining.destinationId = expedition.destinationId;
    mining.siteProfile = expedition.siteProfile;
    mining.depthZone = expedition.depth;
    const MiningDrillStats stats = miningDrillStats(state, catalog);
    mining.oxygenSeconds = stats.oxygenSeconds;
    mining.terrain = generateMiningTerrain(state, *destination, expedition.siteProfile, mining.depthZone, stats.terrainWidth, stats.terrainHeight);
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
    appendSurfaceLog(expedition, "Mining drone deployed: -2 supply.");
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
    mining.contactIntensity = std::max(0.0, mining.contactIntensity - dt * 5.5);
    mining.scannerPulseSeconds = std::max(0.0, mining.scannerPulseSeconds - dt);
    updateContactBounce(mining, dt);

    const double moveLength = std::sqrt(mining.moveX * mining.moveX + mining.moveY * mining.moveY);
    if (moveLength > 0.01) {
        const double step = stats.speed * dt;
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
                const bool brokeCell = applyDrillDamage(state, stats, cellX, cellY, dt * (softMiningMaterial(obstacle->material) ? 1.18 : 1.0));
                const bool clearedCell = brokeCell || !miningMaterialSolid(obstacle->material);
                if (resistance > 0.0) {
                    mining.recoilX = -dirX;
                    mining.recoilY = -dirY;
                    const double limited = clearedCell
                        ? proposed
                        : contactLimitedPosition(position, position + (proposed - position) * resistance, xAxis ? cellX : cellY);
                    position = std::clamp(limited, 1.0, xAxis ? static_cast<double>(mining.terrain.width - 2) : static_cast<double>(mining.terrain.height - 2));
                } else {
                    triggerHardContactBounce(mining, dirX, dirY);
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
    MiningCell* target = miningCellAt(mining.terrain, mining.targetCellX, mining.targetCellY);
    const bool canDrill = mining.drilling && target != nullptr && miningMaterialSolid(target->material) && target->material != MiningCellMaterial::Bedrock;
    if (canDrill) {
        mining.contactIntensity = std::max(mining.contactIntensity, softMiningMaterial(target->material) ? 0.35 : 0.82);
        mining.recoilX = -mining.aimDirX;
        mining.recoilY = -mining.aimDirY;
        if (!softMiningMaterial(target->material)) {
            triggerHardContactBounce(mining, mining.aimDirX, mining.aimDirY);
        }
        applyDrillDamage(state, stats, mining.targetCellX, mining.targetCellY, dt);
    } else {
        mining.drillHeat = std::max(0.0, mining.drillHeat - stats.heatCoolingPerSecond * dt);
    }
    mining.drillHeat = std::clamp(mining.drillHeat, 0.0, 1.0);
    mining.hazardDelta = std::clamp(mining.hazardDelta, 0.0, tuning::mining::maxMiningHazardDelta);
    refreshTargetCell(mining);

    if (mining.oxygenSeconds <= 0.0 || mining.drillIntegrity <= 0.0) {
        mining.failurePending = true;
        mining.failureSeconds = 0.0;
        mining.drilling = false;
        mining.drillIntegrity = std::max(0.0, mining.drillIntegrity);
        mining.oxygenSeconds = std::max(0.0, mining.oxygenSeconds);
        mining.contactIntensity = 1.0;
        mining.scannerPulseSeconds = 0.9;
        mining.failureMessage = mining.drillIntegrity <= 0.0
            ? std::string(text::status::miningDrillFailed)
            : std::string(text::status::miningOxygenFailed);
        state.statusLine = mining.failureMessage;
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
