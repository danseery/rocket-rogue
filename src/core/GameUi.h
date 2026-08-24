#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rocket::ui {

namespace actions {
inline constexpr std::string_view newGame = "new_game";
inline constexpr std::string_view continueGame = "continue_game";
inline constexpr std::string_view prepareLaunch = "prepare_launch";
inline constexpr std::string_view startLaunch = "start_launch";
inline constexpr std::string_view returnHome = "return_home";
inline constexpr std::string_view arrivalOps = "arrival_ops";
inline constexpr std::string_view cutEngines = "cut_engines";
inline constexpr std::string_view next = "next";
inline constexpr std::string_view attemptFrontier = "attempt_frontier";
inline constexpr std::string_view openNavigation = "open_navigation";
inline constexpr std::string_view arkJump = "ark_jump";
inline constexpr std::string_view selectNavigationDestinationPrefix = "select_navigation:";
inline constexpr std::string_view rerollOffers = "reroll_offers";
inline constexpr std::string_view arrivalFlyby = "arrival_flyby";
inline constexpr std::string_view acknowledgeApproachIntroduction = "acknowledge_approach_introduction";
inline constexpr std::string_view flybyAbort = "flyby_abort";
inline constexpr std::string_view flybyContinue = "flyby_continue";
inline constexpr std::string_view arrivalOrbit = "arrival_orbit";
inline constexpr std::string_view orbitAbort = "orbit_abort";
inline constexpr std::string_view orbitContinue = "orbit_continue";
inline constexpr std::string_view arrivalLanding = "arrival_landing";
inline constexpr std::string_view arrivalOrbitDepart = "arrival_orbit_depart";
inline constexpr std::string_view skipArrivalFanfare = "skip_arrival_fanfare";
inline constexpr std::string_view acknowledgeStoryBriefing = "acknowledge_story_briefing";
inline constexpr std::string_view repairShip = "repair_ship";
inline constexpr std::string_view recruitCrew = "recruit_crew";
inline constexpr std::string_view recruitCandidatePrefix = "recruit_candidate:";
inline constexpr std::string_view trainCrew = "train_crew";
inline constexpr std::string_view restCrew = "rest_crew";
inline constexpr std::string_view resetSave = "reset_save";
inline constexpr std::string_view buyOfferPrefix = "buy_offer:";
inline constexpr std::string_view selectRefitOfferPrefix = "select_refit_offer:";
inline constexpr std::string_view researchProjectPrefix = "research_project:";
inline constexpr std::string_view acknowledgeResearchBreakthroughPrefix = "acknowledge_research_breakthrough:";
inline constexpr std::string_view surfaceUpgradePrefix = "surface_upgrade:";
inline constexpr std::string_view droneOps = "drone_ops";
inline constexpr std::string_view backToSurfaceOps = "back_to_surface_ops";
inline constexpr std::string_view equipDronePrefix = "equip_drone:";
inline constexpr std::string_view unequipDroneSlotPrefix = "unequip_drone_slot:";
inline constexpr std::string_view upgradeDroneSlot = "upgrade_drone_slot";
inline constexpr std::string_view skipResearch = "skip_research";
inline constexpr std::string_view surveySurface = "survey_surface";
inline constexpr std::string_view mineSurface = "mine_surface";
inline constexpr std::string_view pushSurface = "push_surface";
inline constexpr std::string_view extractSurface = "extract_surface";
inline constexpr std::string_view surfaceScanPulse = "surface_scan_pulse";
inline constexpr std::string_view surfaceScanBank = "surface_scan_bank";
inline constexpr std::string_view surfaceScanAbort = "surface_scan_abort";
inline constexpr std::string_view surfacePushStep = "surface_push_step";
inline constexpr std::string_view surfacePushBank = "surface_push_bank";
inline constexpr std::string_view miningScanner = "mining_scanner";
inline constexpr std::string_view miningTether = "mining_tether";
inline constexpr std::string_view miningRepairDrill = "mining_repair_drill";
inline constexpr std::string_view miningRepairDrone = "mining_repair_drone";
inline constexpr std::string_view miningStow = "mining_stow";
inline constexpr std::string_view miningAbort = "mining_abort";
inline constexpr std::string_view miningFailureAck = "mining_failure_ack";
inline constexpr std::string_view acknowledgeProspectorCompletion = "acknowledge_prospector_completion";
inline constexpr std::string_view acknowledgeLunarMiningBriefing = "acknowledge_lunar_mining_briefing";
inline constexpr std::string_view claimLunarProspector = "claim_lunar_prospector";
inline constexpr std::string_view acknowledgeMarsMiningBriefing = "acknowledge_mars_mining_briefing";
inline constexpr std::string_view claimMarsBayExpansion = "claim_mars_bay_expansion";
inline constexpr std::string_view commissionIoHazardDrone = "commission_io_hazard_drone";
inline constexpr std::string_view beginSaturnSlingshot = "begin_saturn_slingshot";
inline constexpr std::string_view retrySaturnSlingshot = "retry_saturn_slingshot";
inline constexpr std::string_view acknowledgeSaturnSlingshotFailure = "acknowledge_saturn_slingshot_failure";
inline constexpr std::string_view claimSaturnCourse = "claim_saturn_course";
inline constexpr std::string_view acknowledgeJupiterWindow = "acknowledge_jupiter_window";
inline constexpr std::string_view openJupiterRefit = "open_jupiter_refit";
inline constexpr std::string_view beginJupiterSlingshot = "begin_jupiter_slingshot";
inline constexpr std::string_view continueJupiterSlingshot = "continue_jupiter_slingshot";
inline constexpr std::string_view beginTransferAssistPrefix = "begin_transfer_assist:";
inline constexpr std::string_view continueTransferAssist = "continue_transfer_assist";
inline constexpr std::string_view scenarioActionPrefix = "scenario_action:";

inline std::string buyOffer(int index)
{
    return std::string(buyOfferPrefix) + std::to_string(index);
}

inline std::string selectRefitOffer(int index)
{
    return std::string(selectRefitOfferPrefix) + std::to_string(index);
}

inline std::string researchProject(int index)
{
    return std::string(researchProjectPrefix) + std::to_string(index);
}

inline std::string recruitCandidate(int index)
{
    return std::string(recruitCandidatePrefix) + std::to_string(index);
}

inline std::string selectNavigationDestination(int index)
{
    return std::string(selectNavigationDestinationPrefix) + std::to_string(index);
}

inline std::string surfaceUpgrade(int index)
{
    return std::string(surfaceUpgradePrefix) + std::to_string(index);
}

inline std::string equipDrone(int index)
{
    return std::string(equipDronePrefix) + std::to_string(index);
}

inline std::string unequipDroneSlot(int slotIndex)
{
    return std::string(unequipDroneSlotPrefix) + std::to_string(slotIndex);
}

inline std::string beginTransferAssist(std::string_view definitionId)
{
    return std::string(beginTransferAssistPrefix) + std::string(definitionId);
}

// Scenario IDs and step IDs are stable content identifiers. Keep the action
// payload flat so it crosses native RmlUi and the web shell through the same
// existing data-rr-action binding without positional button inference.
inline std::string scenarioAction(
    std::string_view scenarioId,
    std::string_view stepId,
    int actionKind)
{
    return std::string(scenarioActionPrefix) + std::string(scenarioId) + "|" +
        std::string(stepId) + "|" + std::to_string(actionKind);
}
} // namespace actions

namespace modals {
inline constexpr std::string_view settings = "settings";
inline constexpr std::string_view hangarDetails = "hangar_details";
inline constexpr std::string_view ship = "ship";
inline constexpr std::string_view crew = "crew";
inline constexpr std::string_view frontier = "frontier";
inline constexpr std::string_view launchBlocked = "launch_blocked";
inline constexpr std::string_view pilotIntake = "pilot_intake";
inline constexpr std::string_view legacy = "legacy";
inline constexpr std::string_view inventory = "inventory";
inline constexpr std::string_view map = "map";
inline constexpr std::string_view research = "research";
inline constexpr std::string_view surface = "surface";
inline constexpr std::string_view missionLog = "mission_log";
inline constexpr std::string_view miningFailure = "mining_failure";
inline constexpr std::string_view launchOutcome = "launch_outcome";
inline constexpr std::string_view flightReport = "flight_report";
inline constexpr std::string_view launchIntroduction = "launch_introduction";
inline constexpr std::string_view flightControlsIntroduction = "flight_controls_introduction";
inline constexpr std::string_view approachIntroduction = "approach_introduction";
inline constexpr std::string_view flybyIntroduction = "flyby_introduction";
inline constexpr std::string_view orbitIntroduction = "orbit_introduction";
inline constexpr std::string_view landingIntroduction = "landing_introduction";
inline constexpr std::string_view miniDroneIntroduction = "mini_drone_introduction";
inline constexpr std::string_view droneSynergies = "drone_synergies";
inline constexpr std::string_view miningIntroduction = "mining_introduction";
inline constexpr std::string_view surfaceSurveyIntroduction = "surface_survey_introduction";
inline constexpr std::string_view surfaceDigIntroduction = "surface_dig_introduction";
inline constexpr std::string_view prospectorCompletion = "prospector_completion";
inline constexpr std::string_view lunarMiningBriefing = "lunar_mining_briefing";
inline constexpr std::string_view marsMiningBriefing = "mars_mining_briefing";
inline constexpr std::string_view marsBayCompletion = "mars_bay_completion";
inline constexpr std::string_view ioVolcanicBriefing = "io_volcanic_briefing";
inline constexpr std::string_view saturnSlingshotBriefing = "saturn_slingshot_briefing";
inline constexpr std::string_view saturnSlingshotFailure = "saturn_slingshot_failure";
inline constexpr std::string_view jupiterWindow = "jupiter_window";
inline constexpr std::string_view jupiterSlingshotActive = "jupiter_slingshot_active";
} // namespace modals

namespace briefings {
inline constexpr std::string_view launch = "launch_controls";
inline constexpr std::string_view flightControlsCalibration = "flight_controls_calibration";
inline constexpr std::string_view approach = "approach_overview";
inline constexpr std::string_view flyby = "flyby_blueprints";
inline constexpr std::string_view orbit = "orbit_blueprints";
inline constexpr std::string_view landing = "landing_drone_upgrades";
inline constexpr std::string_view miniDrones = "mini_drones";
inline constexpr std::string_view mining = "mining_overview";
inline constexpr std::string_view surfaceSurveyIntroduction = "surface_survey_introduction";
inline constexpr std::string_view surfaceDigIntroduction = "surface_dig_introduction";
inline constexpr std::string_view surfaceSurveyComplete = "surface_survey_complete";
inline constexpr std::string_view surfaceDigComplete = "surface_dig_complete";
inline constexpr std::string_view prospectorComplete = "prospector_complete";

inline bool acknowledged(const std::vector<std::string>& ids, std::string_view id)
{
    for (const std::string& existing : ids) {
        if (existing == id) {
            return true;
        }
    }
    return false;
}

inline void acknowledge(std::vector<std::string>& ids, std::string_view id)
{
    if (!acknowledged(ids, id)) {
        ids.emplace_back(id);
    }
}
} // namespace briefings

} // namespace rocket::ui
