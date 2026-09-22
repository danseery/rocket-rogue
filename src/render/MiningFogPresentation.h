#pragma once

#include <algorithm>
#include <span>
#include "core/GameTypes.h"

namespace rocket {
// Underground layer space above row zero is not explorable terrain in this
// view. Keep its veil independent of scan/reveal flags and actor lights.
inline float miningUpperBoundaryFog(int depth, float gridY)
{
    if (depth <= 0) return 0.0F;
    const float t = std::clamp(gridY / 2.0F, 0.0F, 1.0F);
    return 1.0F - t * t * (3.0F - 2.0F * t);
}

// Trace only known open passage against real rock in the previous layer.
// Coordinates are relative to the current baseline. Never cap open layer
// seams or outline unrevealed cavities/resources through the permanent fog.
template<class Emit>
void forEachMiningUpperPassageEdge(int depth, std::span<const MiningCell> cells,
    int width, int height, Emit emit)
{
    if (depth <= 0 || width <= 0 || height <= 0 ||
        cells.size() < static_cast<std::size_t>(width) * height) return;
    const auto solid = [&](int x, int y) {
        return x >= 0 && x < width && y >= 0 && y < height &&
            cells[static_cast<std::size_t>(y) * width + x].material != MiningCellMaterial::Empty;
    };
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const auto& cell = cells[static_cast<std::size_t>(y) * width + x];
        if (!cell.revealed || cell.material != MiningCellMaterial::Empty) continue;
        const int top = y - height;
        if (solid(x-1,y)) emit(x,top,x,top+1);
        if (solid(x+1,y)) emit(x+1,top,x+1,top+1);
        if (solid(x,y-1)) emit(x,top,x+1,top);
        if (solid(x,y+1)) emit(x,top+1,x+1,top+1);
    }
}
}
