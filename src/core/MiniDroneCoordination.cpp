#include "core/MiniDroneCoordination.h"

#include "core/MiningSystem.h"
#include "core/Tuning.h"

#include <algorithm>
#include <cmath>

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
    case MiningCellMaterial::Regolith:
        return 0.0;
    case MiningCellMaterial::HardRock:
        return 1.5;
    default:
        return 4.0;
    }
}

} // namespace

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
        if (!isCandidateCell(agent.targetCellX, agent.targetCellY)) {
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
    if (agent.role != MiniDroneRole::Mining || !isCandidateCell(agent.targetCellX, agent.targetCellY)) {
        return false;
    }
    const auto reservation = reservations_.find(cellKey(agent.targetCellX, agent.targetCellY));
    return reservation != reservations_.end() && reservation->second == &agent;
}

bool MiningDroneCoordinator::acquireAssignment(MiningMiniDroneAgent& agent)
{
    releaseAssignment(agent);

    const double acquireRadius = tuning::mining::miningDroneAcquireRadiusCells;
    const double acquireRangeSq = acquireRadius * acquireRadius;
    double bestScore = 1.0e9;
    int bestX = -1;
    int bestY = -1;
    const int minX = std::max(0, static_cast<int>(std::floor(mining_.droneX - acquireRadius)));
    const int maxX = std::min(mining_.terrain.width - 1, static_cast<int>(std::ceil(mining_.droneX + acquireRadius)));
    const int minY = std::max(0, static_cast<int>(std::floor(mining_.droneY - acquireRadius)));
    const int maxY = std::min(mining_.terrain.height - 1, static_cast<int>(std::ceil(mining_.droneY + acquireRadius)));
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            if (!isCandidateCell(x, y) || reservations_.contains(cellKey(x, y))) {
                continue;
            }
            const double centerX = static_cast<double>(x) + 0.5;
            const double centerY = static_cast<double>(y) + 0.5;
            const double mamaDx = centerX - mining_.droneX;
            const double mamaDy = centerY - mining_.droneY;
            if (mamaDx * mamaDx + mamaDy * mamaDy > acquireRangeSq) {
                continue;
            }

            const MiningCell* cell = miningCellAt(mining_.terrain, x, y);
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
    if (agent.behavior == MiningMiniDroneBehavior::Traveling || agent.behavior == MiningMiniDroneBehavior::Working) {
        agent.behavior = MiningMiniDroneBehavior::Following;
    }
}

} // namespace rocket
