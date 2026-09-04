#pragma once

#include "core/GameTypes.h"

namespace rocket {

struct ContentCatalog;
struct GameState;

struct PayloadTransferPlan {
    MaterialInventory toContract;
    MaterialInventory toShipHold;
    MaterialInventory remainingAtSource;
    int holdMassBefore = 0;
    int holdMassAfter = 0;
    bool capacityBlocked = false;
};

int materialCargoMass(const MaterialInventory& materials);
int shipHoldCapacity(const GameState& state, const ContentCatalog& catalog);
int shipHoldUsed(const GameState& state);
int shipHoldAvailable(const GameState& state, const ContentCatalog& catalog);

MaterialInventory activeContractMaterialNeed(
    const GameState& state,
    const ContentCatalog& catalog,
    std::string_view destinationId);

PayloadTransferPlan planPayloadTransfer(
    const MaterialInventory& source,
    const MaterialInventory& contractNeed,
    const MaterialInventory& existingShipHold,
    int holdCapacity);

void applyPayloadTransferPlan(
    GameState& state,
    const ContentCatalog& catalog,
    std::string_view destinationId,
    const PayloadTransferPlan& plan);

bool validateCargoRequirements(const ContentCatalog& catalog, std::string* error = nullptr);

} // namespace rocket
