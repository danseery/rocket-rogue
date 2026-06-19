#pragma once

#include "core/Content.h"
#include "core/Random.h"

namespace rocket {

GameState createNewGame(const ContentCatalog& catalog, std::uint64_t seed);

void startNewExpedition(GameState& state, const ContentCatalog& catalog);
void syncLaunchConfig(GameState& state, const ContentCatalog& catalog);
void generateModuleOffers(GameState& state, const ContentCatalog& catalog, Random& rng);
bool buyOffer(GameState& state, const ContentCatalog& catalog, int index);
bool repairShip(GameState& state);
bool trainCrew(GameState& state);
bool restCrew(GameState& state);
bool recruitCrew(GameState& state, const ContentCatalog& catalog);
bool commitToNextFrontier(GameState& state, const ContentCatalog& catalog);
double defaultProvingTarget(const Destination& destination);
void unlockFromBlueprints(GameState& state);
void applyLaunchOutcome(GameState& state, const ContentCatalog& catalog, const LaunchOutcome& outcome);
int frontierReadinessRequired(const GameState& state, const ContentCatalog& catalog);
bool canCommitToNextFrontier(const GameState& state, const ContentCatalog& catalog);

ModuleStats aggregateShipStats(const GameState& state, const ContentCatalog& catalog);
Astronaut* activeAstronaut(GameState& state);
const Astronaut* activeAstronaut(const GameState& state);
const Destination& currentDestination(const GameState& state, const ContentCatalog& catalog);
const Destination* nextDestination(const GameState& state, const ContentCatalog& catalog);

} // namespace rocket
