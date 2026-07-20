#pragma once

#include "core/DetailPresentation.h"
#include "core/GameFormat.h"
#include "core/GameText.h"
#include "core/GameUi.h"
#include "core/MiningSystem.h"
#include "core/PanelPresentation.h"

#include <algorithm>
#include <cmath>
#include <string>
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
    return static_cast<int>(std::count_if(state.meta.droneUpgrades.begin(), state.meta.droneUpgrades.end(), [](const DroneUpgradeRecord& record) {
        return record.level > 1;
    }));
}

inline std::string activeElementalSummary(const MiningRunState& mining)
{
    for (const MiningEnemy& enemy : mining.enemies) {
        if (enemy.active && enemy.type == MiningEnemyType::Elemental && enemy.affinity != MiningElementalAffinity::None) {
            return std::string(miningElementalAffinityName(enemy.affinity)) + " elemental";
        }
    }
    return "None";
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
    const SurfaceExpeditionState& surface = state.run.surfaceExpedition;
    const MiningArenaMetadata& arena = mining.arenaMetadata;
    const MiningArenaRules arenaRules = resolveMiningArenaRules({arena.act, arena.difficulty, arena.seed});
    const bool arkKnown = arkDiscovered(state);
    const MiningLoadStats load = miningLoadStats(state, catalog);
    const int carriedCargo = miningCarriedCargo(mining);
    const int bankedCargo = miningBankedCargo(mining);
    const double extractionDelta = std::clamp(
        mining.hazardDelta + static_cast<double>(std::max(0, bankedCargo)) * tuning::mining::cargoExtractionRiskScale - stats.extractionRiskRelief,
        0.0,
        tuning::mining::maxMiningHazardDelta);
    MiningRunPresentation presentation;
    presentation.failurePending = mining.failurePending;
    presentation.failureTitle = "Emergency recall";
    presentation.failureBody = mining.failureMessage.empty()
        ? std::string("Mining drone recall is in progress.")
        : mining.failureMessage;
    presentation.rigHealthRatio = std::clamp(mining.droneHealth, 0.0, 1.0);
    presentation.rigHealth = display::percent(presentation.rigHealthRatio);
    presentation.metrics = {
        panelMetric(text::labels::droneHealth, presentation.rigHealth),
        panelMetric(text::labels::drillBit, mining.drillIntegrity <= 0.0 ? "Broken" : display::percent(std::clamp(mining.drillIntegrity, 0.0, 1.0))),
        panelMetric(text::labels::oxygen, miningOxygenValue(mining.oxygenSeconds)),
        panelMetric(text::labels::carried, std::to_string(carriedCargo)),
        panelMetric(text::labels::banked, std::to_string(bankedCargo)),
        panelMetric(text::labels::load, display::fixed(load.currentLoad, 1)),
        panelMetric(text::fuel::reserveLabel(arkKnown), std::to_string(surface.sharedFuel) + "/" + std::to_string(std::max(1, surface.sharedFuelCapacity))),
        panelMetric("Next fuel", miningFuelCycleValue(mining.fuelCycleProgress)),
        panelMetric(text::labels::depth, std::to_string(mining.depthZone)),
        panelMetric("Arena", std::string(miningActName(arena.act)) + " L" + std::to_string(arena.difficulty)),
        panelMetric("Seed", std::to_string(arena.seed)),
        panelMetric("Ruleset", "v" + std::to_string(arena.rulesVersion)),
        panelMetric(text::labels::drillHeat, display::percent(mining.drillHeat)),
        panelMetric(text::labels::extractionRisk, display::signedPercent(extractionDelta)),
        panelMetric("Enemies", std::to_string(activeMiningEnemyCount(mining))),
        panelMetric("Drones", drones.names.empty() ? "0" : std::to_string(static_cast<int>(drones.names.size()))),
        panelMetric("Synergies", std::to_string(static_cast<int>(drones.synergyNames.size()))),
        panelMetric("Build", drones.signatureName.empty() ? "Solo" : drones.signatureName),
        panelMetric(text::labels::targetMaterial, std::string(miningMaterialName(mining.targetMaterial))),
        panelMetric(text::labels::toughness, miningToughnessValue(mining))
    };
    if (mining.artifact.present) {
        presentation.metrics.push_back(panelMetric("Artifact", miningArtifactStateLabel(mining.artifact.state)));
        presentation.metrics.push_back(panelMetric("Tether", mining.artifact.tethered ? "Locked" : "Free"));
        presentation.metrics.push_back(panelMetric("Artifact integrity", display::percent(mining.artifact.maxHealth <= 0.0 ? 0.0 : mining.artifact.health / mining.artifact.maxHealth)));
    }
    presentation.payloadMetrics = {
        panelMetric("Carried cargo", std::to_string(carriedCargo)),
        panelMetric("Banked cargo", std::to_string(bankedCargo)),
        panelMetric("Carried mats", std::to_string(mining.temporaryMaterials.common + mining.temporaryMaterials.rare + mining.temporaryMaterials.exotic)),
        panelMetric("Banked mats", std::to_string(mining.stowedMaterials.common + mining.stowedMaterials.rare + mining.stowedMaterials.exotic)),
        panelMetric("Carried artifacts", std::to_string(mining.temporaryArtifacts.size())),
        panelMetric("Banked artifacts", std::to_string(mining.stowedArtifacts.size()))
    };
    presentation.combatTitle = drones.signatureName.empty()
        ? (drones.synergyNames.empty() ? "Swarm command" : drones.synergyNames.front())
        : drones.signatureName;
    presentation.combatDetail = "Cyan shots and blue numbers are your mini-drones. Red/orange shots and red numbers are hostile fire. Keep mining while the build fights.";
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
        panelMetric("Drone dmg", display::fixed(mining.defenseDamageDealt, 1)),
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
        detailPresentationRow("Controls", std::string("WASD/arrows turn and thrust the forward drill; mouse hold or Space drills; E scans; T tethers artifacts; return to the ship to bank, refill oxygen, and Leave.")),
        detailPresentationRow("Site", std::string(surfaceSiteProfileName(surface.siteProfile))),
        detailPresentationRow("Drill power", display::fixed(stats.power, 1)),
        detailPresentationRow("Scanner radius", display::fixed(stats.scannerRadius, 1)),
        detailPresentationRow(text::fuel::reserveLabel(arkKnown), std::to_string(surface.sharedFuel) + "/" + std::to_string(std::max(1, surface.sharedFuelCapacity)) + " available for this dig"),
        detailPresentationRow("Fuel spent this dig", std::to_string(mining.fuelSpent)),
        detailPresentationRow("Fuel draw", text::fuel::drawDetail(arkKnown) + " Consumption rate " + display::fixed(load.fuelConsumptionMultiplier, 2) + "x."),
        detailPresentationRow("Load burden", display::fixed(load.currentLoad, 1) + " load; " + display::fixed(load.freeBuffer, 1) + " free carry; speed " + display::percent(load.speedMultiplier)),
        detailPresentationRow("Drone loadout", miningDroneSummary(drones)),
        detailPresentationRow("Build signature", drones.signatureName.empty() ? "None" : drones.signatureName),
        detailPresentationRow("Signature payoff", drones.signatureDetail.empty() ? "Equip complementary Drone Ops roles before the run to activate a signature build." : drones.signatureDetail),
        detailPresentationRow("Active synergies", miningNameListSummary(drones.synergyNames)),
        detailPresentationRow("Combat read", std::string("Your rig and mini-drones are cyan/blue. Melee enemies rush the rig; ranged enemies fire red or elemental shots from standoff range.")),
        detailPresentationRow("Damage text", std::string("Blue numbers are your mini-drones hurting enemies. Red numbers are rig damage. Gold CRIT text marks a critical hit.")),
        detailPresentationRow("Drone crits", display::percent(std::clamp(tuning::mining::alliedCritChance + drones.alliedCritChanceBonus, 0.0, tuning::mining::alliedCritChanceMaximum)) + " crit; volley x" + std::to_string(1 + drones.sentryVolleyBonus)),
        detailPresentationRow("Drone auto-mining", drones.passiveMiningRate > 0.0 ? ("+" + display::fixed(drones.passiveMiningRate * 60.0, 1) + " common/min") : "None"),
        detailPresentationRow("Passive defense", display::fixed(tuning::mining::baseDefenseDamagePerSecond + drones.sentryDamagePerSecond, 1) + " DPS; " + display::percent(std::clamp(drones.enemyDamageRelief + drones.environmentalShieldRelief, 0.0, 1.0)) + " shield relief"),
        detailPresentationRow("Area control", drones.areaControlDamagePerSecond > 0.0 ? (display::fixed(drones.areaControlDamagePerSecond, 1) + " DPS field; " + display::percent(drones.enemySlow) + " slow") : "None"),
        detailPresentationRow("Reactive armor", drones.reactiveArmorDamagePerSecond > 0.0 ? (display::fixed(drones.reactiveArmorDamagePerSecond, 1) + " DPS on contact") : "None"),
        detailPresentationRow("Elemental contact", activeElementalSummary(mining)),
        detailPresentationRow("Elemental exposure", mining.elementalExposureSeconds > 0.0 ? (display::fixed(mining.elementalExposureSeconds, 1) + "s") : "None"),
        detailPresentationRow("Cryo slow", mining.movementSlowSeconds > 0.0 ? (display::percent(1.0 - mining.movementSlowScale) + " for " + display::fixed(mining.movementSlowSeconds, 1) + "s") : "None"),
        detailPresentationRow("Drone oxygen reserve", drones.oxygenSeconds > 0.0 ? ("+" + std::to_string(static_cast<int>(std::round(drones.oxygenSeconds))) + "s") : "None"),
        detailPresentationRow("Drone stability", drones.hardRockBounceRelief > 0.0 ? display::percent(drones.hardRockBounceRelief) + " less hard-rock bounce" : "None"),
        detailPresentationRow("Hostile tunnels", hostileTunnelSummary(mining.terrain)),
        detailPresentationRow("Active threats", activeThreatSummary(mining)),
        detailPresentationRow("Enemies defeated", std::to_string(mining.enemiesDefeated)),
        detailPresentationRow("Ore efficiency", display::signedPercent(stats.oreYieldChance)),
        detailPresentationRow("Heat control", display::fixed(stats.heatCoolingPerSecond, 2)),
        detailPresentationRow("Drill protection", display::signedPercent(stats.integrityRelief)),
        detailPresentationRow("Survey footprint", std::to_string(stats.terrainWidth) + "x" + std::to_string(stats.terrainHeight)),
        detailPresentationRow("Run target", text::fuel::miningRunTarget(arkKnown)),
        detailPresentationRow("Depth pressure", std::string("Deeper zones have tougher terrain, richer pockets, and more extraction risk."))
    };
    if (mining.artifact.present) {
        presentation.details.push_back(detailPresentationRow("Artifact recovery", std::string("Expose the object, press T nearby to tether, pull it to the ship ring, and avoid drilling or bouncing it.")));
    }
    if (!mining.active) {
        presentation.commandTitle = "Extraction secured";
        presentation.commandDetail = "Payload banked. Departure sequence in progress.";
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
            const int droneRepairCost = miningDroneRepairCost(mining);
            presentation.commandTitle = "Ship service";
            presentation.commandDetail = "Repair, scan the site, then leave";
            presentation.actions = {
                drillRepairCost <= 0
                    ? disabledPanelButton("Bit ready")
                    : (mining.stowedMaterials.common >= drillRepairCost
                              ? panelActionButton("Repair bit (" + std::to_string(drillRepairCost) + " common)", ui::actions::miningRepairDrill, "ok")
                              : disabledPanelButton("Need " + std::to_string(drillRepairCost) + " common for bit")),
                droneRepairCost <= 0
                    ? disabledPanelButton("Drone ready")
                    : (mining.stowedMaterials.common >= droneRepairCost
                              ? panelActionButton("Repair drone (" + std::to_string(droneRepairCost) + " common)", ui::actions::miningRepairDrone, "ok")
                              : disabledPanelButton("Need " + std::to_string(droneRepairCost) + " common for drone")),
                // The scanner is useful before the rig leaves the shuttle. Keep
                // the same visible action as the E key so the opening HUD never
                // appears to require a hidden input before surveying.
                panelActionButton(text::buttons::pulseScanner, ui::actions::miningScanner, "warn"),
                panelActionButton(text::buttons::stowPayload, ui::actions::miningStow, "ok")
            };
        } else {
            const MiningArtifactObject& artifact = mining.artifact;
            const bool artifactRecoverable = artifact.present
                && artifact.state != MiningArtifactState::Delivered
                && artifact.state != MiningArtifactState::Destroyed;
            const bool artifactExposed = artifact.revealed || artifact.state == MiningArtifactState::Loose;
            const double artifactDistance = artifactRecoverable
                ? std::hypot(artifact.x - mining.droneX, artifact.y - mining.droneY)
                : 0.0;
            presentation.commandTitle = "Commands";
            presentation.commandHints.push_back("Pulse Scanner (E) - Reveals nearby resources");
            presentation.actions.push_back(panelActionButton(text::buttons::pulseScanner, ui::actions::miningScanner, "warn"));
            if (artifact.present) {
                PanelButtonPresentation tetherAction = disabledPanelButton("No tether target");
                if (artifact.tethered) {
                    tetherAction = panelActionButton("Release tether", ui::actions::miningTether, "warn");
                } else if (artifactRecoverable && !artifactExposed) {
                    tetherAction = disabledPanelButton("Scan or expose artifact");
                } else if (artifactRecoverable && artifactDistance > tuning::mining::artifactTetherRangeCells) {
                    tetherAction = disabledPanelButton("Move within tether range");
                } else if (artifactRecoverable) {
                    tetherAction = panelActionButton(text::buttons::tetherArtifact, ui::actions::miningTether, "warn");
                }
                // Keep the command discoverable once artifact play is active,
                // even when the current target is not yet meaningful.
                if (!tetherAction.enabled) {
                    tetherAction.actionId = std::string(ui::actions::miningTether);
                }
                presentation.commandHints.push_back("Tether Artifact (T) - Requires an exposed nearby target");
                presentation.actions.push_back(std::move(tetherAction));
            }
            if (!mining.miniDrones.empty()) {
                presentation.commandHints.push_back("Assigned Drones (Automatic) - Execute their roles automatically");
            }
            presentation.actions.push_back(panelActionButton(text::buttons::abortMining, ui::actions::miningAbort, "danger"));
        }
    }
    return presentation;
}

} // namespace rocket
