#pragma once
#include "core/ExpeditionSystem.h"
#include <vector>

namespace rocket {
struct MissionRequirementView {
    std::string text;
    bool complete = false;
    std::string detail = {};
};
// Read-only projection. Scenarios and physical payload ownership remain authoritative.
struct MissionView {
    bool arrivalStage = false;
    std::vector<MissionRequirementView> arrivalGoals, recoveryGoals;
    bool available = false, complete = false, optional = false, sectorKnown = false, artifactLocated = false;
    std::string id, stepId, location, title, instruction, purpose, reward;
    std::string targetId, sectorId, artifactId, action;
    CampaignObjectiveKind kind = CampaignObjectiveKind::Mission;
    std::uint64_t wreckId = 0;
    std::vector<std::string> progress;
    std::vector<MissionRequirementView> requirements;
    std::vector<MissionRequirementView> trackerGoals;
};
MissionView missionView(const GameState&, const ContentCatalog&, std::string_view missionId,
    const FlightRunState* flight = nullptr, bool currentSurveyComplete = false);
MissionView trackedMissionView(const GameState&, const ContentCatalog&,
    const FlightRunState* flight = nullptr, bool currentSurveyComplete = false);
std::vector<MissionView> missionLog(const GameState&, const ContentCatalog&);
bool reconcileTrackedMission(GameState&, const ContentCatalog&);
std::string missionSectorName(std::string_view sectorId);
std::string firstMoonMissionInstructions(const GameState&, const ContentCatalog&);
int arrivalTutorialIndex(std::string_view body);
bool arrivalBriefingRequired(const GameState&, std::string_view body);
bool updateArrivalTutorial(GameState&, const ContentCatalog&, const FlightRunState&, bool surveyed, const OrbitalSiteProgress*);
void recordTutorialTouchdown(GameState&, const ContentCatalog&);
void migrateArrivalTutorials(GameState&, const ContentCatalog&);
}
