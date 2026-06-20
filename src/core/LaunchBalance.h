#pragma once

#include "core/GameTypes.h"
#include "core/Tuning.h"

#include <algorithm>
#include <cmath>

namespace rocket::launch_balance {

inline int cappedOverpreparedData(int overpreparedData)
{
    return std::min(std::max(0, overpreparedData), tuning::launch::overpreparedDataRiskCap);
}

inline double performanceScore(const ModuleStats& stats, double crewTrainingBonus)
{
    const tuning::launch::PerformanceWeights& weights = tuning::launch::performanceWeights;
    return std::max(0.0,
        stats.thrust * weights.thrust +
            stats.fuel * weights.fuel +
            stats.hull * weights.hull +
            stats.cooling * weights.cooling +
            stats.sensors * weights.sensors +
            crewTrainingBonus);
}

inline double readinessRatio(int currentReadiness, int requiredReadiness)
{
    if (requiredReadiness <= 0) {
        return 1.0;
    }
    return std::clamp(static_cast<double>(std::max(0, currentReadiness)) / static_cast<double>(requiredReadiness), 0.0, 1.0);
}

inline int overpreparedData(int currentReadiness, int requiredReadiness)
{
    return requiredReadiness <= 0 ? 0 : std::max(0, currentReadiness - requiredReadiness);
}

inline double provingPayoutBonus(int overpreparedData, bool frontierTransfer)
{
    if (frontierTransfer) {
        return 0.0;
    }
    return std::clamp(
        static_cast<double>(std::max(0, overpreparedData)) * tuning::rewards::provingPayoutPerExtraData,
        0.0,
        tuning::rewards::provingPayoutBonusMaximum);
}

inline double transferPreparation(double readinessRatio, int overpreparedData, bool frontierTransfer)
{
    if (!frontierTransfer) {
        return 0.0;
    }
    return readinessRatio * tuning::launch::transferPrepReadinessScale +
        static_cast<double>(cappedOverpreparedData(overpreparedData)) * tuning::launch::transferPrepOverpreparedScale;
}

inline double transferHazard(const Destination& destination, double transferPreparation, bool frontierTransfer)
{
    if (!frontierTransfer) {
        return 0.0;
    }
    return std::max(
        tuning::launch::transferHazardMinimum,
        tuning::launch::transferHazardBase +
            static_cast<double>(destination.tier) * tuning::launch::transferHazardTierScale -
            transferPreparation);
}

inline double unprovenHazard(int attempts, int successes)
{
    if (successes > 0) {
        return 0.0;
    }
    return std::max(0.0, tuning::launch::unprovenHazardBase - static_cast<double>(std::max(0, attempts)) * tuning::launch::unprovenHazardAttemptRelief);
}

inline double launchHazard(const Destination& destination, const ModuleStats& stats, int shipDamage, double transferHazard, double unprovenHazard)
{
    return destination.hazard * tuning::launch::destinationHazardScale +
        transferHazard +
        unprovenHazard +
        std::max(0.0, stats.volatility) * tuning::launch::volatilityHazardScale +
        static_cast<double>(std::max(0, shipDamage)) * tuning::launch::shipDamageHazardScale;
}

inline double launchSafety(double performance, double hazard)
{
    return std::clamp(
        tuning::launch::safetyBase + performance - hazard,
        tuning::launch::safetyMinimum,
        tuning::launch::safetyMaximum);
}

inline double longShotChance(int attempts, double sensors)
{
    return std::clamp(
        tuning::launch::longShotChanceBase +
            static_cast<double>(std::max(0, attempts)) * tuning::launch::longShotChanceAttemptScale +
            sensors * tuning::launch::longShotChanceSensorScale,
        tuning::launch::longShotChanceMinimum,
        tuning::launch::longShotChanceMaximum);
}

inline double transferCeilingPenalty(const Destination& destination, double performance, double readinessRatio, int overpreparedData, bool frontierTransfer)
{
    if (!frontierTransfer) {
        return 0.0;
    }
    return std::max(
        tuning::launch::transferCeilingPenaltyMinimum,
        tuning::launch::transferCeilingPenaltyBase +
            static_cast<double>(destination.tier) * tuning::launch::transferCeilingTierScale -
            performance * tuning::launch::transferCeilingPerformanceRelief -
            readinessRatio * tuning::launch::transferCeilingReadinessRelief -
            static_cast<double>(cappedOverpreparedData(overpreparedData)) * tuning::launch::transferCeilingOverpreparedRelief);
}

inline double unprovenPrepRelief(double readinessRatio, int overpreparedData, bool frontierTransfer)
{
    if (!frontierTransfer) {
        return 0.0;
    }
    return readinessRatio * tuning::launch::unprovenPrepReadinessRelief +
        static_cast<double>(cappedOverpreparedData(overpreparedData)) * tuning::launch::unprovenPrepOverpreparedRelief;
}

inline double unprovenCeilingPenalty(int attempts, int successes, double unprovenPrepRelief)
{
    if (successes > 0) {
        return 0.0;
    }
    return std::max(
        tuning::launch::unprovenCeilingPenaltyMinimum,
        tuning::launch::unprovenCeilingPenaltyBase -
            static_cast<double>(std::min(std::max(0, attempts), tuning::launch::unprovenCeilingAttemptCap)) * tuning::launch::unprovenCeilingAttemptRelief -
            unprovenPrepRelief);
}

inline double pressureCeilingPenalty(int successes, double missionPressure)
{
    return missionPressure * (successes == 0 ? tuning::launch::pressureCeilingUnprovenScale : tuning::launch::pressureCeilingProvenScale);
}

inline double transferReadinessCeilingBonus(double readinessRatio, int overpreparedData, bool frontierTransfer)
{
    if (!frontierTransfer) {
        return 0.0;
    }
    return readinessRatio * tuning::launch::transferReadinessCeilingBonusScale +
        static_cast<double>(cappedOverpreparedData(overpreparedData)) * tuning::launch::transferOverpreparedCeilingBonusScale;
}

inline double provingOverpreparedCeilingBonus(int overpreparedData, bool frontierTransfer)
{
    return frontierTransfer ? 0.0 : static_cast<double>(std::max(0, overpreparedData)) * tuning::launch::provingOverpreparedCeilingBonusScale;
}

inline double maxCrashCeiling(
    const Destination& destination,
    double transferCeilingPenalty,
    double unprovenCeilingPenalty,
    double pressureCeilingPenalty,
    double longShotBonus,
    double transferReadinessCeilingBonus,
    double overpreparedCeilingBonus,
    double safety)
{
    return std::max(
        destination.minCrashMultiplier + tuning::launch::minimumCrashSpan,
        destination.maxCrashMultiplier -
            transferCeilingPenalty -
            unprovenCeilingPenalty -
            pressureCeilingPenalty +
            longShotBonus +
            transferReadinessCeilingBonus +
            overpreparedCeilingBonus +
            safety * tuning::launch::safetyCeilingBonusScale);
}

inline double sensorQuality(const ModuleStats& stats)
{
    return std::clamp(
        tuning::launch::sensorQualityBase + stats.sensors * tuning::launch::sensorQualityScale,
        tuning::launch::sensorQualityMinimum,
        tuning::launch::sensorQualityMaximum);
}

inline double transferHeatLoad(const Destination& destination, bool frontierTransfer)
{
    return frontierTransfer
        ? tuning::launch::transferHeatBase + static_cast<double>(destination.tier) * tuning::launch::transferHeatTierScale
        : 0.0;
}

inline double heatRate(const Destination& destination, const ModuleStats& stats, double transferHeatLoad)
{
    return std::clamp(
        destination.hazard * tuning::launch::heatHazardScale +
            transferHeatLoad +
            std::max(0.0, stats.volatility) * tuning::launch::heatVolatilityScale -
            stats.cooling * tuning::launch::heatCoolingRelief,
        tuning::launch::heatRateMinimum,
        tuning::launch::heatRateMaximum);
}

inline double pressureModifier(double missionPressure, const ModuleStats& stats)
{
    return std::clamp(
        missionPressure - std::max(0.0, stats.pressure) * tuning::launch::pressureControlScale,
        tuning::launch::pressureModifierMinimum,
        tuning::launch::pressureModifierMaximum);
}

inline int incidentCount(const Destination& destination, bool frontierTransfer, int incidentCapacity)
{
    const int transferBonus = frontierTransfer ? tuning::launch::incidentTransferBonus : 0;
    return std::clamp(
        tuning::launch::incidentBaseCount + destination.tier / tuning::launch::incidentTierDivisor + transferBonus,
        tuning::launch::incidentMinimumCount,
        incidentCapacity);
}

inline double incidentProfileSpan(double profileEnd)
{
    return std::max(tuning::launch::incidentProfileMinimumSpan, profileEnd - 1.0);
}

inline double incidentSeverity(const Destination& destination, const ModuleStats& stats, int shipDamage, double missionPressure)
{
    return tuning::launch::incidentSeverityBase +
        destination.hazard * tuning::launch::incidentSeverityHazardScale +
        missionPressure * tuning::launch::incidentSeverityPressureScale +
        std::max(0.0, stats.volatility) * tuning::launch::incidentSeverityVolatilityScale +
        static_cast<double>(std::max(0, shipDamage)) * tuning::launch::incidentSeverityDamageScale;
}

inline double incidentLane(int incidentIndex, int incidentCount, double laneJitter)
{
    return (static_cast<double>(incidentIndex) + laneJitter) / static_cast<double>(incidentCount + 1);
}

inline double incidentCenterMultiplier(double profileSpan, double lane, double crashMultiplier)
{
    return std::clamp(
        1.0 + profileSpan * lane,
        tuning::launch::incidentCenterMinimum,
        std::max(tuning::launch::incidentCenterFallbackMaximum, crashMultiplier - tuning::launch::incidentCrashMargin));
}

inline double incidentWidth(const Destination& destination, double randomWidth)
{
    return randomWidth + destination.hazard * tuning::launch::incidentWidthHazardScale;
}

inline double incidentAmount(double severity, double randomScale)
{
    return severity * randomScale;
}

} // namespace rocket::launch_balance
