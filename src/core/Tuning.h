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
inline constexpr double startingCredits = 100.0;
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
    {4, content::unlock::thermal, "Thermal systems unlocked."},
    {8, content::unlock::recovery, "Recovery hardware unlocked."},
    {12, content::unlock::deepSpace, "Deep-space module family unlocked."},
    {18, content::unlock::ai, "Predictive guidance unlocked."},
    {24, content::unlock::exotic, "Exotic prototype modules unlocked."}
};
} // namespace unlocks

namespace launch {
struct PerformanceWeights {
    double thrust;
    double fuel;
    double hull;
    double cooling;
    double sensors;
};

inline constexpr PerformanceWeights performanceWeights {0.060, 0.040, 0.055, 0.050, 0.035};
inline constexpr int telemetrySampleCount = 12;
inline constexpr int peakTelemetrySampleCount = 18;
inline constexpr double warningCautionThreshold = 0.62;
inline constexpr double warningCriticalThreshold = 0.88;
inline constexpr double overburnMinimumDenominator = 0.20;
inline constexpr double overburnExponent = 2.65;
inline constexpr double overburnMaximumMultiplier = 8.0;
inline constexpr double baseTravelSpeedMultiplier = 2.625;
inline constexpr double maxFrameStepSeconds = 0.08;
inline constexpr double minimumEffectiveThrust = 0.40;
inline constexpr double cruiseBaseRate = 0.016;
inline constexpr double cruiseThrustScale = 0.0017;
inline constexpr double cruiseTierScale = 0.0008;
inline constexpr double accelerationBaseRate = 0.00026;
inline constexpr double accelerationHazardScale = 0.00008;
inline constexpr double cutEngineThrottleFactor = 0.58;
inline constexpr double cutEngineHeatRelief = 0.18;
inline constexpr double cutEngineVibrationRelief = 0.14;
inline constexpr double cutEngineGuidancePenalty = 0.22;
inline constexpr double pressureReliefSuccessAmount = 0.24;
inline constexpr double pressureReliefFailedAmount = 0.05;
inline constexpr double pressureReliefFailurePenalty = 0.16;
inline constexpr double pressureReliefGuidancePenalty = 0.14;
inline constexpr double pressureReliefFailedGuidancePenalty = 0.08;
inline constexpr double cargoFuelRelief = 0.22;
inline constexpr double cargoGuidancePenalty = 0.16;
inline constexpr double cargoVibrationPenalty = 0.12;
inline constexpr double cargoReturnPenalty = 0.075;
inline constexpr double dampenedMinimum = 0.03;
inline constexpr double dampenedMaximum = 0.52;
inline constexpr int overpreparedDataRiskCap = 3;
inline constexpr double transferPrepReadinessScale = 0.18;
inline constexpr double transferPrepOverpreparedScale = 0.050;
inline constexpr double transferHazardMinimum = 0.50;
inline constexpr double transferHazardBase = 1.00;
inline constexpr double transferHazardTierScale = 0.10;
inline constexpr double unprovenHazardBase = 0.20;
inline constexpr double unprovenHazardAttemptRelief = 0.035;
inline constexpr double destinationHazardScale = 0.26;
inline constexpr double volatilityHazardScale = 0.11;
inline constexpr double shipDamageHazardScale = 0.008;
inline constexpr double safetyBase = 1.0;
inline constexpr double safetyMinimum = 0.55;
inline constexpr double safetyMaximum = 1.75;
inline constexpr double longShotChanceBase = 0.012;
inline constexpr double longShotChanceAttemptScale = 0.006;
inline constexpr double longShotChanceSensorScale = 0.0015;
inline constexpr double longShotChanceMinimum = 0.012;
inline constexpr double longShotChanceMaximum = 0.055;
inline constexpr double longShotBonusMinimum = 0.24;
inline constexpr double longShotBonusMaximum = 0.52;
inline constexpr double transferCeilingPenaltyMinimum = 0.28;
inline constexpr double transferCeilingPenaltyBase = 0.90;
inline constexpr double transferCeilingTierScale = 0.10;
inline constexpr double transferCeilingPerformanceRelief = 0.32;
inline constexpr double transferCeilingReadinessRelief = 0.22;
inline constexpr double transferCeilingOverpreparedRelief = 0.080;
inline constexpr double unprovenPrepReadinessRelief = 0.20;
inline constexpr double unprovenPrepOverpreparedRelief = 0.075;
inline constexpr int unprovenCeilingAttemptCap = 8;
inline constexpr double unprovenCeilingPenaltyMinimum = 0.42;
inline constexpr double unprovenCeilingPenaltyBase = 0.86;
inline constexpr double unprovenCeilingAttemptRelief = 0.025;
inline constexpr double pressureCeilingUnprovenScale = 0.38;
inline constexpr double pressureCeilingProvenScale = 0.12;
inline constexpr double transferReadinessCeilingBonusScale = 0.20;
inline constexpr double transferOverpreparedCeilingBonusScale = 0.12;
inline constexpr double provingOverpreparedCeilingBonusScale = 0.18;
inline constexpr double minimumCrashSpan = 0.18;
inline constexpr double safetyCeilingBonusScale = 0.14;
inline constexpr double sensorQualityBase = 0.22;
inline constexpr double sensorQualityScale = 0.065;
inline constexpr double sensorQualityMinimum = 0.15;
inline constexpr double sensorQualityMaximum = 0.92;
inline constexpr double transferHeatBase = 0.04;
inline constexpr double transferHeatTierScale = 0.035;
inline constexpr double heatHazardScale = 0.56;
inline constexpr double heatVolatilityScale = 0.16;
inline constexpr double heatCoolingRelief = 0.046;
inline constexpr double heatRateMinimum = 0.34;
inline constexpr double heatRateMaximum = 2.45;
inline constexpr double pressureControlScale = 0.040;
inline constexpr double pressureModifierMinimum = 0.03;
inline constexpr double pressureModifierMaximum = 0.60;
inline constexpr int incidentBaseCount = 2;
inline constexpr int incidentTierDivisor = 2;
inline constexpr int incidentTransferBonus = 1;
inline constexpr int incidentMinimumCount = 2;
inline constexpr double incidentProfileMinimumSpan = 0.16;
inline constexpr double incidentSeverityBase = 0.12;
inline constexpr double incidentSeverityHazardScale = 0.032;
inline constexpr double incidentSeverityPressureScale = 0.10;
inline constexpr double incidentSeverityVolatilityScale = 0.035;
inline constexpr double incidentSeverityDamageScale = 0.0012;
inline constexpr double incidentLaneJitterMinimum = 0.22;
inline constexpr double incidentLaneJitterMaximum = 0.82;
inline constexpr double incidentCenterMinimum = 1.04;
inline constexpr double incidentCenterFallbackMaximum = 1.05;
inline constexpr double incidentCrashMargin = 0.035;
inline constexpr double incidentWidthMinimum = 0.045;
inline constexpr double incidentWidthMaximum = 0.105;
inline constexpr double incidentWidthHazardScale = 0.006;
inline constexpr double incidentAmountMinimum = 0.78;
inline constexpr double incidentAmountMaximum = 1.42;
inline constexpr int incidentVariantCount = 6;
inline constexpr double incidentHeatOffset = 0.08;
inline constexpr double incidentHeatCoolingMitigation = 0.026;
inline constexpr double incidentHeatPressureScale = 0.34;
inline constexpr double incidentHeatPressureMitigation = 0.020;
inline constexpr double incidentPressureOffset = 0.07;
inline constexpr double incidentPressureControlMitigation = 0.034;
inline constexpr double incidentPressureFuelMitigation = 0.010;
inline constexpr double incidentPressureFuelMixScale = 0.42;
inline constexpr double incidentPressureFuelMixMitigation = 0.019;
inline constexpr double incidentVibrationOffset = 0.06;
inline constexpr double incidentVibrationHullMitigation = 0.030;
inline constexpr double incidentVibrationGuidanceScale = 0.36;
inline constexpr double incidentVibrationGuidanceMitigation = 0.024;
inline constexpr double incidentFuelMixOffset = 0.05;
inline constexpr double incidentFuelMixFuelMitigation = 0.034;
inline constexpr double incidentFuelMixPressureScale = 0.30;
inline constexpr double incidentFuelMixPressureMitigation = 0.024;
inline constexpr double incidentGuidanceOffset = 0.06;
inline constexpr double incidentGuidanceSensorMitigation = 0.036;
inline constexpr double incidentGuidanceVibrationScale = 0.28;
inline constexpr double incidentGuidanceVibrationMitigation = 0.022;
inline constexpr double incidentAbortOffset = 0.05;
inline constexpr double incidentAbortEscapeMitigation = 0.040;
inline constexpr double incidentAbortSensorMitigation = 0.018;
inline constexpr double incidentAbortGuidanceScale = 0.32;
inline constexpr double incidentAbortGuidanceMitigation = 0.026;
} // namespace launch

namespace telemetry {
struct PulseProfile {
    double base;
    double waveScale;
    double frequency;
    double phase;
};

inline constexpr double progressDenominator = 0.01;
inline constexpr double progressMaximum = 1.40;
inline constexpr double burnLoadDenominator = 0.10;
inline constexpr double burnLoadMaximum = 1.45;
inline constexpr double lateFlightStart = 0.45;
inline constexpr double lateFlightRange = 0.65;

inline constexpr double damagedHullScale = 0.16;
inline constexpr double thrustFuelOffset = 0.48;
inline constexpr double coolingReliefScale = 0.030;
inline constexpr double hullReliefScale = 0.035;
inline constexpr double fuelReliefScale = 0.032;
inline constexpr double sensorReliefScale = 0.040;
inline constexpr double escapeReliefScale = 0.052;

inline constexpr PulseProfile pressurePulse {0.75, 0.55, 10.7, 2.1};
inline constexpr PulseProfile vibrationPulse {0.70, 0.65, 15.9, 4.4};
inline constexpr PulseProfile fuelMixPulse {0.78, 0.48, 8.3, 5.6};
inline constexpr PulseProfile guidancePulse {0.72, 0.52, 6.6, 3.2};

inline constexpr double earlyPressureBase = 0.090;
inline constexpr double earlyPressureThrustScale = 0.014;
inline constexpr double earlyPressureVolatilityScale = 0.035;
inline constexpr double earlyPressureFuelRelief = 0.22;
inline constexpr double earlyPressureCoolingRelief = 0.16;
inline constexpr double earlyVibrationBase = 0.080;
inline constexpr double earlyVibrationThrustScale = 0.012;
inline constexpr double earlyVibrationVolatilityScale = 0.050;
inline constexpr double earlyVibrationDamageScale = 0.35;
inline constexpr double earlyVibrationHullRelief = 0.22;
inline constexpr double earlyVibrationSensorRelief = 0.14;
inline constexpr double earlyFuelMixBase = 0.060;
inline constexpr double earlyFuelMixImbalanceScale = 0.014;
inline constexpr double earlyFuelMixVolatilityScale = 0.030;
inline constexpr double earlyFuelMixFuelRelief = 0.18;
inline constexpr double earlyFuelMixSensorRelief = 0.08;

inline constexpr double heatMaximum = 1.25;
inline constexpr double pressureLateBase = 0.31;
inline constexpr double pressureLateThrustScale = 0.052;
inline constexpr double pressureLateVolatilityScale = 0.075;
inline constexpr double pressureHeatScale = 0.15;
inline constexpr double pressureFuelRelief = 0.18;
inline constexpr double pressureCoolingRelief = 0.08;
inline constexpr double vibrationLateBase = 0.24;
inline constexpr double vibrationLateVolatilityScale = 0.15;
inline constexpr double vibrationLateThrustScale = 0.017;
inline constexpr double vibrationDamageScale = 0.65;
inline constexpr double vibrationHullRelief = 0.20;
inline constexpr double vibrationSensorRelief = 0.08;
inline constexpr double fuelMixLateBase = 0.23;
inline constexpr double fuelMixLateThrustOverFuelScale = 0.052;
inline constexpr double fuelMixLateVolatilityScale = 0.042;
inline constexpr double fuelMixPressureScale = 0.15;
inline constexpr double fuelMixFuelRelief = 0.22;
inline constexpr double crewNavBase = 0.35;
inline constexpr double crewNavBurnScale = 0.65;
inline constexpr double guidanceBase = 0.045;
inline constexpr double guidanceVibrationLoadScale = 0.070;
inline constexpr double guidancePressureLoadScale = 0.050;
inline constexpr double guidanceLateBase = 0.28;
inline constexpr double guidanceLateVolatilityScale = 0.055;
inline constexpr double guidanceVibrationScale = 0.20;
inline constexpr double guidancePressureScale = 0.10;
inline constexpr double guidanceSensorRelief = 0.48;

inline constexpr double readableLoadBase = 0.040;
inline constexpr double readableLoadVolatilityScale = 0.014;
inline constexpr double readablePressureBase = 0.75;
inline constexpr double readablePressurePulseScale = 0.38;
inline constexpr double readablePressureFuelRelief = 0.04;
inline constexpr double readablePressureCoolingRelief = 0.03;
inline constexpr double readablePressureMaximum = 0.32;
inline constexpr double readableVibrationBase = 0.85;
inline constexpr double readableVibrationPulseScale = 0.42;
inline constexpr double readableVibrationHullRelief = 0.04;
inline constexpr double readableVibrationSensorRelief = 0.03;
inline constexpr double readableVibrationCargoScale = 0.48;
inline constexpr double readableVibrationMaximum = 0.36;
inline constexpr double readableFuelMixBase = 0.70;
inline constexpr double readableFuelMixPulseScale = 0.34;
inline constexpr double readableFuelMixFuelRelief = 0.04;
inline constexpr double readableFuelMixSensorRelief = 0.02;
inline constexpr double readableFuelMixCargoRelief = 0.35;
inline constexpr double readableFuelMixMaximum = 0.24;
inline constexpr double readableGuidanceBase = 0.62;
inline constexpr double readableGuidancePulseScale = 0.30;
inline constexpr double readableGuidanceVibrationScale = 0.05;
inline constexpr double readableGuidanceSensorRelief = 0.04;
inline constexpr double readableGuidanceCutPenaltyScale = 0.48;
inline constexpr double readableGuidanceReliefPenaltyScale = 0.52;
inline constexpr double readableGuidanceCargoPenaltyScale = 0.58;
inline constexpr double readableGuidanceCrewNavScale = 0.74;
inline constexpr double readableGuidanceMaximum = 0.58;

inline constexpr double earlyThermalStart = 0.60;
inline constexpr double earlyThermalRange = 0.55;
inline constexpr double earlyThermalScale = 0.38;
inline constexpr double warningStartBase = 0.84;
inline constexpr double warningStartSensorScale = 0.22;
inline constexpr double certaintyWindowMinimumRange = 0.05;
inline constexpr double earlyAbortBase = 0.040;
inline constexpr double earlyAbortGuidanceScale = 0.080;
inline constexpr double earlyAbortVibrationScale = 0.050;
inline constexpr double earlyAbortProgressScale = 0.030;
inline constexpr double earlyAbortEscapeRelief = 0.10;
inline constexpr double certaintyAbortBase = 0.54;
inline constexpr double certaintyAbortGuidanceScale = 0.25;
inline constexpr double certaintyAbortVibrationScale = 0.22;
inline constexpr double abortHeatScale = 0.10;
inline constexpr double abortEscapeRelief = 0.76;

inline constexpr double stressHeatScale = 0.28;
inline constexpr double stressPressureScale = 0.20;
inline constexpr double stressVibrationScale = 0.22;
inline constexpr double stressGuidanceScale = 0.18;
inline constexpr double stressAbortScale = 0.32;
inline constexpr double stressProgressScale = 0.08;
} // namespace telemetry

namespace session {
inline constexpr double minTravelDenominator = 0.10;
inline constexpr double maxTravelProgress = 1.42;
inline constexpr double returnDefaultDuration = 2.40;
inline constexpr double returnBaseDuration = 2.10;
inline constexpr double returnDurationPerProgress = 1.40;
inline constexpr double returnDriftDurationMultiplier = 1.25;
inline constexpr double returnTurnSeconds = 1.15;
inline constexpr double launchShakeSeconds = 0.55;
inline constexpr double arrivalFanfareSeconds = 2.50;
inline constexpr double returnTelemetryProgressDenominator = 0.10;
inline constexpr double returnTelemetryHeadroomMinimum = 0.04;
inline constexpr double returnTelemetryOvershootHeadroomScale = 0.22;
inline constexpr double returnTelemetryOvershootBase = 0.18;
inline constexpr double returnTelemetryOvershootExtraHeadroomScale = 0.10;
inline constexpr double returnTelemetrySettleHeadroomScale = 0.08;
inline constexpr double returnTelemetrySettleMaximum = 0.06;
inline constexpr double returnTelemetryCrashMargin = 0.02;
inline constexpr double returnEarlyProgressThreshold = 0.28;
inline constexpr double returnWarningThreshold = 0.78;
inline constexpr double heatCautionThreshold = 0.82;
inline constexpr double driftFuelMixThreshold = 0.86;
inline constexpr double driftFuelReserveThreshold = 1.0;
inline constexpr double driftTargetShare = 0.85;
inline constexpr double pressureReliefFailureBase = 0.16;
inline constexpr double pressureReliefFailurePressureScale = 0.16;
inline constexpr double pressureReliefFailureControlScale = 0.035;
inline constexpr double pressureReliefFailureHullScale = 0.010;
inline constexpr double pressureReliefFailureMinimum = 0.04;
inline constexpr double pressureReliefFailureMaximum = 0.34;
inline constexpr double decompressionBase = 0.012;
inline constexpr double decompressionPressureScale = 0.045;
inline constexpr double decompressionDamageScale = 0.00025;
inline constexpr double decompressionHullScale = 0.0035;
inline constexpr double decompressionMinimum = 0.004;
inline constexpr double decompressionMaximum = 0.08;
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
inline constexpr double brakeAcceleration = 0.52;
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
inline constexpr int impactHullDamage = 18;
} // namespace flyby

namespace orbit {
inline constexpr double durationSeconds = 15.0;
inline constexpr double planetBaseRadius = 0.145;
inline constexpr double planetTierRadius = 0.016;
inline constexpr double targetRadiusScale = 2.95;
inline constexpr double goodBandScale = 0.55;
inline constexpr double perfectBandScale = 0.24;
inline constexpr double startAngleRadians = -2.70;
inline constexpr double startTangentialSpeed = 0.36;
inline constexpr double thrustAcceleration = 0.075;
inline constexpr double sensorGoodBandScale = 0.012;
inline constexpr double sensorPerfectBandScale = 0.0045;
inline constexpr double escapeBandScale = 0.004;
inline constexpr double thrustControlScale = 0.018;
inline constexpr double coolingControlScale = 0.006;
inline constexpr double volatilityControlPenalty = 0.006;
inline constexpr double fuelDurationScale = 0.15;
inline constexpr double sensorDurationScale = 0.08;
inline constexpr double maxAssistDurationBonus = 2.0;
inline constexpr double gravitySoftening = 0.120;
inline constexpr double gravityScale = 0.42;
inline constexpr double driftDrag = 0.008;
inline constexpr double minSpeed = 0.18;
inline constexpr double maxSpeed = 0.48;
inline constexpr double escapeRadiusScale = 2.40;
inline constexpr double collisionPadding = 0.018;
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
inline constexpr double manualEjectPayoutFactor = 0.26;
inline constexpr double transferArrivalPayoutFactor = 1.45;
inline constexpr double fullProfileRewardFloor = 1.00;
inline constexpr double pushedProfileShelfShare = 0.45;
inline constexpr double shallowRecoveryTargetShare = 0.25;
inline constexpr double shallowRecoveryPenaltyBase = 15.0;
inline constexpr int shallowRecoveryPenaltyMaxExponent = 6;
inline constexpr double cleanShallowRecoveryWarningThreshold = 0.62;
inline constexpr int cleanShallowRecoveryDestructionStreak = 3;
} // namespace rewards

namespace research {
inline constexpr int firstResearchTier = 2;
inline constexpr int enemyEncounterTier = 4;
inline constexpr int offerCount = 3;
inline constexpr int baseSupply = 7;
inline constexpr int supplyPerTier = 1;
inline constexpr int sharedFuelCapacity = 3;
inline constexpr int surveySupplyCost = 1;
inline constexpr int mineSupplyCost = 2;
inline constexpr int pushSupplyCost = 2;
inline constexpr int surveyCommonGain = 1;
inline constexpr int mineCommonGain = 2;
inline constexpr int mineRareDepthThreshold = 1;
inline constexpr int artifactDepthThreshold = 2;
inline constexpr int extractionRareLossDivisor = 2;
inline constexpr int probeSupplyBonus = 1;
inline constexpr int probeSurveyCommonBonus = 1;
inline constexpr int drillMineCommonBonus = 1;
inline constexpr double drillRareChanceBonus = 0.18;
inline constexpr double cargoRigExtractionRiskRelief = 0.08;
inline constexpr double cargoRigCargoRiskRelief = 0.006;
inline constexpr double baseHazard = 0.12;
inline constexpr double hazardPerTier = 0.035;
inline constexpr double hazardPerDepth = 0.055;
inline constexpr double extractionRiskBase = 0.05;
inline constexpr double extractionRiskHazardScale = 0.42;
inline constexpr double extractionRiskCargoScale = 0.018;
inline constexpr double extractionRiskLowSupplyPenalty = 0.10;
inline constexpr double extractionRiskMaximum = 0.72;
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
inline constexpr double oxygenSeconds = 15.0;
inline constexpr double fuelSecondsPerUnit = 15.0;
inline constexpr double targetRunSeconds = 120.0;
inline constexpr double droneSpeedCellsPerSecond = 7.2;
inline constexpr double softTerrainMoveScale = 0.42;
inline constexpr double hardTerrainBounceImpulse = 13.5;
inline constexpr double hardTerrainBounceCooldownSeconds = 0.30;
inline constexpr double contactBounceSpring = 58.0;
inline constexpr double contactBounceDamping = 0.68;
inline constexpr double contactBounceMaxCells = 0.56;
inline constexpr double passiveLightRadius = 2.15;
inline constexpr double drillRangeCells = 2.05;
inline constexpr double drillAimDeadzoneCells = 0.75;
inline constexpr int drillAimDirections = 8;
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
inline constexpr double heatRisePerSecond = 0.20;
inline constexpr double heatHardRockBonus = 0.08;
inline constexpr double heatCoolingPerSecond = 0.16;
inline constexpr double heatSlowThreshold = 0.72;
inline constexpr double heatDamageThreshold = 0.90;
inline constexpr double overheatedDrillSlow = 0.48;
inline constexpr double overheatIntegrityDamagePerSecond = 0.055;
inline constexpr double hazardPocketIntegrityDamage = 0.12;
inline constexpr double hazardPocketRisk = 0.035;
inline constexpr double depthHazardRisk = 0.030;
inline constexpr double cargoExtractionRiskScale = 0.006;
inline constexpr double maxMiningHazardDelta = 0.26;
inline constexpr double scannerRevealRadius = 5.5;
inline constexpr double scannerProbeBonus = 2.0;
inline constexpr double scannerCooldownSeconds = 4.0;
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
inline constexpr double artifactTetherRangeCells = 3.4;
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
inline constexpr double enemyContactRadiusCells = 0.82;
inline constexpr double enemyElementalRadiusCells = 1.85;
inline constexpr double enemyDamageScale = 0.030;
inline constexpr double areaControlRangeCells = 5.4;
inline constexpr double flyingDartStrength = 0.62;
inline constexpr double flyingDartFrequency = 8.5;
inline constexpr double mammalBurrowPower = 9.5;
inline constexpr double elementalHeatRisePerSecond = 0.32;
inline constexpr double elementalRadiationHazardPerSecond = 0.010;
inline constexpr double elementalToxicIntegrityDamagePerSecond = 0.010;
inline constexpr double elementalCryoSlowDurationSeconds = 0.55;
inline constexpr double elementalCryoSlowScale = 0.58;
inline constexpr double minibossHealthScale = 1.85;
inline constexpr double bossHealthScale = 2.65;
inline constexpr double roomEnemyHealthScale = 1.25;
} // namespace mining

namespace outcomes {
inline constexpr double manualEjectSurvivalBase = 0.48;
inline constexpr double vehicleLossSurvivalBase = 0.22;
inline constexpr double survivalEscapeScale = 0.07;
inline constexpr double survivalHazardScale = 0.035;
inline constexpr double survivalMinimum = 0.05;
inline constexpr double survivalMaximum = 0.90;
inline constexpr double manualEjectInjuryChance = 0.42;
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
inline constexpr double manualEjectRecoveryBase = 14.0;
inline constexpr double manualEjectRecoveryTierScale = 7.0;
inline constexpr double manualEjectRecoveryBurnScale = 3.0;
inline constexpr double manualEjectRecoveryEscapeRelief = 1.2;
inline constexpr double manualEjectRecoveryMinimum = 8.0;
inline constexpr double manualEjectRecoveryMaximum = 64.0;
inline constexpr double manualEjectDamageHazardScale = 3.8;
inline constexpr double manualEjectDamageBurnScale = 2.2;
inline constexpr double manualEjectDamageAbortScale = 10.0;
inline constexpr double manualEjectDamageEscapeRelief = 0.75;
inline constexpr int manualEjectDamageMinimum = 4;
inline constexpr int manualEjectDamageMaximum = 34;
inline constexpr double manualEjectCrewInjuryBase = 0.14;
inline constexpr double manualEjectCrewInjuryAbortScale = 0.32;
inline constexpr double manualEjectCrewInjuryDamageScale = 0.002;
inline constexpr double manualEjectCrewInjuryEscapeRelief = 0.030;
inline constexpr double manualEjectCrewInjuryMinimum = 0.02;
inline constexpr double manualEjectCrewInjuryMaximum = 0.48;
inline constexpr double manualEjectBlueprintTargetShare = 0.82;

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
inline constexpr double manualEjectUsefulDataTargetShare = 0.90;
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
