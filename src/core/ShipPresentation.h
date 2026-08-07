#pragma once

#include "core/DetailPresentation.h"
#include "core/GameFormat.h"
#include "core/GameState.h"
#include "core/GameText.h"
#include "core/RefitPresentation.h"

#include <algorithm>
#include <string>
#include <vector>

namespace rocket {

inline std::string shipModuleSummary(const ShipModule& module)
{
    return module.name + " (" + std::string(toString(module.rarity)) + ")";
}

inline std::vector<DetailPresentationRow> shipDetailsPresentation(const GameState& state, const ContentCatalog& catalog)
{
    std::vector<DetailPresentationRow> rows;
    const ModuleStats stats = aggregateShipStats(state, catalog);

    for (const ModuleStatDisplay& stat : moduleStatDisplays(stats)) {
        if (stat.showInShipDetails) {
            rows.push_back(detailPresentationRow(stat.detailLabel, display::money(stat.value)));
        }
    }

    rows.push_back(detailPresentationRow(text::moduleStats::damage, display::wholePercent(state.run.shipDamage)));
    rows.push_back(detailPresentationHeader("Launch handling"));
    const double poweredCorrection =
        (tuning::launch::pilotingSteeringBase +
            std::max(0.0, stats.thrust) * tuning::launch::pilotingSteeringThrustScale +
            std::max(0.0, stats.sensors) * tuning::launch::pilotingSteeringSensorScale) *
        (tuning::launch::pilotingPoweredSteeringBase +
            tuning::launch::pilotingInitialThrottle * tuning::launch::pilotingPoweredSteeringThrottleScale);
    const double fuelCapacity = std::max(
        0.55,
        1.0 + (stats.fuel - 1.0) * tuning::launch::pilotingFuelStatCapacityScale);
    const double courseLimit = tuning::launch::pilotingCourseLost * (1.0 + std::min(
        tuning::launch::pilotingCourseMaximumSensorTolerance,
        std::max(0.0, stats.sensors) * tuning::launch::pilotingCourseSensorToleranceScale));
    const double warningLead = std::min(
        tuning::launch::pilotingIncidentWarningMaximumSeconds,
        tuning::launch::pilotingIncidentWarningBaseSeconds +
            std::max(0.0, stats.sensors) * tuning::launch::pilotingIncidentWarningSensorSeconds);
    const double coolingRate = tuning::launch::pilotingCoolingBase +
        std::max(0.0, stats.cooling) * tuning::launch::pilotingCoolingStatScale +
        tuning::launch::pilotingCutCoolingBonus;
    const double pressureGrace = std::clamp(
        tuning::launch::pilotingPressureGraceBaseSeconds +
            std::max(0.0, stats.hull) * tuning::launch::pilotingPressureGraceHullSeconds,
        tuning::launch::pilotingPressureGraceBaseSeconds,
        tuning::launch::pilotingPressureGraceMaximumSeconds);
    const double ventRate = tuning::launch::pilotingValveReliefBase +
        std::max(0.0, stats.pressure) * tuning::launch::pilotingValveReliefStatScale;
    rows.push_back(detailPresentationRow("Cruise throttle", display::percent(tuning::launch::pilotingInitialThrottle)));
    rows.push_back(detailPresentationRow("Powered correction", display::fixed(poweredCorrection, 2) + " course/s"));
    rows.push_back(detailPresentationRow("Fuel capacity", display::fixed(fuelCapacity * 100.0, 0) + "%"));
    rows.push_back(detailPresentationRow("Engine-off cooling", display::fixed(coolingRate * 100.0, 1) + "%/s before incidents"));
    rows.push_back(detailPresentationRow("Pressure grace", display::fixed(pressureGrace, 2) + "s at 100%"));
    rows.push_back(detailPresentationRow("Valve venting", display::fixed(ventRate * 100.0, 1) + "%/s"));
    rows.push_back(detailPresentationRow("Course corridor", "\xC2\xB1" + display::fixed(courseLimit, 2)));
    rows.push_back(detailPresentationRow("Incident warning", display::fixed(warningLead, 2) + "s"));
    rows.push_back(detailPresentationRow(
        "Disturbance strength",
        display::multiplier(std::max(0.50, 1.0 + stats.volatility * tuning::launch::pilotingVolatilityIncidentScale))));
    rows.push_back(detailPresentationHeader("Installed ship systems"));
    for (const std::string& moduleId : state.meta.ownedModuleIds) {
        if (const ShipModule* module = catalog.findModule(moduleId)) {
            const bool operational = std::find(state.run.equippedModuleIds.begin(), state.run.equippedModuleIds.end(), moduleId) != state.run.equippedModuleIds.end();
            const bool builtIn = module->refitTrack == RefitTrack::None && module->unlockKey == content::unlock::starter;
            const std::string status = builtIn ? "Built in" : (operational ? "Installed" : "Offline this expedition");
            rows.push_back(detailPresentationRow(status, shipModuleSummary(*module)));
        }
    }

    return rows;
}

} // namespace rocket
