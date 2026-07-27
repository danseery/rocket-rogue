#pragma once

#include "core/GameTypes.h"

#include <unordered_map>

namespace rocket {

MiniDroneAnchorFrame resolveMiniDroneAnchor(
    const MiningRunState& mining,
    MiningAnchorTarget target = MiningAnchorTarget::ControlledActor);
void transferMiniDroneSwarmAnchor(
    MiningRunState& mining,
    MiningOperatorMode previousMode,
    MiningOperatorMode nextMode,
    bool depthTransition = false);

double miniDroneOrbitRadius(MiniDroneRole role);

struct MiniDroneCoordinationPoint {
    double x = 0.0;
    double y = 0.0;
};

MiniDroneCoordinationPoint miniDroneOrbitPoint(
    const MiningRunState& mining,
    const MiningMiniDroneAgent& agent);

class MiniDroneTaskCoordinator {
public:
    virtual ~MiniDroneTaskCoordinator() = default;

    virtual void synchronizeAssignments() = 0;
    virtual bool hasAssignment(const MiningMiniDroneAgent& agent) const = 0;
    virtual bool acquireAssignment(MiningMiniDroneAgent& agent) = 0;
    virtual void releaseAssignment(MiningMiniDroneAgent& agent) = 0;
};

class MiningDroneCoordinator final : public MiniDroneTaskCoordinator {
public:
    explicit MiningDroneCoordinator(MiningRunState& mining);

    void synchronizeAssignments() override;
    bool hasAssignment(const MiningMiniDroneAgent& agent) const override;
    bool acquireAssignment(MiningMiniDroneAgent& agent) override;
    void releaseAssignment(MiningMiniDroneAgent& agent) override;

private:
    bool isCandidateCell(int x, int y) const;
    int cellKey(int x, int y) const;
    void clearAssignment(MiningMiniDroneAgent& agent);

    MiningRunState& mining_;
    std::unordered_map<int, MiningMiniDroneAgent*> reservations_;
};

class HazardDroneCoordinator final : public MiniDroneTaskCoordinator {
public:
    explicit HazardDroneCoordinator(MiningRunState& mining);

    void synchronizeAssignments() override;
    bool hasAssignment(const MiningMiniDroneAgent& agent) const override;
    bool acquireAssignment(MiningMiniDroneAgent& agent) override;
    void releaseAssignment(MiningMiniDroneAgent& agent) override;

    bool reservedByOther(int x, int y, const MiningMiniDroneAgent& agent) const;

private:
    bool isCandidateCell(const MiningMiniDroneAgent& agent, int x, int y) const;
    int cellKey(int x, int y) const;
    void clearAssignment(MiningMiniDroneAgent& agent);

    MiningRunState& mining_;
    std::vector<MiningMiniDroneAgent*> hazardDrones_;
    std::unordered_map<int, MiningMiniDroneAgent*> reservations_;
};

class AttackDroneCoordinator final : public MiniDroneTaskCoordinator {
public:
    explicit AttackDroneCoordinator(MiningRunState& mining);

    void synchronizeAssignments() override;
    bool hasAssignment(const MiningMiniDroneAgent& agent) const override;
    bool acquireAssignment(MiningMiniDroneAgent& agent) override;
    void releaseAssignment(MiningMiniDroneAgent& agent) override;

    MiniDroneCoordinationPoint formationPoint(const MiningMiniDroneAgent& agent) const;

private:
    bool targetValid(int enemyIndex) const;
    int findPriorityTarget(const MiningMiniDroneAgent& agent) const;
    bool targetVisibleToSquad(
        const MiningEnemy& enemy,
        const MiningMiniDroneAgent& agent) const;
    int formationSlot(const MiningMiniDroneAgent& agent) const;
    int assignedDroneCount(
        int enemyIndex,
        MiningAnchorTarget anchorTarget) const;

    MiningRunState& mining_;
    std::vector<MiningMiniDroneAgent*> attackDrones_;
};

struct DefenseShieldImpact {
    MiningMiniDroneAgent* interceptor = nullptr;
    double absorbedDamage = 0.0;
    double remainingDamage = 0.0;
    double impactX = 0.0;
    double impactY = 0.0;
};

class DefenseDroneCoordinator final : public MiniDroneTaskCoordinator {
public:
    explicit DefenseDroneCoordinator(MiningRunState& mining);

    void synchronizeAssignments() override;
    bool hasAssignment(const MiningMiniDroneAgent& agent) const override;
    bool acquireAssignment(MiningMiniDroneAgent& agent) override;
    void releaseAssignment(MiningMiniDroneAgent& agent) override;

    void advanceFormation(double dt);
    MiniDroneCoordinationPoint formationPoint(const MiningMiniDroneAgent& agent) const;
    DefenseShieldImpact absorbIncomingDamage(double sourceX, double sourceY, double rawDamage);

private:
    bool targetValid(int enemyIndex) const;
    int findClosestThreat(const MiningMiniDroneAgent& agent) const;
    int formationSlot(const MiningMiniDroneAgent& agent) const;
    double desiredAngle(const MiningMiniDroneAgent& agent) const;

    MiningRunState& mining_;
    std::vector<MiningMiniDroneAgent*> defenseDrones_;
};

class SurveyDroneCoordinator final : public MiniDroneTaskCoordinator {
public:
    explicit SurveyDroneCoordinator(MiningRunState& mining);

    void synchronizeAssignments() override;
    bool hasAssignment(const MiningMiniDroneAgent& agent) const override;
    bool acquireAssignment(MiningMiniDroneAgent& agent) override;
    void releaseAssignment(MiningMiniDroneAgent& agent) override;

private:
    bool isCandidateCell(int x, int y) const;
    bool isAnchoredAhead(
        const MiningMiniDroneAgent& agent,
        int x,
        int y) const;
    bool isInAssignedLane(const MiningMiniDroneAgent& agent, int x) const;
    int formationSlot(const MiningMiniDroneAgent& agent) const;
    int anchoredDroneCount(const MiningMiniDroneAgent& agent) const;
    double laneCenterX(const MiningMiniDroneAgent& agent) const;
    double laneHalfWidth(const MiningMiniDroneAgent& agent) const;
    int cellKey(int x, int y) const;
    void clearAssignment(MiningMiniDroneAgent& agent);

    MiningRunState& mining_;
    std::vector<MiningMiniDroneAgent*> surveyDrones_;
    std::unordered_map<int, MiningMiniDroneAgent*> reservations_;
};

} // namespace rocket
