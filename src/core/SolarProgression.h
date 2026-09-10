#pragma once

#include "core/Content.h"
#include "core/GameState.h"

#include <string>
#include <string_view>

namespace rocket {

struct ScenarioObjectivePresentation;

const SolarMissionDefinition* solarMissionForBody(const ContentCatalog&, std::string_view bodyId);
bool solarMissionClaimed(const GameState&, const ContentCatalog&, const SolarMissionDefinition&);
bool solarMissionAvailable(const GameState&, const SolarMissionDefinition&);
const SolarMissionDefinition* nextSolarMission(const GameState&, const ContentCatalog&);
bool solarBodyRevealed(const GameState&, const ContentCatalog&, std::string_view bodyId);
bool validateSolarMissionCatalog(const ContentCatalog&, std::string* error = nullptr);
bool reconcileSolarMissionMessages(GameState&, const ContentCatalog&);
ScenarioObjectivePresentation solarMissionObjectiveForBody(
    const GameState&, const ContentCatalog&, std::string_view bodyId);

} // namespace rocket
