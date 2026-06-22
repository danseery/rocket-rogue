#pragma once

#include <string>
#include <string_view>

namespace rocket::ui {

namespace actions {
inline constexpr std::string_view startLaunch = "start_launch";
inline constexpr std::string_view ejectNow = "eject_now";
inline constexpr std::string_view returnHome = "return_home";
inline constexpr std::string_view arrivalOps = "arrival_ops";
inline constexpr std::string_view cutEngines = "cut_engines";
inline constexpr std::string_view pressureRelief = "pressure_relief";
inline constexpr std::string_view closeReliefValve = "close_relief_valve";
inline constexpr std::string_view jettisonCargo = "jettison_cargo";
inline constexpr std::string_view next = "next";
inline constexpr std::string_view attemptFrontier = "attempt_frontier";
inline constexpr std::string_view rerollOffers = "reroll_offers";
inline constexpr std::string_view arrivalFlyby = "arrival_flyby";
inline constexpr std::string_view arrivalOrbit = "arrival_orbit";
inline constexpr std::string_view arrivalLanding = "arrival_landing";
inline constexpr std::string_view repairShip = "repair_ship";
inline constexpr std::string_view recruitCrew = "recruit_crew";
inline constexpr std::string_view trainCrew = "train_crew";
inline constexpr std::string_view restCrew = "rest_crew";
inline constexpr std::string_view resetSave = "reset_save";
inline constexpr std::string_view buyOfferPrefix = "buy_offer:";
inline constexpr std::string_view researchProjectPrefix = "research_project:";
inline constexpr std::string_view skipResearch = "skip_research";
inline constexpr std::string_view surveySurface = "survey_surface";
inline constexpr std::string_view mineSurface = "mine_surface";
inline constexpr std::string_view pushSurface = "push_surface";
inline constexpr std::string_view extractSurface = "extract_surface";

inline std::string buyOffer(int index)
{
    return std::string(buyOfferPrefix) + std::to_string(index);
}

inline std::string researchProject(int index)
{
    return std::string(researchProjectPrefix) + std::to_string(index);
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
inline constexpr std::string_view research = "research";
inline constexpr std::string_view surface = "surface";
inline constexpr std::string_view phaseBriefing = "phase_briefing";
} // namespace modals

} // namespace rocket::ui
