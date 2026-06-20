#pragma once

#include <string>
#include <string_view>

namespace rocket::ui {

namespace actions {
inline constexpr std::string_view startLaunch = "start_launch";
inline constexpr std::string_view ejectNow = "eject_now";
inline constexpr std::string_view returnHome = "return_home";
inline constexpr std::string_view cutEngines = "cut_engines";
inline constexpr std::string_view pressureRelief = "pressure_relief";
inline constexpr std::string_view closeReliefValve = "close_relief_valve";
inline constexpr std::string_view jettisonCargo = "jettison_cargo";
inline constexpr std::string_view next = "next";
inline constexpr std::string_view attemptFrontier = "attempt_frontier";
inline constexpr std::string_view rerollOffers = "reroll_offers";
inline constexpr std::string_view repairShip = "repair_ship";
inline constexpr std::string_view recruitCrew = "recruit_crew";
inline constexpr std::string_view trainCrew = "train_crew";
inline constexpr std::string_view restCrew = "rest_crew";
inline constexpr std::string_view resetSave = "reset_save";
inline constexpr std::string_view buyOfferPrefix = "buy_offer:";

inline std::string buyOffer(int index)
{
    return std::string(buyOfferPrefix) + std::to_string(index);
}
} // namespace actions

namespace modals {
inline constexpr std::string_view telemetry = "telemetry";
inline constexpr std::string_view settings = "settings";
inline constexpr std::string_view ship = "ship";
inline constexpr std::string_view crew = "crew";
inline constexpr std::string_view frontier = "frontier";
inline constexpr std::string_view launchBlocked = "launch_blocked";
inline constexpr std::string_view legacy = "legacy";
} // namespace modals

} // namespace rocket::ui
