#pragma once

#include "render/SceneAtlas.generated.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace rocket {

// SceneComposer maps each logical source UV rectangle through this record
// before packing its frame-lifetime instance. Sprite selection remains in
// original normalized source space at the composition API boundary.
struct SceneAtlasUvRect {
    std::uint8_t page = 0;
    float u0 = 0.0F;
    float v0 = 0.0F;
    float u1 = 0.0F;
    float v1 = 0.0F;
    bool valid = false;
};

inline std::size_t sceneAtlasPageForTexture(TextureId textureId) noexcept
{
    const std::size_t textureIndexValue = textureIndex(textureId);
    if (textureId == TextureId::None || textureIndexValue >= kSceneAtlasTextures.size()) {
        return kSceneAtlasPages.size();
    }
    const SceneAtlasTexture& texture = kSceneAtlasTextures[textureIndexValue];
    if (texture.frameCount == 0 || texture.firstFrame >= kSceneAtlasFrames.size()) {
        return kSceneAtlasPages.size();
    }
    return kSceneAtlasFrames[texture.firstFrame].page;
}

inline SceneAtlasUvRect mapSceneAtlasUvRect(
    TextureId textureId,
    float sourceU0,
    float sourceV0,
    float sourceU1,
    float sourceV1) noexcept
{
    const std::size_t textureIndexValue = textureIndex(textureId);
    if (textureId == TextureId::None || textureIndexValue >= kSceneAtlasTextures.size()) {
        return {};
    }
    const SceneAtlasTexture& texture = kSceneAtlasTextures[textureIndexValue];
    if (texture.frameCount == 0 || texture.columns == 0 || texture.rows == 0) {
        return {};
    }

    const float midpointU = std::clamp((sourceU0 + sourceU1) * 0.5F, 0.0F, 1.0F);
    const float midpointV = std::clamp((sourceV0 + sourceV1) * 0.5F, 0.0F, 1.0F);
    const int column = std::clamp(
        static_cast<int>(std::floor(midpointU * static_cast<float>(texture.columns))),
        0,
        static_cast<int>(texture.columns) - 1);
    const int row = std::clamp(
        static_cast<int>(std::floor(midpointV * static_cast<float>(texture.rows))),
        0,
        static_cast<int>(texture.rows) - 1);
    const std::size_t localFrame = static_cast<std::size_t>(row) * texture.columns
        + static_cast<std::size_t>(column);
    if (localFrame >= texture.frameCount
        || static_cast<std::size_t>(texture.firstFrame) + localFrame >= kSceneAtlasFrames.size()) {
        return {};
    }

    const SceneAtlasFrame& frame = kSceneAtlasFrames[texture.firstFrame + localFrame];
    if (frame.page >= kSceneAtlasPages.size()) {
        return {};
    }
    const SceneAtlasPage& page = kSceneAtlasPages[frame.page];
    const float localU0 = sourceU0 * static_cast<float>(texture.columns) - static_cast<float>(column);
    const float localV0 = sourceV0 * static_cast<float>(texture.rows) - static_cast<float>(row);
    const float localU1 = sourceU1 * static_cast<float>(texture.columns) - static_cast<float>(column);
    const float localV1 = sourceV1 * static_cast<float>(texture.rows) - static_cast<float>(row);
    const float inversePageWidth = 1.0F / static_cast<float>(page.width);
    const float inversePageHeight = 1.0F / static_cast<float>(page.height);
    const auto mapU = [&](float value) {
        return (static_cast<float>(frame.x) + value * static_cast<float>(frame.width))
            * inversePageWidth;
    };
    const auto mapV = [&](float value) {
        return (static_cast<float>(frame.y) + value * static_cast<float>(frame.height))
            * inversePageHeight;
    };
    return {
        frame.page,
        mapU(localU0),
        mapV(localV0),
        mapU(localU1),
        mapV(localV1),
        true,
    };
}

} // namespace rocket
