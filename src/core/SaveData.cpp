#include "core/SaveData.h"
#include "core/ContentIds.h"
#include "core/GameText.h"
#include "core/MiningSystem.h"
#include "core/ResearchSystem.h"
#include "core/SaveSchema.h"
#include "core/Tuning.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace rocket {

namespace {

std::vector<std::string> split(std::string_view text, char delimiter)
{
    std::vector<std::string> values;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find(delimiter, start);
        const std::size_t count = end == std::string_view::npos ? text.size() - start : end - start;
        if (count > 0) {
            values.emplace_back(text.substr(start, count));
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return values;
}

std::string join(const std::vector<std::string>& values, char delimiter)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << delimiter;
        }
        out << values[i];
    }
    return out.str();
}

std::string joinInts(const std::vector<int>& values, char delimiter)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            out << delimiter;
        }
        out << values[i];
    }
    return out.str();
}

int statusToInt(CrewStatus status)
{
    switch (status) {
    case CrewStatus::Active:
        return 0;
    case CrewStatus::Injured:
        return 1;
    case CrewStatus::Dead:
        return 2;
    }
    return 0;
}

CrewStatus statusFromInt(int value)
{
    switch (value) {
    case 1:
        return CrewStatus::Injured;
    case 2:
        return CrewStatus::Dead;
    default:
        return CrewStatus::Active;
    }
}

int surfaceSiteProfileToInt(SurfaceSiteProfile profile)
{
    switch (profile) {
    case SurfaceSiteProfile::SurveyBasin:
        return 0;
    case SurfaceSiteProfile::OreShelf:
        return 1;
    case SurfaceSiteProfile::FractureField:
        return 2;
    }
    return 0;
}

SurfaceSiteProfile surfaceSiteProfileFromInt(int value)
{
    switch (value) {
    case 1:
        return SurfaceSiteProfile::OreShelf;
    case 2:
        return SurfaceSiteProfile::FractureField;
    default:
        return SurfaceSiteProfile::SurveyBasin;
    }
}

int miningMaterialToInt(MiningCellMaterial material)
{
    switch (material) {
    case MiningCellMaterial::Empty:
        return 0;
    case MiningCellMaterial::Regolith:
        return 1;
    case MiningCellMaterial::HardRock:
        return 2;
    case MiningCellMaterial::CommonOre:
        return 3;
    case MiningCellMaterial::RareOre:
        return 4;
    case MiningCellMaterial::ExoticVein:
        return 5;
    case MiningCellMaterial::ArtifactCache:
        return 6;
    case MiningCellMaterial::HazardPocket:
        return 7;
    case MiningCellMaterial::Bedrock:
        return 8;
    }
    return 0;
}

MiningCellMaterial miningMaterialFromInt(int value)
{
    switch (value) {
    case 1:
        return MiningCellMaterial::Regolith;
    case 2:
        return MiningCellMaterial::HardRock;
    case 3:
        return MiningCellMaterial::CommonOre;
    case 4:
        return MiningCellMaterial::RareOre;
    case 5:
        return MiningCellMaterial::ExoticVein;
    case 6:
        return MiningCellMaterial::ArtifactCache;
    case 7:
        return MiningCellMaterial::HazardPocket;
    case 8:
        return MiningCellMaterial::Bedrock;
    default:
        return MiningCellMaterial::Empty;
    }
}

int artifactKindToInt(ArtifactKind kind)
{
    return kind == ArtifactKind::Story ? 1 : 0;
}

ArtifactKind artifactKindFromInt(int value)
{
    return value == 1 ? ArtifactKind::Story : ArtifactKind::Boost;
}

int artifactRewardToInt(ArtifactRewardType reward)
{
    switch (reward) {
    case ArtifactRewardType::None:
        return 0;
    case ArtifactRewardType::Credits:
        return 1;
    case ArtifactRewardType::ArkFuel:
        return 2;
    case ArtifactRewardType::BlueprintInsight:
        return 3;
    }
    return 0;
}

ArtifactRewardType artifactRewardFromInt(int value)
{
    switch (value) {
    case 1:
        return ArtifactRewardType::Credits;
    case 2:
        return ArtifactRewardType::ArkFuel;
    case 3:
        return ArtifactRewardType::BlueprintInsight;
    default:
        return ArtifactRewardType::None;
    }
}

int miningArtifactStateToInt(MiningArtifactState state)
{
    switch (state) {
    case MiningArtifactState::None:
        return 0;
    case MiningArtifactState::Embedded:
        return 1;
    case MiningArtifactState::Loose:
        return 2;
    case MiningArtifactState::Delivered:
        return 3;
    case MiningArtifactState::Destroyed:
        return 4;
    }
    return 0;
}

MiningArtifactState miningArtifactStateFromInt(int value)
{
    switch (value) {
    case 1:
        return MiningArtifactState::Embedded;
    case 2:
        return MiningArtifactState::Loose;
    case 3:
        return MiningArtifactState::Delivered;
    case 4:
        return MiningArtifactState::Destroyed;
    default:
        return MiningArtifactState::None;
    }
}

int miningCellFeatureToInt(MiningCellFeature feature)
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
    case MiningCellFeature::MinibossLair:
        return 5;
    case MiningCellFeature::HiveNest:
        return 6;
    case MiningCellFeature::OrganicBurrow:
        return 7;
    case MiningCellFeature::BossChamber:
        return 8;
    }
    return 0;
}

MiningCellFeature miningCellFeatureFromInt(int value)
{
    switch (value) {
    case 1:
        return MiningCellFeature::MainTunnel;
    case 2:
        return MiningCellFeature::BranchTunnel;
    case 3:
        return MiningCellFeature::EncounterZone;
    case 4:
        return MiningCellFeature::TreasureVault;
    case 5:
        return MiningCellFeature::MinibossLair;
    case 6:
        return MiningCellFeature::HiveNest;
    case 7:
        return MiningCellFeature::OrganicBurrow;
    case 8:
        return MiningCellFeature::BossChamber;
    default:
        return MiningCellFeature::None;
    }
}

int miningEnemyTypeToInt(MiningEnemyType enemy)
{
    switch (enemy) {
    case MiningEnemyType::None:
        return 0;
    case MiningEnemyType::Ant:
        return 1;
    case MiningEnemyType::Flying:
        return 2;
    case MiningEnemyType::Beetle:
        return 3;
    case MiningEnemyType::Elemental:
        return 4;
    case MiningEnemyType::Mammal:
        return 5;
    case MiningEnemyType::Spawner:
        return 6;
    }
    return 0;
}

MiningEnemyType miningEnemyTypeFromInt(int value)
{
    switch (value) {
    case 1:
        return MiningEnemyType::Ant;
    case 2:
        return MiningEnemyType::Flying;
    case 3:
        return MiningEnemyType::Beetle;
    case 4:
        return MiningEnemyType::Elemental;
    case 5:
        return MiningEnemyType::Mammal;
    case 6:
        return MiningEnemyType::Spawner;
    default:
        return MiningEnemyType::None;
    }
}

int miningElementalAffinityToInt(MiningElementalAffinity affinity)
{
    switch (affinity) {
    case MiningElementalAffinity::None:
        return 0;
    case MiningElementalAffinity::Thermal:
        return 1;
    case MiningElementalAffinity::Cryo:
        return 2;
    case MiningElementalAffinity::Radiation:
        return 3;
    case MiningElementalAffinity::Toxic:
        return 4;
    }
    return 0;
}

MiningElementalAffinity miningElementalAffinityFromInt(int value)
{
    switch (value) {
    case 1:
        return MiningElementalAffinity::Thermal;
    case 2:
        return MiningElementalAffinity::Cryo;
    case 3:
        return MiningElementalAffinity::Radiation;
    case 4:
        return MiningElementalAffinity::Toxic;
    default:
        return MiningElementalAffinity::None;
    }
}

int screenToInt(Screen screen)
{
    switch (screen) {
    case Screen::ArrivalOps:
        return 2;
    case Screen::Research:
        return 3;
    case Screen::SurfaceExpedition:
    case Screen::SurfaceScan:
    case Screen::SurfacePush:
        return 4;
    case Screen::SurfaceUpgrade:
        return 5;
    case Screen::Mining:
        return 6;
    case Screen::DroneOps:
        return 7;
    case Screen::Navigation:
        return 8;
    default:
        return 0;
    }
}

Screen screenFromInt(int value)
{
    switch (value) {
    case 2:
        return Screen::ArrivalOps;
    case 3:
        return Screen::Research;
    case 4:
        return Screen::SurfaceExpedition;
    case 5:
        return Screen::SurfaceUpgrade;
    case 6:
        return Screen::Mining;
    case 7:
        return Screen::DroneOps;
    case 8:
        return Screen::Navigation;
    default:
        return Screen::Hangar;
    }
}

int campaignMilestoneToInt(CampaignMilestone milestone)
{
    switch (milestone) {
    case CampaignMilestone::SolarTutorial:
        return 0;
    case CampaignMilestone::ArkDiscovered:
        return 1;
    case CampaignMilestone::FirstArkJumpReady:
        return 2;
    case CampaignMilestone::FirstArkJumpComplete:
        return 3;
    case CampaignMilestone::GravityWellDisaster:
        return 4;
    case CampaignMilestone::HostileSystemStranded:
        return 5;
    case CampaignMilestone::ArkRepairing:
        return 6;
    }
    return 0;
}

CampaignMilestone campaignMilestoneFromInt(int value)
{
    switch (value) {
    case 1:
        return CampaignMilestone::ArkDiscovered;
    case 2:
        return CampaignMilestone::FirstArkJumpReady;
    case 3:
        return CampaignMilestone::FirstArkJumpComplete;
    case 4:
        return CampaignMilestone::GravityWellDisaster;
    case 5:
        return CampaignMilestone::HostileSystemStranded;
    case 6:
        return CampaignMilestone::ArkRepairing;
    default:
        return CampaignMilestone::SolarTutorial;
    }
}

int gameChapterToInt(GameChapter chapter)
{
    return chapterNumber(chapter);
}

GameChapter gameChapterFromInt(int value)
{
    switch (value) {
    case 2:
        return GameChapter::LunarProgram;
    case 3:
        return GameChapter::RedFrontier;
    case 4:
        return GameChapter::Breakthrough;
    case 5:
        return GameChapter::Straylight;
    case 6:
        return GameChapter::Arkfall;
    case 7:
        return GameChapter::LastCampfire;
    case 8:
        return GameChapter::VoidCompass;
    case 9:
        return GameChapter::Ouroboros;
    case 10:
        return GameChapter::Ascent;
    default:
        return GameChapter::ProvingGround;
    }
}

int arkConditionToInt(ArkCondition condition)
{
    switch (condition) {
    case ArkCondition::NotFound:
        return 0;
    case ArkCondition::DerelictOperable:
        return 1;
    case ArkCondition::InFlight:
        return 2;
    case ArkCondition::DamagedStranded:
        return 3;
    case ArkCondition::Repairing:
        return 4;
    }
    return 0;
}

ArkCondition arkConditionFromInt(int value)
{
    switch (value) {
    case 1:
        return ArkCondition::DerelictOperable;
    case 2:
        return ArkCondition::InFlight;
    case 3:
        return ArkCondition::DamagedStranded;
    case 4:
        return ArkCondition::Repairing;
    default:
        return ArkCondition::NotFound;
    }
}

int parseInt(std::string_view text, int fallback)
{
    int value = fallback;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

std::uint64_t parseU64(std::string_view text, std::uint64_t fallback)
{
    std::uint64_t value = fallback;
    std::from_chars(text.data(), text.data() + text.size(), value);
    return value;
}

double parseDouble(std::string_view text, double fallback)
{
    std::string copy(text);
    char* end = nullptr;
    const double value = std::strtod(copy.c_str(), &end);
    return end == copy.c_str() ? fallback : value;
}

template <typename Value>
void writeField(std::ostringstream& out, std::string_view key, const Value& value)
{
    out << key << save_schema::keyValueDelimiter << value << "\n";
}

std::string serializeCrew(const std::vector<Astronaut>& crew)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < crew.size(); ++i) {
        if (i > 0) {
            out << save_schema::crewRecordDelimiter;
        }
        out << crew[i].id
            << save_schema::crewFieldDelimiter << crew[i].training
            << save_schema::crewFieldDelimiter << crew[i].stress
            << save_schema::crewFieldDelimiter << statusToInt(crew[i].status);
    }
    return out.str();
}

std::vector<Astronaut> parseCrew(std::string_view text)
{
    std::vector<Astronaut> crew;
    for (const std::string& record : split(text, save_schema::crewRecordDelimiter)) {
        const std::vector<std::string> fields = split(record, save_schema::crewFieldDelimiter);
        if (fields.empty()) {
            continue;
        }

        Astronaut astronaut;
        astronaut.id = fields[0];
        if (fields.size() > 1) {
            astronaut.training = parseInt(fields[1], 0);
        }
        if (fields.size() > 2) {
            astronaut.stress = parseInt(fields[2], 0);
        }
        if (fields.size() > 3) {
            astronaut.status = statusFromInt(parseInt(fields[3], 0));
        }
        crew.push_back(astronaut);
    }
    return crew;
}

std::string serializeMaterials(const MaterialInventory& materials)
{
    std::ostringstream out;
    out << materials.common
        << save_schema::crewFieldDelimiter << materials.rare
        << save_schema::crewFieldDelimiter << materials.exotic;
    return out.str();
}

MaterialInventory parseMaterials(std::string_view text)
{
    MaterialInventory materials;
    const std::vector<std::string> fields = split(text, save_schema::crewFieldDelimiter);
    if (!fields.empty()) {
        materials.common = parseInt(fields[0], 0);
    }
    if (fields.size() > 1) {
        materials.rare = parseInt(fields[1], 0);
    }
    if (fields.size() > 2) {
        materials.exotic = parseInt(fields[2], 0);
    }
    return materials;
}

std::string serializeDepthProspects(const std::vector<SurfaceDepthProspect>& prospects)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < prospects.size(); ++i) {
        if (i > 0) {
            out << save_schema::listDelimiter;
        }
        const SurfaceDepthProspect& prospect = prospects[i];
        out << prospect.depthOffset
            << save_schema::crewFieldDelimiter << prospect.absoluteDepth
            << save_schema::crewFieldDelimiter << prospect.possibleMaterials.common
            << save_schema::crewFieldDelimiter << prospect.possibleMaterials.rare
            << save_schema::crewFieldDelimiter << prospect.possibleMaterials.exotic
            << save_schema::crewFieldDelimiter << prospect.possibleArtifacts;
    }
    return out.str();
}

std::vector<SurfaceDepthProspect> parseDepthProspects(std::string_view text)
{
    std::vector<SurfaceDepthProspect> prospects;
    for (const std::string& record : split(text, save_schema::listDelimiter)) {
        const std::vector<std::string> fields = split(record, save_schema::crewFieldDelimiter);
        if (fields.size() < 2) {
            continue;
        }
        SurfaceDepthProspect prospect;
        prospect.depthOffset = std::max(0, parseInt(fields[0], prospect.depthOffset));
        prospect.absoluteDepth = std::max(0, parseInt(fields[1], prospect.absoluteDepth));
        if (fields.size() > 2) {
            prospect.possibleMaterials.common = std::max(0, parseInt(fields[2], 0));
        }
        if (fields.size() > 3) {
            prospect.possibleMaterials.rare = std::max(0, parseInt(fields[3], 0));
        }
        if (fields.size() > 4) {
            prospect.possibleMaterials.exotic = std::max(0, parseInt(fields[4], 0));
        }
        if (fields.size() > 5) {
            prospect.possibleArtifacts = std::max(0, parseInt(fields[5], 0));
        }
        prospects.push_back(prospect);
    }
    return prospects;
}

std::string serializePair(double x, double y)
{
    std::ostringstream out;
    out << x << save_schema::crewFieldDelimiter << y;
    return out.str();
}

void parsePair(std::string_view text, double& x, double& y)
{
    const std::vector<std::string> fields = split(text, save_schema::crewFieldDelimiter);
    if (!fields.empty()) {
        x = parseDouble(fields[0], x);
    }
    if (fields.size() > 1) {
        y = parseDouble(fields[1], y);
    }
}

std::string serializeMiningTerrainSize(const MiningTerrain& terrain)
{
    std::ostringstream out;
    out << terrain.width
        << save_schema::crewFieldDelimiter << terrain.height
        << save_schema::crewFieldDelimiter << terrain.depthZone;
    return out.str();
}

void parseMiningTerrainSize(std::string_view text, MiningTerrain& terrain)
{
    const std::vector<std::string> fields = split(text, save_schema::crewFieldDelimiter);
    if (!fields.empty()) {
        terrain.width = std::max(1, parseInt(fields[0], terrain.width));
    }
    if (fields.size() > 1) {
        terrain.height = std::max(1, parseInt(fields[1], terrain.height));
    }
    if (fields.size() > 2) {
        terrain.depthZone = std::max(0, parseInt(fields[2], terrain.depthZone));
    }
}

std::string serializeMiningCells(const MiningTerrain& terrain)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < terrain.cells.size(); ++i) {
        if (i > 0) {
            out << save_schema::listDelimiter;
        }
        const MiningCell& cell = terrain.cells[i];
        out << miningMaterialToInt(cell.material)
            << save_schema::crewFieldDelimiter << cell.maxToughness
            << save_schema::crewFieldDelimiter << cell.remainingToughness
            << save_schema::crewFieldDelimiter << (cell.revealed ? 1 : 0)
            << save_schema::crewFieldDelimiter << (cell.hazard ? 1 : 0)
            << save_schema::crewFieldDelimiter << miningCellFeatureToInt(cell.feature)
            << save_schema::crewFieldDelimiter << miningEnemyTypeToInt(cell.enemy)
            << save_schema::crewFieldDelimiter << miningElementalAffinityToInt(cell.hazardAffinity);
    }
    return out.str();
}

void parseMiningCells(std::string_view text, MiningTerrain& terrain)
{
    terrain.cells.clear();
    terrain.cells.reserve(static_cast<std::size_t>(terrain.width * terrain.height));
    for (const std::string& record : split(text, save_schema::listDelimiter)) {
        const std::vector<std::string> fields = split(record, save_schema::crewFieldDelimiter);
        if (fields.empty()) {
            continue;
        }
        MiningCell cell;
        cell.material = miningMaterialFromInt(parseInt(fields[0], 0));
        if (fields.size() > 1) {
            cell.maxToughness = parseDouble(fields[1], cell.maxToughness);
        }
        if (fields.size() > 2) {
            cell.remainingToughness = parseDouble(fields[2], cell.remainingToughness);
        }
        if (fields.size() > 3) {
            cell.revealed = parseInt(fields[3], 0) != 0;
        }
        if (fields.size() > 4) {
            cell.hazard = parseInt(fields[4], 0) != 0;
        }
        if (fields.size() > 5) {
            cell.feature = miningCellFeatureFromInt(parseInt(fields[5], 0));
        }
        if (fields.size() > 6) {
            cell.enemy = miningEnemyTypeFromInt(parseInt(fields[6], 0));
        }
        if (fields.size() > 7) {
            cell.hazardAffinity = miningElementalAffinityFromInt(parseInt(fields[7], 0));
        }
        terrain.cells.push_back(cell);
    }
    const int chunksX = (terrain.width + tuning::mining::chunkSize - 1) / tuning::mining::chunkSize;
    const int chunksY = (terrain.height + tuning::mining::chunkSize - 1) / tuning::mining::chunkSize;
    terrain.dirtyChunks.assign(static_cast<std::size_t>(std::max(1, chunksX * chunksY)), 1);
}

std::string serializeDroneUpgrades(const std::vector<DroneUpgradeRecord>& upgrades)
{
    std::ostringstream out;
    bool first = true;
    for (const DroneUpgradeRecord& upgrade : upgrades) {
        if (upgrade.droneId.empty()) {
            continue;
        }
        if (!first) {
            out << save_schema::crewRecordDelimiter;
        }
        first = false;
        out << upgrade.droneId << save_schema::crewFieldDelimiter << std::clamp(upgrade.level, 1, 3);
    }
    return out.str();
}

std::vector<DroneUpgradeRecord> parseDroneUpgrades(std::string_view text)
{
    std::vector<DroneUpgradeRecord> upgrades;
    for (const std::string& record : split(text, save_schema::crewRecordDelimiter)) {
        if (record.empty()) {
            continue;
        }
        const std::vector<std::string> fields = split(record, save_schema::crewFieldDelimiter);
        if (fields.empty() || fields[0].empty()) {
            continue;
        }
        DroneUpgradeRecord upgrade;
        upgrade.droneId = fields[0];
        if (fields.size() > 1) {
            upgrade.level = std::clamp(parseInt(fields[1], 1), 1, 3);
        }
        upgrades.push_back(std::move(upgrade));
    }
    return upgrades;
}

std::string serializeMiningEnemies(const std::vector<MiningEnemy>& enemies)
{
    std::ostringstream out;
    bool first = true;
    for (const MiningEnemy& enemy : enemies) {
        if (!first) {
            out << save_schema::listDelimiter;
        }
        first = false;
        out << miningEnemyTypeToInt(enemy.type)
            << save_schema::crewFieldDelimiter << miningCellFeatureToInt(enemy.sourceFeature)
            << save_schema::crewFieldDelimiter << enemy.x
            << save_schema::crewFieldDelimiter << enemy.y
            << save_schema::crewFieldDelimiter << enemy.velocityX
            << save_schema::crewFieldDelimiter << enemy.velocityY
            << save_schema::crewFieldDelimiter << enemy.health
            << save_schema::crewFieldDelimiter << enemy.maxHealth
            << save_schema::crewFieldDelimiter << enemy.armor
            << save_schema::crewFieldDelimiter << enemy.speed
            << save_schema::crewFieldDelimiter << enemy.damagePerSecond
            << save_schema::crewFieldDelimiter << enemy.effectRadius
            << save_schema::crewFieldDelimiter << (enemy.active ? 1 : 0)
            << save_schema::crewFieldDelimiter << miningElementalAffinityToInt(enemy.affinity)
            << save_schema::crewFieldDelimiter << enemy.attackCooldownSeconds
            << save_schema::crewFieldDelimiter << miningEnemyTypeToInt(enemy.spawn.enemyType)
            << save_schema::crewFieldDelimiter << miningElementalAffinityToInt(enemy.spawn.affinity)
            << save_schema::crewFieldDelimiter << enemy.spawn.maxSpawns
            << save_schema::crewFieldDelimiter << enemy.spawn.spawned
            << save_schema::crewFieldDelimiter << enemy.spawn.intervalSeconds
            << save_schema::crewFieldDelimiter << enemy.spawn.cooldownSeconds;
    }
    return out.str();
}

std::vector<MiningEnemy> parseMiningEnemies(std::string_view text)
{
    std::vector<MiningEnemy> enemies;
    for (const std::string& record : split(text, save_schema::listDelimiter)) {
        if (record.empty()) {
            continue;
        }
        const std::vector<std::string> fields = split(record, save_schema::crewFieldDelimiter);
        if (fields.empty()) {
            continue;
        }
        MiningEnemy enemy;
        enemy.type = miningEnemyTypeFromInt(parseInt(fields[0], 0));
        if (fields.size() > 1) {
            enemy.sourceFeature = miningCellFeatureFromInt(parseInt(fields[1], 0));
        }
        if (fields.size() > 2) {
            enemy.x = parseDouble(fields[2], enemy.x);
        }
        if (fields.size() > 3) {
            enemy.y = parseDouble(fields[3], enemy.y);
        }
        if (fields.size() > 4) {
            enemy.velocityX = parseDouble(fields[4], enemy.velocityX);
        }
        if (fields.size() > 5) {
            enemy.velocityY = parseDouble(fields[5], enemy.velocityY);
        }
        if (fields.size() > 6) {
            enemy.health = parseDouble(fields[6], enemy.health);
        }
        if (fields.size() > 7) {
            enemy.maxHealth = parseDouble(fields[7], enemy.maxHealth);
        }
        if (fields.size() > 8) {
            enemy.armor = parseDouble(fields[8], enemy.armor);
        }
        if (fields.size() > 9) {
            enemy.speed = parseDouble(fields[9], enemy.speed);
        }
        if (fields.size() > 10) {
            enemy.damagePerSecond = parseDouble(fields[10], enemy.damagePerSecond);
        }
        if (fields.size() > 11) {
            enemy.effectRadius = parseDouble(fields[11], enemy.effectRadius);
        }
        if (fields.size() > 12) {
            enemy.active = parseInt(fields[12], 1) != 0;
        }
        if (fields.size() > 13) {
            enemy.affinity = miningElementalAffinityFromInt(parseInt(fields[13], 0));
        }
        if (fields.size() > 14) {
            enemy.attackCooldownSeconds = parseDouble(fields[14], enemy.attackCooldownSeconds);
        }
        if (fields.size() > 15) {
            enemy.spawn.enemyType = miningEnemyTypeFromInt(parseInt(fields[15], 0));
        }
        if (fields.size() > 16) {
            enemy.spawn.affinity = miningElementalAffinityFromInt(parseInt(fields[16], 0));
        }
        if (fields.size() > 17) {
            enemy.spawn.maxSpawns = std::max(0, parseInt(fields[17], 0));
        }
        if (fields.size() > 18) {
            enemy.spawn.spawned = std::clamp(parseInt(fields[18], 0), 0, enemy.spawn.maxSpawns);
        }
        if (fields.size() > 19) {
            enemy.spawn.intervalSeconds = std::max(0.0, parseDouble(fields[19], 0.0));
        }
        if (fields.size() > 20) {
            enemy.spawn.cooldownSeconds = std::max(0.0, parseDouble(fields[20], 0.0));
        }
        enemies.push_back(enemy);
    }
    return enemies;
}

std::string serializeMiningMiniDrones(const std::vector<MiningMiniDroneAgent>& agents)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < agents.size(); ++i) {
        if (i > 0) {
            out << save_schema::listDelimiter;
        }
        const MiningMiniDroneAgent& agent = agents[i];
        out << static_cast<int>(agent.role)
            << save_schema::crewFieldDelimiter << agent.roleIndex
            << save_schema::crewFieldDelimiter << agent.upgradeLevel
            << save_schema::crewFieldDelimiter << agent.x
            << save_schema::crewFieldDelimiter << agent.y
            << save_schema::crewFieldDelimiter << agent.velocityX
            << save_schema::crewFieldDelimiter << agent.velocityY
            << save_schema::crewFieldDelimiter << static_cast<int>(agent.behavior)
            << save_schema::crewFieldDelimiter << agent.targetCellX
            << save_schema::crewFieldDelimiter << agent.targetCellY
            << save_schema::crewFieldDelimiter << agent.targetEnemyIndex
            << save_schema::crewFieldDelimiter << agent.actionCooldownSeconds
            << save_schema::crewFieldDelimiter << (agent.finishTargetBeforeReturn ? 1 : 0)
            << save_schema::crewFieldDelimiter << agent.surveyPulseSeconds
            << save_schema::crewFieldDelimiter << agent.haulMaterials.common
            << save_schema::crewFieldDelimiter << agent.haulMaterials.rare
            << save_schema::crewFieldDelimiter << agent.haulMaterials.exotic
            << save_schema::crewFieldDelimiter << agent.taskProgressSeconds
            << save_schema::crewFieldDelimiter << agent.defenseAngleRadians
            << save_schema::crewFieldDelimiter << (agent.defenseAngleInitialized ? 1 : 0)
            << save_schema::crewFieldDelimiter << agent.shieldCharge
            << save_schema::crewFieldDelimiter << agent.shieldRechargeSeconds
            << save_schema::crewFieldDelimiter << agent.shieldImpactSeconds;
    }
    return out.str();
}

std::vector<MiningMiniDroneAgent> parseMiningMiniDrones(std::string_view text)
{
    std::vector<MiningMiniDroneAgent> agents;
    for (const std::string& record : split(text, save_schema::listDelimiter)) {
        if (record.empty()) {
            continue;
        }
        const std::vector<std::string> fields = split(record, save_schema::crewFieldDelimiter);
        if (fields.size() < 8) {
            continue;
        }
        MiningMiniDroneAgent agent;
        agent.role = static_cast<MiniDroneRole>(std::clamp(
            parseInt(fields[0], static_cast<int>(MiniDroneRole::Mining)),
            static_cast<int>(MiniDroneRole::Mining),
            static_cast<int>(MiniDroneRole::Defense)));
        agent.roleIndex = std::max(0, parseInt(fields[1], agent.roleIndex));
        agent.upgradeLevel = std::clamp(parseInt(fields[2], agent.upgradeLevel), 1, 3);
        agent.x = parseDouble(fields[3], agent.x);
        agent.y = parseDouble(fields[4], agent.y);
        agent.velocityX = parseDouble(fields[5], agent.velocityX);
        agent.velocityY = parseDouble(fields[6], agent.velocityY);
        agent.behavior = static_cast<MiningMiniDroneBehavior>(std::clamp(
            parseInt(fields[7], static_cast<int>(MiningMiniDroneBehavior::Following)),
            static_cast<int>(MiningMiniDroneBehavior::Following),
            static_cast<int>(MiningMiniDroneBehavior::Docked)));
        if (fields.size() > 8) {
            agent.targetCellX = parseInt(fields[8], agent.targetCellX);
        }
        if (fields.size() > 9) {
            agent.targetCellY = parseInt(fields[9], agent.targetCellY);
        }
        if (fields.size() > 10) {
            agent.targetEnemyIndex = parseInt(fields[10], agent.targetEnemyIndex);
        }
        if (fields.size() > 11) {
            agent.actionCooldownSeconds = std::max(0.0, parseDouble(fields[11], agent.actionCooldownSeconds));
        }
        if (fields.size() > 12) {
            agent.finishTargetBeforeReturn = parseInt(fields[12], 0) != 0;
        }
        if (fields.size() > 13) {
            agent.surveyPulseSeconds = std::max(0.0, parseDouble(fields[13], agent.surveyPulseSeconds));
        }
        if (fields.size() > 14) {
            agent.haulMaterials.common = std::max(0, parseInt(fields[14], agent.haulMaterials.common));
        }
        if (fields.size() > 15) {
            agent.haulMaterials.rare = std::max(0, parseInt(fields[15], agent.haulMaterials.rare));
        }
        if (fields.size() > 16) {
            agent.haulMaterials.exotic = std::max(0, parseInt(fields[16], agent.haulMaterials.exotic));
        }
        if (fields.size() > 17) {
            agent.taskProgressSeconds = std::max(0.0, parseDouble(fields[17], agent.taskProgressSeconds));
        }
        if (fields.size() > 18) {
            agent.defenseAngleRadians = parseDouble(fields[18], agent.defenseAngleRadians);
        }
        if (fields.size() > 19) {
            agent.defenseAngleInitialized = parseInt(fields[19], 0) != 0;
        }
        if (fields.size() > 20) {
            agent.shieldCharge = std::clamp(parseDouble(fields[20], agent.shieldCharge), 0.0, 1.0);
        }
        if (fields.size() > 21) {
            agent.shieldRechargeSeconds = std::max(0.0, parseDouble(fields[21], agent.shieldRechargeSeconds));
        }
        if (fields.size() > 22) {
            agent.shieldImpactSeconds = std::max(0.0, parseDouble(fields[22], agent.shieldImpactSeconds));
        }
        agents.push_back(agent);
    }
    return agents;
}

std::string serializeArtifacts(const std::vector<ArtifactRecord>& artifacts)
{
    std::ostringstream out;
    for (std::size_t i = 0; i < artifacts.size(); ++i) {
        if (i > 0) {
            out << save_schema::artifactRecordDelimiter;
        }
        out << artifacts[i].id
            << save_schema::artifactFieldDelimiter << artifacts[i].originDestinationId
            << save_schema::artifactFieldDelimiter << (artifacts[i].identified ? 1 : 0)
            << save_schema::artifactFieldDelimiter << artifactKindToInt(artifacts[i].kind)
            << save_schema::artifactFieldDelimiter << artifactRewardToInt(artifacts[i].rewardType)
            << save_schema::artifactFieldDelimiter << artifacts[i].condition
            << save_schema::artifactFieldDelimiter << (artifacts[i].rewardApplied ? 1 : 0);
    }
    return out.str();
}

std::vector<ArtifactRecord> parseArtifacts(std::string_view text)
{
    std::vector<ArtifactRecord> artifacts;
    for (const std::string& record : split(text, save_schema::artifactRecordDelimiter)) {
        const std::vector<std::string> fields = split(record, save_schema::artifactFieldDelimiter);
        if (fields.size() < 2) {
            continue;
        }

        ArtifactRecord artifact;
        artifact.id = fields[0];
        artifact.originDestinationId = fields[1];
        if (fields.size() > 2) {
            artifact.identified = parseInt(fields[2], 0) != 0;
        }
        if (fields.size() > 3) {
            artifact.kind = artifactKindFromInt(parseInt(fields[3], artifactKindToInt(artifact.kind)));
        }
        if (fields.size() > 4) {
            artifact.rewardType = artifactRewardFromInt(parseInt(fields[4], artifactRewardToInt(artifact.rewardType)));
        }
        if (fields.size() > 5) {
            artifact.condition = std::clamp(parseDouble(fields[5], artifact.condition), 0.0, 1.0);
        }
        if (fields.size() > 6) {
            artifact.rewardApplied = parseInt(fields[6], artifact.rewardApplied ? 1 : 0) != 0;
        }
        artifacts.push_back(artifact);
    }
    return artifacts;
}

std::string serializeMiningArtifact(const MiningArtifactObject& artifact)
{
    if (!artifact.present) {
        return "";
    }
    std::ostringstream out;
    out << artifact.id
        << save_schema::crewFieldDelimiter << artifactKindToInt(artifact.kind)
        << save_schema::crewFieldDelimiter << artifactRewardToInt(artifact.rewardType)
        << save_schema::crewFieldDelimiter << miningArtifactStateToInt(artifact.state)
        << save_schema::crewFieldDelimiter << artifact.x
        << save_schema::crewFieldDelimiter << artifact.y
        << save_schema::crewFieldDelimiter << artifact.velocityX
        << save_schema::crewFieldDelimiter << artifact.velocityY
        << save_schema::crewFieldDelimiter << artifact.health
        << save_schema::crewFieldDelimiter << artifact.maxHealth
        << save_schema::crewFieldDelimiter << artifact.embedStrength
        << save_schema::crewFieldDelimiter << (artifact.tethered ? 1 : 0)
        << save_schema::crewFieldDelimiter << (artifact.revealed ? 1 : 0);
    return out.str();
}

MiningArtifactObject parseMiningArtifact(std::string_view text)
{
    MiningArtifactObject artifact;
    const std::vector<std::string> fields = split(text, save_schema::crewFieldDelimiter);
    if (fields.size() < 4 || fields[0].empty()) {
        return artifact;
    }
    artifact.present = true;
    artifact.id = fields[0];
    artifact.kind = artifactKindFromInt(parseInt(fields[1], 0));
    artifact.rewardType = artifactRewardFromInt(parseInt(fields[2], 0));
    artifact.state = miningArtifactStateFromInt(parseInt(fields[3], 0));
    if (fields.size() > 4) {
        artifact.x = parseDouble(fields[4], artifact.x);
    }
    if (fields.size() > 5) {
        artifact.y = parseDouble(fields[5], artifact.y);
    }
    if (fields.size() > 6) {
        artifact.velocityX = parseDouble(fields[6], artifact.velocityX);
    }
    if (fields.size() > 7) {
        artifact.velocityY = parseDouble(fields[7], artifact.velocityY);
    }
    if (fields.size() > 8) {
        artifact.health = parseDouble(fields[8], artifact.health);
    }
    if (fields.size() > 9) {
        artifact.maxHealth = std::max(0.001, parseDouble(fields[9], artifact.maxHealth));
    }
    if (fields.size() > 10) {
        artifact.embedStrength = std::clamp(parseDouble(fields[10], artifact.embedStrength), 0.0, 1.0);
    }
    if (fields.size() > 11) {
        artifact.tethered = parseInt(fields[11], 0) != 0;
    }
    if (fields.size() > 12) {
        artifact.revealed = parseInt(fields[12], 0) != 0;
    }
    artifact.health = std::clamp(artifact.health, 0.0, artifact.maxHealth);
    if (artifact.state == MiningArtifactState::None) {
        artifact.present = false;
    }
    return artifact;
}

void normalizeLegacyMiningArtifact(MiningRunState& mining)
{
    if (!mining.active) {
        return;
    }
    bool foundArtifactTile = mining.artifact.present;
    for (int y = 0; y < mining.terrain.height; ++y) {
        for (int x = 0; x < mining.terrain.width; ++x) {
            const std::size_t index = static_cast<std::size_t>(y * mining.terrain.width + x);
            if (index >= mining.terrain.cells.size()) {
                continue;
            }
            MiningCell& cell = mining.terrain.cells[index];
            if (cell.material != MiningCellMaterial::ArtifactCache) {
                continue;
            }
            if (!foundArtifactTile) {
                mining.artifact = {};
                mining.artifact.present = true;
                mining.artifact.id = mining.destinationId + "_mining_artifact_" + std::to_string(mining.depthZone) + "_legacy";
                mining.artifact.kind = ArtifactKind::Boost;
                mining.artifact.rewardType = ArtifactRewardType::BlueprintInsight;
                mining.artifact.state = MiningArtifactState::Embedded;
                mining.artifact.x = static_cast<double>(x) + 0.5;
                mining.artifact.y = static_cast<double>(y) + 0.5;
                mining.artifact.health = tuning::mining::artifactMaxHealth;
                mining.artifact.maxHealth = tuning::mining::artifactMaxHealth;
                mining.artifact.embedStrength = 1.0;
                mining.artifact.revealed = cell.revealed;
                foundArtifactTile = true;
                continue;
            }
            cell.material = mining.depthZone >= 2 ? MiningCellMaterial::ExoticVein : MiningCellMaterial::RareOre;
            cell.hazard = false;
        }
    }
}

void normalizeLegacyMiningReturnZone(MiningRunState& mining)
{
    if (!mining.active) {
        return;
    }
    const bool invalidZone = mining.returnZoneX <= 0.0 ||
        mining.returnZoneY <= 0.0 ||
        mining.returnZoneX >= static_cast<double>(std::max(1, mining.terrain.width)) ||
        mining.returnZoneY >= static_cast<double>(std::max(1, mining.terrain.height));
    if (invalidZone) {
        mining.returnZoneX = static_cast<double>(std::max(1, mining.terrain.width)) * 0.5;
        mining.returnZoneY = 4.0;
    }
    mining.droneHealth = std::clamp(mining.droneHealth, 0.0, 1.0);
    mining.drillIntegrity = std::clamp(mining.drillIntegrity, 0.0, 1.0);
    mining.stowedCargo = std::max(0, mining.stowedCargo);
    mining.cargo = std::max(0, mining.cargo);
}

MiningElementalAffinity legacyHazardAffinity(const MiningRunState& mining, int x)
{
    if (mining.destinationId == content::destination::moon ||
        mining.destinationId == content::destination::outerPlanets) {
        return MiningElementalAffinity::Cryo;
    }
    if (mining.destinationId == content::destination::mars) {
        return MiningElementalAffinity::Thermal;
    }
    if (mining.destinationId == content::destination::nearbyStar) {
        if (mining.siteProfile == SurfaceSiteProfile::FractureField) {
            return MiningElementalAffinity::Toxic;
        }
        return mining.depthZone <= 0
            ? MiningElementalAffinity::Thermal
            : MiningElementalAffinity::Radiation;
    }
    if (mining.destinationId == content::destination::nearbyGalaxy) {
        return (x / 4) % 2 == 0
            ? MiningElementalAffinity::Radiation
            : MiningElementalAffinity::Toxic;
    }
    return MiningElementalAffinity::Thermal;
}

void normalizeLegacyMiningHazards(MiningRunState& mining)
{
    for (int y = 0; y < mining.terrain.height; ++y) {
        for (int x = 0; x < mining.terrain.width; ++x) {
            MiningCell* cell = miningCellAt(mining.terrain, x, y);
            if (cell == nullptr || cell->material != MiningCellMaterial::HazardPocket) {
                continue;
            }
            cell->hazard = true;
            if (cell->hazardAffinity == MiningElementalAffinity::None) {
                cell->hazardAffinity = legacyHazardAffinity(mining, x);
            }
        }
    }
}

void migrateLegacyHazardDroneRecords(MetaProgress& meta)
{
    auto replaceId = [](std::string& id) {
        if (id == content::drone::legacyStabilizerDrone) {
            id = content::drone::hazardDrone;
        }
    };
    for (std::string& id : meta.ownedDroneIds) {
        replaceId(id);
    }
    std::vector<std::string> uniqueOwned;
    uniqueOwned.reserve(meta.ownedDroneIds.size());
    for (const std::string& id : meta.ownedDroneIds) {
        if (std::find(uniqueOwned.begin(), uniqueOwned.end(), id) == uniqueOwned.end()) {
            uniqueOwned.push_back(id);
        }
    }
    meta.ownedDroneIds = std::move(uniqueOwned);
    for (std::string& id : meta.equippedDroneIds) {
        replaceId(id);
    }
    for (DroneUpgradeRecord& record : meta.droneUpgrades) {
        replaceId(record.droneId);
    }
    std::vector<DroneUpgradeRecord> merged;
    for (const DroneUpgradeRecord& record : meta.droneUpgrades) {
        const auto existing = std::find_if(merged.begin(), merged.end(), [&](const DroneUpgradeRecord& candidate) {
            return candidate.droneId == record.droneId;
        });
        if (existing == merged.end()) {
            merged.push_back(record);
        } else {
            existing->level = std::max(existing->level, record.level);
        }
    }
    meta.droneUpgrades = std::move(merged);
}

std::vector<int> parseInts(std::string_view text)
{
    std::vector<int> values;
    for (const std::string& item : split(text, save_schema::listDelimiter)) {
        values.push_back(parseInt(item, 0));
    }
    return values;
}

std::vector<std::string> arrayToVector(const std::array<std::string, 3>& values)
{
    return {values.begin(), values.end()};
}

std::array<std::string, 3> vectorToOfferArray(const std::vector<std::string>& values)
{
    std::array<std::string, 3> result {};
    const std::size_t count = std::min(result.size(), values.size());
    for (std::size_t i = 0; i < count; ++i) {
        result[i] = values[i];
    }
    return result;
}

} // namespace

SaveData captureSaveData(const GameState& state)
{
    SaveData save;
    save.seed = state.seed;
    save.credits = state.run.credits;
    save.destinationIndex = state.run.destinationIndex;
    save.frontierReadiness = state.run.frontierReadiness;
    save.shipDamage = state.run.shipDamage;
    save.frameId = state.run.frameId;
    save.offerRerollsThisExpedition = state.run.offerRerollsThisExpedition;
    save.repairOpsThisExpedition = state.run.repairOpsThisExpedition;
    save.trainingOpsThisExpedition = state.run.trainingOpsThisExpedition;
    save.restOpsThisExpedition = state.run.restOpsThisExpedition;
    save.shallowRecoveryStreak = state.run.shallowRecoveryStreak;
    save.cleanShallowRecoveryStreak = state.run.cleanShallowRecoveryStreak;
    save.nextLaunchFuelBoost = state.run.nextLaunchFuelBoost;
    save.nextLaunchSpeedBoost = state.run.nextLaunchSpeedBoost;
    if ((state.screen == Screen::ArrivalFanfare || state.screen == Screen::Flyby || state.screen == Screen::Orbit) && state.run.arrivalOps.active) {
        save.screen = Screen::ArrivalOps;
    } else if ((state.screen == Screen::SurfaceScan || state.screen == Screen::SurfacePush) && state.run.surfaceExpedition.active) {
        save.screen = Screen::SurfaceExpedition;
    } else {
        save.screen = state.screen == Screen::ArrivalOps || state.screen == Screen::Research || state.screen == Screen::SurfaceExpedition || state.screen == Screen::SurfaceUpgrade || state.screen == Screen::Mining || state.screen == Screen::DroneOps || state.screen == Screen::Navigation ? state.screen : Screen::Hangar;
    }
    save.campaignMilestone = state.meta.campaignMilestone;
    save.chapter = state.meta.chapter;
    save.ark = state.meta.ark;
    save.navigation = state.meta.navigation;
    save.inventoryModuleIds = state.run.inventoryModuleIds;
    save.equippedModuleIds = state.run.equippedModuleIds;
    save.surfaceUpgradeIds = state.run.surfaceUpgradeIds;
    save.crewUpgradeIds = state.run.crewUpgradeIds;
    save.researchProjectIds = arrayToVector(state.run.researchProjectIds);
    save.arrivalOps = state.run.arrivalOps;
    save.surfaceExpedition = state.run.surfaceExpedition;
    save.mining = state.run.mining;
    save.unlockKeys = state.meta.unlockKeys;
    save.blueprintProgress = state.meta.blueprintProgress;
    save.materials = state.meta.materials;
    save.ownedModuleIds = state.meta.ownedModuleIds;
    save.defaultEquippedModuleIds = state.meta.defaultEquippedModuleIds;
    save.droneBaySlots = state.meta.droneBaySlots;
    save.ownedDroneIds = state.meta.ownedDroneIds;
    save.equippedDroneIds = state.meta.equippedDroneIds;
    save.droneUpgrades = state.meta.droneUpgrades;
    save.artifacts = state.meta.artifacts;
    save.furthestTier = state.meta.furthestTier;
    save.shipsLost = state.meta.shipsLost;
    save.astronautsLost = state.meta.astronautsLost;
    save.closestSurvivalMargin = state.meta.closestSurvivalMargin;
    save.closestSurvivalBurn = state.meta.closestSurvivalBurn;
    save.closestSurvivalFailurePoint = state.meta.closestSurvivalFailurePoint;
    save.maxBurnDepth = state.meta.maxBurnDepth;
    save.maxPeakWarning = state.meta.maxPeakWarning;
    save.maxPeakAbortRisk = state.meta.maxPeakAbortRisk;
    save.bestCreditDelta = state.meta.bestCreditDelta;
    save.worstCreditDelta = state.meta.worstCreditDelta;
    save.totalFlybyMisses = state.meta.totalFlybyMisses;
    save.totalFlybyGoods = state.meta.totalFlybyGoods;
    save.totalFlybyPerfects = state.meta.totalFlybyPerfects;
    save.destinationAttempts = state.meta.destinationAttempts;
    save.destinationSuccesses = state.meta.destinationSuccesses;
    save.destinationFlybys = state.meta.destinationFlybys;
    save.destinationOrbits = state.meta.destinationOrbits;
    save.destinationLandings = state.meta.destinationLandings;
    save.memorials = state.meta.memorials;
    save.famousLaunches = state.meta.famousLaunches;
    save.crew = state.run.crew;
    return save;
}

void restoreSaveData(GameState& state, const ContentCatalog& catalog, const SaveData& save)
{
    state.seed = save.seed;
    state.run.credits = save.credits;
    state.run.destinationIndex = std::clamp(save.destinationIndex, 0, static_cast<int>(catalog.destinations.size()) - 1);
    state.run.frontierReadiness = std::max(0, save.frontierReadiness);
    state.run.shipDamage = std::clamp(save.shipDamage, 0, 100);
    state.run.frameId = catalog.findFrame(save.frameId) == nullptr ? catalog.frames.front().id : save.frameId;
    state.run.offerRerollsThisExpedition = std::max(0, save.offerRerollsThisExpedition);
    state.run.repairOpsThisExpedition = std::max(0, save.repairOpsThisExpedition);
    state.run.trainingOpsThisExpedition = std::max(0, save.trainingOpsThisExpedition);
    state.run.restOpsThisExpedition = std::max(0, save.restOpsThisExpedition);
    state.run.shallowRecoveryStreak = std::max(0, save.shallowRecoveryStreak);
    state.run.cleanShallowRecoveryStreak = std::max(0, save.cleanShallowRecoveryStreak);
    state.run.nextLaunchFuelBoost = std::max(0.0, save.nextLaunchFuelBoost);
    state.run.nextLaunchSpeedBoost = std::max(0.0, save.nextLaunchSpeedBoost);
    state.run.inventoryModuleIds = save.inventoryModuleIds.empty() ? state.run.inventoryModuleIds : save.inventoryModuleIds;
    state.run.equippedModuleIds = save.equippedModuleIds.empty() ? state.run.equippedModuleIds : save.equippedModuleIds;
    state.run.surfaceUpgradeIds = save.surfaceUpgradeIds;
    state.run.crewUpgradeIds = save.crewUpgradeIds;
    state.run.researchProjectIds = vectorToOfferArray(save.researchProjectIds);
    state.run.arrivalOps = save.arrivalOps;
    state.run.surfaceExpedition = save.surfaceExpedition;
    state.run.mining = save.mining;
    if (state.run.mining.active) {
        state.run.surfaceExpedition.miningSitePrepared = true;
        state.run.surfaceExpedition.miningRunUsed = true;
        const double hullLength = std::sqrt(
            state.run.mining.hullDirX * state.run.mining.hullDirX +
            state.run.mining.hullDirY * state.run.mining.hullDirY);
        if (hullLength <= 0.0001) {
            state.run.mining.hullDirX = 0.0;
            state.run.mining.hullDirY = 1.0;
        } else {
            state.run.mining.hullDirX /= hullLength;
            state.run.mining.hullDirY /= hullLength;
        }
        state.run.mining.aimDirX = state.run.mining.hullDirX;
        state.run.mining.aimDirY = state.run.mining.hullDirY;
        state.run.mining.aimX = state.run.mining.droneX + state.run.mining.aimDirX * tuning::mining::drillRangeCells;
        state.run.mining.aimY = state.run.mining.droneY + state.run.mining.aimDirY * tuning::mining::drillRangeCells;
    }
    if (state.run.surfaceExpedition.active) {
        const int expectedFuelCapacity = surfaceSharedFuelCapacity(state, catalog);
        if (state.run.surfaceExpedition.sharedFuelCapacity <= 0) {
            state.run.surfaceExpedition.sharedFuelCapacity = expectedFuelCapacity;
            state.run.surfaceExpedition.sharedFuel = state.run.mining.active
                ? std::max(0, state.run.surfaceExpedition.sharedFuelCapacity - std::max(0, state.run.mining.fuelSpent))
                : state.run.surfaceExpedition.sharedFuelCapacity;
        } else if (state.run.surfaceExpedition.sharedFuelCapacity < expectedFuelCapacity) {
            const int capacityDelta = expectedFuelCapacity - state.run.surfaceExpedition.sharedFuelCapacity;
            state.run.surfaceExpedition.sharedFuelCapacity = expectedFuelCapacity;
            state.run.surfaceExpedition.sharedFuel = std::min(
                expectedFuelCapacity,
                std::max(0, state.run.surfaceExpedition.sharedFuel) + capacityDelta);
        }
    }
    if (state.run.surfaceExpedition.logEntries.size() > static_cast<std::size_t>(tuning::research::surfaceLogEntryLimit)) {
        state.run.surfaceExpedition.logEntries.erase(
            state.run.surfaceExpedition.logEntries.begin(),
            state.run.surfaceExpedition.logEntries.end() - tuning::research::surfaceLogEntryLimit);
    }
    state.screen = save.screen;
    if (state.screen == Screen::ArrivalOps && !state.run.arrivalOps.active) {
        state.screen = Screen::Hangar;
    }
    if (state.screen == Screen::Research && save.researchProjectIds.empty()) {
        state.screen = Screen::Hangar;
    }
    if (state.screen == Screen::SurfaceExpedition && !state.run.surfaceExpedition.active) {
        state.screen = Screen::Hangar;
    }
    if (state.screen == Screen::SurfaceUpgrade && (!state.run.surfaceExpedition.active || !state.run.surfaceExpedition.surfaceUpgradeOfferAvailable)) {
        state.screen = state.run.surfaceExpedition.active ? Screen::SurfaceExpedition : Screen::Hangar;
    }
    if (state.screen == Screen::Mining && (!state.run.surfaceExpedition.active || !state.run.mining.active)) {
        state.screen = state.run.surfaceExpedition.active ? Screen::SurfaceExpedition : Screen::Hangar;
        state.run.mining = {};
    }
    if (state.screen == Screen::DroneOps && !state.run.surfaceExpedition.active) {
        state.screen = Screen::Hangar;
    }
    if (state.run.mining.active && static_cast<int>(state.run.mining.terrain.cells.size()) != state.run.mining.terrain.width * state.run.mining.terrain.height) {
        state.run.mining = {};
        if (state.screen == Screen::Mining) {
            state.screen = state.run.surfaceExpedition.active ? Screen::SurfaceExpedition : Screen::Hangar;
        }
    }
    normalizeLegacyMiningArtifact(state.run.mining);
    normalizeLegacyMiningReturnZone(state.run.mining);
    normalizeLegacyMiningHazards(state.run.mining);
    state.meta.unlockKeys = save.unlockKeys.empty() ? std::vector<std::string>{content::unlock::starter} : save.unlockKeys;
    state.meta.blueprintProgress = save.blueprintProgress;
    state.meta.materials = save.materials;
    state.meta.ownedModuleIds = save.ownedModuleIds.empty() ? state.run.inventoryModuleIds : save.ownedModuleIds;
    state.meta.defaultEquippedModuleIds = save.defaultEquippedModuleIds.empty() ? state.run.equippedModuleIds : save.defaultEquippedModuleIds;
    state.meta.droneBaySlots = save.droneBaySlots;
    state.meta.ownedDroneIds = save.ownedDroneIds;
    state.meta.equippedDroneIds = save.equippedDroneIds;
    state.meta.droneUpgrades = save.droneUpgrades;
    migrateLegacyHazardDroneRecords(state.meta);
    ensureDroneBayState(state, catalog);
    if (state.screen == Screen::DroneOps && !droneBayUnlocked(state)) {
        state.screen = state.run.surfaceExpedition.active ? Screen::SurfaceExpedition : Screen::Hangar;
    }
    state.meta.campaignMilestone = save.campaignMilestone;
    state.meta.chapter = save.chapter;
    state.meta.ark = save.ark;
    state.meta.navigation = save.navigation;
    if (state.screen == Screen::Navigation && !navigationAvailable(state)) {
        state.screen = Screen::Hangar;
    }
    state.meta.artifacts = save.artifacts;
    state.meta.furthestTier = save.furthestTier;
    state.meta.shipsLost = save.shipsLost;
    state.meta.astronautsLost = save.astronautsLost;
    state.meta.closestSurvivalMargin = std::max(0.0, save.closestSurvivalMargin);
    state.meta.closestSurvivalBurn = std::max(0.0, save.closestSurvivalBurn);
    state.meta.closestSurvivalFailurePoint = std::max(0.0, save.closestSurvivalFailurePoint);
    state.meta.maxBurnDepth = std::max(0.0, save.maxBurnDepth);
    state.meta.maxPeakWarning = std::max(0.0, save.maxPeakWarning);
    state.meta.maxPeakAbortRisk = std::max(0.0, save.maxPeakAbortRisk);
    state.meta.bestCreditDelta = save.bestCreditDelta;
    state.meta.worstCreditDelta = save.worstCreditDelta;
    state.meta.totalFlybyMisses = std::max(0, save.totalFlybyMisses);
    state.meta.totalFlybyGoods = std::max(0, save.totalFlybyGoods);
    state.meta.totalFlybyPerfects = std::max(0, save.totalFlybyPerfects);
    state.meta.destinationAttempts = save.destinationAttempts;
    state.meta.destinationSuccesses = save.destinationSuccesses;
    state.meta.destinationFlybys = save.destinationFlybys;
    state.meta.destinationOrbits = save.destinationOrbits;
    state.meta.destinationLandings = save.destinationLandings;
    state.meta.memorials = save.memorials;
    state.meta.famousLaunches = save.famousLaunches;
    if (!save.crew.empty()) {
        for (auto& astronaut : state.run.crew) {
            const auto found = std::find_if(save.crew.begin(), save.crew.end(), [&](const Astronaut& savedAstronaut) {
                return savedAstronaut.id == astronaut.id;
            });
            if (found != save.crew.end()) {
                astronaut.training = found->training;
                astronaut.stress = found->stress;
                astronaut.status = found->status;
            }
        }

        for (const Astronaut& savedAstronaut : save.crew) {
            const auto found = std::find_if(state.run.crew.begin(), state.run.crew.end(), [&](const Astronaut& existing) {
                return existing.id == savedAstronaut.id;
            });
            if (found == state.run.crew.end()) {
                Astronaut recruit = savedAstronaut;
                recruit.name = text::isReplacementId(savedAstronaut.id) ? std::string(text::panel::messages::replacementCadet) : savedAstronaut.id;
                recruit.background = std::string(text::panel::messages::restoredCrewBackground);
                recruit.trait = std::string(text::panel::messages::generatedRecruitTrait);
                state.run.crew.push_back(recruit);
            }
        }
    }
    syncLaunchConfig(state, catalog);
}

std::string serializeSaveData(const SaveData& save)
{
    std::ostringstream out;
    out << save_schema::header << "\n";
    writeField(out, save_schema::field::version, save.version);
    writeField(out, save_schema::field::seed, save.seed);
    writeField(out, save_schema::field::credits, save.credits);
    writeField(out, save_schema::field::destinationIndex, save.destinationIndex);
    writeField(out, save_schema::field::frontierReadiness, save.frontierReadiness);
    writeField(out, save_schema::field::shipDamage, save.shipDamage);
    writeField(out, save_schema::field::frameId, save.frameId);
    writeField(out, save_schema::field::offerRerolls, save.offerRerollsThisExpedition);
    writeField(out, save_schema::field::repairOps, save.repairOpsThisExpedition);
    writeField(out, save_schema::field::trainingOps, save.trainingOpsThisExpedition);
    writeField(out, save_schema::field::restOps, save.restOpsThisExpedition);
    writeField(out, save_schema::field::shallowRecoveryStreak, save.shallowRecoveryStreak);
    writeField(out, save_schema::field::cleanShallowRecoveryStreak, save.cleanShallowRecoveryStreak);
    writeField(out, save_schema::field::nextLaunchFuelBoost, save.nextLaunchFuelBoost);
    writeField(out, save_schema::field::nextLaunchSpeedBoost, save.nextLaunchSpeedBoost);
    writeField(out, save_schema::field::screen, screenToInt(save.screen));
    writeField(out, save_schema::field::campaignMilestone, campaignMilestoneToInt(save.campaignMilestone));
    writeField(out, save_schema::field::chapter, gameChapterToInt(save.chapter));
    writeField(out, save_schema::field::arkCondition, arkConditionToInt(save.ark.condition));
    writeField(out, save_schema::field::arkFuelReserve, save.ark.fuelReserve);
    writeField(out, save_schema::field::arkHullDamage, save.ark.hullDamage);
    writeField(out, save_schema::field::arkRepairProgress, save.ark.repairProgress);
    writeField(out, save_schema::field::arkRepairModules, join(save.ark.repairModuleIds, save_schema::listDelimiter));
    writeField(out, save_schema::field::arkFirstJumpComplete, save.ark.firstJumpComplete ? 1 : 0);
    writeField(out, save_schema::field::arkGravityWellDisaster, save.ark.gravityWellDisaster ? 1 : 0);
    writeField(out, save_schema::field::navigationSystem, save.navigation.currentSystemId);
    writeField(out, save_schema::field::navigationArkLocation, save.navigation.arkLocationId);
    writeField(out, save_schema::field::navigationSelectedDestination, save.navigation.selectedDestinationId);
    writeField(out, save_schema::field::navigationDiscoveredDestinations, join(save.navigation.discoveredDestinationIds, save_schema::listDelimiter));
    writeField(out, save_schema::field::inventory, join(save.inventoryModuleIds, save_schema::listDelimiter));
    writeField(out, save_schema::field::equipped, join(save.equippedModuleIds, save_schema::listDelimiter));
    writeField(out, save_schema::field::ownedModules, join(save.ownedModuleIds, save_schema::listDelimiter));
    writeField(out, save_schema::field::defaultEquippedModules, join(save.defaultEquippedModuleIds, save_schema::listDelimiter));
    writeField(out, save_schema::field::crewUpgrades, join(save.crewUpgradeIds, save_schema::listDelimiter));
    writeField(out, save_schema::field::researchProjects, join(save.researchProjectIds, save_schema::listDelimiter));
    writeField(out, save_schema::field::arrivalActive, save.arrivalOps.active ? 1 : 0);
    writeField(out, save_schema::field::arrivalDestination, save.arrivalOps.destinationId);
    writeField(out, save_schema::field::surfaceActive, save.surfaceExpedition.active ? 1 : 0);
    writeField(out, save_schema::field::surfaceDestination, save.surfaceExpedition.destinationId);
    writeField(out, save_schema::field::surfaceSite, surfaceSiteProfileToInt(save.surfaceExpedition.siteProfile));
    writeField(out, save_schema::field::surfaceSupply, save.surfaceExpedition.supply);
    writeField(out, save_schema::field::surfaceSharedFuel, save.surfaceExpedition.sharedFuel);
    writeField(out, save_schema::field::surfaceSharedFuelCapacity, save.surfaceExpedition.sharedFuelCapacity);
    writeField(out, save_schema::field::surfaceCargo, save.surfaceExpedition.cargo);
    writeField(out, save_schema::field::surfaceHazard, save.surfaceExpedition.hazard);
    writeField(out, save_schema::field::surfaceDepth, save.surfaceExpedition.depth);
    writeField(out, save_schema::field::surfaceMaterials, serializeMaterials(save.surfaceExpedition.temporaryMaterials));
    writeField(out, save_schema::field::surfaceArtifacts, serializeArtifacts(save.surfaceExpedition.temporaryArtifacts));
    writeField(out, save_schema::field::surfaceProspectMaterials, serializeMaterials(save.surfaceExpedition.prospectMaterials));
    writeField(out, save_schema::field::surfaceProspectArtifacts, save.surfaceExpedition.prospectArtifacts);
    writeField(out, save_schema::field::surfaceDepthProspects, serializeDepthProspects(save.surfaceExpedition.depthProspects));
    writeField(out, save_schema::field::surfaceEnemies, save.surfaceExpedition.enemyEncountersEnabled ? 1 : 0);
    writeField(out, save_schema::field::surfaceMiningPrepared, save.surfaceExpedition.miningSitePrepared ? 1 : 0);
    writeField(out, save_schema::field::surfaceMiningUsed, save.surfaceExpedition.miningRunUsed ? 1 : 0);
    writeField(out, save_schema::field::surfaceLog, join(save.surfaceExpedition.logEntries, save_schema::textListDelimiter));
    writeField(out, save_schema::field::surfaceUpgrades, join(save.surfaceUpgradeIds, save_schema::listDelimiter));
    writeField(out, save_schema::field::surfaceUpgradeOffers, join(arrayToVector(save.surfaceExpedition.surfaceUpgradeOfferIds), save_schema::listDelimiter));
    writeField(out, save_schema::field::surfaceUpgradeOfferAvailable, save.surfaceExpedition.surfaceUpgradeOfferAvailable ? 1 : 0);
    writeField(out, save_schema::field::surfaceUpgradeOffersSeen, save.surfaceExpedition.surfaceUpgradeOffersSeen);
    writeField(out, save_schema::field::miningActive, save.mining.active ? 1 : 0);
    writeField(out, save_schema::field::miningDestination, save.mining.destinationId);
    writeField(out, save_schema::field::miningSite, surfaceSiteProfileToInt(save.mining.siteProfile));
    writeField(out, save_schema::field::miningElapsed, save.mining.elapsedSeconds);
    writeField(out, save_schema::field::miningOxygen, save.mining.oxygenSeconds);
    writeField(out, save_schema::field::miningDroneHealth, save.mining.droneHealth);
    writeField(out, save_schema::field::miningFuelCycle, save.mining.fuelCycleProgress);
    writeField(out, save_schema::field::miningFuelSpent, save.mining.fuelSpent);
    writeField(out, save_schema::field::miningDrone, serializePair(save.mining.droneX, save.mining.droneY));
    writeField(out, save_schema::field::miningReturnZone, serializePair(save.mining.returnZoneX, save.mining.returnZoneY));
    writeField(out, save_schema::field::miningAim, serializePair(save.mining.aimX, save.mining.aimY));
    writeField(out, save_schema::field::miningHullHeading, serializePair(save.mining.hullDirX, save.mining.hullDirY));
    writeField(out, save_schema::field::miningDrill, serializePair(save.mining.drillHeat, save.mining.drillIntegrity));
    writeField(out, save_schema::field::miningDepth, save.mining.depthZone);
    writeField(out, save_schema::field::miningCargo, save.mining.cargo);
    writeField(out, save_schema::field::miningMaterials, serializeMaterials(save.mining.temporaryMaterials));
    writeField(out, save_schema::field::miningArtifacts, serializeArtifacts(save.mining.temporaryArtifacts));
    writeField(out, save_schema::field::miningStowedCargo, save.mining.stowedCargo);
    writeField(out, save_schema::field::miningStowedMaterials, serializeMaterials(save.mining.stowedMaterials));
    writeField(out, save_schema::field::miningStowedArtifacts, serializeArtifacts(save.mining.stowedArtifacts));
    writeField(out, save_schema::field::miningHazard, save.mining.hazardDelta);
    writeField(out, save_schema::field::miningPassiveDroneYield, save.mining.passiveDroneYield);
    writeField(out, save_schema::field::miningCellsBroken, save.mining.cellsBroken);
    writeField(out, save_schema::field::miningEnemies, serializeMiningEnemies(save.mining.enemies));
    writeField(out, save_schema::field::miningMiniDrones, serializeMiningMiniDrones(save.mining.miniDrones));
    writeField(
        out,
        save_schema::field::miningCombat,
        std::to_string(save.mining.enemiesDefeated) + std::string(1, save_schema::crewFieldDelimiter) +
            std::to_string(save.mining.defenseDamageDealt) + std::string(1, save_schema::crewFieldDelimiter) +
            std::to_string(save.mining.enemyDamageTaken) + std::string(1, save_schema::crewFieldDelimiter) +
            std::to_string(save.mining.areaControlDamageDealt) + std::string(1, save_schema::crewFieldDelimiter) +
            std::to_string(save.mining.reactiveArmorDamageDealt) + std::string(1, save_schema::crewFieldDelimiter) +
            std::to_string(save.mining.environmentalShieldAbsorbed) + std::string(1, save_schema::crewFieldDelimiter) +
            std::to_string(save.mining.elementalExposureSeconds) + std::string(1, save_schema::crewFieldDelimiter) +
            std::to_string(save.mining.movementSlowSeconds) + std::string(1, save_schema::crewFieldDelimiter) +
            std::to_string(save.mining.movementSlowScale));
    writeField(out, save_schema::field::miningArtifact, serializeMiningArtifact(save.mining.artifact));
    writeField(out, save_schema::field::miningTerrainSize, serializeMiningTerrainSize(save.mining.terrain));
    writeField(out, save_schema::field::miningTerrainCells, serializeMiningCells(save.mining.terrain));
    writeField(out, save_schema::field::unlocks, join(save.unlockKeys, save_schema::listDelimiter));
    writeField(out, save_schema::field::blueprints, save.blueprintProgress);
    writeField(out, save_schema::field::materials, serializeMaterials(save.materials));
    writeField(out, save_schema::field::droneBaySlots, save.droneBaySlots);
    writeField(out, save_schema::field::ownedDrones, join(save.ownedDroneIds, save_schema::listDelimiter));
    writeField(out, save_schema::field::equippedDrones, join(save.equippedDroneIds, save_schema::listDelimiter));
    writeField(out, save_schema::field::droneUpgrades, serializeDroneUpgrades(save.droneUpgrades));
    writeField(out, save_schema::field::artifacts, serializeArtifacts(save.artifacts));
    writeField(out, save_schema::field::furthestTier, save.furthestTier);
    writeField(out, save_schema::field::shipsLost, save.shipsLost);
    writeField(out, save_schema::field::astronautsLost, save.astronautsLost);
    writeField(out, save_schema::field::closestSurvivalMargin, save.closestSurvivalMargin);
    writeField(out, save_schema::field::closestSurvivalBurn, save.closestSurvivalBurn);
    writeField(out, save_schema::field::closestSurvivalFailurePoint, save.closestSurvivalFailurePoint);
    writeField(out, save_schema::field::maxBurnDepth, save.maxBurnDepth);
    writeField(out, save_schema::field::maxPeakWarning, save.maxPeakWarning);
    writeField(out, save_schema::field::maxPeakAbortRisk, save.maxPeakAbortRisk);
    writeField(out, save_schema::field::bestCreditDelta, save.bestCreditDelta);
    writeField(out, save_schema::field::worstCreditDelta, save.worstCreditDelta);
    writeField(out, save_schema::field::totalFlybyMisses, save.totalFlybyMisses);
    writeField(out, save_schema::field::totalFlybyGoods, save.totalFlybyGoods);
    writeField(out, save_schema::field::totalFlybyPerfects, save.totalFlybyPerfects);
    writeField(out, save_schema::field::destinationAttempts, joinInts(save.destinationAttempts, save_schema::listDelimiter));
    writeField(out, save_schema::field::destinationSuccesses, joinInts(save.destinationSuccesses, save_schema::listDelimiter));
    writeField(out, save_schema::field::destinationFlybys, joinInts(save.destinationFlybys, save_schema::listDelimiter));
    writeField(out, save_schema::field::destinationOrbits, joinInts(save.destinationOrbits, save_schema::listDelimiter));
    writeField(out, save_schema::field::destinationLandings, joinInts(save.destinationLandings, save_schema::listDelimiter));
    writeField(out, save_schema::field::memorials, join(save.memorials, save_schema::textListDelimiter));
    writeField(out, save_schema::field::famousLaunches, join(save.famousLaunches, save_schema::textListDelimiter));
    writeField(out, save_schema::field::crew, serializeCrew(save.crew));
    return out.str();
}

std::optional<SaveData> deserializeSaveData(std::string_view text)
{
    const std::size_t firstLineEnd = text.find('\n');
    const std::string_view headerLine = firstLineEnd == std::string_view::npos ? text : text.substr(0, firstLineEnd);
    if (headerLine != save_schema::header) {
        return std::nullopt;
    }

    SaveData save;
    for (std::string_view line : split(text, '\n')) {
        const std::size_t equals = line.find(save_schema::keyValueDelimiter);
        if (equals == std::string_view::npos) {
            continue;
        }

        const std::string_view key = line.substr(0, equals);
        const std::string_view value = line.substr(equals + 1);

        if (key == save_schema::field::version) {
            save.version = parseInt(value, save.version);
        } else if (key == save_schema::field::seed) {
            save.seed = parseU64(value, save.seed);
        } else if (key == save_schema::field::credits) {
            save.credits = parseDouble(value, save.credits);
        } else if (key == save_schema::field::destinationIndex) {
            save.destinationIndex = parseInt(value, save.destinationIndex);
        } else if (key == save_schema::field::frontierReadiness) {
            save.frontierReadiness = parseInt(value, save.frontierReadiness);
        } else if (key == save_schema::field::shipDamage) {
            save.shipDamage = parseInt(value, save.shipDamage);
        } else if (key == save_schema::field::frameId) {
            save.frameId = std::string(value);
        } else if (key == save_schema::field::offerRerolls) {
            save.offerRerollsThisExpedition = parseInt(value, save.offerRerollsThisExpedition);
        } else if (key == save_schema::field::repairOps) {
            save.repairOpsThisExpedition = parseInt(value, save.repairOpsThisExpedition);
        } else if (key == save_schema::field::trainingOps) {
            save.trainingOpsThisExpedition = parseInt(value, save.trainingOpsThisExpedition);
        } else if (key == save_schema::field::restOps) {
            save.restOpsThisExpedition = parseInt(value, save.restOpsThisExpedition);
        } else if (key == save_schema::field::shallowRecoveryStreak) {
            save.shallowRecoveryStreak = parseInt(value, save.shallowRecoveryStreak);
        } else if (key == save_schema::field::cleanShallowRecoveryStreak) {
            save.cleanShallowRecoveryStreak = parseInt(value, save.cleanShallowRecoveryStreak);
        } else if (key == save_schema::field::nextLaunchFuelBoost) {
            save.nextLaunchFuelBoost = parseDouble(value, save.nextLaunchFuelBoost);
        } else if (key == save_schema::field::nextLaunchSpeedBoost) {
            save.nextLaunchSpeedBoost = parseDouble(value, save.nextLaunchSpeedBoost);
        } else if (key == save_schema::field::screen) {
            save.screen = screenFromInt(parseInt(value, screenToInt(save.screen)));
        } else if (key == save_schema::field::campaignMilestone) {
            save.campaignMilestone = campaignMilestoneFromInt(parseInt(value, campaignMilestoneToInt(save.campaignMilestone)));
        } else if (key == save_schema::field::chapter) {
            save.chapter = gameChapterFromInt(parseInt(value, gameChapterToInt(save.chapter)));
        } else if (key == save_schema::field::arkCondition) {
            save.ark.condition = arkConditionFromInt(parseInt(value, arkConditionToInt(save.ark.condition)));
        } else if (key == save_schema::field::arkFuelReserve) {
            save.ark.fuelReserve = parseInt(value, save.ark.fuelReserve);
        } else if (key == save_schema::field::arkHullDamage) {
            save.ark.hullDamage = parseInt(value, save.ark.hullDamage);
        } else if (key == save_schema::field::arkRepairProgress) {
            save.ark.repairProgress = parseInt(value, save.ark.repairProgress);
        } else if (key == save_schema::field::arkRepairModules) {
            save.ark.repairModuleIds = split(value, save_schema::listDelimiter);
        } else if (key == save_schema::field::arkFirstJumpComplete) {
            save.ark.firstJumpComplete = parseInt(value, 0) != 0;
        } else if (key == save_schema::field::arkGravityWellDisaster) {
            save.ark.gravityWellDisaster = parseInt(value, 0) != 0;
        } else if (key == save_schema::field::navigationSystem) {
            save.navigation.currentSystemId = std::string(value);
        } else if (key == save_schema::field::navigationArkLocation) {
            save.navigation.arkLocationId = std::string(value);
        } else if (key == save_schema::field::navigationSelectedDestination) {
            save.navigation.selectedDestinationId = std::string(value);
        } else if (key == save_schema::field::navigationDiscoveredDestinations) {
            save.navigation.discoveredDestinationIds = split(value, save_schema::listDelimiter);
        } else if (key == save_schema::field::inventory) {
            save.inventoryModuleIds = split(value, save_schema::listDelimiter);
        } else if (key == save_schema::field::equipped) {
            save.equippedModuleIds = split(value, save_schema::listDelimiter);
        } else if (key == save_schema::field::ownedModules) {
            save.ownedModuleIds = split(value, save_schema::listDelimiter);
        } else if (key == save_schema::field::defaultEquippedModules) {
            save.defaultEquippedModuleIds = split(value, save_schema::listDelimiter);
        } else if (key == save_schema::field::crewUpgrades) {
            save.crewUpgradeIds = split(value, save_schema::listDelimiter);
        } else if (key == save_schema::field::researchProjects) {
            save.researchProjectIds = split(value, save_schema::listDelimiter);
        } else if (key == save_schema::field::arrivalActive) {
            save.arrivalOps.active = parseInt(value, 0) != 0;
        } else if (key == save_schema::field::arrivalDestination) {
            save.arrivalOps.destinationId = std::string(value);
        } else if (key == save_schema::field::surfaceActive) {
            save.surfaceExpedition.active = parseInt(value, 0) != 0;
        } else if (key == save_schema::field::surfaceDestination) {
            save.surfaceExpedition.destinationId = std::string(value);
        } else if (key == save_schema::field::surfaceSite) {
            save.surfaceExpedition.siteProfile = surfaceSiteProfileFromInt(parseInt(value, surfaceSiteProfileToInt(save.surfaceExpedition.siteProfile)));
        } else if (key == save_schema::field::surfaceSupply) {
            save.surfaceExpedition.supply = parseInt(value, save.surfaceExpedition.supply);
        } else if (key == save_schema::field::surfaceSharedFuel) {
            save.surfaceExpedition.sharedFuel = parseInt(value, save.surfaceExpedition.sharedFuel);
        } else if (key == save_schema::field::surfaceSharedFuelCapacity) {
            save.surfaceExpedition.sharedFuelCapacity = parseInt(value, save.surfaceExpedition.sharedFuelCapacity);
        } else if (key == save_schema::field::surfaceCargo) {
            save.surfaceExpedition.cargo = parseInt(value, save.surfaceExpedition.cargo);
        } else if (key == save_schema::field::surfaceHazard) {
            save.surfaceExpedition.hazard = parseDouble(value, save.surfaceExpedition.hazard);
        } else if (key == save_schema::field::surfaceDepth) {
            save.surfaceExpedition.depth = parseInt(value, save.surfaceExpedition.depth);
        } else if (key == save_schema::field::surfaceMaterials) {
            save.surfaceExpedition.temporaryMaterials = parseMaterials(value);
        } else if (key == save_schema::field::surfaceArtifacts) {
            save.surfaceExpedition.temporaryArtifacts = parseArtifacts(value);
        } else if (key == save_schema::field::surfaceProspectMaterials) {
            save.surfaceExpedition.prospectMaterials = parseMaterials(value);
        } else if (key == save_schema::field::surfaceProspectArtifacts) {
            save.surfaceExpedition.prospectArtifacts = parseInt(value, save.surfaceExpedition.prospectArtifacts);
        } else if (key == save_schema::field::surfaceDepthProspects) {
            save.surfaceExpedition.depthProspects = parseDepthProspects(value);
        } else if (key == save_schema::field::surfaceEnemies) {
            save.surfaceExpedition.enemyEncountersEnabled = parseInt(value, 0) != 0;
        } else if (key == save_schema::field::surfaceMiningPrepared) {
            save.surfaceExpedition.miningSitePrepared = parseInt(value, 0) != 0;
        } else if (key == save_schema::field::surfaceMiningUsed) {
            save.surfaceExpedition.miningRunUsed = parseInt(value, 0) != 0;
        } else if (key == save_schema::field::surfaceLog) {
            save.surfaceExpedition.logEntries = split(value, save_schema::textListDelimiter);
        } else if (key == save_schema::field::surfaceUpgrades) {
            save.surfaceUpgradeIds = split(value, save_schema::listDelimiter);
        } else if (key == save_schema::field::surfaceUpgradeOffers) {
            save.surfaceExpedition.surfaceUpgradeOfferIds = vectorToOfferArray(split(value, save_schema::listDelimiter));
        } else if (key == save_schema::field::surfaceUpgradeOfferAvailable) {
            save.surfaceExpedition.surfaceUpgradeOfferAvailable = parseInt(value, 0) != 0;
        } else if (key == save_schema::field::surfaceUpgradeOffersSeen) {
            save.surfaceExpedition.surfaceUpgradeOffersSeen = parseInt(value, save.surfaceExpedition.surfaceUpgradeOffersSeen);
        } else if (key == save_schema::field::miningActive) {
            save.mining.active = parseInt(value, 0) != 0;
        } else if (key == save_schema::field::miningDestination) {
            save.mining.destinationId = std::string(value);
        } else if (key == save_schema::field::miningSite) {
            save.mining.siteProfile = surfaceSiteProfileFromInt(parseInt(value, surfaceSiteProfileToInt(save.mining.siteProfile)));
        } else if (key == save_schema::field::miningElapsed) {
            save.mining.elapsedSeconds = parseDouble(value, save.mining.elapsedSeconds);
        } else if (key == save_schema::field::miningOxygen) {
            save.mining.oxygenSeconds = parseDouble(value, save.mining.oxygenSeconds);
        } else if (key == save_schema::field::miningDroneHealth) {
            save.mining.droneHealth = std::clamp(parseDouble(value, save.mining.droneHealth), 0.0, 1.0);
        } else if (key == save_schema::field::miningFuelCycle) {
            save.mining.fuelCycleProgress = std::clamp(parseDouble(value, save.mining.fuelCycleProgress), 0.0, 1.0);
        } else if (key == save_schema::field::miningFuelBurn) {
            const double legacyBurnSeconds = std::max(0.0, parseDouble(value, 0.0));
            save.mining.fuelCycleProgress = std::clamp(
                legacyBurnSeconds * tuning::mining::fuelCycleProgressPerSecond,
                0.0,
                1.0);
        } else if (key == save_schema::field::miningFuelSpent) {
            save.mining.fuelSpent = parseInt(value, save.mining.fuelSpent);
        } else if (key == save_schema::field::miningDrone) {
            parsePair(value, save.mining.droneX, save.mining.droneY);
        } else if (key == save_schema::field::miningReturnZone) {
            parsePair(value, save.mining.returnZoneX, save.mining.returnZoneY);
        } else if (key == save_schema::field::miningAim) {
            parsePair(value, save.mining.aimX, save.mining.aimY);
            const double aimDirX = save.mining.aimX - save.mining.droneX;
            const double aimDirY = save.mining.aimY - save.mining.droneY;
            const double aimDirLength = std::sqrt(aimDirX * aimDirX + aimDirY * aimDirY);
            if (aimDirLength > 0.0001) {
                save.mining.aimDirX = aimDirX / aimDirLength;
                save.mining.aimDirY = aimDirY / aimDirLength;
            }
        } else if (key == save_schema::field::miningHullHeading) {
            parsePair(value, save.mining.hullDirX, save.mining.hullDirY);
            const double hullLength = std::sqrt(save.mining.hullDirX * save.mining.hullDirX + save.mining.hullDirY * save.mining.hullDirY);
            if (hullLength > 0.0001) {
                save.mining.hullDirX /= hullLength;
                save.mining.hullDirY /= hullLength;
            } else {
                save.mining.hullDirX = 0.0;
                save.mining.hullDirY = 1.0;
            }
        } else if (key == save_schema::field::miningDrill) {
            parsePair(value, save.mining.drillHeat, save.mining.drillIntegrity);
        } else if (key == save_schema::field::miningDepth) {
            save.mining.depthZone = parseInt(value, save.mining.depthZone);
        } else if (key == save_schema::field::miningCargo) {
            save.mining.cargo = parseInt(value, save.mining.cargo);
        } else if (key == save_schema::field::miningMaterials) {
            save.mining.temporaryMaterials = parseMaterials(value);
        } else if (key == save_schema::field::miningArtifacts) {
            save.mining.temporaryArtifacts = parseArtifacts(value);
        } else if (key == save_schema::field::miningStowedCargo) {
            save.mining.stowedCargo = parseInt(value, save.mining.stowedCargo);
        } else if (key == save_schema::field::miningStowedMaterials) {
            save.mining.stowedMaterials = parseMaterials(value);
        } else if (key == save_schema::field::miningStowedArtifacts) {
            save.mining.stowedArtifacts = parseArtifacts(value);
        } else if (key == save_schema::field::miningHazard) {
            save.mining.hazardDelta = parseDouble(value, save.mining.hazardDelta);
        } else if (key == save_schema::field::miningPassiveDroneYield) {
            save.mining.passiveDroneYield = parseInt(value, save.mining.passiveDroneYield);
        } else if (key == save_schema::field::miningCellsBroken) {
            save.mining.cellsBroken = parseInt(value, save.mining.cellsBroken);
        } else if (key == save_schema::field::miningEnemies) {
            save.mining.enemies = parseMiningEnemies(value);
        } else if (key == save_schema::field::miningMiniDrones) {
            save.mining.miniDrones = parseMiningMiniDrones(value);
        } else if (key == save_schema::field::miningCombat) {
            const std::vector<std::string> fields = split(value, save_schema::crewFieldDelimiter);
            if (!fields.empty()) {
                save.mining.enemiesDefeated = parseInt(fields[0], save.mining.enemiesDefeated);
            }
            if (fields.size() > 1) {
                save.mining.defenseDamageDealt = parseDouble(fields[1], save.mining.defenseDamageDealt);
            }
            if (fields.size() > 2) {
                save.mining.enemyDamageTaken = parseDouble(fields[2], save.mining.enemyDamageTaken);
            }
            if (fields.size() > 3) {
                save.mining.areaControlDamageDealt = parseDouble(fields[3], save.mining.areaControlDamageDealt);
            }
            if (fields.size() > 4) {
                save.mining.reactiveArmorDamageDealt = parseDouble(fields[4], save.mining.reactiveArmorDamageDealt);
            }
            if (fields.size() > 5) {
                save.mining.environmentalShieldAbsorbed = parseDouble(fields[5], save.mining.environmentalShieldAbsorbed);
            }
            if (fields.size() > 6) {
                save.mining.elementalExposureSeconds = parseDouble(fields[6], save.mining.elementalExposureSeconds);
            }
            if (fields.size() > 7) {
                save.mining.movementSlowSeconds = parseDouble(fields[7], save.mining.movementSlowSeconds);
            }
            if (fields.size() > 8) {
                save.mining.movementSlowScale = parseDouble(fields[8], save.mining.movementSlowScale);
            }
        } else if (key == save_schema::field::miningArtifact) {
            save.mining.artifact = parseMiningArtifact(value);
        } else if (key == save_schema::field::miningTerrainSize) {
            parseMiningTerrainSize(value, save.mining.terrain);
        } else if (key == save_schema::field::miningTerrainCells) {
            parseMiningCells(value, save.mining.terrain);
        } else if (key == save_schema::field::unlocks) {
            save.unlockKeys = split(value, save_schema::listDelimiter);
        } else if (key == save_schema::field::blueprints) {
            save.blueprintProgress = parseInt(value, save.blueprintProgress);
        } else if (key == save_schema::field::materials) {
            save.materials = parseMaterials(value);
        } else if (key == save_schema::field::droneBaySlots) {
            save.droneBaySlots = parseInt(value, save.droneBaySlots);
        } else if (key == save_schema::field::ownedDrones) {
            save.ownedDroneIds = split(value, save_schema::listDelimiter);
        } else if (key == save_schema::field::equippedDrones) {
            save.equippedDroneIds = split(value, save_schema::listDelimiter);
        } else if (key == save_schema::field::droneUpgrades) {
            save.droneUpgrades = parseDroneUpgrades(value);
        } else if (key == save_schema::field::artifacts) {
            save.artifacts = parseArtifacts(value);
        } else if (key == save_schema::field::furthestTier) {
            save.furthestTier = parseInt(value, save.furthestTier);
        } else if (key == save_schema::field::shipsLost) {
            save.shipsLost = parseInt(value, save.shipsLost);
        } else if (key == save_schema::field::astronautsLost) {
            save.astronautsLost = parseInt(value, save.astronautsLost);
        } else if (key == save_schema::field::closestSurvivalMargin) {
            save.closestSurvivalMargin = parseDouble(value, save.closestSurvivalMargin);
        } else if (key == save_schema::field::closestSurvivalBurn) {
            save.closestSurvivalBurn = parseDouble(value, save.closestSurvivalBurn);
        } else if (key == save_schema::field::closestSurvivalFailurePoint) {
            save.closestSurvivalFailurePoint = parseDouble(value, save.closestSurvivalFailurePoint);
        } else if (key == save_schema::field::maxBurnDepth) {
            save.maxBurnDepth = parseDouble(value, save.maxBurnDepth);
        } else if (key == save_schema::field::maxPeakWarning) {
            save.maxPeakWarning = parseDouble(value, save.maxPeakWarning);
        } else if (key == save_schema::field::maxPeakAbortRisk) {
            save.maxPeakAbortRisk = parseDouble(value, save.maxPeakAbortRisk);
        } else if (key == save_schema::field::bestCreditDelta) {
            save.bestCreditDelta = parseDouble(value, save.bestCreditDelta);
        } else if (key == save_schema::field::worstCreditDelta) {
            save.worstCreditDelta = parseDouble(value, save.worstCreditDelta);
        } else if (key == save_schema::field::totalFlybyMisses) {
            save.totalFlybyMisses = parseInt(value, save.totalFlybyMisses);
        } else if (key == save_schema::field::totalFlybyGoods) {
            save.totalFlybyGoods = parseInt(value, save.totalFlybyGoods);
        } else if (key == save_schema::field::totalFlybyPerfects) {
            save.totalFlybyPerfects = parseInt(value, save.totalFlybyPerfects);
        } else if (key == save_schema::field::destinationAttempts) {
            save.destinationAttempts = parseInts(value);
        } else if (key == save_schema::field::destinationSuccesses) {
            save.destinationSuccesses = parseInts(value);
        } else if (key == save_schema::field::destinationFlybys) {
            save.destinationFlybys = parseInts(value);
        } else if (key == save_schema::field::destinationOrbits) {
            save.destinationOrbits = parseInts(value);
        } else if (key == save_schema::field::destinationLandings) {
            save.destinationLandings = parseInts(value);
        } else if (key == save_schema::field::memorials) {
            save.memorials = split(value, save_schema::textListDelimiter);
        } else if (key == save_schema::field::famousLaunches) {
            save.famousLaunches = split(value, save_schema::textListDelimiter);
        } else if (key == save_schema::field::crew) {
            save.crew = parseCrew(value);
        }
    }

    return save;
}

} // namespace rocket
