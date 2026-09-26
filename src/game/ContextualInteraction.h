#pragma once

#include "core/ExpeditionSystem.h"
#include "core/MiningSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace rocket {

enum class ContextualTargetKind { None, Artifact, Rig, FuelCell, Wreck };

struct ContextualInteraction {
    ContextualTargetKind kind = ContextualTargetKind::None;
    std::string label;
    std::string requirement;
    std::string action;
    double x = 0.0;
    double y = 0.0;
    std::uint64_t wreckId = 0;

    bool visible() const { return kind != ContextualTargetKind::None; }
    bool enabled() const { return !action.empty(); }
};

// Mirrors the authoritative tether resolver. A nearby blocked target may
// teach one prerequisite, but a distant or unrevealed object never advertises
// an action that pressing T/North cannot perform.
inline ContextualInteraction miningContextualInteraction(const MiningRunState& mining)
{
    if (!mining.active || mining.failurePending || miningAtReturnZone(mining)) return {};
    const MiningTetherTargetResolution target = resolveMiningTetherTarget(mining);
    ContextualInteraction view;
    if (mining.artifact.tethered) {
        view.kind = ContextualTargetKind::Artifact;
        view.label = "Release artifact";
        view.action = "mining_tether";
        view.x = mining.artifact.x;
        view.y = mining.artifact.y;
        return view;
    }
    if (mining.operatorRigTethered) {
        view.kind = ContextualTargetKind::Rig;
        view.label = "Release rig";
        view.action = "mining_tether";
        view.x = mining.droneX;
        view.y = mining.droneY;
        return view;
    }
    const auto tetheredFuel = std::find_if(mining.looseObjects.begin(), mining.looseObjects.end(), [](const MiningLooseObject& object) {
        return object.active && object.kind == MiningLooseObjectKind::FuelCell && object.tethered;
    });
    if (tetheredFuel != mining.looseObjects.end()) {
        view.kind = ContextualTargetKind::FuelCell;
        view.label = "Release rig fuel cell";
        view.action = "mining_tether";
        view.x = tetheredFuel->x;
        view.y = tetheredFuel->y;
        return view;
    }
    if (target.target == MiningTetherTarget::Artifact) {
        view.kind = ContextualTargetKind::Artifact;
        view.label = "Tether artifact";
        view.x = mining.artifact.x;
        view.y = mining.artifact.y;
        if (target.blocker == MiningTetherBlocker::SuitRequired) view.requirement = "Exit rig";
        else if (target.blocker == MiningTetherBlocker::ArtifactGateLocked) view.requirement = "Clear seal";
        else view.action = "mining_tether";
    } else if (target.target == MiningTetherTarget::MiningRig) {
        view.kind = ContextualTargetKind::Rig;
        view.label = mining.rigDisabled ? "Tow disabled rig" : "Tether rig";
        view.action = "mining_tether";
        view.x = mining.droneX;
        view.y = mining.droneY;
    } else if (target.target == MiningTetherTarget::FuelCell) {
        const auto found = std::find_if(mining.looseObjects.begin(), mining.looseObjects.end(), [&](const MiningLooseObject& object) {
            return object.active && object.persistentId == target.fuelCellId;
        });
        if (found != mining.looseObjects.end()) {
            view.kind = ContextualTargetKind::FuelCell;
            view.label = "Tether rig fuel cell";
            view.action = "mining_tether";
            view.x = found->x;
            view.y = found->y;
        }
    }
    return view;
}

inline ContextualInteraction flightWreckInteraction(
    const PersistentExpeditionState& expedition,
    const FlightRunState& flight,
    const SystemDefinition& system,
    std::string_view trackedTarget)
{
    ContextualInteraction best;
    if (!flight.active || flight.mode == FlightMode::Landing || flight.docking.active) return best;
    auto ship = expedition.location;
    captureSystemLocation(ship, flight);
    ship = convertSystemFrame(ship, CoordinateFrame::System, "", system);
    double bestDistance = 0.0;
    int bestPriority = -1;
    for (const WreckState& wreck : expedition.wrecks) {
        if (!wreck.location.systemId.empty() && !ship.systemId.empty()
            && wreck.location.systemId != ship.systemId) continue;
        if (!canSalvageWreck(expedition, flight, system, wreck.id, false)) continue;
        const auto place = convertSystemFrame(wreck.location, CoordinateFrame::System, "", system);
        const double distanceToWreck = std::hypot(ship.position.x - place.position.x,
            ship.position.y - place.position.y);
        const bool trackedArtifact = trackedTarget == "wreck:" + std::to_string(wreck.id)
            && wreckCarriesArtifact(expedition, wreck.id);
        const bool speedMatched = canSalvageWreck(expedition, flight, system, wreck.id);
        const int priority = trackedArtifact ? 2 : speedMatched ? 1 : 0;
        if (best.visible() && (priority < bestPriority || (priority == bestPriority && distanceToWreck >= bestDistance))) continue;
        best.kind = ContextualTargetKind::Wreck;
        best.label = "Salvage wreck";
        best.wreckId = wreck.id;
        best.x = place.position.x;
        best.y = place.position.y;
        best.action = speedMatched
            ? "expedition:recover:" + std::to_string(wreck.id) : "";
        best.requirement = best.enabled() ? "" : "Match speed";
        bestDistance = distanceToWreck;
        bestPriority = priority;
    }
    return best;
}

} // namespace rocket
