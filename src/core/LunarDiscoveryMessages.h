#pragma once
namespace rocket {
struct GameState;
struct ContentCatalog;
bool reconcileLunarMessages(GameState &, const ContentCatalog &);
} // namespace rocket
