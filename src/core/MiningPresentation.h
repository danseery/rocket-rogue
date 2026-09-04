#pragma once

#include "core/DetailPresentation.h"
#include "core/GameFormat.h"
#include "core/GameText.h"
#include "core/GameUi.h"
#include "core/MiningSystem.h"
#include "core/PanelPresentation.h"
#include "core/PayloadTransfer.h"
#include "core/ResearchSystem.h"
#include "core/RigFuelSystem.h"
#include "core/ScenarioSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <string_view>
#include <vector>

namespace rocket {

struct MiningRunPresentation {
    std::vector<PanelMetricPresentation> metrics;
    std::vector<PanelMetricPresentation> payloadMetrics;
    std::vector<PanelMetricPresentation> combatMetrics;
    std::vector<DetailPresentationRow> details;
    std::vector<PanelButtonPresentation> actions;
    std::string combatTitle;
    std::string combatDetail;
    std::string commandTitle;
    std::string commandDetail;
    std::vector<std::string> commandHints;
    std::string rigHealth;
    double rigHealthRatio = 1.0;
    bool failurePending = false;
    std::string failureTitle;
    std::string failureBody;
};

struct MiningHudTilePresentation {
    std::string label;
    std::string value;
    std::string cssClass;
    std::string microLabel;
    std::string microValue;
};

struct MiningOreManifestPresentation {
    std::array<MiningHudTilePresentation, 3> ores;
    std::string legend;
};

struct MiningHudPresentation {
    std::string runLabel;
    std::string objective;
    std::array<MiningHudTilePresentation, 4> vitals;
    MiningOreManifestPresentation oreManifest;
    std::array<MiningHudTilePresentation, 2> payload;
    std::vector<PanelButtonPresentation> actions;
    std::vector<DetailPresentationRow> details;
    bool atShip = false;
    bool failurePending = false;
    std::string failureTitle;
    std::string failureBody;
};

inline std::string miningOxygenValue(double seconds)
{
    return std::to_string(static_cast<int>(std::ceil(std::max(0.0, seconds)))) + "s";
}

inline std::string miningFuelCycleValue(double progress)
{
    return display::percent(1.0 - std::clamp(progress, 0.0, 1.0));
}

inline std::string miningToughnessValue(const MiningRunState& mining)
{
    if (mining.targetMaxToughness <= 0.0) {
        return "Clear";
    }
    return display::percent(std::clamp(mining.targetRemainingToughness / mining.targetMaxToughness, 0.0, 1.0));
}

inline std::string miningDroneSummary(const MiniDroneLoadoutEffects& drones)
{
    if (drones.names.empty()) {
        return "None";
    }
    std::string summary = drones.names.front();
    for (std::size_t i = 1; i < drones.names.size(); ++i) {
        summary += ", " + drones.names[i];
    }
    return summary;
}

inline std::string miningNameListSummary(const std::vector<std::string>& names)
{
    if (names.empty()) {
        return "None";
    }
    std::string summary = names.front();
    for (std::size_t i = 1; i < names.size(); ++i) {
        summary += ", " + names[i];
    }
    return summary;
}

inline std::string hostileTunnelSummary(const MiningTerrain& terrain)
{
    int structures = 0;
    int rooms = 0;
    MiningEnemyType firstEnemy = MiningEnemyType::None;
    for (const MiningCell& cell : terrain.cells) {
        if (cell.feature != MiningCellFeature::None) {
            structures += 1;
        }
        if (cell.feature == MiningCellFeature::TreasureVault || cell.feature == MiningCellFeature::MinibossLair || cell.feature == MiningCellFeature::HiveNest || cell.feature == MiningCellFeature::BossChamber) {
            rooms += 1;
        }
        if (firstEnemy == MiningEnemyType::None && cell.enemy != MiningEnemyType::None) {
            firstEnemy = cell.enemy;
        }
    }
    if (structures <= 0) {
        return "None";
    }
    std::string summary = std::to_string(structures) + " pre-dug cells";
    if (rooms > 0) {
        summary += ", " + std::to_string(rooms) + " room cells";
    }
    if (firstEnemy != MiningEnemyType::None) {
        summary += ", " + std::string(miningEnemyTypeName(firstEnemy));
    }
    return summary;
}

inline int activeMiningEnemyCount(const MiningRunState& mining)
{
    return static_cast<int>(std::count_if(mining.enemies.begin(), mining.enemies.end(), [](const MiningEnemy& enemy) {
        return enemy.active;
    }));
}

inline int activeMiningSwarmEnemyCount(const MiningRunState& mining)
{
    return static_cast<int>(std::count_if(mining.enemies.begin(), mining.enemies.end(), [](const MiningEnemy& enemy) {
        return enemy.active && enemy.swarmAssociated;
    }));
}

inline int activeMiningProjectileCount(const MiningRunState& mining, MiningCombatTeam team)
{
    return static_cast<int>(std::count_if(mining.combatProjectiles.begin(), mining.combatProjectiles.end(), [team](const MiningProjectileVisual& projectile) {
        return projectile.team == team;
    }));
}

inline int activeMiningCritTextCount(const MiningRunState& mining)
{
    return static_cast<int>(std::count_if(mining.damageNumbers.begin(), mining.damageNumbers.end(), [](const MiningDamageNumber& number) {
        return number.critical;
    }));
}

inline int tunedMiningDroneCount(const GameState& state)
{
    return static_cast<int>(std::count_if(
        state.run.planetaryExpedition.runDroneRanks.begin(),
        state.run.planetaryExpedition.runDroneRanks.end(),
        [](const RunDroneRank& record) {
            return record.rank > 1;
        }));
}

inline std::string activeElementalSummary(const MiningRunState& mining)
{
    for (const MiningEnemy& enemy : mining.enemies) {
        if (!enemy.active || enemy.affinity == MiningElementalAffinity::None) {
            continue;
        }
        const bool trueElite = enemy.elite ||
            enemy.sourceFeature == MiningCellFeature::MinibossLair ||
            enemy.sourceFeature == MiningCellFeature::BossChamber;
        if (trueElite) {
            return "ELITE " + std::string(miningElementalAffinityName(enemy.affinity));
        }
        if (enemy.type == MiningEnemyType::Elemental) {
            return std::string(miningElementalAffinityName(enemy.affinity)) + " elemental";
        }
    }
    return "None";
}

inline std::string miningEnemyEcologySummary(const MiningRunState& mining)
{
    const std::string theme = std::string(miningEnemyThemeName(mining.enemyTheme));
    if (mining.enemyTheme == MiningEnemyTheme::Neutral) {
        return theme + " ecology; no site affinity.";
    }
    return theme + " ecology. Ordinary hostiles share the site skin; Elementals and true elites apply " +
        std::string(miningElementalAffinityName(miningEnemyThemeAffinity(mining.enemyTheme))) +
        " pressure.";
}

inline std::string activeThreatSummary(const MiningRunState& mining)
{
    int ants = 0;
    int flying = 0;
    int beetles = 0;
    int elementals = 0;
    int mammals = 0;
    int spawners = 0;
    int bosses = 0;
    for (const MiningEnemy& enemy : mining.enemies) {
        if (!enemy.active) {
            continue;
        }
        switch (enemy.type) {
        case MiningEnemyType::Ant:
            ants += 1;
            break;
        case MiningEnemyType::Flying:
            flying += 1;
            break;
        case MiningEnemyType::Beetle:
            beetles += 1;
            break;
        case MiningEnemyType::Elemental:
            elementals += 1;
            break;
        case MiningEnemyType::Mammal:
            mammals += 1;
            break;
        case MiningEnemyType::Spawner:
            spawners += 1;
            break;
        case MiningEnemyType::None:
            break;
        }
        if (enemy.sourceFeature == MiningCellFeature::MinibossLair || enemy.sourceFeature == MiningCellFeature::BossChamber) {
            bosses += 1;
        }
    }

    std::vector<std::string> parts;
    auto addPart = [&parts](std::string label, int count) {
        if (count > 0) {
            parts.push_back(std::move(label) + " x" + std::to_string(count));
        }
    };
    addPart("Ant", ants);
    addPart("Flying", flying);
    addPart("Beetle", beetles);
    addPart("Elemental", elementals);
    addPart("Mammal", mammals);
    addPart("Spawner", spawners);
    addPart("Boss", bosses);
    if (parts.empty()) {
        return "None";
    }
    std::string summary = parts.front();
    for (std::size_t i = 1; i < parts.size(); ++i) {
        summary += ", " + parts[i];
    }
    return summary;
}

inline std::string miningArtifactStateLabel(MiningArtifactState state)
{
    switch (state) {
    case MiningArtifactState::Embedded:
        return "Embedded";
    case MiningArtifactState::Loose:
        return "Loose";
    case MiningArtifactState::Delivered:
        return "Delivered";
    case MiningArtifactState::Destroyed:
        return "Destroyed";
    case MiningArtifactState::None:
        break;
    }
    return "None";
}

inline MiningRunPresentation miningRunPresentation(const GameState& state, const ContentCatalog& catalog)
{
    const MiningRunState& mining = state.run.mining;
    const MiningDrillStats stats = miningDrillStats(state, catalog);
    const MiniDroneLoadoutEffects drones = miniDroneLoadoutEffects(state, catalog);
    const MiningArenaMetadata& arena = mining.arenaMetadata;
    const MiningArenaRules arenaRules = resolveMiningArenaRules({arena.act, arena.difficulty, arena.seed});
    const MiningLoadStats load = miningLoadStats(state, catalog);
    const bool evaActive =
        mining.operatorMode == MiningOperatorMode::Jetpack &&
        mining.operatorPresent;
    const bool protectedObjectiveNeedsPulse =
        mining.gate.completeOnShipCapture &&
        mining.gate.protectedObjective.kind == ProtectedObjectiveKind::Artifact &&
        mining.artifact.present &&
        !mining.artifact.revealed;
    const MiningDrillStats operatorStats =
        miningOperatorDrillStats();
    const double rigOxygenCapacity = mining.rigOxygen.capacity;
    const double displayedDrillPower = evaActive
        ? operatorStats.power
        : stats.power;
    const double displayedDrillRange = evaActive
        ? tuning::mining::operatorDrillRangeCells
        : tuning::mining::drillRangeCells;
    const double displayedScannerRadius = evaActive
        ? operatorStats.scannerRadius
        : stats.scannerRadius;
    const int carriedCargo = miningCarriedCargo(mining);
    const int bankedCargo = miningBankedCargo(mining);
    MiningRunPresentation presentation;
    presentation.failurePending = mining.failurePending;
    presentation.failureTitle = evaActive
        ? "Operator recovery failed"
        : "Emergency recall";
    presentation.failureBody = mining.failureMessage.empty()
        ? std::string("Mining Rig recall is in progress.")
        : mining.failureMessage;
    presentation.rigHealthRatio = std::clamp(mining.droneHealth, 0.0, 1.0);
    presentation.rigHealth = display::percent(presentation.rigHealthRatio);
    presentation.metrics = {
        panelMetric(text::labels::droneHealth, presentation.rigHealth),
        panelMetric("Suit integrity", display::percent(mining.operatorIntegrity)),
        panelMetric("Mode", evaActive ? "EVA" : "Rig"),
        panelMetric("Gravity", display::fixed(mining.gravityStrength, 1) + " cells/s2"),
        panelMetric(text::labels::drillBit, mining.drillIntegrity <= 0.0 ? "Broken" : display::percent(std::clamp(mining.drillIntegrity, 0.0, 1.0))),
        panelMetric("Rig O2", miningOxygenValue(mining.rigOxygen.current) + "/" + miningOxygenValue(rigOxygenCapacity)),
        panelMetric("Suit O2", miningOxygenValue(mining.suitOxygen.current) + "/" + miningOxygenValue(mining.suitOxygen.capacity)),
        panelMetric("Rig cargo", std::to_string(carriedCargo)),
        panelMetric("Ship cargo", std::to_string(bankedCargo)),
        panelMetric(text::labels::load, display::fixed(load.currentLoad, 1)),
        panelMetric("Rig Fuel", display::fixed(mining.rigFuel.current, 1) + "/" + display::fixed(mining.rigFuel.capacity, 1)),
        panelMetric("Ship Hold", std::to_string(shipHoldUsed(state)) + "/" + std::to_string(shipHoldCapacity(state, catalog))),
        panelMetric(text::labels::depth, std::to_string(mining.depthZone)),
        panelMetric("Arena", std::string(miningActName(arena.act)) + " L" + std::to_string(arena.difficulty)),
        panelMetric("Seed", std::to_string(arena.seed)),
        panelMetric("Ruleset", "v" + std::to_string(arena.rulesVersion)),
        panelMetric(text::labels::drillHeat, display::percent(mining.drillHeat)),
        panelMetric("Enemies", std::to_string(activeMiningEnemyCount(mining))),
        panelMetric("Support Drones", drones.names.empty() ? "0" : std::to_string(static_cast<int>(drones.names.size()))),
        panelMetric("Synergies", std::to_string(static_cast<int>(drones.synergyNames.size()))),
        panelMetric("Build", drones.signatureName.empty() ? "Solo" : drones.signatureName),
        panelMetric(text::labels::targetMaterial, std::string(miningMaterialName(mining.targetMaterial))),
        panelMetric(text::labels::toughness, miningToughnessValue(mining))
    };
    if (mining.artifact.present) {
        presentation.metrics.push_back(panelMetric("Artifact", miningArtifactStateLabel(mining.artifact.state)));
        const bool artifactTethered = mining.artifact.tethered;
        const bool operatorRigTethered = mining.operatorRigTethered;
        presentation.metrics.push_back(panelMetric(
            "Tether",
            artifactTethered
                ? "Artifact locked"
                : (operatorRigTethered ? "EVA tow locked" : "Free")));
        presentation.metrics.push_back(panelMetric("Artifact integrity", display::percent(mining.artifact.maxHealth <= 0.0 ? 0.0 : mining.artifact.health / mining.artifact.maxHealth)));
    } else if (mining.operatorRigTethered) {
        presentation.metrics.push_back(panelMetric("Tether", "EVA tow locked"));
    }
    presentation.payloadMetrics = {
        panelMetric("Rig cargo", std::to_string(carriedCargo)),
        panelMetric("Ship cargo", std::to_string(bankedCargo)),
        panelMetric("Rig ore", std::to_string(mining.temporaryMaterials.common + mining.temporaryMaterials.rare + mining.temporaryMaterials.exotic)),
        panelMetric("Ship ore", std::to_string(mining.stowedMaterials.common + mining.stowedMaterials.rare + mining.stowedMaterials.exotic)),
        panelMetric("Rig artifacts", std::to_string(mining.temporaryArtifacts.size())),
        panelMetric("Ship artifacts", std::to_string(mining.stowedArtifacts.size()))
    };
    presentation.combatTitle = drones.signatureName.empty()
        ? (drones.synergyNames.empty() ? "Support command" : drones.synergyNames.front())
        : drones.signatureName;
    presentation.combatDetail = "Cyan shots and blue numbers are your Support Drones. Red/orange shots and red numbers are hostile fire. Keep mining while the build fights.";
    const double alliedCritChance = std::clamp(tuning::mining::alliedCritChance + drones.alliedCritChanceBonus, 0.0, tuning::mining::alliedCritChanceMaximum);
    const double shieldRelief = std::clamp(drones.enemyDamageRelief + drones.environmentalShieldRelief, 0.0, 1.0);
    presentation.combatMetrics = {
        panelMetric("Active build", drones.signatureName.empty() ? (drones.synergyNames.empty() ? "Solo cover" : drones.synergyNames.front()) : drones.signatureName),
        panelMetric("Threats", std::to_string(activeMiningEnemyCount(mining))),
        panelMetric("Allied shots", std::to_string(activeMiningProjectileCount(mining, MiningCombatTeam::Allied))),
        panelMetric("Enemy shots", std::to_string(activeMiningProjectileCount(mining, MiningCombatTeam::Enemy))),
        panelMetric("Crit text", std::to_string(activeMiningCritTextCount(mining))),
        panelMetric("Volley", "x" + std::to_string(1 + drones.sentryVolleyBonus)),
        panelMetric("Crit chance", display::percent(alliedCritChance)),
        panelMetric("Shield", display::percent(shieldRelief)),
        panelMetric("Tuned", std::to_string(tunedMiningDroneCount(state))),
        panelMetric("KOs", std::to_string(mining.enemiesDefeated)),
        panelMetric("Support dmg", display::fixed(mining.defenseDamageDealt, 1)),
        panelMetric("Field dmg", display::fixed(mining.areaControlDamageDealt, 1)),
        panelMetric("Counter", display::fixed(mining.reactiveArmorDamageDealt, 1)),
        panelMetric("Shielded", display::percent(mining.environmentalShieldAbsorbed)),
        panelMetric("Rig dmg", display::percent(mining.enemyDamageTaken))
    };
    presentation.details = {
        detailPresentationRow("Arena identity", std::string(miningActName(arena.act)) + " • Level " + std::to_string(arena.difficulty) + " • Seed " + std::to_string(arena.seed) + " • Ruleset v" + std::to_string(arena.rulesVersion)),
        detailPresentationRow("Band", std::string(miningProgressionBandName(arenaRules.band))),
        detailPresentationRow("New complication", std::string(arenaRules.complication)),
        detailPresentationRow("Recommended counters", std::string(arenaRules.recommendedCounters)),
        detailPresentationRow(
            "Controls",
            evaActive
                ? std::string("WASD/left stick thrusts; mouse/right stick aims; left click/R2 fires; right click/L2 drills; E/X scans; T/Y tethers; F or hold A enters the rig.")
                : std::string("WASD/left stick thrusts and steers the rig drill; left click/R2 drills; E/X scans; T/Y tethers; F or hold A exits for EVA.")),
        detailPresentationRow("Site", std::string(surfaceSiteProfileName(state.run.planetaryExpedition.siteProfile))),
        detailPresentationRow("Rig health", presentation.rigHealth),
        detailPresentationRow("Rig O2", miningOxygenValue(mining.rigOxygen.current) + " / " + miningOxygenValue(rigOxygenCapacity)),
        detailPresentationRow("Suit O2", miningOxygenValue(mining.suitOxygen.current) + " / " + miningOxygenValue(mining.suitOxygen.capacity)),
        detailPresentationRow("Drill power", display::fixed(displayedDrillPower, 1)),
        detailPresentationRow("Drill range", display::fixed(displayedDrillRange, 1) + " cells"),
        detailPresentationRow("Scanner radius", display::fixed(displayedScannerRadius, 1) + " cells"),
        detailPresentationRow(
            "Tether range",
            display::fixed(tuning::mining::artifactTetherRangeCells, 1) + " cells"),
        detailPresentationRow(
            "Sidearm",
            evaActive
                ? ("Infinite fire / " +
                      display::fixed(tuning::mining::operatorSidearmDamage, 1) +
                      " damage / " +
                      display::fixed(tuning::mining::operatorSidearmRangeCells, 1) +
                      " cells / " +
                      display::fixed(tuning::mining::operatorSidearmIntervalSeconds, 2) +
                      "s cadence")
                : std::string("EVA equipment")),
        detailPresentationRow("Rig Fuel", display::fixed(mining.rigFuel.current, 1) + "/" + display::fixed(mining.rigFuel.capacity, 1)),
        detailPresentationRow("Ship Hold", std::to_string(shipHoldUsed(state)) + "/" + std::to_string(shipHoldCapacity(state, catalog))),
        detailPresentationRow(
            "Rig Fuel Loop",
            (installedRigFuelLoopRank(state) > 0
                 ? "RANK " + std::to_string(installedRigFuelLoopRank(state))
                 : std::string("BASE")) +
                " / " + display::percent(rigFuelEfficiency(installedRigFuelLoopRank(state))) + " lower powered consumption"),
        detailPresentationRow(
            "Fuel draw",
            "Powered thrust " + display::fixed(load.fuelConsumptionMultiplier, 2) +
                "x load; drilling draws independently. Idling and coasting are free."),
        detailPresentationRow("Load burden", display::fixed(load.currentLoad, 1) + " load; " + display::fixed(load.freeBuffer, 1) + " free carry; speed " + display::percent(load.speedMultiplier)),
        detailPresentationRow("Support Drone loadout", miningDroneSummary(drones)),
        detailPresentationRow("Build signature", drones.signatureName.empty() ? "None" : drones.signatureName),
        detailPresentationRow("Signature payoff", drones.signatureDetail.empty() ? "Equip complementary Drone Ops roles before the run to activate a signature build." : drones.signatureDetail),
        detailPresentationRow("Active synergies", miningNameListSummary(drones.synergyNames)),
        detailPresentationRow("Combat read", std::string("Support Drones follow, orbit, and defend the controlled actor. Melee enemies rush that anchor; ranged enemies fire from standoff range.")),
        detailPresentationRow("Damage text", std::string("Blue numbers are allied damage. Red numbers are damage to the active Mining Rig or EVA suit. Gold CRIT text is reserved for Support Drone attacks.")),
        detailPresentationRow("Support Drone crits", display::percent(std::clamp(tuning::mining::alliedCritChance + drones.alliedCritChanceBonus, 0.0, tuning::mining::alliedCritChanceMaximum)) + " crit; volley x" + std::to_string(1 + drones.sentryVolleyBonus)),
        detailPresentationRow("Support auto-mining", drones.passiveMiningRate > 0.0 ? ("+" + display::fixed(drones.passiveMiningRate * 60.0, 1) + " common/min") : "None"),
        detailPresentationRow("Passive defense", display::fixed(tuning::mining::baseDefenseDamagePerSecond + drones.sentryDamagePerSecond, 1) + " DPS; " + display::percent(std::clamp(drones.enemyDamageRelief + drones.environmentalShieldRelief, 0.0, 1.0)) + " shield relief"),
        detailPresentationRow("Area control", drones.areaControlDamagePerSecond > 0.0 ? (display::fixed(drones.areaControlDamagePerSecond, 1) + " DPS field; " + display::percent(drones.enemySlow) + " slow") : "None"),
        detailPresentationRow("Reactive armor", drones.reactiveArmorDamagePerSecond > 0.0 ? (display::fixed(drones.reactiveArmorDamagePerSecond, 1) + " DPS on contact") : "None"),
        detailPresentationRow("Site ecology", miningEnemyEcologySummary(mining)),
        detailPresentationRow("Elemental / elite pressure", activeElementalSummary(mining)),
        detailPresentationRow("Elemental exposure", mining.elementalExposureSeconds > 0.0 ? (display::fixed(mining.elementalExposureSeconds, 1) + "s") : "None"),
        detailPresentationRow("Cryo slow", mining.movementSlowSeconds > 0.0 ? (display::percent(1.0 - mining.movementSlowScale) + " for " + display::fixed(mining.movementSlowSeconds, 1) + "s") : "None"),
        detailPresentationRow("Support oxygen reserve", drones.oxygenSeconds > 0.0 ? ("+" + std::to_string(static_cast<int>(std::round(drones.oxygenSeconds))) + "s") : "None"),
        detailPresentationRow("Support stability", drones.hardRockBounceRelief > 0.0 ? display::percent(drones.hardRockBounceRelief) + " less hard-rock bounce" : "None"),
        detailPresentationRow("Hostile tunnels", hostileTunnelSummary(mining.terrain)),
        detailPresentationRow("Active threats", activeThreatSummary(mining)),
        detailPresentationRow("Enemies defeated", std::to_string(mining.enemiesDefeated)),
        detailPresentationRow("Ore efficiency", display::signedPercent(stats.oreYieldChance)),
        detailPresentationRow("Heat control", display::fixed(stats.heatCoolingPerSecond, 2)),
        detailPresentationRow("Drill protection", display::signedPercent(stats.integrityRelief)),
        detailPresentationRow("Survey footprint", std::to_string(stats.terrainWidth) + "x" + std::to_string(stats.terrainHeight)),
        detailPresentationRow("Run target", std::string("Explore, recover physical cargo, and return it to the mothership.")),
        detailPresentationRow("Depth pressure", std::string("Deeper zones have tougher terrain and richer pockets. Ship cargo always returns intact."))
    };
    if (mining.artifact.present) {
        presentation.details.push_back(detailPresentationRow("Artifact recovery", std::string("Expose the object, press T nearby to tether, pull it to the ship ring, and avoid drilling or bouncing it.")));
    }
    if (!mining.active) {
        presentation.commandTitle = "Extraction secured";
        presentation.commandDetail = "All Ship cargo is secured. Departure sequence in progress.";
        presentation.actions = {
            disabledPanelButton("Departure in progress")
        };
    } else if (mining.failurePending) {
        presentation.commandTitle = "Systems offline";
        presentation.commandDetail = "Recall in progress";
        presentation.actions = {
            disabledPanelButton("Drill disabled")
        };
    } else {
        const bool atShip = miningAtReturnZone(mining);
        if (atShip) {
            const int drillRepairCost = miningDrillRepairCost(mining);
            const bool disabledRigAtShip =
                mining.rigDisabled && miningRigAtReturnZone(mining);
            const int actorRepairCost = disabledRigAtShip
                ? miningDroneRepairCost(mining)
                : (evaActive
                ? (mining.operatorIntegrity < 1.0
                        ? static_cast<int>(tuning::mining::operatorIntegrityRepairCommonCost)
                        : 0)
                : miningDroneRepairCost(mining));
            const std::string actorName =
                disabledRigAtShip ? "Mining Rig" : (evaActive ? "suit" : "Mining Rig");
            presentation.commandTitle = "Ship service";
            presentation.commandDetail = disabledRigAtShip
                ? "Shuttle umbilical patch restores 35% rig integrity; reboard or leave"
                : (protectedObjectiveNeedsPulse
                        ? "ANOMALOUS RETURN - PULSE SCANNER [E/X]"
                        : (arenaRules.mechanics.fogAndScanner
                        ? "Repair, scan the site, then leave"
                        : "Repair the rig, then leave"));
            presentation.actions = {
                drillRepairCost <= 0
                    ? disabledPanelButton("Bit ready")
                    : (mining.stowedMaterials.common >= drillRepairCost
                              ? panelActionButton("Repair bit (" + std::to_string(drillRepairCost) + " common)", ui::actions::miningRepairDrill, "ok")
                              : disabledPanelButton("Need " + std::to_string(drillRepairCost) + " common for bit")),
                disabledRigAtShip
                    ? panelActionButton("Shuttle patch Mining Rig (35% integrity)", ui::actions::miningRepairDrone, "ok")
                    : (actorRepairCost <= 0
                    ? disabledPanelButton(disabledRigAtShip || !evaActive ? "Rig ready" : "Suit ready")
                    : (mining.stowedMaterials.common >= actorRepairCost
                              ? panelActionButton("Repair " + actorName + " (" + std::to_string(actorRepairCost) + " common)", ui::actions::miningRepairDrone, "ok")
                              : disabledPanelButton("Need " + std::to_string(actorRepairCost) + " common for " + actorName)))
            };
            if (arenaRules.mechanics.fogAndScanner || protectedObjectiveNeedsPulse) {
                presentation.actions.push_back(
                    panelActionButton(
                        protectedObjectiveNeedsPulse ? "ANOMALOUS RETURN - PULSE SCANNER [E/X]" : text::buttons::pulseScanner,
                        ui::actions::miningScanner,
                        protectedObjectiveNeedsPulse ? "warn anomaly-action" : "warn"));
            }
            if (droneBayUnlocked(state)) {
                presentation.actions.push_back(
                    panelActionButton("Drone loadout", ui::actions::droneOps, "ghost"));
            }
            presentation.actions.push_back(
                panelActionButton("Bank payload", ui::actions::miningStow, "ok"));
            const MiningDroneRecoveryStatus droneRecovery =
                miningDroneRecoveryStatus(mining);
            if (droneRecovery.outstandingDrones > 0) {
                presentation.actions.push_back(panelActionButton(
                    (droneRecovery.recallInProgress ? "Drones returning (" : "Wait for drones (") +
                        std::to_string(droneRecovery.outstandingDrones) + ")",
                    ui::actions::miningWaitForDrones,
                    "ghost"));
                presentation.actions.push_back(panelActionButton(
                    "Leave now - lose " +
                        std::to_string(droneRecovery.outstandingDrones) +
                        (droneRecovery.outstandingDrones == 1 ? " drone / " : " drones / ") +
                        std::to_string(droneRecovery.outstandingCargoMass) + " mass",
                    ui::actions::miningDepart,
                    "warn"));
            } else {
                presentation.actions.push_back(
                    panelActionButton("Depart planet", ui::actions::miningDepart, "warn"));
            }
        } else {
            const MiningArtifactObject& artifact = mining.artifact;
            const MiningTetherTargetResolution tetherTarget = resolveMiningTetherTarget(mining);
            presentation.commandTitle = "Commands";
            if (arenaRules.mechanics.fogAndScanner || protectedObjectiveNeedsPulse) {
                presentation.commandHints.push_back(protectedObjectiveNeedsPulse
                    ? "ANOMALOUS RETURN - PULSE SCANNER [E/X]"
                    : "Pulse Scanner (E) - Reveals nearby resources");
                presentation.actions.push_back(
                    panelActionButton(
                        protectedObjectiveNeedsPulse ? "PULSE SCANNER [E/X]" : text::buttons::pulseScanner,
                        ui::actions::miningScanner,
                        protectedObjectiveNeedsPulse ? "warn anomaly-action" : "warn"));
            }
            PanelButtonPresentation tetherAction = disabledPanelButton("No tether target");
            if (artifact.tethered) {
                tetherAction = panelActionButton("Release artifact tether", ui::actions::miningTether, "warn");
            } else if (mining.operatorRigTethered) {
                tetherAction = panelActionButton("Release Mining Rig tether", ui::actions::miningTether, "warn");
            } else if (tetherTarget.target == MiningTetherTarget::Artifact &&
                       tetherTarget.blocker == MiningTetherBlocker::None) {
                tetherAction = panelActionButton("Tether artifact", ui::actions::miningTether, "warn");
            } else if (tetherTarget.target == MiningTetherTarget::MiningRig) {
                tetherAction = panelActionButton(
                    mining.rigDisabled ? "Tether disabled Mining Rig" : "Tether Mining Rig",
                    ui::actions::miningTether,
                    "warn");
            } else if (tetherTarget.target == MiningTetherTarget::FuelCell) {
                tetherAction = panelActionButton("Tether fuel cell", ui::actions::miningTether, "warn");
            } else if (tetherTarget.blocker == MiningTetherBlocker::ArtifactGateLocked) {
                tetherAction = disabledPanelButton("Complete gate to tether artifact");
            } else if (tetherTarget.blocker == MiningTetherBlocker::ArtifactUnexposed) {
                tetherAction = disabledPanelButton("Scan or expose artifact");
            } else if (tetherTarget.blocker == MiningTetherBlocker::ArtifactOutOfRange) {
                tetherAction = disabledPanelButton("Move within artifact tether range");
            } else if (tetherTarget.blocker == MiningTetherBlocker::SuitRequired) {
                tetherAction = disabledPanelButton("EXIT RIG - EVA REQUIRED");
            } else if (tetherTarget.blocker == MiningTetherBlocker::RigDifferentDepth) {
                tetherAction = disabledPanelButton("Mining Rig is on another depth");
            } else if (tetherTarget.blocker == MiningTetherBlocker::RigOutOfRange) {
                tetherAction = disabledPanelButton("Move within Mining Rig tether range");
            }
            // Keep the command discoverable even when the current target is
            // out of range; the same T action can tether either object.
            if (!tetherAction.enabled) {
                tetherAction.actionId = std::string(ui::actions::miningTether);
            }
            presentation.commandHints.push_back(
                evaActive
                    ? "Tether (T) - Attach to the nearest exposed artifact or same-depth Mining Rig"
                    : "Tether (T) - Attach to an exposed nearby artifact");
            presentation.actions.push_back(std::move(tetherAction));
            if (!mining.miniDrones.empty()) {
                presentation.commandHints.push_back("Assigned Support Drones (Automatic) - Execute their roles automatically");
            }
            presentation.actions.push_back(panelActionButton(text::buttons::abortMining, ui::actions::miningAbort, "danger"));
        }
    }
    return presentation;
}

inline MiningHudPresentation miningHudPresentation(const GameState& state, const ContentCatalog& catalog)
{
    const MiningRunState& mining = state.run.mining;
    const MiningRunPresentation run = miningRunPresentation(state, catalog);
    const MiningLoadStats load = miningLoadStats(state, catalog);
    const MiningArenaRules arenaRules = resolveMiningArenaRules({
        mining.arenaMetadata.act,
        mining.arenaMetadata.difficulty,
        mining.arenaMetadata.seed});
    const bool evaActive =
        mining.operatorMode == MiningOperatorMode::Jetpack &&
        mining.operatorPresent;
    const auto loadBandCss = [](RigLoadBand band) -> std::string {
        switch (band) {
        case RigLoadBand::Light: return "load load-light";
        case RigLoadBand::Standard: return "load load-standard";
        case RigLoadBand::Laden: return "load load-laden";
        case RigLoadBand::Packrat: return "load load-packrat";
        case RigLoadBand::Full: return "load load-full";
        }
        return "load";
    };

    auto metricValue = [&run](std::string_view label, std::string fallback = "--") {
        const auto found = std::find_if(run.metrics.begin(), run.metrics.end(), [label](const PanelMetricPresentation& metric) {
            return metric.label == label;
        });
        return found == run.metrics.end() ? std::move(fallback) : found->value;
    };
    auto payloadValue = [&run](std::string_view label, std::string fallback = "0") {
        const auto found = std::find_if(run.payloadMetrics.begin(), run.payloadMetrics.end(), [label](const PanelMetricPresentation& metric) {
            return metric.label == label;
        });
        return found == run.payloadMetrics.end() ? std::move(fallback) : found->value;
    };
    auto copyAction = [](const PanelButtonPresentation& source, std::string label, std::string extraClass = {}) {
        PanelButtonPresentation action = source;
        action.label = std::move(label);
        if (!extraClass.empty()) {
            if (!action.cssClass.empty()) {
                action.cssClass += " ";
            }
            action.cssClass += std::move(extraClass);
        }
        return action;
    };
    auto findAction = [&run](std::string_view actionId) -> const PanelButtonPresentation* {
        const auto found = std::find_if(run.actions.begin(), run.actions.end(), [actionId](const PanelButtonPresentation& action) {
            return action.actionId == actionId;
        });
        return found == run.actions.end() ? nullptr : &*found;
    };

    std::string drillCssClass = "drill mining-vital-drill";
    if (mining.drillIntegrity <= 0.0) {
        drillCssClass += " mining-vital-broken";
    }

    MiningHudPresentation presentation;
    presentation.runLabel = std::string(miningActName(mining.arenaMetadata.act))
        + " \xE2\x80\xA2 LEVEL " + std::to_string(mining.arenaMetadata.difficulty);
    const int currentDepth = std::max(0, mining.depthZone);
    const ScenarioObjectivePresentation scenarioObjective = scenarioObjectiveForMining(state, catalog);
    if (scenarioObjective.available &&
        scenarioObjective.state != ScenarioStepState::Complete &&
        scenarioObjective.required > 0) {
        const int required = std::max(1, scenarioObjective.required);
        const int current = std::clamp(scenarioObjective.current, 0, required);
        presentation.objective = scenarioObjective.title + " \xE2\x80\xA2 " + std::to_string(current) + "/" +
            std::to_string(required) + " \xE2\x80\xA2 ";
    } else {
        presentation.objective = currentDepth == 0
            ? "SURFACE \xE2\x80\xA2 "
            : "DEPTH +" + std::to_string(currentDepth) + " \xE2\x80\xA2 ";
    }
    if (mining.gate.type == MiningGateType::SurveyTriangulation &&
        !mining.gate.surveyComplete) {
        const int completed = static_cast<int>(std::count_if(
            mining.gate.markers.begin(),
            mining.gate.markers.end(),
            [](const MiningGateMarker& marker) { return marker.activated; }));
        presentation.objective = "SIGNAL TRIANGULATION " +
            std::to_string(completed) + "/3";
    }
    if (currentDepth == 0) {
        presentation.objective += "SHIP HERE";
    } else {
        presentation.objective += "SHIP \xE2\x86\x91 " + std::to_string(currentDepth);
    }
    if (mining.swarm.enabled) {
        if (mining.depthZone != mining.swarm.depthZone) {
            presentation.objective = "SWARM SIGNAL \xE2\x80\xA2 DEPTH +" +
                std::to_string(mining.swarm.depthZone) + " \xE2\x80\xA2 ARTIFACT " +
                display::percent(mining.swarm.artifactChance);
        } else if (mining.swarm.cacheExposed) {
            presentation.objective = mining.swarm.cacheClaimed
                ? "SWARM CACHE SECURED \xE2\x80\xA2 RETURN TO SHIP"
                : (mining.swarm.bonusArtifactRolled
                    ? "SWARM BROKEN \xE2\x80\xA2 CACHE + ARTIFACT SIGNAL"
                    : "SWARM BROKEN \xE2\x80\xA2 CACHE EXPOSED");
        } else if (mining.swarm.alerted && mining.swarm.alertSeconds > 0.0) {
            presentation.objective = "SWARM NEST DETECTED \xE2\x80\xA2 ASCEND TO DISENGAGE";
        } else if (mining.swarm.wave > 0) {
            presentation.objective = "SWARM " + std::to_string(mining.swarm.wave) + "/3 \xE2\x80\xA2 " +
                std::to_string(activeMiningSwarmEnemyCount(mining)) + " HOSTILES";
        }
    }
    presentation.atShip = mining.active && miningAtReturnZone(mining);
    presentation.failurePending = run.failurePending;
    presentation.failureTitle = run.failureTitle;
    presentation.failureBody = run.failureBody;
    presentation.details = run.details;
    presentation.vitals = {{
        {evaActive ? "SUIT O2" : "RIG O2",
            miningOxygenValue(miningActiveOxygenSeconds(mining)), "oxygen", {}, {}},
        {"RIG FUEL", display::fixed(mining.rigFuel.current, 1), "fuel", {}, {}},
        {evaActive ? "SUIT INTEGRITY" : "RIG INTEGRITY",
            evaActive ? display::percent(mining.operatorIntegrity) : display::percent(mining.droneHealth),
            std::move(drillCssClass),
            (arenaRules.mechanics.drillHeat && (mining.drilling || mining.drillHeat > 0.01)) ? "HEAT" : "",
            (arenaRules.mechanics.drillHeat && (mining.drilling || mining.drillHeat > 0.01)) ? metricValue(text::labels::drillHeat, "0%") : ""},
        {"RIG LOAD",
            std::to_string(static_cast<int>(std::round(load.currentLoad))) + "/" +
                std::to_string(static_cast<int>(std::round(load.capacity))),
            loadBandCss(load.band),
            std::string(rigLoadBandName(load.band)),
            "SHIP " + std::to_string(shipHoldUsed(state)) + "/" +
                std::to_string(shipHoldCapacity(state, catalog))}
    }};
    const auto droneOre = [&](auto member) {
        int total = 0;
        for (const MiningMiniDroneAgent& agent : mining.miniDrones) {
            total += std::max(0, agent.haulMaterials.*member);
        }
        return total;
    };
    presentation.oreManifest.legend = "RIG / DRONES / SHIP";
    presentation.oreManifest.ores = {{
        {"COMMON", std::to_string(std::max(0, mining.temporaryMaterials.common)) + " / " +
            std::to_string(droneOre(&MaterialInventory::common)) + " / " + std::to_string(std::max(0, mining.stowedMaterials.common)), "common", {}, {}},
        {"RARE", std::to_string(std::max(0, mining.temporaryMaterials.rare)) + " / " +
            std::to_string(droneOre(&MaterialInventory::rare)) + " / " + std::to_string(std::max(0, mining.stowedMaterials.rare)), "rare", {}, {}},
        {"EXOTIC", std::to_string(std::max(0, mining.temporaryMaterials.exotic)) + " / " +
            std::to_string(droneOre(&MaterialInventory::exotic)) + " / " + std::to_string(std::max(0, mining.stowedMaterials.exotic)), "exotic", {}, {}}
    }};
    const bool triangulationConcealed =
        mining.gate.type == MiningGateType::SurveyTriangulation &&
        !mining.gate.surveyComplete;
    const int triangulationCompleted = static_cast<int>(std::count_if(
        mining.gate.markers.begin(),
        mining.gate.markers.end(),
        [](const MiningGateMarker& marker) { return marker.activated; }));
    presentation.payload = {{
        {"SHIP", payloadValue("Ship cargo"), "stowed", {}, {}},
        {triangulationConcealed ? "SIGNAL" : "ARTIFACT",
            triangulationConcealed
                ? std::to_string(triangulationCompleted) + "/3"
                : (mining.artifact.present ? metricValue("Artifact integrity", "0%") : "--"),
            triangulationConcealed ? "signal active" : (mining.artifact.present ? "artifact active" : "artifact"),
            {}, {}}
    }};

    if (!mining.active || mining.failurePending) {
        presentation.actions = run.actions;
        return presentation;
    }

    if (presentation.atShip) {
        if (miningDrillRepairCost(mining) > 0) {
            if (const PanelButtonPresentation* repair = findAction(ui::actions::miningRepairDrill)) {
                presentation.actions.push_back(copyAction(*repair, "REPAIR DRILL", "mining-repair-action"));
            } else {
                presentation.actions.push_back(disabledPanelButton("REPAIR DRILL"));
            }
        }
        const bool disabledRigAtShip =
            mining.rigDisabled && miningRigAtReturnZone(mining);
        const bool actorNeedsRepair = disabledRigAtShip
            ? miningDroneRepairCost(mining) > 0
            : (evaActive
                    ? mining.operatorIntegrity < 1.0
                    : miningDroneRepairCost(mining) > 0);
        if (actorNeedsRepair) {
            if (const PanelButtonPresentation* repair = findAction(ui::actions::miningRepairDrone)) {
                presentation.actions.push_back(copyAction(
                    *repair,
                    disabledRigAtShip
                        ? "SHUTTLE PATCH RIG // 35%"
                        : (!evaActive ? "REPAIR RIG" : "REPAIR SUIT"),
                    "mining-repair-action"));
            } else {
                presentation.actions.push_back(disabledPanelButton(
                    disabledRigAtShip || !evaActive ? "REPAIR RIG" : "REPAIR SUIT"));
            }
        }
        if (const PanelButtonPresentation* scanner = findAction(ui::actions::miningScanner)) {
            presentation.actions.push_back(copyAction(*scanner, "PULSE SCANNER", "mining-scan-action"));
        }
        if (const PanelButtonPresentation* stow = findAction(ui::actions::miningStow)) {
            presentation.actions.push_back(copyAction(*stow, "BANK PAYLOAD", "mining-bank-action"));
        }
        if (const PanelButtonPresentation* depart = findAction(ui::actions::miningDepart)) {
            presentation.actions.push_back(copyAction(*depart, "DEPART PLANET", "mining-depart-action"));
        }
        return presentation;
    }

    if (const PanelButtonPresentation* scanner = findAction(ui::actions::miningScanner)) {
        presentation.actions.push_back(copyAction(*scanner, "PULSE SCANNER", "mining-scan-action"));
    }
    if (const PanelButtonPresentation* tether = findAction(ui::actions::miningTether)) {
        std::string hudLabel = "TETHER TARGET";
        if (tether->label.find("fuel") != std::string::npos || tether->label.find("Fuel") != std::string::npos) {
            hudLabel = tether->label.find("Release") != std::string::npos
                ? "RELEASE FUEL CELL"
                : "TETHER FUEL CELL";
        } else if (tether->label.find("artifact") != std::string::npos || tether->label.find("Artifact") != std::string::npos) {
            hudLabel = tether->label.find("Release") != std::string::npos
                ? "RELEASE ARTIFACT"
                : "TETHER ARTIFACT";
        } else if (tether->label.find("Release") != std::string::npos) {
            hudLabel = "RELEASE TARGET";
        }
        presentation.actions.push_back(copyAction(*tether, std::move(hudLabel), "mining-tether-action"));
    } else {
        PanelButtonPresentation unavailableTether = disabledPanelButton("TETHER TARGET");
        unavailableTether.actionId = std::string(ui::actions::miningTether);
        unavailableTether.cssClass = "mining-tether-action";
        presentation.actions.push_back(std::move(unavailableTether));
    }
    if (const PanelButtonPresentation* recall = findAction(ui::actions::miningAbort)) {
        presentation.actions.push_back(copyAction(*recall, "EMERGENCY RECALL", "mining-recall-action"));
    }
    return presentation;
}

} // namespace rocket
