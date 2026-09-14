#pragma once
#include "core/ExpeditionSystem.h"

namespace rocket {
bool straylightOwnsPresentation(const GameState&);
bool straylightCommitted(const GameState&);
double straylightCinematicDuration(StraylightStage);
bool revealStraylightOnDelivery(GameState&, const ContentCatalog&);
bool reconcileStraylightSequence(GameState&, const ContentCatalog&);
bool applyStraylightAction(GameState&, const ContentCatalog&, std::string_view action);
bool finishStraylightCinematic(GameState&, const ContentCatalog&);
std::optional<CampaignObjective> straylightObjective(const GameState&);
}
