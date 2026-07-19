#include "core/GameState.h"
#include "core/ContentIds.h"
#include "core/GameText.h"
#include "core/Tuning.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>

namespace rocket {

namespace {

enum class RefitOfferKind {
    ShipModule,
    CrewUpgrade
};

struct RefitCandidate {
    RefitOfferKind kind = RefitOfferKind::ShipModule;
    std::string id;
    int cost = 0;
};

bool hasMaterialCost(const MaterialInventory& cost)
{
    return cost.common > 0 || cost.rare > 0 || cost.exotic > 0;
}

bool materialRefitsAvailable(const GameState& state)
{
    return state.meta.furthestTier >= tuning::research::firstResearchTier;
}

std::vector<std::string> starterInventory()
{
    return {
        content::module::sparrowEngine,
        content::module::stableTank,
        content::module::patchworkHull,
        content::module::radiatorVanes,
        content::module::analogTelemetry,
        content::module::springCapsule
    };
}

bool containsId(const std::vector<std::string>& ids, std::string_view id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

void addUniqueId(std::vector<std::string>& ids, const std::string& id)
{
    if (!id.empty() && !containsId(ids, id)) {
        ids.push_back(id);
    }
}

void pruneUnknownModules(std::vector<std::string>& ids, const ContentCatalog& catalog)
{
    ids.erase(
        std::remove_if(
            ids.begin(),
            ids.end(),
            [&](const std::string& id) {
                return catalog.findModule(id) == nullptr;
            }),
        ids.end());
}

void ensurePermanentModuleState(GameState& state, const ContentCatalog& catalog)
{
    for (const std::string& moduleId : starterInventory()) {
        addUniqueId(state.meta.ownedModuleIds, moduleId);
    }

    pruneUnknownModules(state.meta.ownedModuleIds, catalog);
    pruneUnknownModules(state.meta.defaultEquippedModuleIds, catalog);

    state.meta.defaultEquippedModuleIds.erase(
        std::remove_if(
            state.meta.defaultEquippedModuleIds.begin(),
            state.meta.defaultEquippedModuleIds.end(),
            [&](const std::string& id) {
                return !containsId(state.meta.ownedModuleIds, id);
            }),
        state.meta.defaultEquippedModuleIds.end());

    if (state.meta.defaultEquippedModuleIds.empty()) {
        state.meta.defaultEquippedModuleIds = starterInventory();
    }

    for (const std::string& moduleId : starterInventory()) {
        const ShipModule* starter = catalog.findModule(moduleId);
        if (starter == nullptr) {
            continue;
        }
        const bool slotCovered = std::any_of(
            state.meta.defaultEquippedModuleIds.begin(),
            state.meta.defaultEquippedModuleIds.end(),
            [&](const std::string& equippedId) {
                const ShipModule* equipped = catalog.findModule(equippedId);
                return equipped != nullptr && equipped->slot == starter->slot;
            });
        if (!slotCovered) {
            state.meta.defaultEquippedModuleIds.push_back(moduleId);
        }
    }
}

void ensureDestinationHistory(GameState& state, const ContentCatalog& catalog)
{
    const std::size_t count = catalog.destinations.size();
    std::vector<std::string> catalogIds;
    catalogIds.reserve(count);
    for (const Destination& destination : catalog.destinations) {
        catalogIds.push_back(destination.id);
    }

    if (!state.meta.destinationHistoryIds.empty() && state.meta.destinationHistoryIds != catalogIds) {
        const auto remap = [&](const std::vector<int>& source) {
            std::vector<int> result(count, 0);
            for (std::size_t oldIndex = 0; oldIndex < state.meta.destinationHistoryIds.size() && oldIndex < source.size(); ++oldIndex) {
                const auto found = std::find(catalogIds.begin(), catalogIds.end(), state.meta.destinationHistoryIds[oldIndex]);
                if (found != catalogIds.end()) {
                    result[static_cast<std::size_t>(std::distance(catalogIds.begin(), found))] = source[oldIndex];
                }
            }
            return result;
        };
        state.meta.destinationAttempts = remap(state.meta.destinationAttempts);
        state.meta.destinationSuccesses = remap(state.meta.destinationSuccesses);
        state.meta.destinationFlybys = remap(state.meta.destinationFlybys);
        state.meta.destinationOrbits = remap(state.meta.destinationOrbits);
        state.meta.destinationLandings = remap(state.meta.destinationLandings);
    } else {
        state.meta.destinationAttempts.resize(count, 0);
        state.meta.destinationSuccesses.resize(count, 0);
        state.meta.destinationFlybys.resize(count, 0);
        state.meta.destinationOrbits.resize(count, 0);
        state.meta.destinationLandings.resize(count, 0);
    }
    state.meta.destinationHistoryIds = std::move(catalogIds);
}

int destinationIndexForId(const ContentCatalog& catalog, const std::string& destinationId)
{
    for (std::size_t i = 0; i < catalog.destinations.size(); ++i) {
        if (catalog.destinations[i].id == destinationId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool isShallowRecoveryOutcome(const Destination& destination, const LaunchOutcome& outcome)
{
    return (outcome.recoveryMethod == RecoveryMethod::ReturnHome || outcome.recoveryMethod == RecoveryMethod::ManualEject)
        && outcome.ejectMultiplier < 1.0 + (destination.targetMultiplier - 1.0) * tuning::rewards::shallowRecoveryTargetShare;
}

bool isCleanShallowRecoveryOutcome(const Destination& destination, const LaunchOutcome& outcome)
{
    return isShallowRecoveryOutcome(destination, outcome)
        && outcome.peakWarning < tuning::rewards::cleanShallowRecoveryWarningThreshold;
}

int navigationFuelCost(const Destination& destination)
{
    // Khepri Prime and Rift Belt moved to campaign tiers 7 and 8, but their
    // established sortie economics remain the former tier-4/tier-5 values.
    if (destination.id == content::destination::nearbyStar) {
        return 6;
    }
    if (destination.id == content::destination::nearbyGalaxy) {
        return 7;
    }
    return 2 + destination.tier;
}

} // namespace

int moduleOfferCost(Rarity rarity)
{
    return tuning::moduleOfferCost(rarity);
}

int moduleOfferCost(const ShipModule& module)
{
    return moduleOfferCost(module.rarity);
}

int crewUpgradeCost(const CrewUpgrade& upgrade)
{
    return moduleOfferCost(upgrade.rarity);
}

bool canAffordMaterials(const MaterialInventory& owned, const MaterialInventory& cost)
{
    return owned.common >= cost.common && owned.rare >= cost.rare && owned.exotic >= cost.exotic;
}

bool spendMaterials(MaterialInventory& owned, const MaterialInventory& cost)
{
    if (!canAffordMaterials(owned, cost)) {
        return false;
    }

    owned.common -= cost.common;
    owned.rare -= cost.rare;
    owned.exotic -= cost.exotic;
    return true;
}

bool canAffordModuleOffer(const GameState& state, const ShipModule& module)
{
    return state.run.credits >= static_cast<double>(moduleOfferCost(module)) &&
        canAffordMaterials(state.meta.materials, module.materialCost);
}

CrewUpgradeStats& operator+=(CrewUpgradeStats& lhs, const CrewUpgradeStats& rhs)
{
    lhs.trainingGain += rhs.trainingGain;
    lhs.trainingStressRelief += rhs.trainingStressRelief;
    lhs.restStressBonus += rhs.restStressBonus;
    lhs.launchStressRelief += rhs.launchStressRelief;
    lhs.traitModifier += rhs.traitModifier;
    return lhs;
}

int crewStressStepCount(int stress)
{
    return std::clamp(stress, 0, tuning::crew::maxStress) / tuning::crew::stressPerStep;
}

int effectiveTrainingLevel(const Astronaut& astronaut)
{
    return astronaut.training - crewStressStepCount(astronaut.stress);
}

double crewNavigationPenaltyFromStress(int stress)
{
    return static_cast<double>(crewStressStepCount(stress)) * tuning::crew::navigationPenaltyPerStressStep;
}

double crewAbortRiskMultiplierFromStress(int stress)
{
    return 1.0 + static_cast<double>(crewStressStepCount(stress)) * tuning::crew::abortRiskPerStressStep;
}

PostLaunchCrewStress postLaunchCrewStress(const LaunchOutcome& outcome, const CrewUpgradeStats& upgrades)
{
    const double warningLoad = std::clamp(
        (outcome.peakWarning - tuning::stress::warningStressStart) / tuning::stress::warningStressRange,
        0.0,
        1.0);
    const double abortLoad = std::clamp(
        (outcome.peakAbortRisk - tuning::stress::abortStressStart) / tuning::stress::abortStressRange,
        0.0,
        1.0);

    PostLaunchCrewStress stress;
    stress.baseStress = outcome.type == LaunchResultType::Destroyed
        ? tuning::stress::destroyedLaunchStress
        : tuning::stress::survivedLaunchStress;
    stress.warningStress = static_cast<int>(std::round(warningLoad * tuning::stress::warningStressScale));
    stress.abortStress = static_cast<int>(std::round(abortLoad * tuning::stress::abortStressScale));
    stress.relief = std::max(0, upgrades.launchStressRelief);
    stress.total = std::max(0, stress.baseStress + stress.warningStress + stress.abortStress - stress.relief);
    return stress;
}

int postLaunchCrewStressGain(const LaunchOutcome& outcome, const CrewUpgradeStats& upgrades)
{
    return postLaunchCrewStress(outcome, upgrades).total;
}

double defaultProvingTarget(const Destination& destination)
{
    return std::clamp(
        1.0 + (destination.targetMultiplier - 1.0) * tuning::mission::defaultProvingTargetShare,
        tuning::mission::defaultProvingTargetMinimum,
        destination.targetMultiplier);
}

GameState createNewGame(const ContentCatalog& catalog, std::uint64_t seed)
{
    GameState state;
    state.seed = seed;
    state.meta.unlockKeys = {content::unlock::starter};
    state.meta.ark.fuelReserve = tuning::ark::startingFuelReserve;
    state.run.credits = tuning::hangar::startingCredits;
    state.run.crew = catalog.astronauts;
    ensureDestinationHistory(state, catalog);
    startNewExpedition(state, catalog);

    Random rng(seed);
    generateModuleOffers(state, catalog, rng);

    state.statusLine = std::string(text::status::programInitialized);
    return state;
}

void startNewExpedition(GameState& state, const ContentCatalog& catalog)
{
    ensureDestinationHistory(state, catalog);
    ensurePermanentModuleState(state, catalog);
    state.screen = hostileSystemActive(state) ? Screen::Navigation : Screen::Hangar;
    state.run.active = true;
    if (hostileSystemActive(state) && !state.meta.navigation.selectedDestinationId.empty()) {
        const int selectedIndex = destinationIndexForId(catalog, state.meta.navigation.selectedDestinationId);
        state.run.destinationIndex = selectedIndex >= 0 ? selectedIndex : std::clamp(state.meta.furthestTier, 0, static_cast<int>(catalog.destinations.size()) - 1);
    } else {
        state.run.destinationIndex = std::clamp(state.meta.furthestTier, 0, static_cast<int>(catalog.destinations.size()) - 1);
    }
    state.run.frontierReadiness = std::clamp(state.run.frontierReadiness, 0, frontierReadinessCap(state, catalog));
    state.run.shipDamage = 0;
    state.run.frameId = catalog.frames.empty() ? "" : catalog.frames.front().id;
    state.run.inventoryModuleIds = state.meta.ownedModuleIds;
    state.run.equippedModuleIds = state.meta.defaultEquippedModuleIds;
    state.run.surfaceUpgradeIds.clear();
    state.run.offerModuleIds = {};
    state.run.offerCrewUpgradeIds = {};
    state.run.launchesThisExpedition = 0;
    state.run.offerRerollsThisExpedition = 0;
    state.run.repairOpsThisExpedition = 0;
    state.run.trainingOpsThisExpedition = 0;
    state.run.restOpsThisExpedition = 0;
    if (state.run.credits < tuning::hangar::minimumExpeditionCredits) {
        state.run.credits = tuning::hangar::minimumExpeditionCredits;
    }

    if (state.run.crew.empty()) {
        state.run.crew = catalog.astronauts;
    }

    if (activeAstronaut(state) == nullptr && !catalog.astronauts.empty()) {
        Astronaut recruit = catalog.astronauts.front();
        recruit.id = text::replacementId(state.meta.astronautsLost + state.meta.shipsLost + 1);
        recruit.name = std::string(text::panel::messages::replacementCadet);
        recruit.background = std::string(text::panel::messages::emergencyRecruitBackground);
        recruit.trait = std::string(text::panel::messages::generatedRecruitTrait);
        recruit.training = 0;
        recruit.stress = tuning::hangar::emergencyReplacementStress;
        recruit.status = CrewStatus::Active;
        state.run.crew.push_back(recruit);
    }

    for (auto& astronaut : state.run.crew) {
        if (astronaut.status == CrewStatus::Injured) {
            astronaut.stress = std::min(tuning::crew::maxStress, astronaut.stress + tuning::hangar::injuredCarryoverStress);
        }
    }

    syncLaunchConfig(state, catalog);
    state.launchConfig.burnGoalMultiplier = defaultProvingTarget(currentDestination(state, catalog));
}

void syncLaunchConfig(GameState& state, const ContentCatalog& catalog)
{
    ensureDestinationHistory(state, catalog);
    const Destination* destination = catalog.findDestination(state.launchConfig.destinationId);
    if (destination == nullptr || !state.launchConfig.frontierTransfer) {
        destination = &currentDestination(state, catalog);
        state.launchConfig.destinationId = destination->id;
    }
    state.launchConfig.frameId = state.run.frameId;
    state.launchConfig.equippedModuleIds = state.run.equippedModuleIds;

    if (state.launchConfig.burnGoalMultiplier < tuning::mission::launchConfigMinimumMultiplier ||
        state.launchConfig.burnGoalMultiplier > destination->targetMultiplier + tuning::mission::launchConfigOverTargetAllowance) {
        state.launchConfig.burnGoalMultiplier = state.launchConfig.frontierTransfer ? destination->targetMultiplier : defaultProvingTarget(*destination);
    }

    if (state.launchConfig.astronautId.empty()) {
        if (const Astronaut* astronaut = activeAstronaut(state)) {
            state.launchConfig.astronautId = astronaut->id;
        }
    } else {
        const auto selected = std::find_if(state.run.crew.begin(), state.run.crew.end(), [&](const Astronaut& astronaut) {
            return astronaut.id == state.launchConfig.astronautId && astronaut.status != CrewStatus::Dead;
        });
        if (selected == state.run.crew.end()) {
            if (const Astronaut* astronaut = activeAstronaut(state)) {
                state.launchConfig.astronautId = astronaut->id;
            } else {
                state.launchConfig.astronautId.clear();
            }
        }
    }

    syncChapterProgress(state, catalog);
}

void generateModuleOffers(GameState& state, const ContentCatalog& catalog, Random& rng)
{
    state.run.offerModuleIds = {};
    state.run.offerCrewUpgradeIds = {};

    const auto modulePool = unlockedModules(catalog, state.meta);
    const auto crewPool = unlockedCrewUpgrades(catalog, state.meta);
    if (modulePool.empty() && crewPool.empty()) {
        state.run.offerModuleIds = {};
        state.run.offerCrewUpgradeIds = {};
        return;
    }

    std::vector<RefitCandidate> candidates;
    const auto addCandidates = [&](bool onlyUnowned) {
        for (const ShipModule* module : modulePool) {
            if (!materialRefitsAvailable(state) && hasMaterialCost(module->materialCost)) {
                continue;
            }
            const bool alreadyOwned = std::find(
                state.meta.ownedModuleIds.begin(),
                state.meta.ownedModuleIds.end(),
                module->id) != state.meta.ownedModuleIds.end();
            if (!onlyUnowned || !alreadyOwned) {
                candidates.push_back({RefitOfferKind::ShipModule, module->id, moduleOfferCost(*module)});
            }
        }

        for (const CrewUpgrade* upgrade : crewPool) {
            const bool alreadyOwned = std::find(
                state.run.crewUpgradeIds.begin(),
                state.run.crewUpgradeIds.end(),
                upgrade->id) != state.run.crewUpgradeIds.end();
            if (!onlyUnowned || !alreadyOwned) {
                candidates.push_back({RefitOfferKind::CrewUpgrade, upgrade->id, crewUpgradeCost(*upgrade)});
            }
        }
    };

    addCandidates(true);
    if (candidates.size() < state.run.offerModuleIds.size()) {
        candidates.clear();
        addCandidates(false);
    }

    if (candidates.empty()) {
        return;
    }

    const auto sameCandidate = [](const RefitCandidate& lhs, const RefitCandidate& rhs) {
        return lhs.kind == rhs.kind && lhs.id == rhs.id;
    };

    std::vector<RefitCandidate> pickedIds;
    pickedIds.reserve(state.run.offerModuleIds.size());
    for (std::size_t i = 0; i < state.run.offerModuleIds.size(); ++i) {
        const RefitCandidate* picked = nullptr;
        for (int attempts = 0; attempts < 12 && picked == nullptr; ++attempts) {
            const RefitCandidate& candidate = candidates[static_cast<std::size_t>(rng.rangeInt(0, static_cast<int>(candidates.size()) - 1))];
            if (std::find_if(pickedIds.begin(), pickedIds.end(), [&](const RefitCandidate& existing) { return sameCandidate(existing, candidate); }) == pickedIds.end()) {
                picked = &candidate;
            }
        }
        if (picked == nullptr) {
            picked = &candidates[static_cast<std::size_t>(rng.rangeInt(0, static_cast<int>(candidates.size()) - 1))];
        }
        if (picked->kind == RefitOfferKind::ShipModule) {
            state.run.offerModuleIds[i] = picked->id;
        } else {
            state.run.offerCrewUpgradeIds[i] = picked->id;
        }
        pickedIds.push_back(*picked);
    }

    const auto isAffordable = [&](std::size_t index) {
        const std::string& moduleId = state.run.offerModuleIds[index];
        if (!moduleId.empty()) {
            const ShipModule* module = catalog.findModule(moduleId);
            return module != nullptr && canAffordModuleOffer(state, *module);
        }

        const std::string& upgradeId = state.run.offerCrewUpgradeIds[index];
        const CrewUpgrade* upgrade = catalog.findCrewUpgrade(upgradeId);
        return upgrade != nullptr && state.run.credits >= static_cast<double>(crewUpgradeCost(*upgrade));
    };

    bool hasAffordableOffer = false;
    for (std::size_t i = 0; i < state.run.offerModuleIds.size(); ++i) {
        hasAffordableOffer = hasAffordableOffer || isAffordable(i);
    }

    if (!hasAffordableOffer && state.run.credits > 0.0) {
        const RefitCandidate* cheapestAffordable = nullptr;
        for (const RefitCandidate& candidate : candidates) {
            if (std::find_if(pickedIds.begin(), pickedIds.end(), [&](const RefitCandidate& existing) { return sameCandidate(existing, candidate); }) != pickedIds.end()) {
                continue;
            }
            if (state.run.credits >= static_cast<double>(candidate.cost) && (cheapestAffordable == nullptr || candidate.cost < cheapestAffordable->cost)) {
                cheapestAffordable = &candidate;
            }
        }

        if (cheapestAffordable != nullptr) {
            state.run.offerModuleIds.back().clear();
            state.run.offerCrewUpgradeIds.back().clear();
            if (cheapestAffordable->kind == RefitOfferKind::ShipModule) {
                state.run.offerModuleIds.back() = cheapestAffordable->id;
            } else {
                state.run.offerCrewUpgradeIds.back() = cheapestAffordable->id;
            }
        }
    }
}

double offerRerollCost(const GameState& state)
{
    return tuning::offerRerollCost(state.run.offerRerollsThisExpedition);
}

bool rerollOffers(GameState& state, const ContentCatalog& catalog, Random& rng)
{
    const double cost = offerRerollCost(state);
    if (state.run.credits < cost) {
        state.statusLine = std::string(text::status::refitRerollUnaffordable);
        return false;
    }

    state.run.credits -= cost;
    state.run.offerRerollsThisExpedition += 1;
    generateModuleOffers(state, catalog, rng);
    state.statusLine = text::refitRerolled(static_cast<int>(offerRerollCost(state)));
    return true;
}

bool buyOffer(GameState& state, const ContentCatalog& catalog, int index)
{
    if (index < 0 || index >= static_cast<int>(state.run.offerModuleIds.size())) {
        return false;
    }

    const auto offerIndex = static_cast<std::size_t>(index);
    const ShipModule* module = catalog.findModule(state.run.offerModuleIds[offerIndex]);
    const CrewUpgrade* crewUpgrade = catalog.findCrewUpgrade(state.run.offerCrewUpgradeIds[offerIndex]);
    if (module == nullptr && crewUpgrade == nullptr) {
        return false;
    }

    const int cost = module != nullptr ? moduleOfferCost(*module) : crewUpgradeCost(*crewUpgrade);
    if (state.run.credits < static_cast<double>(cost)) {
        state.statusLine = text::insufficientCreditsFor(module != nullptr ? module->name : crewUpgrade->name);
        return false;
    }
    if (module != nullptr && !canAffordMaterials(state.meta.materials, module->materialCost)) {
        state.statusLine = std::string(text::panel::needMaterials);
        return false;
    }

    state.run.credits -= static_cast<double>(cost);
    if (module != nullptr) {
        spendMaterials(state.meta.materials, module->materialCost);
        addUniqueId(state.meta.ownedModuleIds, module->id);
        addUniqueId(state.run.inventoryModuleIds, module->id);

        auto slotIt = std::find_if(state.run.equippedModuleIds.begin(), state.run.equippedModuleIds.end(), [&](const std::string& equippedId) {
            const ShipModule* equipped = catalog.findModule(equippedId);
            return equipped != nullptr && equipped->slot == module->slot;
        });

        if (slotIt != state.run.equippedModuleIds.end()) {
            *slotIt = module->id;
        } else {
            state.run.equippedModuleIds.push_back(module->id);
        }

        auto defaultSlotIt = std::find_if(state.meta.defaultEquippedModuleIds.begin(), state.meta.defaultEquippedModuleIds.end(), [&](const std::string& equippedId) {
            const ShipModule* equipped = catalog.findModule(equippedId);
            return equipped != nullptr && equipped->slot == module->slot;
        });
        if (defaultSlotIt != state.meta.defaultEquippedModuleIds.end()) {
            *defaultSlotIt = module->id;
        } else {
            state.meta.defaultEquippedModuleIds.push_back(module->id);
        }
    } else {
        state.run.crewUpgradeIds.push_back(crewUpgrade->id);
    }

    state.run.offerModuleIds = {};
    state.run.offerCrewUpgradeIds = {};
    state.statusLine = text::refitInstalled(module != nullptr ? module->name : crewUpgrade->name);
    syncLaunchConfig(state, catalog);
    return true;
}

namespace {

double standardRepairShipCost(const GameState& state)
{
    const int repaired = repairShipAmount(state);
    if (repaired <= 0) {
        return 0.0;
    }

    const double baseCost = tuning::hangar::repairBaseCost + static_cast<double>(repaired) * tuning::hangar::repairCostPerDamage;
    return tuning::escalatedHangarOpCost(baseCost, state.run.repairOpsThisExpedition);
}

bool salvageRebuildAvailable(const GameState& state)
{
    return state.run.shipDamage >= tuning::damage::destroyedShipDamage && state.run.credits < standardRepairShipCost(state);
}

} // namespace

int repairShipAmount(const GameState& state)
{
    return std::min(tuning::hangar::repairAmountCap, state.run.shipDamage);
}

double repairShipCost(const GameState& state)
{
    const double standardCost = standardRepairShipCost(state);
    if (salvageRebuildAvailable(state)) {
        return std::max(0.0, state.run.credits);
    }
    return standardCost;
}

bool repairShip(GameState& state)
{
    if (state.run.shipDamage <= 0) {
        state.statusLine = std::string(text::status::shipAlreadyReady);
        return false;
    }

    const int repaired = repairShipAmount(state);
    const bool salvageRebuild = salvageRebuildAvailable(state);
    const double cost = repairShipCost(state);
    if (state.run.credits < cost) {
        state.statusLine = std::string(text::status::repairsUnaffordable);
        return false;
    }

    state.run.credits -= cost;
    state.run.shipDamage -= repaired;
    state.run.repairOpsThisExpedition += 1;
    state.statusLine = salvageRebuild ? text::salvagedHull(repaired) : text::repairedHull(repaired);
    return true;
}

int crewTrainingGain(const GameState& state, const ContentCatalog& catalog)
{
    const CrewUpgradeStats upgrades = aggregateCrewUpgradeStats(state, catalog);
    return std::max(1, 1 + upgrades.trainingGain);
}

int crewTrainingStressGain(const GameState& state, const ContentCatalog& catalog)
{
    const CrewUpgradeStats upgrades = aggregateCrewUpgradeStats(state, catalog);
    return std::max(tuning::hangar::trainingMinimumStress, tuning::hangar::trainingBaseStress - upgrades.trainingStressRelief);
}

double crewTrainingCost(const GameState& state, const ContentCatalog&)
{
    return tuning::escalatedHangarOpCost(tuning::hangar::trainingBaseCost, state.run.trainingOpsThisExpedition);
}

double crewRestCost(const GameState& state, const ContentCatalog&)
{
    const Astronaut* astronaut = activeAstronaut(state);
    if (astronaut == nullptr) {
        return tuning::escalatedHangarOpCost(tuning::hangar::restNoCrewBaseCost, state.run.restOpsThisExpedition);
    }
    const double baseCost = tuning::hangar::restBaseCost + static_cast<double>(astronaut->stress) * tuning::hangar::restCostPerStress;
    return tuning::escalatedHangarOpCost(baseCost, state.run.restOpsThisExpedition);
}

bool trainCrew(GameState& state, const ContentCatalog& catalog)
{
    Astronaut* astronaut = activeAstronaut(state);
    if (astronaut == nullptr) {
        state.statusLine = std::string(text::status::noTrainableAstronaut);
        return false;
    }

    const HangarOperationPreview preview = hangarOperationPreview(state, catalog);
    if (astronaut->training >= tuning::crew::maxTraining) {
        state.statusLine = std::string(text::panel::messages::simulatorMastered);
        return false;
    }

    if (astronaut->stress >= tuning::crew::maxStress || astronaut->stress + preview.trainingStressGain > tuning::crew::maxStress) {
        state.statusLine = text::tooStressedForTraining(astronaut->name);
        return false;
    }

    if (!preview.trainingAvailable) {
        state.statusLine = std::string(text::status::trainingBudgetDenied);
        return false;
    }

    state.run.credits -= preview.trainingCost;
    astronaut->training = std::min(tuning::crew::maxTraining, astronaut->training + preview.trainingGain);
    astronaut->stress += preview.trainingStressGain;
    state.run.trainingOpsThisExpedition += 1;
    state.statusLine = text::simulatorComplete(astronaut->name);
    return true;
}

int crewRestStressRecovery(const GameState& state, const ContentCatalog& catalog)
{
    const CrewUpgradeStats upgrades = aggregateCrewUpgradeStats(state, catalog);
    const double difficulty = missionPressureModifier(state, catalog, currentDestination(state, catalog));
    const double recoveryFactor = std::clamp(1.0 - difficulty, tuning::hangar::restDifficultyMinFactor, tuning::hangar::restDifficultyMaxFactor);
    return std::max(
        tuning::hangar::restMinimumRecovery,
        static_cast<int>(std::round(static_cast<double>(tuning::hangar::restBaseRecovery + upgrades.restStressBonus) * recoveryFactor)));
}

bool restCrew(GameState& state, const ContentCatalog& catalog)
{
    Astronaut* astronaut = activeAstronaut(state);
    if (astronaut == nullptr) {
        state.statusLine = std::string(text::status::noRestableAstronaut);
        return false;
    }

    const HangarOperationPreview preview = hangarOperationPreview(state, catalog);
    if (!preview.restNeeded) {
        state.statusLine = std::string(text::status::noRestNeeded);
        return false;
    }

    if (!preview.restAvailable) {
        state.statusLine = std::string(text::status::restBudgetDenied);
        return false;
    }

    state.run.credits -= preview.restCost;
    astronaut->stress = std::max(0, astronaut->stress - preview.restStressRecovery);
    if (astronaut->status == CrewStatus::Injured) {
        astronaut->status = CrewStatus::Active;
        astronaut->stress = std::max(0, astronaut->stress - std::max(4, preview.restStressRecovery / 2));
    }
    state.run.restOpsThisExpedition += 1;
    state.statusLine = text::crewRecovered(astronaut->name, preview.restStressRecovery);
    return true;
}

double recruitCrewCost(const GameState& state)
{
    return activeAstronaut(state) == nullptr ? tuning::hangar::emergencyRecruitCost : tuning::hangar::recruitCost;
}

HangarOperationPreview hangarOperationPreview(const GameState& state, const ContentCatalog& catalog)
{
    HangarOperationPreview preview;
    const Astronaut* astronaut = activeAstronaut(state);

    preview.repairAmount = repairShipAmount(state);
    preview.repairCost = repairShipCost(state);
    preview.repairAvailable = preview.repairAmount > 0 && state.run.credits >= preview.repairCost;

    preview.trainingGain = crewTrainingGain(state, catalog);
    preview.trainingStressGain = crewTrainingStressGain(state, catalog);
    preview.trainingCost = crewTrainingCost(state, catalog);
    preview.trainingAvailable = astronaut != nullptr &&
        state.run.credits >= preview.trainingCost &&
        astronaut->training < tuning::crew::maxTraining &&
        astronaut->stress < tuning::crew::maxStress &&
        astronaut->stress + preview.trainingStressGain <= tuning::crew::maxStress;

    preview.restStressRecovery = crewRestStressRecovery(state, catalog);
    preview.restCost = crewRestCost(state, catalog);
    preview.restNeeded = astronaut != nullptr && (astronaut->stress > 0 || astronaut->status == CrewStatus::Injured);
    preview.restAvailable = preview.restNeeded && state.run.credits >= preview.restCost;

    preview.emergencyRecruitment = astronaut == nullptr;
    preview.recruitCost = recruitCrewCost(state);
    preview.recruitAvailable = state.run.credits >= preview.recruitCost;

    return preview;
}

std::vector<const Astronaut*> recruitCandidateTemplates(const GameState& state, const ContentCatalog& catalog, int count)
{
    std::vector<const Astronaut*> candidates;
    if (catalog.astronauts.empty() || count <= 0) {
        return candidates;
    }

    const int rosterSize = static_cast<int>(catalog.astronauts.size());
    const int start = (state.meta.astronautsLost + state.meta.shipsLost + state.run.launchesThisExpedition +
        static_cast<int>(state.run.crew.size())) % rosterSize;
    const int candidateCount = std::min(count, rosterSize);

    candidates.reserve(static_cast<std::size_t>(candidateCount));
    for (int offset = 0; offset < candidateCount; ++offset) {
        const int index = (start + offset) % rosterSize;
        candidates.push_back(&catalog.astronauts[static_cast<std::size_t>(index)]);
    }

    return candidates;
}

bool recruitCrew(GameState& state, const ContentCatalog& catalog, int candidateIndex)
{
    if (catalog.astronauts.empty()) {
        state.statusLine = std::string(text::status::noRecruitProfiles);
        return false;
    }

    const HangarOperationPreview preview = hangarOperationPreview(state, catalog);
    if (!preview.recruitAvailable) {
        state.statusLine = std::string(text::status::recruitUnaffordable);
        return false;
    }

    const Astronaut* selectedTemplate = nullptr;
    if (preview.emergencyRecruitment && candidateIndex >= 0) {
        const std::vector<const Astronaut*> candidates = recruitCandidateTemplates(state, catalog);
        if (candidateIndex >= static_cast<int>(candidates.size())) {
            state.statusLine = std::string(text::status::noRecruitProfiles);
            return false;
        }
        selectedTemplate = candidates[static_cast<std::size_t>(candidateIndex)];
    }

    if (selectedTemplate == nullptr) {
        selectedTemplate = catalog.astronauts.empty() ? nullptr : &catalog.astronauts[static_cast<std::size_t>(
            (state.meta.astronautsLost + state.meta.shipsLost + state.run.launchesThisExpedition + state.run.crew.size()) %
            static_cast<int>(catalog.astronauts.size()))];
    }

    if (selectedTemplate == nullptr) {
        state.statusLine = std::string(text::status::noRecruitProfiles);
        return false;
    }

    Astronaut recruit = *selectedTemplate;
    const int recruitNumber = state.meta.astronautsLost + state.meta.shipsLost + static_cast<int>(state.run.crew.size()) + 1;
    recruit.id = text::recruitId(recruitNumber);
    recruit.name = preview.emergencyRecruitment ? selectedTemplate->name : text::nextGenerationName(selectedTemplate->name);
    recruit.background = selectedTemplate->background;
    recruit.training = preview.emergencyRecruitment ? selectedTemplate->training : std::max(0, selectedTemplate->training - tuning::hangar::recruitTrainingPenalty);
    recruit.stress = preview.emergencyRecruitment ? tuning::hangar::emergencyRecruitStress : tuning::hangar::recruitStress;
    recruit.status = CrewStatus::Active;

    state.run.credits -= preview.recruitCost;
    state.run.crew.push_back(recruit);
    syncLaunchConfig(state, catalog);
    state.statusLine = text::recruitJoined(recruit.name, preview.emergencyRecruitment);
    return true;
}

bool recruitCrew(GameState& state, const ContentCatalog& catalog)
{
    return recruitCrew(state, catalog, -1);
}

bool arkDiscovered(const GameState& state)
{
    return state.meta.ark.condition != ArkCondition::NotFound ||
        state.meta.campaignMilestone != CampaignMilestone::SolarTutorial;
}

bool hostileSystemActive(const GameState& state)
{
    return state.meta.campaignMilestone == CampaignMilestone::HostileSystemStranded ||
        state.meta.campaignMilestone == CampaignMilestone::ArkRepairing ||
        state.meta.campaignMilestone == CampaignMilestone::GravityWellDisaster ||
        state.meta.ark.gravityWellDisaster ||
        state.meta.ark.condition == ArkCondition::DamagedStranded ||
        state.meta.ark.condition == ArkCondition::Repairing;
}

bool navigationAvailable(const GameState& state)
{
    return hostileSystemActive(state);
}

GameChapter chapterForState(const GameState& state, const ContentCatalog& catalog)
{
    const auto destinationSuccesses = [&](const std::string& destinationId) {
        const int index = destinationIndexForId(catalog, destinationId);
        if (index < 0 || index >= static_cast<int>(state.meta.destinationSuccesses.size())) {
            return 0;
        }
        return state.meta.destinationSuccesses[static_cast<std::size_t>(index)];
    };

    if (state.meta.campaignMilestone == CampaignMilestone::ArkRepairing ||
        state.meta.ark.condition == ArkCondition::Repairing) {
        return GameChapter::Ouroboros;
    }

    if (destinationSuccesses(content::destination::nearbyGalaxy) > 0) {
        return GameChapter::VoidCompass;
    }

    if (destinationSuccesses(content::destination::nearbyStar) > 0) {
        return GameChapter::LastCampfire;
    }

    if (hostileSystemActive(state)) {
        return GameChapter::Arkfall;
    }

    if (state.meta.ark.firstJumpComplete ||
        state.meta.campaignMilestone == CampaignMilestone::FirstArkJumpComplete ||
        state.meta.navigation.currentSystemId == "relay_system") {
        return GameChapter::Straylight;
    }

    const int outerIndex = destinationIndexForId(catalog, content::destination::jupiter);
    if (arkDiscovered(state) ||
        (outerIndex >= 0 && state.run.destinationIndex >= outerIndex) ||
        (outerIndex >= 0 && state.meta.furthestTier >= catalog.destinations[static_cast<std::size_t>(outerIndex)].tier)) {
        return GameChapter::Breakthrough;
    }

    const int marsIndex = destinationIndexForId(catalog, content::destination::mars);
    if (marsIndex >= 0 &&
        (state.run.destinationIndex >= marsIndex ||
            state.meta.furthestTier >= catalog.destinations[static_cast<std::size_t>(marsIndex)].tier)) {
        return GameChapter::RedFrontier;
    }

    const int moonIndex = destinationIndexForId(catalog, content::destination::moon);
    if (moonIndex >= 0 &&
        (state.run.destinationIndex >= moonIndex ||
            state.meta.furthestTier >= catalog.destinations[static_cast<std::size_t>(moonIndex)].tier)) {
        return GameChapter::LunarProgram;
    }

    return GameChapter::ProvingGround;
}

void syncChapterProgress(GameState& state, const ContentCatalog& catalog)
{
    const GameChapter derived = chapterForState(state, catalog);
    if (chapterNumber(derived) > chapterNumber(state.meta.chapter)) {
        state.meta.chapter = derived;
    }
}

bool migrateLegacyDeepSpaceFrontier(GameState& state, const ContentCatalog& catalog)
{
    bool migrated = false;
    const int jupiterIndex = destinationIndexForId(catalog, content::destination::jupiter);
    const int neptuneIndex = destinationIndexForId(catalog, content::destination::neptune);
    const int khepriIndex = destinationIndexForId(catalog, content::destination::nearbyStar);
    if (state.launchConfig.destinationId == content::destination::outerPlanets && jupiterIndex >= 0) {
        state.run.destinationIndex = jupiterIndex;
        state.launchConfig.frontierTransfer = false;
        state.launchConfig.destinationId = content::destination::jupiter;
        migrated = true;
    }

    const bool legacyDirectDeepSpace = !arkDiscovered(state)
        && khepriIndex >= 0
        && state.run.destinationIndex >= khepriIndex
        && (state.launchConfig.destinationId == content::destination::nearbyStar
            || state.launchConfig.destinationId == content::destination::nearbyGalaxy);
    if (legacyDirectDeepSpace && neptuneIndex >= 0) {
        state.meta.campaignMilestone = CampaignMilestone::ArkDiscovered;
        state.meta.ark.condition = ArkCondition::DerelictOperable;
        state.meta.ark.fuelReserve = std::max(state.meta.ark.fuelReserve, 1);
        state.run.destinationIndex = neptuneIndex;
        state.launchConfig.frontierTransfer = false;
        state.launchConfig.destinationId = content::destination::neptune;
        state.storyBriefing = {};
        state.screen = Screen::Hangar;
        migrated = true;
    }

    if (arkDiscovered(state) && neptuneIndex >= 0) {
        if (!state.meta.straylightDiscoveryAcknowledged) {
            state.meta.straylightDiscoveryAcknowledged = true;
            migrated = true;
        }
        state.meta.furthestTier = std::max(state.meta.furthestTier, catalog.destinations[static_cast<std::size_t>(neptuneIndex)].tier);
        for (const std::string_view id : {std::string_view(content::destination::jupiter), std::string_view(content::destination::saturn), std::string_view(content::destination::uranus), std::string_view(content::destination::neptune)}) {
            const int index = destinationIndexForId(catalog, std::string(id));
            if (index >= 0 && static_cast<std::size_t>(index) < state.meta.destinationSuccesses.size()
                && state.meta.destinationSuccesses[static_cast<std::size_t>(index)] < 1) {
                state.meta.destinationSuccesses[static_cast<std::size_t>(index)] = 1;
                migrated = true;
            }
        }
        if (state.meta.navigation.arkLocationId.empty()) {
            state.meta.navigation.arkLocationId = content::destination::neptune;
            migrated = true;
        }
        addUniqueId(state.meta.navigation.discoveredDestinationIds, content::destination::neptune);
    }

    if (migrated) {
        state.statusLine = "Legacy frontier progress migrated to the individual outer-planet route.";
        syncLaunchConfig(state, catalog);
    }
    return migrated;
}

void scheduleStoryBriefing(GameState& state, StoryBriefingId briefing, Screen continuation)
{
    state.storyBriefing.pending = briefing;
    state.storyBriefing.continuation = continuation;
}

bool acknowledgeStoryBriefing(GameState& state, const ContentCatalog& catalog)
{
    const StoryBriefingId briefing = state.storyBriefing.pending;
    if (briefing == StoryBriefingId::None) {
        return false;
    }

    const Screen continuation = state.storyBriefing.continuation;
    if (briefing == StoryBriefingId::CampaignIntroduction) {
        state.meta.campaignIntroductionAcknowledged = true;
        state.statusLine = "Mission brief acknowledged. Build a flight profile and open the route to the Moon.";
    } else if (briefing == StoryBriefingId::StraylightDiscovery) {
        state.meta.straylightDiscoveryAcknowledged = true;
        discoverArk(state, catalog);
    }
    state.storyBriefing = {};
    state.screen = continuation;
    syncLaunchConfig(state, catalog);
    return true;
}

std::vector<const Destination*> navigationDestinations(const GameState& state, const ContentCatalog& catalog)
{
    std::vector<const Destination*> destinations;
    if (!hostileSystemActive(state)) {
        return destinations;
    }

    for (const Destination& destination : catalog.destinations) {
        if (destination.tier >= 7) {
            destinations.push_back(&destination);
        }
    }
    return destinations;
}

void discoverArk(GameState& state, const ContentCatalog& catalog)
{
    if (arkDiscovered(state)) {
        return;
    }

    state.meta.campaignMilestone = CampaignMilestone::ArkDiscovered;
    state.meta.ark.condition = ArkCondition::DerelictOperable;
    state.meta.ark.fuelReserve = std::max(state.meta.ark.fuelReserve, 1);
    state.meta.navigation.currentSystemId = "solar_system";
    state.meta.navigation.arkLocationId = content::destination::neptune;
    state.meta.navigation.discoveredDestinationIds = {content::destination::neptune};
    state.statusLine = "Ark discovered beyond Neptune: derelict, under-equipped, but operable.";
    syncLaunchConfig(state, catalog);
}

bool performArkJump(GameState& state, const ContentCatalog& catalog)
{
    if (!arkDiscovered(state)) {
        state.statusLine = "The Ark has not been found yet.";
        return false;
    }

    if (!state.meta.ark.firstJumpComplete) {
        state.meta.ark.firstJumpComplete = true;
        state.meta.ark.condition = ArkCondition::DerelictOperable;
        state.meta.campaignMilestone = CampaignMilestone::FirstArkJumpComplete;
        state.meta.navigation.currentSystemId = "relay_system";
        state.meta.navigation.arkLocationId = "relay_void";
        state.statusLine = "First Ark jump complete. The vessel can move, but every jump spends the future.";
        syncChapterProgress(state, catalog);
        return true;
    }

    if (!state.meta.ark.gravityWellDisaster) {
        state.meta.ark.gravityWellDisaster = true;
        state.meta.ark.condition = ArkCondition::DamagedStranded;
        state.meta.ark.hullDamage = std::max(state.meta.ark.hullDamage, 72);
        state.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
        state.meta.navigation.currentSystemId = "hostile_system";
        state.meta.navigation.arkLocationId = "gravity_well";
        state.meta.navigation.discoveredDestinationIds = {content::destination::nearbyStar, content::destination::nearbyGalaxy};
        state.meta.navigation.selectedDestinationId = content::destination::nearbyStar;
        state.meta.ark.fuelReserve = std::max(state.meta.ark.fuelReserve, tuning::ark::hostileSystemFuelReserve);
        addUniqueId(state.meta.unlockKeys, content::unlock::deepSpace);
        addUniqueId(state.meta.unlockKeys, content::unlock::droneBay);
        addUniqueId(state.meta.unlockKeys, content::unlock::perimeterDrones);
        state.meta.droneBaySlots = std::max(state.meta.droneBaySlots, 3);
        const std::array<std::string_view, 2> arkfallCombatDrones {
            content::drone::attackDrone,
            content::drone::defenseDrone
        };
        for (const std::string_view droneId : arkfallCombatDrones) {
            addUniqueId(state.meta.ownedDroneIds, std::string(droneId));
            const auto upgrade = std::find_if(
                state.meta.droneUpgrades.begin(),
                state.meta.droneUpgrades.end(),
                [droneId](const DroneUpgradeRecord& record) { return record.droneId == droneId; });
            if (upgrade == state.meta.droneUpgrades.end()) {
                state.meta.droneUpgrades.push_back({std::string(droneId), 1});
            }
        }
        const int hostileIndex = destinationIndexForId(catalog, content::destination::nearbyStar);
        if (hostileIndex >= 0) {
            state.run.destinationIndex = hostileIndex;
            state.launchConfig.destinationId = content::destination::nearbyStar;
            state.launchConfig.burnGoalMultiplier = catalog.destinations[static_cast<std::size_t>(hostileIndex)].targetMultiplier;
        }
        state.screen = Screen::Navigation;
        state.statusLine = "Gravity well impact. The Ark is stranded; Mk I Attack and Defense drones are online, with at least 3 Drone Bay slots. Choose shuttle sorties from the local system.";
        syncLaunchConfig(state, catalog);
        return true;
    }

    state.statusLine = "The Ark cannot jump until alien artifacts and fuel systems are recovered.";
    return false;
}

bool selectNavigationDestination(GameState& state, const ContentCatalog& catalog, int index)
{
    const std::vector<const Destination*> destinations = navigationDestinations(state, catalog);
    if (index < 0 || index >= static_cast<int>(destinations.size())) {
        state.statusLine = "No navigable destination selected.";
        return false;
    }

    const Destination& destination = *destinations[static_cast<std::size_t>(index)];
    const int fuelCost = navigationFuelCost(destination);
    if (state.meta.ark.fuelReserve < fuelCost) {
        state.statusLine = "Ark fuel reserve is short for " + destination.name + ". Recover fuel or choose a closer sortie.";
        return false;
    }
    const int destinationIndex = destinationIndexForId(catalog, destination.id);
    if (destinationIndex < 0) {
        state.statusLine = "Navigation target is not in the catalog.";
        return false;
    }

    state.meta.navigation.selectedDestinationId = destination.id;
    state.meta.ark.fuelReserve = std::max(0, state.meta.ark.fuelReserve - fuelCost);
    addUniqueId(state.meta.navigation.discoveredDestinationIds, destination.id);
    state.run.destinationIndex = destinationIndex;
    state.launchConfig.frontierTransfer = true;
    state.launchConfig.destinationId = destination.id;
    state.launchConfig.burnGoalMultiplier = destination.targetMultiplier;
    syncLaunchConfig(state, catalog);
    state.screen = Screen::Hangar;
    state.statusLine = "Course plotted from the Ark to " + destination.name + ". Shared fuel spent: -" + std::to_string(fuelCost) + ". Prep the shuttle, then launch.";
    return true;
}

int frontierReadinessRequired(const GameState& state, const ContentCatalog& catalog)
{
    const Destination& destination = currentDestination(state, catalog);
    if (nextDestination(state, catalog) == nullptr) {
        return 0;
    }
    if (destination.id == content::destination::moon) {
        return tuning::mission::moonReadinessRequired;
    }
    return tuning::mission::readinessBaseRequired + destination.tier;
}

int frontierReadinessCap(const GameState& state, const ContentCatalog& catalog)
{
    const int required = frontierReadinessRequired(state, catalog);
    if (required <= 0) {
        return 0;
    }
    return required + tuning::mission::readinessOverCap;
}

const Destination* nextDestination(const GameState& state, const ContentCatalog& catalog)
{
    if (!hostileSystemActive(state)) {
        const int neptuneIndex = destinationIndexForId(catalog, content::destination::neptune);
        if (neptuneIndex >= 0 && state.run.destinationIndex >= neptuneIndex) {
            return nullptr;
        }
    }

    const int nextIndex = state.run.destinationIndex + 1;
    if (nextIndex < 0 || nextIndex >= static_cast<int>(catalog.destinations.size())) {
        return nullptr;
    }
    return &catalog.destinations[static_cast<std::size_t>(nextIndex)];
}

bool canCommitToNextFrontier(const GameState& state, const ContentCatalog& catalog)
{
    const int required = frontierReadinessRequired(state, catalog);
    return nextDestination(state, catalog) != nullptr && required > 0 && state.run.frontierReadiness >= required;
}

bool bankFrontierReadiness(GameState& state, const ContentCatalog& catalog)
{
    if (frontierReadinessRequired(state, catalog) <= 0) {
        return false;
    }

    const int before = state.run.frontierReadiness;
    state.run.frontierReadiness = std::min(frontierReadinessCap(state, catalog), state.run.frontierReadiness + 1);
    return state.run.frontierReadiness > before;
}

double missionPressureModifier(const GameState& state, const ContentCatalog& catalog, const Destination& destination)
{
    const int index = destinationIndexForId(catalog, destination.id);
    if (index < 0) {
        return tuning::mission::unknownDestinationDifficulty;
    }

    const auto attemptsIndex = static_cast<std::size_t>(index);
    const int attempts = attemptsIndex < state.meta.destinationAttempts.size() ? state.meta.destinationAttempts[attemptsIndex] : 0;
    const int successes = attemptsIndex < state.meta.destinationSuccesses.size() ? state.meta.destinationSuccesses[attemptsIndex] : 0;

    if (attempts <= 0) {
        return tuning::mission::unattemptedDifficulty;
    }
    if (successes <= 0) {
        return std::max(tuning::mission::failedAttemptDifficultyFloor, tuning::mission::failedAttemptDifficultyBase / static_cast<double>(attempts));
    }
    return std::max(tuning::mission::provenDifficultyFloor, tuning::mission::provenDifficultyBase / static_cast<double>(successes + 1));
}

bool commitToNextFrontier(GameState& state, const ContentCatalog& catalog)
{
    const Destination* next = nextDestination(state, catalog);
    if (next == nullptr) {
        state.statusLine = std::string(text::status::noFartherFrontier);
        return false;
    }

    const int required = frontierReadinessRequired(state, catalog);
    if (state.run.frontierReadiness < required) {
        state.statusLine = text::moreFlightDataNeeded(next->name);
        return false;
    }

    state.run.destinationIndex += 1;
    state.run.frontierReadiness = 0;
    state.meta.furthestTier = std::max(state.meta.furthestTier, next->tier);
    state.launchConfig.frontierTransfer = false;
    state.launchConfig.destinationId = next->id;
    state.launchConfig.burnGoalMultiplier = defaultProvingTarget(*next);
    syncLaunchConfig(state, catalog);
    state.statusLine = text::transferAchieved(next->name);
    return true;
}

void unlockFromBlueprints(GameState& state)
{
    for (const auto& threshold : tuning::unlocks::blueprintUnlocks) {
        if (state.meta.blueprintProgress >= threshold.threshold && !hasUnlock(state.meta, threshold.key)) {
            state.meta.unlockKeys.push_back(std::string(threshold.key));
            state.statusLine = std::string(threshold.message);
        }
    }
}

void updateLegacyRecords(MetaProgress& meta, const LaunchOutcome& outcome)
{
    const double creditDelta = outcome.payout - outcome.recoveryCost;
    meta.maxBurnDepth = std::max(meta.maxBurnDepth, outcome.ejectMultiplier);
    meta.maxPeakWarning = std::max(meta.maxPeakWarning, outcome.peakWarning);
    meta.maxPeakAbortRisk = std::max(meta.maxPeakAbortRisk, outcome.peakAbortRisk);
    meta.bestCreditDelta = std::max(meta.bestCreditDelta, creditDelta);
    meta.worstCreditDelta = std::min(meta.worstCreditDelta, creditDelta);

    const double survivalMargin = outcome.crashMultiplier - outcome.ejectMultiplier;
    if (outcome.type != LaunchResultType::Destroyed && survivalMargin > 0.0
        && (meta.closestSurvivalMargin <= 0.0 || survivalMargin < meta.closestSurvivalMargin)) {
        meta.closestSurvivalMargin = survivalMargin;
        meta.closestSurvivalBurn = outcome.ejectMultiplier;
        meta.closestSurvivalFailurePoint = outcome.crashMultiplier;
    }
}

bool isSkinOfYourTeethOutcome(const LaunchOutcome& outcome)
{
    const double survivalMargin = outcome.crashMultiplier - outcome.ejectMultiplier;
    return outcome.type != LaunchResultType::Destroyed &&
        survivalMargin > 0.0 &&
        survivalMargin <= tuning::records::closeCallSurvivalMargin;
}

void applyLaunchOutcome(GameState& state, const ContentCatalog& catalog, const LaunchOutcome& rawOutcome)
{
    LaunchOutcome outcome = rawOutcome;
    if (isSkinOfYourTeethOutcome(outcome)) {
        outcome.payout *= 1.0 + tuning::records::skinOfYourTeethCreditBonus;
    }

    ensureDestinationHistory(state, catalog);
    state.lastOutcome = outcome;
    updateLegacyRecords(state.meta, outcome);
    state.launchConfig.frontierTransfer = false;
    state.run.launchesThisExpedition += 1;
    state.run.shipDamage = std::clamp(state.run.shipDamage + outcome.shipDamage, 0, tuning::damage::destroyedShipDamage);
    state.meta.blueprintProgress += outcome.blueprintGain;
    unlockFromBlueprints(state);
    const int outcomeDestinationIndex = destinationIndexForId(catalog, outcome.destinationId);
    const Destination* outcomeDestination = catalog.findDestination(outcome.destinationId);
    const bool solarFrontierAdvance = outcomeDestination != nullptr
        && outcomeDestination->tier == state.run.destinationIndex + 1;
    const bool selectedHostileSortie = hostileSystemActive(state)
        && outcomeDestinationIndex == state.run.destinationIndex;
    const bool validDestinationSuccess = outcome.type == LaunchResultType::MissionComplete
        && outcomeDestination != nullptr
        && (!outcome.frontierTransfer || solarFrontierAdvance || selectedHostileSortie);
    if (outcomeDestinationIndex >= 0) {
        const auto index = static_cast<std::size_t>(outcomeDestinationIndex);
        state.meta.destinationAttempts[index] += 1;
        if (validDestinationSuccess) {
            state.meta.destinationSuccesses[index] += 1;
        }
    }
    const bool shallowRecovery = outcomeDestination != nullptr && isShallowRecoveryOutcome(*outcomeDestination, outcome);
    const bool cleanShallowRecovery = outcomeDestination != nullptr && isCleanShallowRecoveryOutcome(*outcomeDestination, outcome);
    const bool cleanShallowRecoveryDestroyed = outcome.type == LaunchResultType::Destroyed
        && cleanShallowRecovery
        && state.run.cleanShallowRecoveryStreak + 1 >= tuning::rewards::cleanShallowRecoveryDestructionStreak;

    if (outcome.type != LaunchResultType::Destroyed && shallowRecovery) {
        state.run.shallowRecoveryStreak += 1;
        state.run.cleanShallowRecoveryStreak = cleanShallowRecovery ? state.run.cleanShallowRecoveryStreak + 1 : 0;
    } else {
        state.run.shallowRecoveryStreak = 0;
        state.run.cleanShallowRecoveryStreak = 0;
    }

    Astronaut* astronaut = activeAstronaut(state);
    if (astronaut != nullptr) {
        const CrewUpgradeStats upgrades = aggregateCrewUpgradeStats(state, catalog);
        astronaut->stress = std::min(tuning::crew::maxStress, astronaut->stress + postLaunchCrewStressGain(outcome, upgrades));
        if (outcome.crewKilled) {
            astronaut->training = 0;
            astronaut->status = CrewStatus::Dead;
            state.meta.astronautsLost += 1;
            state.meta.memorials.push_back(astronaut->name + " lost during " + outcome.destinationId);
        } else if (outcome.crewInjured) {
            astronaut->status = CrewStatus::Injured;
        }
    }

    if (!outcome.moduleDestroyedId.empty() && outcome.type != LaunchResultType::Destroyed) {
        state.run.equippedModuleIds.erase(std::remove(state.run.equippedModuleIds.begin(), state.run.equippedModuleIds.end(), outcome.moduleDestroyedId), state.run.equippedModuleIds.end());
        state.run.inventoryModuleIds.erase(std::remove(state.run.inventoryModuleIds.begin(), state.run.inventoryModuleIds.end(), outcome.moduleDestroyedId), state.run.inventoryModuleIds.end());
    }

    if (outcome.type == LaunchResultType::Destroyed) {
        state.meta.shipsLost += 1;
        state.run.frontierReadiness = std::max(0, state.run.frontierReadiness - 1);
        state.run.credits = std::max(tuning::hangar::minimumExpeditionCredits, state.run.credits - tuning::mission::destroyedCreditPenalty);
        state.run.surfaceUpgradeIds.clear();
        state.run.active = false;
        if (cleanShallowRecoveryDestroyed) {
            state.statusLine = std::string(text::status::cleanShallowRecoveryDestroyed);
        } else if (outcome.recoveryMethod == RecoveryMethod::ReturnHome) {
            state.statusLine = std::string(text::status::returnVehicleLost);
        } else if (outcome.frontierTransfer) {
            state.statusLine = std::string(text::status::transferVehicleLost);
        } else {
            state.statusLine = std::string(text::status::vehicleLost);
        }
    } else {
        state.run.restOpsThisExpedition = 0;
        state.run.credits = std::max(0.0, state.run.credits + outcome.payout - outcome.recoveryCost);
        const Destination* destination = outcomeDestination;
        if (destination != nullptr) {
            if (!outcome.frontierTransfer || outcome.type == LaunchResultType::MissionComplete) {
                state.meta.furthestTier = std::max(state.meta.furthestTier, destination->tier);
            }
            std::ostringstream famous;
            famous << destination->name << " at x" << outcome.ejectMultiplier;
            state.meta.famousLaunches.push_back(famous.str());
        }

        if (outcome.frontierTransfer) {
            if (outcome.type == LaunchResultType::MissionComplete) {
                if (destination != nullptr && solarFrontierAdvance) {
                    state.run.destinationIndex += 1;
                    state.run.frontierReadiness = 0;
                    state.meta.furthestTier = std::max(state.meta.furthestTier, destination->tier);
                    state.launchConfig.destinationId = destination->id;
                    state.launchConfig.burnGoalMultiplier = defaultProvingTarget(*destination);
                    state.statusLine = text::transferAchievedNewRoute(destination->name);
                    if (destination->id == content::destination::jupiter ||
                        destination->id == content::destination::saturn ||
                        destination->id == content::destination::uranus) {
                        state.run.frontierReadiness = frontierReadinessRequired(state, catalog);
                    }
                } else if (destination != nullptr && selectedHostileSortie) {
                    state.statusLine = text::transferAchievedNewRoute(destination->name);
                } else {
                    state.statusLine = std::string(text::status::transferLedgerRejected);
                }
            } else {
                if (destination != nullptr && outcome.ejectMultiplier >= destination->targetMultiplier * tuning::outcomes::transferUsefulDataTargetShare) {
                    state.run.frontierReadiness = std::min(frontierReadinessCap(state, catalog), state.run.frontierReadiness + 1);
                    state.statusLine = outcome.recoveryMethod == RecoveryMethod::ManualEject
                        ? std::string(text::status::transferAbortedEject)
                        : std::string(text::status::transferAbortedReturn);
                } else {
                    state.statusLine = outcome.recoveryMethod == RecoveryMethod::ManualEject
                        ? std::string(text::status::transferEjectEarly)
                        : std::string(text::status::transferReturnEarly);
                }
            }
        } else if (outcome.type == LaunchResultType::MissionComplete) {
            state.run.frontierReadiness = std::min(frontierReadinessCap(state, catalog), state.run.frontierReadiness + 1);
            if (canCommitToNextFrontier(state, catalog)) {
                const Destination* next = nextDestination(state, catalog);
                const int required = frontierReadinessRequired(state, catalog);
                state.statusLine = state.run.frontierReadiness > required
                    ? std::string(text::status::extraProvingData)
                    : text::fullProfileReturned(next == nullptr ? std::string_view("the next route") : std::string_view(next->name));
            } else {
                state.statusLine = std::string(text::status::missionDataBanked);
            }
        } else {
            const Destination& current = currentDestination(state, catalog);
            const double usefulDataTargetShare = outcome.recoveryMethod == RecoveryMethod::ManualEject
                ? tuning::outcomes::manualEjectUsefulDataTargetShare
                : tuning::outcomes::returnUsefulDataTargetShare;
            const double usefulDataThreshold = current.targetMultiplier * usefulDataTargetShare;
            if (outcome.ejectMultiplier >= usefulDataThreshold && frontierReadinessRequired(state, catalog) > 0) {
                state.run.frontierReadiness = std::min(frontierReadinessCap(state, catalog), state.run.frontierReadiness + 1);
                state.statusLine = outcome.recoveryMethod == RecoveryMethod::ManualEject
                    ? std::string(text::status::emergencyEjectUseful)
                    : std::string(text::status::earlyReturnUseful);
            } else {
                state.statusLine = outcome.recoveryMethod == RecoveryMethod::ManualEject
                    ? std::string(text::status::emergencyEjectShallow)
                    : std::string(text::status::earlyReturnShallow);
            }
        }
    }

    if (outcome.type == LaunchResultType::MissionComplete &&
        outcomeDestination != nullptr &&
        outcomeDestination->id == content::destination::neptune &&
        !state.meta.straylightDiscoveryAcknowledged) {
        scheduleStoryBriefing(state, StoryBriefingId::StraylightDiscovery, Screen::ArrivalOps);
    }

    syncLaunchConfig(state, catalog);
}

ModuleStats aggregateShipStats(const GameState& state, const ContentCatalog& catalog)
{
    ModuleStats stats;
    if (const ShipFrame* frame = catalog.findFrame(state.run.frameId)) {
        stats += frame->baseStats;
    }

    for (const std::string& moduleId : state.run.equippedModuleIds) {
        if (const ShipModule* module = catalog.findModule(moduleId)) {
            stats += module->stats;
        }
    }

    const double damagePenalty = static_cast<double>(state.run.shipDamage) / static_cast<double>(tuning::damage::destroyedShipDamage);
    stats.hull -= damagePenalty * tuning::damage::hullPenaltyPerDamage;
    stats.cooling -= damagePenalty * tuning::damage::coolingPenaltyPerDamage;
    stats.escape -= damagePenalty * tuning::damage::escapePenaltyPerDamage;

    return stats;
}

CrewUpgradeStats aggregateCrewUpgradeStats(const GameState& state, const ContentCatalog& catalog)
{
    CrewUpgradeStats stats;
    for (const std::string& upgradeId : state.run.crewUpgradeIds) {
        if (const CrewUpgrade* upgrade = catalog.findCrewUpgrade(upgradeId)) {
            stats += upgrade->stats;
        }
    }
    return stats;
}

Astronaut* activeAstronaut(GameState& state)
{
    auto found = std::find_if(state.run.crew.begin(), state.run.crew.end(), [](const Astronaut& astronaut) {
        return astronaut.status != CrewStatus::Dead;
    });
    return found == state.run.crew.end() ? nullptr : &*found;
}

const Astronaut* activeAstronaut(const GameState& state)
{
    auto found = std::find_if(state.run.crew.begin(), state.run.crew.end(), [](const Astronaut& astronaut) {
        return astronaut.status != CrewStatus::Dead;
    });
    return found == state.run.crew.end() ? nullptr : &*found;
}

const Destination& currentDestination(const GameState& state, const ContentCatalog& catalog)
{
    const int index = std::clamp(state.run.destinationIndex, 0, static_cast<int>(catalog.destinations.size()) - 1);
    return catalog.destinations[static_cast<std::size_t>(index)];
}

} // namespace rocket
