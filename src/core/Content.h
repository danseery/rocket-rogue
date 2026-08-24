#pragma once

#include "core/GameTypes.h"

#include <string>
#include <string_view>

namespace rocket {

struct ContentCatalog {
    std::vector<ShipModule> modules;
    std::vector<CrewUpgrade> crewUpgrades;
    std::vector<SurfaceUpgrade> surfaceUpgrades;
    std::vector<MiniDrone> miniDrones;
    std::vector<DroneModuleDefinition> droneModules;
    std::vector<DroneSynergyDefinition> droneSynergies;
    std::vector<ResearchProject> researchProjects;
    std::vector<ShipFrame> frames;
    std::vector<Astronaut> astronauts;
    std::vector<Destination> destinations;
    std::vector<TransferAssistDefinition> transferAssists;
    std::vector<ScenarioDefinition> scenarios;
    std::vector<ScenarioFactoryDefinition> scenarioFactories;
    std::vector<MiningSiteDefinition> miningSites;

    const ShipModule* findModule(std::string_view id) const;
    const CrewUpgrade* findCrewUpgrade(std::string_view id) const;
    const SurfaceUpgrade* findSurfaceUpgrade(std::string_view id) const;
    const MiniDrone* findMiniDrone(std::string_view id) const;
    const DroneModuleDefinition* findDroneModule(std::string_view id) const;
    const DroneSynergyDefinition* findDroneSynergy(std::string_view id) const;
    const ResearchProject* findResearchProject(std::string_view id) const;
    const ShipFrame* findFrame(std::string_view id) const;
    const Astronaut* findAstronaut(std::string_view id) const;
    const Destination* findDestination(std::string_view id) const;
    const TransferAssistDefinition* findTransferAssist(std::string_view id) const;
    const ScenarioDefinition* findScenario(std::string_view id) const;
    const ScenarioFactoryDefinition* findScenarioFactory(std::string_view id) const;
    const MiningSiteDefinition* findMiningSite(std::string_view id) const;
};

ContentCatalog createDefaultContent();

bool hasUnlock(const MetaProgress& meta, std::string_view key);
std::string unlockDisplayName(std::string_view key);
bool isModuleUnlocked(const MetaProgress& meta, const ShipModule& module);
bool isCrewUpgradeUnlocked(const MetaProgress& meta, const CrewUpgrade& upgrade);
bool isMiniDroneUnlocked(const MetaProgress& meta, const MiniDrone& drone);
std::vector<const ShipModule*> unlockedModules(const ContentCatalog& catalog, const MetaProgress& meta);
std::vector<const CrewUpgrade*> unlockedCrewUpgrades(const ContentCatalog& catalog, const MetaProgress& meta);
std::vector<const MiniDrone*> unlockedMiniDrones(const ContentCatalog& catalog, const MetaProgress& meta);

} // namespace rocket
