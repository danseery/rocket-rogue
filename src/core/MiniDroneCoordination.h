#pragma once

#include "core/GameTypes.h"

#include <unordered_map>

namespace rocket {

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

} // namespace rocket
