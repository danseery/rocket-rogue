#include "core/MiningSystem.h"
#include "core/ContentIds.h"
#include "core/GameFormat.h"
#include "core/GameText.h"
#include "core/MiniDroneCoordination.h"
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

double miningShipStartX(const MiningRunState& mining)
{
    const double leftClearance = tuning::mining::returnZoneRadiusCells + 0.5;
    const double rightClearance = std::max(leftClearance, static_cast<double>(mining.terrain.width) - leftClearance);
    return std::clamp(
        static_cast<double>(mining.terrain.width) * tuning::mining::returnZoneHorizontalFraction,
        leftClearance,
        rightClearance);
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
    if (destination.id == content::destination::moon) {
        return MiningElementalAffinity::Cryo;
    }
    if (destination.id == content::destination::mars) {
        return MiningElementalAffinity::Thermal;
    }
    if (destination.id == content::destination::outerPlanets) {
        return MiningElementalAffinity::Cryo;
    }
    if (destination.id == content::destination::nearbyStar) {
        if (profile == SurfaceSiteProfile::FractureField) {
            return MiningElementalAffinity::Toxic;
        }
        return depthZone <= 0 ? MiningElementalAffinity::Thermal : MiningElementalAffinity::Radiation;
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
            reward = MiningCellMaterial::ExoticVein;
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
        return depthZone >= 2 ? MiningCellMaterial::ExoticVein : MiningCellMaterial::RareOre;
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
    if (mining.artifact.present && (mining.artifact.state == MiningArtifactState::Embedded || mining.artifact.state == MiningArtifactState::Loose)) {
        const double dx = mining.artifact.x - centerX;
        const double dy = mining.artifact.y - centerY;
        if (dx * dx + dy * dy <= radiusSq) {
            mining.artifact.revealed = true;
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
    return cell != nullptr && !miningMaterialSolid(cell->material);
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
    *target = makeCell(MiningCellMaterial::Empty, mining.depthZone);
    target->revealed = true;
    mining.cellsBroken += 1;
    if (brokenMaterial == MiningCellMaterial::ArtifactCache) {
        releaseEmbeddedArtifact(mining);
    } else {
        const MaterialInventory gain = brokenCellReward(
            state,
            stats,
            brokenMaterial,
            miniDroneHaul == nullptr);
        if (miniDroneHaul != nullptr) {
            addMiningMaterials(*miniDroneHaul, gain);
        } else {
            addMiningMaterials(mining.temporaryMaterials, gain);
            mining.cargo += materialCargo(gain);
        }
    }
    revealAround(mining, static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5, 1.35);
    markDirty(mining.terrain, x, y);
    return true;
}

bool applyDrillDamage(GameState& state, const MiningDrillStats& stats, int x, int y, double dt)
{
    MiningRunState& mining = state.run.mining;
    MiningCell* target = miningCellAt(mining.terrain, x, y);
    if (!drillableCell(target)) {
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

MiniDroneHomePoint miniDroneHomePoint(const MiningRunState& mining, const MiningMiniDroneAgent& agent, int roleCount)
{
    const double spread = miniDroneRoleSpread(agent.roleIndex);
    switch (agent.role) {
    case MiniDroneRole::Mining:
        return {mining.droneX - 1.35 + spread, mining.droneY + 0.85};
    case MiniDroneRole::Resource:
    {
        const int formationCount = std::max(1, roleCount);
        const double minimumRadius = formationCount <= 1
            ? 0.0
            : tuning::mining::resourceDroneMinimumSpacingCells /
                (2.0 * std::sin(3.14159265358979323846 / static_cast<double>(formationCount)));
        const double radius = std::max(tuning::mining::resourceDroneCollectionRadiusCells, minimumRadius);
        const double angle = 2.0 * 3.14159265358979323846 *
            static_cast<double>(agent.roleIndex) / static_cast<double>(formationCount);
        return {mining.droneX + std::cos(angle) * radius, mining.droneY + std::sin(angle) * radius};
    }
    case MiniDroneRole::Survey:
    {
        const double formationOffset = tuning::mining::surveyDroneFormationOffsetCells(agent.roleIndex, roleCount);
        return {
            mining.droneX + formationOffset,
            mining.droneY + tuning::mining::surveyDroneLeadDistanceCells +
                std::abs(formationOffset) * tuning::mining::surveyDroneFormationArcDepthPerCell
        };
    }
    case MiniDroneRole::Hazard:
        return {
            mining.droneX + tuning::mining::hazardDroneHomeOffsetCells + spread,
            mining.droneY - 0.65
        };
    case MiniDroneRole::Attack:
    {
        const int formationCount = std::max(1, roleCount);
        const double minimumRadius = formationCount <= 1
            ? 0.0
            : tuning::mining::attackDroneHomeMinimumSpacingCells /
                (2.0 * std::sin(3.14159265358979323846 / static_cast<double>(formationCount)));
        const double radius = std::max(tuning::mining::attackDroneHomeRadiusCells, minimumRadius);
        const double angle = -3.14159265358979323846 * 0.5 +
            2.0 * 3.14159265358979323846 * static_cast<double>(agent.roleIndex) / static_cast<double>(formationCount);
        return {mining.droneX + std::cos(angle) * radius, mining.droneY + std::sin(angle) * radius};
    }
    case MiniDroneRole::Defense:
    {
        const int formationCount = std::max(1, roleCount);
        const double angle = 2.0 * 3.14159265358979323846 *
            static_cast<double>(agent.roleIndex) / static_cast<double>(formationCount);
        return {
            mining.droneX + std::cos(angle) * tuning::mining::defenseDroneGuardDistanceCells,
            mining.droneY + std::sin(angle) * tuning::mining::defenseDroneGuardDistanceCells
        };
    }
    }
    return {mining.droneX, mining.droneY};
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
    const double nextX = std::clamp(agent.x + agent.velocityX * dt, 0.5, static_cast<double>(mining.terrain.width) - 0.5);
    const double nextY = std::clamp(agent.y + agent.velocityY * dt, 0.5, static_cast<double>(mining.terrain.height) - 0.5);
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
        std::sqrt(agent.velocityX * agent.velocityX + agent.velocityY * agent.velocityY) <=
            tuning::mining::miniDroneStopSpeedCellsPerSecond;
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
    double dx = agent.x - mining.droneX;
    double dy = agent.y - mining.droneY;
    double distance = std::sqrt(dx * dx + dy * dy);
    if (distance >= tuning::mining::attackDroneRigClearanceCells) {
        return;
    }
    if (distance <= 0.0001) {
        dx = fallbackX - mining.droneX;
        dy = fallbackY - mining.droneY;
        distance = std::max(0.0001, std::sqrt(dx * dx + dy * dy));
    }
    const double normalX = dx / distance;
    const double normalY = dy / distance;
    agent.x = std::clamp(
        mining.droneX + normalX * tuning::mining::attackDroneRigClearanceCells,
        0.5,
        static_cast<double>(mining.terrain.width) - 0.5);
    agent.y = std::clamp(
        mining.droneY + normalY * tuning::mining::attackDroneRigClearanceCells,
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

bool miningHasLooseMaterials(const MiningRunState& mining)
{
    return mining.temporaryMaterials.common > 0 ||
        mining.temporaryMaterials.rare > 0 ||
        mining.temporaryMaterials.exotic > 0;
}

double resourceDroneTransferInterval(const MiningMiniDroneAgent& agent)
{
    const int upgrades = std::max(0, agent.upgradeLevel - 1);
    return tuning::mining::resourceDroneTransferSeconds /
        (1.0 + static_cast<double>(upgrades) * tuning::mining::resourceDroneUpgradeRateBonus);
}

bool loadOneResourceChunk(MiningRunState& mining, MiningMiniDroneAgent& agent)
{
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
                tuning::mining::hazardDroneRequiredMark(cell->hazardAffinity) > agent.upgradeLevel ||
                coordinator.reservedByOther(x, y, agent)) {
                continue;
            }
            candidates.push_back({
                x,
                y,
                tuning::mining::hazardDroneRequiredMark(cell->hazardAffinity),
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
        const bool refined = unitHash(
            state.seed,
            candidate.x,
            candidate.y,
            mining.depthZone,
            0x48415A415244ULL + static_cast<std::uint64_t>(agent.upgradeLevel)) <
            tuning::mining::hazardDroneRefinementChance(agent.upgradeLevel);
        *cell = makeCell(
            refined ? refinedHazardMaterial(affinity) : MiningCellMaterial::Regolith,
            mining.depthZone);
        cell->revealed = true;
        cell->feature = feature;
        cell->enemy = enemy;
        markDirty(mining.terrain, candidate.x, candidate.y);
        refinedCount += refined ? 1 : 0;
    }
    if (batchSize > 0) {
        state.statusLine = refinedCount > 0
            ? "Hazard Drone stabilized " + std::to_string(batchSize) + " tiles and refined " + std::to_string(refinedCount) +
                (refinedCount == 1 ? " valuable tile." : " valuable tiles.")
            : "Hazard Drone stabilized " + std::to_string(batchSize) + " hazardous tile" + (batchSize == 1 ? "." : "s.");
    }
    return batchSize;
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
    int totalRoleCounts[6] = {};
    for (const auto& [role, upgradeLevel] : expected) {
        (void)upgradeLevel;
        ++totalRoleCounts[static_cast<int>(role)];
    }
    int roleIndices[6] = {};
    for (const auto& [role, upgradeLevel] : expected) {
        MiningMiniDroneAgent agent;
        agent.role = role;
        agent.roleIndex = roleIndices[static_cast<int>(role)]++;
        agent.upgradeLevel = std::clamp(upgradeLevel, 1, 3);
        const MiniDroneHomePoint home = miniDroneHomePoint(mining, agent, totalRoleCounts[static_cast<int>(role)]);
        agent.x = home.x;
        agent.y = home.y;
        if (role == MiniDroneRole::Defense) {
            agent.defenseAngleRadians = std::atan2(home.y - mining.droneY, home.x - mining.droneX);
            agent.defenseAngleInitialized = true;
        }
        agent.behavior = MiningMiniDroneBehavior::Following;
        mining.miniDrones.push_back(agent);
    }
}

void updateMiningMiniDroneAgents(GameState& state, const ContentCatalog& catalog, const MiningDrillStats& stats, double dt)
{
    ensureMiningMiniDroneAgents(state, catalog);
    MiningRunState& mining = state.run.mining;
    MiningDroneCoordinator miningCoordinator(mining);
    miningCoordinator.synchronizeAssignments();
    HazardDroneCoordinator hazardCoordinator(mining);
    hazardCoordinator.synchronizeAssignments();
    AttackDroneCoordinator attackCoordinator(mining);
    attackCoordinator.synchronizeAssignments();
    DefenseDroneCoordinator defenseCoordinator(mining);
    defenseCoordinator.synchronizeAssignments();
    defenseCoordinator.advanceFormation(dt);
    SurveyDroneCoordinator surveyCoordinator(mining);
    surveyCoordinator.synchronizeAssignments();
    const MiniDroneLoadoutEffects loadoutEffects = miniDroneLoadoutEffects(state, catalog);
    int roleCounts[6] = {};
    for (const MiningMiniDroneAgent& agent : mining.miniDrones) {
        ++roleCounts[static_cast<int>(agent.role)];
    }

    for (MiningMiniDroneAgent& agent : mining.miniDrones) {
        agent.actionCooldownSeconds = std::max(0.0, agent.actionCooldownSeconds - dt);
        agent.surveyPulseSeconds = std::max(0.0, agent.surveyPulseSeconds - dt);
        const MiniDroneHomePoint home = miniDroneHomePoint(mining, agent, roleCounts[static_cast<int>(agent.role)]);
        switch (agent.role) {
        case MiniDroneRole::Mining: {
            const double mamaDistanceSq = miniDroneDistanceSquared(agent, mining.droneX, mining.droneY);
            const double leashSq = tuning::mining::miningDroneLeashRadiusCells * tuning::mining::miningDroneLeashRadiusCells;
            const int capacity = tuning::mining::miningDroneCapacityChunks(agent.upgradeLevel);
            int carriedChunks = miniDroneHaulChunkCount(agent);
            const MiniDroneHomePoint dock = miniDroneShipDockPoint(mining, agent);

            if (carriedChunks >= capacity && miningCoordinator.hasAssignment(agent)) {
                miningCoordinator.releaseAssignment(agent);
                agent.finishTargetBeforeReturn = false;
                agent.behavior = MiningMiniDroneBehavior::Traveling;
            }

            const bool deliveryInProgress = carriedChunks > 0 &&
                !miningCoordinator.hasAssignment(agent) &&
                (agent.behavior == MiningMiniDroneBehavior::Traveling ||
                    agent.behavior == MiningMiniDroneBehavior::Docked);
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

            if (miningCoordinator.hasAssignment(agent)) {
                if (mamaDistanceSq > leashSq) {
                    agent.finishTargetBeforeReturn = true;
                }
                const double targetX = static_cast<double>(agent.targetCellX) + 0.5;
                const double targetY = static_cast<double>(agent.targetCellY) + 0.5;
                const double workRangeSq = tuning::mining::miningDroneWorkRangeCells * tuning::mining::miningDroneWorkRangeCells;
                if (miniDroneDistanceSquared(agent, targetX, targetY) > workRangeSq) {
                    agent.behavior = MiningMiniDroneBehavior::Traveling;
                    moveMiniDroneToward(agent, mining, targetX, targetY, tuning::mining::miniDroneTravelSpeedCellsPerSecond, dt);
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
                if (moveMiniDroneToward(agent, mining, home.x, home.y, tuning::mining::miniDroneReturnSpeedCellsPerSecond, dt)) {
                    agent.finishTargetBeforeReturn = false;
                    agent.behavior = MiningMiniDroneBehavior::Following;
                }
            } else if (agent.actionCooldownSeconds > 0.0) {
                agent.behavior = MiningMiniDroneBehavior::Following;
                moveMiniDroneToward(agent, mining, home.x, home.y, tuning::mining::miniDroneTravelSpeedCellsPerSecond, dt);
            } else if (!miningCoordinator.acquireAssignment(agent)) {
                if (carriedChunks > 0) {
                    agent.behavior = MiningMiniDroneBehavior::Traveling;
                } else {
                    agent.actionCooldownSeconds = 0.45;
                    agent.behavior = MiningMiniDroneBehavior::Following;
                    moveMiniDroneToward(agent, mining, home.x, home.y, tuning::mining::miniDroneTravelSpeedCellsPerSecond, dt);
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
                    tuning::mining::miniDroneReturnSpeedCellsPerSecond,
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
                tuning::mining::miniDroneReturnSpeedCellsPerSecond,
                dt,
                MiniDroneArrivalStyle::SmoothFormation);
            break;
        }
        case MiniDroneRole::Resource: {
            const double transferInterval = resourceDroneTransferInterval(agent);
            const MiniDroneHomePoint dock = miniDroneShipDockPoint(mining, agent);
            int carriedChunks = miniDroneHaulChunkCount(agent);
            const bool shouldDeliver = carriedChunks > 0 &&
                (agent.behavior == MiningMiniDroneBehavior::Traveling ||
                    agent.behavior == MiningMiniDroneBehavior::Docked ||
                    carriedChunks >= tuning::mining::resourceDroneCapacityChunks ||
                    !miningHasLooseMaterials(mining));
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
                tuning::mining::miniDroneReturnSpeedCellsPerSecond,
                dt,
                MiniDroneArrivalStyle::SmoothFormation);
            const double collectionTolerance = previousBehavior == MiningMiniDroneBehavior::Working
                ? tuning::mining::resourceDroneCollectionExitToleranceCells
                : tuning::mining::resourceDroneCollectionEnterToleranceCells;
            const bool inCollectionPosition = miniDroneDistanceSquared(agent, home.x, home.y) <=
                collectionTolerance * collectionTolerance;
            if (!miningHasLooseMaterials(mining) || carriedChunks >= tuning::mining::resourceDroneCapacityChunks) {
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
                if (carriedChunks >= tuning::mining::resourceDroneCapacityChunks || !miningHasLooseMaterials(mining)) {
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
                    tuning::mining::surveyDroneReturnSpeedCellsPerSecond,
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
            if (moveMiniDroneToward(
                    agent,
                    mining,
                    home.x,
                    home.y,
                    tuning::mining::surveyDroneReturnSpeedCellsPerSecond,
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
            if (hazardCoordinator.hasAssignment(agent) ||
                (agent.actionCooldownSeconds <= 0.0 && hazardCoordinator.acquireAssignment(agent))) {
                const double targetX = static_cast<double>(agent.targetCellX) + 0.5;
                const double targetY = static_cast<double>(agent.targetCellY) + 0.5;
                const double workRangeSq = tuning::mining::hazardDroneWorkRangeCells *
                    tuning::mining::hazardDroneWorkRangeCells;
                if (miniDroneDistanceSquared(agent, targetX, targetY) > workRangeSq) {
                    agent.behavior = MiningMiniDroneBehavior::Traveling;
                    moveMiniDroneToward(
                        agent,
                        mining,
                        targetX,
                        targetY,
                        tuning::mining::miniDroneTravelSpeedCellsPerSecond,
                        dt);
                    break;
                }
                slowMiniDroneAtTask(agent, mining, dt);
                agent.behavior = MiningMiniDroneBehavior::Working;
                agent.taskProgressSeconds += dt;
                const double treatmentSeconds = tuning::mining::hazardDroneTreatmentSeconds(agent.upgradeLevel) /
                    (1.0 + loadoutEffects.hazardTreatmentRateBonus);
                if (agent.taskProgressSeconds >= treatmentSeconds) {
                    completeHazardTreatment(state, agent, hazardCoordinator);
                    hazardCoordinator.releaseAssignment(agent);
                    agent.actionCooldownSeconds = 0.20;
                    agent.behavior = MiningMiniDroneBehavior::Returning;
                }
                break;
            }
            agent.behavior = MiningMiniDroneBehavior::Returning;
            if (moveMiniDroneToward(
                    agent,
                    mining,
                    home.x,
                    home.y,
                    tuning::mining::miniDroneReturnSpeedCellsPerSecond,
                    dt,
                    MiniDroneArrivalStyle::SmoothFormation)) {
                agent.behavior = MiningMiniDroneBehavior::Following;
            }
            break;
        }
        }
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
    double maxToxicExposure = 0.0;
    double maxRadiationExposure = 0.0;
    for (const DrillFootprintCell& contact : cells) {
        const MiningCell* cell = miningCellAt(mining.terrain, contact.x, contact.y);
        if (cell == nullptr) {
            continue;
        }
        const double contactDt = dt * contact.powerScale;
        touchedSoftMaterial = touchedSoftMaterial || softMiningMaterial(cell->material);
        touchedHardMaterial = touchedHardMaterial || !softMiningMaterial(cell->material);
        double heatDelta = drillHeatDelta(cell->material, stats, contactDt);
        if (cell->material == MiningCellMaterial::HazardPocket && cell->hazard) {
            switch (cell->hazardAffinity) {
            case MiningElementalAffinity::Thermal:
                heatDelta += tuning::mining::elementalHeatRisePerSecond * contactDt;
                break;
            case MiningElementalAffinity::Cryo:
                mining.movementSlowSeconds = std::max(
                    mining.movementSlowSeconds,
                    tuning::mining::elementalCryoSlowDurationSeconds);
                mining.movementSlowScale = std::min(
                    mining.movementSlowScale,
                    tuning::mining::elementalCryoSlowScale);
                break;
            case MiningElementalAffinity::Toxic:
                maxToxicExposure = std::max(maxToxicExposure, contactDt);
                break;
            case MiningElementalAffinity::Radiation:
                maxRadiationExposure = std::max(maxRadiationExposure, contactDt);
                break;
            case MiningElementalAffinity::None:
                break;
            }
        }
        maxHeatDelta = std::max(maxHeatDelta, heatDelta);
        maxIntegrityExposure = std::max(maxIntegrityExposure, contactDt);
        brokeAny = applyDrillDamage(state, stats, contact.x, contact.y, contactDt) || brokeAny;
    }

    applyDrillSystemLoad(mining, stats, maxHeatDelta, maxIntegrityExposure);
    mining.drillIntegrity = std::max(
        0.0,
        mining.drillIntegrity - tuning::mining::elementalToxicIntegrityDamagePerSecond * maxToxicExposure);
    mining.hazardDelta += tuning::mining::elementalRadiationHazardPerSecond * maxRadiationExposure;
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
        if (cell == nullptr || cell->material == MiningCellMaterial::Bedrock || cell->material == MiningCellMaterial::Empty) {
            return false;
        }
        *cell = makeCell(MiningCellMaterial::ArtifactCache, mining.depthZone);
        cell->revealed = revealed;
        cell->feature = MiningCellFeature::TreasureVault;
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
        if (mining.cellsBroken % 4 == 0) {
            gain.common = 1;
        }
        break;
    case MiningCellMaterial::CommonOre:
        gain.common = 1;
        break;
    case MiningCellMaterial::RareOre:
        gain.rare = 1;
        if (includeYieldBonuses &&
            unitHash(state.seed, mining.cellsBroken, mining.depthZone, 0, 71) < stats.rareYieldChance) {
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
    default:
        break;
    }

    if (includeYieldBonuses && gain.common > 0 &&
        unitHash(state.seed, mining.cellsBroken, mining.depthZone, 0, 83) < stats.oreYieldChance) {
        gain.common += 1;
    }
    if (includeYieldBonuses && gain.rare > 0 &&
        unitHash(state.seed, mining.cellsBroken, mining.depthZone, 1, 83) < stats.oreYieldChance * 0.75) {
        gain.rare += 1;
    }
    if (includeYieldBonuses && gain.exotic > 0 &&
        unitHash(state.seed, mining.cellsBroken, mining.depthZone, 2, 83) < stats.oreYieldChance * 0.45) {
        gain.exotic += 1;
    }
    return gain;
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

bool enemyUsesRangedAttack(MiningEnemyType type)
{
    return type == MiningEnemyType::Flying || type == MiningEnemyType::Elemental;
}

bool enemyUsesMeleeAttack(MiningEnemyType type)
{
    return type == MiningEnemyType::Ant || type == MiningEnemyType::Beetle || type == MiningEnemyType::Mammal;
}

void updateMiningEnemySpawners(MiningRunState& mining, double dt)
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
            activeEnemyCount < tuning::mining::maxActiveEnemies) {
            const double angle = static_cast<double>(spec.spawned % 8) * (kPi * 0.25);
            const double radius = tuning::mining::enemySpawnerSpawnRadiusCells;
            double spawnX = std::clamp(spawner.x + std::cos(angle) * radius, 1.0, static_cast<double>(mining.terrain.width - 2));
            double spawnY = std::clamp(spawner.y + std::sin(angle) * radius, 1.0, static_cast<double>(mining.terrain.height - 2));
            if (!enemyIgnoresTerrain(spec.enemyType) && !canOccupy(mining.terrain, spawnX, spawnY)) {
                spawnX = spawner.x;
                spawnY = spawner.y;
            }

            spawnedEnemies.push_back(makeMiningEnemy(
                spec.enemyType,
                spawner.sourceFeature,
                spec.affinity,
                spawnX,
                spawnY));
            spec.spawned += 1;
            activeEnemyCount += 1;
            spec.cooldownSeconds += interval;
        }
    }

    mining.enemies.insert(mining.enemies.end(), spawnedEnemies.begin(), spawnedEnemies.end());
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
    addMiningMaterials(mining.temporaryMaterials, gain);
    mining.cargo += materialCargo(gain);
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
        mining.droneHealth = std::max(0.0, mining.droneHealth - toxicDamage);
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
    advanceMiningCombatVisuals(mining, dt);
    mining.movementSlowSeconds = std::max(0.0, mining.movementSlowSeconds - dt);
    if (mining.movementSlowSeconds <= 0.0) {
        mining.movementSlowScale = 1.0;
    }
    mining.alliedFireCooldownSeconds = std::max(0.0, mining.alliedFireCooldownSeconds - dt);
    mining.areaControlPulseCooldownSeconds = std::max(0.0, mining.areaControlPulseCooldownSeconds - dt);
    updateMiningEnemySpawners(mining, dt);
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
        const double dx = mining.droneX - enemy.x;
        const double dy = mining.droneY - enemy.y;
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
                mining.droneX,
                mining.droneY,
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
            const double dx = mining.droneX - enemy.x;
            const double dy = mining.droneY - enemy.y;
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
        const double dx = mining.droneX - enemy.x;
        const double dy = mining.droneY - enemy.y;
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
            mining.droneHealth = std::max(0.0, mining.droneHealth - damage);
            mining.enemyDamageTaken += damage;
            mining.environmentalShieldAbsorbed += shieldImpact.absorbedDamage +
                std::max(0.0, shieldImpact.remainingDamage - damage);
            mining.contactIntensity = std::max(
                mining.contactIntensity,
                shieldImpact.absorbedDamage > 0.0 ? 0.38 : 0.65);
            if (damage > 0.00001) {
                pushMiningDamageNumber(mining, mining.droneX, mining.droneY, damage * 100.0, MiningCombatTeam::Enemy, critical, true);
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
            mining.droneHealth = std::max(0.0, mining.droneHealth - damage);
            mining.enemyDamageTaken += damage;
            mining.environmentalShieldAbsorbed += shieldImpact.absorbedDamage +
                std::max(0.0, shieldImpact.remainingDamage - damage);
            mining.contactIntensity = std::max(
                mining.contactIntensity,
                shieldImpact.absorbedDamage > 0.0 ? 0.30 : 0.50);
            if (damage > 0.00001) {
                pushMiningDamageNumber(mining, mining.droneX, mining.droneY, damage * 100.0, MiningCombatTeam::Enemy, critical, true);
            }
            enemy.attackCooldownSeconds = tuning::mining::enemyRangedAttackIntervalSeconds;
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
    stats.heatCoolingPerSecond *= tuning::mining::heatCoolingMultiplier;
    return stats;
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
    const int cost = miningDrillRepairCost(mining);
    if (!mining.active || !miningAtReturnZone(mining) || !spendMiningRepairMaterials(mining, cost)) {
        return false;
    }
    mining.drillIntegrity = 1.0;
    mining.drillBreakNotified = false;
    return true;
}

bool repairMiningDrone(GameState& state)
{
    MiningRunState& mining = state.run.mining;
    const int cost = miningDroneRepairCost(mining);
    if (!mining.active || !miningAtReturnZone(mining) || !spendMiningRepairMaterials(mining, cost)) {
        return false;
    }
    mining.droneHealth = 1.0;
    return true;
}

bool miningAtReturnZone(const MiningRunState& mining)
{
    if (!mining.active) {
        return false;
    }
    const double dx = mining.droneX - mining.returnZoneX;
    const double dy = mining.droneY - mining.returnZoneY;
    return dx * dx + dy * dy <= tuning::mining::returnZoneRadiusCells * tuning::mining::returnZoneRadiusCells;
}

MiningLoadStats miningLoadStats(const GameState& state, const ContentCatalog& catalog)
{
    MiningLoadStats load;
    const MiningRunState& mining = state.run.mining;
    const MiningDrillStats stats = miningDrillStats(state, catalog);
    const double towWeight = (mining.artifact.present &&
                                 mining.artifact.tethered &&
                                 mining.artifact.state != MiningArtifactState::Delivered &&
                                 mining.artifact.state != MiningArtifactState::Destroyed)
        ? tuning::mining::tetheredArtifactCargoWeight * std::clamp(1.0 - stats.artifactTowEfficiency, 0.20, 1.0)
        : 0.0;
    load.currentLoad = static_cast<double>(miningCarriedCargo(mining)) + towWeight;
    load.freeBuffer = tuning::mining::baseCarryBufferCargo + stats.storage;
    load.burden = std::max(0.0, load.currentLoad - load.freeBuffer);
    const double penaltyScale = std::clamp(1.0 - stats.engineEfficiency, 0.20, 1.0);
    load.speedMultiplier = std::clamp(
        1.0 - load.burden * tuning::mining::loadSpeedPenaltyPerCargo * penaltyScale,
        tuning::mining::minLoadedSpeedMultiplier,
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
    if (!miningAtReturnZone(mining)) {
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
    mining.oxygenDepletedNotified = false;
    return true;
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
            const MiningCellMaterial material = generatedMaterial(
                state,
                destination,
                profile,
                x,
                y,
                terrain.depthZone,
                terrain.width,
                terrain.height);
            const MiningElementalAffinity affinity = material == MiningCellMaterial::HazardPocket
                ? elementalAffinityForLane(destination, profile, terrain.depthZone, x / 4)
                : MiningElementalAffinity::None;
            terrain.cells.push_back(makeCell(material, terrain.depthZone, affinity));
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
    mining.fuelCycleProgress = 0.0;
    mining.fuelSpent = 1;
    mining.terrain = generateMiningTerrain(state, *destination, expedition.siteProfile, mining.depthZone, stats.terrainWidth, stats.terrainHeight);
    applySurfaceProspects(mining.terrain, expedition);
    const bool forcedArtifact = expedition.prospectArtifacts > 0;
    placeMiningArtifact(state, mining, *destination, forcedArtifact, forcedArtifact);
    expedition.prospectMaterials = {};
    expedition.prospectArtifacts = 0;
    expedition.depthProspects.clear();
    spawnMiningEnemies(mining, *destination);
    mining.droneX = static_cast<double>(mining.terrain.width) * 0.5;
    mining.droneY = 4.0;
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
    mining.moveX = std::clamp(xAxis, -1.0, 1.0);
    mining.moveY = std::clamp(yAxis, -1.0, 1.0);
    const double moveLength = std::sqrt(mining.moveX * mining.moveX + mining.moveY * mining.moveY);
    if (moveLength > 0.01) {
        setAimDirection(mining, mining.moveX, mining.moveY);
        mining.hullDirX = mining.aimDirX;
        mining.hullDirY = mining.aimDirY;
        refreshTargetCell(mining);
    }
}

void setMiningAim(GameState& state, double, double)
{
    MiningRunState& mining = state.run.mining;
    if (!mining.active) {
        return;
    }
    // Compatibility entry point: the drill is fixed to the rig's forward heading.
    refreshTargetCell(mining);
}

void setMiningDrilling(GameState& state, bool drilling)
{
    if (state.run.mining.active) {
        state.run.mining.drilling = drilling && state.run.mining.drillIntegrity > 0.0 && !state.run.mining.failurePending;
    }
}

void toggleMiningTether(GameState& state)
{
    MiningRunState& mining = state.run.mining;
    MiningArtifactObject& artifact = mining.artifact;
    if (!mining.active || !artifact.present || artifact.state == MiningArtifactState::Delivered || artifact.state == MiningArtifactState::Destroyed) {
        return;
    }
    if (artifact.tethered) {
        const double speed = std::sqrt(artifact.velocityX * artifact.velocityX + artifact.velocityY * artifact.velocityY);
        if (speed > tuning::mining::artifactDropDamageThreshold) {
            damageMiningArtifact(mining, (speed - tuning::mining::artifactDropDamageThreshold) * tuning::mining::artifactImpactDamageScale);
        }
        artifact.tethered = false;
        return;
    }

    const double dx = artifact.x - mining.droneX;
    const double dy = artifact.y - mining.droneY;
    const double distance = std::sqrt(dx * dx + dy * dy);
    const bool exposed = artifact.revealed || artifact.state == MiningArtifactState::Loose;
    if (exposed && distance <= tuning::mining::artifactTetherRangeCells) {
        artifact.tethered = true;
        artifact.revealed = true;
    }
}

void pulseMiningScanner(GameState& state, const ContentCatalog& catalog)
{
    MiningRunState& mining = state.run.mining;
    if (!mining.active) {
        return;
    }
    ensureMiningMiniDroneAgents(state, catalog);
    const double scannerRadius = miningDrillStats(state, catalog).scannerRadius;
    revealAround(mining, mining.droneX, mining.droneY, scannerRadius);
    for (const MiningMiniDroneAgent& agent : mining.miniDrones) {
        if (agent.role == MiniDroneRole::Survey) {
            revealAround(mining, agent.x, agent.y, scannerRadius);
        }
    }
    mining.scannerPulseSeconds = 0.9;
}

void updateMiningArtifact(GameState& state, double dt)
{
    MiningRunState& mining = state.run.mining;
    MiningArtifactObject& artifact = mining.artifact;
    if (!artifact.present || artifact.state == MiningArtifactState::Delivered || artifact.state == MiningArtifactState::Destroyed) {
        return;
    }

    const double dx = mining.droneX - artifact.x;
    const double dy = mining.droneY - artifact.y;
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

    if (artifact.tethered && distance > 0.001) {
        const double extension = std::max(0.0, distance - tuning::mining::artifactTetherRestLengthCells);
        const double pullX = dx / distance * extension * tuning::mining::artifactTetherSpring;
        const double pullY = dy / distance * extension * tuning::mining::artifactTetherSpring;
        artifact.velocityX += (pullX - artifact.velocityX * tuning::mining::artifactTetherDamping) * dt;
        artifact.velocityY += (pullY - artifact.velocityY * tuning::mining::artifactTetherDamping) * dt;
    } else {
        artifact.velocityX *= std::max(0.0, 1.0 - dt * 1.8);
        artifact.velocityY *= std::max(0.0, 1.0 - dt * 1.8);
    }

    auto solidAt = [&](double x, double y) {
        const int cellX = std::clamp(static_cast<int>(std::floor(x)), 0, mining.terrain.width - 1);
        const int cellY = std::clamp(static_cast<int>(std::floor(y)), 0, mining.terrain.height - 1);
        const MiningCell* cell = miningCellAt(mining.terrain, cellX, cellY);
        return cell != nullptr && miningMaterialSolid(cell->material);
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
    if (artifact.tethered && bayDx * bayDx + bayDy * bayDy <= tuning::mining::artifactDeliveryRadiusCells * tuning::mining::artifactDeliveryRadiusCells) {
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
    const MiningLoadStats loadStats = miningLoadStats(state, catalog);
    auto finishAtReturnZone = [&]() {
        if (!miningAtReturnZone(mining)) {
            return false;
        }
        const SurfaceActionOutcome outcome = finishMiningRun(state, catalog, false);
        if (!outcome.message.empty()) {
            state.statusLine = outcome.message;
        }
        return outcome.applied;
    };
    mining.elapsedSeconds += dt;
    mining.oxygenSeconds = std::max(0.0, mining.oxygenSeconds - dt);
    if (mining.oxygenSeconds <= 0.0) {
        if (finishAtReturnZone()) {
            return;
        }
        mining.droneHealth = std::max(0.0, mining.droneHealth - tuning::mining::oxygenDroneDamagePerSecond * dt);
        mining.contactIntensity = std::max(mining.contactIntensity, 0.45);
        if (!mining.oxygenDepletedNotified) {
            mining.oxygenDepletedNotified = true;
            state.statusLine = std::string(text::status::miningOxygenFailed);
        }
    }
    updateMiningMiniDroneAgents(state, catalog, stats, dt);
    applyMiningEnemyCombat(state, catalog, dt);
    mining.fuelCycleProgress +=
        dt * tuning::mining::fuelCycleProgressPerSecond * loadStats.fuelConsumptionMultiplier;
    if (mining.oxygenSeconds > 0.0) {
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

    const double moveLength = std::sqrt(mining.moveX * mining.moveX + mining.moveY * mining.moveY);
    if (moveLength > 0.01) {
        const double recovery = std::clamp(mining.contactSpeedRecovery, 0.0, 1.0);
        const double easedRecovery = recovery * recovery * (3.0 - 2.0 * recovery);
        const double contactSpeedScale = tuning::mining::postContactMinSpeedScale +
            (1.0 - tuning::mining::postContactMinSpeedScale) * easedRecovery;
        const double step = stats.speed * std::clamp(mining.movementSlowScale, 0.40, 1.0) *
            loadStats.speedMultiplier * contactSpeedScale * dt;
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
            if (mining.drilling && mining.drillIntegrity > 0.0 && obstacle->material != MiningCellMaterial::Bedrock) {
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
    const bool drillTouchesTerrain = mining.drilling && mining.drillIntegrity > 0.0 && !drillFootprintCells(mining, mining.aimDirX, mining.aimDirY).empty();
    if (drillTouchesTerrain) {
        applyDrillFootprintDamage(state, stats, mining.aimDirX, mining.aimDirY, dt);
    } else {
        const bool externalThermalLoad = std::any_of(mining.enemies.begin(), mining.enemies.end(), [&](const MiningEnemy& enemy) {
            if (!enemy.active || enemy.type != MiningEnemyType::Elemental || enemy.affinity != MiningElementalAffinity::Thermal) {
                return false;
            }
            const double dx = mining.droneX - enemy.x;
            const double dy = mining.droneY - enemy.y;
            return dx * dx + dy * dy <= enemy.effectRadius * enemy.effectRadius;
        });
        if (!externalThermalLoad) {
            mining.drillHeat = std::max(0.0, mining.drillHeat - stats.heatCoolingPerSecond * dt);
        }
    }
    mining.drillHeat = std::clamp(mining.drillHeat, 0.0, 1.0);
    mining.hazardDelta = std::clamp(mining.hazardDelta, 0.0, tuning::mining::maxMiningHazardDelta);
    updateMiningArtifact(state, dt);
    if (bankMiningPayloadAtShip(state, catalog)) {
        state.statusLine = std::string(text::status::miningStowed);
    } else if (!miningAtReturnZone(mining) && miningCarriedCargo(mining) > 0) {
        state.statusLine = std::string(text::status::miningReturnToShip);
    }
    refreshTargetCell(mining);

    if (mining.drillIntegrity <= 0.0 && !mining.drillBreakNotified) {
        mining.drillBreakNotified = true;
        mining.drilling = false;
        state.statusLine = std::string(text::status::miningDrillFailed);
    }
    if (mining.droneHealth <= 0.0) {
        if (finishAtReturnZone()) {
            return;
        }
        triggerMiningFailure(
            state,
            std::string("Drone health lost. Emergency recall fired; carried payload was lost."));
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

    if (!abort && !miningAtReturnZone(mining)) {
        outcome.message = std::string(text::status::miningReturnToShip);
        return outcome;
    }
    if (!abort) {
        bankMiningPayloadAtShip(state, catalog);
    }

    outcome.applied = true;
    outcome.message = abort
        ? (mining.failureMessage.empty() ? std::string(text::status::miningAborted) : mining.failureMessage)
        : std::string("Banked payload loaded for surface extraction.");
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
    expedition.temporaryArtifacts.insert(expedition.temporaryArtifacts.end(), mining.stowedArtifacts.begin(), mining.stowedArtifacts.end());
    expedition.cargo += mining.stowedCargo;
    expedition.depth = std::max(expedition.depth, mining.depthZone);
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
