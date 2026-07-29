#include "core/MiniDroneCoordination.h"

#include "core/MiningSystem.h"
#include "core/Tuning.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <queue>
#include <vector>

namespace rocket {

namespace {

double targetPriority(MiningCellMaterial material)
{
    switch (material) {
    case MiningCellMaterial::ExoticVein:
        return -4.0;
    case MiningCellMaterial::RareOre:
        return -3.2;
    case MiningCellMaterial::CommonOre:
        return -2.4;
    case MiningCellMaterial::FuelPocket:
    case MiningCellMaterial::OxygenPocket:
        return -1.4;
    case MiningCellMaterial::Regolith:
        return 0.0;
    case MiningCellMaterial::HardRock:
        return 1.5;
    default:
        return 4.0;
    }
}

double surveyTargetPriority(MiningCellMaterial material)
{
    switch (material) {
    case MiningCellMaterial::ArtifactCache:
        return 0.0;
    case MiningCellMaterial::ExoticVein:
        return 1.0;
    case MiningCellMaterial::RareOre:
        return 2.0;
    case MiningCellMaterial::CommonOre:
        return 3.0;
    case MiningCellMaterial::FuelPocket:
    case MiningCellMaterial::OxygenPocket:
        return 3.5;
    case MiningCellMaterial::HazardPocket:
        return 4.0;
    case MiningCellMaterial::HardRock:
        return 5.0;
    case MiningCellMaterial::Regolith:
        return 6.0;
    case MiningCellMaterial::Bedrock:
        return 7.0;
    case MiningCellMaterial::Empty:
        return 8.0;
    }
    return 9.0;
}

constexpr double kPi = 3.14159265358979323846;
constexpr double kTau = kPi * 2.0;

double normalizedAngle(double angle)
{
    angle = std::fmod(angle, kTau);
    if (angle < 0.0) {
        angle += kTau;
    }
    return angle;
}

double shortestAngleDelta(double from, double to)
{
    return std::remainder(to - from, kTau);
}

MiniDroneAnchorFrame agentAnchor(
    const MiningRunState& mining,
    const MiningMiniDroneAgent& agent)
{
    return resolveMiniDroneAnchor(mining, agent.anchorTarget);
}

int earliestIncompleteCocoonLayer(const MiningRunState& mining)
{
    for (int layer = 0;
         layer < static_cast<int>(mining.gate.cocoonLayers.size());
         ++layer) {
        if (!mining.gate.cocoonLayers[static_cast<std::size_t>(layer)].completed) {
            return layer;
        }
    }
    return -1;
}

bool isActiveRevealedCocoonCell(const MiningRunState& mining, const MiningCell& cell)
{
    if (cell.cocoonLayer < 0 ||
        cell.cocoonLayer != earliestIncompleteCocoonLayer(mining) ||
        cell.cocoonLayer >= static_cast<int>(mining.gate.cocoonLayers.size())) {
        return false;
    }
    const MiningCocoonLayerProgress& layer =
        mining.gate.cocoonLayers[static_cast<std::size_t>(cell.cocoonLayer)];
    return layer.revealed && !layer.completed;
}

int hazardRequiredMarkForCell(const MiningRunState& mining, const MiningCell& cell)
{
    int required = tuning::mining::hazardDroneRequiredMark(cell.hazardAffinity);
    if (cell.cocoonLayer >= 0 &&
        cell.cocoonLayer < static_cast<int>(mining.gate.cocoonLayers.size())) {
        required = std::max(
            required,
            mining.gate.cocoonLayers[static_cast<std::size_t>(cell.cocoonLayer)].requiredHazardMark);
    }
    return required;
}

bool miniDroneCanOccupyCell(const MiningTerrain& terrain, int x, int y)
{
    const MiningCell* cell = miningCellAt(terrain, x, y);
    // Support Drones may use suit-only apertures, but cannot phase through
    // terrain. This is intentionally the same passability they use in flight.
    return cell != nullptr && !miningMaterialSolid(cell->material);
}

struct MiniDroneTaskPath {
    int length = -1;
    std::optional<MiniDroneCoordinationPoint> nextWaypoint;
};

MiniDroneTaskPath findMiniDroneTaskPath(
    const MiningRunState& mining,
    const MiningMiniDroneAgent& agent,
    int targetCellX,
    int targetCellY,
    double workRangeCells)
{
    const MiningTerrain& terrain = mining.terrain;
    if (terrain.width <= 0 || terrain.height <= 0 ||
        targetCellX < 0 || targetCellY < 0 ||
        targetCellX >= terrain.width || targetCellY >= terrain.height) {
        return {};
    }

    int startX = std::clamp(static_cast<int>(std::floor(agent.x)), 0, terrain.width - 1);
    int startY = std::clamp(static_cast<int>(std::floor(agent.y)), 0, terrain.height - 1);
    std::optional<MiniDroneCoordinationPoint> recoveryWaypoint;
    if (!miniDroneCanOccupyCell(terrain, startX, startY)) {
        // Recover from an obsolete target position or a boundary collision by
        // stepping toward an adjacent open cell before planning normally.
        bool foundRecoveryCell = false;
        double bestRecoveryDistance = 1.0e12;
        for (int y = std::max(0, startY - 1); y <= std::min(terrain.height - 1, startY + 1); ++y) {
            for (int x = std::max(0, startX - 1); x <= std::min(terrain.width - 1, startX + 1); ++x) {
                if (!miniDroneCanOccupyCell(terrain, x, y)) {
                    continue;
                }
                const double distance = std::hypot(
                    static_cast<double>(x) + 0.5 - agent.x,
                    static_cast<double>(y) + 0.5 - agent.y);
                if (distance < bestRecoveryDistance) {
                    foundRecoveryCell = true;
                    bestRecoveryDistance = distance;
                    startX = x;
                    startY = y;
                }
            }
        }
        if (!foundRecoveryCell) {
            return {};
        }
        recoveryWaypoint = MiniDroneCoordinationPoint {
            static_cast<double>(startX) + 0.5,
            static_cast<double>(startY) + 0.5
        };
    }

    const int cellCount = terrain.width * terrain.height;
    const int start = startY * terrain.width + startX;
    std::vector<int> previous(static_cast<std::size_t>(cellCount), -2);
    std::queue<int> frontier;
    previous[static_cast<std::size_t>(start)] = -1;
    frontier.push(start);
    const double targetX = static_cast<double>(targetCellX) + 0.5;
    const double targetY = static_cast<double>(targetCellY) + 0.5;
    // A drone works from the nearest open portion of an adjacent cell. Its
    // center need not occupy the solid task cell itself.
    const double usablePositionRadius = std::max(0.0, workRangeCells) + 0.51;
    constexpr std::array<std::pair<int, int>, 4> steps {{
        {0, -1}, {1, 0}, {0, 1}, {-1, 0}
    }};

    while (!frontier.empty()) {
        const int current = frontier.front();
        frontier.pop();
        const int currentX = current % terrain.width;
        const int currentY = current / terrain.width;
        const double centerX = static_cast<double>(currentX) + 0.5;
        const double centerY = static_cast<double>(currentY) + 0.5;
        if (std::hypot(centerX - targetX, centerY - targetY) <= usablePositionRadius) {
            int next = current;
            while (previous[static_cast<std::size_t>(next)] != -1 &&
                   previous[static_cast<std::size_t>(next)] != start) {
                next = previous[static_cast<std::size_t>(next)];
            }
            int length = 0;
            for (int cursor = current; previous[static_cast<std::size_t>(cursor)] != -1;
                 cursor = previous[static_cast<std::size_t>(cursor)]) {
                ++length;
            }
            if (recoveryWaypoint.has_value()) {
                return {length + 1, recoveryWaypoint};
            }
            if (current == start) {
                if (workRangeCells <= 0.0) {
                    return {length, MiniDroneCoordinationPoint {targetX, targetY}};
                }
                const double approachX = centerX - targetX;
                const double approachY = centerY - targetY;
                const double approachLength = std::max(0.0001, std::hypot(approachX, approachY));
                // Stop inside treatment range while keeping the collider fully
                // outside the solid target cell. Aiming at the cell center can
                // otherwise pin the drone against the terrain boundary.
                const double safeWorkDistance = std::min(
                    workRangeCells * 0.92,
                    0.5 + tuning::mining::miniDroneColliderRadiusCells + 0.02);
                return {
                    length,
                    MiniDroneCoordinationPoint {
                        targetX + approachX / approachLength * safeWorkDistance,
                        targetY + approachY / approachLength * safeWorkDistance
                    }
                };
            }
            return {
                length,
                MiniDroneCoordinationPoint {
                    static_cast<double>(next % terrain.width) + 0.5,
                    static_cast<double>(next / terrain.width) + 0.5
                }
            };
        }

        for (const auto& [offsetX, offsetY] : steps) {
            const int nextX = currentX + offsetX;
            const int nextY = currentY + offsetY;
            if (nextX < 0 || nextY < 0 || nextX >= terrain.width || nextY >= terrain.height ||
                !miniDroneCanOccupyCell(terrain, nextX, nextY)) {
                continue;
            }
            const int next = nextY * terrain.width + nextX;
            if (previous[static_cast<std::size_t>(next)] != -2) {
                continue;
            }
            previous[static_cast<std::size_t>(next)] = current;
            frontier.push(next);
        }
    }
    return {};
}

} // namespace

MiniDroneAnchorFrame resolveMiniDroneAnchor(
    const MiningRunState& mining,
    MiningAnchorTarget target)
{
    const auto rigFrame = [&]() {
        return MiniDroneAnchorFrame {
            MiningActorIdentity::Rig,
            mining.droneX,
            mining.droneY,
            mining.rigVelocityX,
            mining.rigVelocityY,
            mining.hullDirX,
            mining.hullDirY,
            tuning::mining::rigColliderRadiusCells,
            mining.rigDepthZone,
            !mining.rigDisabled && mining.rigDepthZone == mining.depthZone
        };
    };
    const auto operatorFrame = [&]() {
        return MiniDroneAnchorFrame {
            MiningActorIdentity::Operator,
            mining.operatorX,
            mining.operatorY,
            mining.operatorVelocityX,
            mining.operatorVelocityY,
            mining.operatorAimDirX,
            mining.operatorAimDirY,
            tuning::mining::operatorColliderRadiusCells,
            mining.depthZone,
            mining.operatorPresent
        };
    };

    switch (target) {
    case MiningAnchorTarget::Rig:
        return rigFrame();
    case MiningAnchorTarget::Operator:
        return operatorFrame();
    case MiningAnchorTarget::ControlledActor:
        break;
    }

    if (mining.operatorMode == MiningOperatorMode::Jetpack) {
        MiniDroneAnchorFrame frame = operatorFrame();
        if (frame.valid) {
            return frame;
        }
        return rigFrame();
    }
    MiniDroneAnchorFrame frame = rigFrame();
    if (frame.valid) {
        return frame;
    }
    return operatorFrame();
}

void transferMiniDroneSwarmAnchor(
    MiningRunState& mining,
    MiningOperatorMode previousMode,
    MiningOperatorMode nextMode,
    bool depthTransition)
{
    if (previousMode == nextMode && !depthTransition) {
        return;
    }
    for (MiningMiniDroneAgent& agent : mining.miniDrones) {
        if (!depthTransition &&
            agent.anchorTarget != MiningAnchorTarget::ControlledActor) {
            continue;
        }
        agent.targetCellX = -1;
        agent.targetCellY = -1;
        agent.targetEnemyIndex = -1;
        agent.taskProgressSeconds = 0.0;
        agent.finishTargetBeforeReturn = false;
        agent.surveyPulseSeconds = 0.0;
        agent.behavior = MiningMiniDroneBehavior::Returning;
        if (depthTransition) {
            MiniDroneAnchorFrame anchor =
                resolveMiniDroneAnchor(mining, agent.anchorTarget);
            MiniDroneCoordinationPoint orbit;
            if (anchor.valid) {
                orbit = miniDroneOrbitPoint(mining, agent);
            } else {
                const MiningAnchorTarget savedTarget = agent.anchorTarget;
                agent.anchorTarget = MiningAnchorTarget::ControlledActor;
                anchor = resolveMiniDroneAnchor(
                    mining,
                    MiningAnchorTarget::ControlledActor);
                orbit = miniDroneOrbitPoint(mining, agent);
                agent.anchorTarget = savedTarget;
            }
            agent.x = std::clamp(orbit.x, 0.5, static_cast<double>(mining.terrain.width) - 0.5);
            agent.y = std::clamp(orbit.y, 0.5, static_cast<double>(mining.terrain.height) - 0.5);
            agent.velocityX = anchor.velocityX;
            agent.velocityY = anchor.velocityY;
        }
    }
    mining.combatProjectiles.clear();
    mining.damageNumbers.clear();
}

double miniDroneOrbitRadius(MiniDroneRole role)
{
    switch (role) {
    case MiniDroneRole::Mining:
        return tuning::mining::miniDroneMiningOrbitRadiusCells;
    case MiniDroneRole::Resource:
        return tuning::mining::miniDroneResourceOrbitRadiusCells;
    case MiniDroneRole::Survey:
        return tuning::mining::miniDroneSurveyOrbitRadiusCells;
    case MiniDroneRole::Hazard:
        return tuning::mining::miniDroneHazardOrbitRadiusCells;
    case MiniDroneRole::Attack:
        return tuning::mining::miniDroneAttackOrbitRadiusCells;
    case MiniDroneRole::Defense:
        return tuning::mining::miniDroneDefenseOrbitRadiusCells;
    }
    return tuning::mining::miniDroneMiningOrbitRadiusCells;
}

MiniDroneCoordinationPoint miniDroneOrbitPoint(
    const MiningRunState& mining,
    const MiningMiniDroneAgent& agent)
{
    const MiniDroneAnchorFrame anchor = resolveMiniDroneAnchor(mining, agent.anchorTarget);
    if (!anchor.valid) {
        return {agent.x, agent.y};
    }

    const int roleCount = std::max(1, static_cast<int>(std::count_if(
        mining.miniDrones.begin(),
        mining.miniDrones.end(),
        [&](const MiningMiniDroneAgent& candidate) {
            return candidate.role == agent.role && candidate.anchorTarget == agent.anchorTarget;
        })));
    const int slot = std::clamp(agent.stableFormationSlot, 0, roleCount - 1);
    const double direction = static_cast<int>(agent.role) % 2 == 0 ? 1.0 : -1.0;
    const double angle = direction * agent.orbitPhaseRadians +
        kTau * static_cast<double>(slot) / static_cast<double>(roleCount);
    const double centerX = anchor.x + anchor.velocityX * tuning::mining::miniDroneAnchorVelocityLeadSeconds;
    const double centerY = anchor.y + anchor.velocityY * tuning::mining::miniDroneAnchorVelocityLeadSeconds;
    const double radius = miniDroneOrbitRadius(agent.role);
    MiniDroneCoordinationPoint desired {
        centerX + std::cos(angle) * radius,
        centerY + std::sin(angle) * radius
    };

    const auto validPoint = [&](double x, double y) {
        if (x < 0.5 || y < 0.5 ||
            x > static_cast<double>(mining.terrain.width) - 0.5 ||
            y > static_cast<double>(mining.terrain.height) - 0.5) {
            return false;
        }
        const MiningCell* cell = miningCellAt(
            mining.terrain,
            std::clamp(static_cast<int>(std::floor(x)), 0, mining.terrain.width - 1),
            std::clamp(static_cast<int>(std::floor(y)), 0, mining.terrain.height - 1));
        return cell != nullptr && !miningMaterialSolid(cell->material);
    };
    if (validPoint(desired.x, desired.y)) {
        return desired;
    }
    for (int step = 1; step <= 8; ++step) {
        const double t = static_cast<double>(step) / 8.0;
        const double projectedX = desired.x + (centerX - desired.x) * t;
        const double projectedY = desired.y + (centerY - desired.y) * t;
        if (validPoint(projectedX, projectedY)) {
            return {projectedX, projectedY};
        }
    }
    return {
        std::clamp(anchor.x, 0.5, static_cast<double>(mining.terrain.width) - 0.5),
        std::clamp(anchor.y, 0.5, static_cast<double>(mining.terrain.height) - 0.5)
    };
}

int miniDroneTaskPathLength(
    const MiningRunState& mining,
    const MiningMiniDroneAgent& agent,
    int targetCellX,
    int targetCellY,
    double workRangeCells)
{
    return findMiniDroneTaskPath(
        mining,
        agent,
        targetCellX,
        targetCellY,
        workRangeCells).length;
}

std::optional<MiniDroneCoordinationPoint> miniDroneTaskNavigationWaypoint(
    const MiningRunState& mining,
    const MiningMiniDroneAgent& agent,
    int targetCellX,
    int targetCellY,
    double workRangeCells)
{
    return findMiniDroneTaskPath(
        mining,
        agent,
        targetCellX,
        targetCellY,
        workRangeCells).nextWaypoint;
}

MiningDroneCoordinator::MiningDroneCoordinator(MiningRunState& mining)
    : mining_(mining)
{
}

void MiningDroneCoordinator::synchronizeAssignments()
{
    reservations_.clear();
    for (MiningMiniDroneAgent& agent : mining_.miniDrones) {
        if (agent.role != MiniDroneRole::Mining) {
            continue;
        }
        if (!agentAnchor(mining_, agent).valid ||
            !isCandidateCell(agent.targetCellX, agent.targetCellY) ||
            miniDroneTaskPathLength(
                mining_, agent, agent.targetCellX, agent.targetCellY,
                tuning::mining::miningDroneWorkRangeCells) < 0) {
            clearAssignment(agent);
            continue;
        }

        const int key = cellKey(agent.targetCellX, agent.targetCellY);
        if (!reservations_.emplace(key, &agent).second) {
            clearAssignment(agent);
        }
    }
}

bool MiningDroneCoordinator::hasAssignment(const MiningMiniDroneAgent& agent) const
{
    if (agent.role != MiniDroneRole::Mining ||
        !agentAnchor(mining_, agent).valid ||
        !isCandidateCell(agent.targetCellX, agent.targetCellY) ||
        miniDroneTaskPathLength(
            mining_, agent, agent.targetCellX, agent.targetCellY,
            tuning::mining::miningDroneWorkRangeCells) < 0) {
        return false;
    }
    const auto reservation = reservations_.find(cellKey(agent.targetCellX, agent.targetCellY));
    return reservation != reservations_.end() && reservation->second == &agent;
}

bool MiningDroneCoordinator::acquireAssignment(MiningMiniDroneAgent& agent)
{
    releaseAssignment(agent);
    const MiniDroneAnchorFrame anchor = agentAnchor(mining_, agent);
    if (!anchor.valid) {
        return false;
    }

    const double acquireRadius = tuning::mining::miningDroneAcquireRadiusCells;
    const double acquireRangeSq = acquireRadius * acquireRadius;
    double bestScore = 1.0e9;
    int bestX = -1;
    int bestY = -1;
    const int minX = std::max(0, static_cast<int>(std::floor(anchor.x - acquireRadius)));
    const int maxX = std::min(mining_.terrain.width - 1, static_cast<int>(std::ceil(anchor.x + acquireRadius)));
    const int minY = std::max(0, static_cast<int>(std::floor(anchor.y - acquireRadius)));
    const int maxY = std::min(mining_.terrain.height - 1, static_cast<int>(std::ceil(anchor.y + acquireRadius)));
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            if (!isCandidateCell(x, y) || reservations_.contains(cellKey(x, y))) {
                continue;
            }
            const double centerX = static_cast<double>(x) + 0.5;
            const double centerY = static_cast<double>(y) + 0.5;
            const double mamaDx = centerX - anchor.x;
            const double mamaDy = centerY - anchor.y;
            if (mamaDx * mamaDx + mamaDy * mamaDy > acquireRangeSq) {
                continue;
            }

            const MiningCell* cell = miningCellAt(mining_.terrain, x, y);
            if (miniDroneTaskPathLength(
                    mining_, agent, x, y,
                    tuning::mining::miningDroneWorkRangeCells) < 0) {
                continue;
            }
            const double agentDx = centerX - agent.x;
            const double agentDy = centerY - agent.y;
            const double score = std::sqrt(agentDx * agentDx + agentDy * agentDy) + targetPriority(cell->material);
            if (score < bestScore) {
                bestScore = score;
                bestX = x;
                bestY = y;
            }
        }
    }

    if (bestX < 0 || bestY < 0) {
        return false;
    }
    agent.targetCellX = bestX;
    agent.targetCellY = bestY;
    agent.taskProgressSeconds = 0.0;
    agent.behavior = MiningMiniDroneBehavior::Traveling;
    reservations_.emplace(cellKey(bestX, bestY), &agent);
    return true;
}

void MiningDroneCoordinator::releaseAssignment(MiningMiniDroneAgent& agent)
{
    if (agent.targetCellX >= 0 && agent.targetCellY >= 0) {
        const auto reservation = reservations_.find(cellKey(agent.targetCellX, agent.targetCellY));
        if (reservation != reservations_.end() && reservation->second == &agent) {
            reservations_.erase(reservation);
        }
    }
    clearAssignment(agent);
}

bool MiningDroneCoordinator::isCandidateCell(int x, int y) const
{
    const MiningCell* cell = miningCellAt(mining_.terrain, x, y);
    return cell != nullptr && miningMaterialSolid(cell->material) && cell->material != MiningCellMaterial::Bedrock &&
        cell->revealed && cell->material != MiningCellMaterial::HazardPocket &&
        cell->material != MiningCellMaterial::ArtifactCache;
}

int MiningDroneCoordinator::cellKey(int x, int y) const
{
    return y * mining_.terrain.width + x;
}

void MiningDroneCoordinator::clearAssignment(MiningMiniDroneAgent& agent)
{
    agent.targetCellX = -1;
    agent.targetCellY = -1;
    agent.taskProgressSeconds = 0.0;
    if (agent.behavior == MiningMiniDroneBehavior::Traveling || agent.behavior == MiningMiniDroneBehavior::Working) {
        agent.behavior = MiningMiniDroneBehavior::Following;
    }
}

HazardDroneCoordinator::HazardDroneCoordinator(MiningRunState& mining)
    : mining_(mining)
{
}

void HazardDroneCoordinator::synchronizeAssignments()
{
    hazardDrones_.clear();
    reservations_.clear();
    for (MiningMiniDroneAgent& agent : mining_.miniDrones) {
        if (agent.role == MiniDroneRole::Hazard) {
            hazardDrones_.push_back(&agent);
        }
    }
    std::sort(hazardDrones_.begin(), hazardDrones_.end(), [](const MiningMiniDroneAgent* lhs, const MiningMiniDroneAgent* rhs) {
        return lhs->roleIndex < rhs->roleIndex;
    });
    for (MiningMiniDroneAgent* agent : hazardDrones_) {
        if (!agentAnchor(mining_, *agent).valid ||
            !isEligibleTarget(*agent, agent->targetCellX, agent->targetCellY)) {
            clearAssignment(*agent);
            continue;
        }
        const int key = cellKey(agent->targetCellX, agent->targetCellY);
        reservations_[key].push_back(agent);
    }
}

bool HazardDroneCoordinator::hasAssignment(const MiningMiniDroneAgent& agent) const
{
    if (agent.role != MiniDroneRole::Hazard ||
        !agentAnchor(mining_, agent).valid ||
        !isEligibleTarget(agent, agent.targetCellX, agent.targetCellY)) {
        return false;
    }
    const auto reservation = reservations_.find(cellKey(agent.targetCellX, agent.targetCellY));
    return reservation != reservations_.end() &&
        std::find(reservation->second.begin(), reservation->second.end(), &agent) != reservation->second.end();
}

bool HazardDroneCoordinator::acquireAssignment(MiningMiniDroneAgent& agent)
{
    releaseAssignment(agent);
    const MiniDroneAnchorFrame anchor = agentAnchor(mining_, agent);
    if (!anchor.valid) {
        return false;
    }
    bool foundCandidate = false;
    bool bestIsProtectedLayer = false;
    double bestArtifactDistance = 1.0e12;
    double bestAnchorDistance = 1.0e12;
    double bestIntensity = -1.0;
    int bestX = -1;
    int bestY = -1;
    // Revealed cells in the earliest incomplete protected layer are eligible
    // anywhere in the arena. Ordinary hazards remain constrained to the
    // controlled actor's operating radius.
    const int minX = 0;
    const int maxX = mining_.terrain.width - 1;
    const int minY = 0;
    const int maxY = mining_.terrain.height - 1;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            if (!isEligibleTarget(agent, x, y) || reservations_.contains(cellKey(x, y))) {
                continue;
            }
            const double centerX = static_cast<double>(x) + 0.5;
            const double centerY = static_cast<double>(y) + 0.5;
            const double rigDx = centerX - anchor.x;
            const double rigDy = centerY - anchor.y;
            const MiningCell* cell = miningCellAt(mining_.terrain, x, y);
            const double anchorDistance = std::hypot(rigDx, rigDy);
            const double intensityPriority = static_cast<double>(hazardRequiredMarkForCell(mining_, *cell));
            const bool isProtectedLayer = isActiveRevealedCocoonCell(mining_, *cell);
            const double artifactDistance = isProtectedLayer && mining_.artifact.present
                ? std::hypot(centerX - mining_.artifact.x, centerY - mining_.artifact.y)
                : 0.0;
            // The rig is the operational anchor. Active protected-layer segments
            // always win; within that group we work from the objective outward.
            // Ordinary hazards then favor the closest threat to the player, not the
            // closest leftover tile to a drone that has just returned from elsewhere.
            const bool better = !foundCandidate
                || (isProtectedLayer != bestIsProtectedLayer && isProtectedLayer)
                || (isProtectedLayer == bestIsProtectedLayer && isProtectedLayer && artifactDistance < bestArtifactDistance - 0.0001)
                || (!isProtectedLayer && !bestIsProtectedLayer
                    && anchorDistance < bestAnchorDistance - 0.0001)
                || (isProtectedLayer == bestIsProtectedLayer
                    && (isProtectedLayer
                        ? std::abs(artifactDistance - bestArtifactDistance) <= 0.0001
                        : std::abs(anchorDistance - bestAnchorDistance) <= 0.0001)
                    && intensityPriority > bestIntensity)
                || (isProtectedLayer == bestIsProtectedLayer
                    && (isProtectedLayer
                        ? std::abs(artifactDistance - bestArtifactDistance) <= 0.0001
                        : std::abs(anchorDistance - bestAnchorDistance) <= 0.0001)
                    && intensityPriority == bestIntensity
                    && (bestY < 0 || y * mining_.terrain.width + x < bestY * mining_.terrain.width + bestX));
            if (better) {
                foundCandidate = true;
                bestIsProtectedLayer = isProtectedLayer;
                bestArtifactDistance = artifactDistance;
                bestAnchorDistance = anchorDistance;
                bestIntensity = intensityPriority;
                bestX = x;
                bestY = y;
            }
        }
    }
    if (bestX < 0 || bestY < 0) {
        return false;
    }
    agent.targetCellX = bestX;
    agent.targetCellY = bestY;
    agent.taskProgressSeconds = 0.0;
    agent.behavior = MiningMiniDroneBehavior::Traveling;
    reservations_[cellKey(bestX, bestY)].push_back(&agent);
    return true;
}

void HazardDroneCoordinator::assignAvailableDrones()
{
    // Assign the whole squad before any one drone advances. This prevents the
    // first duplicate in loadout order from completing a tile and repeatedly
    // taking the next reservation before its wingmate gets a job.
    for (MiningMiniDroneAgent* agent : hazardDrones_) {
        if (agent->actionCooldownSeconds <= 0.0 && !hasAssignment(*agent)) {
            if (!acquireAssignment(*agent)) {
                acquireAssistAssignment(*agent);
            }
        }
    }
}

bool HazardDroneCoordinator::acquireAssistAssignment(MiningMiniDroneAgent& agent)
{
    releaseAssignment(agent);
    const MiniDroneAnchorFrame anchor = agentAnchor(mining_, agent);
    if (!anchor.valid) {
        return false;
    }
    bool foundCandidate = false;
    bool bestIsProtectedLayer = false;
    double bestArtifactDistance = 1.0e12;
    double bestAnchorDistance = 1.0e12;
    double bestIntensity = -1.0;
    int bestX = -1;
    int bestY = -1;
    for (const auto& [key, workers] : reservations_) {
        if (workers.empty()) {
            continue;
        }
        const int x = key % mining_.terrain.width;
        const int y = key / mining_.terrain.width;
        if (!isEligibleTarget(agent, x, y)) {
            continue;
        }
        const MiningCell* cell = miningCellAt(mining_.terrain, x, y);
        const double centerX = static_cast<double>(x) + 0.5;
        const double centerY = static_cast<double>(y) + 0.5;
        const bool isProtectedLayer = isActiveRevealedCocoonCell(mining_, *cell);
        const double artifactDistance = isProtectedLayer && mining_.artifact.present
            ? std::hypot(centerX - mining_.artifact.x, centerY - mining_.artifact.y)
            : 0.0;
        const double anchorDistance = std::hypot(centerX - anchor.x, centerY - anchor.y);
        const double intensityPriority = static_cast<double>(
            hazardRequiredMarkForCell(mining_, *cell));
        const bool better = !foundCandidate
            || (isProtectedLayer != bestIsProtectedLayer && isProtectedLayer)
            || (isProtectedLayer == bestIsProtectedLayer && isProtectedLayer &&
                artifactDistance < bestArtifactDistance - 0.0001)
            || (!isProtectedLayer && !bestIsProtectedLayer &&
                anchorDistance < bestAnchorDistance - 0.0001)
            || (isProtectedLayer == bestIsProtectedLayer &&
                (isProtectedLayer
                    ? std::abs(artifactDistance - bestArtifactDistance) <= 0.0001
                    : std::abs(anchorDistance - bestAnchorDistance) <= 0.0001) &&
                intensityPriority > bestIntensity)
            || (isProtectedLayer == bestIsProtectedLayer &&
                (isProtectedLayer
                    ? std::abs(artifactDistance - bestArtifactDistance) <= 0.0001
                    : std::abs(anchorDistance - bestAnchorDistance) <= 0.0001) &&
                intensityPriority == bestIntensity &&
                (bestY < 0 || key < cellKey(bestX, bestY)));
        if (better) {
            foundCandidate = true;
            bestIsProtectedLayer = isProtectedLayer;
            bestArtifactDistance = artifactDistance;
            bestAnchorDistance = anchorDistance;
            bestIntensity = intensityPriority;
            bestX = x;
            bestY = y;
        }
    }
    if (bestX < 0 || bestY < 0) {
        return false;
    }
    agent.targetCellX = bestX;
    agent.targetCellY = bestY;
    agent.taskProgressSeconds = 0.0;
    agent.behavior = MiningMiniDroneBehavior::Traveling;
    reservations_[cellKey(bestX, bestY)].push_back(&agent);
    return true;
}

std::vector<std::pair<int, int>> HazardDroneCoordinator::assignedTargets() const
{
    std::vector<std::pair<int, int>> result;
    result.reserve(reservations_.size());
    for (const auto& [key, workers] : reservations_) {
        if (!workers.empty()) {
            result.emplace_back(key % mining_.terrain.width, key / mining_.terrain.width);
        }
    }
    std::sort(result.begin(), result.end(), [&](const auto& lhs, const auto& rhs) {
        return cellKey(lhs.first, lhs.second) < cellKey(rhs.first, rhs.second);
    });
    return result;
}

std::vector<MiningMiniDroneAgent*> HazardDroneCoordinator::assignedWorkers(int x, int y) const
{
    const auto reservation = reservations_.find(cellKey(x, y));
    if (reservation == reservations_.end()) {
        return {};
    }
    std::vector<MiningMiniDroneAgent*> result = reservation->second;
    std::sort(result.begin(), result.end(), [](const MiningMiniDroneAgent* lhs, const MiningMiniDroneAgent* rhs) {
        return lhs->roleIndex < rhs->roleIndex;
    });
    return result;
}

MiniDroneCoordinationPoint HazardDroneCoordinator::treatmentApproachPoint(
    const MiningMiniDroneAgent& agent) const
{
    const std::vector<MiningMiniDroneAgent*> workers =
        assignedWorkers(agent.targetCellX, agent.targetCellY);
    const auto found = std::find(workers.begin(), workers.end(), &agent);
    const int workerCount = std::max(1, static_cast<int>(workers.size()));
    const int workerSlot = found == workers.end()
        ? std::max(0, agent.roleIndex)
        : static_cast<int>(std::distance(workers.begin(), found));
    const double angle = -kPi * 0.5 +
        kTau * static_cast<double>(workerSlot) / static_cast<double>(workerCount);
    const double radius = tuning::mining::hazardDroneWorkRangeCells * 0.72;
    return {
        std::clamp(
            static_cast<double>(agent.targetCellX) + 0.5 + std::cos(angle) * radius,
            0.5,
            static_cast<double>(mining_.terrain.width) - 0.5),
        std::clamp(
            static_cast<double>(agent.targetCellY) + 0.5 + std::sin(angle) * radius,
            0.5,
            static_cast<double>(mining_.terrain.height) - 0.5)
    };
}

void HazardDroneCoordinator::releaseTargetAssignments(int x, int y)
{
    const auto reservation = reservations_.find(cellKey(x, y));
    if (reservation == reservations_.end()) {
        return;
    }
    const std::vector<MiningMiniDroneAgent*> workers = reservation->second;
    reservations_.erase(reservation);
    for (MiningMiniDroneAgent* worker : workers) {
        if (worker == nullptr) {
            continue;
        }
        clearAssignment(*worker);
        worker->actionCooldownSeconds = 0.0;
        worker->behavior = MiningMiniDroneBehavior::Returning;
    }
}

void HazardDroneCoordinator::releaseAssignment(MiningMiniDroneAgent& agent)
{
    if (agent.targetCellX >= 0 && agent.targetCellY >= 0) {
        const auto reservation = reservations_.find(cellKey(agent.targetCellX, agent.targetCellY));
        if (reservation != reservations_.end()) {
            std::erase(reservation->second, &agent);
            if (reservation->second.empty()) {
                reservations_.erase(reservation);
            }
        }
    }
    clearAssignment(agent);
}

bool HazardDroneCoordinator::reservedByOther(int x, int y, const MiningMiniDroneAgent& agent) const
{
    const auto reservation = reservations_.find(cellKey(x, y));
    return reservation != reservations_.end() &&
        std::find(reservation->second.begin(), reservation->second.end(), &agent) == reservation->second.end();
}

bool HazardDroneCoordinator::isCandidateCell(const MiningMiniDroneAgent& agent, int x, int y) const
{
    const MiningCell* cell = miningCellAt(mining_.terrain, x, y);
    return cell != nullptr && cell->revealed && cell->hazard &&
        cell->material == MiningCellMaterial::HazardPocket &&
        (cell->cocoonLayer < 0 || isActiveRevealedCocoonCell(mining_, *cell)) &&
        hazardRequiredMarkForCell(mining_, *cell) <= agent.upgradeLevel;
}

bool HazardDroneCoordinator::isEligibleTarget(const MiningMiniDroneAgent& agent, int x, int y) const
{
    if (!isCandidateCell(agent, x, y)) {
        return false;
    }
    const MiningCell* cell = miningCellAt(mining_.terrain, x, y);
    if (cell != nullptr && isActiveRevealedCocoonCell(mining_, *cell)) {
        return true;
    }
    const MiniDroneAnchorFrame anchor = agentAnchor(mining_, agent);
    if (!anchor.valid) {
        return false;
    }
    const double dx = static_cast<double>(x) + 0.5 - anchor.x;
    const double dy = static_cast<double>(y) + 0.5 - anchor.y;
    const double radius = tuning::mining::hazardDroneAcquireRadiusCells;
    return dx * dx + dy * dy <= radius * radius;
}

int HazardDroneCoordinator::cellKey(int x, int y) const
{
    return y * mining_.terrain.width + x;
}

void HazardDroneCoordinator::clearAssignment(MiningMiniDroneAgent& agent)
{
    agent.targetCellX = -1;
    agent.targetCellY = -1;
    agent.taskProgressSeconds = 0.0;
    if (agent.behavior == MiningMiniDroneBehavior::Traveling || agent.behavior == MiningMiniDroneBehavior::Working) {
        agent.behavior = MiningMiniDroneBehavior::Returning;
    }
}

AttackDroneCoordinator::AttackDroneCoordinator(MiningRunState& mining)
    : mining_(mining)
{
}

void AttackDroneCoordinator::synchronizeAssignments()
{
    attackDrones_.clear();
    for (MiningMiniDroneAgent& agent : mining_.miniDrones) {
        if (agent.role == MiniDroneRole::Attack) {
            attackDrones_.push_back(&agent);
        }
    }
    std::sort(attackDrones_.begin(), attackDrones_.end(), [](const MiningMiniDroneAgent* lhs, const MiningMiniDroneAgent* rhs) {
        return lhs->roleIndex < rhs->roleIndex;
    });

    for (MiningMiniDroneAgent* agent : attackDrones_) {
        int targetIndex = agent->targetEnemyIndex;
        if (!targetValid(targetIndex) ||
            !targetVisibleToSquad(
                mining_.enemies[static_cast<std::size_t>(targetIndex)],
                *agent)) {
            targetIndex = findPriorityTarget(*agent);
        }
        if (targetIndex < 0) {
            releaseAssignment(*agent);
            continue;
        }
        const bool targetChanged = agent->targetEnemyIndex != targetIndex;
        agent->targetEnemyIndex = targetIndex;
        agent->behavior = MiningMiniDroneBehavior::Engaging;
        if (targetChanged) {
            agent->actionCooldownSeconds = std::max(
                agent->actionCooldownSeconds,
                static_cast<double>(formationSlot(*agent)) * tuning::mining::attackDroneShotStaggerSeconds);
        }
    }
}

bool AttackDroneCoordinator::hasAssignment(const MiningMiniDroneAgent& agent) const
{
    return agent.role == MiniDroneRole::Attack &&
        targetValid(agent.targetEnemyIndex) &&
        agentAnchor(mining_, agent).valid;
}

bool AttackDroneCoordinator::acquireAssignment(MiningMiniDroneAgent& agent)
{
    if (agent.role != MiniDroneRole::Attack) {
        return false;
    }
    const int targetIndex = findPriorityTarget(agent);
    if (targetIndex < 0) {
        releaseAssignment(agent);
        return false;
    }
    agent.targetEnemyIndex = targetIndex;
    agent.behavior = MiningMiniDroneBehavior::Engaging;
    return true;
}

void AttackDroneCoordinator::releaseAssignment(MiningMiniDroneAgent& agent)
{
    agent.targetEnemyIndex = -1;
    if (agent.behavior == MiningMiniDroneBehavior::Engaging) {
        agent.behavior = MiningMiniDroneBehavior::Returning;
    }
}

MiniDroneCoordinationPoint AttackDroneCoordinator::formationPoint(const MiningMiniDroneAgent& agent) const
{
    if (!hasAssignment(agent)) {
        return miniDroneOrbitPoint(mining_, agent);
    }
    const MiniDroneAnchorFrame anchor = agentAnchor(mining_, agent);
    const MiningEnemy& target = mining_.enemies[static_cast<std::size_t>(agent.targetEnemyIndex)];
    const double towardRigX = anchor.x - target.x;
    const double towardRigY = anchor.y - target.y;
    const double baseAngle = std::atan2(towardRigY, towardRigX);
    const int count = std::max(
        1,
        assignedDroneCount(agent.targetEnemyIndex, agent.anchorTarget));
    const int slot = formationSlot(agent);
    const double angle = baseAngle + 3.14159265358979323846 / static_cast<double>(count) +
        2.0 * 3.14159265358979323846 * static_cast<double>(slot) / static_cast<double>(count);
    MiniDroneCoordinationPoint point {
        target.x + std::cos(angle) * tuning::mining::attackDroneStandoffCells,
        target.y + std::sin(angle) * tuning::mining::attackDroneStandoffCells
    };
    double rigDx = point.x - anchor.x;
    double rigDy = point.y - anchor.y;
    double rigDistance = std::sqrt(rigDx * rigDx + rigDy * rigDy);
    if (rigDistance < tuning::mining::attackDroneRigClearanceCells) {
        if (rigDistance <= 0.0001) {
            rigDx = std::cos(angle);
            rigDy = std::sin(angle);
            rigDistance = 1.0;
        }
        point.x = anchor.x + rigDx / rigDistance * tuning::mining::attackDroneRigClearanceCells;
        point.y = anchor.y + rigDy / rigDistance * tuning::mining::attackDroneRigClearanceCells;
    }
    point.x = std::clamp(point.x, 0.5, static_cast<double>(mining_.terrain.width) - 0.5);
    point.y = std::clamp(point.y, 0.5, static_cast<double>(mining_.terrain.height) - 0.5);
    return point;
}

bool AttackDroneCoordinator::targetValid(int enemyIndex) const
{
    return enemyIndex >= 0 && enemyIndex < static_cast<int>(mining_.enemies.size()) &&
        mining_.enemies[static_cast<std::size_t>(enemyIndex)].active;
}

int AttackDroneCoordinator::findPriorityTarget(
    const MiningMiniDroneAgent& agent) const
{
    const MiniDroneAnchorFrame anchor = agentAnchor(mining_, agent);
    if (!anchor.valid) {
        return -1;
    }
    double bestScore = 1.0e12;
    int bestIndex = -1;
    for (std::size_t i = 0; i < mining_.enemies.size(); ++i) {
        const MiningEnemy& enemy = mining_.enemies[i];
        if (!enemy.active || !targetVisibleToSquad(enemy, agent)) {
            continue;
        }
        const double dx = enemy.x - anchor.x;
        const double dy = enemy.y - anchor.y;
        const double score = dx * dx + dy * dy;
        if (score < bestScore) {
            bestScore = score;
            bestIndex = static_cast<int>(i);
        }
    }
    return bestIndex;
}

bool AttackDroneCoordinator::targetVisibleToSquad(
    const MiningEnemy& enemy,
    const MiningMiniDroneAgent& agent) const
{
    const MiniDroneAnchorFrame anchor = agentAnchor(mining_, agent);
    if (!anchor.valid) {
        return false;
    }
    const double rangeSq = tuning::mining::attackDroneFieldOfViewCells * tuning::mining::attackDroneFieldOfViewCells;
    const double rigDx = enemy.x - anchor.x;
    const double rigDy = enemy.y - anchor.y;
    if (rigDx * rigDx + rigDy * rigDy <= rangeSq) {
        return true;
    }
    return std::any_of(attackDrones_.begin(), attackDrones_.end(), [&](const MiningMiniDroneAgent* candidate) {
        if (candidate->anchorTarget != agent.anchorTarget) {
            return false;
        }
        const double dx = enemy.x - candidate->x;
        const double dy = enemy.y - candidate->y;
        return dx * dx + dy * dy <= rangeSq;
    });
}

int AttackDroneCoordinator::formationSlot(const MiningMiniDroneAgent& agent) const
{
    int slot = 0;
    for (const MiningMiniDroneAgent* candidate : attackDrones_) {
        if (candidate == &agent) {
            return slot;
        }
        if (candidate->anchorTarget == agent.anchorTarget &&
            (candidate->targetEnemyIndex == agent.targetEnemyIndex ||
                agent.targetEnemyIndex < 0)) {
            ++slot;
        }
    }
    return slot;
}

int AttackDroneCoordinator::assignedDroneCount(
    int enemyIndex,
    MiningAnchorTarget anchorTarget) const
{
    return static_cast<int>(std::count_if(attackDrones_.begin(), attackDrones_.end(), [&](const MiningMiniDroneAgent* agent) {
        return agent->targetEnemyIndex == enemyIndex &&
            agent->anchorTarget == anchorTarget;
    }));
}

DefenseDroneCoordinator::DefenseDroneCoordinator(MiningRunState& mining)
    : mining_(mining)
{
}

void DefenseDroneCoordinator::synchronizeAssignments()
{
    defenseDrones_.clear();
    for (MiningMiniDroneAgent& agent : mining_.miniDrones) {
        if (agent.role == MiniDroneRole::Defense) {
            defenseDrones_.push_back(&agent);
        }
    }
    std::sort(defenseDrones_.begin(), defenseDrones_.end(), [](const MiningMiniDroneAgent* lhs, const MiningMiniDroneAgent* rhs) {
        return lhs->roleIndex < rhs->roleIndex;
    });

    for (MiningMiniDroneAgent* agent : defenseDrones_) {
        const MiniDroneAnchorFrame anchor = agentAnchor(mining_, *agent);
        if (!anchor.valid) {
            releaseAssignment(*agent);
            agent->behavior = MiningMiniDroneBehavior::Returning;
            continue;
        }
        if (!agent->defenseAngleInitialized) {
            const double dx = agent->x - anchor.x;
            const double dy = agent->y - anchor.y;
            agent->defenseAngleRadians = std::hypot(dx, dy) > 0.001
                ? normalizedAngle(std::atan2(dy, dx))
                : normalizedAngle(
                    kTau * static_cast<double>(formationSlot(*agent)) /
                    static_cast<double>(std::max<std::size_t>(1, defenseDrones_.size())));
            agent->defenseAngleInitialized = true;
        }
        agent->targetEnemyIndex = findClosestThreat(*agent);
        agent->behavior = agent->targetEnemyIndex >= 0
            ? MiningMiniDroneBehavior::Guarding
            : MiningMiniDroneBehavior::Following;
    }
}

bool DefenseDroneCoordinator::hasAssignment(const MiningMiniDroneAgent& agent) const
{
    return agent.role == MiniDroneRole::Defense &&
        targetValid(agent.targetEnemyIndex) &&
        agentAnchor(mining_, agent).valid;
}

bool DefenseDroneCoordinator::acquireAssignment(MiningMiniDroneAgent& agent)
{
    if (agent.role != MiniDroneRole::Defense) {
        return false;
    }
    agent.targetEnemyIndex = findClosestThreat(agent);
    agent.behavior = agent.targetEnemyIndex >= 0
        ? MiningMiniDroneBehavior::Guarding
        : MiningMiniDroneBehavior::Following;
    return agent.targetEnemyIndex >= 0;
}

void DefenseDroneCoordinator::releaseAssignment(MiningMiniDroneAgent& agent)
{
    agent.targetEnemyIndex = -1;
    if (agent.behavior == MiningMiniDroneBehavior::Guarding) {
        agent.behavior = MiningMiniDroneBehavior::Following;
    }
}

void DefenseDroneCoordinator::advanceFormation(double dt)
{
    for (MiningMiniDroneAgent* agent : defenseDrones_) {
        agent->shieldImpactSeconds = std::max(0.0, agent->shieldImpactSeconds - dt);
        if (agent->shieldCharge <= 0.0) {
            agent->shieldRechargeSeconds = std::max(0.0, agent->shieldRechargeSeconds - dt);
            if (agent->shieldRechargeSeconds <= 0.0) {
                agent->shieldCharge = 1.0;
            }
        } else {
            agent->shieldRechargeSeconds = 0.0;
        }

        const double targetAngle = desiredAngle(*agent);
        const double response = 1.0 - std::exp(
            -tuning::mining::defenseDroneTrackingSlerpPerSecond(agent->upgradeLevel) * dt);
        agent->defenseAngleRadians = normalizedAngle(
            agent->defenseAngleRadians +
            shortestAngleDelta(agent->defenseAngleRadians, targetAngle) * response);
    }
}

MiniDroneCoordinationPoint DefenseDroneCoordinator::formationPoint(const MiningMiniDroneAgent& agent) const
{
    const MiniDroneAnchorFrame anchor = agentAnchor(mining_, agent);
    if (!anchor.valid) {
        return {agent.x, agent.y};
    }
    return {
        std::clamp(
            anchor.x + std::cos(agent.defenseAngleRadians) * tuning::mining::defenseDroneGuardDistanceCells,
            0.5,
            static_cast<double>(mining_.terrain.width) - 0.5),
        std::clamp(
            anchor.y + std::sin(agent.defenseAngleRadians) * tuning::mining::defenseDroneGuardDistanceCells,
            0.5,
            static_cast<double>(mining_.terrain.height) - 0.5)
    };
}

DefenseShieldImpact DefenseDroneCoordinator::absorbIncomingDamage(
    double sourceX,
    double sourceY,
    double rawDamage)
{
    DefenseShieldImpact impact;
    impact.remainingDamage = std::max(0.0, rawDamage);
    const MiniDroneAnchorFrame activeAnchor =
        resolveMiniDroneAnchor(mining_, MiningAnchorTarget::ControlledActor);
    impact.impactX = activeAnchor.x;
    impact.impactY = activeAnchor.y;
    if (impact.remainingDamage <= 0.0 || defenseDrones_.empty() ||
        !activeAnchor.valid) {
        return impact;
    }

    const double halfArc = tuning::mining::defenseDroneShieldArcRadians * 0.5;
    double bestDelta = 1.0e12;
    MiniDroneAnchorFrame interceptorAnchor;
    for (MiningMiniDroneAgent* agent : defenseDrones_) {
        if (agent->shieldCharge <= 0.0 || agent->shieldRechargeSeconds > 0.0) {
            continue;
        }
        const MiniDroneAnchorFrame anchor = agentAnchor(mining_, *agent);
        if (!anchor.valid || anchor.actor != activeAnchor.actor ||
            anchor.depthZone != activeAnchor.depthZone) {
            continue;
        }
        const double sourceAngle =
            std::atan2(sourceY - anchor.y, sourceX - anchor.x);
        const double guardAngle = std::atan2(
            agent->y - anchor.y,
            agent->x - anchor.x);
        const double delta = std::abs(shortestAngleDelta(guardAngle, sourceAngle));
        if (delta <= halfArc && delta < bestDelta) {
            bestDelta = delta;
            impact.interceptor = agent;
            interceptorAnchor = anchor;
        }
    }
    if (impact.interceptor == nullptr) {
        return impact;
    }

    MiningMiniDroneAgent& interceptor = *impact.interceptor;
    const double maximumHitPoints = tuning::mining::defenseDroneShieldHitPoints(interceptor.upgradeLevel);
    const double availableHitPoints = interceptor.shieldCharge * maximumHitPoints;
    impact.absorbedDamage = std::min(impact.remainingDamage, availableHitPoints);
    impact.remainingDamage = std::max(0.0, impact.remainingDamage - impact.absorbedDamage);
    interceptor.shieldCharge = std::clamp(
        (availableHitPoints - impact.absorbedDamage) / std::max(0.001, maximumHitPoints),
        0.0,
        1.0);
    interceptor.shieldImpactSeconds = tuning::mining::defenseDroneShieldImpactPulseSeconds;
    if (interceptor.shieldCharge <= 0.0001) {
        interceptor.shieldCharge = 0.0;
        interceptor.shieldRechargeSeconds = tuning::mining::defenseDroneRechargeSeconds(interceptor.upgradeLevel);
    }

    const double impactRadius = tuning::mining::defenseDroneGuardDistanceCells +
        tuning::mining::defenseDroneShieldArcOffsetCells;
    const double guardAngle = std::atan2(
        interceptor.y - interceptorAnchor.y,
        interceptor.x - interceptorAnchor.x);
    impact.impactX =
        interceptorAnchor.x + std::cos(guardAngle) * impactRadius;
    impact.impactY =
        interceptorAnchor.y + std::sin(guardAngle) * impactRadius;
    return impact;
}

bool DefenseDroneCoordinator::targetValid(int enemyIndex) const
{
    return enemyIndex >= 0 && enemyIndex < static_cast<int>(mining_.enemies.size()) &&
        mining_.enemies[static_cast<std::size_t>(enemyIndex)].active;
}

int DefenseDroneCoordinator::findClosestThreat(
    const MiningMiniDroneAgent& agent) const
{
    const MiniDroneAnchorFrame anchor = agentAnchor(mining_, agent);
    if (!anchor.valid) {
        return -1;
    }
    double bestDistanceSq = 1.0e12;
    int bestIndex = -1;
    for (std::size_t i = 0; i < mining_.enemies.size(); ++i) {
        const MiningEnemy& enemy = mining_.enemies[i];
        if (!enemy.active) {
            continue;
        }
        const double dx = enemy.x - anchor.x;
        const double dy = enemy.y - anchor.y;
        const double distanceSq = dx * dx + dy * dy;
        if (distanceSq < bestDistanceSq) {
            bestDistanceSq = distanceSq;
            bestIndex = static_cast<int>(i);
        }
    }
    return bestIndex;
}

int DefenseDroneCoordinator::formationSlot(const MiningMiniDroneAgent& agent) const
{
    int slot = 0;
    for (const MiningMiniDroneAgent* candidate : defenseDrones_) {
        if (candidate == &agent) {
            return slot;
        }
        if (candidate->anchorTarget == agent.anchorTarget) {
            ++slot;
        }
    }
    return std::max(0, agent.stableFormationSlot);
}

double DefenseDroneCoordinator::desiredAngle(const MiningMiniDroneAgent& agent) const
{
    const MiniDroneAnchorFrame anchor = agentAnchor(mining_, agent);
    double baseAngle = 0.0;
    if (anchor.valid && targetValid(agent.targetEnemyIndex)) {
        const MiningEnemy& enemy =
            mining_.enemies[static_cast<std::size_t>(agent.targetEnemyIndex)];
        baseAngle = std::atan2(
            enemy.y - anchor.y,
            enemy.x - anchor.x);
    }
    const int count = std::max(
        1,
        static_cast<int>(std::count_if(
            defenseDrones_.begin(),
            defenseDrones_.end(),
            [&](const MiningMiniDroneAgent* candidate) {
                return candidate->anchorTarget == agent.anchorTarget;
            })));
    return normalizedAngle(baseAngle +
        kTau * static_cast<double>(formationSlot(agent)) / static_cast<double>(count));
}

SurveyDroneCoordinator::SurveyDroneCoordinator(MiningRunState& mining)
    : mining_(mining)
{
}

void SurveyDroneCoordinator::synchronizeAssignments()
{
    surveyDrones_.clear();
    for (MiningMiniDroneAgent& agent : mining_.miniDrones) {
        if (agent.role == MiniDroneRole::Survey) {
            surveyDrones_.push_back(&agent);
        }
    }
    std::sort(surveyDrones_.begin(), surveyDrones_.end(), [](const MiningMiniDroneAgent* lhs, const MiningMiniDroneAgent* rhs) {
        return lhs->roleIndex < rhs->roleIndex;
    });

    reservations_.clear();
    for (MiningMiniDroneAgent* agent : surveyDrones_) {
        if (!isCandidateCell(agent->targetCellX, agent->targetCellY) ||
            !isAnchoredAhead(*agent, agent->targetCellX, agent->targetCellY) ||
            !isInAssignedLane(*agent, agent->targetCellX)) {
            clearAssignment(*agent);
            continue;
        }
        const int key = cellKey(agent->targetCellX, agent->targetCellY);
        if (!reservations_.emplace(key, agent).second) {
            clearAssignment(*agent);
        }
    }
}

bool SurveyDroneCoordinator::hasAssignment(const MiningMiniDroneAgent& agent) const
{
    if (agent.role != MiniDroneRole::Survey ||
        !agentAnchor(mining_, agent).valid ||
        !isCandidateCell(agent.targetCellX, agent.targetCellY)) {
        return false;
    }
    const auto reservation = reservations_.find(cellKey(agent.targetCellX, agent.targetCellY));
    return reservation != reservations_.end() && reservation->second == &agent;
}

bool SurveyDroneCoordinator::acquireAssignment(MiningMiniDroneAgent& agent)
{
    releaseAssignment(agent);
    const MiniDroneAnchorFrame anchor = agentAnchor(mining_, agent);
    if (!anchor.valid) {
        return false;
    }
    double bestScore = 1.0e12;
    int bestX = -1;
    int bestY = -1;
    const double formationHalfWidth =
        tuning::mining::surveyDroneFormationHalfWidthCells(
            anchoredDroneCount(agent));
    const double assignedLaneCenterX = laneCenterX(agent);
    const double assignedLaneHalfWidth = laneHalfWidth(agent);
    const int minX = std::max(0, static_cast<int>(std::floor(anchor.x - formationHalfWidth)));
    const int maxX = std::min(mining_.terrain.width - 1, static_cast<int>(std::ceil(anchor.x + formationHalfWidth)));
    const int minY = std::max(0, static_cast<int>(std::floor(anchor.y + tuning::mining::surveyDroneMinimumLeadCells)));
    const int maxY = std::min(mining_.terrain.height - 1, static_cast<int>(std::ceil(anchor.y + tuning::mining::surveyDroneMaximumLeadCells)));
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            if (!isCandidateCell(x, y) || reservations_.contains(cellKey(x, y))) {
                continue;
            }
            const MiningCell* cell = miningCellAt(mining_.terrain, x, y);
            const double centerX = static_cast<double>(x) + 0.5;
            const double centerY = static_cast<double>(y) + 0.5;
            const double laneDistance = std::abs(centerX - assignedLaneCenterX);
            if (laneDistance > assignedLaneHalfWidth) {
                continue;
            }
            const double forwardDepth = centerY - anchor.y;
            const double agentDistance = std::hypot(centerX - agent.x, centerY - agent.y);
            const double score = surveyTargetPriority(cell->material) * 1000.0 - forwardDepth * 8.0 +
                laneDistance * 4.0 + agentDistance * 0.10;
            if (score < bestScore) {
                bestScore = score;
                bestX = x;
                bestY = y;
            }
        }
    }
    if (bestX < 0 || bestY < 0) {
        return false;
    }
    agent.targetCellX = bestX;
    agent.targetCellY = bestY;
    agent.taskProgressSeconds = 0.0;
    agent.behavior = MiningMiniDroneBehavior::Traveling;
    reservations_.emplace(cellKey(bestX, bestY), &agent);
    return true;
}

void SurveyDroneCoordinator::releaseAssignment(MiningMiniDroneAgent& agent)
{
    if (agent.targetCellX >= 0 && agent.targetCellY >= 0) {
        const auto reservation = reservations_.find(cellKey(agent.targetCellX, agent.targetCellY));
        if (reservation != reservations_.end() && reservation->second == &agent) {
            reservations_.erase(reservation);
        }
    }
    clearAssignment(agent);
}

bool SurveyDroneCoordinator::isCandidateCell(int x, int y) const
{
    const MiningCell* cell = miningCellAt(mining_.terrain, x, y);
    return cell != nullptr && !cell->revealed;
}

bool SurveyDroneCoordinator::isAnchoredAhead(
    const MiningMiniDroneAgent& agent,
    int x,
    int y) const
{
    const MiniDroneAnchorFrame anchor = agentAnchor(mining_, agent);
    if (!anchor.valid) {
        return false;
    }
    const double centerX = static_cast<double>(x) + 0.5;
    const double centerY = static_cast<double>(y) + 0.5;
    return std::abs(centerX - anchor.x) <=
            tuning::mining::surveyDroneFormationHalfWidthCells(
                anchoredDroneCount(agent)) &&
        centerY >= anchor.y + tuning::mining::surveyDroneMinimumLeadCells &&
        centerY <= anchor.y + tuning::mining::surveyDroneMaximumLeadCells;
}

bool SurveyDroneCoordinator::isInAssignedLane(const MiningMiniDroneAgent& agent, int x) const
{
    return std::abs(static_cast<double>(x) + 0.5 - laneCenterX(agent)) <=
        laneHalfWidth(agent);
}

int SurveyDroneCoordinator::formationSlot(const MiningMiniDroneAgent& agent) const
{
    int slot = 0;
    for (const MiningMiniDroneAgent* candidate : surveyDrones_) {
        if (candidate == &agent) {
            return slot;
        }
        if (candidate->anchorTarget == agent.anchorTarget) {
            ++slot;
        }
    }
    return std::max(0, agent.stableFormationSlot);
}

int SurveyDroneCoordinator::anchoredDroneCount(
    const MiningMiniDroneAgent& agent) const
{
    return std::max(
        1,
        static_cast<int>(std::count_if(
            surveyDrones_.begin(),
            surveyDrones_.end(),
            [&](const MiningMiniDroneAgent* candidate) {
                return candidate->anchorTarget == agent.anchorTarget;
            })));
}

double SurveyDroneCoordinator::laneCenterX(const MiningMiniDroneAgent& agent) const
{
    const MiniDroneAnchorFrame anchor = agentAnchor(mining_, agent);
    return anchor.x + tuning::mining::surveyDroneFormationOffsetCells(
        formationSlot(agent),
        anchoredDroneCount(agent));
}

double SurveyDroneCoordinator::laneHalfWidth(
    const MiningMiniDroneAgent& agent) const
{
    return anchoredDroneCount(agent) <= 1
        ? tuning::mining::surveyDroneAnchorHalfWidthCells
        : tuning::mining::surveyDroneSearchLaneHalfWidthCells;
}

int SurveyDroneCoordinator::cellKey(int x, int y) const
{
    return y * mining_.terrain.width + x;
}

void SurveyDroneCoordinator::clearAssignment(MiningMiniDroneAgent& agent)
{
    agent.targetCellX = -1;
    agent.targetCellY = -1;
    agent.taskProgressSeconds = 0.0;
    if (agent.behavior == MiningMiniDroneBehavior::Traveling || agent.behavior == MiningMiniDroneBehavior::Scouting) {
        agent.behavior = MiningMiniDroneBehavior::Returning;
    }
}

} // namespace rocket
