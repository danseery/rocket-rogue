#pragma once

#include "core/GameText.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rocket {

enum class Screen {
    Hangar,
    Launch,
    Results,
    Upgrade,
    Legacy
};

enum class SlotType {
    Engine,
    Fuel,
    Hull,
    Cooling,
    Sensors,
    Escape
};

enum class Rarity {
    Common,
    Uncommon,
    Rare,
    Prototype
};

enum class CrewStatus {
    Active,
    Injured,
    Dead
};

enum class LaunchResultType {
    None,
    SafeEject,
    MissionComplete,
    Destroyed
};

enum class RecoveryMethod {
    None,
    ReturnHome,
    ManualEject,
    TransferArrival
};

struct ModuleStats {
    double thrust = 0.0;
    double fuel = 0.0;
    double hull = 0.0;
    double cooling = 0.0;
    double sensors = 0.0;
    double escape = 0.0;
    double pressure = 0.0;
    double volatility = 0.0;
    double payout = 0.0;
    double repair = 0.0;
};

struct CrewUpgradeStats {
    int trainingGain = 0;
    int trainingStressRelief = 0;
    int restStressBonus = 0;
    int launchStressRelief = 0;
    double traitModifier = 0.0;
};

struct ShipModule {
    std::string id;
    std::string name;
    SlotType slot = SlotType::Engine;
    Rarity rarity = Rarity::Common;
    ModuleStats stats;
    int durability = 100;
    std::string unlockKey = "starter";
    std::vector<std::string> tags;
};

struct CrewUpgrade {
    std::string id;
    std::string name;
    std::string description;
    Rarity rarity = Rarity::Common;
    CrewUpgradeStats stats;
    std::string unlockKey = "starter";
    std::vector<std::string> tags;
};

struct ShipFrame {
    std::string id;
    std::string name;
    std::vector<SlotType> slots;
    ModuleStats baseStats;
    int maxDamage = 100;
};

struct Astronaut {
    std::string id;
    std::string name;
    std::string background;
    std::string trait;
    int training = 0;
    int stress = 0;
    CrewStatus status = CrewStatus::Active;
};

struct Destination {
    std::string id;
    std::string name;
    int tier = 0;
    double targetMultiplier = 1.5;
    double minCrashMultiplier = 1.05;
    double maxCrashMultiplier = 2.0;
    double baseReward = 10.0;
    double hazard = 1.0;
    std::string unlockKey = "starter";
};

struct LaunchConfig {
    std::string destinationId;
    double burnGoalMultiplier = 1.5;
    bool frontierTransfer = false;
    std::string astronautId;
    std::string frameId;
    std::vector<std::string> equippedModuleIds;
};

struct TelemetryEvent {
    double multiplier = 1.0;
    double heat = 0.0;
    double pressure = 0.0;
    double vibration = 0.0;
    double fuelMix = 0.0;
    double guidance = 0.0;
    double abortRisk = 0.0;
    double stress = 0.0;
    double warning = 0.0;
    std::string message;
};

struct LaunchOutcome {
    LaunchResultType type = LaunchResultType::None;
    RecoveryMethod recoveryMethod = RecoveryMethod::None;
    std::string destinationId;
    std::string assignedAstronautId;
    bool frontierTransfer = false;
    double crashMultiplier = 1.0;
    double ejectMultiplier = 1.0;
    double payout = 0.0;
    double recoveryCost = 0.0;
    int shipDamage = 0;
    bool crewKilled = false;
    bool crewInjured = false;
    std::string moduleDestroyedId;
    int blueprintGain = 0;
    double peakWarning = 0.0;
    double peakAbortRisk = 0.0;
    std::vector<TelemetryEvent> telemetry;
};

struct MetaProgress {
    std::vector<std::string> unlockKeys;
    int blueprintProgress = 0;
    int furthestTier = 0;
    int shipsLost = 0;
    int astronautsLost = 0;
    std::vector<int> destinationAttempts;
    std::vector<int> destinationSuccesses;
    std::vector<std::string> memorials;
    std::vector<std::string> famousLaunches;
};

struct RunState {
    bool active = true;
    int destinationIndex = 0;
    int frontierReadiness = 0;
    int shipDamage = 0;
    double credits = 100.0;
    std::string frameId;
    std::vector<std::string> inventoryModuleIds;
    std::vector<std::string> equippedModuleIds;
    std::vector<std::string> crewUpgradeIds;
    std::vector<Astronaut> crew;
    std::array<std::string, 3> offerModuleIds {};
    std::array<std::string, 3> offerCrewUpgradeIds {};
    int launchesThisExpedition = 0;
    int offerRerollsThisExpedition = 0;
    int repairOpsThisExpedition = 0;
    int trainingOpsThisExpedition = 0;
    int restOpsThisExpedition = 0;
};

struct GameState {
    Screen screen = Screen::Hangar;
    std::uint64_t seed = 0xC0DEC0FFEEULL;
    RunState run;
    MetaProgress meta;
    LaunchConfig launchConfig;
    LaunchOutcome lastOutcome;
    std::string statusLine = std::string(text::status::welcome);
};

std::string_view toString(SlotType slot);
std::string_view toString(Rarity rarity);
std::string_view toString(CrewStatus status);
std::string_view toString(LaunchResultType result);
std::string_view toString(RecoveryMethod method);

ModuleStats operator+(ModuleStats lhs, const ModuleStats& rhs);
ModuleStats& operator+=(ModuleStats& lhs, const ModuleStats& rhs);

} // namespace rocket
