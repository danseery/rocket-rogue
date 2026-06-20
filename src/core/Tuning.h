#pragma once

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
inline constexpr std::string_view calmUnderHeat = "Calm under heat";
inline constexpr std::string_view readsTelemetryEarly = "Reads telemetry early";
inline constexpr std::string_view improvesEjectionOdds = "Improves ejection odds";
inline constexpr double calmUnderHeatBonus = 0.12;
inline constexpr double readsTelemetryEarlyBonus = 0.06;
inline constexpr double improvesEjectionOddsPerformanceBonus = 0.04;
inline constexpr double improvesEjectionOddsEscapeBonus = 0.16;
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
inline constexpr int trainingBaseStress = 6;
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
inline constexpr int readinessOverCap = 3;
inline constexpr double destroyedCreditPenalty = 30.0;
} // namespace mission

namespace unlocks {
struct BlueprintUnlock {
    int threshold;
    std::string_view key;
    std::string_view message;
};

inline constexpr BlueprintUnlock blueprintUnlocks[] = {
    {4, "thermal", "Thermal systems unlocked."},
    {8, "recovery", "Recovery hardware unlocked."},
    {12, "deep_space", "Deep-space module family unlocked."},
    {18, "ai", "Predictive guidance unlocked."},
    {24, "exotic", "Exotic prototype modules unlocked."}
};
} // namespace unlocks

namespace launch {
inline constexpr int telemetrySampleCount = 12;
inline constexpr int peakTelemetrySampleCount = 18;
inline constexpr double warningCautionThreshold = 0.62;
inline constexpr double warningCriticalThreshold = 0.88;
inline constexpr double overburnMinimumDenominator = 0.20;
inline constexpr double overburnExponent = 2.65;
inline constexpr double overburnMaximumMultiplier = 8.0;
inline constexpr double travelSpeedMultiplier = 1.10;
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

namespace rewards {
inline constexpr double provingPayoutPerExtraData = 0.20;
inline constexpr double provingPayoutBonusMaximum = 0.60;
inline constexpr double returnHomeBasePayoutFactor = 0.74;
inline constexpr double returnHomeReachedGoalFactor = 1.18;
inline constexpr double manualEjectPayoutFactor = 0.26;
inline constexpr double transferArrivalPayoutFactor = 1.45;
inline constexpr double fullProfileRewardFloor = 1.00;
inline constexpr double pushedProfileShelfShare = 0.45;
} // namespace rewards

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
