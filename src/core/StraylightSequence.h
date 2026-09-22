#pragma once
#include "core/ExpeditionSystem.h"

namespace rocket {
constexpr bool straylightIdentityKnown(StraylightStage stage) { return stage >= StraylightStage::FirstContact; }
SystemDefinition solarPresentationSystem(const GameState&);
bool straylightRevealInRange(const GameState&, const FlightRunState&);
bool straylightOwnsPresentation(const GameState&);
bool straylightCommitted(const GameState&);
double straylightCinematicDuration(StraylightStage);
bool revealStraylightOnDelivery(GameState&, const ContentCatalog&);
bool reconcileStraylightSequence(GameState&, const ContentCatalog&);
bool applyStraylightAction(GameState&, const ContentCatalog&, std::string_view action);
bool finishStraylightCinematic(GameState&, const ContentCatalog&);
std::optional<CampaignObjective> straylightObjective(const GameState&);
}
