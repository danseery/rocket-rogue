#include "core/GameState.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace rocket {

namespace {

int moduleCost(const ShipModule& module)
{
    switch (module.rarity) {
    case Rarity::Common:
        return 28;
    case Rarity::Uncommon:
        return 48;
    case Rarity::Rare:
        return 74;
    case Rarity::Prototype:
        return 105;
    }
    return 35;
}

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

} // namespace

double defaultProvingTarget(const Destination& destination)
{
    return std::clamp(1.0 + (destination.targetMultiplier - 1.0) * 0.55, 1.15, destination.targetMultiplier);
}

GameState createNewGame(const ContentCatalog& catalog, std::uint64_t seed)
{
    GameState state;
    state.seed = seed;
    state.meta.unlockKeys = {"starter"};
    state.run.credits = 100.0;
    state.run.crew = catalog.astronauts;
    startNewExpedition(state, catalog);

    Random rng(seed);
    generateModuleOffers(state, catalog, rng);

    state.statusLine = "Program initialized. Prove the current frontier, then commit outward.";
    return state;
}

void startNewExpedition(GameState& state, const ContentCatalog& catalog)
{
    state.screen = Screen::Hangar;
    state.run.active = true;
    state.run.destinationIndex = std::clamp(state.meta.furthestTier, 0, static_cast<int>(catalog.destinations.size()) - 1);
    state.run.frontierReadiness = std::clamp(state.run.frontierReadiness, 0, frontierReadinessRequired(state, catalog));
    state.run.shipDamage = 0;
    state.run.frameId = catalog.frames.empty() ? "" : catalog.frames.front().id;
    state.run.inventoryModuleIds = starterInventory();
    state.run.equippedModuleIds = starterInventory();
    state.run.launchesThisExpedition = 0;
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
    state.launchConfig.targetEjectMultiplier = defaultProvingTarget(currentDestination(state, catalog));
}

void syncLaunchConfig(GameState& state, const ContentCatalog& catalog)
{
    const Destination* destination = catalog.findDestination(state.launchConfig.destinationId);
    if (destination == nullptr || !state.launchConfig.frontierTransfer) {
        destination = &currentDestination(state, catalog);
        state.launchConfig.destinationId = destination->id;
    }
    state.launchConfig.frameId = state.run.frameId;
    state.launchConfig.equippedModuleIds = state.run.equippedModuleIds;

    if (state.launchConfig.targetEjectMultiplier < 1.05 || state.launchConfig.targetEjectMultiplier > destination->targetMultiplier + 1.5) {
        state.launchConfig.targetEjectMultiplier = state.launchConfig.frontierTransfer ? destination->targetMultiplier : defaultProvingTarget(*destination);
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
    const auto pool = unlockedModules(catalog, state.meta);
    if (pool.empty()) {
        state.run.offerModuleIds = {};
        return;
    }

    for (auto& offerId : state.run.offerModuleIds) {
        const ShipModule* picked = pool[static_cast<std::size_t>(rng.rangeInt(0, static_cast<int>(pool.size()) - 1))];
        offerId = picked->id;
    }
}

bool buyOffer(GameState& state, const ContentCatalog& catalog, int index)
{
    if (index < 0 || index >= static_cast<int>(state.run.offerModuleIds.size())) {
        return false;
    }

    const ShipModule* module = catalog.findModule(state.run.offerModuleIds[static_cast<std::size_t>(index)]);
    if (module == nullptr) {
        return false;
    }

    const int cost = moduleCost(*module);
    if (state.run.credits < static_cast<double>(cost)) {
        state.statusLine = "Insufficient mission credits for " + module->name + ".";
        return false;
    }

    state.run.credits -= static_cast<double>(cost);
    state.run.inventoryModuleIds.push_back(module->id);

    auto slotIt = std::find_if(state.run.equippedModuleIds.begin(), state.run.equippedModuleIds.end(), [&](const std::string& equippedId) {
        const ShipModule* equipped = catalog.findModule(equippedId);
        return equipped != nullptr && equipped->slot == module->slot;
    });

    if (slotIt != state.run.equippedModuleIds.end()) {
        *slotIt = module->id;
    }

    state.statusLine = "Installed " + module->name + ". The hangar crew looks nervous, which is usually good.";
    syncLaunchConfig(state, catalog);
    return true;
}

bool repairShip(GameState& state)
{
    if (state.run.shipDamage <= 0) {
        state.statusLine = "The ship is already flight-ready.";
        return false;
    }

    const int repaired = std::min(35, state.run.shipDamage);
    const double cost = std::max(8.0, static_cast<double>(repaired) * 1.15);
    if (state.run.credits < cost) {
        state.statusLine = "Not enough mission credits for repairs.";
        return false;
    }

    state.run.credits -= cost;
    state.run.shipDamage -= repaired;
    state.statusLine = "Repaired " + std::to_string(repaired) + " hull damage.";
    return true;
}

bool trainCrew(GameState& state)
{
    Astronaut* astronaut = activeAstronaut(state);
    if (astronaut == nullptr) {
        state.statusLine = "No active astronaut is available to train.";
        return false;
    }

    constexpr double cost = 18.0;
    if (state.run.credits < cost) {
        state.statusLine = "Training budget denied.";
        return false;
    }

    state.run.credits -= cost;
    astronaut->training = std::min(10, astronaut->training + 1);
    astronaut->stress = std::min(100, astronaut->stress + 6);
    state.statusLine = astronaut->name + " completed simulator burns.";
    return true;
}

bool restCrew(GameState& state)
{
    Astronaut* astronaut = activeAstronaut(state);
    if (astronaut == nullptr) {
        state.statusLine = "No active astronaut is available to rest.";
        return false;
    }

    constexpr double cost = 10.0;
    if (state.run.credits < cost) {
        state.statusLine = "No room in the budget for shore leave.";
        return false;
    }

    state.run.credits -= cost;
    astronaut->stress = std::max(0, astronaut->stress - 24);
    if (astronaut->status == CrewStatus::Injured) {
        astronaut->status = CrewStatus::Active;
        astronaut->stress = std::max(0, astronaut->stress - 12);
    }
    state.statusLine = astronaut->name + " is cleared for another reckless miracle.";
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
    state.launchConfig.targetEjectMultiplier = defaultProvingTarget(*next);
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
    state.lastOutcome = outcome;
    state.launchConfig.frontierTransfer = false;
    state.run.launchesThisExpedition += 1;
    state.run.shipDamage = std::clamp(state.run.shipDamage + outcome.shipDamage, 0, 100);
    state.meta.blueprintProgress += outcome.blueprintGain;
    unlockFromBlueprints(state);

    Astronaut* astronaut = activeAstronaut(state);
    if (astronaut != nullptr) {
        astronaut->stress = std::min(100, astronaut->stress + (outcome.type == LaunchResultType::Destroyed ? 34 : 12));
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
                    state.launchConfig.targetEjectMultiplier = defaultProvingTarget(*destination);
                    state.statusLine = "Transfer achieved: " + destination->name + ". New proving flights begin here.";
                } else {
                    state.statusLine = "Transfer data accepted, but the route ledger rejected the destination.";
                }
            } else {
                if (destination != nullptr && outcome.ejectMultiplier >= destination->targetMultiplier * 0.55) {
                    state.run.frontierReadiness = std::min(frontierReadinessRequired(state, catalog), state.run.frontierReadiness + 1);
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
            const int required = frontierReadinessRequired(state, catalog);
            state.run.frontierReadiness = std::min(required, state.run.frontierReadiness + 1);
            if (canCommitToNextFrontier(state, catalog)) {
                const Destination* next = nextDestination(state, catalog);
                state.statusLine = "Full proving profile returned. Attempt the transfer to " + (next == nullptr ? std::string("the next route") : next->name) + " when ready.";
            } else {
                state.statusLine = "Mission data banked. Keep testing, upgrading, and deciding how bold the next burn should be.";
            }
        } else {
            const Destination& current = currentDestination(state, catalog);
            const double usefulDataThreshold = outcome.recoveryMethod == RecoveryMethod::ManualEject ? current.targetMultiplier * 0.90 : current.targetMultiplier * 0.70;
            if (outcome.ejectMultiplier >= usefulDataThreshold && frontierReadinessRequired(state, catalog) > 0) {
                state.run.frontierReadiness = std::min(frontierReadinessRequired(state, catalog), state.run.frontierReadiness + 1);
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
