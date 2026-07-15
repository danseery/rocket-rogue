#include "core/MiningProgression.h"

#include "core/ContentIds.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace rocket {

namespace {

struct BandContract {
    MiningAct act;
    MiningProgressionBand band;
    MiningRewardBudget rewards;
    int referenceSlots;
    int referenceMark;
    int maxActiveEnemies;
    std::string_view complication;
    std::string_view tutorial;
    std::string_view mineralAvailability;
    std::string_view knownEnemyRoles;
    std::string_view recommendedCounters;
};

constexpr std::array<BandContract, miningFirstClearProgressCount> bandContracts {{
    {MiningAct::ActOne, MiningProgressionBand::Learn, {0, 0, 0, 0}, 0, 0, 0,
        "Core excavation", "Learn the rig: drill a route, watch oxygen and fuel, then return to the ship.",
        "Common minerals", "No hostile contacts", "Main rig"},
    {MiningAct::ActOne, MiningProgressionBand::Combine, {1, 0, 1, 0}, 2, 1, 0,
        "Heat and integrity", "Heat, integrity, recoil, repairs, and cargo weight now matter together.",
        "Common minerals; a trace rare deposit", "No hostile contacts", "Mining or Resource Mk I"},
    {MiningAct::ActOne, MiningProgressionBand::Pressure, {1, 0, 2, 0}, 3, 1, 0,
        "Elemental terrain", "Thermal and Cryo pockets reward scouting and Hazard support.",
        "Limited rare minerals", "No hostile contacts", "Survey and Hazard Mk I"},
    {MiningAct::ActOne, MiningProgressionBand::Mastery, {2, 0, 3, 0}, 4, 2, 0,
        "Toxic combinations", "Master the complete noncombat mining rules before hostile contact begins.",
        "Limited rare minerals; no exotic minerals", "No hostile contacts", "Environmental drones up to Mk II"},

    {MiningAct::ActTwo, MiningProgressionBand::Learn, {1, 0, 2, 0}, 3, 1, 2,
        "Ant melee contact", "Passive Attack and Defense drones handle combat while you keep piloting the rig.",
        "Limited rare minerals", "Ant melee", "Attack and Defense Mk I"},
    {MiningAct::ActTwo, MiningProgressionBand::Combine, {2, 0, 3, 0}, 3, 1, 4,
        "Ranged and armored contact", "Flying enemies, Beetles, and vault routes force a utility choice.",
        "Improved rare minerals", "Ant melee, Flying ranged, armored Beetles", "Attack, Defense, and one utility drone"},
    {MiningAct::ActTwo, MiningProgressionBand::Pressure, {2, 0, 4, 1}, 4, 2, 6,
        "Elementals and hives", "Thermal and Cryo enemies test combat and Hazard-drone pairings.",
        "Strong rare minerals; a possible exotic trace", "Elementals and hive pressure", "Combat and Hazard Mk II synergies"},
    {MiningAct::ActTwo, MiningProgressionBand::Mastery, {3, 1, 5, 1}, 5, 2, 8,
        "Minibosses and spawners", "Toxic Elementals and reinforcement sources test your first signature build.",
        "Rich rare minerals; one exotic mineral", "Full Act 2 roster, minibosses, one spawner", "A focused Mk II signature build"},

    {MiningAct::ActThree, MiningProgressionBand::Learn, {3, 1, 5, 1}, 5, 3, 6,
        "Burrowers and Radiation", "Mammal burrowers and Radiation arrive separately before they combine.",
        "Rich rare minerals; one exotic mineral", "Burrowing Mammals and Radiation threats", "Hazard Mk III and Defense"},
    {MiningAct::ActThree, MiningProgressionBand::Combine, {4, 1, 6, 2}, 5, 3, 8,
        "Mixed attack lanes", "Mixed affinities, spawners, and miniboss lanes demand a complete formation.",
        "Abundant rare minerals; limited exotic minerals", "Mixed affinities, spawners, and minibosses", "Mixed Mk II and Mk III signature build"},
    {MiningAct::ActThree, MiningProgressionBand::Pressure, {5, 2, 7, 3}, 6, 3, 11,
        "Boss chambers", "Boss chambers combine reinforcement pressure with overlapping attack lanes.",
        "Abundant rare and exotic minerals", "Bosses with combined spawner pressure", "Six-slot Mk III specialization"},
    {MiningAct::ActThree, MiningProgressionBand::Mastery, {6, 3, 8, 4}, 6, 3, 14,
        "Full-spectrum pressure", "The full roster, every affinity, bosses, and multiple spawners now combine.",
        "Maximum controlled rare and exotic yield", "Full roster, all affinities, bosses, multiple spawners", "Six-slot Full Spectrum or equivalent focused build"},
}};

MiningAct normalizedAct(MiningAct act)
{
    switch (act) {
    case MiningAct::ActOne:
    case MiningAct::ActTwo:
    case MiningAct::ActThree:
        return act;
    }
    return MiningAct::ActOne;
}

int actOrdinal(MiningAct act)
{
    return static_cast<int>(normalizedAct(act)) - 1;
}

int bandOrdinal(MiningProgressionBand band)
{
    switch (band) {
    case MiningProgressionBand::Learn:
        return 0;
    case MiningProgressionBand::Combine:
        return 1;
    case MiningProgressionBand::Pressure:
        return 2;
    case MiningProgressionBand::Mastery:
        return 3;
    }
    return 0;
}

const BandContract& contractFor(MiningAct act, MiningProgressionBand band)
{
    return bandContracts[static_cast<std::size_t>(actOrdinal(act) * static_cast<int>(miningProgressionBandCount) + bandOrdinal(band))];
}

template <typename Enum, std::size_t Size>
void allow(std::array<bool, Size>& values, Enum value)
{
    const auto index = static_cast<std::size_t>(value);
    if (index < values.size()) {
        values[index] = true;
    }
}

template <typename Enum, std::size_t Size>
void deny(std::array<bool, Size>& values, Enum value)
{
    const auto index = static_cast<std::size_t>(value);
    if (index < values.size()) {
        values[index] = false;
    }
}

void addReferenceRole(MiningReferenceDroneCapability& reference, MiniDroneRole role)
{
    if (reference.roleCount < reference.roles.size()) {
        reference.roles[reference.roleCount++] = role;
    }
}

void applyActOneProgression(MiningArenaRules& rules, int difficulty)
{
    rules.mechanics.movement = true;
    rules.mechanics.drilling = true;
    rules.mechanics.returnZone = true;
    allow(rules.allowedMaterials, MiningCellMaterial::Empty);
    allow(rules.allowedMaterials, MiningCellMaterial::Regolith);
    allow(rules.allowedMaterials, MiningCellMaterial::CommonOre);
    allow(rules.allowedMaterials, MiningCellMaterial::Bedrock);
    allow(rules.allowedAffinities, MiningElementalAffinity::None);
    allow(rules.allowedRoomFeatures, MiningCellFeature::None);
    allow(rules.allowedRoomFeatures, MiningCellFeature::MainTunnel);

    if (difficulty >= 2) {
        rules.mechanics.fogAndScanner = true;
        rules.mechanics.oxygenAndFuel = true;
    }
    if (difficulty >= 3) {
        allow(rules.allowedMaterials, MiningCellMaterial::HardRock);
        allow(rules.allowedRoomFeatures, MiningCellFeature::BranchTunnel);
    }
    if (difficulty >= 4) {
        rules.mechanics.drillHeat = true;
        rules.mechanics.drillIntegrity = true;
        rules.mechanics.contactRebound = true;
        rules.mechanics.fieldRepairs = true;
        allow(rules.allowedMaterials, MiningCellMaterial::RareOre);
    }
    if (difficulty >= 5) {
        rules.mechanics.cargoDrag = true;
    }
    if (difficulty >= 7) {
        rules.mechanics.environmentalHazards = true;
        rules.mechanics.siteAndDepthVariation = true;
        allow(rules.allowedMaterials, MiningCellMaterial::HazardPocket);
        allow(rules.allowedAffinities, MiningElementalAffinity::Thermal);
        allow(rules.allowedAffinities, MiningElementalAffinity::Cryo);
    }
    if (difficulty >= 8) {
        rules.mechanics.artifactRecovery = true;
        rules.mechanics.artifactTethering = true;
        allow(rules.allowedMaterials, MiningCellMaterial::ArtifactCache);
    }
    if (difficulty >= 9) {
        allow(rules.allowedAffinities, MiningElementalAffinity::Toxic);
    }
}

void applyActTwoProgression(MiningArenaRules& rules, int difficulty)
{
    applyActOneProgression(rules, 10);
    rules.allowedAffinities[static_cast<std::size_t>(MiningElementalAffinity::Toxic)] = false;
    rules.mechanics.passiveDroneCombat = true;
    allow(rules.allowedEnemyTypes, MiningEnemyType::Ant);
    allow(rules.allowedRoomFeatures, MiningCellFeature::EncounterZone);

    if (difficulty >= 4) {
        allow(rules.allowedEnemyTypes, MiningEnemyType::Flying);
        allow(rules.allowedRoomFeatures, MiningCellFeature::TreasureVault);
    }
    if (difficulty >= 5) {
        allow(rules.allowedEnemyTypes, MiningEnemyType::Beetle);
    }
    if (difficulty >= 7) {
        allow(rules.allowedMaterials, MiningCellMaterial::ExoticVein);
        allow(rules.allowedEnemyTypes, MiningEnemyType::Elemental);
        allow(rules.allowedRoomFeatures, MiningCellFeature::HiveNest);
    }
    if (difficulty >= 9) {
        allow(rules.allowedAffinities, MiningElementalAffinity::Toxic);
        allow(rules.allowedRoomFeatures, MiningCellFeature::MinibossLair);
    }
    if (difficulty >= 10) {
        allow(rules.allowedEnemyTypes, MiningEnemyType::Spawner);
        rules.maxSpawners = 1;
    }
}

void applyActThreeProgression(MiningArenaRules& rules, int difficulty)
{
    applyActTwoProgression(rules, 10);
    deny(rules.allowedEnemyTypes, MiningEnemyType::Spawner);
    deny(rules.allowedRoomFeatures, MiningCellFeature::MinibossLair);
    rules.maxSpawners = 0;
    allow(rules.allowedEnemyTypes, MiningEnemyType::Mammal);
    allow(rules.allowedRoomFeatures, MiningCellFeature::OrganicBurrow);
    if (difficulty >= 2) {
        allow(rules.allowedAffinities, MiningElementalAffinity::Radiation);
    }
    if (difficulty >= 4) {
        allow(rules.allowedEnemyTypes, MiningEnemyType::Spawner);
        allow(rules.allowedRoomFeatures, MiningCellFeature::MinibossLair);
        rules.maxSpawners = 1;
    }
    if (difficulty >= 7) {
        allow(rules.allowedRoomFeatures, MiningCellFeature::BossChamber);
    }
    if (difficulty >= 9) {
        rules.maxSpawners = 2;
    }
}

void setReferenceRoles(MiningArenaRules& rules)
{
    const MiningAct act = rules.request.act;
    const MiningProgressionBand band = rules.band;
    MiningReferenceDroneCapability& reference = rules.referenceDrones;

    if (act == MiningAct::ActOne) {
        if (band == MiningProgressionBand::Combine) {
            addReferenceRole(reference, MiniDroneRole::Mining);
            addReferenceRole(reference, MiniDroneRole::Resource);
        } else if (band == MiningProgressionBand::Pressure) {
            addReferenceRole(reference, MiniDroneRole::Survey);
            addReferenceRole(reference, MiniDroneRole::Hazard);
        } else if (band == MiningProgressionBand::Mastery) {
            addReferenceRole(reference, MiniDroneRole::Mining);
            addReferenceRole(reference, MiniDroneRole::Resource);
            addReferenceRole(reference, MiniDroneRole::Survey);
            addReferenceRole(reference, MiniDroneRole::Hazard);
        }
        return;
    }

    addReferenceRole(reference, MiniDroneRole::Attack);
    addReferenceRole(reference, MiniDroneRole::Defense);
    if (act == MiningAct::ActTwo && band == MiningProgressionBand::Learn) {
        addReferenceRole(reference, MiniDroneRole::Mining);
        return;
    }

    addReferenceRole(reference, MiniDroneRole::Hazard);
    if (reference.slots >= 4) {
        addReferenceRole(reference, MiniDroneRole::Survey);
    }
    if (reference.slots >= 5) {
        addReferenceRole(reference, MiniDroneRole::Mining);
    }
    if (reference.slots >= 6) {
        addReferenceRole(reference, MiniDroneRole::Resource);
    }
}

std::uint64_t mix64(std::uint64_t value)
{
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

std::uint64_t stableStringHash(std::string_view text)
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char character : text) {
        hash ^= character;
        hash *= 1099511628211ULL;
    }
    return hash;
}

template <typename Enum, std::size_t Size>
bool isAllowed(const std::array<bool, Size>& values, Enum value)
{
    const auto index = static_cast<std::size_t>(value);
    return index < values.size() && values[index];
}

} // namespace

MiningProgressionBand miningProgressionBandForDifficulty(int difficulty)
{
    const int normalized = std::clamp(difficulty, 1, 10);
    if (normalized <= 3) {
        return MiningProgressionBand::Learn;
    }
    if (normalized <= 6) {
        return MiningProgressionBand::Combine;
    }
    if (normalized <= 8) {
        return MiningProgressionBand::Pressure;
    }
    return MiningProgressionBand::Mastery;
}

std::string_view miningActName(MiningAct act)
{
    switch (normalizedAct(act)) {
    case MiningAct::ActOne:
        return "Act 1";
    case MiningAct::ActTwo:
        return "Act 2";
    case MiningAct::ActThree:
        return "Act 3";
    }
    return "Act 1";
}

std::string_view miningProgressionBandName(MiningProgressionBand band)
{
    switch (band) {
    case MiningProgressionBand::Learn:
        return "Learn";
    case MiningProgressionBand::Combine:
        return "Combine";
    case MiningProgressionBand::Pressure:
        return "Pressure";
    case MiningProgressionBand::Mastery:
        return "Mastery";
    }
    return "Learn";
}

MiningArenaRules resolveMiningArenaRules(const MiningArenaRequest& rawRequest)
{
    MiningArenaRules rules;
    rules.request = rawRequest;
    rules.request.act = normalizedAct(rawRequest.act);
    rules.request.difficulty = std::clamp(rawRequest.difficulty, 1, 10);
    rules.band = miningProgressionBandForDifficulty(rules.request.difficulty);

    const BandContract& contract = contractFor(rules.request.act, rules.band);
    rules.rewardBudget = contract.rewards;
    rules.maxActiveEnemies = contract.maxActiveEnemies;
    rules.complication = contract.complication;
    rules.tutorialCallout = contract.tutorial;
    rules.mineralAvailability = contract.mineralAvailability;
    rules.knownEnemyRoles = contract.knownEnemyRoles;
    rules.recommendedCounters = contract.recommendedCounters;
    rules.referenceDrones.slots = contract.referenceSlots;
    rules.referenceDrones.maximumMark = contract.referenceMark;
    rules.referenceDrones.summary = contract.recommendedCounters;

    const double levelOffset = static_cast<double>(rules.request.difficulty - 1);
    switch (rules.request.act) {
    case MiningAct::ActOne:
        applyActOneProgression(rules, rules.request.difficulty);
        rules.terrainToughnessScale = 0.75 + 0.05 * levelOffset;
        break;
    case MiningAct::ActTwo:
        applyActTwoProgression(rules, rules.request.difficulty);
        rules.terrainToughnessScale = 1.10 + 0.06 * levelOffset;
        rules.enemyHealthScale = 0.70 + 0.08 * levelOffset;
        rules.enemyDamageScale = 0.65 + 0.07 * levelOffset;
        break;
    case MiningAct::ActThree:
        applyActThreeProgression(rules, rules.request.difficulty);
        rules.terrainToughnessScale = 1.45 + 0.07 * levelOffset;
        rules.enemyHealthScale = 1.25 + 0.10 * levelOffset;
        rules.enemyDamageScale = 1.10 + 0.08 * levelOffset;
        break;
    }

    setReferenceRoles(rules);
    return rules;
}

MiningCampaignProgression resolveCampaignMiningProgression(
    GameChapter chapter,
    std::string_view destinationId,
    int surfaceDepth,
    int completedHostileSorties)
{
    MiningCampaignProgression progression;
    const int depth = std::max(0, surfaceDepth);

    if (chapter == GameChapter::ProvingGround || destinationId == content::destination::earthOrbit) {
        return progression;
    }

    progression.miningAvailable = true;
    switch (chapter) {
    case GameChapter::ProvingGround:
        progression.miningAvailable = false;
        break;
    case GameChapter::LunarProgram:
        progression.act = MiningAct::ActOne;
        progression.minimumDifficulty = 1;
        progression.maximumDifficulty = 3;
        break;
    case GameChapter::RedFrontier:
        progression.act = MiningAct::ActOne;
        progression.minimumDifficulty = 4;
        progression.maximumDifficulty = 6;
        break;
    case GameChapter::Breakthrough:
        progression.act = MiningAct::ActOne;
        progression.minimumDifficulty = 7;
        progression.maximumDifficulty = 8;
        break;
    case GameChapter::Straylight:
        progression.act = MiningAct::ActOne;
        progression.minimumDifficulty = 9;
        progression.maximumDifficulty = 10;
        break;
    case GameChapter::Arkfall:
        progression.act = MiningAct::ActTwo;
        progression.minimumDifficulty = 1;
        progression.maximumDifficulty = 3;
        break;
    case GameChapter::LastCampfire:
        progression.act = MiningAct::ActTwo;
        progression.minimumDifficulty = 4 + std::clamp(completedHostileSorties - 1, 0, 2);
        progression.maximumDifficulty = 10;
        progression.difficulty = std::clamp(progression.minimumDifficulty + std::min(depth, 4), progression.minimumDifficulty, progression.maximumDifficulty);
        return progression;
    case GameChapter::VoidCompass:
        progression.act = MiningAct::ActThree;
        progression.minimumDifficulty = 1;
        progression.maximumDifficulty = 4;
        break;
    case GameChapter::Ouroboros:
        progression.act = MiningAct::ActThree;
        progression.minimumDifficulty = 5;
        progression.maximumDifficulty = 8;
        break;
    case GameChapter::Ascent:
        progression.act = MiningAct::ActThree;
        progression.minimumDifficulty = 9;
        progression.maximumDifficulty = 10;
        break;
    }

    progression.difficulty = std::clamp(progression.minimumDifficulty + depth, progression.minimumDifficulty, progression.maximumDifficulty);
    return progression;
}

std::uint64_t deriveMiningArenaSeed(
    std::uint64_t campaignSeed,
    std::string_view destinationId,
    int landingOrdinal,
    int surfaceDepth)
{
    std::uint64_t seed = mix64(campaignSeed);
    seed ^= mix64(stableStringHash(destinationId));
    seed ^= mix64(static_cast<std::uint64_t>(std::max(0, landingOrdinal)) + 0x4c414e44494e47ULL);
    seed ^= mix64(static_cast<std::uint64_t>(std::max(0, surfaceDepth)) + 0x4445505448ULL);
    seed = mix64(seed);
    return seed == 0 ? 0xC0DEC0FFEEULL : seed;
}

MiningArenaRequest campaignMiningArenaRequest(
    GameChapter chapter,
    std::string_view destinationId,
    int surfaceDepth,
    int completedHostileSorties,
    std::uint64_t campaignSeed,
    int landingOrdinal)
{
    const MiningCampaignProgression progression = resolveCampaignMiningProgression(
        chapter,
        destinationId,
        surfaceDepth,
        completedHostileSorties);
    return {
        progression.act,
        progression.difficulty,
        deriveMiningArenaSeed(campaignSeed, destinationId, landingOrdinal, surfaceDepth),
    };
}

MiningRewardBudget effectiveMiningRewardBudget(const MiningArenaRules& rules, bool firstClearFulfilled)
{
    if (!firstClearFulfilled) {
        return rules.rewardBudget;
    }
    return {
        0,
        0,
        static_cast<int>(std::ceil(static_cast<double>(rules.rewardBudget.rareCap) * 0.5)),
        rules.rewardBudget.exoticCap / 2,
    };
}

std::size_t miningFirstClearProgressIndex(MiningAct act, MiningProgressionBand band)
{
    return static_cast<std::size_t>(actOrdinal(act) * static_cast<int>(miningProgressionBandCount) + bandOrdinal(band));
}

const MiningFirstClearProgress& miningFirstClearProgress(
    const MetaProgress& meta,
    MiningAct act,
    MiningProgressionBand band)
{
    return meta.miningFirstClearProgress[miningFirstClearProgressIndex(act, band)];
}

bool miningFirstClearFulfilled(const MetaProgress& meta, const MiningArenaRules& rules)
{
    const MiningFirstClearProgress& progress = miningFirstClearProgress(meta, rules.request.act, rules.band);
    return progress.rareBanked >= rules.rewardBudget.rareGuarantee
        && progress.exoticBanked >= rules.rewardBudget.exoticGuarantee;
}

void creditBankedMiningFirstClearRewards(
    MetaProgress& meta,
    const MiningArenaRules& rules,
    int rareBanked,
    int exoticBanked)
{
    MiningFirstClearProgress& progress = meta.miningFirstClearProgress[
        miningFirstClearProgressIndex(rules.request.act, rules.band)];
    progress.rareBanked = std::clamp(
        progress.rareBanked + std::max(0, rareBanked),
        0,
        rules.rewardBudget.rareGuarantee);
    progress.exoticBanked = std::clamp(
        progress.exoticBanked + std::max(0, exoticBanked),
        0,
        rules.rewardBudget.exoticGuarantee);
}

bool miningMaterialAllowed(const MiningArenaRules& rules, MiningCellMaterial material)
{
    return isAllowed(rules.allowedMaterials, material);
}

bool miningEnemyAllowed(const MiningArenaRules& rules, MiningEnemyType enemy)
{
    return isAllowed(rules.allowedEnemyTypes, enemy);
}

bool miningAffinityAllowed(const MiningArenaRules& rules, MiningElementalAffinity affinity)
{
    return isAllowed(rules.allowedAffinities, affinity);
}

bool miningRoomFeatureAllowed(const MiningArenaRules& rules, MiningCellFeature feature)
{
    return isAllowed(rules.allowedRoomFeatures, feature);
}

} // namespace rocket
