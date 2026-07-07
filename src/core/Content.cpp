#include "core/Content.h"
#include "core/ContentIds.h"
#include "core/GameText.h"
#include "core/Tuning.h"

#include <algorithm>
#include <utility>

namespace rocket {

namespace {

ShipModule module(std::string id, std::string name, SlotType slot, Rarity rarity, ModuleStats stats, std::string unlockKey, std::vector<std::string> tags, MaterialInventory materialCost = {})
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
    return result;
}

CrewUpgrade crewUpgrade(std::string id, std::string name, std::string description, Rarity rarity, CrewUpgradeStats stats, std::string unlockKey, std::vector<std::string> tags)
{
    CrewUpgrade result;
    result.id = std::move(id);
    result.name = std::move(name);
    result.description = std::move(description);
    result.rarity = rarity;
    result.stats = stats;
    result.unlockKey = std::move(unlockKey);
    result.tags = std::move(tags);
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

ContentCatalog createDefaultContent()
{
    ContentCatalog catalog;

    catalog.modules = {
        module(content::module::sparrowEngine, "Sparrow Engine", SlotType::Engine, Rarity::Common, {.thrust = 2.0, .volatility = 0.2}, content::unlock::starter, {"steady", content::unlock::starter}),
        module(content::module::kestrelEngine, "Kestrel Engine", SlotType::Engine, Rarity::Uncommon, {.thrust = 3.6, .fuel = -0.4, .volatility = 0.45, .payout = 0.4}, content::unlock::deepSpace, {"fast", "hungry"}),
        module(content::module::novaDrive, "Nova Drive", SlotType::Engine, Rarity::Rare, {.thrust = 5.2, .cooling = -0.8, .volatility = 1.1, .payout = 1.1}, content::unlock::exotic, {"prototype", "dangerous"}, {.rare = 2, .exotic = 1}),

        module(content::module::stableTank, "Stable Tank", SlotType::Fuel, Rarity::Common, {.fuel = 2.5, .hull = 0.2, .pressure = 0.4}, content::unlock::starter, {"safe", "pressure"}),
        module(content::module::slushTank, "Slush Tank", SlotType::Fuel, Rarity::Uncommon, {.fuel = 4.0, .cooling = 0.4, .pressure = 0.8, .volatility = 0.35}, content::unlock::thermal, {"cold", "pressure"}),
        module(content::module::deepReservoir, "Deep Reservoir", SlotType::Fuel, Rarity::Rare, {.thrust = 0.5, .fuel = 5.4, .volatility = 0.75}, content::unlock::deepSpace, {"long-haul"}, {.common = 2, .rare = 1}),

        module(content::module::patchworkHull, "Patchwork Hull", SlotType::Hull, Rarity::Common, {.hull = 2.6, .repair = 0.6}, content::unlock::starter, {"cheap"}),
        module(content::module::titaniumRib, "Titanium Rib", SlotType::Hull, Rarity::Uncommon, {.hull = 4.2, .cooling = -0.2}, content::unlock::recovery, {"durable"}),
        module(content::module::ablativeSkin, "Ablative Skin", SlotType::Hull, Rarity::Rare, {.hull = 3.4, .cooling = 1.2, .escape = 0.4}, content::unlock::thermal, {"heat-shield"}),

        module(content::module::radiatorVanes, "Radiator Vanes", SlotType::Cooling, Rarity::Common, {.hull = -0.2, .cooling = 2.5}, content::unlock::starter, {"cooling"}),
        module(content::module::cryoLoop, "Cryo Loop", SlotType::Cooling, Rarity::Uncommon, {.fuel = -0.4, .cooling = 4.4}, content::unlock::thermal, {"precision"}),
        module(content::module::sacrificialSink, "Sacrificial Heat Sink", SlotType::Cooling, Rarity::Rare, {.hull = -0.8, .cooling = 6.0, .repair = -0.4}, content::unlock::recovery, {"one-more-burn"}),

        module(content::module::analogTelemetry, "Analog Telemetry", SlotType::Sensors, Rarity::Common, {.sensors = 2.0, .pressure = 0.3}, content::unlock::starter, {"honest", "pressure"}),
        module(content::module::hazardRadar, "Hazard Radar", SlotType::Sensors, Rarity::Uncommon, {.sensors = 3.8, .escape = 0.2, .pressure = 0.7}, content::unlock::deepSpace, {"warning", "pressure"}),
        module(content::module::predictiveGuidance, "Predictive Guidance", SlotType::Sensors, Rarity::Prototype, {.thrust = 0.6, .sensors = 5.2, .pressure = 1.0, .volatility = 0.25}, content::unlock::ai, {"forecast", "pressure"}, {.rare = 2}),

        module(content::module::springCapsule, "Spring Capsule", SlotType::Escape, Rarity::Common, {.thrust = -0.2, .escape = 2.8}, content::unlock::starter, {"eject"}),
        module(content::module::abortTower, "Abort Tower", SlotType::Escape, Rarity::Uncommon, {.hull = 0.5, .escape = 4.6, .payout = -0.2}, content::unlock::recovery, {"crew-first"}),
        module(content::module::phoenixPod, "Phoenix Pod", SlotType::Escape, Rarity::Rare, {.escape = 6.2, .volatility = -0.3, .repair = 0.2}, content::unlock::exotic, {"legendary"}, {.rare = 1, .exotic = 1}),

        module(content::module::surfaceMapper, "Surface Mapper", SlotType::Sensors, Rarity::Common, {.sensors = 0.4, .miningWidth = 1.0}, content::unlock::surfaceProbes, {"surface", "mining", "survey"}),
        module(content::module::regolithAuger, "Regolith Auger", SlotType::Engine, Rarity::Common, {.volatility = 0.10, .miningPower = 1.0}, content::unlock::surfaceDrills, {"surface", "mining", "drill"}),
        module(content::module::oreSorter, "Ore Sorter", SlotType::Fuel, Rarity::Uncommon, {.fuel = -0.2, .miningYield = 1.0}, content::unlock::surfaceDrills, {"surface", "mining", "yield"}, {.common = 1}),
        module(content::module::coolantSleeve, "Coolant Sleeve", SlotType::Cooling, Rarity::Uncommon, {.cooling = 0.4, .miningCooling = 1.1}, content::unlock::surfaceDrills, {"surface", "mining", "cooling"}, {.common = 1}),
        module(content::module::diamondBearings, "Diamond Bearings", SlotType::Hull, Rarity::Rare, {.hull = 0.2, .miningDurability = 1.2}, content::unlock::surfaceDrills, {"surface", "mining", "durable"}, {.common = 1, .rare = 1}),
        module(content::module::deepBoreFrame, "Deep-Bore Frame", SlotType::Fuel, Rarity::Rare, {.fuel = -0.4, .miningPower = 0.4, .miningDepth = 1.0}, content::unlock::cargoRigs, {"surface", "mining", "deep"}, {.common = 2, .rare = 1}),
        module(content::module::cargoSpine, "Cargo Spine", SlotType::Hull, Rarity::Common, {.hull = 0.4, .miningStorage = 3.0}, content::unlock::cargoRigs, {"surface", "mining", "cargo"}, {.common = 2}),
        module(content::module::haulerThrusters, "Hauler Thrusters", SlotType::Engine, Rarity::Uncommon, {.thrust = 0.6, .fuel = -0.2, .volatility = 0.20, .miningEngineEfficiency = 0.24}, content::unlock::cargoRigs, {"surface", "mining", "hauler"}, {.common = 2, .rare = 1}),
        module(content::module::massDriverWinch, "Mass Driver Winch", SlotType::Escape, Rarity::Rare, {.escape = 0.6, .volatility = 0.25, .miningStorage = 2.0, .miningEngineEfficiency = 0.34}, content::unlock::cargoRigs, {"surface", "mining", "artifact"}, {.rare = 2, .exotic = 1})
    };

    catalog.crewUpgrades = {
        crewUpgrade(content::crewUpgrade::analogSimBay, "Analog Simulator Bay", "Lower-stress rehearsal gear for routine burns.", Rarity::Common, {.trainingStressRelief = 2}, content::unlock::starter, {"simulator", "training"}),
        crewUpgrade(content::crewUpgrade::highGSimulator, "High-G Simulator", "A better simulator that teaches more per session.", Rarity::Uncommon, {.trainingGain = 1, .trainingStressRelief = 1}, content::unlock::recovery, {"simulator", "training"}),
        crewUpgrade(content::crewUpgrade::medicalRecoveryWard, "Medical Recovery Ward", "Dedicated med bays clear stress faster between launches.", Rarity::Uncommon, {.restStressBonus = 12}, content::unlock::recovery, {"medical", "stress"}),
        crewUpgrade(content::crewUpgrade::missionPsychOffice, "Mission Psychology Office", "Debrief support reduces post-flight stress load.", Rarity::Rare, {.launchStressRelief = 5, .traitModifier = 0.10}, content::unlock::thermal, {"psych", "stress"}),
        crewUpgrade(content::crewUpgrade::traitCoachingLab, "Trait Coaching Lab", "Specialist coaching amplifies astronaut trait advantages.", Rarity::Rare, {.trainingStressRelief = 2, .traitModifier = 0.25}, content::unlock::ai, {"coaching", "traits"})
    };

    catalog.surfaceUpgrades = {
        surfaceUpgrade(content::surfaceUpgrade::thermalDrillJackets, "Thermal Drill Jackets", "Insulated drill collars bleed heat before the bit redlines and steady deeper pushes.", Rarity::Common, SurfaceUpgradeCategory::Drill, {.drillCooling = 2.4, .drillDurability = 0.4}, {"drill", "cooling", "depth"}),
        surfaceUpgrade(content::surfaceUpgrade::widebandPulse, "Wideband Pulse", "A wider scanner ping maps shadowed ore seams, bad pockets, and one deeper layer.", Rarity::Common, SurfaceUpgradeCategory::Scanner, {.scannerRadius = 2.5, .hazardRelief = 0.02}, {"scanner", "reveal", "depth"}),
        surfaceUpgrade(content::surfaceUpgrade::cargoSkids, "Cargo Skids", "Low-friction skids help the drone bank heavier canisters before load drag gets ugly.", Rarity::Common, SurfaceUpgradeCategory::Drone, {.extractionRiskRelief = 0.02, .droneStorage = 2.0, .droneEngineEfficiency = 0.08}, {"drone", "cargo"}),
        surfaceUpgrade(content::surfaceUpgrade::shockMounts, "Shock Mounts", "Spring-loaded mounts protect the drill train through hard-rock chatter and contact jolts.", Rarity::Uncommon, SurfaceUpgradeCategory::Drill, {.drillDurability = 2.2, .hardRockBounceRelief = 0.18, .hazardRelief = 0.015}, {"drill", "durability", "recoil"}),
        surfaceUpgrade(content::surfaceUpgrade::oreScentArray, "Ore-Scent Array", "Spectral sniffers help the crew sort richer pockets from plain dust before the dig.", Rarity::Rare, SurfaceUpgradeCategory::Scanner, {.oreYieldChance = 0.14, .scannerRadius = 1.2, .hazardRelief = 0.01}, {"scanner", "yield", "survey"}),
        surfaceUpgrade(content::surfaceUpgrade::coolantMist, "Coolant Mist", "A hiss of cold vapor keeps the drill biting without cooking the head.", Rarity::Common, SurfaceUpgradeCategory::Drill, {.drillCooling = 1.6, .drillDurability = 0.6}, {"drill", "cooling"}),
        surfaceUpgrade(content::surfaceUpgrade::recoilBraces, "Recoil Braces", "Kickback struts turn hard-rock bonks into controlled shoves while the drone keeps moving.", Rarity::Uncommon, SurfaceUpgradeCategory::Drone, {.drillDurability = 0.5, .hardRockBounceRelief = 0.24, .droneSpeed = 0.25}, {"drone", "recoil", "control"}),
        surfaceUpgrade(content::surfaceUpgrade::oreHopper, "Ore Hopper", "A squat canister rack gives loose ore a cleaner ride back to the ship zone.", Rarity::Common, SurfaceUpgradeCategory::Drone, {.oreYieldChance = 0.07, .extractionRiskRelief = 0.01, .droneStorage = 1.0}, {"drone", "yield"}),
        surfaceUpgrade(content::surfaceUpgrade::emergencyWinch, "Emergency Winch", "A return tether preserves banked haul and softens emergency recall penalties.", Rarity::Uncommon, SurfaceUpgradeCategory::Drone, {.oxygenSeconds = 10.0, .extractionRiskRelief = 0.035, .artifactTowEfficiency = 0.25}, {"drone", "recovery"}),
        surfaceUpgrade(content::surfaceUpgrade::deepEchoMapper, "Deep Echo Mapper", "Low-frequency pings read deeper silhouettes and artifact pockets before the flare fades.", Rarity::Rare, SurfaceUpgradeCategory::Scanner, {.oreYieldChance = 0.04, .scannerRadius = 3.0, .hazardRelief = 0.015}, {"scanner", "depth", "artifact"}),
        surfaceUpgrade(content::surfaceUpgrade::expandablePanniers, "Expandable Panniers", "Fold-out panniers widen the free carry buffer before ore starts slowing the drone.", Rarity::Common, SurfaceUpgradeCategory::Drone, {.droneStorage = 3.0}, {"drone", "cargo", "storage"}),
        surfaceUpgrade(content::surfaceUpgrade::vectorNozzles, "Vector Nozzles", "Trim jets keep loaded turns crisp and burn less fuel under a heavy haul.", Rarity::Uncommon, SurfaceUpgradeCategory::Drone, {.droneSpeed = 0.15, .droneEngineEfficiency = 0.25}, {"drone", "engine", "load"}),
        surfaceUpgrade(content::surfaceUpgrade::artifactTowline, "Artifact Towline", "Braided towline spreads artifact drag so tethered relics pull cleaner toward the ship.", Rarity::Rare, SurfaceUpgradeCategory::Drone, {.extractionRiskRelief = 0.015, .artifactTowEfficiency = 0.40}, {"drone", "artifact", "tether"})
    };

    catalog.miniDrones = {
        miniDrone(content::drone::miningDrone, "Mining Drone", "Peels revealed ore pockets while the main rig keeps tunneling under pressure.", Rarity::Common, MiniDroneRole::Mining, {.passiveMiningRate = 0.12}, content::unlock::droneBay, {"excavation", "resource"}),
        miniDrone(content::drone::resourceDrone, "Resource Drone", "Carries backup oxygen and return consumables so the rig can stay longer before the swarm wins.", Rarity::Common, MiniDroneRole::Resource, {.oxygenSeconds = 28.0, .extractionRiskRelief = 0.015}, content::unlock::droneBay, {"logistics", "endurance"}),
        miniDrone(content::drone::surveyDrone, "Survey Drone", "Widens scanner pulses and outlines ore, artifacts, and hostile silhouettes through fog.", Rarity::Uncommon, MiniDroneRole::Survey, {.scannerRadius = 2.0}, content::unlock::droneBay, {"exploration", "navigation"}),
        miniDrone(content::drone::stabilizerDrone, "Stabilizer Drone", "Counter-thrusts hard-rock chatter so enemy hits and rough drilling cost less rig health.", Rarity::Uncommon, MiniDroneRole::Stabilizer, {.drillIntegrityRelief = 0.12, .hardRockBounceRelief = 0.28}, content::unlock::droneBay, {"engineering", "resilience"}),
        miniDrone(content::drone::attackDrone, "Attack Drone", "Auto-fires cyan shots, crits priority targets, and pulses a slowing field while you mine.", Rarity::Rare, MiniDroneRole::Attack, {.enemyEncounterRelief = 0.05, .sentryDamagePerSecond = 3.2, .areaControlDamagePerSecond = 0.85, .enemySlow = 0.12}, content::unlock::perimeterDrones, {"combat", "post-solar"}),
        miniDrone(content::drone::defenseDrone, "Defense Drone", "Projects teal shield arcs, absorbs incoming fire, and counter-hits enemies that reach the rig.", Rarity::Rare, MiniDroneRole::Defense, {.drillIntegrityRelief = 0.06, .enemyEncounterRelief = 0.08, .enemyDamageRelief = 0.32, .reactiveArmorDamagePerSecond = 1.6, .environmentalShieldRelief = 0.18}, content::unlock::perimeterDrones, {"defense", "post-solar"})
    };

    catalog.researchProjects = {
        researchProject(content::research::blueprintSurvey, "Blueprint Survey", "Map Mars strata for recoverable ship schematics.", Rarity::Common, 2, 2, {}, content::unlock::starter, "", {"blueprint", "survey"}),
        researchProject(content::research::fieldProbeNetwork, "Field Probe Network", "Seed landing zones with small probes before the crew commits action kits.", Rarity::Common, 2, 2, {.common = 1}, content::unlock::starter, content::unlock::surfaceProbes, {"surface", "survey"}),
        researchProject(content::research::appliedMaterialsLab, "Applied Materials Lab", "Convert field samples into sturdier research procedures.", Rarity::Uncommon, 2, 3, {.common = 2}, content::unlock::starter, content::unlock::recovery, {"materials", "facility"}),
        researchProject(content::research::missionAnalysisLab, "Mission Analysis Lab", "Build a debrief room that turns samples and flight notes into cleaner blueprints.", Rarity::Uncommon, 2, 3, {.common = 2, .rare = 1}, content::unlock::starter, content::unlock::analysisLab, {"blueprint", "facility"}),
        researchProject(content::research::regolithDrillRig, "Regolith Drill Rig", "Build compact drills that pull more useful ore from short surface sorties.", Rarity::Uncommon, 2, 3, {.common = 2, .rare = 1}, content::unlock::surfaceProbes, content::unlock::surfaceDrills, {"surface", "mining"}),
        researchProject(content::research::droneBayProgram, "Drone Bay Program", "Build a persistent bay for helper drones that support mining, scouting, logistics, and later defense.", Rarity::Uncommon, 2, 3, {.common = 2, .rare = 1}, content::unlock::surfaceDrills, content::unlock::droneBay, {"surface", "drone", "logistics"}),
        researchProject(content::research::cargoReturnRig, "Cargo Return Rig", "Prototype restraint frames that make heavier payloads less terrifying to extract.", Rarity::Uncommon, 2, 3, {.common = 3}, content::unlock::recovery, content::unlock::cargoRigs, {"surface", "extraction"}),
        researchProject(content::research::prototypeSchematic, "Prototype Schematic", "Use rare samples to unlock experimental ship components.", Rarity::Rare, 2, 4, {.common = 1, .rare = 1}, content::unlock::starter, content::unlock::thermal, {"prototype", "ship"}),
        researchProject(content::research::xenogeologyProgram, "Xenogeology Program", "Study outer-system deposits for deep-space unlocks.", Rarity::Rare, 3, 5, {.rare = 2}, content::unlock::deepSpace, content::unlock::ai, {"materials", "deep_space"}),
        researchProject(content::research::arkScaffoldProgram, "Ark Keel Program", "Lay the first orbital keel sections for a future deep-space home base.", Rarity::Prototype, 3, 6, {.common = 4, .rare = 2}, content::unlock::deepSpace, content::unlock::arkScaffold, {"ark", "home_base", "deep_space"}),
        researchProject(content::research::perimeterDroneNetwork, "Perimeter Drone Network", "Deploy autonomous sentries so field teams can keep working under hostile contact.", Rarity::Rare, 4, 5, {.rare = 2, .exotic = 1}, content::unlock::ai, content::unlock::perimeterDrones, {"surface", "defense"}),
        researchProject(content::research::artifactDecoding, "Artifact Decoding", "Decode recovered signals into exotic research threads.", Rarity::Prototype, 4, 7, {.rare = 2, .exotic = 1}, content::unlock::ai, content::unlock::exotic, {"artifact", "story"})
    };

    catalog.frames = {
        {content::frame::pathfinder, "Pathfinder Frame", {SlotType::Engine, SlotType::Fuel, SlotType::Hull, SlotType::Cooling, SlotType::Sensors, SlotType::Escape}, {.thrust = 1.0, .fuel = 1.0, .hull = 1.0, .cooling = 1.0, .sensors = 1.0, .escape = 1.0}, 100},
        {content::frame::sprinter, "Sprinter Frame", {SlotType::Engine, SlotType::Engine, SlotType::Fuel, SlotType::Cooling, SlotType::Sensors, SlotType::Escape}, {.thrust = 1.8, .fuel = 0.5, .hull = 0.4, .cooling = 0.8, .sensors = 0.8, .escape = 0.7, .volatility = 0.4}, 92},
        {content::frame::ark, "Ark Frame", {SlotType::Engine, SlotType::Fuel, SlotType::Hull, SlotType::Hull, SlotType::Cooling, SlotType::Escape}, {.thrust = 0.5, .fuel = 1.1, .hull = 2.0, .cooling = 0.8, .sensors = 0.5, .escape = 1.3}, 118}
    };

    catalog.astronauts = {
        {content::astronaut::ava, "Mara Capybara", "Capybara Tank - Survival", std::string(tuning::traits::beastMode), 2, 0, CrewStatus::Active},
        {content::astronaut::marco, "Bram Beaver", "Beaver Engineer - Resilience", std::string(tuning::traits::hardReboot), 1, 5, CrewStatus::Active},
        {content::astronaut::nia, "Vela Fox", "Fox Ace - Navigation", std::string(tuning::traits::outtaHere), 1, 0, CrewStatus::Active},
        {content::astronaut::eli, "Pip Prairie Dog", "Prairie Dog Scout - Digging", std::string(tuning::traits::deepFocus), 1, 0, CrewStatus::Active},
        {content::astronaut::jo, "Nix Squirrel", "Squirrel Hoarder - Resource Gathering", std::string(tuning::traits::rummageSale), 0, 0, CrewStatus::Active},
        {content::astronaut::sana, "Kip Chipmunk", "Chipmunk Speedster - Exploration", std::string(tuning::traits::phaseShift), 1, 10, CrewStatus::Active}
    };

    catalog.destinations = {
        {content::destination::earthOrbit, "Earth Orbit", 0, 1.45, 1.05, 2.25, 12.0, 0.65, content::unlock::starter},
        {content::destination::moon, "Moon", 1, 1.95, 1.10, 3.10, 20.0, 0.95, content::unlock::starter},
        {content::destination::mars, "Mars", 2, 2.65, 1.15, 4.15, 34.0, 1.35, content::unlock::starter},
        {content::destination::outerPlanets, "Outer Planets", 3, 3.55, 1.20, 5.55, 54.0, 1.85, content::unlock::deepSpace},
        {content::destination::nearbyStar, "Khepri Prime", 4, 5.10, 1.25, 7.85, 88.0, 2.45, content::unlock::deepSpace},
        {content::destination::nearbyGalaxy, "Rift Belt", 5, 7.00, 1.30, 10.50, 144.0, 3.15, content::unlock::exotic}
    };

    return catalog;
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
    if (key == content::unlock::droneBay) {
        return "Drone bay";
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
    case MiniDroneRole::Stabilizer:
        return "Stabilizer";
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
        return "Find the operational Ark beyond Neptune.";
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
    lhs.repair += rhs.repair;
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
