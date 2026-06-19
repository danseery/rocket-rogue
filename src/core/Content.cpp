#include "core/Content.h"

#include <algorithm>
#include <utility>

namespace rocket {

namespace {

ShipModule module(std::string id, std::string name, SlotType slot, Rarity rarity, ModuleStats stats, std::string unlockKey, std::vector<std::string> tags)
{
    ShipModule result;
    result.id = std::move(id);
    result.name = std::move(name);
    result.slot = slot;
    result.rarity = rarity;
    result.stats = stats;
    result.unlockKey = std::move(unlockKey);
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
        module("sparrow_engine", "Sparrow Engine", SlotType::Engine, Rarity::Common, {.thrust = 2.0, .volatility = 0.2}, "starter", {"steady", "starter"}),
        module("kestrel_engine", "Kestrel Engine", SlotType::Engine, Rarity::Uncommon, {.thrust = 3.6, .fuel = -0.4, .volatility = 0.45, .payout = 0.4}, "deep_space", {"fast", "hungry"}),
        module("nova_drive", "Nova Drive", SlotType::Engine, Rarity::Rare, {.thrust = 5.2, .cooling = -0.8, .volatility = 1.1, .payout = 1.1}, "exotic", {"prototype", "dangerous"}),

        module("stable_tank", "Stable Tank", SlotType::Fuel, Rarity::Common, {.fuel = 2.5, .hull = 0.2}, "starter", {"safe"}),
        module("slush_tank", "Slush Tank", SlotType::Fuel, Rarity::Uncommon, {.fuel = 4.0, .cooling = 0.4, .volatility = 0.35}, "thermal", {"cold"}),
        module("deep_reservoir", "Deep Reservoir", SlotType::Fuel, Rarity::Rare, {.thrust = 0.5, .fuel = 5.4, .volatility = 0.75}, "deep_space", {"long-haul"}),

        module("patchwork_hull", "Patchwork Hull", SlotType::Hull, Rarity::Common, {.hull = 2.6, .repair = 0.6}, "starter", {"cheap"}),
        module("titanium_rib", "Titanium Rib", SlotType::Hull, Rarity::Uncommon, {.hull = 4.2, .cooling = -0.2}, "recovery", {"durable"}),
        module("ablative_skin", "Ablative Skin", SlotType::Hull, Rarity::Rare, {.hull = 3.4, .cooling = 1.2, .escape = 0.4}, "thermal", {"heat-shield"}),

        module("radiator_vanes", "Radiator Vanes", SlotType::Cooling, Rarity::Common, {.hull = -0.2, .cooling = 2.5}, "starter", {"cooling"}),
        module("cryo_loop", "Cryo Loop", SlotType::Cooling, Rarity::Uncommon, {.fuel = -0.4, .cooling = 4.4}, "thermal", {"precision"}),
        module("sacrificial_sink", "Sacrificial Heat Sink", SlotType::Cooling, Rarity::Rare, {.hull = -0.8, .cooling = 6.0, .repair = -0.4}, "recovery", {"one-more-burn"}),

        module("analog_telemetry", "Analog Telemetry", SlotType::Sensors, Rarity::Common, {.sensors = 2.0}, "starter", {"honest"}),
        module("hazard_radar", "Hazard Radar", SlotType::Sensors, Rarity::Uncommon, {.sensors = 3.8, .escape = 0.2}, "deep_space", {"warning"}),
        module("predictive_guidance", "Predictive Guidance", SlotType::Sensors, Rarity::Prototype, {.thrust = 0.6, .sensors = 5.2, .volatility = 0.25}, "ai", {"forecast"}),

        module("spring_capsule", "Spring Capsule", SlotType::Escape, Rarity::Common, {.thrust = -0.2, .escape = 2.8}, "starter", {"eject"}),
        module("abort_tower", "Abort Tower", SlotType::Escape, Rarity::Uncommon, {.hull = 0.5, .escape = 4.6, .payout = -0.2}, "recovery", {"crew-first"}),
        module("phoenix_pod", "Phoenix Pod", SlotType::Escape, Rarity::Rare, {.escape = 6.2, .volatility = -0.3, .repair = 0.2}, "exotic", {"legendary"})
    };

    catalog.frames = {
        {"pathfinder", "Pathfinder Frame", {SlotType::Engine, SlotType::Fuel, SlotType::Hull, SlotType::Cooling, SlotType::Sensors, SlotType::Escape}, {.thrust = 1.0, .fuel = 1.0, .hull = 1.0, .cooling = 1.0, .sensors = 1.0, .escape = 1.0}, 100},
        {"sprinter", "Sprinter Frame", {SlotType::Engine, SlotType::Engine, SlotType::Fuel, SlotType::Cooling, SlotType::Sensors, SlotType::Escape}, {.thrust = 1.8, .fuel = 0.5, .hull = 0.4, .cooling = 0.8, .sensors = 0.8, .escape = 0.7, .volatility = 0.4}, 92},
        {"ark", "Ark Frame", {SlotType::Engine, SlotType::Fuel, SlotType::Hull, SlotType::Hull, SlotType::Cooling, SlotType::Escape}, {.thrust = 0.5, .fuel = 1.1, .hull = 2.0, .cooling = 0.8, .sensors = 0.5, .escape = 1.3}, 118}
    };

    catalog.astronauts = {
        {"ava", "Ava Singh", "Test pilot", "Calm under heat", 2, 0, CrewStatus::Active},
        {"marco", "Marco Bell", "Flight engineer", "Repairs modules faster", 1, 5, CrewStatus::Active},
        {"nia", "Nia Okonkwo", "Navigator", "Reads telemetry early", 1, 0, CrewStatus::Active},
        {"eli", "Eli Park", "Rescue specialist", "Improves ejection odds", 1, 0, CrewStatus::Active},
        {"jo", "Jo Alvarez", "Rookie", "Learns quickly", 0, 0, CrewStatus::Active},
        {"sana", "Sana Wright", "Systems analyst", "Finds blueprint fragments", 1, 10, CrewStatus::Active}
    };

    catalog.destinations = {
        {"earth_orbit", "Earth Orbit", 0, 1.45, 1.05, 2.25, 12.0, 0.65, "starter"},
        {"moon", "Moon", 1, 1.95, 1.10, 3.10, 20.0, 0.95, "starter"},
        {"mars", "Mars", 2, 2.65, 1.15, 4.15, 34.0, 1.35, "starter"},
        {"outer_planets", "Outer Planets", 3, 3.55, 1.20, 5.55, 54.0, 1.85, "deep_space"},
        {"nearby_star", "Nearby Star", 4, 5.10, 1.25, 7.85, 88.0, 2.45, "deep_space"},
        {"nearby_galaxy", "Nearby Galaxy", 5, 7.00, 1.30, 10.50, 144.0, 3.15, "exotic"}
    };

    return catalog;
}

bool hasUnlock(const MetaProgress& meta, std::string_view key)
{
    if (key.empty() || key == "starter") {
        return true;
    }

    return std::find(meta.unlockKeys.begin(), meta.unlockKeys.end(), key) != meta.unlockKeys.end();
}

bool isModuleUnlocked(const MetaProgress& meta, const ShipModule& module)
{
    return hasUnlock(meta, module.unlockKey);
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

std::string_view toString(SlotType slot)
{
    switch (slot) {
    case SlotType::Engine:
        return "Engine";
    case SlotType::Fuel:
        return "Fuel";
    case SlotType::Hull:
        return "Hull";
    case SlotType::Cooling:
        return "Cooling";
    case SlotType::Sensors:
        return "Sensors";
    case SlotType::Escape:
        return "Escape";
    }
    return "Unknown";
}

std::string_view toString(Rarity rarity)
{
    switch (rarity) {
    case Rarity::Common:
        return "Common";
    case Rarity::Uncommon:
        return "Uncommon";
    case Rarity::Rare:
        return "Rare";
    case Rarity::Prototype:
        return "Prototype";
    }
    return "Unknown";
}

std::string_view toString(CrewStatus status)
{
    switch (status) {
    case CrewStatus::Active:
        return "Active";
    case CrewStatus::Injured:
        return "Injured";
    case CrewStatus::Dead:
        return "Dead";
    }
    return "Unknown";
}

std::string_view toString(LaunchResultType result)
{
    switch (result) {
    case LaunchResultType::None:
        return "None";
    case LaunchResultType::SafeEject:
        return "Safe Eject";
    case LaunchResultType::MissionComplete:
        return "Mission Complete";
    case LaunchResultType::Destroyed:
        return "Destroyed";
    }
    return "Unknown";
}

std::string_view toString(RecoveryMethod method)
{
    switch (method) {
    case RecoveryMethod::None:
        return "None";
    case RecoveryMethod::ReturnHome:
        return "Return Home";
    case RecoveryMethod::ManualEject:
        return "Manual Eject";
    case RecoveryMethod::TransferArrival:
        return "Transfer Arrival";
    }
    return "Unknown";
}

ModuleStats& operator+=(ModuleStats& lhs, const ModuleStats& rhs)
{
    lhs.thrust += rhs.thrust;
    lhs.fuel += rhs.fuel;
    lhs.hull += rhs.hull;
    lhs.cooling += rhs.cooling;
    lhs.sensors += rhs.sensors;
    lhs.escape += rhs.escape;
    lhs.volatility += rhs.volatility;
    lhs.payout += rhs.payout;
    lhs.repair += rhs.repair;
    return lhs;
}

ModuleStats operator+(ModuleStats lhs, const ModuleStats& rhs)
{
    lhs += rhs;
    return lhs;
}

} // namespace rocket
