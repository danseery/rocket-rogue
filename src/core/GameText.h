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
inline constexpr std::string_view noRestNeeded = "Crew is already rested and cleared for duty.";
inline constexpr std::string_view restBudgetDenied = "No room in the budget for shore leave.";
inline constexpr std::string_view noRecruitProfiles = "Mission control has no recruit profiles on file.";
inline constexpr std::string_view recruitUnaffordable = "Not enough mission credits to recruit new crew.";
inline constexpr std::string_view noFartherFrontier = "No farther frontier is charted in this proof of concept.";
inline constexpr std::string_view transferLedgerRejected = "Transfer data accepted, but the route ledger rejected the destination.";
inline constexpr std::string_view refitWindowOpened = "Choose one refit. Installation takes the whole hangar window.";
inline constexpr std::string_view refitWindowClosed = "Refit window closed. Handle repairs, crew, and the next flight plan.";
inline constexpr std::string_view refitRerollUnaffordable = "Not enough mission credits to reroll the refit board.";
inline constexpr std::string_view researchWindowOpened = "Mars arrival confirmed. Pick one research priority before the field team deploys.";
inline constexpr std::string_view arrivalOpsOpened = "Arrival confirmed. Choose how much mission risk to take before refit.";
inline constexpr std::string_view flybyCompleted = "Flyby complete. The probe pass banked safe science and kept the vehicle moving.";
inline constexpr std::string_view orbitCompleted = "Orbital insertion complete. High-value mapping data recovered.";
inline constexpr std::string_view moonFlybyRequired = "Moon landing requires one flyby before the agency clears orbital work.";
inline constexpr std::string_view moonOrbitRequired = "Moon landing requires a successful orbit before descent.";
inline constexpr std::string_view landingCommitted = "Landing committed. Surface team is preparing the extraction plan.";
inline constexpr std::string_view researchWindowClosed = "Research window closed. Surface team is preparing the landing zone.";
inline constexpr std::string_view researchCompleted = "Research completed. Blueprint work logged in the agency archive.";
inline constexpr std::string_view researchSkipped = "Research skipped. The field team kept the schedule moving.";
inline constexpr std::string_view surfaceExpeditionStarted = "Surface expedition underway. Gather what you can, then extract before the risk spikes.";
inline constexpr std::string_view surfaceSurveyed = "Surface survey logged recoverable samples.";
inline constexpr std::string_view surfaceMined = "Mining team filled the return canisters.";
inline constexpr std::string_view surfacePushed = "Expedition pushed into riskier terrain.";
inline constexpr std::string_view surfaceExtracted = "Surface payload recovered.";
inline constexpr std::string_view surfaceExtractionRough = "Extraction was rough; only part of the payload survived.";
inline constexpr std::string_view surfaceSupplyBlocked = "Surface team does not have enough supply for that action.";
inline constexpr std::string_view surfaceDustHazard = "Dust interference burned extra supply during the survey.";
inline constexpr std::string_view surfaceDrillHazard = "Drill chatter damaged cargo canisters.";
inline constexpr std::string_view surfaceTerrainHazard = "Unstable terrain forced a costly route correction.";
inline constexpr std::string_view surfaceEquipmentFailure = "Equipment fault consumed spare supplies.";
inline constexpr std::string_view surfaceUnexpectedDeposit = "Field team uncovered an unexpected deposit.";
inline constexpr std::string_view surfaceCrewDiscovery = "Crew discovery added a blueprint lead.";
inline constexpr std::string_view surfaceEnemyContact = "Hostile contact forced the field team into a defensive retreat.";
inline constexpr std::string_view miningStarted = "Mining drone deployed. Drill, scan, stow, or abort before the field clock runs out.";
inline constexpr std::string_view miningStowed = "Mining payload stowed for surface extraction.";
inline constexpr std::string_view miningAborted = "Mining drone recalled. Payload is partial, but the crew is back on plan.";
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
inline constexpr std::string_view shallowRecoveryPenalty = "Mission control flagged repeated shallow recoveries and cut the credit award.";
inline constexpr std::string_view cleanShallowRecoveryDestroyed = "Mission control caught the pattern. The next clean panic recovery was denied, and the vehicle was lost.";
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
inline constexpr std::string_view flightData = "Flight data";
inline constexpr std::string_view missionDifficulty = "Mission difficulty";
inline constexpr std::string_view crewStress = "Crew stress";
inline constexpr std::string_view burnDepth = "Burn depth";
inline constexpr std::string_view returnProgress = "Return progress";
inline constexpr std::string_view requiredBurn = "Required burn";
inline constexpr std::string_view dataGoal = "Data goal";
inline constexpr std::string_view returnRisk = "Return risk";
inline constexpr std::string_view outcome = "Outcome";
inline constexpr std::string_view recovery = "Recovery";
inline constexpr std::string_view failurePoint = "Failure point";
inline constexpr std::string_view peakWarning = "Peak warning";
inline constexpr std::string_view peakAbort = "Peak abort";
inline constexpr std::string_view creditDelta = "Credit delta";
inline constexpr std::string_view blueprints = "Blueprints";
inline constexpr std::string_view artifactInsight = "Artifact insight";
inline constexpr std::string_view labBonus = "Lab bonus";
inline constexpr std::string_view commonMaterials = "Common mats";
inline constexpr std::string_view rareMaterials = "Rare mats";
inline constexpr std::string_view exoticMaterials = "Exotic mats";
inline constexpr std::string_view artifacts = "Artifacts";
inline constexpr std::string_view site = "Site";
inline constexpr std::string_view fieldKit = "Field kit";
inline constexpr std::string_view hazard = "Hazard";
inline constexpr std::string_view supply = "Supply";
inline constexpr std::string_view cargo = "Cargo";
inline constexpr std::string_view depth = "Depth";
inline constexpr std::string_view extractionRisk = "Extraction risk";
inline constexpr std::string_view contactRisk = "Contact risk";
inline constexpr std::string_view oxygen = "Oxygen";
inline constexpr std::string_view drillHeat = "Drill heat";
inline constexpr std::string_view drillIntegrity = "Drill integrity";
inline constexpr std::string_view targetMaterial = "Target";
inline constexpr std::string_view toughness = "Toughness";
inline constexpr std::string_view temp = "TEMP";
inline constexpr std::string_view press = "PRESS";
inline constexpr std::string_view vibration = "VIB";
inline constexpr std::string_view nav = "NAV";
inline constexpr std::string_view mix = "MIX";
inline constexpr std::string_view abort = "ABORT";
} // namespace labels

namespace units {
inline constexpr std::string_view damage = "damage";
inline constexpr std::string_view effective = "effective";
inline constexpr std::string_view steps = "steps";
inline constexpr std::string_view traitModifiers = "trait modifiers";
} // namespace units

namespace enums {
inline constexpr std::string_view unknown = "Unknown";

namespace slot {
inline constexpr std::string_view engine = "Engine";
inline constexpr std::string_view fuel = "Fuel";
inline constexpr std::string_view hull = "Hull";
inline constexpr std::string_view cooling = "Cooling";
inline constexpr std::string_view sensors = "Sensors";
inline constexpr std::string_view escape = "Escape";
} // namespace slot

namespace rarity {
inline constexpr std::string_view common = "Common";
inline constexpr std::string_view uncommon = "Uncommon";
inline constexpr std::string_view rare = "Rare";
inline constexpr std::string_view prototype = "Prototype";
} // namespace rarity

namespace crewStatus {
inline constexpr std::string_view active = "Active";
inline constexpr std::string_view injured = "Injured";
inline constexpr std::string_view dead = "Dead";
} // namespace crewStatus

namespace launchResult {
inline constexpr std::string_view none = "None";
inline constexpr std::string_view safeEject = "Safe Eject";
inline constexpr std::string_view missionComplete = "Mission Complete";
inline constexpr std::string_view destroyed = "Destroyed";
} // namespace launchResult

namespace recovery {
inline constexpr std::string_view none = "None";
inline constexpr std::string_view returnHome = "Return Home";
inline constexpr std::string_view manualEject = "Manual Eject";
inline constexpr std::string_view transferArrival = "Transfer Arrival";
} // namespace recovery
} // namespace enums

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
inline constexpr std::string_view miningPower = "Drill speed";
inline constexpr std::string_view miningYield = "Mining yield";
inline constexpr std::string_view miningCooling = "Drill cooling";
inline constexpr std::string_view miningDurability = "Drill durability";
inline constexpr std::string_view miningWidth = "Survey width";
inline constexpr std::string_view miningDepth = "Bore depth";
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
inline constexpr std::string_view miningPowerChip = "DRILL";
inline constexpr std::string_view miningYieldChip = "YIELD";
inline constexpr std::string_view miningCoolingChip = "MCOOL";
inline constexpr std::string_view miningDurabilityChip = "DUR";
inline constexpr std::string_view miningWidthChip = "WIDTH";
inline constexpr std::string_view miningDepthChip = "DEPTH";
inline constexpr std::string_view trainingChip = "TRAIN";
inline constexpr std::string_view simStressChip = "SIM STRESS";
inline constexpr std::string_view restChip = "REST";
inline constexpr std::string_view launchStressChip = "LAUNCH STRESS";
inline constexpr std::string_view traitChip = "TRAIT";
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
inline constexpr std::string_view cutsTougherRock = "Cuts tougher rock faster";
inline constexpr std::string_view recoversMoreOre = "Recovers more ore from each pocket";
inline constexpr std::string_view keepsDrillCool = "Keeps the mining drill under thermal limits";
inline constexpr std::string_view protectsDrillHead = "Protects the drill head from abuse";
inline constexpr std::string_view expandsSurveyGrid = "Opens a wider mining sector";
inline constexpr std::string_view opensDeeperShaft = "Opens deeper mining lanes";
} // namespace moduleThreats

namespace buttons {
inline constexpr std::string_view returnHome = "Return home";
inline constexpr std::string_view returningHome = "Returning home";
inline constexpr std::string_view arrivalOps = "Arrival ops";
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
inline constexpr std::string_view settings = "Settings";
inline constexpr std::string_view details = "Details";
inline constexpr std::string_view briefing = "Briefing";
inline constexpr std::string_view resetSave = "Reset save";
inline constexpr std::string_view startReplacementRefit = "Start replacement refit";
inline constexpr std::string_view reviewRefitOptions = "Review refit options";
inline constexpr std::string_view assignRepairBay = "Assign repair bay";
inline constexpr std::string_view recruitCrew = "Recruit crew";
inline constexpr std::string_view legacy = "Legacy";
inline constexpr std::string_view runFlyby = "Run flyby";
inline constexpr std::string_view enterOrbit = "Enter orbit";
inline constexpr std::string_view attemptLanding = "Attempt landing";
inline constexpr std::string_view conductResearch = "Conduct research";
inline constexpr std::string_view skipResearch = "Skip research";
inline constexpr std::string_view surveySite = "Survey site";
inline constexpr std::string_view mineDeposit = "Mine deposit";
inline constexpr std::string_view pushDeeper = "Push deeper";
inline constexpr std::string_view extractPayload = "Extract payload";
inline constexpr std::string_view pulseScanner = "Pulse scanner";
inline constexpr std::string_view stowPayload = "Stow payload";
inline constexpr std::string_view abortMining = "Abort mining";
} // namespace buttons

namespace panel {
inline constexpr std::string_view title = "Rocket Rogue";
inline constexpr std::string_view complete = "Complete";
inline constexpr std::string_view ready = "Ready";
inline constexpr std::string_view noActiveCrew = "No active crew";
inline constexpr std::string_view noneCleared = "None cleared";
inline constexpr std::string_view noPilot = "No pilot";
inline constexpr std::string_view noSpareModules = "No spare modules";
inline constexpr std::string_view noMaterials = "No materials";
inline constexpr std::string_view needMaterials = "Need materials";
inline constexpr std::string_view baselineTrainingRoom = "Baseline training room";
inline constexpr std::string_view noneCharted = "None charted";
inline constexpr std::string_view shipStable = "Ship stable";
inline constexpr std::string_view crewRested = "Crew rested";
inline constexpr std::string_view zeroCredits = "0 credits";

namespace sections {
inline constexpr std::string_view returnBurn = "Return burn";
inline constexpr std::string_view transferAttempt = "Transfer attempt";
inline constexpr std::string_view provingFlight = "Proving flight";
inline constexpr std::string_view telemetry = "Telemetry";
inline constexpr std::string_view flightControls = "Flight controls";
inline constexpr std::string_view result = "Result";
inline constexpr std::string_view arrivalOps = "Arrival Ops";
inline constexpr std::string_view missionResult = "Mission result";
inline constexpr std::string_view burnProfile = "Burn profile";
inline constexpr std::string_view peakTelemetry = "Peak telemetry";
inline constexpr std::string_view achievements = "Achievements";
inline constexpr std::string_view research = "Research Phase";
inline constexpr std::string_view surfaceExpedition = "Surface Expedition";
inline constexpr std::string_view miningRun = "Mining Run";
inline constexpr std::string_view refitWindow = "Refit window";
inline constexpr std::string_view recoveredResources = "Recovered resources";
inline constexpr std::string_view hangarBay = "Hangar Bay";
inline constexpr std::string_view hangarOps = "Hangar ops";
inline constexpr std::string_view missionLog = "Mission log";
} // namespace sections

namespace surfaceSites {
inline constexpr std::string_view surveyBasin = "Survey Basin";
inline constexpr std::string_view surveyBasinDetail = "Open terrain favors safe surveying and extra common samples.";
inline constexpr std::string_view oreShelf = "Ore Shelf";
inline constexpr std::string_view oreShelfDetail = "Dense deposits improve mining returns, but equipment strain rises.";
inline constexpr std::string_view fractureField = "Fracture Field";
inline constexpr std::string_view fractureFieldDetail = "Broken terrain improves artifact odds, while extraction gets uglier.";
} // namespace surfaceSites

namespace achievements {
inline constexpr std::string_view skinOfYourTeethTitle = "Skin of your teeth";

inline std::string skinOfYourTeethDetail(std::string_view margin)
{
    return "Survived within " + std::string(margin) + " of the failure point.";
}
} // namespace achievements

namespace modals {
inline constexpr std::string_view telemetryDetails = "Telemetry Details";
inline constexpr std::string_view settings = "Settings";
inline constexpr std::string_view shipDetails = "Ship Details";
inline constexpr std::string_view crewDetails = "Crew Details";
inline constexpr std::string_view frontierDetails = "Frontier Details";
inline constexpr std::string_view launchHold = "Launch Hold";
inline constexpr std::string_view legacy = "Legacy";
inline constexpr std::string_view researchDetails = "Research Details";
inline constexpr std::string_view surfaceDetails = "Surface Details";
inline constexpr std::string_view arrivalBriefing = "Arrival Briefing";
inline constexpr std::string_view researchBriefing = "Research Briefing";
inline constexpr std::string_view surfaceBriefing = "Surface Briefing";
} // namespace modals

namespace details {
inline constexpr std::string_view keyboard = "Keyboard";
inline constexpr std::string_view keyboardValue = "R return home, E eject";
inline constexpr std::string_view save = "Save";
inline constexpr std::string_view saveValue = "Browser localStorage";
inline constexpr std::string_view build = "Build";
inline constexpr std::string_view buildValue = "WebGL2 / Emscripten POC";
inline constexpr std::string_view equippedShipUpgrades = "Equipped Ship Upgrades";
inline constexpr std::string_view storedShipUpgrades = "Stored Ship Upgrades";
inline constexpr std::string_view inventory = "Inventory";
inline constexpr std::string_view active = "Active";
inline constexpr std::string_view crewClass = "Class";
inline constexpr std::string_view trait = "Trait";
inline constexpr std::string_view training = "Training";
inline constexpr std::string_view stress = "Stress";
inline constexpr std::string_view stressEffects = "Stress effects";
inline constexpr std::string_view status = "Status";
inline constexpr std::string_view crewFacilities = "Crew Facilities";
inline constexpr std::string_view facilities = "Facilities";
inline constexpr std::string_view facilityEffects = "Facility Effects";
inline constexpr std::string_view simulatorGain = "Simulator gain";
inline constexpr std::string_view simulatorStress = "Simulator stress";
inline constexpr std::string_view medicalRest = "Medical rest";
inline constexpr std::string_view launchStress = "Launch stress";
inline constexpr std::string_view traitModifiers = "Trait modifiers";
inline constexpr std::string_view current = "Current";
inline constexpr std::string_view next = "Next";
inline constexpr std::string_view transferBurn = "Transfer burn";
inline constexpr std::string_view crew = "Crew";
inline constexpr std::string_view requiredAction = "Required action";
inline constexpr std::string_view ship = "Ship";
inline constexpr std::string_view frontier = "Frontier";
inline constexpr std::string_view blueprints = "Blueprints";
inline constexpr std::string_view commonMaterials = "Common materials";
inline constexpr std::string_view rareMaterials = "Rare materials";
inline constexpr std::string_view exoticMaterials = "Exotic materials";
inline constexpr std::string_view artifacts = "Artifacts";
inline constexpr std::string_view artifactArchive = "Artifact Archive";
inline constexpr std::string_view decoded = "Decoded";
inline constexpr std::string_view awaitingResearch = "Awaiting research";
inline constexpr std::string_view shipsLost = "Ships lost";
inline constexpr std::string_view astronautsLost = "Astronauts lost";
inline constexpr std::string_view furthestTier = "Furthest tier";
inline constexpr std::string_view closestSurvival = "Closest survival";
inline constexpr std::string_view maxBurnDepth = "Max burn depth";
inline constexpr std::string_view maxPeakWarning = "Max peak warning";
inline constexpr std::string_view maxPeakAbort = "Max peak abort";
inline constexpr std::string_view bestCreditDelta = "Best credit delta";
inline constexpr std::string_view worstCreditDelta = "Worst credit delta";
inline constexpr std::string_view repairVehicle = "Repair vehicle";
inline constexpr std::string_view recruitCrew = "Recruit crew";
inline constexpr std::string_view repairAndRecruitCrew = "Repair vehicle and recruit crew";
inline constexpr std::string_view clearForLaunch = "Clear for launch";
inline constexpr std::string_view fieldRules = "Field Rules";
inline constexpr std::string_view fieldSpecialist = "Field specialist";
inline constexpr std::string_view researchRules = "Research Rules";
inline constexpr std::string_view arrivalPhase = "Arrival";
inline constexpr std::string_view researchPhase = "Research";
inline constexpr std::string_view surfacePhase = "Surface";
inline constexpr std::string_view refitPhase = "Refit";
inline constexpr std::string_view flyby = "Flyby";
inline constexpr std::string_view orbit = "Orbit";
inline constexpr std::string_view landing = "Landing";
inline constexpr std::string_view blueprintUse = "Blueprint use";
inline constexpr std::string_view materialsUse = "Materials use";
inline constexpr std::string_view artifactInsightUse = "Artifact insight";
inline constexpr std::string_view labBonusUse = "Lab bonus";
inline constexpr std::string_view skippedResearch = "Skipping research";
inline constexpr std::string_view surveyRisk = "Survey risk";
inline constexpr std::string_view miningRisk = "Mining risk";
inline constexpr std::string_view depthRisk = "Depth risk";
inline constexpr std::string_view extraction = "Extraction";
inline constexpr std::string_view toolMitigation = "Tool mitigation";
inline constexpr std::string_view hostileContact = "Hostile contact";
inline constexpr std::string_view phaseIntent = "Intent";
inline constexpr std::string_view phaseInputs = "Inputs";
inline constexpr std::string_view phaseOutputs = "Outputs";
inline constexpr std::string_view phaseRisk = "Risk";
inline constexpr std::string_view phaseNext = "Next";

inline std::string trainingDelta(int trainingGain)
{
    return "+" + std::to_string(trainingGain) + " training";
}

inline std::string stressDelta(int stressGain)
{
    return "+" + std::to_string(stressGain) + " stress";
}

inline std::string stressRecoveryNow(int stressRecovery)
{
    return "-" + std::to_string(stressRecovery) + " stress now";
}

inline std::string launchStressRelief(int stressRelief)
{
    return "-" + std::to_string(stressRelief) + " stress";
}
} // namespace details

namespace outcomes {
inline constexpr std::string_view returnFailure = "Return Failure";
inline constexpr std::string_view transferLost = "Transfer Lost";
inline constexpr std::string_view vehicleLost = "Vehicle Lost";
inline constexpr std::string_view emergencyEject = "Emergency Eject";
inline constexpr std::string_view profileReturned = "Profile Returned";
inline constexpr std::string_view earlyReturn = "Early Return";
inline constexpr std::string_view transferComplete = "Transfer Complete";
inline constexpr std::string_view transferAborted = "Transfer Aborted";
inline constexpr std::string_view dataProfileComplete = "Data Profile Complete";
inline constexpr std::string_view provingReturn = "Proving Return";
} // namespace outcomes

namespace messages {
inline constexpr std::string_view crewLossRecorded = "Crew loss recorded in the memorial ledger.";
inline constexpr std::string_view crewInjured = "Crew injured. Rest before you ask for another miracle.";
inline constexpr std::string_view postArrivalResearchReady = "Arrival opens a research window and surface expedition before refit.";
inline constexpr std::string_view chooseArrivalOperation = "Choose the arrival posture. Flyby is safest, orbit gives better science, landing opens surface materials and artifacts.";
inline constexpr std::string_view flybyDetail = "Safe science pass. Banks credits and blueprint progress, then returns to refit.";
inline constexpr std::string_view orbitDetail = "Riskier orbital work. Banks stronger science and opens research where facilities exist.";
inline constexpr std::string_view landingDetail = "Highest-value operation. Opens surface exploration, material recovery, and artifacts.";
inline constexpr std::string_view chooseOneRefit = "Choose one ship or crew upgrade. The install crew can only complete one refit before the next launch cycle.";
inline constexpr std::string_view recoveredResourcesDetail = "Recovered samples, blueprints, and artifacts feed research and material-gated ship parts.";
inline constexpr std::string_view totalHullBlocked = "Mission control will not clear a vehicle at total hull damage.";
inline constexpr std::string_view noLivingCrewBlocked = "No living crew specialist is currently cleared for launch.";
inline constexpr std::string_view noStructuralWork = "No structural work is needed right now.";
inline constexpr std::string_view simulatorMastered = "Simulator plan exhausted; this pilot is already at max training.";
inline constexpr std::string_view simulatorWouldOverstress = "Simulator burn would push crew stress over safe limits.";
inline constexpr std::string_view emergencyReplacement = "Emergency field specialist clears the launch soft lock.";
inline constexpr std::string_view reserveRoster = "Add another qualified specialist before the next proving run.";
inline constexpr std::string_view crewOpsFallback = "Improves crew operations";
inline constexpr std::string_view emergencyRecruitBackground = "Emergency habitat roster";
inline constexpr std::string_view agencyIntakeBackground = "Frontier habitat intake";
inline constexpr std::string_view replacementCadet = "Reserve Habitat Cadet";
inline constexpr std::string_view restoredCrewBackground = "Restored habitat crew record";
inline constexpr std::string_view generatedRecruitTrait = "Field Instincts";
inline constexpr std::string_view chooseOneResearch = "Choose one research priority. Mars field work can turn blueprints and recovered materials into better options.";
inline constexpr std::string_view researchAdvisoryReady = "Research choice ready";
inline constexpr std::string_view researchAdvisoryReadyDetail = "At least one project is funded. Choose the unlock that best supports the next launch cycle.";
inline constexpr std::string_view researchAdvisoryMaterials = "Materials short";
inline constexpr std::string_view researchAdvisoryMaterialsDetail = "Current projects need samples you do not have. Skip research and deploy the surface team to recover materials.";
inline constexpr std::string_view researchAdvisoryEmpty = "No active projects";
inline constexpr std::string_view researchAdvisoryEmptyDetail = "The lab has no viable project queued for this arrival. Send the field team down and keep the expedition moving.";
inline constexpr std::string_view surfaceExpeditionBrief = "Choose one field action, then extract the payload when the risk is no longer worth the next dig.";
inline constexpr std::string_view surfacePostureScout = "Recommended: gather data";
inline constexpr std::string_view surfacePostureScoutDetail = "No payload is loaded yet. Survey or mine before spending supply on deeper terrain.";
inline constexpr std::string_view surfacePostureStable = "Recommended: one more action is reasonable";
inline constexpr std::string_view surfacePostureStableDetail = "You have recoverable cargo and enough supply margin to keep working.";
inline constexpr std::string_view surfacePostureNarrowing = "Recommended: extract soon";
inline constexpr std::string_view surfacePostureNarrowingDetail = "Supply is low or recovery risk is rising. Take another action only if the reward is worth it.";
inline constexpr std::string_view surfacePostureGreedy = "Recommended: extract now";
inline constexpr std::string_view surfacePostureGreedyDetail = "The payload is valuable, but recovery risk is climbing into expensive territory.";
inline constexpr std::string_view surfacePostureExtract = "Required: extract now";
inline constexpr std::string_view surfacePostureExtractDetail = "No supply remains for field work. Bring the payload home before conditions get worse.";
inline constexpr std::string_view surfaceSurveyDetail = "Recover common samples; probes improve yield and reduce dust trouble.";
inline constexpr std::string_view surfaceMineDetail = "Fill cargo canisters; drills improve yield and rare-material odds.";
inline constexpr std::string_view surfacePushDetail = "Increase depth for artifacts and richer deposits; terrain gets less forgiving.";
inline constexpr std::string_view surfaceExtractDetail = "Recover the payload and return to refit. Cargo rigs reduce extraction risk.";

inline std::string closeCallSurvival(std::string_view margin)
{
    return "Skin of your teeth: survived within " + std::string(margin) + " of the failure point.";
}

inline std::string supplyCost(int cost)
{
    return std::to_string(cost) + " supply";
}

inline std::string needSupply(int cost)
{
    return "Need " + supplyCost(cost);
}
} // namespace messages

namespace ops {
inline constexpr std::string_view repairBay = "Repair bay";
inline constexpr std::string_view crewIntake = "Crew intake";
inline constexpr std::string_view reserveRoster = "Reserve roster";
inline constexpr std::string_view simulatorBurn = "Simulator burn";
inline constexpr std::string_view medicalRest = "Medical rest";
} // namespace ops

inline std::string credits(std::string value)
{
    return value + " credits";
}

inline std::string needCredits(std::string value)
{
    return "Need " + value + " credits";
}

inline std::string lostModule(std::string_view moduleId)
{
    return "Lost module: " + std::string(moduleId);
}

inline std::string attemptFrontier(std::string_view destinationName)
{
    return "Attempt: " + std::string(destinationName);
}

inline std::string rerollOffers(std::string cost)
{
    return "Reroll offers (" + cost + " credits)";
}

inline std::string blueprintGain(int amount)
{
    return "+" + std::to_string(amount) + " BP";
}

inline std::string materialSummary(int common, int rare, int exotic)
{
    return std::to_string(common) + " common, " + std::to_string(rare) + " rare, " + std::to_string(exotic) + " exotic";
}

inline std::string creditsAndMaterials(std::string_view credits, std::string_view materials)
{
    return std::string(credits) + " + " + std::string(materials);
}

inline std::string artifactSummary(int identified, int total)
{
    return std::to_string(identified) + " identified / " + std::to_string(total) + " recovered";
}

inline std::string unlocksFamily(std::string_view family)
{
    return "Unlocks: " + std::string(family);
}

inline std::string repairDetail(int repairAmount)
{
    return "Restore up to " + std::to_string(repairAmount) + " hull damage. Repeated assignments cost more this expedition.";
}

inline std::string simulatorDetail(int trainingGain, int stressGain)
{
    return "+" + std::to_string(trainingGain) + " training, +" + std::to_string(stressGain) + " stress. Repeated assignments cost more this expedition.";
}

inline std::string restDetail(int stressRecovery)
{
    return "-" + std::to_string(stressRecovery) + " stress at current difficulty. Repeated assignments cost more this expedition.";
}

inline constexpr std::string_view noRestDetail = "No stress or injury requires medical rest right now.";
inline constexpr std::string_view simulatorCapped = "Training capped";
inline constexpr std::string_view crewTooStressed = "Crew too stressed";

inline std::string trainingImpact(int trainingGain)
{
    return "+" + std::to_string(trainingGain) + " training per simulator burn";
}

inline std::string restImpact(int restStressBonus)
{
    return "+" + std::to_string(restStressBonus) + " rest recovery";
}

inline std::string launchStressImpact(int launchStressRelief)
{
    return "-" + std::to_string(launchStressRelief) + " stress after launches";
}

inline std::string simulatorStressImpact(int trainingStressRelief)
{
    return "-" + std::to_string(trainingStressRelief) + " simulator stress";
}

inline std::string traitModifierImpact(std::string percentValue)
{
    return percentValue + " " + std::string(units::traitModifiers);
}
} // namespace panel

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

inline std::string recruitId(int recruitNumber)
{
    return "recruit_" + std::to_string(recruitNumber);
}

inline std::string replacementId(int replacementNumber)
{
    return "replacement_" + std::to_string(replacementNumber);
}

inline bool isReplacementId(std::string_view astronautId)
{
    return astronautId.rfind("replacement_", 0) == 0;
}

inline std::string emergencyCadetName(int recruitNumber)
{
    return "Emergency Cadet " + std::to_string(recruitNumber);
}

inline std::string nextGenerationName(std::string_view templateName)
{
    return std::string(templateName) + " II";
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
