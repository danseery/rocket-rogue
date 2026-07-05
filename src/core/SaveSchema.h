#pragma once

#include <string_view>

namespace rocket::save_schema {

inline constexpr std::string_view header = "RR_SAVE_V1";
inline constexpr char keyValueDelimiter = '=';
inline constexpr char listDelimiter = ',';
inline constexpr char textListDelimiter = '|';
inline constexpr char crewRecordDelimiter = ';';
inline constexpr char crewFieldDelimiter = ':';
inline constexpr char artifactRecordDelimiter = ';';
inline constexpr char artifactFieldDelimiter = ':';

namespace field {

inline constexpr std::string_view version = "version";
inline constexpr std::string_view seed = "seed";
inline constexpr std::string_view credits = "credits";
inline constexpr std::string_view destinationIndex = "destinationIndex";
inline constexpr std::string_view frontierReadiness = "frontierReadiness";
inline constexpr std::string_view shipDamage = "shipDamage";
inline constexpr std::string_view frameId = "frameId";
inline constexpr std::string_view offerRerolls = "offerRerolls";
inline constexpr std::string_view repairOps = "repairOps";
inline constexpr std::string_view trainingOps = "trainingOps";
inline constexpr std::string_view restOps = "restOps";
inline constexpr std::string_view shallowRecoveryStreak = "shallowRecoveryStreak";
inline constexpr std::string_view cleanShallowRecoveryStreak = "cleanShallowRecoveryStreak";
inline constexpr std::string_view nextLaunchFuelBoost = "nextLaunchFuelBoost";
inline constexpr std::string_view nextLaunchSpeedBoost = "nextLaunchSpeedBoost";
inline constexpr std::string_view screen = "screen";
inline constexpr std::string_view campaignMilestone = "campaignMilestone";
inline constexpr std::string_view arkCondition = "arkCondition";
inline constexpr std::string_view arkFuelReserve = "arkFuelReserve";
inline constexpr std::string_view arkHullDamage = "arkHullDamage";
inline constexpr std::string_view arkRepairProgress = "arkRepairProgress";
inline constexpr std::string_view arkRepairModules = "arkRepairModules";
inline constexpr std::string_view arkFirstJumpComplete = "arkFirstJumpComplete";
inline constexpr std::string_view arkGravityWellDisaster = "arkGravityWellDisaster";
inline constexpr std::string_view navigationSystem = "navigationSystem";
inline constexpr std::string_view navigationArkLocation = "navigationArkLocation";
inline constexpr std::string_view navigationSelectedDestination = "navigationSelectedDestination";
inline constexpr std::string_view navigationDiscoveredDestinations = "navigationDiscoveredDestinations";
inline constexpr std::string_view inventory = "inventory";
inline constexpr std::string_view equipped = "equipped";
inline constexpr std::string_view ownedModules = "ownedModules";
inline constexpr std::string_view defaultEquippedModules = "defaultEquippedModules";
inline constexpr std::string_view crewUpgrades = "crewUpgrades";
inline constexpr std::string_view researchProjects = "researchProjects";
inline constexpr std::string_view arrivalActive = "arrivalActive";
inline constexpr std::string_view arrivalDestination = "arrivalDestination";
inline constexpr std::string_view surfaceActive = "surfaceActive";
inline constexpr std::string_view surfaceDestination = "surfaceDestination";
inline constexpr std::string_view surfaceSite = "surfaceSite";
inline constexpr std::string_view surfaceSupply = "surfaceSupply";
inline constexpr std::string_view surfaceSharedFuel = "surfaceSharedFuel";
inline constexpr std::string_view surfaceSharedFuelCapacity = "surfaceSharedFuelCapacity";
inline constexpr std::string_view surfaceCargo = "surfaceCargo";
inline constexpr std::string_view surfaceHazard = "surfaceHazard";
inline constexpr std::string_view surfaceDepth = "surfaceDepth";
inline constexpr std::string_view surfaceMaterials = "surfaceMaterials";
inline constexpr std::string_view surfaceArtifacts = "surfaceArtifacts";
inline constexpr std::string_view surfaceProspectMaterials = "surfaceProspectMaterials";
inline constexpr std::string_view surfaceProspectArtifacts = "surfaceProspectArtifacts";
inline constexpr std::string_view surfaceDepthProspects = "surfaceDepthProspects";
inline constexpr std::string_view surfaceEnemies = "surfaceEnemies";
inline constexpr std::string_view surfaceMiningPrepared = "surfaceMiningPrepared";
inline constexpr std::string_view surfaceMiningUsed = "surfaceMiningUsed";
inline constexpr std::string_view surfaceLog = "surfaceLog";
inline constexpr std::string_view surfaceUpgrades = "surfaceUpgrades";
inline constexpr std::string_view surfaceUpgradeOffers = "surfaceUpgradeOffers";
inline constexpr std::string_view surfaceUpgradeOfferAvailable = "surfaceUpgradeOfferAvailable";
inline constexpr std::string_view surfaceUpgradeOffersSeen = "surfaceUpgradeOffersSeen";
inline constexpr std::string_view miningActive = "miningActive";
inline constexpr std::string_view miningDestination = "miningDestination";
inline constexpr std::string_view miningSite = "miningSite";
inline constexpr std::string_view miningElapsed = "miningElapsed";
inline constexpr std::string_view miningOxygen = "miningOxygen";
inline constexpr std::string_view miningFuelBurn = "miningFuelBurn";
inline constexpr std::string_view miningFuelSpent = "miningFuelSpent";
inline constexpr std::string_view miningDrone = "miningDrone";
inline constexpr std::string_view miningAim = "miningAim";
inline constexpr std::string_view miningDrill = "miningDrill";
inline constexpr std::string_view miningDepth = "miningDepth";
inline constexpr std::string_view miningCargo = "miningCargo";
inline constexpr std::string_view miningMaterials = "miningMaterials";
inline constexpr std::string_view miningArtifacts = "miningArtifacts";
inline constexpr std::string_view miningHazard = "miningHazard";
inline constexpr std::string_view miningPassiveDroneYield = "miningPassiveDroneYield";
inline constexpr std::string_view miningCellsBroken = "miningCellsBroken";
inline constexpr std::string_view miningEnemies = "miningEnemies";
inline constexpr std::string_view miningCombat = "miningCombat";
inline constexpr std::string_view miningArtifact = "miningArtifact";
inline constexpr std::string_view miningTerrainSize = "miningTerrainSize";
inline constexpr std::string_view miningTerrainCells = "miningTerrainCells";
inline constexpr std::string_view unlocks = "unlocks";
inline constexpr std::string_view blueprints = "blueprints";
inline constexpr std::string_view materials = "materials";
inline constexpr std::string_view droneBaySlots = "droneBaySlots";
inline constexpr std::string_view ownedDrones = "ownedDrones";
inline constexpr std::string_view equippedDrones = "equippedDrones";
inline constexpr std::string_view artifacts = "artifacts";
inline constexpr std::string_view furthestTier = "furthestTier";
inline constexpr std::string_view shipsLost = "shipsLost";
inline constexpr std::string_view astronautsLost = "astronautsLost";
inline constexpr std::string_view closestSurvivalMargin = "closestSurvivalMargin";
inline constexpr std::string_view closestSurvivalBurn = "closestSurvivalBurn";
inline constexpr std::string_view closestSurvivalFailurePoint = "closestSurvivalFailurePoint";
inline constexpr std::string_view maxBurnDepth = "maxBurnDepth";
inline constexpr std::string_view maxPeakWarning = "maxPeakWarning";
inline constexpr std::string_view maxPeakAbortRisk = "maxPeakAbortRisk";
inline constexpr std::string_view bestCreditDelta = "bestCreditDelta";
inline constexpr std::string_view worstCreditDelta = "worstCreditDelta";
inline constexpr std::string_view totalFlybyMisses = "totalFlybyMisses";
inline constexpr std::string_view totalFlybyGoods = "totalFlybyGoods";
inline constexpr std::string_view totalFlybyPerfects = "totalFlybyPerfects";
inline constexpr std::string_view destinationAttempts = "destinationAttempts";
inline constexpr std::string_view destinationSuccesses = "destinationSuccesses";
inline constexpr std::string_view destinationFlybys = "destinationFlybys";
inline constexpr std::string_view destinationOrbits = "destinationOrbits";
inline constexpr std::string_view destinationLandings = "destinationLandings";
inline constexpr std::string_view memorials = "memorials";
inline constexpr std::string_view famousLaunches = "famousLaunches";
inline constexpr std::string_view crew = "crew";

} // namespace field

} // namespace rocket::save_schema
