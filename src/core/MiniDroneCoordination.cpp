#include "core/MiniDroneCoordination.h"

#include "core/MiningSystem.h"
#include "core/Tuning.h"

#include <algorithm>
#include <cmath>
#include <iterator>

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
        if (!isCandidateCell(*agent, agent->targetCellX, agent->targetCellY)) {
            clearAssignment(*agent);
            continue;
        }
        const int key = cellKey(agent->targetCellX, agent->targetCellY);
        if (!reservations_.emplace(key, agent).second) {
            clearAssignment(*agent);
        }
    }
}

bool HazardDroneCoordinator::hasAssignment(const MiningMiniDroneAgent& agent) const
{
    if (agent.role != MiniDroneRole::Hazard || !isCandidateCell(agent, agent.targetCellX, agent.targetCellY)) {
        return false;
    }
    const auto reservation = reservations_.find(cellKey(agent.targetCellX, agent.targetCellY));
    return reservation != reservations_.end() && reservation->second == &agent;
}

bool HazardDroneCoordinator::acquireAssignment(MiningMiniDroneAgent& agent)
{
    releaseAssignment(agent);
    const double radius = tuning::mining::hazardDroneAcquireRadiusCells;
    const double radiusSq = radius * radius;
    double bestScore = 1.0e12;
    int bestX = -1;
    int bestY = -1;
    const int minX = std::max(0, static_cast<int>(std::floor(mining_.droneX - radius)));
    const int maxX = std::min(mining_.terrain.width - 1, static_cast<int>(std::ceil(mining_.droneX + radius)));
    const int minY = std::max(0, static_cast<int>(std::floor(mining_.droneY - radius)));
    const int maxY = std::min(mining_.terrain.height - 1, static_cast<int>(std::ceil(mining_.droneY + radius)));
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            if (!isCandidateCell(agent, x, y) || reservations_.contains(cellKey(x, y))) {
                continue;
            }
            const double centerX = static_cast<double>(x) + 0.5;
            const double centerY = static_cast<double>(y) + 0.5;
            const double rigDx = centerX - mining_.droneX;
            const double rigDy = centerY - mining_.droneY;
            if (rigDx * rigDx + rigDy * rigDy > radiusSq) {
                continue;
            }
            const MiningCell* cell = miningCellAt(mining_.terrain, x, y);
            const double agentDistance = std::hypot(centerX - agent.x, centerY - agent.y);
            const double intensityPriority = static_cast<double>(tuning::mining::hazardDroneRequiredMark(cell->hazardAffinity));
            int eligibleNeighbors = 0;
            for (int neighborY = y - 1; neighborY <= y + 1; ++neighborY) {
                for (int neighborX = x - 1; neighborX <= x + 1; ++neighborX) {
                    if ((neighborX != x || neighborY != y) &&
                        isCandidateCell(agent, neighborX, neighborY) &&
                        !reservations_.contains(cellKey(neighborX, neighborY))) {
                        ++eligibleNeighbors;
                    }
                }
            }
            const int usefulNeighbors = std::min(
                eligibleNeighbors,
                tuning::mining::hazardDroneBatchSize(agent.upgradeLevel) - 1);
            const double score = -intensityPriority * 1000.0 - static_cast<double>(usefulNeighbors) * 25.0 +
                agentDistance + static_cast<double>(y * mining_.terrain.width + x) * 0.00001;
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

void HazardDroneCoordinator::releaseAssignment(MiningMiniDroneAgent& agent)
{
    if (agent.targetCellX >= 0 && agent.targetCellY >= 0) {
        const auto reservation = reservations_.find(cellKey(agent.targetCellX, agent.targetCellY));
        if (reservation != reservations_.end() && reservation->second == &agent) {
            reservations_.erase(reservation);
        }
    }
    clearAssignment(agent);
}

bool HazardDroneCoordinator::reservedByOther(int x, int y, const MiningMiniDroneAgent& agent) const
{
    const auto reservation = reservations_.find(cellKey(x, y));
    return reservation != reservations_.end() && reservation->second != &agent;
}

bool HazardDroneCoordinator::isCandidateCell(const MiningMiniDroneAgent& agent, int x, int y) const
{
    const MiningCell* cell = miningCellAt(mining_.terrain, x, y);
    return cell != nullptr && cell->revealed && cell->hazard &&
        cell->material == MiningCellMaterial::HazardPocket &&
        tuning::mining::hazardDroneRequiredMark(cell->hazardAffinity) <= agent.upgradeLevel;
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

    double bestExistingScore = 1.0e12;
    for (const MiningMiniDroneAgent* agent : attackDrones_) {
        if (!targetValid(agent->targetEnemyIndex)) {
            continue;
        }
        const MiningEnemy& enemy = mining_.enemies[static_cast<std::size_t>(agent->targetEnemyIndex)];
        const double dx = enemy.x - mining_.droneX;
        const double dy = enemy.y - mining_.droneY;
        const double score = dx * dx + dy * dy;
        if (score < bestExistingScore) {
            bestExistingScore = score;
            focusTargetIndex_ = agent->targetEnemyIndex;
        }
    }
    if (!targetValid(focusTargetIndex_)) {
        focusTargetIndex_ = findPriorityTarget();
    }

    for (MiningMiniDroneAgent* agent : attackDrones_) {
        if (focusTargetIndex_ < 0) {
            releaseAssignment(*agent);
            continue;
        }
        const bool targetChanged = agent->targetEnemyIndex != focusTargetIndex_;
        agent->targetEnemyIndex = focusTargetIndex_;
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
    return agent.role == MiniDroneRole::Attack && targetValid(agent.targetEnemyIndex) && agent.targetEnemyIndex == focusTargetIndex_;
}

bool AttackDroneCoordinator::acquireAssignment(MiningMiniDroneAgent& agent)
{
    if (agent.role != MiniDroneRole::Attack) {
        return false;
    }
    if (!targetValid(focusTargetIndex_)) {
        focusTargetIndex_ = findPriorityTarget();
    }
    if (focusTargetIndex_ < 0) {
        releaseAssignment(agent);
        return false;
    }
    agent.targetEnemyIndex = focusTargetIndex_;
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
        return {mining_.droneX, mining_.droneY};
    }
    const MiningEnemy& target = mining_.enemies[static_cast<std::size_t>(agent.targetEnemyIndex)];
    const double towardRigX = mining_.droneX - target.x;
    const double towardRigY = mining_.droneY - target.y;
    const double baseAngle = std::atan2(towardRigY, towardRigX);
    const int count = std::max(1, assignedDroneCount(agent.targetEnemyIndex));
    const int slot = formationSlot(agent);
    const double angle = baseAngle + 3.14159265358979323846 / static_cast<double>(count) +
        2.0 * 3.14159265358979323846 * static_cast<double>(slot) / static_cast<double>(count);
    MiniDroneCoordinationPoint point {
        target.x + std::cos(angle) * tuning::mining::attackDroneStandoffCells,
        target.y + std::sin(angle) * tuning::mining::attackDroneStandoffCells
    };
    double rigDx = point.x - mining_.droneX;
    double rigDy = point.y - mining_.droneY;
    double rigDistance = std::sqrt(rigDx * rigDx + rigDy * rigDy);
    if (rigDistance < tuning::mining::attackDroneRigClearanceCells) {
        if (rigDistance <= 0.0001) {
            rigDx = std::cos(angle);
            rigDy = std::sin(angle);
            rigDistance = 1.0;
        }
        point.x = mining_.droneX + rigDx / rigDistance * tuning::mining::attackDroneRigClearanceCells;
        point.y = mining_.droneY + rigDy / rigDistance * tuning::mining::attackDroneRigClearanceCells;
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

int AttackDroneCoordinator::findPriorityTarget() const
{
    double bestScore = 1.0e12;
    int bestIndex = -1;
    for (std::size_t i = 0; i < mining_.enemies.size(); ++i) {
        const MiningEnemy& enemy = mining_.enemies[i];
        if (!enemy.active || !targetVisibleToSquad(enemy)) {
            continue;
        }
        const double dx = enemy.x - mining_.droneX;
        const double dy = enemy.y - mining_.droneY;
        const double score = dx * dx + dy * dy;
        if (score < bestScore) {
            bestScore = score;
            bestIndex = static_cast<int>(i);
        }
    }
    return bestIndex;
}

bool AttackDroneCoordinator::targetVisibleToSquad(const MiningEnemy& enemy) const
{
    const double rangeSq = tuning::mining::attackDroneFieldOfViewCells * tuning::mining::attackDroneFieldOfViewCells;
    const double rigDx = enemy.x - mining_.droneX;
    const double rigDy = enemy.y - mining_.droneY;
    if (rigDx * rigDx + rigDy * rigDy <= rangeSq) {
        return true;
    }
    return std::any_of(attackDrones_.begin(), attackDrones_.end(), [&](const MiningMiniDroneAgent* agent) {
        const double dx = enemy.x - agent->x;
        const double dy = enemy.y - agent->y;
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
        if (candidate->targetEnemyIndex == agent.targetEnemyIndex || agent.targetEnemyIndex < 0) {
            ++slot;
        }
    }
    return slot;
}

int AttackDroneCoordinator::assignedDroneCount(int enemyIndex) const
{
    return static_cast<int>(std::count_if(attackDrones_.begin(), attackDrones_.end(), [&](const MiningMiniDroneAgent* agent) {
        return agent->targetEnemyIndex == enemyIndex;
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

    focusTargetIndex_ = findClosestThreat();
    for (MiningMiniDroneAgent* agent : defenseDrones_) {
        if (!agent->defenseAngleInitialized) {
            const double dx = agent->x - mining_.droneX;
            const double dy = agent->y - mining_.droneY;
            agent->defenseAngleRadians = std::hypot(dx, dy) > 0.001
                ? normalizedAngle(std::atan2(dy, dx))
                : normalizedAngle(
                    kTau * static_cast<double>(formationSlot(*agent)) /
                    static_cast<double>(std::max<std::size_t>(1, defenseDrones_.size())));
            agent->defenseAngleInitialized = true;
        }
        agent->targetEnemyIndex = focusTargetIndex_;
        agent->behavior = focusTargetIndex_ >= 0
            ? MiningMiniDroneBehavior::Guarding
            : MiningMiniDroneBehavior::Following;
    }
}

bool DefenseDroneCoordinator::hasAssignment(const MiningMiniDroneAgent& agent) const
{
    return agent.role == MiniDroneRole::Defense && targetValid(focusTargetIndex_) &&
        agent.targetEnemyIndex == focusTargetIndex_;
}

bool DefenseDroneCoordinator::acquireAssignment(MiningMiniDroneAgent& agent)
{
    if (agent.role != MiniDroneRole::Defense) {
        return false;
    }
    if (!targetValid(focusTargetIndex_)) {
        focusTargetIndex_ = findClosestThreat();
    }
    agent.targetEnemyIndex = focusTargetIndex_;
    agent.behavior = focusTargetIndex_ >= 0
        ? MiningMiniDroneBehavior::Guarding
        : MiningMiniDroneBehavior::Following;
    return focusTargetIndex_ >= 0;
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
    return {
        std::clamp(
            mining_.droneX + std::cos(agent.defenseAngleRadians) * tuning::mining::defenseDroneGuardDistanceCells,
            0.5,
            static_cast<double>(mining_.terrain.width) - 0.5),
        std::clamp(
            mining_.droneY + std::sin(agent.defenseAngleRadians) * tuning::mining::defenseDroneGuardDistanceCells,
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
    impact.impactX = mining_.droneX;
    impact.impactY = mining_.droneY;
    if (impact.remainingDamage <= 0.0 || defenseDrones_.empty()) {
        return impact;
    }

    const double sourceAngle = std::atan2(sourceY - mining_.droneY, sourceX - mining_.droneX);
    const double halfArc = tuning::mining::defenseDroneShieldArcRadians * 0.5;
    double bestDelta = 1.0e12;
    for (MiningMiniDroneAgent* agent : defenseDrones_) {
        if (agent->shieldCharge <= 0.0 || agent->shieldRechargeSeconds > 0.0) {
            continue;
        }
        const double guardAngle = std::atan2(agent->y - mining_.droneY, agent->x - mining_.droneX);
        const double delta = std::abs(shortestAngleDelta(guardAngle, sourceAngle));
        if (delta <= halfArc && delta < bestDelta) {
            bestDelta = delta;
            impact.interceptor = agent;
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
    const double guardAngle = std::atan2(interceptor.y - mining_.droneY, interceptor.x - mining_.droneX);
    impact.impactX = mining_.droneX + std::cos(guardAngle) * impactRadius;
    impact.impactY = mining_.droneY + std::sin(guardAngle) * impactRadius;
    return impact;
}

bool DefenseDroneCoordinator::targetValid(int enemyIndex) const
{
    return enemyIndex >= 0 && enemyIndex < static_cast<int>(mining_.enemies.size()) &&
        mining_.enemies[static_cast<std::size_t>(enemyIndex)].active;
}

int DefenseDroneCoordinator::findClosestThreat() const
{
    double bestDistanceSq = 1.0e12;
    int bestIndex = -1;
    for (std::size_t i = 0; i < mining_.enemies.size(); ++i) {
        const MiningEnemy& enemy = mining_.enemies[i];
        if (!enemy.active) {
            continue;
        }
        const double dx = enemy.x - mining_.droneX;
        const double dy = enemy.y - mining_.droneY;
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
    const auto match = std::find(defenseDrones_.begin(), defenseDrones_.end(), &agent);
    return match == defenseDrones_.end()
        ? std::max(0, agent.roleIndex)
        : static_cast<int>(std::distance(defenseDrones_.begin(), match));
}

double DefenseDroneCoordinator::desiredAngle(const MiningMiniDroneAgent& agent) const
{
    double baseAngle = 0.0;
    if (targetValid(focusTargetIndex_)) {
        const MiningEnemy& enemy = mining_.enemies[static_cast<std::size_t>(focusTargetIndex_)];
        baseAngle = std::atan2(enemy.y - mining_.droneY, enemy.x - mining_.droneX);
    }
    const int count = std::max(1, static_cast<int>(defenseDrones_.size()));
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
            !isAnchoredAhead(agent->targetCellX, agent->targetCellY) ||
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
    if (agent.role != MiniDroneRole::Survey || !isCandidateCell(agent.targetCellX, agent.targetCellY)) {
        return false;
    }
    const auto reservation = reservations_.find(cellKey(agent.targetCellX, agent.targetCellY));
    return reservation != reservations_.end() && reservation->second == &agent;
}

bool SurveyDroneCoordinator::acquireAssignment(MiningMiniDroneAgent& agent)
{
    releaseAssignment(agent);
    double bestScore = 1.0e12;
    int bestX = -1;
    int bestY = -1;
    const double formationHalfWidth = tuning::mining::surveyDroneFormationHalfWidthCells(static_cast<int>(surveyDrones_.size()));
    const double assignedLaneCenterX = laneCenterX(agent);
    const double assignedLaneHalfWidth = laneHalfWidth();
    const int minX = std::max(0, static_cast<int>(std::floor(mining_.droneX - formationHalfWidth)));
    const int maxX = std::min(mining_.terrain.width - 1, static_cast<int>(std::ceil(mining_.droneX + formationHalfWidth)));
    const int minY = std::max(0, static_cast<int>(std::floor(mining_.droneY + tuning::mining::surveyDroneMinimumLeadCells)));
    const int maxY = std::min(mining_.terrain.height - 1, static_cast<int>(std::ceil(mining_.droneY + tuning::mining::surveyDroneMaximumLeadCells)));
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
            const double forwardDepth = centerY - mining_.droneY;
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

bool SurveyDroneCoordinator::isAnchoredAhead(int x, int y) const
{
    const double centerX = static_cast<double>(x) + 0.5;
    const double centerY = static_cast<double>(y) + 0.5;
    return std::abs(centerX - mining_.droneX) <=
            tuning::mining::surveyDroneFormationHalfWidthCells(static_cast<int>(surveyDrones_.size())) &&
        centerY >= mining_.droneY + tuning::mining::surveyDroneMinimumLeadCells &&
        centerY <= mining_.droneY + tuning::mining::surveyDroneMaximumLeadCells;
}

bool SurveyDroneCoordinator::isInAssignedLane(const MiningMiniDroneAgent& agent, int x) const
{
    return std::abs(static_cast<double>(x) + 0.5 - laneCenterX(agent)) <= laneHalfWidth();
}

int SurveyDroneCoordinator::formationSlot(const MiningMiniDroneAgent& agent) const
{
    const auto match = std::find(surveyDrones_.begin(), surveyDrones_.end(), &agent);
    return match == surveyDrones_.end()
        ? std::max(0, agent.roleIndex)
        : static_cast<int>(std::distance(surveyDrones_.begin(), match));
}

double SurveyDroneCoordinator::laneCenterX(const MiningMiniDroneAgent& agent) const
{
    return mining_.droneX + tuning::mining::surveyDroneFormationOffsetCells(
        formationSlot(agent),
        static_cast<int>(surveyDrones_.size()));
}

double SurveyDroneCoordinator::laneHalfWidth() const
{
    return surveyDrones_.size() <= 1
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
