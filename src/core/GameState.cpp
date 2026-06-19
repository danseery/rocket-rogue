#include "core/GameState.h"

#include <algorithm>
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

std::vector<std::string> starterInventory()
{
    return {
        "sparrow_engine",
        "stable_tank",
        "patchwork_hull",
        "radiator_vanes",
        "analog_telemetry",
        "spring_capsule"
    };
}

void ensureDestinationHistory(GameState& state, const ContentCatalog& catalog)
{
    const std::size_t count = catalog.destinations.size();
    state.meta.destinationAttempts.resize(count, 0);
    state.meta.destinationSuccesses.resize(count, 0);
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

double escalatedHangarOpCost(double baseCost, int uses)
{
    const int safeUses = std::max(0, uses);
    return std::ceil(baseCost * std::pow(1.35, static_cast<double>(safeUses)) + static_cast<double>(safeUses) * 12.0);
}

} // namespace

int moduleOfferCost(Rarity rarity)
{
    switch (rarity) {
    case Rarity::Common:
        return 22;
    case Rarity::Uncommon:
        return 34;
    case Rarity::Rare:
        return 62;
    case Rarity::Prototype:
        return 92;
    }
    return 34;
}

int moduleOfferCost(const ShipModule& module)
{
    return moduleOfferCost(module.rarity);
}

int crewUpgradeCost(const CrewUpgrade& upgrade)
{
    return moduleOfferCost(upgrade.rarity);
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
    return std::clamp(stress, 0, 100) / 14;
}

int effectiveTrainingLevel(const Astronaut& astronaut)
{
    return astronaut.training - crewStressStepCount(astronaut.stress);
}

double crewNavigationPenaltyFromStress(int stress)
{
    return static_cast<double>(crewStressStepCount(stress)) * 0.022;
}

double crewAbortRiskMultiplierFromStress(int stress)
{
    return 1.0 + static_cast<double>(crewStressStepCount(stress)) / 7.0;
}

double defaultProvingTarget(const Destination& destination)
{
    return std::clamp(1.0 + (destination.targetMultiplier - 1.0) * 0.47, 1.15, destination.targetMultiplier);
}

GameState createNewGame(const ContentCatalog& catalog, std::uint64_t seed)
{
    GameState state;
    state.seed = seed;
    state.meta.unlockKeys = {"starter"};
    state.run.credits = 100.0;
    state.run.crew = catalog.astronauts;
    ensureDestinationHistory(state, catalog);
    startNewExpedition(state, catalog);

    Random rng(seed);
    generateModuleOffers(state, catalog, rng);

    state.statusLine = "Program initialized. Prove the current frontier, then commit outward.";
    return state;
}

void startNewExpedition(GameState& state, const ContentCatalog& catalog)
{
    ensureDestinationHistory(state, catalog);
    state.screen = Screen::Hangar;
    state.run.active = true;
    state.run.destinationIndex = std::clamp(state.meta.furthestTier, 0, static_cast<int>(catalog.destinations.size()) - 1);
    state.run.frontierReadiness = std::clamp(state.run.frontierReadiness, 0, frontierReadinessCap(state, catalog));
    state.run.shipDamage = 0;
    state.run.frameId = catalog.frames.empty() ? "" : catalog.frames.front().id;
    state.run.inventoryModuleIds = starterInventory();
    state.run.equippedModuleIds = starterInventory();
    state.run.offerModuleIds = {};
    state.run.offerCrewUpgradeIds = {};
    state.run.launchesThisExpedition = 0;
    state.run.offerRerollsThisExpedition = 0;
    state.run.repairOpsThisExpedition = 0;
    state.run.trainingOpsThisExpedition = 0;
    state.run.restOpsThisExpedition = 0;
    if (state.run.credits < 45.0) {
        state.run.credits = 45.0;
    }

    if (state.run.crew.empty()) {
        state.run.crew = catalog.astronauts;
    }

    if (activeAstronaut(state) == nullptr && !catalog.astronauts.empty()) {
        Astronaut recruit = catalog.astronauts.front();
        recruit.id = "replacement_" + std::to_string(state.meta.astronautsLost + state.meta.shipsLost + 1);
        recruit.name = "Replacement Cadet";
        recruit.background = "Emergency recruitment pool";
        recruit.trait = "Learns quickly";
        recruit.training = 0;
        recruit.stress = 15;
        recruit.status = CrewStatus::Active;
        state.run.crew.push_back(recruit);
    }

    for (auto& astronaut : state.run.crew) {
        if (astronaut.status == CrewStatus::Injured) {
            astronaut.stress = std::min(100, astronaut.stress + 8);
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

    if (state.launchConfig.burnGoalMultiplier < 1.05 || state.launchConfig.burnGoalMultiplier > destination->targetMultiplier + 1.5) {
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
            const bool alreadyOwned = std::find(
                state.run.inventoryModuleIds.begin(),
                state.run.inventoryModuleIds.end(),
                module->id) != state.run.inventoryModuleIds.end();
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
            return module != nullptr && state.run.credits >= static_cast<double>(moduleOfferCost(*module));
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
    return 10.0 * static_cast<double>(state.run.offerRerollsThisExpedition + 1);
}

bool rerollOffers(GameState& state, const ContentCatalog& catalog, Random& rng)
{
    const double cost = offerRerollCost(state);
    if (state.run.credits < cost) {
        state.statusLine = "Not enough mission credits to reroll the refit board.";
        return false;
    }

    state.run.credits -= cost;
    state.run.offerRerollsThisExpedition += 1;
    generateModuleOffers(state, catalog, rng);
    state.statusLine = "Refit offers rerolled. The next reroll will cost " + std::to_string(static_cast<int>(offerRerollCost(state))) + " credits.";
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
        state.statusLine = "Insufficient mission credits for " + (module != nullptr ? module->name : crewUpgrade->name) + ".";
        return false;
    }

    state.run.credits -= static_cast<double>(cost);
    if (module != nullptr) {
        state.run.inventoryModuleIds.push_back(module->id);

        auto slotIt = std::find_if(state.run.equippedModuleIds.begin(), state.run.equippedModuleIds.end(), [&](const std::string& equippedId) {
            const ShipModule* equipped = catalog.findModule(equippedId);
            return equipped != nullptr && equipped->slot == module->slot;
        });

        if (slotIt != state.run.equippedModuleIds.end()) {
            *slotIt = module->id;
        }
    } else {
        state.run.crewUpgradeIds.push_back(crewUpgrade->id);
    }

    state.run.offerModuleIds = {};
    state.run.offerCrewUpgradeIds = {};
    state.statusLine = "Installed " + (module != nullptr ? module->name : crewUpgrade->name) + ". The refit took the available hangar window.";
    syncLaunchConfig(state, catalog);
    return true;
}

int repairShipAmount(const GameState& state)
{
    return std::min(35, state.run.shipDamage);
}

double repairShipCost(const GameState& state)
{
    const int repaired = repairShipAmount(state);
    if (repaired <= 0) {
        return 0.0;
    }

    const double baseCost = 6.0 + static_cast<double>(repaired) * 0.42;
    return escalatedHangarOpCost(baseCost, state.run.repairOpsThisExpedition);
}

bool repairShip(GameState& state)
{
    if (state.run.shipDamage <= 0) {
        state.statusLine = "The ship is already flight-ready.";
        return false;
    }

    const int repaired = repairShipAmount(state);
    const double cost = repairShipCost(state);
    if (state.run.credits < cost) {
        state.statusLine = "Not enough mission credits for repairs.";
        return false;
    }

    state.run.credits -= cost;
    state.run.shipDamage -= repaired;
    state.run.repairOpsThisExpedition += 1;
    state.statusLine = "Repaired " + std::to_string(repaired) + " hull damage.";
    return true;
}

int crewTrainingStressGain(const GameState& state, const ContentCatalog& catalog)
{
    const CrewUpgradeStats upgrades = aggregateCrewUpgradeStats(state, catalog);
    return std::max(0, 6 - upgrades.trainingStressRelief);
}

double crewTrainingCost(const GameState& state, const ContentCatalog&)
{
    return escalatedHangarOpCost(10.0, state.run.trainingOpsThisExpedition);
}

double crewRestCost(const GameState& state, const ContentCatalog&)
{
    const Astronaut* astronaut = activeAstronaut(state);
    if (astronaut == nullptr) {
        return escalatedHangarOpCost(8.0, state.run.restOpsThisExpedition);
    }
    const double baseCost = 6.0 + static_cast<double>(astronaut->stress) * 0.06;
    return escalatedHangarOpCost(baseCost, state.run.restOpsThisExpedition);
}

bool trainCrew(GameState& state, const ContentCatalog& catalog)
{
    Astronaut* astronaut = activeAstronaut(state);
    if (astronaut == nullptr) {
        state.statusLine = "No active astronaut is available to train.";
        return false;
    }

    const int stressGain = crewTrainingStressGain(state, catalog);
    if (astronaut->stress >= 100 || astronaut->stress + stressGain > 100) {
        state.statusLine = astronaut->name + " is too stressed for simulator work. Rest the crew first.";
        return false;
    }

    const double cost = crewTrainingCost(state, catalog);
    if (state.run.credits < cost) {
        state.statusLine = "Training budget denied.";
        return false;
    }

    const CrewUpgradeStats upgrades = aggregateCrewUpgradeStats(state, catalog);
    const int trainingGain = std::max(1, 1 + upgrades.trainingGain);
    state.run.credits -= cost;
    astronaut->training = std::min(10, astronaut->training + trainingGain);
    astronaut->stress += stressGain;
    state.run.trainingOpsThisExpedition += 1;
    state.statusLine = astronaut->name + " completed simulator burns.";
    return true;
}

int crewRestStressRecovery(const GameState& state, const ContentCatalog& catalog)
{
    const CrewUpgradeStats upgrades = aggregateCrewUpgradeStats(state, catalog);
    const double difficulty = missionPressureModifier(state, catalog, currentDestination(state, catalog));
    const double recoveryFactor = std::clamp(1.0 - difficulty, 0.45, 0.95);
    return std::max(8, static_cast<int>(std::round(static_cast<double>(24 + upgrades.restStressBonus) * recoveryFactor)));
}

bool restCrew(GameState& state, const ContentCatalog& catalog)
{
    Astronaut* astronaut = activeAstronaut(state);
    if (astronaut == nullptr) {
        state.statusLine = "No active astronaut is available to rest.";
        return false;
    }

    const double cost = crewRestCost(state, catalog);
    if (state.run.credits < cost) {
        state.statusLine = "No room in the budget for shore leave.";
        return false;
    }

    const int stressRecovery = crewRestStressRecovery(state, catalog);
    state.run.credits -= cost;
    astronaut->stress = std::max(0, astronaut->stress - stressRecovery);
    if (astronaut->status == CrewStatus::Injured) {
        astronaut->status = CrewStatus::Active;
        astronaut->stress = std::max(0, astronaut->stress - std::max(4, stressRecovery / 2));
    }
    state.run.restOpsThisExpedition += 1;
    state.statusLine = astronaut->name + " recovered " + std::to_string(stressRecovery) + " stress under current mission conditions.";
    return true;
}

bool recruitCrew(GameState& state, const ContentCatalog& catalog)
{
    const bool emergencyRecruitment = activeAstronaut(state) == nullptr;
    const double cost = emergencyRecruitment ? 0.0 : 24.0;

    if (catalog.astronauts.empty()) {
        state.statusLine = "Mission control has no recruit profiles on file.";
        return false;
    }

    if (state.run.credits < cost) {
        state.statusLine = "Not enough mission credits to recruit new crew.";
        return false;
    }

    const Astronaut& templateAstronaut = catalog.astronauts[static_cast<std::size_t>(
        (state.meta.astronautsLost + state.meta.shipsLost + state.run.launchesThisExpedition + state.run.crew.size()) %
        static_cast<int>(catalog.astronauts.size()))];

    Astronaut recruit = templateAstronaut;
    const int recruitNumber = state.meta.astronautsLost + state.meta.shipsLost + static_cast<int>(state.run.crew.size()) + 1;
    recruit.id = "recruit_" + std::to_string(recruitNumber);
    recruit.name = emergencyRecruitment ? "Emergency Cadet " + std::to_string(recruitNumber) : templateAstronaut.name + " II";
    recruit.background = emergencyRecruitment ? "Emergency recruitment pool" : "New agency intake";
    recruit.training = emergencyRecruitment ? 0 : std::max(0, templateAstronaut.training - 1);
    recruit.stress = emergencyRecruitment ? 18 : 8;
    recruit.status = CrewStatus::Active;

    state.run.credits -= cost;
    state.run.crew.push_back(recruit);
    syncLaunchConfig(state, catalog);
    state.statusLine = emergencyRecruitment
        ? recruit.name + " was rushed in from the emergency recruitment pool."
        : recruit.name + " joined the roster.";
    return true;
}

int frontierReadinessRequired(const GameState& state, const ContentCatalog& catalog)
{
    const Destination& destination = currentDestination(state, catalog);
    if (destination.tier >= static_cast<int>(catalog.destinations.size()) - 1) {
        return 0;
    }
    return 3 + destination.tier;
}

int frontierReadinessCap(const GameState& state, const ContentCatalog& catalog)
{
    const int required = frontierReadinessRequired(state, catalog);
    if (required <= 0) {
        return 0;
    }
    return required + 3;
}

const Destination* nextDestination(const GameState& state, const ContentCatalog& catalog)
{
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

double missionPressureModifier(const GameState& state, const ContentCatalog& catalog, const Destination& destination)
{
    const int index = destinationIndexForId(catalog, destination.id);
    if (index < 0) {
        return 0.25;
    }

    const auto attemptsIndex = static_cast<std::size_t>(index);
    const int attempts = attemptsIndex < state.meta.destinationAttempts.size() ? state.meta.destinationAttempts[attemptsIndex] : 0;
    const int successes = attemptsIndex < state.meta.destinationSuccesses.size() ? state.meta.destinationSuccesses[attemptsIndex] : 0;

    if (attempts <= 0) {
        return 0.50;
    }
    if (successes <= 0) {
        return std::max(0.08, 0.25 / static_cast<double>(attempts));
    }
    return std::max(0.05, 0.14 / static_cast<double>(successes + 1));
}

bool commitToNextFrontier(GameState& state, const ContentCatalog& catalog)
{
    const Destination* next = nextDestination(state, catalog);
    if (next == nullptr) {
        state.statusLine = "No farther frontier is charted in this proof of concept.";
        return false;
    }

    const int required = frontierReadinessRequired(state, catalog);
    if (state.run.frontierReadiness < required) {
        state.statusLine = "More flight data is needed before committing to " + next->name + ".";
        return false;
    }

    state.run.destinationIndex += 1;
    state.run.frontierReadiness = 0;
    state.meta.furthestTier = std::max(state.meta.furthestTier, next->tier);
    state.launchConfig.frontierTransfer = false;
    state.launchConfig.destinationId = next->id;
    state.launchConfig.burnGoalMultiplier = defaultProvingTarget(*next);
    syncLaunchConfig(state, catalog);
    state.statusLine = "Transfer achieved: " + next->name + ". The proving route has moved outward.";
    return true;
}

void unlockFromBlueprints(GameState& state)
{
    const struct UnlockThreshold {
        int threshold;
        const char* key;
        const char* message;
    } thresholds[] = {
        {4, "thermal", "Thermal systems unlocked."},
        {8, "recovery", "Recovery hardware unlocked."},
        {12, "deep_space", "Deep-space module family unlocked."},
        {18, "ai", "Predictive guidance unlocked."},
        {24, "exotic", "Exotic prototype modules unlocked."}
    };

    for (const auto& threshold : thresholds) {
        if (state.meta.blueprintProgress >= threshold.threshold && !hasUnlock(state.meta, threshold.key)) {
            state.meta.unlockKeys.push_back(threshold.key);
            state.statusLine = threshold.message;
        }
    }
}

void applyLaunchOutcome(GameState& state, const ContentCatalog& catalog, const LaunchOutcome& outcome)
{
    ensureDestinationHistory(state, catalog);
    state.lastOutcome = outcome;
    state.launchConfig.frontierTransfer = false;
    state.run.launchesThisExpedition += 1;
    state.run.shipDamage = std::clamp(state.run.shipDamage + outcome.shipDamage, 0, 100);
    state.meta.blueprintProgress += outcome.blueprintGain;
    unlockFromBlueprints(state);
    const int outcomeDestinationIndex = destinationIndexForId(catalog, outcome.destinationId);
    if (outcomeDestinationIndex >= 0) {
        const auto index = static_cast<std::size_t>(outcomeDestinationIndex);
        state.meta.destinationAttempts[index] += 1;
        if (outcome.type == LaunchResultType::MissionComplete) {
            state.meta.destinationSuccesses[index] += 1;
        }
    }

    Astronaut* astronaut = activeAstronaut(state);
    if (astronaut != nullptr) {
        const CrewUpgradeStats upgrades = aggregateCrewUpgradeStats(state, catalog);
        const double warningLoad = std::clamp((outcome.peakWarning - 0.55) / 0.45, 0.0, 1.0);
        const double abortLoad = std::clamp((outcome.peakAbortRisk - 0.35) / 0.65, 0.0, 1.0);
        const int dangerStress = static_cast<int>(std::round(warningLoad * 8.0 + abortLoad * 8.0));
        const int stressGain = std::max(0, (outcome.type == LaunchResultType::Destroyed ? 34 : 12) + dangerStress - upgrades.launchStressRelief);
        astronaut->stress = std::min(100, astronaut->stress + stressGain);
        if (outcome.crewKilled) {
            astronaut->status = CrewStatus::Dead;
            state.meta.astronautsLost += 1;
            state.meta.memorials.push_back(astronaut->name + " lost during " + outcome.destinationId);
        } else if (outcome.crewInjured) {
            astronaut->status = CrewStatus::Injured;
        }
    }

    if (!outcome.moduleDestroyedId.empty()) {
        state.run.equippedModuleIds.erase(std::remove(state.run.equippedModuleIds.begin(), state.run.equippedModuleIds.end(), outcome.moduleDestroyedId), state.run.equippedModuleIds.end());
        state.run.inventoryModuleIds.erase(std::remove(state.run.inventoryModuleIds.begin(), state.run.inventoryModuleIds.end(), outcome.moduleDestroyedId), state.run.inventoryModuleIds.end());
    }

    if (outcome.type == LaunchResultType::Destroyed) {
        state.meta.shipsLost += 1;
        state.run.frontierReadiness = outcome.frontierTransfer ? 0 : std::max(0, state.run.frontierReadiness - 1);
        state.run.credits = std::max(45.0, state.run.credits - 30.0);
        state.run.active = false;
        if (outcome.recoveryMethod == RecoveryMethod::ReturnHome) {
            state.statusLine = "Return trajectory failed. Vehicle lost during recovery.";
        } else if (outcome.frontierTransfer) {
            state.statusLine = "Transfer vehicle lost. New crew, new vehicle, same frontier ledger.";
        } else {
            state.statusLine = "Vehicle lost. The agency recovered fragments and uncomfortable lessons.";
        }
    } else {
        state.run.credits = std::max(0.0, state.run.credits + outcome.payout - outcome.recoveryCost);
        const Destination* destination = catalog.findDestination(outcome.destinationId);
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
                if (destination != nullptr && destination->tier == state.run.destinationIndex + 1) {
                    state.run.destinationIndex += 1;
                    state.run.frontierReadiness = 0;
                    state.meta.furthestTier = std::max(state.meta.furthestTier, destination->tier);
                    state.launchConfig.destinationId = destination->id;
                    state.launchConfig.burnGoalMultiplier = defaultProvingTarget(*destination);
                    state.statusLine = "Transfer achieved: " + destination->name + ". New proving flights begin here.";
                } else {
                    state.statusLine = "Transfer data accepted, but the route ledger rejected the destination.";
                }
            } else {
                if (destination != nullptr && outcome.ejectMultiplier >= destination->targetMultiplier * 0.55) {
                    state.run.frontierReadiness = std::min(frontierReadinessCap(state, catalog), state.run.frontierReadiness + 1);
                    state.statusLine = outcome.recoveryMethod == RecoveryMethod::ManualEject
                        ? "Transfer aborted by ejection. Rescue was expensive, but the data record survived."
                        : "Transfer aborted. Vehicle returned with valuable long-burn data.";
                } else {
                    state.statusLine = outcome.recoveryMethod == RecoveryMethod::ManualEject
                        ? "Transfer ejection was early. Crew survived, but rescue ate the budget."
                        : "Transfer return was early. Crew survived, but the frontier remains unproven.";
                }
            }
        } else if (outcome.type == LaunchResultType::MissionComplete) {
            state.run.frontierReadiness = std::min(frontierReadinessCap(state, catalog), state.run.frontierReadiness + 1);
            if (canCommitToNextFrontier(state, catalog)) {
                const Destination* next = nextDestination(state, catalog);
                const int required = frontierReadinessRequired(state, catalog);
                state.statusLine = state.run.frontierReadiness > required
                    ? "Extra proving data banked. The curve is opening up, and so is the temptation."
                    : "Full proving profile returned. Attempt the transfer to " + (next == nullptr ? std::string("the next route") : next->name) + " when ready.";
            } else {
                state.statusLine = "Mission data banked. Keep testing, upgrading, and deciding how bold the next burn should be.";
            }
        } else {
            const Destination& current = currentDestination(state, catalog);
            const double usefulDataThreshold = outcome.recoveryMethod == RecoveryMethod::ManualEject ? current.targetMultiplier * 0.90 : current.targetMultiplier * 0.70;
            if (outcome.ejectMultiplier >= usefulDataThreshold && frontierReadinessRequired(state, catalog) > 0) {
                state.run.frontierReadiness = std::min(frontierReadinessCap(state, catalog), state.run.frontierReadiness + 1);
                state.statusLine = outcome.recoveryMethod == RecoveryMethod::ManualEject
                    ? "Emergency eject confirmed. Rescue recovered enough telemetry to matter."
                    : "Early return confirmed. Useful flight data recovered from the proving route.";
            } else {
                state.statusLine = outcome.recoveryMethod == RecoveryMethod::ManualEject
                    ? "Emergency eject confirmed. Crew survived, budget bruised, little data banked."
                    : "Early return confirmed. Safe, but the burn was too shallow to teach much.";
            }
        }
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

    const double damagePenalty = static_cast<double>(state.run.shipDamage) / 100.0;
    stats.hull -= damagePenalty * 2.2;
    stats.cooling -= damagePenalty * 1.2;
    stats.escape -= damagePenalty * 0.8;

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
