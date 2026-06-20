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

namespace damage {
inline constexpr double hullPenaltyPerDamage = 2.2;
inline constexpr double coolingPenaltyPerDamage = 1.2;
inline constexpr double escapePenaltyPerDamage = 0.8;
inline constexpr int destroyedShipDamage = 100;
inline constexpr double moduleLossChance = 0.62;
} // namespace damage

} // namespace rocket::tuning
