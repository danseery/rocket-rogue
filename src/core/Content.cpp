#include "core/Content.h"
#include "core/ContentIds.h"
#include "core/GameText.h"
#include "core/ScenarioSystem.h"
#include "core/PayloadTransfer.h"
#include "core/Tuning.h"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace rocket {

namespace {

ShipModule module(
    std::string id,
    std::string name,
    SlotType slot,
    Rarity rarity,
    ModuleStats stats,
    std::string unlockKey,
    std::vector<std::string> tags,
    MaterialInventory materialCost = {},
    RefitTrack refitTrack = RefitTrack::None,
    int refitRank = 0,
    std::string prerequisiteId = {},
    bool provingTier = false,
    LaunchUpgradeKind launchUpgradeKind = LaunchUpgradeKind::None,
    int launchUpgradeRank = 0,
    SurfaceDepthUpgradeKind surfaceDepthUpgradeKind = SurfaceDepthUpgradeKind::None,
    int surfaceDepthUpgradeRank = 0,
    int rigFuelLoopRank = 0)
{
    ShipModule result;
    result.id = std::move(id);
    result.name = std::move(name);
    result.slot = slot;
    result.rarity = rarity;
    result.stats = stats;
    result.materialCost = materialCost;
    result.unlockKey = std::move(unlockKey);
    result.tags = std::move(tags);
    result.refitTrack = refitTrack;
    result.refitRank = refitRank;
    result.prerequisiteId = std::move(prerequisiteId);
    result.provingTier = provingTier;
    result.launchUpgradeKind = launchUpgradeKind;
    result.launchUpgradeRank = launchUpgradeRank;
    result.surfaceDepthUpgradeKind = surfaceDepthUpgradeKind;
    result.surfaceDepthUpgradeRank = surfaceDepthUpgradeRank;
    result.rigFuelLoopRank = rigFuelLoopRank;
    return result;
}

SurfaceUpgrade surfaceUpgrade(
    std::string id,
    std::string name,
    std::string description,
    Rarity rarity,
    SurfaceUpgradeCategory category,
    SurfaceUpgradeStats stats,
    std::vector<std::string> tags)
{
    SurfaceUpgrade result;
    result.id = std::move(id);
    result.name = std::move(name);
    result.description = std::move(description);
    result.rarity = rarity;
    result.category = category;
    result.stats = stats;
    result.tags = std::move(tags);
    return result;
}

MiniDrone miniDrone(
    std::string id,
    std::string name,
    std::string description,
    Rarity rarity,
    MiniDroneRole role,
    MiniDroneStats stats,
    std::string unlockKey,
    std::vector<std::string> tags)
{
    MiniDrone result;
    result.id = std::move(id);
    result.name = std::move(name);
    result.description = std::move(description);
    result.rarity = rarity;
    result.role = role;
    result.stats = stats;
    result.unlockKey = std::move(unlockKey);
    result.tags = std::move(tags);
    return result;
}

ResearchProject researchProject(
    std::string id,
    std::string name,
    std::string description,
    Rarity rarity,
    int requiredDestinationTier,
    int blueprintGain,
    MaterialInventory materialCost,
    std::string unlockKey,
    std::string rewardUnlockKey,
    std::vector<std::string> tags)
{
    ResearchProject result;
    result.id = std::move(id);
    result.name = std::move(name);
    result.description = std::move(description);
    result.rarity = rarity;
    result.requiredDestinationTier = requiredDestinationTier;
    result.blueprintGain = blueprintGain;
    result.materialCost = materialCost;
    result.unlockKey = std::move(unlockKey);
    result.rewardUnlockKey = std::move(rewardUnlockKey);
    result.tags = std::move(tags);
    return result;
}

} // namespace

const ShipModule* ContentCatalog::findModule(std::string_view id) const
{
    const auto found = std::find_if(modules.begin(), modules.end(), [id](const ShipModule& module) {
        return module.id == id;
    });
    return found == modules.end() ? nullptr : &*found;
}

const CrewUpgrade* ContentCatalog::findCrewUpgrade(std::string_view id) const
{
    const auto found = std::find_if(crewUpgrades.begin(), crewUpgrades.end(), [id](const CrewUpgrade& upgrade) {
        return upgrade.id == id;
    });
    return found == crewUpgrades.end() ? nullptr : &*found;
}

const SurfaceUpgrade* ContentCatalog::findSurfaceUpgrade(std::string_view id) const
{
    const auto found = std::find_if(surfaceUpgrades.begin(), surfaceUpgrades.end(), [id](const SurfaceUpgrade& upgrade) {
        return upgrade.id == id;
    });
    return found == surfaceUpgrades.end() ? nullptr : &*found;
}

const MiniDrone* ContentCatalog::findMiniDrone(std::string_view id) const
{
    const auto found = std::find_if(miniDrones.begin(), miniDrones.end(), [id](const MiniDrone& drone) {
        return drone.id == id;
    });
    return found == miniDrones.end() ? nullptr : &*found;
}

const DroneModuleDefinition* ContentCatalog::findDroneModule(std::string_view id) const
{
    const auto found = std::find_if(droneModules.begin(), droneModules.end(), [id](const DroneModuleDefinition& module) {
        return module.id == id;
    });
    return found == droneModules.end() ? nullptr : &*found;
}

const DroneSynergyDefinition* ContentCatalog::findDroneSynergy(std::string_view id) const
{
    const auto found = std::find_if(droneSynergies.begin(), droneSynergies.end(), [id](const DroneSynergyDefinition& synergy) {
        return synergy.id == id;
    });
    return found == droneSynergies.end() ? nullptr : &*found;
}

const ResearchProject* ContentCatalog::findResearchProject(std::string_view id) const
{
    const auto found = std::find_if(researchProjects.begin(), researchProjects.end(), [id](const ResearchProject& project) {
        return project.id == id;
    });
    return found == researchProjects.end() ? nullptr : &*found;
}

const ShipFrame* ContentCatalog::findFrame(std::string_view id) const
{
    const auto found = std::find_if(frames.begin(), frames.end(), [id](const ShipFrame& frame) {
        return frame.id == id;
    });
    return found == frames.end() ? nullptr : &*found;
}

const CrewArchetypeDefinition* ContentCatalog::findCrewArchetype(std::string_view id) const
{
    const auto found = std::find_if(crewArchetypes.begin(), crewArchetypes.end(), [id](const CrewArchetypeDefinition& archetype) {
        return archetype.id == id;
    });
    return found == crewArchetypes.end() ? nullptr : &*found;
}

const Astronaut* ContentCatalog::findAstronaut(std::string_view id) const
{
    const auto found = std::find_if(astronauts.begin(), astronauts.end(), [id](const Astronaut& astronaut) {
        return astronaut.id == id;
    });
    return found == astronauts.end() ? nullptr : &*found;
}

const Destination* ContentCatalog::findDestination(std::string_view id) const
{
    const auto found = std::find_if(destinations.begin(), destinations.end(), [id](const Destination& destination) {
        return destination.id == id;
    });
    return found == destinations.end() ? nullptr : &*found;
}

const RouteLinkDefinition* ContentCatalog::findRouteLink(std::string_view id) const
{
    const auto found = std::find_if(routeLinks.begin(), routeLinks.end(), [id](const RouteLinkDefinition& link) {
        return link.id == id;
    });
    return found == routeLinks.end() ? nullptr : &*found;
}

const RouteLinkDefinition* ContentCatalog::findRouteLink(
    std::string_view sourceDestinationId,
    std::string_view targetDestinationId) const
{
    const auto found = std::find_if(routeLinks.begin(), routeLinks.end(), [&](const RouteLinkDefinition& link) {
        return link.sourceDestinationId == sourceDestinationId &&
            link.targetDestinationId == targetDestinationId;
    });
    return found == routeLinks.end() ? nullptr : &*found;
}

const TransferAssistDefinition* ContentCatalog::findTransferAssist(std::string_view id) const
{
    const auto found = std::find_if(
        transferAssists.begin(),
        transferAssists.end(),
        [&](const TransferAssistDefinition& definition) { return definition.id == id; });
    return found == transferAssists.end() ? nullptr : &*found;
}

const ScenarioDefinition* ContentCatalog::findScenario(std::string_view id) const
{
    const auto found = std::find_if(scenarios.begin(), scenarios.end(), [id](const ScenarioDefinition& scenario) {
        return scenario.id == id;
    });
    return found == scenarios.end() ? nullptr : &*found;
}

const ScenarioFactoryDefinition* ContentCatalog::findScenarioFactory(std::string_view id) const
{
    const auto found = std::find_if(scenarioFactories.begin(), scenarioFactories.end(), [id](const ScenarioFactoryDefinition& factory) {
        return factory.id == id;
    });
    return found == scenarioFactories.end() ? nullptr : &*found;
}

const MiningSiteDefinition* ContentCatalog::findMiningSite(std::string_view id) const
{
    const auto found = std::find_if(miningSites.begin(), miningSites.end(), [id](const MiningSiteDefinition& site) {
        return site.id == id;
    });
    return found == miningSites.end() ? nullptr : &*found;
}

ContentCatalog createDefaultContent()
{
    ContentCatalog catalog;

    catalog.modules = {
        module(content::module::fuelTanks1, "Fuel Tanks I", SlotType::Fuel, Rarity::Common, {}, content::unlock::starter, {"launch", "fuel"}, {}, RefitTrack::Reach, 1, "", true, LaunchUpgradeKind::FuelTanks, 1),
        module(content::module::fuelTanks2, "Fuel Tanks II", SlotType::Fuel, Rarity::Common, {}, content::unlock::starter, {"launch", "fuel"}, {}, RefitTrack::Reach, 2, content::module::fuelTanks1, true, LaunchUpgradeKind::FuelTanks, 2),
        module(content::module::fuelTanks3, "Fuel Tanks III", SlotType::Fuel, Rarity::Prototype, {}, content::unlock::starter, {"launch", "fuel"}, {}, RefitTrack::Reach, 3, content::module::fuelTanks2, true, LaunchUpgradeKind::FuelTanks, 3),
        module(content::module::flightControls1, "Flight Controls I", SlotType::Sensors, Rarity::Common, {}, content::unlock::starter, {"launch", "controls"}, {}, RefitTrack::Control, 1, "", true, LaunchUpgradeKind::FlightControls, 1),
        module(content::module::flightControls2, "Flight Controls II", SlotType::Sensors, Rarity::Common, {}, content::unlock::starter, {"launch", "controls"}, {}, RefitTrack::Control, 2, content::module::flightControls1, true, LaunchUpgradeKind::FlightControls, 2),
        module(content::module::flightControls3, "Flight Controls III", SlotType::Sensors, Rarity::Common, {}, content::unlock::starter, {"launch", "controls"}, {}, RefitTrack::Control, 3, content::module::flightControls2, true, LaunchUpgradeKind::FlightControls, 3),
        module(content::module::coolingSystem1, "Engine Cooling I", SlotType::Cooling, Rarity::Common, {}, content::unlock::starter, {"launch", "temperature"}, {}, RefitTrack::Control, 1, "", true, LaunchUpgradeKind::Cooling, 1),
        module(content::module::coolingSystem2, "Engine Cooling II", SlotType::Cooling, Rarity::Common, {}, content::unlock::starter, {"launch", "temperature"}, {}, RefitTrack::Control, 2, content::module::coolingSystem1, true, LaunchUpgradeKind::Cooling, 2),
        module(content::module::coolingSystem3, "Engine Cooling III", SlotType::Cooling, Rarity::Common, {}, content::unlock::starter, {"launch", "temperature"}, {}, RefitTrack::Control, 3, content::module::coolingSystem2, true, LaunchUpgradeKind::Cooling, 3),
        module(content::module::hullPlating1, "Hull Plating I", SlotType::Hull, Rarity::Common, {}, content::unlock::starter, {"launch", "hull"}, {}, RefitTrack::Recovery, 1, "", true, LaunchUpgradeKind::Hull, 1),
        module(content::module::hullPlating2, "Hull Plating II", SlotType::Hull, Rarity::Common, {}, content::unlock::starter, {"launch", "hull"}, {}, RefitTrack::Recovery, 2, content::module::hullPlating1, true, LaunchUpgradeKind::Hull, 2),
        module(content::module::hullPlating3, "Hull Plating III", SlotType::Hull, Rarity::Common, {}, content::unlock::starter, {"launch", "hull"}, {}, RefitTrack::Recovery, 3, content::module::hullPlating2, true, LaunchUpgradeKind::Hull, 3),
        module(content::module::surveyArray1, "Survey Array I", SlotType::Sensors, Rarity::Common, {}, content::unlock::surfaceProbes, {"surface", "survey", "permanent"}, {}, RefitTrack::Control, 1, "", false, LaunchUpgradeKind::None, 0, SurfaceDepthUpgradeKind::SurveyArray, 1),
        module(content::module::surveyArray2, "Survey Array II", SlotType::Sensors, Rarity::Uncommon, {}, content::unlock::surfaceProbes, {"surface", "survey", "permanent"}, {}, RefitTrack::Control, 2, content::module::surveyArray1, false, LaunchUpgradeKind::None, 0, SurfaceDepthUpgradeKind::SurveyArray, 2),
        module(content::module::surveyArray3, "Survey Array III", SlotType::Sensors, Rarity::Rare, {}, content::unlock::surfaceProbes, {"surface", "survey", "permanent"}, {}, RefitTrack::Control, 3, content::module::surveyArray2, false, LaunchUpgradeKind::None, 0, SurfaceDepthUpgradeKind::SurveyArray, 3),
        module(content::module::boreSystem1, "Bore System I", SlotType::Engine, Rarity::Common, {}, content::unlock::surfaceDrills, {"surface", "dig", "permanent"}, {}, RefitTrack::Reach, 1, "", false, LaunchUpgradeKind::None, 0, SurfaceDepthUpgradeKind::BoreSystem, 1),
        module(content::module::boreSystem2, "Bore System II", SlotType::Engine, Rarity::Uncommon, {}, content::unlock::surfaceDrills, {"surface", "dig", "permanent"}, {}, RefitTrack::Reach, 2, content::module::boreSystem1, false, LaunchUpgradeKind::None, 0, SurfaceDepthUpgradeKind::BoreSystem, 2),
        module(content::module::boreSystem3, "Bore System III", SlotType::Engine, Rarity::Rare, {}, content::unlock::surfaceDrills, {"surface", "dig", "permanent"}, {}, RefitTrack::Reach, 3, content::module::boreSystem2, false, LaunchUpgradeKind::None, 0, SurfaceDepthUpgradeKind::BoreSystem, 3),
        module(content::module::rigFuelLoop1, "Rig Fuel Loop I", SlotType::Fuel, Rarity::Common, {}, content::unlock::surfaceDrills, {"surface", "mining", "fuel", "permanent"}, {}, RefitTrack::Recovery, 1, "", false, LaunchUpgradeKind::None, 0, SurfaceDepthUpgradeKind::None, 0, 1),
        module(content::module::rigFuelLoop2, "Rig Fuel Loop II", SlotType::Fuel, Rarity::Uncommon, {}, content::unlock::surfaceDrills, {"surface", "mining", "fuel", "permanent"}, {}, RefitTrack::Recovery, 2, content::module::rigFuelLoop1, false, LaunchUpgradeKind::None, 0, SurfaceDepthUpgradeKind::None, 0, 2),
        module(content::module::rigFuelLoop3, "Rig Fuel Loop III", SlotType::Fuel, Rarity::Rare, {}, content::unlock::surfaceDrills, {"surface", "mining", "fuel", "permanent"}, {}, RefitTrack::Recovery, 3, content::module::rigFuelLoop2, false, LaunchUpgradeKind::None, 0, SurfaceDepthUpgradeKind::None, 0, 3),

        module(content::module::sparrowEngine, "Sparrow Engine", SlotType::Engine, Rarity::Common, {.thrust = 2.0, .volatility = 0.2}, content::unlock::starter, {"steady", content::unlock::starter}),
        module(content::module::kestrelEngine, "Kestrel Engine", SlotType::Engine, Rarity::Uncommon, {.thrust = 1.6, .fuel = -0.4, .volatility = 0.25, .payout = 0.4}, content::unlock::deepSpace, {"fast", "hungry"}, {}, RefitTrack::Reach, 1, content::module::sparrowEngine),
        module(content::module::novaDrive, "Nova Drive", SlotType::Engine, Rarity::Rare, {.thrust = 1.6, .fuel = 0.4, .cooling = -0.8, .volatility = 0.65, .payout = 0.7}, content::unlock::exotic, {"prototype", "dangerous"}, {.rare = 2, .exotic = 1}, RefitTrack::Reach, 2, content::module::kestrelEngine),

        module(content::module::stableTank, "Stable Tank", SlotType::Fuel, Rarity::Common, {.fuel = 2.5, .hull = 0.2, .pressure = 0.4}, content::unlock::starter, {"safe", "pressure"}),
        module(content::module::slushTank, "Slush Tank", SlotType::Fuel, Rarity::Uncommon, {.fuel = 1.5, .hull = -0.2, .cooling = 0.4, .pressure = 0.4, .volatility = 0.35}, content::unlock::thermal, {"cold", "pressure"}, {}, RefitTrack::Reach, 1, content::module::stableTank),
        module(content::module::deepReservoir, "Deep Reservoir", SlotType::Fuel, Rarity::Rare, {.thrust = 0.5, .fuel = 1.4, .cooling = -0.4, .pressure = -0.8, .volatility = 0.4}, content::unlock::deepSpace, {"long-haul"}, {.common = 2, .rare = 1}, RefitTrack::Reach, 2, content::module::slushTank),

        module(content::module::patchworkHull, "Patchwork Hull", SlotType::Hull, Rarity::Common, {.hull = 2.6}, content::unlock::starter, {"cheap"}),
        module(content::module::titaniumRib, "Titanium Rib", SlotType::Hull, Rarity::Uncommon, {.hull = 1.6, .cooling = -0.2}, content::unlock::recovery, {"durable"}, {}, RefitTrack::Recovery, 1, content::module::patchworkHull),
        module(content::module::ablativeSkin, "Ablative Skin", SlotType::Hull, Rarity::Rare, {.hull = 0.8, .cooling = 1.2, .escape = 0.4}, content::unlock::thermal, {"heat-shield"}, {}, RefitTrack::Recovery, 1, content::module::patchworkHull),

        module(content::module::radiatorVanes, "Radiator Vanes", SlotType::Cooling, Rarity::Common, {.hull = -0.2, .cooling = 2.5}, content::unlock::starter, {"cooling"}),
        module(content::module::cryoLoop, "Cryo Loop", SlotType::Cooling, Rarity::Uncommon, {.fuel = -0.4, .cooling = 1.9}, content::unlock::thermal, {"precision"}, {}, RefitTrack::Control, 1, content::module::radiatorVanes),
        module(content::module::sacrificialSink, "Sacrificial Heat Sink", SlotType::Cooling, Rarity::Rare, {.fuel = 0.4, .hull = -0.8, .cooling = 1.6}, content::unlock::recovery, {"one-more-burn"}, {}, RefitTrack::Control, 2, content::module::cryoLoop),

        module(content::module::analogTelemetry, "Analog Telemetry", SlotType::Sensors, Rarity::Common, {.sensors = 2.0, .pressure = 0.3}, content::unlock::starter, {"honest", "pressure"}),
        module(content::module::hazardRadar, "Hazard Radar", SlotType::Sensors, Rarity::Uncommon, {.sensors = 1.8, .escape = 0.2, .pressure = 0.4}, content::unlock::deepSpace, {"warning", "pressure"}, {}, RefitTrack::Control, 1, content::module::analogTelemetry),
        module(content::module::predictiveGuidance, "Predictive Guidance", SlotType::Sensors, Rarity::Prototype, {.thrust = 0.6, .sensors = 1.4, .escape = -0.2, .pressure = 0.3, .volatility = 0.25}, content::unlock::ai, {"forecast", "pressure"}, {.rare = 2}, RefitTrack::Control, 2, content::module::hazardRadar),

        module(content::module::springCapsule, "Spring Capsule", SlotType::Escape, Rarity::Common, {.thrust = -0.2, .escape = 2.8}, content::unlock::starter, {"eject"}),
        module(content::module::abortTower, "Abort Tower", SlotType::Escape, Rarity::Uncommon, {.thrust = 0.2, .hull = 0.5, .escape = 1.8, .payout = -0.2}, content::unlock::recovery, {"crew-first"}, {}, RefitTrack::Recovery, 1, content::module::springCapsule),
        module(content::module::phoenixPod, "Phoenix Pod", SlotType::Escape, Rarity::Rare, {.hull = -0.5, .escape = 1.6, .volatility = -0.3, .payout = 0.2}, content::unlock::exotic, {"legendary"}, {.rare = 1, .exotic = 1}, RefitTrack::Recovery, 2, content::module::abortTower),

        module(content::module::surfaceMapper, "Surface Mapper", SlotType::Sensors, Rarity::Common, {.sensors = 0.4, .miningWidth = 1.0}, content::unlock::surfaceProbes, {"surface", "mining", "survey"}, {}, RefitTrack::Control),
        module(content::module::regolithAuger, "Regolith Auger", SlotType::Engine, Rarity::Common, {.volatility = 0.10, .miningPower = 1.0}, content::unlock::surfaceDrills, {"surface", "mining", "drill"}, {}, RefitTrack::Reach),
        module(content::module::oreSorter, "Ore Sorter", SlotType::Fuel, Rarity::Uncommon, {.fuel = -0.2, .miningYield = 1.0}, content::unlock::surfaceDrills, {"surface", "mining", "yield"}, {.common = 1}, RefitTrack::Recovery),
        module(content::module::coolantSleeve, "Coolant Sleeve", SlotType::Cooling, Rarity::Uncommon, {.cooling = 0.4, .miningCooling = 1.1}, content::unlock::surfaceDrills, {"surface", "mining", "cooling"}, {.common = 1}, RefitTrack::Control),
        module(content::module::diamondBearings, "Diamond Bearings", SlotType::Hull, Rarity::Rare, {.hull = 0.2, .miningDurability = 1.2}, content::unlock::surfaceDrills, {"surface", "mining", "durable"}, {.common = 1, .rare = 1}, RefitTrack::Control),
        module(content::module::deepBoreFrame, "Deep-Bore Frame", SlotType::Fuel, Rarity::Rare, {.fuel = -0.4, .miningPower = 0.4, .miningDepth = 1.0}, content::unlock::cargoRigs, {"surface", "mining", "deep"}, {.common = 2, .rare = 1}, RefitTrack::Reach),
        module(content::module::cargoSpine, "Cargo Spine", SlotType::Hull, Rarity::Common, {.hull = 0.4, .miningStorage = 3.0}, content::unlock::cargoRigs, {"surface", "mining", "cargo"}, {.common = 2}, RefitTrack::Recovery),
        module(content::module::haulerThrusters, "Hauler Thrusters", SlotType::Engine, Rarity::Uncommon, {.thrust = 0.6, .fuel = -0.2, .volatility = 0.20, .miningEngineEfficiency = 0.24}, content::unlock::cargoRigs, {"surface", "mining", "hauler"}, {.common = 2, .rare = 1}, RefitTrack::Reach),
        module(content::module::massDriverWinch, "Mass Driver Winch", SlotType::Escape, Rarity::Rare, {.escape = 0.6, .volatility = 0.25, .miningStorage = 2.0, .miningEngineEfficiency = 0.34}, content::unlock::cargoRigs, {"surface", "mining", "artifact"}, {.rare = 2, .exotic = 1}, RefitTrack::Recovery)
    };

    // The pre-v10 ship packages remain resolvable for existing saves. New
    // offers consist only of the four direct Launch tracks and purpose-built
    // Surface/Mining modules.
    for (ShipModule& moduleDefinition : catalog.modules) {
        const bool surfaceModule = std::find(
            moduleDefinition.tags.begin(),
            moduleDefinition.tags.end(),
            "surface") != moduleDefinition.tags.end();
        moduleDefinition.compatibilityOnly =
            moduleDefinition.launchUpgradeKind == LaunchUpgradeKind::None &&
            !surfaceModule;
    }

    // Crew identity now comes from authored archetypes. Training, rest,
    // stress, and their facility economy are intentionally absent from the current schema.
    catalog.crewUpgrades.clear();

    catalog.surfaceUpgrades = {
        surfaceUpgrade(content::surfaceUpgrade::resonantDischarge, "Resonant Discharge", "A combat-tuned scanner pulse shocks enemies caught in the player-centered ring.", Rarity::Rare, SurfaceUpgradeCategory::Scanner, {.scannerPulseDamage = 1}, {"scanner", "combat", "pulse"}),
        surfaceUpgrade(content::surfaceUpgrade::thermalDrillJackets, "Thermal Drill Jackets", "Insulated drill collars bleed heat before the bit redlines and steady deeper pushes.", Rarity::Common, SurfaceUpgradeCategory::Drill, {.drillCooling = 2.4, .drillDurability = 0.4}, {"drill", "cooling", "depth"}),
        surfaceUpgrade(content::surfaceUpgrade::widebandPulse, "Wideband Pulse", "A wider scanner ping maps shadowed ore seams, bad pockets, and one deeper layer.", Rarity::Common, SurfaceUpgradeCategory::Scanner, {.scannerRadius = 2.5, .hazardRelief = 0.02}, {"scanner", "reveal", "depth"}),
        surfaceUpgrade(content::surfaceUpgrade::cargoSkids, "Cargo Skids", "Low-friction skids help the Mining Rig haul heavier canisters without load drag getting ugly.", Rarity::Common, SurfaceUpgradeCategory::Drone, {.droneStorage = 2.0, .droneEngineEfficiency = 0.08}, {"drone", "cargo"}),
        surfaceUpgrade(content::surfaceUpgrade::shockMounts, "Shock Mounts", "Spring-loaded mounts protect the drill train through hard-rock chatter and contact jolts.", Rarity::Uncommon, SurfaceUpgradeCategory::Drill, {.drillDurability = 2.2, .hardRockBounceRelief = 0.18, .hazardRelief = 0.015}, {"drill", "durability", "recoil"}),
        surfaceUpgrade(content::surfaceUpgrade::oreScentArray, "Ore-Scent Array", "Spectral sniffers help the crew sort richer pockets from plain dust before the dig.", Rarity::Rare, SurfaceUpgradeCategory::Scanner, {.oreYieldChance = 0.14, .scannerRadius = 1.2, .hazardRelief = 0.01}, {"scanner", "yield", "survey"}),
        surfaceUpgrade(content::surfaceUpgrade::coolantMist, "Coolant Mist", "A hiss of cold vapor keeps the drill biting without cooking the head.", Rarity::Common, SurfaceUpgradeCategory::Drill, {.drillCooling = 1.6, .drillDurability = 0.6}, {"drill", "cooling"}),
        surfaceUpgrade(content::surfaceUpgrade::recoilBraces, "Recoil Braces", "Kickback struts turn hard-rock bonks into controlled shoves while the drone keeps moving.", Rarity::Uncommon, SurfaceUpgradeCategory::Drone, {.drillDurability = 0.5, .hardRockBounceRelief = 0.24, .droneSpeed = 0.25}, {"drone", "recoil", "control"}),
        surfaceUpgrade(content::surfaceUpgrade::oreHopper, "Ore Hopper", "A squat canister rack gives loose ore a cleaner ride back to the ship zone.", Rarity::Common, SurfaceUpgradeCategory::Drone, {.oreYieldChance = 0.07, .droneStorage = 1.0}, {"drone", "yield"}),
        // Keep the established ID for save compatibility. This is an artifact
        // handling upgrade, not the retired EVA-to-rig return-tether concept.
        surfaceUpgrade(content::surfaceUpgrade::emergencyWinch, "Artifact Winch", "A powered recovery spool reduces artifact towing burden by 25% per rank.", Rarity::Uncommon, SurfaceUpgradeCategory::Drone, {.artifactTowEfficiency = 0.25}, {"artifact", "tether", "transport"}),
        surfaceUpgrade(content::surfaceUpgrade::deepEchoMapper, "Deep Echo Mapper", "Low-frequency pings read deeper silhouettes and artifact pockets before the flare fades.", Rarity::Rare, SurfaceUpgradeCategory::Scanner, {.oreYieldChance = 0.04, .scannerRadius = 3.0, .hazardRelief = 0.015}, {"scanner", "depth", "artifact"}),
        surfaceUpgrade(content::surfaceUpgrade::expandablePanniers, "Expandable Panniers", "Fold-out panniers widen the free carry buffer before ore starts slowing the drone.", Rarity::Common, SurfaceUpgradeCategory::Drone, {.droneStorage = 3.0}, {"drone", "cargo", "storage"}),
        surfaceUpgrade(content::surfaceUpgrade::vectorNozzles, "Vector Nozzles", "Trim jets keep loaded turns crisp and burn less fuel under a heavy haul.", Rarity::Uncommon, SurfaceUpgradeCategory::Drone, {.droneSpeed = 0.15, .droneEngineEfficiency = 0.25}, {"drone", "engine", "load"}),
        surfaceUpgrade(content::surfaceUpgrade::artifactTowline, "Artifact Towline", "Braided towline spreads artifact drag so tethered relics pull cleaner toward the ship.", Rarity::Rare, SurfaceUpgradeCategory::Drone, {.artifactTowEfficiency = 0.40}, {"drone", "artifact", "tether"})
    };

    catalog.miniDrones = {
        miniDrone(content::drone::miningDrone, "Prospector Support Drone", "Peels revealed ore pockets while the Mining Rig keeps tunneling under pressure.", Rarity::Common, MiniDroneRole::Mining, {.passiveMiningRate = 0.12}, content::unlock::droneBay, {"excavation", "resource"}),
        miniDrone(content::drone::resourceDrone, "Resource Drone", "Carries backup oxygen and return consumables so the rig can stay longer before the swarm wins.", Rarity::Common, MiniDroneRole::Resource, {.oxygenSeconds = 28.0}, content::unlock::droneSupportSuite, {"logistics", "endurance"}),
        miniDrone(content::drone::surveyDrone, "Survey Drone", "Widens scanner pulses and outlines ore, artifacts, and hostile silhouettes through fog.", Rarity::Uncommon, MiniDroneRole::Survey, {.scannerRadius = 2.0}, content::unlock::droneSupportSuite, {"exploration", "navigation"}),
        miniDrone(content::drone::hazardDrone, "Hazard Drone", "Treats revealed thermal, cryo, toxic, and radiation pockets before the rig gets too close.", Rarity::Uncommon, MiniDroneRole::Hazard, {}, content::unlock::ioHazardDrone, {"engineering", "remediation"}),
        miniDrone(content::drone::attackDrone, "Attack Drone", "Auto-fires cyan shots, crits priority targets, and pulses a slowing field while you mine.", Rarity::Rare, MiniDroneRole::Attack, {.enemyEncounterRelief = 0.05, .sentryDamagePerSecond = 3.2, .areaControlDamagePerSecond = 0.85, .enemySlow = 0.12}, content::unlock::perimeterDrones, {"combat", "post-solar"}),
        miniDrone(content::drone::defenseDrone, "Defense Drone", "Holds a rotating charged shield arc, recharges after a break, and counter-hits enemies that reach the rig.", Rarity::Rare, MiniDroneRole::Defense, {.drillIntegrityRelief = 0.06, .enemyEncounterRelief = 0.08, .enemyDamageRelief = 0.32, .reactiveArmorDamagePerSecond = 1.6, .environmentalShieldRelief = 0.18}, content::unlock::perimeterDrones, {"defense", "post-solar"})
    };

    catalog.droneModules = {
        {content::droneModule::combatDrill, "Combat Drill", MiniDroneRole::Mining, MiniDroneRole::Attack, DroneModuleKind::CombatDrill, content::unlock::perimeterDrones, Rarity::Rare},
        {content::droneModule::drillGuard, "Drill Guard", MiniDroneRole::Mining, MiniDroneRole::Defense, DroneModuleKind::DrillGuard, content::unlock::perimeterDrones, Rarity::Rare},
        {content::droneModule::pulseStrike, "Pulse Strike", MiniDroneRole::Survey, MiniDroneRole::Attack, DroneModuleKind::PulseStrike, content::unlock::perimeterDrones, Rarity::Rare},
        {content::droneModule::spectrumFilter, "Spectrum Filter", MiniDroneRole::Survey, MiniDroneRole::Hazard, DroneModuleKind::SpectrumFilter, content::unlock::ioHazardDrone, Rarity::Uncommon},
        {content::droneModule::oreRelay, "Ore Relay", MiniDroneRole::Resource, MiniDroneRole::Mining, DroneModuleKind::OreRelay, content::unlock::droneSupportSuite, Rarity::Uncommon},
        {content::droneModule::treasurePing, "Treasure Ping", MiniDroneRole::Resource, MiniDroneRole::Survey, DroneModuleKind::TreasurePing, content::unlock::droneSupportSuite, Rarity::Uncommon},
        {content::droneModule::containmentShell, "Containment Shell", MiniDroneRole::Hazard, MiniDroneRole::Defense, DroneModuleKind::ContainmentShell, content::unlock::ioHazardDrone, Rarity::Uncommon},
        {content::droneModule::reclamationLoop, "Reclamation Loop", MiniDroneRole::Hazard, MiniDroneRole::Resource, DroneModuleKind::ReclamationLoop, content::unlock::ioHazardDrone, Rarity::Uncommon},
        {content::droneModule::targetedAssault, "Targeted Assault", MiniDroneRole::Attack, MiniDroneRole::Survey, DroneModuleKind::TargetedAssault, content::unlock::perimeterDrones, Rarity::Rare},
        {content::droneModule::penetratingImpact, "Penetrating Impact", MiniDroneRole::Attack, MiniDroneRole::Mining, DroneModuleKind::PenetratingImpact, content::unlock::perimeterDrones, Rarity::Rare},
        {content::droneModule::retributionArc, "Retribution Arc", MiniDroneRole::Defense, MiniDroneRole::Attack, DroneModuleKind::RetributionArc, content::unlock::perimeterDrones, Rarity::Rare},
        {content::droneModule::hazardScreen, "Hazard Screen", MiniDroneRole::Defense, MiniDroneRole::Hazard, DroneModuleKind::HazardScreen, content::unlock::perimeterDrones, Rarity::Rare}
    };

    // These are selectable run upgrades. Merely equipping the required roles
    // never grants their effects; miniDroneLoadoutEffects also verifies the
    // selected ID and the current role composition before applying them.
    catalog.droneSynergies = {
        {"targeting_grid", "Targeting Grid", "Attack and Survey drones share target paint for faster, more accurate volleys.", Rarity::Rare,
            {MiniDroneRole::Attack, MiniDroneRole::Survey}, {.scannerRadius = 0.75, .alliedCritChanceBonus = 0.12, .alliedFireRateBonus = 0.15}, MiniDroneSignatureKind::None, 0, content::unlock::perimeterCoordination},
        {"killbox_screen", "Killbox Screen", "Attack and Defense drones coordinate covering fire, shields, and counter-hits.", Rarity::Rare,
            {MiniDroneRole::Attack, MiniDroneRole::Defense}, {.enemyDamageRelief = 0.08, .reactiveArmorDamagePerSecond = 0.45, .sentryVolleyBonus = 1}, MiniDroneSignatureKind::None, 0, content::unlock::perimeterCoordination},
        {"excavation_barrage", "Excavation Barrage", "Attack and Mining drones turn ore tempo into area-control pressure.", Rarity::Rare,
            {MiniDroneRole::Attack, MiniDroneRole::Mining}, {.passiveMiningRate = 0.04, .areaControlDamagePerSecond = 0.40, .enemySlow = 0.04}, MiniDroneSignatureKind::None, 0, content::unlock::perimeterCoordination},
        {"containment_screen", "Containment Screen", "Defense and Hazard drones pair remediation with environmental shielding.", Rarity::Rare,
            {MiniDroneRole::Defense, MiniDroneRole::Hazard}, {.environmentalShieldRelief = 0.06, .hazardTreatmentRateBonus = 0.15}, MiniDroneSignatureKind::None, 0, content::unlock::perimeterCoordination},
        {"long_haul_rig", "Long Haul Rig", "Mining and Resource drones keep ore and oxygen flowing on deeper routes.", Rarity::Rare,
            {MiniDroneRole::Mining, MiniDroneRole::Resource}, {.passiveMiningRate = 0.035, .oxygenSeconds = 12.0}, MiniDroneSignatureKind::None, 0, content::unlock::starter},
        {"pathfinder_loop", "Pathfinder Loop", "Resource and Survey drones widen the route picture for artifact recovery.", Rarity::Rare,
            {MiniDroneRole::Resource, MiniDroneRole::Survey}, {.scannerRadius = 0.85}, MiniDroneSignatureKind::None, 0, content::unlock::starter},
        {"sentry_killbox", "Sentry Killbox", "Attack, Defense, and Survey drones form a marked killbox with faster volleys, better crits, and tougher shields.", Rarity::Prototype,
            {MiniDroneRole::Attack, MiniDroneRole::Defense, MiniDroneRole::Survey}, {.enemyDamageRelief = 0.04, .alliedCritChanceBonus = 0.06, .alliedFireRateBonus = 0.20, .sentryVolleyBonus = 1}, MiniDroneSignatureKind::SentryKillbox, 2, content::unlock::perimeterCoordination},
        {"excavation_storm", "Excavation Storm", "Mining, Resource, and Attack drones keep ore flowing while combat pulses punish the work zone.", Rarity::Prototype,
            {MiniDroneRole::Attack, MiniDroneRole::Mining, MiniDroneRole::Resource}, {.passiveMiningRate = 0.045, .areaControlDamagePerSecond = 0.55, .enemySlow = 0.03, .alliedFireRateBonus = 0.10}, MiniDroneSignatureKind::ExcavationStorm, 2, content::unlock::perimeterCoordination},
        {"containment_rig", "Containment Rig", "Defense, Hazard, and Resource drones sustain long digs with treatment, shielding, reserve time, and counter-hits.", Rarity::Prototype,
            {MiniDroneRole::Defense, MiniDroneRole::Hazard, MiniDroneRole::Resource}, {.oxygenSeconds = 10.0, .reactiveArmorDamagePerSecond = 0.35, .environmentalShieldRelief = 0.05, .hazardTreatmentRateBonus = 0.10}, MiniDroneSignatureKind::ContainmentRig, 2, content::unlock::perimeterCoordination},
        {"relic_pathfinder", "Relic Pathfinder", "Mining, Resource, and Survey drones favor artifact routes with wider scans and steady excavation.", Rarity::Prototype,
            {MiniDroneRole::Mining, MiniDroneRole::Resource, MiniDroneRole::Survey}, {.passiveMiningRate = 0.020, .scannerRadius = 0.70}, MiniDroneSignatureKind::RelicPathfinder, 2, content::unlock::starter},
        {"full_spectrum_swarm", "Full Spectrum Swarm", "Every drone role contributes combat, logistics, scans, remediation, and mining support.", Rarity::Prototype,
            {MiniDroneRole::Attack, MiniDroneRole::Defense, MiniDroneRole::Survey, MiniDroneRole::Mining, MiniDroneRole::Resource, MiniDroneRole::Hazard},
            {.passiveMiningRate = 0.025, .oxygenSeconds = 8.0, .scannerRadius = 0.50, .enemyDamageRelief = 0.04, .alliedCritChanceBonus = 0.05, .alliedFireRateBonus = 0.10, .sentryVolleyBonus = 1},
            MiniDroneSignatureKind::FullSpectrumSwarm, 3, content::unlock::perimeterCoordination}
    };

    catalog.researchProjects = {
        researchProject(content::research::blueprintSurvey, "Research Data Survey", "Map Mars strata for recoverable ship schematics and Research Data.", Rarity::Common, 2, 2, {}, content::unlock::starter, "", {"blueprint", "survey"}),
        researchProject(content::research::fieldProbeNetwork, "Field Scanner Network", "Extend the Rig scanner's reach and make buried returns easier to read.", Rarity::Common, 2, 2, {.common = 1}, content::unlock::starter, content::unlock::surfaceProbes, {"surface", "scanner"}),
        researchProject(content::research::appliedMaterialsLab, "Applied Materials Lab", "Convert field samples into sturdier research procedures.", Rarity::Uncommon, 2, 3, {.common = 2}, content::unlock::starter, content::unlock::recovery, {"materials", "facility"}),
        researchProject(content::research::missionAnalysisLab, "Mission Analysis Lab", "Build a debrief room that turns samples and flight notes into cleaner Research Data.", Rarity::Uncommon, 2, 3, {.common = 2, .rare = 1}, content::unlock::starter, content::unlock::analysisLab, {"blueprint", "facility"}),
        researchProject(content::research::regolithDrillRig, "Regolith Drill Rig", "Build compact drills that pull more useful ore from short surface sorties.", Rarity::Uncommon, 2, 3, {.common = 2, .rare = 1}, content::unlock::surfaceProbes, content::unlock::surfaceDrills, {"surface", "mining"}),
        researchProject(content::research::droneBayProgram, "Drone Support Program", "Expand the Prospector cradle for support drones that handle scouting and logistics.", Rarity::Uncommon, 2, 3, {.common = 2, .rare = 1}, content::unlock::surfaceDrills, content::unlock::droneSupportSuite, {"surface", "drone", "logistics"}),
        researchProject(content::research::cargoReturnRig, "Cargo Return Rig", "Prototype restraint frames that make heavier payloads less terrifying to extract.", Rarity::Uncommon, 2, 3, {.common = 3}, content::unlock::recovery, content::unlock::cargoRigs, {"surface", "extraction"}),
        researchProject(content::research::prototypeSchematic, "Prototype Schematic", "Use rare samples to unlock experimental ship components.", Rarity::Rare, 2, 4, {.common = 1, .rare = 1}, content::unlock::starter, content::unlock::thermal, {"prototype", "ship"}),
        researchProject(content::research::xenogeologyProgram, "Xenogeology Program", "Study outer-system deposits for deep-space unlocks.", Rarity::Rare, 3, 5, {.rare = 2}, content::unlock::deepSpace, content::unlock::ai, {"materials", "deep_space"}),
        researchProject(content::research::arkScaffoldProgram, "Ark Keel Program", "Lay the first orbital keel sections for a future deep-space home base.", Rarity::Prototype, 3, 6, {.common = 4, .rare = 2}, content::unlock::deepSpace, content::unlock::arkScaffold, {"ark", "home_base", "deep_space"}),
        researchProject(content::research::perimeterDroneNetwork, "Perimeter Drone Network", "Coordinate Arkfall sentries for advanced combat tuning and named multi-drone formations.", Rarity::Rare, 4, 5, {.rare = 2, .exotic = 1}, content::unlock::perimeterDrones, content::unlock::perimeterCoordination, {"surface", "defense", "coordination"}),
        researchProject(content::research::artifactDecoding, "Artifact Decoding", "Decode recovered signals into exotic research threads.", Rarity::Prototype, 4, 7, {.rare = 2, .exotic = 1}, content::unlock::ai, content::unlock::exotic, {"artifact", "story"})
    };

    catalog.frames = {
        {content::frame::pathfinder, "Pathfinder Frame", {SlotType::Engine, SlotType::Fuel, SlotType::Hull, SlotType::Cooling, SlotType::Sensors, SlotType::Escape}, {.thrust = 1.0, .fuel = 1.0, .hull = 1.0, .cooling = 1.0, .sensors = 1.0, .escape = 1.0}, 100},
        {content::frame::sprinter, "Sprinter Frame", {SlotType::Engine, SlotType::Engine, SlotType::Fuel, SlotType::Cooling, SlotType::Sensors, SlotType::Escape}, {.thrust = 1.8, .fuel = 0.5, .hull = 0.4, .cooling = 0.8, .sensors = 0.8, .escape = 0.7, .volatility = 0.4}, 92},
        {content::frame::ark, "Ark Frame", {SlotType::Engine, SlotType::Fuel, SlotType::Hull, SlotType::Hull, SlotType::Cooling, SlotType::Escape}, {.thrust = 0.5, .fuel = 1.1, .hull = 2.0, .cooling = 0.8, .sensors = 0.5, .escape = 1.3}, 118}
    };

    catalog.crewArchetypes = {
        {"capybara_endurance", "Capybara", "Endurance Specialist", "Deep Breath", "Adds 20 seconds to rig and suit oxygen.", {.rigOxygenSeconds = 20.0, .suitOxygenSeconds = 20.0}},
        {"beaver_engineer", "Beaver", "Rig Engineer", "Field Joinery", "Improves rig integrity and material repairs.", {.rigIntegrity = 0.15, .repairEfficiency = 0.25}},
        {"fox_navigator", "Fox", "Navigator", "Exit Vector", "Improves flight control and emergency recovery.", {.navigationControl = 0.10, .emergencyRecovery = 0.20}},
        {"prairie_dog_excavator", "Prairie Dog", "Excavation Scout", "Ground Sense", "Extends scans and improves physical excavation.", {.scannerRadius = 2.0, .excavationEfficiency = 0.15}},
        {"squirrel_prospector", "Squirrel", "Resource Prospector", "Cache Sense", "Improves the chance of discovering useful resources.", {.resourceDiscovery = 0.12}},
        {"chipmunk_eva", "Chipmunk", "EVA Specialist", "Burrow Jet", "Improves suit traversal and EVA thrust.", {.evaThrust = 0.15}}
    };

    catalog.astronauts = {
        {content::astronaut::ava, "Mara Capybara", "Patient rescue veteran who treats every return as a promise.", std::string(tuning::traits::beastMode), "capybara_endurance", CrewStatus::Active},
        {content::astronaut::marco, "Bram Beaver", "Systems engineer who can make a damaged rig hold together.", std::string(tuning::traits::hardReboot), "beaver_engineer", CrewStatus::Active},
        {content::astronaut::nia, "Vela Fox", "Instinctive navigator with a talent for finding a way home.", std::string(tuning::traits::outtaHere), "fox_navigator", CrewStatus::Active},
        {content::astronaut::eli, "Pip Prairie Dog", "Subsurface scout who reads a seam before the scanner does.", std::string(tuning::traits::deepFocus), "prairie_dog_excavator", CrewStatus::Active},
        {content::astronaut::jo, "Nix Squirrel", "Prospector with an uncanny memory for where resources hide.", std::string(tuning::traits::rummageSale), "squirrel_prospector", CrewStatus::Active},
        {content::astronaut::sana, "Kip Chipmunk", "Fearless EVA specialist built for tight shafts and long tethers.", std::string(tuning::traits::phaseShift), "chipmunk_eva", CrewStatus::Active}
    };

    catalog.destinations = {
        {content::destination::earthOrbit, "Earth Orbit", 0, 1.45, 1.05, 2.25, 12.0, 0.65, content::unlock::starter, 0.0, 1.0, 0.15},
        {content::destination::moon, "Moon", 1, 1.95, 1.10, 3.10, 20.0, 0.95, content::unlock::starter, 0.0, 1.0, 0.35},
        {content::destination::mars, "Mars", 2, 2.65, 1.15, 4.15, 34.0, 1.35, content::unlock::starter, 0.0, 1.0, 0.60},
        {content::destination::jupiter, "Jupiter", 3, 3.15, 1.18, 5.00, 44.0, 1.55, content::unlock::deepSpace, 0.0, 1.0, 1.15},
        {content::destination::saturn, "Saturn", 4, 3.45, 1.20, 5.40, 52.0, 1.70, content::unlock::deepSpace, 0.0, 1.0, 0.95},
        {content::destination::uranus, "Uranus", 5, 3.80, 1.22, 5.85, 62.0, 1.85, content::unlock::deepSpace, 0.0, 1.0, 0.80},
        {content::destination::neptune, "Neptune", 6, 4.20, 1.24, 6.35, 76.0, 2.05, content::unlock::deepSpace, 0.0, 1.0, 1.05},
        {content::destination::nearbyStar, "Khepri Prime", 7, 5.10, 1.25, 7.85, 88.0, 2.45, content::unlock::deepSpace, 0.0, 1.0, 1.20},
        {content::destination::nearbyGalaxy, "Rift Belt", 8, 7.00, 1.30, 10.50, 144.0, 3.15, content::unlock::exotic, 0.0, 1.0, 0.25}
    };

    for (Destination& destination : catalog.destinations) {
        destination.hiddenFromProgression = destination.id == content::destination::earthOrbit;
        // The outer-system navigation mode is authored as destination data;
        // route traversal never needs to recognize a specific destination.
        destination.requiresHostileSystem = destination.tier >= 7;
        // The Moon is the authored first-arrival teaching destination. The
        // arrival loop consumes this capability rather than this ID.
        destination.requiresArrivalSurveySequence =
            destination.id == content::destination::moon;
        if (destination.requiresArrivalSurveySequence) {
            destination.provingRouteReadyTitle = "LUNAR ROUTE CHARTED";
            destination.provingRouteReadyConsequence =
                "The route is complete. Mission Control has cleared the next launch for the Moon.";
        }
        if (destination.id == content::destination::moon) {
            destination.approachBriefTitle = "MOON APPROACH";
            destination.approachBriefDetail =
                "First visit: Capture Orbit, then use the mapped descent. Flyby is introduced later.";
        } else if (destination.id == content::destination::mars) {
            destination.approachBriefTitle = "MARS APPROACH";
            destination.approachBriefDetail =
                "Capture Orbit maps a wider descent corridor. Direct Descent uses a narrower, more turbulent corridor.";
        } else if (destination.id == content::destination::jupiter) {
            destination.approachBriefTitle = "IO APPROACH";
            destination.approachBriefDetail =
                "Skipping Io leaves the active capture objective incomplete and the authored Saturn route blocked.";
            destination.calibratedTransferMarginRequired = 5.0;
            destination.transferMarginBlockerText =
                "Create 5 fuel of Jupiter transfer margin with Fuel Tanks III, a Good-or-better Mars slingshot, or both.";
        } else if (destination.id == content::destination::uranus) {
            destination.approachBriefTitle = "LAST CHARTED DEPARTURE";
            destination.approachBriefDetail =
                "Neptune is the last charted world. Recover the Uranus artifact, then complete a stable Orbit to solve its vector.";
        }
        // The current catalog begins its outward-only expedition at Saturn.
        // This is content policy: recovery and transfer mechanics consume the
        // capability, so future destinations can opt in without a code branch.
        destination.oneWayExpedition = destination.tier >= 4;
        if (destination.id == content::destination::mars) {
            destination.routeRequirementKeys = {content::unlock::routeMars};
        } else if (destination.id == content::destination::jupiter) {
            destination.routeRequirementKeys = {content::unlock::routeJupiter};
        } else if (destination.id == content::destination::saturn) {
            destination.routeRequirementKeys = {content::unlock::routeSaturn};
        } else if (destination.id == content::destination::uranus) {
            destination.routeRequirementKeys = {content::unlock::routeUranus};
        } else if (destination.id == content::destination::neptune) {
            destination.routeRequirementKeys = {content::unlock::routeNeptune};
        }
    }

    // The solar campaign is linear today, but route identity is authored
    // explicitly so travel, recovery, save migration, and scene presentation
    // never infer a physical source from the frontier index.  Cost values
    // preserve the existing target-tier launch math at calibrated throttle.
    catalog.routeLinks = {
        {content::routeLink::earthMoon, content::destination::earthOrbit, content::destination::moon, 10.0, true, false},
        {content::routeLink::moonMars, content::destination::moon, content::destination::mars, 15.0, true, false},
        {content::routeLink::marsJupiter, content::destination::mars, content::destination::jupiter, 20.0, true, false},
        {content::routeLink::jupiterSaturn, content::destination::jupiter, content::destination::saturn, 20.0, false, true},
        {content::routeLink::saturnUranus, content::destination::saturn, content::destination::uranus, 20.0, false, true},
        {content::routeLink::uranusNeptune, content::destination::uranus, content::destination::neptune, 20.0, false, true}
    };

    MiningSiteDefinition lunarAnomalyCrevice;
    lunarAnomalyCrevice.id = content::miningSite::lunarAnomalyCrevice;
    lunarAnomalyCrevice.version = 1;
    lunarAnomalyCrevice.arena = {MiningAct::ActOne, 1, 0, true, MiningGateType::FragileExcavation};
    lunarAnomalyCrevice.gateType = MiningGateType::FragileExcavation;
    lunarAnomalyCrevice.objectivePlacement = MiningSiteObjectivePlacement::EntryCentered;
    lunarAnomalyCrevice.objectivePassage = MiningPassageClass::SuitOnly;
    lunarAnomalyCrevice.activationMessage = "ANOMALOUS RETURN — PULSE SCANNER [E/X]";
    lunarAnomalyCrevice.completeOnShipCapture = true;
    lunarAnomalyCrevice.securedMessage =
        "ARTIFACT SECURED — similar signatures are scattered across the solar system.";
    lunarAnomalyCrevice.cocoon.id = "lunar_signal_crevice";
    lunarAnomalyCrevice.cocoon.version = 1;
    lunarAnomalyCrevice.cocoon.protectedObjective = {
        ProtectedObjectiveKind::Artifact,
        content::protectedObjective::lunarSignalArtifact};
    catalog.miningSites.push_back(std::move(lunarAnomalyCrevice));

    MiningSiteDefinition thermalLayeredRecovery;
    thermalLayeredRecovery.id = content::miningSite::thermalLayeredRecovery;
    thermalLayeredRecovery.version = 2;
    thermalLayeredRecovery.arena = {MiningAct::ActOne, 8, 0, true, MiningGateType::HazardCocoon};
    thermalLayeredRecovery.biome = MiningSiteBiome::ThermalLava;
    thermalLayeredRecovery.enemyTheme = MiningEnemyTheme::Lava;
    thermalLayeredRecovery.gateType = MiningGateType::HazardCocoon;
    thermalLayeredRecovery.objectivePlacement = MiningSiteObjectivePlacement::EntryCentered;
    thermalLayeredRecovery.baselineOxygenSeconds = tuning::mining::ioArtifactOxygenSeconds;
    // The first protected objective teaches the cocoon language with one
    // clearly visible seal, not a surprise second phase.
    thermalLayeredRecovery.cocoon.id = "thermal_intro_seal";
    thermalLayeredRecovery.cocoon.version = 2;
    thermalLayeredRecovery.cocoon.surveySignalDepthOffset = 1;
    // This is a content-owned payload identity. The cocoon and scenario
    // systems consume it generically; compatibility migration can preserve
    // the established artifact ID without either mechanic recognizing Io.
    thermalLayeredRecovery.cocoon.protectedObjective = {
        ProtectedObjectiveKind::Artifact,
        content::protectedObjective::ioMinorArtifact};
    thermalLayeredRecovery.cocoon.layers = {
        {"thermal", "THERMAL SEAL", {{0, -2}, {2, 0}, {0, 2}, {-2, 0}},
            MiningCocoonRevealPolicy::OnAnyCellDiscovered,
            MiningCocoonCompletionRule::TreatAndExcavate,
            MiningElementalAffinity::Thermal, 1}
    };
    catalog.miningSites.push_back(std::move(thermalLayeredRecovery));

    catalog.scenarios = {
        {
            content::scenario::lunarProspector,
            1,
            content::unlock::starter,
            content::destination::moon,
            {
                {"briefing", {}, "MOON", "Lunar Prospector Contract",
                    std::string("Most regolith is inert. Recover ") +
                        std::to_string(tuning::research::prospectorCommonOreGoal) +
                        " gray-seamed Common Ore deposits and return them safely.",
                    "REWARD // PROSPECTOR MK I + SLOT 1", "Accept Contract", {},
                    ScenarioEventKind::None, {}, {}, 1, 0, true, false, false,
                    ScenarioActionKind::AcknowledgeBriefing, {}, {}},
                {"delivery", {"briefing"}, "MOON", "Lunar Prospector Contract",
                    "Return 20 Common Ore to the ship. The contract allocation does not use permanent hold space.",
                    "PROSPECTOR MK I SECURED", "Pulse Scanner", {},
                    ScenarioEventKind::SafeMaterialDelivered, content::destination::moon, "common",
                    tuning::research::prospectorCommonOreGoal, 0, false, false, false,
                    ScenarioActionKind::None, {},
                    {{ScenarioRewardKind::UnlockKey, content::unlock::droneBay, 0, false},
                     {ScenarioRewardKind::DroneBaySlots, {}, 1, false},
                     {ScenarioRewardKind::SupportDrone, content::drone::miningDrone, 0, true},
                     {ScenarioRewardKind::FrontierReadiness, {}, 0, false}}},
                {"anomaly", {"delivery"}, "MOON", "Anomalous Return",
                    "Mission Control is picking up a second signal. Pulse the scanner and recover its source.",
                    "REWARD // MARS ROUTE", "Confirm Recovery", {},
                    ScenarioEventKind::ProtectedObjectiveExtracted, {}, content::miningSite::lunarAnomalyCrevice,
                    1, 0, false, true, false,
                    ScenarioActionKind::ClaimReward, content::miningSite::lunarAnomalyCrevice,
                    {{ScenarioRewardKind::UnlockKey, content::unlock::routeMars, 0, false}}}
            }
        },
        {
            content::scenario::marsBayExpansion,
            2,
            content::unlock::routeMars,
            content::destination::mars,
            {
                {"briefing", {}, "MARS", "Bay Expansion",
                    std::string("Recover ") + std::to_string(tuning::research::marsBayCommonOreGoal) +
                        " Martian Common Ore. Oxygen, drill heat, integrity, repairs, and the return decision are now live.",
                    "REWARD // EMPTY SUPPORT DRONE SLOT 2", "Accept Contract", {},
                    ScenarioEventKind::None, {}, {}, 1, 0, true, false, false,
                    ScenarioActionKind::AcknowledgeBriefing, {}, {}},
                {"delivery", {"briefing"}, "MARS", "Bay Expansion",
                    std::string("Deliver ") + std::to_string(tuning::research::marsBayCommonOreGoal) +
                        " Mars Common Ore loaded onto the Ship. Normal departure returns all Ship ore. The reward opens an empty slot. No second Support Drone is required.",
                    "REWARD // EMPTY SUPPORT DRONE SLOT 2", "Fabricate Slot 2", {},
                    ScenarioEventKind::SafeMaterialDelivered, content::destination::mars, "common",
                    tuning::research::marsBayCommonOreGoal, 0, false, true, false,
                    ScenarioActionKind::ClaimReward, {},
                     {{ScenarioRewardKind::DroneBaySlots, {}, 2, false},
                      {ScenarioRewardKind::UnlockKey, content::unlock::routeJupiter, 0, false},
                      {ScenarioRewardKind::FrontierReadiness, {}, 0, false}}},
                {"funding", {"delivery"}, "JUPITER TRANSFER", "The Jupiter Window",
                    "Create five fuel of transfer margin. Build it into the ship, take it from Mars's gravity, or stack both.",
                    "FUEL TANKS III OR GOOD-OR-BETTER MARS SLINGSHOT — BENEFITS STACK", "Review Jupiter Options", {},
                    ScenarioEventKind::None, {}, {}, 1, 0, true, false, false,
                    ScenarioActionKind::AcknowledgeBriefing, {}, {}}
            }
        },
        {
            content::scenario::volcanicDescent,
            1,
            content::unlock::routeJupiter,
            content::destination::jupiter,
            {
                {"commission", {}, "JUPITER SYSTEM", "Volcanic Descent",
                    "Io regolith is inert. Commission Hazard support to cool lava into mineable gray Common Ore.",
                    "REWARD // HAZARD DRONE MK I", "Commission Hazard Drone", {},
                    ScenarioEventKind::ManualAction, {}, {}, 1, 0, true, false, false,
                    ScenarioActionKind::BeginActivity, {},
                    {{ScenarioRewardKind::UnlockKey, content::unlock::droneBay, 0, false},
                     {ScenarioRewardKind::DroneBaySlots, {}, 1, false},
                     {ScenarioRewardKind::UnlockKey, content::unlock::ioHazardDrone, 0, false},
                     {ScenarioRewardKind::SupportDrone, content::drone::hazardDrone, 1, true}}},
                {"recovery", {"commission"}, "IO // JUPITER SYSTEM", "Artifact Recovery",
                    "Discover the thermal seal, cool and excavate its four segments, then tether the exposed artifact home.",
                    "REWARD // 75 ARTIFACT XP + 10 OBJECTIVE XP + OUTER TRANSFER DATA", "Begin Volcanic Recovery", {},
                    ScenarioEventKind::ProtectedObjectiveExtracted, {}, content::miningSite::thermalLayeredRecovery, 1, 0, false, false, false,
                    ScenarioActionKind::BeginActivity, content::miningSite::thermalLayeredRecovery,
                    {{ScenarioRewardKind::UnlockKey, "outer_transfer_ready", 0, false}}}
            }
        },
        {
            content::scenario::outerTransfer,
            1,
            "outer_transfer_ready",
            content::destination::jupiter,
            {
                {"briefing", {}, "JUPITER DEPARTURE", "Perfect Slingshot",
                    "Saturn is beyond normal transfer range. Hold the gold corridor for a Perfect pass; departure commits the expedition outward.",
                    "REWARD // SATURN ROUTE", "Review", {},
                    ScenarioEventKind::None, {}, {}, 1, 0, true, false, false,
                    ScenarioActionKind::AcknowledgeBriefing, {}, {}},
                {"flyby", {"briefing"}, "JUPITER DEPARTURE", "Perfect Slingshot",
                    "Good is not enough. Hold the gold corridor through the finish.",
                    "REWARD // SATURN ROUTE", "Launch",
                    "INSUFFICIENT SLINGSHOT — Saturn remains locked. Hold the gold corridor for a Perfect pass.",
                    ScenarioEventKind::FlybyFinished, content::scenario::outerTransfer, {}, 1, static_cast<int>(FlybyGrade::Perfect), false, true, true,
                    ScenarioActionKind::BeginActivity, {},
                    {{ScenarioRewardKind::RouteAccess, content::destination::saturn, 0, false},
                      {ScenarioRewardKind::FrontierReadiness, {}, 0, false}}}
            }
        },
        {
            content::scenario::saturnDeparture,
            1,
            content::unlock::routeSaturn,
            content::destination::saturn,
            {
                {"artifact", {}, "SATURN DEPARTURE", "Artifact Secured",
                    "Recover one Saturn artifact and return it safely to permanent inventory.",
                    "REWARD // URANUS ROUTE", "Lock Uranus Course", {},
                    ScenarioEventKind::ArtifactRecovered, content::destination::saturn, {}, 1, 0,
                    false, true, false, ScenarioActionKind::ClaimReward, {},
                    {{ScenarioRewardKind::RouteAccess, content::destination::uranus, 0, false}}}
            }
        },
        {
            content::scenario::uranusDeparture,
            1,
            content::unlock::routeUranus,
            content::destination::uranus,
            {
                {"briefing", {}, "URANUS DEPARTURE", "Signal Beyond Neptune",
                    "Neptune is the last charted world. A repeating carrier signal is pulsing from the dark beyond it. Build a stable Neptune vector before the expedition leaves Uranus.",
                    "OBJECTIVE // 2 FLIGHT DATA", "Track Signal", {},
                    ScenarioEventKind::None, {}, {}, 1, 0, true, false, false,
                    ScenarioActionKind::AcknowledgeBriefing, {}, {}},
                {"artifact", {"briefing"}, "URANUS DEPARTURE", "Uranus Artifact",
                    "Recover the Uranus artifact and return it safely. Its telemetry supplies the first Flight Data key.",
                    "PROGRESS // 1 FLIGHT DATA", "Recover Artifact", {},
                    ScenarioEventKind::ArtifactRecovered, content::destination::uranus, {},
                    1, 0, false, false, false, ScenarioActionKind::None, {}, {}},
                {"vector", {"artifact"}, "URANUS DEPARTURE", "Neptune Vector",
                    "Artifact telemetry secured. Complete a Good or Perfect Orbit to bank the second Flight Data key.",
                    "REWARD // NEPTUNE ROUTE", "Lock Neptune Course", {},
                    ScenarioEventKind::FlightDataBanked, content::destination::uranus, content::destination::neptune,
                    2, 0, false, true, false, ScenarioActionKind::ClaimReward, {},
                    {{ScenarioRewardKind::RouteAccess, content::destination::neptune, 0, false}}}
            }
        },
        {
            content::scenario::neptuneDiscovery,
            1,
            content::unlock::routeNeptune,
            content::destination::neptune,
            {
                {"arrival", {}, "NEPTUNE", "Signal Beyond Neptune",
                    "The Neptune vector resolves on an impossible mass beyond the charted system.",
                    "OBJECTIVE // UNKNOWN CONTACT", "Investigate Contact", {},
                    ScenarioEventKind::DestinationReached, {}, content::destination::neptune,
                    1, 0, false, true, false, ScenarioActionKind::ClaimReward, {},
                    {{ScenarioRewardKind::CampaignMilestone, {}, 0, false, {}, CampaignMilestone::ArkDiscovered}}}
            }
        },
        {
            content::scenario::generatedTemplate,
            1,
            content::unlock::starter,
            {},
            {
                {"delivery", {}, "GENERATED SITE", "Material Recovery",
                    "Safely deliver the requested material.", "REWARD // CONFIGURED BY FACTORY", "Claim Reward", {},
                    ScenarioEventKind::SafeMaterialDelivered, {}, "common", 1, 0, false, true, false,
                    ScenarioActionKind::ClaimReward, {}, {}}
            },
            false
        }
    };
    // Every authored objective supplies the same player-facing contract. The
    // defaults deliberately live beside content rather than being inferred by
    // GamePanel, and specialized beats override only their actual transition.
    for (ScenarioDefinition& scenario : catalog.scenarios) {
        for (ScenarioStepDefinition& step : scenario.steps) {
            if (step.goalText.empty()) step.goalText = step.title;
            if (step.gateText.empty()) step.gateText = step.detail;
            if (step.nextStepText.empty()) step.nextStepText = step.actionLabel;
            if (!step.miningSiteDefinitionId.empty() &&
                step.action != ScenarioActionKind::ClaimReward) {
                step.activity = ScenarioActivityKind::MiningSite;
            } else if (step.completionEvent == ScenarioEventKind::FlybyFinished) {
                step.activity = ScenarioActivityKind::Flyby;
                step.retryPolicy = ScenarioRetryPolicy::PlayerConfirmed;
            }
            const bool awardsRoute = std::any_of(
                step.rewards.begin(), step.rewards.end(), [](const ScenarioReward& reward) {
                    return reward.kind == ScenarioRewardKind::RouteAccess;
                });
            if (awardsRoute) {
                step.transition = {ScenarioTransitionKind::QueueRewardedRoute, Screen::Hangar, StoryBriefingId::None};
            }
        }
    }
    const auto authoredScenario = [&](std::string_view id) -> ScenarioDefinition* {
        const auto found = std::find_if(catalog.scenarios.begin(), catalog.scenarios.end(), [&](const ScenarioDefinition& scenario) {
            return scenario.id == id;
        });
        return found == catalog.scenarios.end() ? nullptr : &*found;
    };
    if (ScenarioDefinition* lunar = authoredScenario(content::scenario::lunarProspector)) {
        lunar->steps[2].transition = {ScenarioTransitionKind::OpenScreen, Screen::Hangar, StoryBriefingId::None};
    }
    if (ScenarioDefinition* mars = authoredScenario(content::scenario::marsBayExpansion)) {
        mars->steps[1].transition = {ScenarioTransitionKind::OpenScreen, Screen::Hangar, StoryBriefingId::None};
    }
    if (ScenarioDefinition* neptune = authoredScenario(content::scenario::neptuneDiscovery)) {
        neptune->steps[0].transition = {ScenarioTransitionKind::PresentStoryTakeover, Screen::Hangar, StoryBriefingId::StraylightDiscovery};
        neptune->steps[0].presentationMode = ScenarioPresentationMode::Takeover;
        neptune->steps[0].goalText = "Investigate the impossible mass beyond Neptune.";
        neptune->steps[0].gateText = "A safe Neptune arrival is required.";
        neptune->steps[0].nextStepText = "Acknowledge the contact and begin the approach.";
    }
    catalog.transferAssists = {
        {
            content::transferAssist::marsJupiter,
            content::destination::mars,
            content::destination::jupiter,
            content::scenario::marsBayExpansion,
            "funding",
            {LaunchTrainingStage::HullIntegrity, LaunchTrainingStage::JupiterTransfer},
            FlybyGrade::Good,
            tuning::flyby::jupiterSlingshotFuelSavings,
            tuning::flyby::slingshotSpeedBoost,
            tuning::flyby::jupiterSlingshotGoodInstabilityPenalty,
            tuning::flyby::impactHullDamage,
            "Mars Slingshot"
        }
    };
    catalog.scenarioFactories = {
        {"generated_mining", 1, content::scenario::generatedTemplate, 0x5343454E4152494FULL}
    };

    std::string scenarioError;
    if (!validateCampaignProgressionCatalog(catalog, &scenarioError)) {
        throw std::logic_error("Invalid campaign scenario catalog: " + scenarioError);
    }
    if (!validateCargoRequirements(catalog, &scenarioError)) {
        throw std::logic_error("Invalid cargo catalog: " + scenarioError);
    }
    return catalog;
}

bool validateRouteCatalog(const ContentCatalog& catalog, std::string* error)
{
    const auto fail = [&](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };

    for (std::size_t index = 0; index < catalog.routeLinks.size(); ++index) {
        const RouteLinkDefinition& link = catalog.routeLinks[index];
        if (link.id.empty() || catalog.findRouteLink(link.id) != &link) {
            return fail("Each route link needs a unique stable id.");
        }
        const Destination* source = catalog.findDestination(link.sourceDestinationId);
        const Destination* target = catalog.findDestination(link.targetDestinationId);
        if (source == nullptr || target == nullptr || source == target) {
            return fail("Each route link needs distinct authored source and target destinations.");
        }
        if (link.cruiseFuelCost <= 0.0) {
            return fail("Each route link needs a positive calibrated fuel profile.");
        }
        if (link.oneWayExpedition && link.recoveryAvailable) {
            return fail("A one-way route cannot also offer recovery.");
        }
        if (link.oneWayExpedition != target->oneWayExpedition) {
            return fail("Route one-way policy must match its target destination policy.");
        }
    }

    for (const Destination& destination : catalog.destinations) {
        if (destination.hiddenFromProgression || destination.requiresHostileSystem ||
            destination.tier < 1 || destination.tier > 6) {
            continue;
        }
        const auto incomingCount = std::count_if(
            catalog.routeLinks.begin(),
            catalog.routeLinks.end(),
            [&](const RouteLinkDefinition& link) { return link.targetDestinationId == destination.id; });
        if (incomingCount != 1) {
            return fail("Each visible solar destination needs exactly one authored incoming route link.");
        }
    }
    return true;
}

bool hasUnlock(const MetaProgress& meta, std::string_view key)
{
    if (key.empty() || key == content::unlock::starter) {
        return true;
    }

    return std::find(meta.unlockKeys.begin(), meta.unlockKeys.end(), key) != meta.unlockKeys.end();
}

std::string unlockDisplayName(std::string_view key)
{
    if (key == content::unlock::thermal) {
        return "Thermal systems";
    }
    if (key == content::unlock::recovery) {
        return "Recovery hardware";
    }
    if (key == content::unlock::deepSpace) {
        return "Deep-space modules";
    }
    if (key == content::unlock::ai) {
        return "Predictive guidance";
    }
    if (key == content::unlock::exotic) {
        return "Exotic prototypes";
    }
    if (key == content::unlock::surfaceProbes) {
        return "Field probes";
    }
    if (key == content::unlock::surfaceDrills) {
        return "Surface drills";
    }
    if (key == content::unlock::cargoRigs) {
        return "Cargo return rigs";
    }
    if (key == content::unlock::analysisLab) {
        return "Mission analysis lab";
    }
    if (key == content::unlock::perimeterDrones) {
        return "Perimeter drones";
    }
    if (key == content::unlock::perimeterCoordination) {
        return "Perimeter coordination";
    }
    if (key == content::unlock::droneBay) {
        return "Drone bay";
    }
    if (key == content::unlock::droneSupportSuite) {
        return "Drone support suite";
    }
    if (key == content::unlock::ioHazardDrone) {
        return "Io hazard drone";
    }
    if (key == content::unlock::routeMars) {
        return "Mars route";
    }
    if (key == content::unlock::routeJupiter) {
        return "Jupiter route";
    }
    if (key == content::unlock::routeSaturn) {
        return "Saturn route";
    }
    if (key == content::unlock::routeUranus) {
        return "Uranus route";
    }
    if (key == content::unlock::routeNeptune) {
        return "Neptune route";
    }
    return {};
}

bool isModuleUnlocked(const MetaProgress& meta, const ShipModule& module)
{
    return hasUnlock(meta, module.unlockKey);
}

bool isCrewUpgradeUnlocked(const MetaProgress& meta, const CrewUpgrade& upgrade)
{
    return hasUnlock(meta, upgrade.unlockKey);
}

bool isMiniDroneUnlocked(const MetaProgress& meta, const MiniDrone& drone)
{
    if (drone.id == content::drone::hazardDrone
        && hasUnlock(meta, content::unlock::droneSupportSuite)
        && std::find(meta.ownedDroneIds.begin(), meta.ownedDroneIds.end(), drone.id) != meta.ownedDroneIds.end()) {
        return true;
    }
    return hasUnlock(meta, drone.unlockKey);
}

std::vector<const ShipModule*> unlockedModules(const ContentCatalog& catalog, const MetaProgress& meta)
{
    std::vector<const ShipModule*> result;
    for (const auto& module : catalog.modules) {
        if (isModuleUnlocked(meta, module)) {
            result.push_back(&module);
        }
    }
    return result;
}

std::vector<const CrewUpgrade*> unlockedCrewUpgrades(const ContentCatalog& catalog, const MetaProgress& meta)
{
    std::vector<const CrewUpgrade*> result;
    for (const auto& upgrade : catalog.crewUpgrades) {
        if (isCrewUpgradeUnlocked(meta, upgrade)) {
            result.push_back(&upgrade);
        }
    }
    return result;
}

std::vector<const MiniDrone*> unlockedMiniDrones(const ContentCatalog& catalog, const MetaProgress& meta)
{
    std::vector<const MiniDrone*> result;
    for (const auto& drone : catalog.miniDrones) {
        if (isMiniDroneUnlocked(meta, drone)) {
            result.push_back(&drone);
        }
    }
    return result;
}

std::string_view toString(SlotType slot)
{
    switch (slot) {
    case SlotType::Engine:
        return text::enums::slot::engine;
    case SlotType::Fuel:
        return text::enums::slot::fuel;
    case SlotType::Hull:
        return text::enums::slot::hull;
    case SlotType::Cooling:
        return text::enums::slot::cooling;
    case SlotType::Sensors:
        return text::enums::slot::sensors;
    case SlotType::Escape:
        return text::enums::slot::escape;
    }
    return text::enums::unknown;
}

std::string_view toString(RefitTrack track)
{
    switch (track) {
    case RefitTrack::Reach:
        return "REACH";
    case RefitTrack::Control:
        return "CONTROL";
    case RefitTrack::Recovery:
        return "RECOVERY";
    case RefitTrack::None:
        break;
    }
    return "SYSTEM";
}

std::string_view toString(LaunchUpgradeKind kind)
{
    switch (kind) {
    case LaunchUpgradeKind::None: return "None";
    case LaunchUpgradeKind::FuelTanks: return "Fuel Tanks";
    case LaunchUpgradeKind::FlightControls: return "Flight Controls";
    case LaunchUpgradeKind::Cooling: return "Engine Cooling";
    case LaunchUpgradeKind::Hull: return "Hull Plating";
    }
    return "None";
}

std::string_view toString(SurfaceDepthUpgradeKind kind)
{
    switch (kind) {
    case SurfaceDepthUpgradeKind::None: return "None";
    case SurfaceDepthUpgradeKind::SurveyArray: return "Survey Array";
    case SurfaceDepthUpgradeKind::BoreSystem: return "Bore System";
    }
    return "None";
}

std::string_view toString(LaunchTrainingStage stage)
{
    switch (stage) {
    case LaunchTrainingStage::FuelCalibration: return "Fuel calibration";
    case LaunchTrainingStage::FlightControlsCalibration: return "Flight controls calibration";
    case LaunchTrainingStage::MoonTransfer: return "Moon transfer";
    case LaunchTrainingStage::ThermalManagement: return "Thermal management";
    case LaunchTrainingStage::MarsTransfer: return "Mars transfer";
    case LaunchTrainingStage::HullIntegrity: return "Hull integrity";
    case LaunchTrainingStage::JupiterTransfer: return "Jupiter transfer";
    case LaunchTrainingStage::Complete: return "Launch training complete";
    }
    return "Fuel calibration";
}

std::string_view toString(LaunchMissionKind kind)
{
    switch (kind) {
    case LaunchMissionKind::Standard: return "Standard";
    case LaunchMissionKind::FuelCalibration: return "Fuel calibration";
    case LaunchMissionKind::FlightControlsCalibration: return "Flight controls calibration";
    case LaunchMissionKind::ThermalManagement: return "Thermal management";
    case LaunchMissionKind::AsteroidBelt: return "Asteroid belt";
    case LaunchMissionKind::StraylightApproach: return "Unknown contact approach";
    }
    return "Standard";
}

std::string_view toString(Rarity rarity)
{
    switch (rarity) {
    case Rarity::Common:
        return text::enums::rarity::common;
    case Rarity::Uncommon:
        return text::enums::rarity::uncommon;
    case Rarity::Rare:
        return text::enums::rarity::rare;
    case Rarity::Prototype:
        return text::enums::rarity::prototype;
    }
    return text::enums::unknown;
}

std::string_view toString(SurfaceUpgradeCategory category)
{
    switch (category) {
    case SurfaceUpgradeCategory::Drill:
        return "Drill";
    case SurfaceUpgradeCategory::Scanner:
        return "Scanner";
    case SurfaceUpgradeCategory::Drone:
        return "Drone";
    }
    return text::enums::unknown;
}

std::string_view toString(MiniDroneRole role)
{
    switch (role) {
    case MiniDroneRole::Mining:
        return "Mining";
    case MiniDroneRole::Resource:
        return "Resource";
    case MiniDroneRole::Survey:
        return "Survey";
    case MiniDroneRole::Hazard:
        return "Hazard";
    case MiniDroneRole::Attack:
        return "Attack";
    case MiniDroneRole::Defense:
        return "Defense";
    }
    return text::enums::unknown;
}

std::string_view toString(CrewStatus status)
{
    switch (status) {
    case CrewStatus::Active:
        return text::enums::crewStatus::active;
    case CrewStatus::Injured:
        return text::enums::crewStatus::injured;
    case CrewStatus::Dead:
        return text::enums::crewStatus::dead;
    }
    return text::enums::unknown;
}

std::string_view toString(LaunchResultType result)
{
    switch (result) {
    case LaunchResultType::None:
        return text::enums::launchResult::none;
    case LaunchResultType::SafeEject:
        return text::enums::launchResult::safeEject;
    case LaunchResultType::MissionComplete:
        return text::enums::launchResult::missionComplete;
    case LaunchResultType::Destroyed:
        return text::enums::launchResult::destroyed;
    }
    return text::enums::unknown;
}

std::string_view toString(RecoveryMethod method)
{
    switch (method) {
    case RecoveryMethod::None:
        return text::enums::recovery::none;
    case RecoveryMethod::ReturnHome:
        return text::enums::recovery::returnHome;
    case RecoveryMethod::ManualEject:
        return text::enums::recovery::manualEject;
    case RecoveryMethod::TransferArrival:
        return text::enums::recovery::transferArrival;
    }
    return text::enums::unknown;
}

std::string_view toString(LaunchFailureCause cause)
{
    switch (cause) {
    case LaunchFailureCause::None:
        return "None";
    case LaunchFailureCause::ThermalRunaway:
        return "Thermal runaway";
    case LaunchFailureCause::PressureRupture:
        return "Pressure rupture";
    case LaunchFailureCause::CourseLost:
        return "Course lost";
    case LaunchFailureCause::FuelExhausted:
        return "Fuel exhausted";
    case LaunchFailureCause::TrainingRescue:
        return "Training rescue";
    case LaunchFailureCause::HullBreach:
        return "Hull breach";
    case LaunchFailureCause::LunarImpact:
        return "Collision hull loss";
    }
    return "None";
}

std::string_view toString(CampaignMilestone milestone)
{
    switch (milestone) {
    case CampaignMilestone::SolarTutorial:
        return "Solar tutorial";
    case CampaignMilestone::ArkDiscovered:
        return "Ark discovered";
    case CampaignMilestone::FirstArkJumpReady:
        return "First Ark jump ready";
    case CampaignMilestone::FirstArkJumpComplete:
        return "First Ark jump complete";
    case CampaignMilestone::GravityWellDisaster:
        return "Gravity-well disaster";
    case CampaignMilestone::HostileSystemStranded:
        return "Hostile system stranded";
    case CampaignMilestone::ArkRepairing:
        return "Ark repairing";
    }
    return text::enums::unknown;
}

std::string_view toString(GameChapter chapter)
{
    switch (chapter) {
    case GameChapter::ProvingGround:
        return "Proving Ground";
    case GameChapter::LunarProgram:
        return "Lunar Program";
    case GameChapter::RedFrontier:
        return "Red Frontier";
    case GameChapter::Breakthrough:
        return "Breakthrough";
    case GameChapter::Straylight:
        return "Straylight";
    case GameChapter::Arkfall:
        return "Arkfall";
    case GameChapter::LastCampfire:
        return "Last Campfire";
    case GameChapter::VoidCompass:
        return "Void Compass";
    case GameChapter::Ouroboros:
        return "Ouroboros";
    case GameChapter::Ascent:
        return "Ascent";
    }
    return text::enums::unknown;
}

int chapterNumber(GameChapter chapter)
{
    return static_cast<int>(chapter);
}

std::string chapterLabel(GameChapter chapter)
{
    return "Chapter " + std::to_string(chapterNumber(chapter)) + ": " + std::string(toString(chapter));
}

std::string_view chapterGate(GameChapter chapter)
{
    switch (chapter) {
    case GameChapter::ProvingGround:
        return "Advance to the Moon.";
    case GameChapter::LunarProgram:
        return "Advance to Mars.";
    case GameChapter::RedFrontier:
        return "Advance to the Outer Planets.";
    case GameChapter::Breakthrough:
        return "Investigate the impossible contact beyond Neptune.";
    case GameChapter::Straylight:
        return "Leave the peaceful relay system with the Ark jump.";
    case GameChapter::Arkfall:
        return "Survive the gravity-well disaster and unlock Navigation.";
    case GameChapter::LastCampfire:
        return "Complete the first hostile-system sortie.";
    case GameChapter::VoidCompass:
        return "Complete the Rift Belt or deeper route.";
    case GameChapter::Ouroboros:
        return "Repair the Ark enough to attempt the next route.";
    case GameChapter::Ascent:
        return "Reach the future New Earth route.";
    }
    return text::enums::unknown;
}

std::string_view toString(ArkCondition condition)
{
    switch (condition) {
    case ArkCondition::NotFound:
        return "Not found";
    case ArkCondition::DerelictOperable:
        return "Derelict but operable";
    case ArkCondition::InFlight:
        return "In flight";
    case ArkCondition::DamagedStranded:
        return "Damaged and stranded";
    case ArkCondition::Repairing:
        return "Repairing";
    }
    return text::enums::unknown;
}

ModuleStats& operator+=(ModuleStats& lhs, const ModuleStats& rhs)
{
    lhs.thrust += rhs.thrust;
    lhs.fuel += rhs.fuel;
    lhs.hull += rhs.hull;
    lhs.cooling += rhs.cooling;
    lhs.sensors += rhs.sensors;
    lhs.escape += rhs.escape;
    lhs.pressure += rhs.pressure;
    lhs.volatility += rhs.volatility;
    lhs.payout += rhs.payout;
    lhs.miningPower += rhs.miningPower;
    lhs.miningYield += rhs.miningYield;
    lhs.miningCooling += rhs.miningCooling;
    lhs.miningDurability += rhs.miningDurability;
    lhs.miningWidth += rhs.miningWidth;
    lhs.miningDepth += rhs.miningDepth;
    lhs.miningStorage += rhs.miningStorage;
    lhs.miningEngineEfficiency += rhs.miningEngineEfficiency;
    return lhs;
}

ModuleStats operator+(ModuleStats lhs, const ModuleStats& rhs)
{
    lhs += rhs;
    return lhs;
}

} // namespace rocket
