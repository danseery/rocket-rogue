#pragma once

#include "core/Content.h"
#include "core/Random.h"

#include <vector>

namespace rocket {

struct HangarOperationPreview {
    int repairAmount = 0;
    double repairCost = 0.0;
    bool repairAvailable = false;
    int trainingGain = 0;
    int trainingStressGain = 0;
    double trainingCost = 0.0;
    bool trainingAvailable = false;
    int restStressRecovery = 0;
    double restCost = 0.0;
    bool restNeeded = false;
    bool restAvailable = false;
    bool emergencyRecruitment = false;
    double recruitCost = 0.0;
    bool recruitAvailable = false;
};

struct PostLaunchCrewStress {
    int baseStress = 0;
    int warningStress = 0;
    int abortStress = 0;
    int relief = 0;
    int total = 0;
};

GameState createNewGame(const ContentCatalog& catalog, std::uint64_t seed);

int moduleOfferCost(Rarity rarity);
int moduleOfferCost(const ShipModule& module);
int crewUpgradeCost(const CrewUpgrade& upgrade);
bool canAffordMaterials(const MaterialInventory& owned, const MaterialInventory& cost);
bool canAffordModuleOffer(const GameState& state, const ShipModule& module);
bool spendMaterials(MaterialInventory& owned, const MaterialInventory& cost);
int crewStressStepCount(int stress);
int effectiveTrainingLevel(const Astronaut& astronaut);
double crewNavigationPenaltyFromStress(int stress);
double crewAbortRiskMultiplierFromStress(int stress);
PostLaunchCrewStress postLaunchCrewStress(const LaunchOutcome& outcome, const CrewUpgradeStats& upgrades);
int postLaunchCrewStressGain(const LaunchOutcome& outcome, const CrewUpgradeStats& upgrades);
void startNewExpedition(GameState& state, const ContentCatalog& catalog);
void syncLaunchConfig(GameState& state, const ContentCatalog& catalog);
void generateModuleOffers(GameState& state, const ContentCatalog& catalog, Random& rng);
double offerRerollCost(const GameState& state);
bool rerollOffers(GameState& state, const ContentCatalog& catalog, Random& rng);
bool buyOffer(GameState& state, const ContentCatalog& catalog, int index);
int repairShipAmount(const GameState& state);
double repairShipCost(const GameState& state);
bool repairShip(GameState& state);
int crewTrainingGain(const GameState& state, const ContentCatalog& catalog);
int crewTrainingStressGain(const GameState& state, const ContentCatalog& catalog);
double crewTrainingCost(const GameState& state, const ContentCatalog& catalog);
double crewRestCost(const GameState& state, const ContentCatalog& catalog);
bool trainCrew(GameState& state, const ContentCatalog& catalog);
bool restCrew(GameState& state, const ContentCatalog& catalog);
int crewRestStressRecovery(const GameState& state, const ContentCatalog& catalog);
double recruitCrewCost(const GameState& state);
HangarOperationPreview hangarOperationPreview(const GameState& state, const ContentCatalog& catalog);
std::vector<const Astronaut*> recruitCandidateTemplates(const GameState& state, const ContentCatalog& catalog, int count = 3);
bool recruitCrew(GameState& state, const ContentCatalog& catalog);
bool recruitCrew(GameState& state, const ContentCatalog& catalog, int candidateIndex);
bool commitToNextFrontier(GameState& state, const ContentCatalog& catalog);
bool bankFrontierReadiness(GameState& state, const ContentCatalog& catalog);
bool arkDiscovered(const GameState& state);
bool hostileSystemActive(const GameState& state);
bool navigationAvailable(const GameState& state);
GameChapter chapterForState(const GameState& state, const ContentCatalog& catalog);
void syncChapterProgress(GameState& state, const ContentCatalog& catalog);
bool migrateLegacyDeepSpaceFrontier(GameState& state, const ContentCatalog& catalog);
std::vector<const Destination*> navigationDestinations(const GameState& state, const ContentCatalog& catalog);
void discoverArk(GameState& state, const ContentCatalog& catalog);
bool performArkJump(GameState& state, const ContentCatalog& catalog);
bool selectNavigationDestination(GameState& state, const ContentCatalog& catalog, int index);
double defaultProvingTarget(const Destination& destination);
void unlockFromBlueprints(GameState& state);
void applyLaunchOutcome(GameState& state, const ContentCatalog& catalog, const LaunchOutcome& outcome);
int frontierReadinessRequired(const GameState& state, const ContentCatalog& catalog);
int frontierReadinessCap(const GameState& state, const ContentCatalog& catalog);
bool canCommitToNextFrontier(const GameState& state, const ContentCatalog& catalog);
double missionPressureModifier(const GameState& state, const ContentCatalog& catalog, const Destination& destination);

ModuleStats aggregateShipStats(const GameState& state, const ContentCatalog& catalog);
CrewUpgradeStats aggregateCrewUpgradeStats(const GameState& state, const ContentCatalog& catalog);
Astronaut* activeAstronaut(GameState& state);
const Astronaut* activeAstronaut(const GameState& state);
const Destination& currentDestination(const GameState& state, const ContentCatalog& catalog);
const Destination* nextDestination(const GameState& state, const ContentCatalog& catalog);

} // namespace rocket
