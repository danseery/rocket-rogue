#pragma once

#include "core/DetailPresentation.h"
#include "core/GameFormat.h"
#include "core/GameState.h"
#include "core/GameText.h"

#include <algorithm>
#include <string>
#include <vector>

namespace rocket {

inline std::vector<DetailPresentationRow> crewDetailsPresentation(const GameState& state, const ContentCatalog& catalog)
{
    std::vector<DetailPresentationRow> rows;
    const Astronaut* astronaut = activeAstronaut(state);
    if (astronaut != nullptr) {
        rows.push_back(detailPresentationRow(text::panel::details::active, astronaut->name));
        rows.push_back(detailPresentationRow(text::panel::details::crewClass, astronaut->background));
        rows.push_back(detailPresentationRow(text::panel::details::trait, astronaut->trait));
        if (const CrewArchetypeDefinition* archetype = catalog.findCrewArchetype(astronaut->archetypeId)) {
            rows.push_back(detailPresentationRow("Species / role", archetype->species + " / " + archetype->role));
            rows.push_back(detailPresentationRow(archetype->perkName, archetype->perkDetail));
        }
        rows.push_back(detailPresentationRow(text::panel::details::status, std::string(toString(astronaut->status))));
    } else {
        rows.push_back(detailPresentationRow(text::panel::details::active, text::panel::noneCleared));
    }

    return rows;
}

} // namespace rocket
