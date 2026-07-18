#include "render/SceneAtlas.h"

#include <cassert>
#include <cmath>
#include <cstddef>

namespace {

bool near(float left, float right)
{
    return std::fabs(left - right) < 0.00001F;
}

void expectFrameMapping(
    rocket::TextureId textureId,
    std::size_t localFrame,
    float sourceU0,
    float sourceV0,
    float sourceU1,
    float sourceV1)
{
    using namespace rocket;
    const SceneAtlasTexture& texture = kSceneAtlasTextures[textureIndex(textureId)];
    const SceneAtlasFrame& frame = kSceneAtlasFrames[texture.firstFrame + localFrame];
    const SceneAtlasPage& page = kSceneAtlasPages[frame.page];
    const SceneAtlasUvRect mapped = mapSceneAtlasUvRect(
        textureId,
        sourceU0,
        sourceV0,
        sourceU1,
        sourceV1);
    assert(mapped.valid);
    assert(mapped.page == frame.page);
    assert(near(mapped.u0, static_cast<float>(frame.x) / static_cast<float>(page.width)));
    assert(near(mapped.v0, static_cast<float>(frame.y) / static_cast<float>(page.height)));
    assert(near(
        mapped.u1,
        static_cast<float>(frame.x + frame.width) / static_cast<float>(page.width)));
    assert(near(
        mapped.v1,
        static_cast<float>(frame.y + frame.height) / static_cast<float>(page.height)));
}

} // namespace

int main()
{
    using namespace rocket;

    assert(!mapSceneAtlasUvRect(TextureId::None, 0.0F, 0.0F, 1.0F, 1.0F).valid);
    assert(sceneAtlasPageForTexture(TextureId::None) == kSceneAtlasPages.size());
    expectFrameMapping(TextureId::Earth, 0, 0.0F, 0.0F, 1.0F, 1.0F);
    expectFrameMapping(TextureId::Explosion, 3, 3.0F / 8.0F, 0.0F, 4.0F / 8.0F, 1.0F);
    expectFrameMapping(
        TextureId::LocalSolarBackground,
        2,
        2.0F / 4.0F,
        0.0F,
        3.0F / 4.0F,
        1.0F);

    for (std::size_t textureIndexValue = 1; textureIndexValue < kSceneAtlasTextures.size(); ++textureIndexValue) {
        const SceneAtlasTexture& texture = kSceneAtlasTextures[textureIndexValue];
        assert(texture.frameCount > 0);
        const std::uint8_t page = kSceneAtlasFrames[texture.firstFrame].page;
        assert(sceneAtlasPageForTexture(static_cast<TextureId>(textureIndexValue)) == page);
        for (std::size_t frameIndex = 0; frameIndex < texture.frameCount; ++frameIndex) {
            assert(kSceneAtlasFrames[texture.firstFrame + frameIndex].page < kSceneAtlasPages.size());
        }
    }

    const SceneAtlasUvRect flipped = mapSceneAtlasUvRect(TextureId::Earth, 1.0F, 1.0F, 0.0F, 0.0F);
    assert(flipped.valid);
    assert(flipped.u0 > flipped.u1);
    assert(flipped.v0 > flipped.v1);
    return 0;
}
