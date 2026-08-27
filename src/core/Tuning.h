#pragma once

#include "core/ContentIds.h"
#include "core/GameTypes.h"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace rocket::tuning {

struct RarityOfferCosts {
    int common = 22;
    int uncommon = 34;
    int rare = 62;
    int prototype = 92;
};

inline constexpr RarityOfferCosts refitCosts {};

namespace presentation {
inline constexpr double statChipMinimumMagnitude = 0.05;
} // namespace presentation

namespace records {
inline constexpr double closeCallSurvivalMargin = 0.05;
inline constexpr double skinOfYourTeethCreditBonus = 0.10;
} // namespace records

inline int moduleOfferCost(Rarity rarity)
{
    switch (rarity) {
    case Rarity::Common:
        return refitCosts.common;
    case Rarity::Uncommon:
        return refitCosts.uncommon;
    case Rarity::Rare:
        return refitCosts.rare;
    case Rarity::Prototype:
        return refitCosts.prototype;
    }
    return refitCosts.uncommon;
}

namespace traits {
inline constexpr std::string_view beastMode = "Beast Mode";
inline constexpr std::string_view hardReboot = "Hard Reboot";
inline constexpr std::string_view outtaHere = "Outta Here";
inline constexpr std::string_view deepFocus = "Deep Focus";
inline constexpr std::string_view rummageSale = "Rummage Sale";
inline constexpr std::string_view phaseShift = "Phase Shift";
inline constexpr std::string_view fieldInstincts = "Field Instincts";
inline constexpr std::string_view calmUnderHeat = beastMode;
inline constexpr std::string_view readsTelemetryEarly = deepFocus;
inline constexpr std::string_view improvesEjectionOdds = outtaHere;
inline constexpr double calmUnderHeatBonus = 0.12;
inline constexpr double readsTelemetryEarlyBonus = 0.06;
inline constexpr double improvesEjectionOddsPerformanceBonus = 0.04;
inline constexpr double improvesEjectionOddsEscapeBonus = 0.16;
inline constexpr double hardRebootPerformanceBonus = 0.08;
inline constexpr double phaseShiftPerformanceBonus = 0.05;
} // namespace traits

namespace crew {
inline constexpr int maxStress = 100;
inline constexpr int stressPerStep = 14;
inline constexpr int maxStressSteps = 7;
inline constexpr int maxTraining = 10;
inline constexpr double effectiveTrainingPerformanceBonus = 0.055;
inline constexpr double navigationPenaltyPerStressStep = 0.022;
inline constexpr double abortRiskPerStressStep = 1.0 / static_cast<double>(maxStressSteps);
inline constexpr double escapeBonusPerTraining = 0.025;
} // namespace crew

namespace hangar {
inline constexpr double startingCredits = 0.0;
inline constexpr double minimumExpeditionCredits = 45.0;
inline constexpr int emergencyReplacementStress = 15;
inline constexpr int injuredCarryoverStress = 8;
inline constexpr int repairAmountCap = 35;
inline constexpr double repairBaseCost = 6.0;
inline constexpr double repairCostPerDamage = 0.42;
inline constexpr double operationCostGrowth = 1.35;
inline constexpr double operationUseSurcharge = 12.0;
inline constexpr double rerollBaseCost = 10.0;
inline constexpr double trainingBaseCost = 10.0;
inline constexpr int trainingBaseStress = 30;
inline constexpr int trainingMinimumStress = crew::stressPerStep + crew::stressPerStep / 2;
inline constexpr double restNoCrewBaseCost = 8.0;
inline constexpr double restBaseCost = 6.0;
inline constexpr double restCostPerStress = 0.06;
inline constexpr int restBaseRecovery = 24;
inline constexpr int restMinimumRecovery = 8;
inline constexpr double restDifficultyMinFactor = 0.45;
inline constexpr double restDifficultyMaxFactor = 0.95;
inline constexpr double recruitCost = 24.0;
inline constexpr double emergencyRecruitCost = 0.0;
inline constexpr int emergencyRecruitStress = 18;
inline constexpr int recruitStress = 8;
inline constexpr int recruitTrainingPenalty = 1;
} // namespace hangar

namespace launchProgression {
inline constexpr int maximumUpgradeRank = 3;
inline constexpr double lessonReward = 22.0;
// The first flight runs slowly and provides long, distinct warning windows so
// the player can read the lesson before making the turnaround decision.
inline constexpr double fuelSurveyPrepareFuelShare = 0.75;
inline constexpr double fuelSurveyTargetFuelShare = 0.50;
inline constexpr double fuelSurveyLateFuelShare = 0.30;
inline constexpr double fuelSurveyProgressRateScale = 0.55;
inline constexpr double fuelSurveySafetyBonus = 3.0;
inline constexpr int fuelSurveyLateStress = 5;
inline constexpr double baseFuelCapacity = 10.0;
inline constexpr double fuelPerTankRank = 5.0;
inline constexpr double moonTransitFuel = 10.0;
inline constexpr double moonCaptureFuel = 5.0;
inline constexpr double calibrationTargetShare = 0.50;
inline constexpr int moonRequiredUpgradeCount = 2;
inline constexpr int marsRequiredUpgradeCount = 2;
inline constexpr int jupiterRequiredUpgradeCount = 2;
} // namespace launchProgression

inline double escalatedHangarOpCost(double baseCost, int uses)
{
    const int safeUses = std::max(0, uses);
    return std::ceil(
        baseCost * std::pow(hangar::operationCostGrowth, static_cast<double>(safeUses)) +
        static_cast<double>(safeUses) * hangar::operationUseSurcharge);
}

inline double offerRerollCost(int rerollsThisExpedition)
{
    return hangar::rerollBaseCost * static_cast<double>(rerollsThisExpedition + 1);
}

namespace mission {
inline constexpr double unknownDestinationDifficulty = 0.25;
inline constexpr double unattemptedDifficulty = 0.50;
inline constexpr double failedAttemptDifficultyBase = 0.25;
inline constexpr double failedAttemptDifficultyFloor = 0.08;
inline constexpr double provenDifficultyBase = 0.14;
inline constexpr double provenDifficultyFloor = 0.05;
inline constexpr double defaultProvingTargetShare = 0.47;
inline constexpr double defaultProvingTargetMinimum = 1.15;
inline constexpr double launchConfigMinimumMultiplier = 1.05;
inline constexpr double launchConfigOverTargetAllowance = 1.50;
inline constexpr int readinessBaseRequired = 3;
inline constexpr int moonReadinessRequired = 3;
inline constexpr int readinessOverCap = 3;
inline constexpr double destroyedCreditPenalty = 30.0;
inline constexpr double openingProvingGoalMargin = 0.08;
inline constexpr double openingMoonConfidenceBase = 0.80;
inline constexpr double openingMoonConfidencePerformanceBaseline = 0.50;
inline constexpr double openingMoonConfidencePerformanceScale = 0.10;
inline constexpr double openingMoonConfidenceOverpreparedScale = 0.02;
inline constexpr double openingMoonConfidenceMinimum = 0.85;
inline constexpr double openingMoonConfidenceMaximum = 0.97;
inline constexpr double openingMoonArrivalMargin = 0.06;
} // namespace mission

namespace ark {
inline constexpr int startingFuelReserve = 4;
inline constexpr int hostileSystemFuelReserve = 8;
} // namespace ark

namespace unlocks {
struct BlueprintUnlock {
    int threshold;
    std::string_view key;
    std::string_view message;
};

inline constexpr BlueprintUnlock blueprintUnlocks[] = {
    {2, content::unlock::thermal, "Thermal systems unlocked."},
    {8, content::unlock::recovery, "Recovery hardware unlocked."},
    {12, content::unlock::deepSpace, "Deep-space module family unlocked."},
    {18, content::unlock::ai, "Predictive guidance unlocked."},
    {24, content::unlock::exotic, "Exotic prototype modules unlocked."}
};
} // namespace unlocks

namespace launch {
// Skill-based route model. Fuel is expressed in whole, player-facing units;
// 60% throttle is the calibrated consumption baseline.
inline constexpr double routeFuelBase = 5.0;
inline constexpr double routeFuelPerTier = 5.0;
inline constexpr double routeFuelMaximum = 20.0;
inline constexpr double calibratedThrottle = 0.60;
inline constexpr double fuelDistanceBaseMultiplier = 0.70;
inline constexpr double fuelDistanceThrottleMultiplier = 0.30;
inline constexpr double poweredVelocityResponse = 5.0;
inline constexpr double coastDecelerationPerSecond = 0.060;
inline constexpr double coastStopSpeed = 0.002;

inline constexpr double controlChaosRankZero = 1.00;
inline constexpr double controlChaosRankOne = 0.55;
inline constexpr double controlChaosRankTwo = 0.20;
inline constexpr double controlChaosRankThree = 0.00;
inline constexpr double controlRightOvershoot = 0.45;
inline constexpr double controlSteeringResponseVariance = 0.20;
inline constexpr double controlStartupDrift = 0.80;
inline constexpr double controlThrottleKick = 0.35;
inline constexpr double controlThrottleKickThreshold = 0.05;
inline constexpr double controlThrottleKickCooldown = 0.35;
inline constexpr double controlDampingMinimum = 0.55;
inline constexpr double controlDampingChaosRelief = 0.55;
inline constexpr double controlAutoTrimMinimum = 0.05;
inline constexpr double controlAutoTrimChaosRelief = 0.20;

inline constexpr double poweredHeatIdleInput = 0.010;
// At Mars, an uninterrupted 60% qualification burn crosses the visible
// warning and fails before returning home. Brief engine cuts are therefore
// the taught skill, while low throttle remains a cooler skilled alternative.
inline constexpr double poweredHeatThrottleInput = 0.280;
inline constexpr double poweredHeatHazardInput = 0.018;
inline constexpr double poweredHeatCoolingBase = 0.020;
inline constexpr double poweredHeatRankZeroMultiplier = 1.00;
inline constexpr double poweredHeatRankOneMultiplier = 0.88;
inline constexpr double poweredHeatRankTwoMultiplier = 0.76;
inline constexpr double poweredHeatRankThreeMultiplier = 0.64;
inline constexpr double engineOffCoolingRankZero = 0.10;
inline constexpr double engineOffCoolingRankOne = 0.14;
inline constexpr double engineOffCoolingRankTwo = 0.18;
inline constexpr double engineOffCoolingRankThree = 0.22;

inline constexpr double hullBaseIntegrity = 100.0;
inline constexpr double hullIntegrityPerRank = 25.0;
inline constexpr int asteroidCount = 10;
inline constexpr int asteroidRowCount = 5;
inline constexpr int asteroidLaneCount = 3;
inline constexpr double asteroidBeltStart = 0.46;
inline constexpr double asteroidBeltEnd = 0.82;
inline constexpr double asteroidLaneOffset = 0.62;
inline constexpr double asteroidBaseRadius = 0.10;
inline constexpr double asteroidMinimumScale = 0.75;
inline constexpr double asteroidMaximumScale = 1.25;
inline constexpr double asteroidShipRadius = 0.065;
// Route progress spans roughly sixteen times the visible course-offset scale.
// Matching that aspect ratio keeps swept collision circles round in screen
// space and leaves enough distance to steer between adjacent open lanes.
inline constexpr double asteroidRouteAxisScale = 16.0;
inline constexpr double asteroidInvulnerabilitySeconds = 0.75;
inline constexpr double asteroidImpactDamageBase = 40.0;
inline constexpr double hullImpactRankZeroMultiplier = 1.00;
inline constexpr double hullImpactRankOneMultiplier = 0.80;
inline constexpr double hullImpactRankTwoMultiplier = 0.65;
inline constexpr double hullImpactRankThreeMultiplier = 0.50;

inline constexpr int telemetrySampleCount = 12;
inline constexpr double warningCautionThreshold = 0.62;
inline constexpr double warningCriticalThreshold = 0.88;
inline constexpr double overburnMinimumDenominator = 0.20;
inline constexpr double overburnExponent = 2.65;
inline constexpr double overburnMaximumMultiplier = 8.0;
inline constexpr double baseTravelSpeedMultiplier = 2.625;
inline constexpr double maxFrameStepSeconds = 0.08;
inline constexpr double cruiseBaseRate = 0.016;
inline constexpr double cruiseTierScale = 0.0008;
inline constexpr double accelerationBaseRate = 0.00026;
inline constexpr double accelerationHazardScale = 0.00008;
inline constexpr double pilotingInitialThrottle = 0.60;
inline constexpr double pilotingThrottleChangePerSecond = 0.35;
inline constexpr double pilotingMinimumPoweredThrottle = 0.18;
inline constexpr double pilotingBaseProgressRate = 0.085;
inline constexpr double pilotingTierDurationScale = 0.080;
inline constexpr double pilotingHeatInitial = 0.18;
inline constexpr double pilotingCourseSafe = 0.35;
inline constexpr double pilotingCourseCaution = 0.65;
inline constexpr double pilotingCourseLost = 1.00;
inline constexpr double pilotingSteeringBase = 0.95;
inline constexpr double pilotingCutSteeringScale = 0.45;
inline constexpr double pilotingPoweredSteeringBase = 0.65;
inline constexpr double pilotingWarningThreshold = 0.70;
inline constexpr double pilotingCriticalThreshold = 0.90;
inline constexpr double pilotingFailureThreshold = 1.00;
inline constexpr double pilotingHeatFailureSeconds = 1.50;
inline constexpr double pilotingCourseFailureSeconds = 2.00;
inline constexpr double pilotingFuelFailureSeconds = 1.00;
inline constexpr double pilotingMaximumTravelProgress = 1.42;
} // namespace launch

namespace telemetry {
inline constexpr double heatMaximum = 1.25;
inline constexpr double stressHeatScale = 0.28;
inline constexpr double stressGuidanceScale = 0.18;
} // namespace telemetry

namespace session {
inline constexpr double minTravelDenominator = 0.10;
inline constexpr double maxTravelProgress = 1.42;
inline constexpr double preflightBoardingSeconds = 1.80;
inline constexpr double returnDefaultDuration = 2.40;
inline constexpr double returnBaseDuration = 2.10;
inline constexpr double returnDurationPerProgress = 1.40;
inline constexpr double returnDriftDurationMultiplier = 1.25;
inline constexpr double returnTurnSeconds = 1.15;
inline constexpr double launchShakeSeconds = 0.55;
inline constexpr double lunarImpactHoldSeconds = 0.08;
inline constexpr double lunarImpactExplosionEndSeconds = 0.92;
inline constexpr double lunarImpactSequenceSeconds = 0.95;
inline constexpr double arrivalFanfareSeconds = 2.50;
inline constexpr double returnTelemetryProgressDenominator = 0.10;
inline constexpr double returnTelemetryHeadroomMinimum = 0.04;
inline constexpr double returnTelemetryOvershootHeadroomScale = 0.22;
inline constexpr double returnTelemetryOvershootBase = 0.18;
inline constexpr double returnTelemetryOvershootExtraHeadroomScale = 0.10;
inline constexpr double returnTelemetrySettleHeadroomScale = 0.08;
inline constexpr double returnTelemetrySettleMaximum = 0.06;
inline constexpr double returnTelemetryCrashMargin = 0.02;
} // namespace session

namespace flyby {
inline constexpr double durationSeconds = 14.0;
inline constexpr double startX = -0.70;
inline constexpr double startY = -0.30;
inline constexpr double startVelocityX = 0.38;
inline constexpr double startVelocityY = 0.04;
inline constexpr double control1X = -0.18;
inline constexpr double control1Y = -0.38;
inline constexpr double control2X = 0.30;
inline constexpr double control2Y = 0.90;
inline constexpr double endX = 0.92;
inline constexpr double endY = 0.48;
inline constexpr double destinationX = 0.50;
inline constexpr double destinationY = 0.05;
inline constexpr double idealRadius = 0.50;
inline constexpr double perfectBand = 0.050;
inline constexpr double goodBand = 0.145;
inline constexpr double planetColliderBaseRadius = 0.13;
inline constexpr double planetColliderTierRadius = 0.012;
inline constexpr double planetColliderPadding = 0.012;
inline constexpr double shipColliderHalfLength = 0.055;
inline constexpr double shipColliderHalfWidth = 0.025;
inline constexpr double thrustAcceleration = 0.66;
// Flyby uses the same held throttle model as Launch: vertical input adjusts
// this retained setpoint, while releasing the input keeps the current burn.
inline constexpr double throttleChangePerSecond = 0.35;
inline constexpr double turnRateRadians = 1.45;
inline constexpr double sensorPerfectBandScale = 0.0025;
inline constexpr double sensorGoodBandScale = 0.0060;
inline constexpr double thrustControlScale = 0.018;
inline constexpr double escapeControlScale = 0.006;
inline constexpr double volatilityControlPenalty = 0.008;
inline constexpr double hullImpactReliefScale = 1.25;
inline constexpr double coolingImpactReliefScale = 0.55;
inline constexpr double escapeImpactReliefScale = 0.35;
inline constexpr int impactMaximumRelief = 12;
inline constexpr double driftDrag = 0.16;
inline constexpr double minSpeed = 0.16;
inline constexpr double maxSpeed = 0.82;
inline constexpr double boundaryPadding = 0.08;
inline constexpr double finishProgress = 0.985;
inline constexpr double minimumFinishSeconds = 4.0;
inline constexpr double gravityEasy = 0.006;
inline constexpr double gravityMedium = 0.014;
inline constexpr double gravityLarge = 0.030;
inline constexpr double gravityDeep = 0.046;
inline constexpr double gravitySoftening = 0.12;
inline constexpr double maxGravityAcceleration = 0.18;
inline constexpr double perfectTimeShare = 0.55;
inline constexpr double goodTimeShare = 0.45;
inline constexpr double perfectMaxMissStreak = 2.20;
inline constexpr double goodRewardFactor = 0.35;
inline constexpr double goodRewardFloor = 12.0;
inline constexpr double perfectRewardMultiplier = 1.25;
inline constexpr double completionRewardMaxScale = 1.60;
inline constexpr int goodBlueprintGain = 1;
inline constexpr double slingshotFuelBoost = 1.5;
inline constexpr double slingshotSpeedBoost = 0.20;
inline constexpr double slingshotMaxSpeedScale = 2.0;
inline constexpr double jupiterSlingshotFuelSavings = 5.0;
inline constexpr double jupiterSlingshotGoodInstabilityPenalty = 0.35;
inline constexpr int impactHullDamage = 18;
} // namespace flyby

namespace orbit {
inline constexpr double durationSeconds = 15.0;
inline constexpr double throttleChangePerSecond = 0.35;
inline constexpr double planetBaseRadius = 0.145;
inline constexpr double planetTierRadius = 0.016;
inline constexpr double targetRadiusScale = 2.95;
inline constexpr double goodBandScale = 0.55;
inline constexpr double perfectBandScale = 0.24;
inline constexpr double perfectHoldSeconds = 3.0;
inline constexpr double goodBandMinimumTimeShare = 0.60;
// Orbit insertion begins at the Flyby endpoint, measured from the destination
// center with the standard atan2(y, x) mathematical angle. With the authored
// path this is approximately 0.797 radians; calculating it here keeps both
// activities aligned when that path is retuned.
inline double flybyExitAngleRadians() noexcept
{
    return std::atan2(
        flyby::endY - flyby::destinationY,
        flyby::endX - flyby::destinationX);
}

// Negative math-space angular travel keeps the same clockwise screen-space
// motion as the preceding pass.
inline constexpr double direction = -1.0;
inline constexpr double thrustAcceleration = 0.075;
inline constexpr double gravitySoftening = 0.120;
inline constexpr double gravityScale = 0.42;
inline constexpr double driftDrag = 0.0;
inline constexpr double minSpeed = 0.18;
inline constexpr double maxSpeed = 0.48;
inline constexpr double escapeRadiusScale = 2.40;
inline constexpr double collisionPadding = 0.018;
// Orbit support is intentionally tied to the visible launch-upgrade tracks,
// not to hidden legacy module stats. Each track has one concrete benefit.
inline constexpr double fuelDurationAssistPerRank = 0.60;
inline constexpr double flightControlsThrustAssistPerRank = 0.10;
inline constexpr double coolingThrustAssistPerRank = 0.05;
inline constexpr double hullCollisionPaddingReliefPerRank = 0.0025;
inline constexpr double minimumCollisionPadding = 0.006;
inline constexpr int goodBlueprintGain = 1;
inline constexpr int perfectBlueprintGain = 2;
inline constexpr double goodRewardFactor = 0.55;
inline constexpr double goodRewardFloor = 18.0;
inline constexpr double perfectRewardMultiplier = 1.45;
} // namespace orbit

namespace rewards {
inline constexpr double provingPayoutPerExtraData = 0.20;
inline constexpr double provingPayoutBonusMaximum = 0.60;
inline constexpr double returnHomeBasePayoutFactor = 0.74;
inline constexpr double returnHomeReachedGoalFactor = 1.18;
inline constexpr double transferArrivalPayoutFactor = 1.45;
inline constexpr double fullProfileRewardFloor = 1.00;
inline constexpr double pushedProfileShelfShare = 0.45;
inline constexpr double shallowRecoveryTargetShare = 0.25;
inline constexpr double shallowRecoveryPenaltyBase = 15.0;
inline constexpr int shallowRecoveryPenaltyMaxExponent = 6;
inline constexpr double shallowRecoveryPenaltyMaximum = 30.0;
inline constexpr double cleanShallowRecoveryWarningThreshold = 0.62;
inline constexpr int cleanShallowRecoveryDestructionStreak = 3;
} // namespace rewards

namespace research {
inline constexpr double unmappedDescentHazardPenalty = 0.20;
inline constexpr int firstResearchTier = 2;
inline constexpr int prospectorCommonOreGoal = 30;
inline constexpr int marsBayCommonOreGoal = 40;
inline constexpr int enemyEncounterTier = 4;
inline constexpr int offerCount = 3;
inline constexpr int baseSupply = 7;
inline constexpr int supplyPerTier = 1;
inline constexpr double expeditionRigPackFuel = 3.0;
inline constexpr double legacySharedFuelCapacity = 30.0;
inline constexpr int surveySupplyCost = 1;
inline constexpr int mineSupplyCost = 2;
inline constexpr int pushSupplyCost = 2;
inline constexpr int surveyCommonGain = 1;
inline constexpr int mineCommonGain = 2;
inline constexpr int mineRareDepthThreshold = 1;
inline constexpr int artifactDepthThreshold = 2;
inline constexpr int probeSupplyBonus = 1;
inline constexpr int probeSurveyCommonBonus = 1;
inline constexpr int drillMineCommonBonus = 1;
inline constexpr double drillRareChanceBonus = 0.18;
inline constexpr double baseHazard = 0.12;
inline constexpr double hazardPerTier = 0.035;
inline constexpr double hazardPerDepth = 0.055;
inline constexpr double surveyHazardChanceScale = 0.16;
inline constexpr double mineHazardChanceScale = 0.22;
inline constexpr double pushHazardChanceScale = 0.34;
inline constexpr double probeHazardRelief = 0.06;
inline constexpr double drillHazardRelief = 0.08;
inline constexpr double cargoRigHazardRelief = 0.05;
inline constexpr double surfaceHazardChanceMinimum = 0.02;
inline constexpr double surfaceHazardChanceMaximum = 0.58;
inline constexpr double dustHazardIncrease = 0.020;
inline constexpr double drillHazardIncrease = 0.030;
inline constexpr double unstableTerrainHazardIncrease = 0.045;
inline constexpr int dustHazardSupplyLoss = 1;
inline constexpr int drillHazardCargoLoss = 1;
inline constexpr int pushHazardSupplyLoss = 1;
inline constexpr int siteSurveyBasinSurveyBonus = 1;
inline constexpr int siteOreShelfMineBonus = 1;
inline constexpr double siteOreShelfRareChanceBonus = 0.10;
inline constexpr double siteFractureFieldArtifactChanceBonus = 0.20;
inline constexpr double siteSurveyBasinHazardRelief = 0.025;
inline constexpr double siteOreShelfHazardIncrease = 0.015;
inline constexpr double siteFractureFieldHazardIncrease = 0.045;
inline constexpr double siteFractureFieldExtractionRiskIncrease = 0.04;
inline constexpr double artifactChanceBase = 0.45;
inline constexpr double surfaceEventChanceBase = 0.14;
inline constexpr double surfaceEventChanceHazardScale = 0.12;
inline constexpr double surfaceEventChanceMaximum = 0.42;
inline constexpr double surfaceEnemyChanceBase = 0.10;
inline constexpr double surfaceEnemyChanceHazardScale = 0.18;
inline constexpr double surfaceEnemyChanceMaximum = 0.36;
inline constexpr double perimeterDroneEnemyRelief = 0.12;
inline constexpr int surfaceEnemySupplyLoss = 1;
inline constexpr int surfaceEnemyCargoLoss = 1;
inline constexpr double surfaceEnemyHazardIncrease = 0.030;
inline constexpr double surfaceEquipmentFailureShare = 0.30;
inline constexpr double surfaceToolFailureRelief = 0.08;
inline constexpr double surfaceEquipmentFailureMinimumShare = 0.12;
inline constexpr double surfaceUnexpectedDepositShare = 0.46;
inline constexpr int surfaceEquipmentFailureSupplyLoss = 1;
inline constexpr double surfaceEquipmentFailureHazardIncrease = 0.020;
inline constexpr int surfaceDepositCommonGain = 1;
inline constexpr double surfaceDepositRareChance = 0.25;
inline constexpr int surfaceCrewDiscoveryBlueprintGain = 1;
inline constexpr int analysisLabBlueprintBonus = 1;
inline constexpr int artifactInsightBlueprintPerIdentified = 1;
inline constexpr int artifactInsightBlueprintMaximum = 3;
inline constexpr int surfaceLogEntryLimit = 5;
inline constexpr int pushMaxSteps = 4;
inline constexpr int scanMaxPulses = pushMaxSteps + 1;
inline constexpr double scanSweepRadiansPerSecond = 2.70;
inline constexpr double scanWindowCenterRadians = 1.57079632679489661923;
inline constexpr double scanGoodWindowHalfAngleRadians = 0.42;
inline constexpr double scanPerfectWindowHalfAngleRadians = 0.13;
// Each newly mapped layer tightens the next pulse window. A miss retries the
// same layer, so it does not make the timing window any smaller.
inline constexpr double scanWindowDepthScale = 0.84;
inline constexpr double scanGoodWindowMinimumHalfAngleRadians = 0.16;
inline constexpr double scanPerfectWindowMinimumHalfAngleRadians = 0.05;
inline double surfaceScanGoodWindowHalfAngleForDepth(int depthOffset) noexcept
{
    const double scale = std::pow(scanWindowDepthScale, std::max(0, depthOffset));
    return std::max(scanGoodWindowMinimumHalfAngleRadians, scanGoodWindowHalfAngleRadians * scale);
}
inline double surfaceScanPerfectWindowHalfAngleForDepth(int depthOffset) noexcept
{
    const double scale = std::pow(scanWindowDepthScale, std::max(0, depthOffset));
    return std::max(scanPerfectWindowMinimumHalfAngleRadians, scanPerfectWindowHalfAngleRadians * scale);
}
inline constexpr int scanGoodInformationPercent = 80;
inline constexpr int scanPerfectInformationPercent = 100;
inline constexpr double scanGoodSuccessFanfareSeconds = 0.70;
inline constexpr double scanPerfectSuccessFanfareSeconds = 1.05;
inline constexpr double scanMissFanfareSeconds = 0.48;
inline double surfaceScanSweepAngleRadians(double elapsedSeconds) noexcept
{
    constexpr double twoPi = 6.28318530717958647692;
    return std::fmod(std::max(0.0, elapsedSeconds) * scanSweepRadiansPerSecond, twoPi);
}
inline constexpr double scanBaseBustRisk = 0.04;
inline constexpr double scanBustRiskPerPulse = 0.055;
inline constexpr double scanBustRiskHazardScale = 0.16;
inline constexpr double scanSignalPerPulse = 0.18;
inline constexpr double scanHazardPerPulse = 0.006;
inline constexpr double scanBustHazardIncrease = 0.035;
inline constexpr double pushBaseCollapseRisk = 0.07;
inline constexpr double pushRiskPerStep = 0.085;
inline constexpr double pushRiskHazardScale = 0.18;
inline constexpr double pushHazardPerStep = 0.030;
inline constexpr double pushCollapseHazardIncrease = 0.060;
} // namespace research

namespace mining {
inline constexpr int terrainWidth = 64;
inline constexpr int terrainHeight = 40;
inline constexpr int chunkSize = 8;
inline constexpr double oxygenSeconds = 30.0;
inline constexpr double ioArtifactOxygenSeconds = 60.0;
inline constexpr double maximumOxygenSeconds = 120.0;
inline constexpr double oxygenPocketRestoreSeconds = 10.0;
inline constexpr double fuelCycleProgressPerSecond = 1.0 / 15.0;
// Presentation reserve for a deliberate return through generated terrain.
// Ideal straight-line speed badly understates acceleration, gravity, steering,
// and the time needed to find each layer transition.
inline constexpr double returnEnduranceTraversalScale = 2.40;
inline constexpr double returnEnduranceDockingSeconds = 2.0;
inline constexpr int returnEnduranceCautionSeconds = 8;
inline constexpr double targetRunSeconds = 120.0;
inline constexpr double droneSpeedCellsPerSecond = 7.2;
inline constexpr double rigAccelerationCellsPerSecondSquared = 14.0;
inline constexpr double rigBrakingCellsPerSecondSquared = 20.0;
inline constexpr double rigColliderRadiusCells = 0.48;
inline constexpr double operatorSpeedCellsPerSecond = 4.6;
inline constexpr double operatorAccelerationCellsPerSecondSquared = 28.0;
inline constexpr double operatorBrakingCellsPerSecondSquared = 24.0;
inline constexpr double operatorColliderRadiusCells = 0.25;
inline constexpr double baseGravityCellsPerSecondSquared = 6.0;
inline constexpr double operatorEntryDistanceCells = 2.50;
inline constexpr double operatorToggleHoldSeconds = 0.60;
inline constexpr double operatorSafeExitSearchRadiusCells = 2.25;
inline constexpr double operatorIntegrityRepairCommonCost = 3.0;
inline constexpr double operatorDrillRangeCells = 1.2;
inline constexpr double operatorDrillPowerScale = 0.45;
inline constexpr double operatorSidearmDamage = 2.4;
inline constexpr double operatorSidearmRangeCells = 8.0;
inline constexpr double operatorSidearmIntervalSeconds = 0.18;
inline constexpr double operatorSidearmTerrainOutputScale = 0.30;
inline constexpr double operatorArtifactMinimumSpeedMultiplier = 0.55;
inline constexpr double operatorArtifactFreeBuffer = 0.0;
inline constexpr double softTerrainMoveScale = 0.42;
inline constexpr double hardTerrainBounceImpulse = 54.0;
inline constexpr double hardTerrainBounceCooldownSeconds = 0.30;
inline constexpr double contactBounceSpring = 58.0;
inline constexpr double contactBounceDamping = 0.68;
inline constexpr double contactBounceMaxCells = 2.24;
inline constexpr double contactIndicatorSeconds = 0.42;
inline constexpr double postContactMinSpeedScale = 0.55;
inline constexpr double postContactSpeedRecoverySeconds = 0.55;
inline constexpr int drillRepairCommonAtFullDamage = 4;
inline constexpr int droneRepairCommonAtFullDamage = 6;
inline constexpr double repairDamageEpsilon = 0.001;
inline constexpr double passiveLightRadius = 2.15;
inline constexpr double drillRangeCells = 2.05;
inline constexpr double drillAimDeadzoneCells = 0.75;
inline constexpr int drillAimDirections = 8;
inline constexpr double visualHeadingSlerpPerSecond = 5.0;
inline constexpr double visualRecoilSmoothingPerSecond = 12.0;
inline constexpr double upgradedVisualRecoilSmoothingPerSecond = 9.0;
inline constexpr double baseDrillPower = 4.2;
inline constexpr double denseMaterialDrillPowerScale = 1.45;
inline constexpr double contactDrillPowerScale = 1.20;
inline constexpr double trainingDrillPowerScale = 0.10;
inline constexpr double surfaceDrillPowerBonus = 1.15;
inline constexpr double prairieDogDrillBonus = 0.90;
inline constexpr double beaverIntegrityRelief = 0.18;
inline constexpr double chipmunkSpeedBonus = 1.30;
inline constexpr double capybaraOxygenBonusSeconds = 18.0;
inline constexpr double foxExtractionRiskRelief = 0.025;
inline constexpr double squirrelRareYieldChance = 0.20;
inline constexpr double heatRisePerSecond = 0.10;
inline constexpr double heatHardRockBonus = 0.08;
inline constexpr double heatCoolingPerSecond = 0.16;
inline constexpr double heatCoolingMultiplier = 2.0;
inline constexpr double drillHeatCautionThreshold = 0.60;
inline constexpr double drillHeatCriticalThreshold = 0.80;
inline constexpr double drillHeatFlashThreshold = 1.0;
inline constexpr double heatSlowThreshold = 0.72;
inline constexpr double heatDamageThreshold = 0.90;
inline constexpr double overheatedDrillSlow = 0.48;
inline constexpr double overheatIntegrityDamagePerSecond = 0.055;
inline constexpr double depthHazardRisk = 0.030;
inline constexpr double maxMiningHazardDelta = 0.26;
inline constexpr double returnZoneHorizontalFraction = 0.29;
inline constexpr double returnZoneRadiusCells = 3.0;
inline constexpr double baseCarryBufferCargo = 3.0;
inline constexpr double tetheredArtifactCargoWeight = 4.0;
inline constexpr double rigTetherPullAccelerationCellsPerSecondSquared = 11.0;
inline constexpr double rigTetherDamping = 1.65;
inline constexpr double rigTetherRestLengthCells = 1.5;
inline constexpr double operatorRigTetherRangeCells = 6.8;
inline constexpr double operatorRigTetherRestLengthCells = 1.35;
inline constexpr double operatorRigTetherSpring = 8.0;
inline constexpr double operatorRigTetherDamping = 2.4;
inline constexpr double loadSpeedPenaltyPerCargo = 0.055;
inline constexpr double loadFuelPenaltyPerCargo = 0.050;
inline constexpr double minLoadedSpeedMultiplier = 0.45;
inline constexpr double maxLoadedFuelMultiplier = 2.0;
inline constexpr double oxygenDroneDamagePerSecond = 0.055;
inline constexpr double emergencyRecallHazardPenalty = 0.20;
inline constexpr double miningExtractionSequenceSeconds = 3.40;
inline constexpr double scannerRevealRadius = 5.5;
inline constexpr double scannerProbeBonus = 2.0;
inline constexpr double scannerCooldownSeconds = 4.0;
inline constexpr double scannerPulseSeconds = 0.64;
inline double scannerRechargePresentationProgress(double cooldownRemaining) noexcept
{
    const double elapsed = scannerCooldownSeconds
        - std::clamp(cooldownRemaining, 0.0, scannerCooldownSeconds);
    return std::clamp(
        (elapsed - scannerPulseSeconds) / (scannerCooldownSeconds - scannerPulseSeconds),
        0.0,
        1.0);
}
inline constexpr double regolithToughness = 2.1;
inline constexpr double hardRockToughness = 6.8;
inline constexpr double commonOreToughness = 3.9;
inline constexpr double rareOreToughness = 5.4;
inline constexpr double exoticVeinToughness = 6.8;
inline constexpr double artifactCacheToughness = 7.4;
inline constexpr double bedrockToughness = 10000.0;
inline constexpr int commonCargo = 1;
inline constexpr int rareCargo = 2;
inline constexpr int exoticCargo = 4;
inline constexpr int artifactCargo = 3;
inline constexpr double artifactBaseSpawnChance = 0.10;
inline constexpr double artifactMaxSpawnChance = 0.28;
inline constexpr double artifactMaxHealth = 1.0;
inline constexpr double artifactDrillDamagePerSecond = 0.22;
inline constexpr double artifactTetherRangeCells = 6.8;
inline constexpr double artifactTetherRestLengthCells = 1.70;
inline constexpr double artifactTetherPullPerSecond = 0.72;
inline constexpr double artifactTetherSpring = 10.5;
inline constexpr double artifactTetherDamping = 4.2;
inline constexpr double artifactImpactDamageScale = 0.055;
inline constexpr double artifactImpactDamageThreshold = 1.45;
inline constexpr double artifactDropDamageThreshold = 2.20;
inline constexpr double artifactDeliveryRadiusCells = 1.45;
inline constexpr double artifactShipBayY = 2.65;
inline constexpr int artifactStoryArkRepair = 1;
inline constexpr int artifactStoryHullRepair = 8;
inline constexpr double artifactCreditReward = 35.0;
inline constexpr int artifactFuelReward = 2;
inline constexpr int artifactBlueprintReward = 2;
inline constexpr int maxActiveEnemies = 14;
inline constexpr double baseDefenseDamagePerSecond = 0.75;
inline constexpr double defenseRangeCells = 8.0;
inline constexpr double alliedShotIntervalSeconds = 0.36;
inline constexpr double alliedCritChance = 0.18;
inline constexpr double alliedCritMultiplier = 1.85;
inline constexpr double alliedCritChanceMaximum = 0.42;
inline constexpr double alliedFireRateBonusMaximum = 0.60;
inline constexpr int alliedSentryVolleyMaximum = 2;
inline constexpr double miniDroneTravelSpeedCellsPerSecond = 4.8;
inline constexpr double miniDroneReturnSpeedCellsPerSecond = 5.6;
inline constexpr double miniDroneCatchUpSpeedCellsPerSecond = 8.5;
inline constexpr double miniDroneCatchUpDistanceCells = 4.5;
inline constexpr double miniDroneOrbitRadiansPerSecond = 0.45;
inline constexpr double miniDroneAnchorVelocityLeadSeconds = 0.18;
inline constexpr double miniDroneMiningOrbitRadiusCells = 1.60;
inline constexpr double miniDroneResourceOrbitRadiusCells = 2.05;
inline constexpr double miniDroneHazardOrbitRadiusCells = 1.90;
inline constexpr double miniDroneDefenseOrbitRadiusCells = 2.70;
inline constexpr double miniDroneAttackOrbitRadiusCells = 3.35;
inline constexpr double miniDroneSurveyOrbitRadiusCells = 3.40;
inline constexpr double miniDroneColliderRadiusCells = 0.16;
inline constexpr double miniDroneSeparationRadiusCells = 0.38;
inline constexpr double miniDroneHomeRadiusCells = 0.35;
inline constexpr double miniDroneBrakeRadiusCells = 1.15;
inline constexpr double miniDroneVelocityResponsePerSecond = 10.0;
inline constexpr double miniDroneFormationResponsePerSecond = 6.0;
inline constexpr double miniDroneTaskStopDampingPerSecond = 11.0;
inline constexpr double miniDroneStopSpeedCellsPerSecond = 0.06;
inline constexpr double miniDroneSameRoleSpacingCells = 0.70;
inline constexpr double miningDroneAcquireRadiusCells = 7.0;
inline constexpr double miningDroneLeashRadiusCells = 8.5;
inline constexpr double miningDroneReacquireRadiusCells = 4.0;
inline constexpr double miningDroneWorkRangeCells = 0.72;
inline constexpr double miningDroneReturnPathFailureSeconds = 1.25;
inline constexpr double miningDroneBaseHarvestRatePerSecond = 0.12;
inline constexpr double miningDroneUpgradeRateBonus = 0.30;
inline constexpr int miningDroneBaseCapacityChunks = 3;
inline constexpr int miningDroneCapacityChunksPerUpgrade = 2;
inline constexpr double miningDroneDropoffSeconds = 0.55;
inline constexpr int miningDroneCapacityChunks(int upgradeLevel)
{
    return miningDroneBaseCapacityChunks +
        (std::clamp(upgradeLevel, 1, 3) - 1) * miningDroneCapacityChunksPerUpgrade;
}
inline constexpr double miningDroneMaterialWorkScale(MiningCellMaterial material)
{
    switch (material) {
    case MiningCellMaterial::Regolith:
        return 0.65;
    case MiningCellMaterial::HardRock:
        return 1.35;
    case MiningCellMaterial::RareOre:
        return 1.15;
    case MiningCellMaterial::ExoticVein:
        return 1.30;
    default:
        return 1.0;
    }
}
inline constexpr double miningDroneWorkSeconds(int upgradeLevel, MiningCellMaterial material)
{
    const double rate = miningDroneBaseHarvestRatePerSecond *
        (1.0 + static_cast<double>(std::clamp(upgradeLevel, 1, 3) - 1) * miningDroneUpgradeRateBonus);
    return miningDroneMaterialWorkScale(material) / rate;
}
inline constexpr double attackDroneStandoffCells = 2.4;
inline constexpr double attackDroneFieldOfViewCells = 8.0;
inline constexpr double attackDroneShotStaggerSeconds = 0.08;
inline constexpr double attackDroneHomeRadiusCells = 3.35;
inline constexpr double attackDroneRigClearanceCells = 2.95;
inline constexpr double attackDroneHomeMinimumSpacingCells = 1.80;
inline constexpr double defenseDroneGuardDistanceCells = 2.70;
inline constexpr double defenseDroneShieldArcOffsetCells = 0.68;
inline constexpr double defenseDroneShieldArcRadians = 1.117010721276371;
inline constexpr double defenseDroneBaseShieldHitPoints = 0.12;
inline constexpr double defenseDroneShieldHitPointsPerUpgrade = 0.06;
inline constexpr double defenseDroneBaseRechargeSeconds = 5.40;
inline constexpr double defenseDroneRechargeRatePerUpgrade = 0.35;
inline constexpr double defenseDroneBaseTrackingSlerpPerSecond = 0.65;
inline constexpr double defenseDroneTrackingSlerpPerUpgrade = 0.25;
inline constexpr double defenseDroneShieldImpactPulseSeconds = 0.34;
inline constexpr double defenseDroneShieldHitPoints(int upgradeLevel)
{
    return defenseDroneBaseShieldHitPoints +
        static_cast<double>(std::clamp(upgradeLevel, 1, 3) - 1) * defenseDroneShieldHitPointsPerUpgrade;
}
inline constexpr double defenseDroneRechargeSeconds(int upgradeLevel)
{
    return defenseDroneBaseRechargeSeconds /
        (1.0 + static_cast<double>(std::clamp(upgradeLevel, 1, 3) - 1) * defenseDroneRechargeRatePerUpgrade);
}
inline constexpr double defenseDroneTrackingSlerpPerSecond(int upgradeLevel)
{
    return defenseDroneBaseTrackingSlerpPerSecond +
        static_cast<double>(std::clamp(upgradeLevel, 1, 3) - 1) * defenseDroneTrackingSlerpPerUpgrade;
}
inline constexpr double resourceDroneCollectionRadiusCells = 2.05;
inline constexpr double resourceDroneMinimumSpacingCells = 1.60;
inline constexpr double resourceDroneCollectionEnterToleranceCells = 0.82;
inline constexpr double resourceDroneCollectionExitToleranceCells = 1.18;
inline constexpr int resourceDroneCapacityChunks = 8;
inline constexpr double resourceDroneTransferSeconds = 0.62;
inline constexpr double resourceDroneUpgradeRateBonus = 0.42;
inline constexpr double resourceDroneDockSpacingCells = 0.85;
inline constexpr double surveyDroneLeadDistanceCells = 3.40;
inline constexpr double surveyDroneFormationSpacingCells = 2.40;
inline constexpr double surveyDroneFormationArcDepthPerCell = 0.08;
inline constexpr double surveyDroneSearchLaneHalfWidthCells = 1.35;
inline constexpr double surveyDroneMaximumFormationHalfWidthCells = 8.5;
inline constexpr double surveyDroneTravelSpeedCellsPerSecond = 2.60;
inline constexpr double surveyDroneReturnSpeedCellsPerSecond = 2.85;
inline constexpr double surveyDroneVelocityResponsePerSecond = 3.25;
inline constexpr double surveyDroneMinimumLeadCells = 2.0;
inline constexpr double surveyDroneMaximumLeadCells = 11.0;
inline constexpr double surveyDroneAnchorHalfWidthCells = 5.0;
inline constexpr double surveyDroneScanRadiusCells = 2.35;
inline constexpr double surveyDroneScanArrivalRadiusCells = 0.65;
inline constexpr double surveyDroneScanDwellSeconds = 0.45;
inline constexpr double surveyDronePulseSeconds = 0.55;
inline constexpr double surveyDroneRechargeSeconds = 3.20;
inline constexpr double surveyDroneFormationOffsetCells(int roleIndex, int roleCount)
{
    const int count = std::max(1, roleCount);
    const double centeredIndex = static_cast<double>(roleIndex) - static_cast<double>(count - 1) * 0.5;
    return centeredIndex * surveyDroneFormationSpacingCells;
}
inline constexpr double surveyDroneFormationHalfWidthCells(int roleCount)
{
    const double firstOffset = surveyDroneFormationOffsetCells(0, std::max(1, roleCount));
    const double outerOffset = firstOffset < 0.0 ? -firstOffset : firstOffset;
    return std::min(
        surveyDroneMaximumFormationHalfWidthCells,
        std::max(surveyDroneAnchorHalfWidthCells, outerOffset + surveyDroneSearchLaneHalfWidthCells));
}
inline constexpr double hazardDroneAcquireRadiusCells = 8.35;
inline constexpr double hazardDroneWorkRangeCells = 0.75;
inline constexpr double hazardDroneHomeOffsetCells = 1.80;
inline constexpr int hazardDroneRequiredMark(MiningElementalAffinity affinity)
{
    switch (affinity) {
    case MiningElementalAffinity::Thermal:
    case MiningElementalAffinity::Cryo:
    case MiningElementalAffinity::None:
        return 1;
    case MiningElementalAffinity::Toxic:
        return 2;
    case MiningElementalAffinity::Radiation:
        return 3;
    }
    return 3;
}
inline constexpr double hazardDroneTreatmentSeconds(int upgradeLevel)
{
    switch (std::clamp(upgradeLevel, 1, 3)) {
    case 1:
        return 1.5;
    case 2:
        return 1.125;
    case 3:
        return 0.75;
    }
    return 3.0;
}
inline constexpr int hazardDroneBatchSize(int upgradeLevel)
{
    return std::clamp(upgradeLevel, 1, 3);
}
inline constexpr double hazardDroneRefinementChance(int upgradeLevel)
{
    switch (std::clamp(upgradeLevel, 1, 3)) {
    case 1:
        return 0.05;
    case 2:
        return 0.08;
    case 3:
        return 0.12;
    }
    return 0.05;
}
inline constexpr double areaControlPulseSeconds = 0.48;
inline constexpr double enemyContactRadiusCells = 0.82;
inline constexpr double enemyElementalRadiusCells = 1.85;
inline constexpr double enemyDamageScale = 0.030;
inline constexpr double enemyMeleeAttackIntervalSeconds = 0.72;
inline constexpr double enemyRangedAttackIntervalSeconds = 0.95;
inline constexpr double enemyRangedAttackRangeCells = 7.0;
inline constexpr double enemyRangedStandoffCells = 3.2;
inline constexpr double enemyAttackAnimationSeconds = 4.0 / 12.0;
inline constexpr double enemyHitAnimationSeconds = 4.0 / 16.0;
inline constexpr double enemyDefeatAnimationSeconds = 4.0 / 10.0;
inline constexpr double enemyCritChance = 0.08;
inline constexpr double enemyCritMultiplier = 1.60;
inline constexpr double areaControlRangeCells = 5.4;
inline constexpr double projectileLifetimeSeconds = 0.34;
inline constexpr double damageNumberLifetimeSeconds = 0.92;
inline constexpr int maxCombatProjectiles = 48;
inline constexpr int maxDamageNumbers = 34;
inline constexpr double flyingDartStrength = 0.62;
inline constexpr double flyingDartFrequency = 8.5;
inline constexpr double mammalBurrowPower = 9.5;
inline constexpr double elementalHeatRisePerSecond = 0.32;
inline constexpr double elementalThermalHullDamagePerSecond = 0.045;
inline constexpr double elementalRadiationHazardPerSecond = 0.010;
inline constexpr double elementalToxicIntegrityDamagePerSecond = 0.010;
inline constexpr double elementalCryoSlowDurationSeconds = 0.55;
inline constexpr double elementalCryoSlowScale = 0.58;
inline constexpr double hazardPocketExposureRadiusCells = 1.75;
inline constexpr double hazardPocketPeripheralExposureFloor = 0.65;
inline constexpr double minibossHealthScale = 1.85;
inline constexpr double bossHealthScale = 2.65;
inline constexpr double roomEnemyHealthScale = 1.25;
inline constexpr double enemySpawnerArmor = 0.12;
inline constexpr double enemySpawnerSpawnRadiusCells = 1.65;
// Swarm Nests are horde set-pieces, separate from the smaller procedural
// encounter cap. The current debug nest (Act 2 Combine) holds 32 at once;
// late Act 3 scales to 64 while remaining below genre peers' triple-digit caps.
inline constexpr int swarmBaseConcurrentEnemies = 24;
inline constexpr int swarmBandConcurrentStep = 8;
inline constexpr int swarmActThreeConcurrentBonus = 16;
inline constexpr double swarmSpawnIntervalSeconds = 0.12;
inline constexpr double swarmEnemyHealthScale = 0.45;
inline constexpr double swarmEnemyDamageScale = 0.20;
inline constexpr int swarmChamberHalfWidthCells = 11;
inline constexpr int swarmChamberHalfHeightCells = 8;
inline constexpr double swarmOffscreenSpawnMarginCells = 1.5;
inline constexpr double swarmIngressSpeedScale = 4.25;
inline constexpr double swarmFlyingSpeedScale = 0.25;
inline constexpr double swarmOrbitRadiansPerSecond = 0.34;
inline constexpr double swarmVerticalRingScale = 0.72;
inline constexpr double swarmMeleeHoldingRadiusCells = 2.15;
inline constexpr double swarmMeleeRetreatRadiusCells = 3.35;
inline constexpr double swarmMeleeDiveRadiusCells = 0.48;
inline constexpr double swarmMeleeDiveCycleSeconds = 1.40;
inline constexpr double swarmMeleeDiveWindowSeconds = 0.30;
inline constexpr double swarmMeleeAttackIntervalSeconds = 1.05;
inline constexpr double swarmMeleeRetreatThresholdSeconds = 0.38;
inline constexpr double swarmRangedFiringRadiusCells = 4.35;
inline constexpr double swarmRangedRetreatRadiusCells = 5.90;
inline constexpr double swarmRangedAttackIntervalSeconds = 1.35;
inline constexpr double swarmRangedRetreatThresholdSeconds = 0.58;
} // namespace mining

namespace outcomes {
inline constexpr double vehicleLossSurvivalBase = 0.22;
inline constexpr double survivalEscapeScale = 0.07;
inline constexpr double survivalHazardScale = 0.035;
inline constexpr double survivalMinimum = 0.05;
inline constexpr double survivalMaximum = 0.90;
inline constexpr double vehicleLossInjuryChance = 0.58;

inline constexpr double returnProfileDepthMaximum = 1.80;
inline constexpr double returnSystemsHullRelief = 0.018;
inline constexpr double returnSystemsCoolingRelief = 0.018;
inline constexpr double returnSystemsFuelRelief = 0.012;
inline constexpr double returnSystemsSensorsRelief = 0.010;
inline constexpr double returnTransferBasePenalty = 0.08;
inline constexpr double returnTransferTierPenalty = 0.015;
inline constexpr double returnRiskBase = 0.022;
inline constexpr double returnRiskHazardScale = 0.022;
inline constexpr double returnRiskProfileDepthScale = 0.060;
inline constexpr double returnRiskWarningScale = 0.105;
inline constexpr double returnRiskHeatScale = 0.050;
inline constexpr double returnRiskDamageScale = 0.0014;
inline constexpr double returnRiskMinimum = 0.01;
inline constexpr double returnRiskMaximum = 0.42;

inline constexpr double payoutStatScale = 0.045;

inline constexpr double transferArrivalDamageHazardScale = 5.8;
inline constexpr double transferArrivalDamageBurnScale = 1.9;
inline constexpr double transferArrivalDamageStressScale = 8.0;
inline constexpr double transferArrivalDamageHullRelief = 0.72;
inline constexpr double transferArrivalDamageCoolingRelief = 0.54;
inline constexpr int transferArrivalDamageMinimum = 4;
inline constexpr int transferArrivalDamageMaximum = 32;

inline constexpr double returnHomeRecoveryBase = 3.0;
inline constexpr double returnHomeRecoveryTierScale = 2.0;
inline constexpr double returnHomeRecoveryBurnScale = 1.5;
inline constexpr double returnHomeRecoveryMinimum = 2.0;
inline constexpr double returnHomeRecoveryMaximum = 28.0;
inline constexpr double returnHomeDamageHazardScale = 4.5;
inline constexpr double returnHomeDamageBurnScale = 1.7;
inline constexpr double returnHomeDamageStressScale = 8.0;
inline constexpr double returnHomeDamageHullRelief = 0.70;
inline constexpr double returnHomeDamageCoolingRelief = 0.58;
inline constexpr int returnHomeDamageMinimum = 0;
inline constexpr int returnHomeDamageEarlyMaximum = 16;
inline constexpr int returnHomeDamageCompleteMaximum = 26;
inline constexpr double returnHomeBlueprintTargetShare = 0.75;

inline constexpr double transferUsefulDataTargetShare = 0.55;
inline constexpr double returnUsefulDataTargetShare = 0.70;
} // namespace outcomes

namespace stress {
inline constexpr double warningStressStart = 0.55;
inline constexpr double warningStressRange = 0.45;
inline constexpr double abortStressStart = 0.35;
inline constexpr double abortStressRange = 0.65;
inline constexpr double warningStressScale = 8.0;
inline constexpr double abortStressScale = 8.0;
inline constexpr int destroyedLaunchStress = 34;
inline constexpr int survivedLaunchStress = 12;
} // namespace stress

namespace damage {
inline constexpr double hullPenaltyPerDamage = 2.2;
inline constexpr double coolingPenaltyPerDamage = 1.2;
inline constexpr double escapePenaltyPerDamage = 0.8;
inline constexpr int destroyedShipDamage = 100;
inline constexpr double moduleLossChance = 0.62;
} // namespace damage

} // namespace rocket::tuning
