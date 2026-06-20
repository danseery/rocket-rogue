#pragma once

#include <string_view>

namespace rocket::save_schema {

inline constexpr std::string_view header = "RR_SAVE_V1";
inline constexpr char keyValueDelimiter = '=';
inline constexpr char listDelimiter = ',';
inline constexpr char textListDelimiter = '|';
inline constexpr char crewRecordDelimiter = ';';
inline constexpr char crewFieldDelimiter = ':';

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
inline constexpr std::string_view inventory = "inventory";
inline constexpr std::string_view equipped = "equipped";
inline constexpr std::string_view crewUpgrades = "crewUpgrades";
inline constexpr std::string_view unlocks = "unlocks";
inline constexpr std::string_view blueprints = "blueprints";
inline constexpr std::string_view furthestTier = "furthestTier";
inline constexpr std::string_view shipsLost = "shipsLost";
inline constexpr std::string_view astronautsLost = "astronautsLost";
inline constexpr std::string_view destinationAttempts = "destinationAttempts";
inline constexpr std::string_view destinationSuccesses = "destinationSuccesses";
inline constexpr std::string_view memorials = "memorials";
inline constexpr std::string_view famousLaunches = "famousLaunches";
inline constexpr std::string_view crew = "crew";

} // namespace field

} // namespace rocket::save_schema
