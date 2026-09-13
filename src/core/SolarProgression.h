#pragma once

#include "core/Content.h"
#include "core/GameState.h"

#include <string>
#include <string_view>

namespace rocket {

struct ScenarioObjectivePresentation;

struct SolarMissionAcceptanceOutcome {
    // An already accepted mission is a successful acknowledgement, but must
    // not apply its rewards again.
    bool accepted = false;
    bool applied = false;
    std::string message;
};

const SolarMissionDefinition* solarMissionForBody(const ContentCatalog&, std::string_view bodyId);
bool solarMissionClaimed(const GameState&, const ContentCatalog&, const SolarMissionDefinition&);
bool solarMissionAvailable(const GameState&, const SolarMissionDefinition&);
bool solarMissionAccepted(const GameState&, const ContentCatalog&, const SolarMissionDefinition&);
SolarMissionAcceptanceOutcome acceptSolarMission(
    GameState&, const ContentCatalog&, const SolarMissionDefinition&);
ScenarioObjectivePresentation solarMissionAcceptanceForBody(
    const GameState&, const ContentCatalog&, std::string_view bodyId);
const SolarMissionDefinition* nextSolarMission(const GameState&, const ContentCatalog&);
bool solarBodyRevealed(const GameState&, const ContentCatalog&, std::string_view bodyId);
bool validateSolarMissionCatalog(const ContentCatalog&, std::string* error = nullptr);
bool reconcileSolarMissionMessages(GameState&, const ContentCatalog&);
ScenarioObjectivePresentation solarMissionObjectiveForBody(
    const GameState&, const ContentCatalog&, std::string_view bodyId);

} // namespace rocket
