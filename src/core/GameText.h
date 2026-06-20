#pragma once

#include <string>
#include <string_view>

namespace rocket::text {

namespace status {
inline constexpr std::string_view welcome = "Welcome to Rocket Rogue.";
inline constexpr std::string_view programInitialized = "Program initialized. Prove the current frontier, then commit outward.";
inline constexpr std::string_view saveRestored = "Save data restored from local mission control.";
inline constexpr std::string_view webglFailed = "WebGL2 failed to initialize.";
inline constexpr std::string_view shipAlreadyReady = "The ship is already flight-ready.";
inline constexpr std::string_view repairsUnaffordable = "Not enough mission credits for repairs.";
inline constexpr std::string_view noTrainableAstronaut = "No active astronaut is available to train.";
inline constexpr std::string_view trainingBudgetDenied = "Training budget denied.";
inline constexpr std::string_view noRestableAstronaut = "No active astronaut is available to rest.";
inline constexpr std::string_view restBudgetDenied = "No room in the budget for shore leave.";
inline constexpr std::string_view noRecruitProfiles = "Mission control has no recruit profiles on file.";
inline constexpr std::string_view recruitUnaffordable = "Not enough mission credits to recruit new crew.";
inline constexpr std::string_view noFartherFrontier = "No farther frontier is charted in this proof of concept.";
inline constexpr std::string_view transferLedgerRejected = "Transfer data accepted, but the route ledger rejected the destination.";
inline constexpr std::string_view refitWindowOpened = "Choose one refit. Installation takes the whole hangar window.";
inline constexpr std::string_view refitWindowClosed = "Refit window closed. Handle repairs, crew, and the next flight plan.";
inline constexpr std::string_view refitRerollUnaffordable = "Not enough mission credits to reroll the refit board.";
inline constexpr std::string_view launchHullBlocked = "That vehicle is less rocket than cautionary sculpture.";
inline constexpr std::string_view launchCrewBlocked = "No living astronaut is cleared for launch.";
inline constexpr std::string_view provingBurnStarted = "Proving burn underway. Return home to bank data; eject only when the vehicle leaves you no choice.";
inline constexpr std::string_view transferBurnStarted = "Transfer attempt committed. Survive to the required burn, or abort before the ship decides for you.";
inline constexpr std::string_view returningWithThrust = "Return burn committed. Engines are pulling the ship home, but the systems are still under load.";
inline constexpr std::string_view driftingHome = "Return trajectory plotted. Fuel reserves are gone, so the ship is coasting home on gravity and luck.";
inline constexpr std::string_view fuelReserveGone = "Fuel reserve is gone. Coasting home on gravity and uncomfortable math.";
inline constexpr std::string_view returnBurnRotating = "Return burn committed. Rotating ship for retrograde flight.";
inline constexpr std::string_view coastingHome = "Coasting home. No thrust, less control, plenty of silence.";
inline constexpr std::string_view returnBurnUnderway = "Return burn underway. Systems are easing, but this is not free.";
inline constexpr std::string_view enginesCutAfterGoal = "Engines cut. Thermal load is dropping, but nav drift is growing.";
inline constexpr std::string_view enginesCut = "Engines cut. Cooler burn, less vibration, slower climb, shakier tracking.";
inline constexpr std::string_view transferBurnStable = "Transfer burn stable. Survive to the required burn or abort.";
inline constexpr std::string_view dataGoalReached = "Data goal reached. Return home now, or overburn for extra telemetry.";
inline constexpr std::string_view provingBurnStable = "Proving burn stable. Push for more data or return home.";
inline constexpr std::string_view engineCutConfirmed = "Engine cut confirmed. Ship is running cooler, but guidance drift is widening.";
inline constexpr std::string_view thrustRestored = "Thrust restored. Burn is climbing again, and so are the hot systems.";
inline constexpr std::string_view pressureReliefStuck = "Pressure relief valve stuck. PRESS is worse and nav authority is degraded.";
inline constexpr std::string_view pressureReliefOpened = "Pressure relief valve opened. PRESS dropped, but the vent shoved the ship off-track.";
inline constexpr std::string_view returnVehicleLost = "Return trajectory failed. Vehicle lost during recovery.";
inline constexpr std::string_view transferVehicleLost = "Transfer vehicle lost. New crew, new vehicle, same frontier ledger.";
inline constexpr std::string_view vehicleLost = "Vehicle lost. The agency recovered fragments and uncomfortable lessons.";
inline constexpr std::string_view extraProvingData = "Extra proving data banked. The curve is opening up, and so is the temptation.";
inline constexpr std::string_view missionDataBanked = "Mission data banked. Keep testing, upgrading, and deciding how bold the next burn should be.";
inline constexpr std::string_view transferAbortedEject = "Transfer aborted by ejection. Rescue was expensive, but the data record survived.";
inline constexpr std::string_view transferAbortedReturn = "Transfer aborted. Vehicle returned with valuable long-burn data.";
inline constexpr std::string_view transferEjectEarly = "Transfer ejection was early. Crew survived, but rescue ate the budget.";
inline constexpr std::string_view transferReturnEarly = "Transfer return was early. Crew survived, but the frontier remains unproven.";
inline constexpr std::string_view emergencyEjectUseful = "Emergency eject confirmed. Rescue recovered enough telemetry to matter.";
inline constexpr std::string_view earlyReturnUseful = "Early return confirmed. Useful flight data recovered from the proving route.";
inline constexpr std::string_view emergencyEjectShallow = "Emergency eject confirmed. Crew survived, budget bruised, little data banked.";
inline constexpr std::string_view earlyReturnShallow = "Early return confirmed. Safe, but the burn was too shallow to teach much.";
inline constexpr std::string_view rapidDecompression = "Rapid decompression after relief-valve actuation. Vehicle lost.";
inline constexpr std::string_view pressureReliefClosed = "Pressure relief valve closed. PRESS is building normally again, and the vent drift is fading.";
inline constexpr std::string_view cargoJettisoned = "Cargo jettisoned. Fuel mix stabilized, but debris and mass shift hurt NAV, VIB, and return margin.";
inline constexpr std::string_view moreProvingDataBeforeTransfer = "More proving data is needed before the transfer attempt.";
} // namespace status

namespace telemetry {
struct WarningCopy {
    std::string_view critical;
    std::string_view caution;
};

inline constexpr std::string_view nominal = "Tracking system margins below limits";
inline constexpr WarningCopy heat {"TEMP: cooling runaway", "TEMP: heat margin narrowing"};
inline constexpr WarningCopy pressure {"PRESS: chamber overpressure", "PRESS: injector pressure unstable"};
inline constexpr WarningCopy vibration {"VIB: structural oscillation", "VIB: frame resonance building"};
inline constexpr WarningCopy guidance {"NAV: guidance divergence", "NAV: tracking solution drifting"};
inline constexpr WarningCopy fuelMix {"MIX: fuel ratio out of range", "MIX: combustion efficiency falling"};
inline constexpr WarningCopy abortRisk {"ABORT: escape window collapsing", "ABORT: capsule margin thinning"};
} // namespace telemetry

namespace labels {
inline constexpr std::string_view missionCredits = "Mission credits";
inline constexpr std::string_view hullDamage = "Hull damage";
inline constexpr std::string_view transferTarget = "Transfer target";
inline constexpr std::string_view currentFrontier = "Current frontier";
inline constexpr std::string_view missionDifficulty = "Mission difficulty";
inline constexpr std::string_view crewStress = "Crew stress";
inline constexpr std::string_view burnDepth = "Burn depth";
inline constexpr std::string_view returnProgress = "Return progress";
inline constexpr std::string_view requiredBurn = "Required burn";
inline constexpr std::string_view dataGoal = "Data goal";
inline constexpr std::string_view returnRisk = "Return risk";
inline constexpr std::string_view peakWarning = "Peak warning";
inline constexpr std::string_view peakAbort = "Peak abort";
inline constexpr std::string_view creditDelta = "Credit delta";
inline constexpr std::string_view temp = "TEMP";
inline constexpr std::string_view press = "PRESS";
inline constexpr std::string_view vibration = "VIB";
inline constexpr std::string_view nav = "NAV";
inline constexpr std::string_view mix = "MIX";
inline constexpr std::string_view abort = "ABORT";
} // namespace labels

namespace moduleStats {
inline constexpr std::string_view thrustDetail = "Thrust";
inline constexpr std::string_view speed = "Speed";
inline constexpr std::string_view fuel = "Fuel";
inline constexpr std::string_view hull = "Hull";
inline constexpr std::string_view tempControl = "TEMP control";
inline constexpr std::string_view sensors = "Sensors";
inline constexpr std::string_view escape = "Escape";
inline constexpr std::string_view pressureControl = "Pressure control";
inline constexpr std::string_view volatility = "Volatility";
inline constexpr std::string_view dataPayout = "Data payout";
inline constexpr std::string_view repairCost = "Repair cost";
inline constexpr std::string_view damage = "Damage";
inline constexpr std::string_view speedChip = "SPD";
inline constexpr std::string_view fuelChip = "FUEL";
inline constexpr std::string_view hullChip = "HULL";
inline constexpr std::string_view tempChip = "TEMP";
inline constexpr std::string_view sensorsChip = "WARN";
inline constexpr std::string_view escapeChip = "ESC";
inline constexpr std::string_view pressureChip = "PCTRL";
inline constexpr std::string_view volatilityChip = "VOL";
inline constexpr std::string_view payoutChip = "PAY";
inline constexpr std::string_view repairChip = "FIX";
} // namespace moduleStats

namespace moduleThreats {
inline constexpr std::string_view shortensExposure = "Shortens exposure time";
inline constexpr std::string_view reducesEngineLoad = "Reduces engine load";
inline constexpr std::string_view stabilizesPressure = "Stabilizes chamber pressure";
inline constexpr std::string_view extendsReturnMargin = "Extends return margin";
inline constexpr std::string_view absorbsDamage = "Absorbs structural damage";
inline constexpr std::string_view lowersTemperature = "Lowers TEMP buildup";
inline constexpr std::string_view reducesPressureUncertainty = "Reduces pressure uncertainty";
inline constexpr std::string_view improvesWarningLuck = "Improves warning luck";
inline constexpr std::string_view improvesCrewSurvival = "Improves crew survival";
inline constexpr std::string_view improvesMissionOdds = "Improves mission odds";
} // namespace moduleThreats

namespace buttons {
inline constexpr std::string_view returnHome = "Return home";
inline constexpr std::string_view returningHome = "Returning home";
inline constexpr std::string_view eject = "Eject";
inline constexpr std::string_view cutEngines = "Cut engines";
inline constexpr std::string_view restoreThrust = "Restore thrust";
inline constexpr std::string_view reliefValve = "Relief valve";
inline constexpr std::string_view closeValve = "Close valve";
inline constexpr std::string_view reliefValveFailed = "Valve failed";
inline constexpr std::string_view valveClosed = "Valve closed";
inline constexpr std::string_view jettisonCargo = "Jettison cargo";
inline constexpr std::string_view cargoGone = "Cargo gone";
inline constexpr std::string_view install = "Install";
inline constexpr std::string_view assign = "Assign";
inline constexpr std::string_view unavailable = "Unavailable";
inline constexpr std::string_view skipRefit = "Skip refit";
inline constexpr std::string_view launchProvingFlight = "Launch proving flight";
inline constexpr std::string_view needFlightData = "Need flight data";
} // namespace buttons

inline std::string insufficientCreditsFor(std::string_view name)
{
    return "Insufficient mission credits for " + std::string(name) + ".";
}

inline std::string refitInstalled(std::string_view name)
{
    return "Installed " + std::string(name) + ". The refit took the available hangar window.";
}

inline std::string refitRerolled(int nextCost)
{
    return "Refit offers rerolled. The next reroll will cost " + std::to_string(nextCost) + " credits.";
}

inline std::string needCredits(int cost)
{
    return "Need " + std::to_string(cost) + " credits";
}

inline std::string repairedHull(int repaired)
{
    return "Repaired " + std::to_string(repaired) + " hull damage.";
}

inline std::string tooStressedForTraining(std::string_view astronautName)
{
    return std::string(astronautName) + " is too stressed for simulator work. Rest the crew first.";
}

inline std::string simulatorComplete(std::string_view astronautName)
{
    return std::string(astronautName) + " completed simulator burns.";
}

inline std::string crewRecovered(std::string_view astronautName, int stressRecovery)
{
    return std::string(astronautName) + " recovered " + std::to_string(stressRecovery) + " stress under current mission conditions.";
}

inline std::string recruitJoined(std::string_view recruitName, bool emergency)
{
    return emergency
        ? std::string(recruitName) + " was rushed in from the emergency recruitment pool."
        : std::string(recruitName) + " joined the roster.";
}

inline std::string moreFlightDataNeeded(std::string_view destinationName)
{
    return "More flight data is needed before committing to " + std::string(destinationName) + ".";
}

inline std::string transferAchieved(std::string_view destinationName)
{
    return "Transfer achieved: " + std::string(destinationName) + ". The proving route has moved outward.";
}

inline std::string transferAchievedNewRoute(std::string_view destinationName)
{
    return "Transfer achieved: " + std::string(destinationName) + ". New proving flights begin here.";
}

inline std::string fullProfileReturned(std::string_view destinationName)
{
    return "Full proving profile returned. Attempt the transfer to " + std::string(destinationName) + " when ready.";
}

inline std::string telemetryDecision(std::string_view message)
{
    return std::string(message) + ". Decide now: return or eject.";
}

inline std::string telemetryStatement(std::string_view message)
{
    return std::string(message) + ".";
}

inline std::string returnDriftWarning(std::string_view message)
{
    return std::string(message) + ". Coasting gives mission control fewer ways to help.";
}

inline std::string returnBurnWarning(std::string_view message)
{
    return std::string(message) + ". The return burn is still biting.";
}

} // namespace rocket::text
