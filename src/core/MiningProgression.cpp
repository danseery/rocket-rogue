#include "core/MiningProgression.h"

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

void allowGate(MiningArenaRules& rules, MiningGateType type)
{
    allow(rules.allowedGateTypes, type);
}

void applyActOneProgression(MiningArenaRules& rules, int difficulty)
{
    rules.mechanics.movement = true;
    rules.mechanics.drilling = true;
    rules.mechanics.returnZone = true;
    // Fuel, oxygen, and physical load are the foundational surface pressures,
    // including on the first Moon expedition.  The scanner remains contextual
    // until its authored anomaly appears, while heat and integrity arrive later.
    rules.mechanics.oxygenAndFuel = true;
    allow(rules.allowedMaterials, MiningCellMaterial::Empty);
    allow(rules.allowedMaterials, MiningCellMaterial::Regolith);
    allow(rules.allowedMaterials, MiningCellMaterial::CommonOre);
    allow(rules.allowedMaterials, MiningCellMaterial::Bedrock);
    allow(rules.allowedAffinities, MiningElementalAffinity::None);
    allow(rules.allowedRoomFeatures, MiningCellFeature::None);
    allow(rules.allowedRoomFeatures, MiningCellFeature::MainTunnel);

    if (difficulty >= 2) {
        rules.mechanics.fogAndScanner = true;
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
        // This pressure tier introduces the full oxygen-and-fuel mining loop. These are
        // stamped as sparse, deterministic regolith replacements by the
        // terrain generator rather than rolled as ordinary ore.
        allow(rules.allowedMaterials, MiningCellMaterial::FuelPocket);
        allow(rules.allowedMaterials, MiningCellMaterial::OxygenPocket);
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
        allowGate(rules, MiningGateType::HazardCocoon);
        allowGate(rules, MiningGateType::SurveyTriangulation);
        allowGate(rules, MiningGateType::FragileExcavation);
    }
    if (difficulty >= 9) {
        allow(rules.allowedAffinities, MiningElementalAffinity::Toxic);
        allowGate(rules, MiningGateType::HeavyTow);
        allowGate(rules, MiningGateType::EnduranceVault);
    }
    rules.maximumGateLocks = 1;
}

void applyActTwoProgression(MiningArenaRules& rules, int difficulty)
{
    applyActOneProgression(rules, 10);
    rules.allowedAffinities[static_cast<std::size_t>(MiningElementalAffinity::Toxic)] = false;
    rules.mechanics.passiveDroneCombat = true;
    allow(rules.allowedEnemyTypes, MiningEnemyType::Ant);
    allow(rules.allowedRoomFeatures, MiningCellFeature::EncounterZone);

    if (difficulty >= 2) {
        allowGate(rules, MiningGateType::EnemySealedChamber);
    }

    if (difficulty >= 4) {
        allow(rules.allowedEnemyTypes, MiningEnemyType::Flying);
        allow(rules.allowedRoomFeatures, MiningCellFeature::TreasureVault);
        allowGate(rules, MiningGateType::ShieldCorridor);
    }
    if (difficulty >= 5) {
        allow(rules.allowedEnemyTypes, MiningEnemyType::Beetle);
    }
    if (difficulty >= 7) {
        allow(rules.allowedMaterials, MiningCellMaterial::ExoticVein);
        allow(rules.allowedEnemyTypes, MiningEnemyType::Elemental);
        allow(rules.allowedRoomFeatures, MiningCellFeature::HiveNest);
        allowGate(rules, MiningGateType::CompoundVault);
    }
    if (difficulty >= 9) {
        allow(rules.allowedAffinities, MiningElementalAffinity::Toxic);
        allow(rules.allowedRoomFeatures, MiningCellFeature::MinibossLair);
    }
    if (difficulty >= 10) {
        allow(rules.allowedEnemyTypes, MiningEnemyType::Spawner);
        rules.maxSpawners = 1;
    }
    // Keep Act 2's opening encounter legible: a second simultaneous lock is
    // introduced only with the level-7 compound-vault pressure tier.
    rules.maximumGateLocks = difficulty >= 7 ? 2 : 1;
}

void applyActThreeProgression(MiningArenaRules& rules, int difficulty)
{
    applyActTwoProgression(rules, 10);
    deny(rules.allowedEnemyTypes, MiningEnemyType::Spawner);
    deny(rules.allowedRoomFeatures, MiningCellFeature::MinibossLair);
    rules.maxSpawners = 0;
    allow(rules.allowedEnemyTypes, MiningEnemyType::Mammal);
    allow(rules.allowedRoomFeatures, MiningCellFeature::OrganicBurrow);
    allowGate(rules, MiningGateType::BurrowBreach);
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
    rules.maximumGateLocks = 3;
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

MiningElementalAffinity strongestAllowedHazardAffinity(const MiningArenaRules& rules)
{
    for (const MiningElementalAffinity affinity : {
             MiningElementalAffinity::Radiation,
             MiningElementalAffinity::Toxic,
             MiningElementalAffinity::Cryo,
             MiningElementalAffinity::Thermal}) {
        if (isAllowed(rules.allowedAffinities, affinity)) {
            return affinity;
        }
    }
    return MiningElementalAffinity::None;
}

void applyThermalLavaSiteRules(MiningArenaRules& rules)
{
    rules.mechanics.fogAndScanner = true;
    rules.mechanics.oxygenAndFuel = true;
    rules.mechanics.drillHeat = true;
    rules.mechanics.drillIntegrity = true;
    rules.mechanics.contactRebound = true;
    rules.mechanics.fieldRepairs = true;
    rules.mechanics.cargoDrag = true;
    rules.mechanics.environmentalHazards = true;
    rules.mechanics.artifactRecovery = true;
    rules.mechanics.artifactTethering = true;
    rules.mechanics.siteAndDepthVariation = true;
    rules.mechanics.passiveDroneCombat = false;

    rules.allowedMaterials.fill(false);
    allow(rules.allowedMaterials, MiningCellMaterial::Empty);
    allow(rules.allowedMaterials, MiningCellMaterial::Regolith);
    // Common Ore is a post-treatment result. Thermal sites do not place it
    // directly in their terrain pass.
    allow(rules.allowedMaterials, MiningCellMaterial::CommonOre);
    allow(rules.allowedMaterials, MiningCellMaterial::ArtifactCache);
    allow(rules.allowedMaterials, MiningCellMaterial::HazardPocket);
    allow(rules.allowedMaterials, MiningCellMaterial::Bedrock);

    rules.allowedAffinities.fill(false);
    allow(rules.allowedAffinities, MiningElementalAffinity::None);
    allow(rules.allowedAffinities, MiningElementalAffinity::Thermal);
    rules.allowedEnemyTypes.fill(false);
    rules.maxActiveEnemies = 0;
    rules.maxSpawners = 0;

    rules.allowedGateTypes.fill(false);
    allowGate(rules, MiningGateType::HazardCocoon);
    rules.maximumGateLocks = 1;
    rules.rewardBudget = {};

    rules.referenceDrones = {};
    rules.referenceDrones.slots = 2;
    rules.referenceDrones.maximumMark = 1;
    addReferenceRole(rules.referenceDrones, MiniDroneRole::Hazard);
    rules.referenceDrones.summary = "Hazard Drone Mk I";
    rules.complication = "Layered thermal seal";
    rules.tutorialCallout =
        "Inert regolith hides no resources. Cool marked thermal seams, then drill the gray Common Ore.";
    rules.mineralAvailability = "Common Ore inside Thermal seams only";
    rules.knownEnemyRoles = "No hostile contacts";
    rules.recommendedCounters = "Hazard Drone Mk I";
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

MiningArenaRules resolveMiningSiteArenaRules(
    const MiningArenaRequest& request,
    const MiningSiteDefinition& site)
{
    MiningArenaRequest resolvedRequest = site.arena;
    if (resolvedRequest.seed == 0) {
        resolvedRequest.seed = request.seed;
    }
    if (resolvedRequest.gateOverride == MiningGateType::None &&
        site.gateType != MiningGateType::None) {
        resolvedRequest.gateOverrideEnabled = true;
        resolvedRequest.gateOverride = site.gateType;
    }

    MiningArenaRules rules = resolveMiningArenaRules(resolvedRequest);
    if (site.biome == MiningSiteBiome::ThermalLava) {
        applyThermalLavaSiteRules(rules);
    }
    if (site.gateType != MiningGateType::None) {
        rules.request.gateOverrideEnabled = true;
        rules.request.gateOverride = site.gateType;
        allowGate(rules, site.gateType);
    }
    return rules;
}

std::string_view miningGateName(MiningGateType type)
{
    switch (type) {
    case MiningGateType::None: return "None";
    case MiningGateType::HazardCocoon: return "Hazard Cocoon";
    case MiningGateType::EnemySealedChamber: return "Enemy-Sealed Chamber";
    case MiningGateType::SurveyTriangulation: return "Survey Triangulation";
    case MiningGateType::FragileExcavation: return "Fragile Excavation";
    case MiningGateType::HeavyTow: return "Heavy Tow";
    case MiningGateType::EnduranceVault: return "Endurance Vault";
    case MiningGateType::ShieldCorridor: return "Shield Corridor";
    case MiningGateType::BurrowBreach: return "Burrow Breach";
    case MiningGateType::CompoundVault: return "Compound Vault";
    }
    return "None";
}

std::string_view miningGateStateName(MiningGateState state)
{
    switch (state) {
    case MiningGateState::None: return "None";
    case MiningGateState::Locked: return "Locked";
    case MiningGateState::InProgress: return "In progress";
    case MiningGateState::Open: return "Open";
    case MiningGateState::Completed: return "Completed";
    }
    return "None";
}

bool miningGateAllowed(const MiningArenaRules& rules, MiningGateType type)
{
    return isAllowed(rules.allowedGateTypes, type);
}

MiningGateType selectMiningGateType(const MiningArenaRules& rules)
{
    if (rules.request.gateOverrideEnabled) {
        return rules.request.gateOverride == MiningGateType::None || miningGateAllowed(rules, rules.request.gateOverride)
            ? rules.request.gateOverride
            : MiningGateType::None;
    }
    std::array<MiningGateType, miningGateTypeCount> candidates {};
    std::size_t count = 0;
    for (int raw = static_cast<int>(MiningGateType::HazardCocoon);
         raw <= static_cast<int>(MiningGateType::CompoundVault);
         ++raw) {
        const auto type = static_cast<MiningGateType>(raw);
        if (miningGateAllowed(rules, type) && type != MiningGateType::CompoundVault) {
            candidates[count++] = type;
        }
    }
    if (count == 0) {
        return MiningGateType::None;
    }
    const std::size_t index = static_cast<std::size_t>(mix64(rules.request.seed ^ 0x4741544553495445ULL) % count);
    return candidates[index];
}

MiningGateDefinition resolveMiningGateDefinition(
    const MiningArenaRules& rules,
    MiningGateType type,
    bool compatibilityCritical)
{
    MiningGateDefinition gate;
    gate.type = type;
    gate.compatibilityCritical = compatibilityCritical;
    gate.name = miningGateName(type);
    switch (type) {
    case MiningGateType::None:
        break;
    case MiningGateType::HazardCocoon:
        gate.requiresHazardTreatment = true;
        gate.hazardAffinity = strongestAllowedHazardAffinity(rules);
        gate.requiredHazardMark = gate.hazardAffinity == MiningElementalAffinity::Radiation ? 3
            : (gate.hazardAffinity == MiningElementalAffinity::Toxic ? 2 : 1);
        gate.requiredCapability = gate.requiredHazardMark == 1 ? "Hazard Drone Mk I" : (gate.requiredHazardMark == 2 ? "Hazard Drone Mk II" : "Hazard Drone Mk III");
        gate.alternatives = "Hard protection lock: every shell tile must be treated.";
        break;
    case MiningGateType::EnemySealedChamber:
        gate.requiresEnemyClearance = true;
        gate.requiredCapability = "Enough passive combat strength to clear the assigned encounter";
        gate.alternatives = "Any Attack, Defense, terrain, and utility combination that wins the room.";
        break;
    case MiningGateType::SurveyTriangulation:
        gate.requiresSurveyTriangulation = true;
        gate.requiredSurveyOrigins = 3;
        gate.requiredCapability = "Three scanner pulses from distinct origins";
        gate.alternatives = "Survey Drones scan efficiently; the rig can reposition and pulse manually.";
        break;
    case MiningGateType::FragileExcavation:
        gate.fragileArtifact = true;
        gate.requiredCapability = "Controlled surrounding excavation";
        gate.alternatives = "Prospector Support Drone, scanner information, low rebound, or careful manual excavation.";
        break;
    case MiningGateType::HeavyTow:
        gate.heavyTow = true;
        gate.requiredCapability = "Tow support and a clear return route";
        gate.alternatives = "Resource support, tow upgrades, empty cargo, or a wide direct tunnel.";
        break;
    case MiningGateType::EnduranceVault:
        gate.endurancePlacement = true;
        gate.requiredCapability = "Extended oxygen range";
        gate.alternatives = "Resource Drone, oxygen upgrades, shortcuts, or a pre-cleared route.";
        break;
    case MiningGateType::ShieldCorridor:
        gate.shieldCorridor = true;
        gate.requiredCapability = "Survive or clear a ranged extraction lane";
        gate.alternatives = "Defense screen, Attack clearance, or terrain cover and a safer tunnel.";
        break;
    case MiningGateType::BurrowBreach:
        gate.burrowBreach = true;
        gate.requiredCapability = "Open the marked bedrock breach";
        gate.alternatives = "Lure a Mammal through it, or use Survey support to find the long route.";
        break;
    case MiningGateType::CompoundVault:
        gate.requiresHazardTreatment = true;
        gate.requiresEnemyClearance = true;
        gate.hazardAffinity = strongestAllowedHazardAffinity(rules);
        gate.requiredHazardMark = gate.hazardAffinity == MiningElementalAffinity::Radiation ? 3
            : (gate.hazardAffinity == MiningElementalAffinity::Toxic ? 2 : 1);
        gate.endurancePlacement = rules.request.act == MiningAct::ActThree;
        gate.heavyTow = rules.request.act == MiningAct::ActThree && rules.request.difficulty >= 9;
        gate.requiredCapability = rules.request.act == MiningAct::ActThree
            ? "Hazard treatment, encounter clearance, and extraction endurance"
            : "Hazard treatment plus encounter clearance";
        gate.alternatives = "A compound protection lock using previously taught systems.";
        break;
    }
    return gate;
}

MiningSiteProgress* pendingCompatibilityMiningSite(
    MetaProgress& meta,
    std::string_view destinationId)
{
    const auto site = std::find_if(meta.miningSites.begin(), meta.miningSites.end(), [&](const MiningSiteProgress& candidate) {
        return candidate.legacyMigrated && !candidate.completed &&
            candidate.destinationId == destinationId;
    });
    return site == meta.miningSites.end() ? nullptr : &*site;
}

const MiningSiteProgress* pendingCompatibilityMiningSite(
    const MetaProgress& meta,
    std::string_view destinationId)
{
    const auto site = std::find_if(meta.miningSites.begin(), meta.miningSites.end(), [&](const MiningSiteProgress& candidate) {
        return candidate.legacyMigrated && !candidate.completed &&
            candidate.destinationId == destinationId;
    });
    return site == meta.miningSites.end() ? nullptr : &*site;
}

void creditExtractedCompatibilityMiningSiteArtifacts(
    MetaProgress& meta,
    const std::vector<ArtifactRecord>& artifacts)
{
    for (MiningSiteProgress& site : meta.miningSites) {
        if (!site.legacyMigrated || site.completed) {
            continue;
        }
        const auto recovered = std::find_if(artifacts.begin(), artifacts.end(), [&](const ArtifactRecord& artifact) {
            return artifact.id == site.artifactId;
        });
        if (recovered != artifacts.end()) {
            site.discovered = true;
            site.completed = true;
        }
    }
}

MiningCampaignProgression resolveCampaignMiningProgression(
    GameChapter chapter,
    std::string_view /* destinationId */,
    int surfaceDepth,
    int completedHostileSorties)
{
    MiningCampaignProgression progression;
    const int depth = std::max(0, surfaceDepth);

    if (chapter == GameChapter::ProvingGround) {
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

std::string_view miningEnemyThemeName(MiningEnemyTheme theme)
{
    switch (theme) {
    case MiningEnemyTheme::Neutral: return "Neutral";
    case MiningEnemyTheme::Lava: return "Lava";
    case MiningEnemyTheme::Ice: return "Ice";
    case MiningEnemyTheme::Radioactive: return "Radioactive";
    case MiningEnemyTheme::Toxic: return "Toxic";
    }
    return "Neutral";
}

MiningElementalAffinity miningEnemyThemeAffinity(MiningEnemyTheme theme)
{
    switch (theme) {
    case MiningEnemyTheme::Lava: return MiningElementalAffinity::Thermal;
    case MiningEnemyTheme::Ice: return MiningElementalAffinity::Cryo;
    case MiningEnemyTheme::Radioactive: return MiningElementalAffinity::Radiation;
    case MiningEnemyTheme::Toxic: return MiningElementalAffinity::Toxic;
    case MiningEnemyTheme::Neutral: return MiningElementalAffinity::None;
    }
    return MiningElementalAffinity::None;
}

MiningEnemyTheme miningEnemyThemeForAffinity(MiningElementalAffinity affinity)
{
    switch (affinity) {
    case MiningElementalAffinity::Thermal: return MiningEnemyTheme::Lava;
    case MiningElementalAffinity::Cryo: return MiningEnemyTheme::Ice;
    case MiningElementalAffinity::Radiation: return MiningEnemyTheme::Radioactive;
    case MiningElementalAffinity::Toxic: return MiningEnemyTheme::Toxic;
    case MiningElementalAffinity::None: return MiningEnemyTheme::Neutral;
    }
    return MiningEnemyTheme::Neutral;
}

MiningEnemyTheme selectMiningEnemyTheme(const MiningArenaRules& rules, std::uint64_t seed)
{
    // Before affinity-bearing enemies enter the curriculum, retain the
    // neutral ecology. Once Elementals are legal, choose one whole-site theme
    // from the same affinity whitelist used by gameplay.
    if (!miningEnemyAllowed(rules, MiningEnemyType::Elemental)) {
        return MiningEnemyTheme::Neutral;
    }
    std::array<MiningEnemyTheme, 4> candidates {};
    std::size_t count = 0;
    const auto addIfAllowed = [&](MiningEnemyTheme theme) {
        if (miningAffinityAllowed(rules, miningEnemyThemeAffinity(theme))) {
            candidates[count++] = theme;
        }
    };
    addIfAllowed(MiningEnemyTheme::Lava);
    addIfAllowed(MiningEnemyTheme::Ice);
    addIfAllowed(MiningEnemyTheme::Radioactive);
    addIfAllowed(MiningEnemyTheme::Toxic);
    if (count == 0) {
        return MiningEnemyTheme::Neutral;
    }
    return candidates[static_cast<std::size_t>(mix64(seed ^ 0x454e454d5954484dULL) % count)];
}

MiningEnemyTheme resolveMiningEnemyTheme(
    const MiningArenaRules& rules,
    const MiningSiteDefinition* authoredSite,
    const MiningSiteProgress* compatibilitySite)
{
    if (authoredSite != nullptr) {
        return authoredSite->biome == MiningSiteBiome::ThermalLava
            ? MiningEnemyTheme::Lava
            : authoredSite->enemyTheme;
    }
    if (compatibilitySite != nullptr &&
        (compatibilitySite->enemyTheme != MiningEnemyTheme::Neutral ||
            !miningEnemyAllowed(rules, MiningEnemyType::Elemental))) {
        return compatibilitySite->enemyTheme;
    }
    return selectMiningEnemyTheme(rules, rules.request.seed);
}

} // namespace rocket
