#include "render/SceneAtlas.h"
#include "render/SceneComposer.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <type_traits>
#include <vector>

namespace rocket {

struct SceneComposerTestAccess {
    static const ScenePacket& composeLines(
        SceneComposer& composer,
        const std::vector<SceneVertex>& vertices,
        float width,
        bool worldSpace)
    {
        RenderSnapshot snapshot;
        composer.beginFrame(snapshot);
        composer.submitLines(vertices, width, worldSpace);
        composer.finalizePacket();
        return composer.packet_;
    }
};

} // namespace rocket

namespace {

using rocket::Color;
using rocket::CoordinateSpace;
using rocket::PipelineClass;
using rocket::PackedSceneInstance;
using rocket::PackedSceneVertex;
using rocket::RenderSnapshot;
using rocket::SceneComposer;
using rocket::SceneDraw;
using rocket::SceneDrawType;
using rocket::SceneInstance;
using rocket::SceneInstanceShape;
using rocket::SceneInstanceStream;
using rocket::ScenePacket;
using rocket::SceneVertex;
using rocket::SceneVertexStream;
using rocket::TextureId;

static_assert(std::is_standard_layout_v<SceneVertex>);
static_assert(std::is_trivially_copyable_v<SceneVertex>);
static_assert(sizeof(SceneVertex) == sizeof(float) * 8U);
static_assert(offsetof(SceneVertex, x) == sizeof(float) * 0U);
static_assert(offsetof(SceneVertex, y) == sizeof(float) * 1U);
static_assert(offsetof(SceneVertex, r) == sizeof(float) * 2U);
static_assert(offsetof(SceneVertex, g) == sizeof(float) * 3U);
static_assert(offsetof(SceneVertex, b) == sizeof(float) * 4U);
static_assert(offsetof(SceneVertex, a) == sizeof(float) * 5U);
static_assert(offsetof(SceneVertex, u) == sizeof(float) * 6U);
static_assert(offsetof(SceneVertex, v) == sizeof(float) * 7U);
static_assert(std::is_standard_layout_v<PackedSceneVertex>);
static_assert(std::is_trivially_copyable_v<PackedSceneVertex>);
static_assert(sizeof(PackedSceneVertex) == 12U);
static_assert(offsetof(PackedSceneVertex, x) == 0U);
static_assert(offsetof(PackedSceneVertex, y) == 2U);
static_assert(offsetof(PackedSceneVertex, r) == 4U);
static_assert(offsetof(PackedSceneVertex, g) == 5U);
static_assert(offsetof(PackedSceneVertex, b) == 6U);
static_assert(offsetof(PackedSceneVertex, a) == 7U);
static_assert(offsetof(PackedSceneVertex, u) == 8U);
static_assert(offsetof(PackedSceneVertex, v) == 10U);
static_assert(std::is_standard_layout_v<PackedSceneInstance>);
static_assert(std::is_trivially_copyable_v<PackedSceneInstance>);
static_assert(sizeof(PackedSceneInstance) == 28U);
static_assert(offsetof(PackedSceneInstance, centerX) == 0U);
static_assert(offsetof(PackedSceneInstance, axisXx) == 4U);
static_assert(offsetof(PackedSceneInstance, axisYx) == 8U);
static_assert(offsetof(PackedSceneInstance, r) == 12U);
static_assert(offsetof(PackedSceneInstance, u0) == 16U);
static_assert(offsetof(PackedSceneInstance, u1) == 20U);
static_assert(offsetof(PackedSceneInstance, shape) == 24U);

void assertValidDrawRanges(const ScenePacket& packet)
{
    std::size_t nextFrameVertex = 0;
    std::size_t nextMiningTerrainVertex = 0;
    std::size_t nextFrameInstance = 0;
    std::size_t nextMiningTerrainInstance = 0;
    for (const SceneDraw& draw : packet.draws) {
        if (draw.drawType == SceneDrawType::InstancedQuad) {
            assert(draw.vertexCount == 6U);
            assert(draw.instanceCount > 0U);
            std::size_t& nextInstance = draw.instanceStream == SceneInstanceStream::MiningTerrain
                ? nextMiningTerrainInstance
                : nextFrameInstance;
            const std::size_t streamSize = draw.instanceStream == SceneInstanceStream::MiningTerrain
                ? packet.miningTerrainInstances.size()
                : packet.instances.size();
            assert(draw.firstInstance == nextInstance);
            nextInstance = static_cast<std::size_t>(draw.firstInstance) + draw.instanceCount;
            assert(nextInstance <= streamSize);
            continue;
        }
        assert(draw.vertexCount > 0U);
        assert(draw.vertexCount % 3U == 0U);
        std::size_t& nextVertex = draw.vertexStream == SceneVertexStream::MiningTerrain
            ? nextMiningTerrainVertex
            : nextFrameVertex;
        const std::size_t streamSize = draw.vertexStream == SceneVertexStream::MiningTerrain
            ? packet.miningTerrainVertices.size()
            : packet.vertices.size();
        assert(draw.firstVertex == nextVertex);
        nextVertex = static_cast<std::size_t>(draw.firstVertex) + draw.vertexCount;
        assert(nextVertex <= streamSize);
    }
    assert(nextFrameVertex == packet.vertices.size());
    assert(nextMiningTerrainVertex == packet.miningTerrainVertices.size());
    assert(nextFrameInstance == packet.instances.size());
    assert(nextMiningTerrainInstance == packet.miningTerrainInstances.size());
}

bool sameVertex(const PackedSceneVertex& left, const PackedSceneVertex& right)
{
    return std::memcmp(&left, &right, sizeof(PackedSceneVertex)) == 0;
}

bool sameInstance(const PackedSceneInstance& left, const PackedSceneInstance& right)
{
    return std::memcmp(&left, &right, sizeof(PackedSceneInstance)) == 0;
}

void testPackedVertexConversion()
{
    assert(rocket::packSceneHalf(0.0F) == 0x0000U);
    assert(rocket::packSceneHalf(-0.0F) == 0x8000U);
    assert(rocket::packSceneHalf(1.0F) == 0x3c00U);
    assert(rocket::packSceneHalf(-2.0F) == 0xc000U);
    assert(rocket::packSceneHalf(65504.0F) == 0x7bffU);
    assert(rocket::packSceneHalf(70000.0F) == 0x7bffU);
    assert(rocket::packSceneHalf(-70000.0F) == 0xfbffU);
    assert(rocket::packSceneHalf(std::numeric_limits<float>::infinity()) == 0x7bffU);
    assert(rocket::packSceneHalf(-std::numeric_limits<float>::infinity()) == 0xfbffU);
    assert(rocket::packSceneHalf(std::numeric_limits<float>::quiet_NaN()) == 0x0000U);
    assert(rocket::packSceneHalf(std::ldexp(1.0F, -24)) == 0x0001U);
    assert(rocket::packSceneHalf(std::ldexp(1.0F, -25)) == 0x0000U);
    assert(rocket::packSceneHalf(1.00048828125F) == 0x3c00U);
    assert(rocket::packSceneHalf(1.00146484375F) == 0x3c02U);
    assert(rocket::unpackSceneHalf(rocket::packSceneHalf(1.0F / 3.0F)) > 0.3330F);
    assert(rocket::unpackSceneHalf(rocket::packSceneHalf(1.0F / 3.0F)) < 0.3335F);
    assert(rocket::unpackSceneHalf(0x0001U) == std::ldexp(1.0F, -24));

    assert(rocket::packSceneUnorm8(-1.0F) == 0U);
    assert(rocket::packSceneUnorm8(0.5F) == 128U);
    assert(rocket::packSceneUnorm8(1.0F) == 255U);
    assert(rocket::packSceneUnorm8(2.0F) == 255U);
    assert(rocket::packSceneUnorm8(std::numeric_limits<float>::infinity()) == 255U);
    assert(rocket::packSceneUnorm8(-std::numeric_limits<float>::infinity()) == 0U);
    assert(rocket::packSceneUnorm8(std::numeric_limits<float>::quiet_NaN()) == 0U);
    assert(rocket::packSceneUnorm16(-1.0F) == 0U);
    assert(rocket::packSceneUnorm16(0.5F) == 32768U);
    assert(rocket::packSceneUnorm16(1.0F) == 65535U);
    assert(rocket::packSceneUnorm16(2.0F) == 65535U);
    assert(std::abs(rocket::unpackSceneUnorm16(32768U) - 0.5F) < 0.00001F);
    // On a 4096-pixel atlas this is less than one sixteenth of a texel,
    // avoiding the multi-texel quantization of half floats near UV 1.0.
    assert((1.0F / 65535.0F) * 4096.0F < 0.063F);

    const SceneVertex source {
        123.456F,
        -0.125F,
        1.25F,
        0.5F,
        -0.1F,
        0.75F,
        0.333333F,
        1.0F
    };
    const PackedSceneVertex packed = rocket::packSceneVertex(source);
    const PackedSceneVertex repeated = rocket::packSceneVertex(source);
    assert(sameVertex(packed, repeated));
    assert(packed.r == 255U);
    assert(packed.g == 128U);
    assert(packed.b == 0U);
    assert(packed.a == 191U);
    const SceneVertex unpacked = rocket::unpackSceneVertex(packed);
    assert(std::abs(unpacked.x - source.x) < 0.04F);
    assert(unpacked.y == source.y);
    assert(std::abs(unpacked.g - source.g) < (1.0F / 255.0F));
    assert(std::abs(unpacked.a - source.a) < (1.0F / 255.0F));
    assert(std::abs(unpacked.u - source.u) < 0.0002F);

    const SceneInstance instance {
        0.25F, -0.5F,
        0.125F, 0.25F,
        -0.375F, 0.5F,
        {1.0F, 0.5F, 0.0F, 0.75F},
        0.125F, 0.25F, 0.625F, 0.75F,
        SceneInstanceShape::RadialGlow,
        72
    };
    const PackedSceneInstance packedInstance = rocket::packSceneInstance(instance);
    const PackedSceneInstance repeatedInstance = rocket::packSceneInstance(instance);
    assert(sameInstance(packedInstance, repeatedInstance));
    const SceneInstance unpackedInstance = rocket::unpackSceneInstance(packedInstance);
    assert(packedInstance.segments == 72U);
    assert(packedInstance.shape == static_cast<std::uint8_t>(SceneInstanceShape::RadialGlow));
    assert(unpackedInstance.centerX == instance.centerX);
    assert(unpackedInstance.axisYx == instance.axisYx);
    assert(unpackedInstance.shape == instance.shape);
    assert(unpackedInstance.segments == instance.segments);
    assert(!unpackedInstance.textured);

    SceneInstance texturedInstance = instance;
    texturedInstance.textured = true;
    const PackedSceneInstance packedTextured = rocket::packSceneInstance(texturedInstance);
    assert((packedTextured.shape & rocket::kSceneInstanceTexturedBit) != 0U);
    assert((packedTextured.shape & rocket::kSceneInstanceShapeMask)
        == static_cast<std::uint8_t>(SceneInstanceShape::RadialGlow));
    const SceneInstance unpackedTextured = rocket::unpackSceneInstance(packedTextured);
    assert(unpackedTextured.textured);
    assert(unpackedTextured.shape == SceneInstanceShape::RadialGlow);

    assert(rocket::compatibleSceneAtlasPages(rocket::kNoSceneAtlasPage, 0U));
    assert(rocket::compatibleSceneAtlasPages(1U, rocket::kNoSceneAtlasPage));
    assert(rocket::compatibleSceneAtlasPages(1U, 1U));
    assert(!rocket::compatibleSceneAtlasPages(0U, 1U));
    assert(rocket::mergedSceneAtlasPage(rocket::kNoSceneAtlasPage, 1U) == 1U);
    assert(rocket::mergedSceneAtlasPage(0U, rocket::kNoSceneAtlasPage) == 0U);
}

void testManifestAndLogicalTextureMapping()
{
    assert(rocket::kSceneAtlasTextures.size() == rocket::textureIndex(TextureId::Count));
    assert(!rocket::kSceneAtlasPages.empty());
    for (std::size_t index = 1; index < rocket::kSceneAtlasTextures.size(); ++index) {
        const rocket::SceneAtlasTexture& texture = rocket::kSceneAtlasTextures[index];
        assert(texture.frameCount > 0U);
        assert(texture.firstFrame < rocket::kSceneAtlasFrames.size());
        assert(rocket::sceneAtlasPageForTexture(static_cast<TextureId>(index))
            < rocket::kSceneAtlasPages.size());
    }

    const rocket::SceneAtlasTexture& background =
        rocket::kSceneAtlasTextures[rocket::textureIndex(TextureId::LocalSolarBackground)];
    assert(background.sourceWidth == 4096U);
    assert(background.sourceHeight == 576U);
    assert(background.frameWidth == 1024U);
    assert(background.frameHeight == 576U);
    assert(background.columns == 4U);
    assert(background.frameCount == 4U);

    const rocket::SceneAtlasTexture& capybara =
        rocket::kSceneAtlasTextures[rocket::textureIndex(TextureId::HeroicCapybara)];
    assert(capybara.sourceWidth == 1024U);
    assert(capybara.sourceHeight == 1024U);
    assert(capybara.frameCount == 1U);
}

void testCampaignIntroductionDrawsHeroicCapybara()
{
    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F, -1.0F});
    composer.setTextureReady(TextureId::LocalSolarBackground, true);
    composer.setTextureReady(TextureId::HeroicCapybara, true);

    RenderSnapshot introduction;
    introduction.screen = rocket::Screen::StoryBriefing;
    introduction.campaignStoryIntroduction = true;
    const ScenePacket& introductionPacket = composer.compose(introduction);
    assert(std::any_of(introductionPacket.draws.begin(), introductionPacket.draws.end(), [](const SceneDraw& draw) {
        return draw.texture == TextureId::HeroicCapybara;
    }));

    RenderSnapshot straylight;
    straylight.screen = rocket::Screen::StoryBriefing;
    straylight.straylightStoryReveal = true;
    const ScenePacket& straylightPacket = composer.compose(straylight);
    assert(std::none_of(straylightPacket.draws.begin(), straylightPacket.draws.end(), [](const SceneDraw& draw) {
        return draw.texture == TextureId::HeroicCapybara;
    }));
}

void testPolygonInstanceMatchesTriangleFan()
{
    constexpr float pi = 3.14159265358979323846F;
    for (const int segments : {8, 14, 24, 48, 72, 88}) {
        const float sector = 2.0F * pi / static_cast<float>(segments);
        const float vertex1X = std::cos(sector);
        const float vertex1Y = std::sin(sector);
        for (const float sectorShare : {0.1F, 0.5F, 0.9F}) {
            const float angle = sector * sectorShare;
            const float centeredAngle = angle - sector * 0.5F;
            const float boundaryRadius = std::cos(sector * 0.5F) / std::cos(centeredAngle);
            for (const float radiusShare : {0.0F, 0.25F, 0.75F, 0.999F}) {
                const float radius = boundaryRadius * radiusShare;
                const float pointX = std::cos(angle) * radius;
                const float pointY = std::sin(angle) * radius;

                // Barycentric center weight in the former fan triangle
                // [(0,0), (1,0), (cos(sector),sin(sector))].
                const float vertex1Weight = pointY / vertex1Y;
                const float vertex0Weight = pointX - vertex1Weight * vertex1X;
                const float fanCenterAlpha = 1.0F - vertex0Weight - vertex1Weight;
                const float instanceAlpha = 1.0F - radius / boundaryRadius;
                assert(std::abs(fanCenterAlpha - instanceAlpha) < 0.00001F);
            }

            // Hard polygon circles retain the old chord, not a new analytic
            // circular edge. The same normalized radius drives glow alpha.
            assert(boundaryRadius <= 1.00001F);
            assert(boundaryRadius >= std::cos(sector * 0.5F) - 0.00001F);
            assert((boundaryRadius * 1.001F) / boundaryRadius > 1.0F);
        }
    }
}

void testOrderedBatchingAndWideLineInstancing()
{
    SceneComposer composer;
    composer.setViewport({1280, 800, 2560, 1600, 2.0F, -1.0F});
    composer.setPresentationTime(0.0);
    composer.setTextureReady(TextureId::LocalSolarBackground, true);
    composer.setTextureReady(TextureId::Earth, true);

    RenderSnapshot snapshot;
    snapshot.titleScreen = true;
    snapshot.animationTime = 0.0;
    const ScenePacket& packet = composer.compose(snapshot);
    assertValidDrawRanges(packet);
    assert(packet.vertices.size_bytes() == packet.vertices.size() * sizeof(PackedSceneVertex));
    assert(packet.vertices.empty()
        || packet.vertices.size_bytes() * 2U < packet.vertices.size() * sizeof(SceneVertex));

    // Solid lines and textured sprites share one ordered instance pipeline.
    // Coordinate space keeps the clip-space backdrop separate, while the
    // world-space orbit line merges into the surrounding instance sequence.
    assert(packet.draws.size() == 2U);
    assert(packet.draws[0].drawType == SceneDrawType::InstancedQuad);
    assert(packet.draws[0].pipeline == PipelineClass::Textured);
    assert(packet.draws[0].atlasPage
        == rocket::sceneAtlasPageForTexture(TextureId::LocalSolarBackground));
    assert(packet.draws[0].coordinateSpace == CoordinateSpace::Clip);
    assert(packet.draws[0].instanceCount == 3U);
    assert(!rocket::unpackSceneInstance(
        packet.instances[packet.draws[0].firstInstance]).textured);
    for (std::size_t frame = 0; frame < 2U; ++frame) {
        const float sourceU0 = static_cast<float>(frame) / 4.0F;
        const float sourceU1 = static_cast<float>(frame + 1U) / 4.0F;
        const rocket::SceneAtlasUvRect expected = rocket::mapSceneAtlasUvRect(
            TextureId::LocalSolarBackground,
            sourceU0,
            0.0F,
            sourceU1,
            1.0F);
        const SceneInstance actual = rocket::unpackSceneInstance(
            packet.instances[packet.draws[0].firstInstance + 1U + frame]);
        constexpr float unorm16Tolerance = 2.0F / 65535.0F;
        assert(expected.valid);
        assert(actual.textured);
        assert(expected.page == packet.draws[0].atlasPage);
        assert(std::abs(actual.u0 - expected.u0) < unorm16Tolerance);
        assert(std::abs(actual.v0 - expected.v0) < unorm16Tolerance);
        assert(std::abs(actual.u1 - expected.u1) < unorm16Tolerance);
        assert(std::abs(actual.v1 - expected.v1) < unorm16Tolerance);
    }

    assert(packet.vertices.empty());
    assert(packet.draws[1].drawType == SceneDrawType::InstancedQuad);
    assert(packet.draws[1].pipeline == PipelineClass::Textured);
    assert(packet.draws[1].coordinateSpace == CoordinateSpace::World);
    assert(packet.draws[1].atlasPage == rocket::sceneAtlasPageForTexture(TextureId::Earth));
    assert(packet.draws[1].instanceCount >= 82U);
    std::size_t texturedInstances = 0;
    for (std::size_t index = 0; index < packet.draws[1].instanceCount; ++index) {
        if (rocket::unpackSceneInstance(
                packet.instances[packet.draws[1].firstInstance + index]).textured) {
            ++texturedInstances;
        }
    }
    assert(texturedInstances == 1U);

    const SceneInstance radialGlow = rocket::unpackSceneInstance(
        packet.instances[packet.draws[1].firstInstance]);
    assert(radialGlow.shape == SceneInstanceShape::RadialGlow);
    assert(radialGlow.segments == 64U);

    // drawTitleBackdrop emits a 64-segment radial glow followed by an
    // 80-segment, one-physical-pixel orbit line. The line instance axes use
    // the same physical-pixel perpendicular math as the triangle fallback.
    const SceneInstance orbitLine = rocket::unpackSceneInstance(
        packet.instances[packet.draws[1].firstInstance + 1U]);
    assert(orbitLine.shape == SceneInstanceShape::Rectangle);
    assert(!orbitLine.textured);
    const float densityX = 2560.0F / 1280.0F;
    const float densityY = 1600.0F / 800.0F;
    const float widthPixelsX = orbitLine.axisXx * 2.0F * packet.transform.worldUnitX * densityX;
    const float widthPixelsY = orbitLine.axisXy * 2.0F * packet.transform.worldUnitY * densityY;
    const float physicalWidth = std::hypot(widthPixelsX, widthPixelsY);
    // Half-float instance axes preserve the line while allowing sub-pixel
    // quantization at high DPI. It must remain visibly one pixel.
    assert(physicalWidth > 0.5F);
    assert(physicalWidth < 1.5F);
    assert(std::hypot(orbitLine.axisYx, orbitLine.axisYy) > 0.0F);

    // Every quad/polygon instance uploads 28 bytes instead of six packed
    // vertices (72 bytes), while the draw record order remains unchanged.
    assert(packet.instances.size_bytes() < packet.instances.size() * 6U * sizeof(PackedSceneVertex));
}

void testUniformAndGradientLineOrdering()
{
    SceneComposer composer;
    composer.setViewport({1280, 800, 2560, 1200, 1.5F, -1.0F});

    constexpr Color firstColor {0.20F, 0.40F, 0.60F, 0.80F};
    constexpr Color secondColor {0.90F, 0.30F, 0.10F, 0.50F};
    constexpr Color thirdColor {0.30F, 0.80F, 0.40F, 0.70F};
    std::vector<SceneVertex> segments {
        {-0.70F, -0.45F, firstColor.r, firstColor.g, firstColor.b, firstColor.a},
        { 0.10F, -0.45F, firstColor.r, firstColor.g, firstColor.b, firstColor.a},
        {-0.35F, -0.20F, firstColor.r, firstColor.g, firstColor.b, firstColor.a},
        { 0.30F,  0.35F, secondColor.r, secondColor.g, secondColor.b, secondColor.a},
        // A zero-length uniform segment is a no-op and must not introduce an
        // extra representation boundary around the pending gradient.
        { 0.20F,  0.20F, thirdColor.r, thirdColor.g, thirdColor.b, thirdColor.a},
        { 0.20F,  0.20F, thirdColor.r, thirdColor.g, thirdColor.b, thirdColor.a},
        {-0.55F,  0.50F, thirdColor.r, thirdColor.g, thirdColor.b, thirdColor.a},
        { 0.45F, -0.25F, thirdColor.r, thirdColor.g, thirdColor.b, thirdColor.a},
        { 0.58F, -0.35F, secondColor.r, secondColor.g, secondColor.b, secondColor.a},
        { 0.58F,  0.45F, secondColor.r, secondColor.g, secondColor.b, secondColor.a},
    };

    const ScenePacket& packet = rocket::SceneComposerTestAccess::composeLines(
        composer, segments, 3.0F, true);
    assertValidDrawRanges(packet);
    assert(packet.draws.size() == 3U);
    assert(packet.draws[0].drawType == SceneDrawType::InstancedQuad);
    assert(packet.draws[0].instanceCount == 1U);
    assert(packet.draws[1].drawType == SceneDrawType::Triangles);
    assert(packet.draws[1].vertexCount == 6U);
    assert(packet.draws[2].drawType == SceneDrawType::InstancedQuad);
    assert(packet.draws[2].instanceCount == 2U);
    assert(packet.instances.size() == 3U);
    assert(packet.vertices.size() == 6U);

    // The fallback retains the original endpoint color interpolation across
    // its exact six-vertex quad order.
    const std::array<SceneVertex, 6> gradientVertices {
        rocket::unpackSceneVertex(packet.vertices[0]),
        rocket::unpackSceneVertex(packet.vertices[1]),
        rocket::unpackSceneVertex(packet.vertices[2]),
        rocket::unpackSceneVertex(packet.vertices[3]),
        rocket::unpackSceneVertex(packet.vertices[4]),
        rocket::unpackSceneVertex(packet.vertices[5]),
    };
    const auto closeColor = [](const SceneVertex& vertex, Color expected) {
        constexpr float tolerance = 1.0F / 255.0F;
        return std::abs(vertex.r - expected.r) <= tolerance
            && std::abs(vertex.g - expected.g) <= tolerance
            && std::abs(vertex.b - expected.b) <= tolerance
            && std::abs(vertex.a - expected.a) <= tolerance;
    };
    assert(closeColor(gradientVertices[0], firstColor));
    assert(closeColor(gradientVertices[1], firstColor));
    assert(closeColor(gradientVertices[2], secondColor));
    assert(closeColor(gradientVertices[3], firstColor));
    assert(closeColor(gradientVertices[4], secondColor));
    assert(closeColor(gradientVertices[5], secondColor));

    const float densityX = 2560.0F / 1280.0F;
    const float densityY = 1200.0F / 800.0F;
    for (const PackedSceneInstance& packed : packet.instances) {
        const SceneInstance instance = rocket::unpackSceneInstance(packed);
        assert(instance.shape == SceneInstanceShape::Rectangle);
        assert(!instance.textured);
        const float widthPixelsX = instance.axisXx * 2.0F
            * packet.transform.worldUnitX * densityX;
        const float widthPixelsY = instance.axisXy * 2.0F
            * packet.transform.worldUnitY * densityY;
        const float physicalWidth = std::hypot(widthPixelsX, widthPixelsY);
        assert(physicalWidth > 2.8F);
        assert(physicalWidth < 3.2F);
    }

    // The fixed unit-quad order reconstructs the old line triangle corners:
    // aLeft, aRight, bRight, aLeft, bRight, bLeft. Check the four unique
    // corners here after the unavoidable half-float basis quantization.
    const SceneInstance horizontal = rocket::unpackSceneInstance(packet.instances[0]);
    const float expectedOffsetY = 1.5F / (packet.transform.worldUnitY * densityY);
    const std::array<std::array<float, 2>, 4> expectedCorners {{
        {-0.70F, -0.45F + expectedOffsetY},
        {-0.70F, -0.45F - expectedOffsetY},
        { 0.10F, -0.45F - expectedOffsetY},
        { 0.10F, -0.45F + expectedOffsetY},
    }};
    const std::array<std::array<float, 2>, 4> reconstructedCorners {{
        {horizontal.centerX - horizontal.axisXx - horizontal.axisYx,
         horizontal.centerY - horizontal.axisXy - horizontal.axisYy},
        {horizontal.centerX + horizontal.axisXx - horizontal.axisYx,
         horizontal.centerY + horizontal.axisXy - horizontal.axisYy},
        {horizontal.centerX + horizontal.axisXx + horizontal.axisYx,
         horizontal.centerY + horizontal.axisXy + horizontal.axisYy},
        {horizontal.centerX - horizontal.axisXx + horizontal.axisYx,
         horizontal.centerY - horizontal.axisXy + horizontal.axisYy},
    }};
    for (std::size_t corner = 0; corner < expectedCorners.size(); ++corner) {
        assert(std::abs(reconstructedCorners[corner][0] - expectedCorners[corner][0]) < 0.001F);
        assert(std::abs(reconstructedCorners[corner][1] - expectedCorners[corner][1]) < 0.001F);
    }
    assert(packet.instances.size_bytes()
        < packet.instances.size() * 6U * sizeof(PackedSceneVertex));

    // Widths below one pixel preserve the original one-physical-pixel floor
    // in clip space as well as world space.
    const std::vector<SceneVertex> clipSegment {
        {-0.80F, 0.10F, firstColor.r, firstColor.g, firstColor.b, firstColor.a},
        { 0.65F, 0.55F, firstColor.r, firstColor.g, firstColor.b, firstColor.a},
    };
    const ScenePacket& clipPacket = rocket::SceneComposerTestAccess::composeLines(
        composer, clipSegment, 0.1F, false);
    assertValidDrawRanges(clipPacket);
    assert(clipPacket.draws.size() == 1U);
    assert(clipPacket.draws[0].drawType == SceneDrawType::InstancedQuad);
    const SceneInstance clipLine = rocket::unpackSceneInstance(clipPacket.instances[0]);
    const float clipWidthPixels = std::hypot(
        clipLine.axisXx * 2.0F * 2560.0F * 0.5F,
        clipLine.axisXy * 2.0F * 1200.0F * 0.5F);
    assert(clipWidthPixels > 0.8F);
    assert(clipWidthPixels < 1.2F);
}

void testAtlasPageBatchingAcrossLogicalTextures()
{
    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F, -1.0F});
    composer.setPresentationTime(0.0);
    composer.setTextureReady(TextureId::RocketOpen, true);
    composer.setTextureReady(TextureId::MiningDrone, true);
    composer.setTextureReady(TextureId::RocketClosed, true);

    RenderSnapshot snapshot;
    snapshot.screen = rocket::Screen::Launch;
    snapshot.preflightActive = true;
    snapshot.preflightProgress = 0.625;
    const ScenePacket& packet = composer.compose(snapshot);
    assertValidDrawRanges(packet);

    const std::array<TextureId, 3> logicalTextures {
        TextureId::RocketOpen,
        TextureId::MiningDrone,
        TextureId::RocketClosed,
    };
    std::size_t logicalTextureIndex = 0;
    const SceneDraw* sharedDraw = nullptr;
    for (const SceneDraw& draw : packet.draws) {
        if (draw.drawType != SceneDrawType::InstancedQuad) {
            continue;
        }
        for (std::size_t instanceIndex = 0; instanceIndex < draw.instanceCount; ++instanceIndex) {
            const SceneInstance actual = rocket::unpackSceneInstance(
                packet.instances[draw.firstInstance + instanceIndex]);
            if (!actual.textured) {
                continue;
            }
            assert(logicalTextureIndex < logicalTextures.size());
            const rocket::SceneAtlasUvRect expected = rocket::mapSceneAtlasUvRect(
                logicalTextures[logicalTextureIndex], 0.0F, 0.0F, 1.0F, 1.0F);
            constexpr float tolerance = 2.0F / 65535.0F;
            assert(expected.valid);
            assert(draw.pipeline == PipelineClass::Textured);
            assert(draw.atlasPage == expected.page);
            assert(std::abs(actual.u0 - expected.u0) < tolerance);
            assert(std::abs(actual.v0 - expected.v0) < tolerance);
            assert(std::abs(actual.u1 - expected.u1) < tolerance);
            assert(std::abs(actual.v1 - expected.v1) < tolerance);
            if (sharedDraw == nullptr) {
                sharedDraw = &draw;
            } else {
                assert(sharedDraw == &draw);
            }
            ++logicalTextureIndex;
        }
    }
    assert(logicalTextureIndex == logicalTextures.size());
    assert(sharedDraw != nullptr);
    assert(sharedDraw->texture == TextureId::RocketOpen);
    assert(sharedDraw->atlasPage == rocket::sceneAtlasPageForTexture(TextureId::RocketOpen));
    assert(sharedDraw->atlasPage == rocket::sceneAtlasPageForTexture(TextureId::MiningDrone));
    assert(sharedDraw->atlasPage == rocket::sceneAtlasPageForTexture(TextureId::RocketClosed));
}

RenderSnapshot miningSnapshot(rocket::MiningRunState& mining)
{
    RenderSnapshot snapshot;
    snapshot.screen = rocket::Screen::Mining;
    snapshot.animationTime = 1.0;
    snapshot.miningWidth = mining.terrain.width;
    snapshot.miningHeight = mining.terrain.height;
    snapshot.miningDroneX = mining.droneX;
    snapshot.miningDroneY = mining.droneY;
    snapshot.miningTargetX = mining.targetTipX;
    snapshot.miningTargetY = mining.targetTipY;
    snapshot.miningReturnZoneX = mining.returnZoneX;
    snapshot.miningReturnZoneY = mining.returnZoneY;
    snapshot.bindMiningFrameViews(mining);
    return snapshot;
}

std::vector<PackedSceneInstance> attackDroneInstances(const RenderSnapshot& snapshot)
{
    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F, -1.0F});
    composer.setPresentationTime(1.0);
    composer.setTextureReady(TextureId::MiniDroneAttack, true);
    const ScenePacket& packet = composer.compose(snapshot);
    assertValidDrawRanges(packet);

    for (const SceneDraw& draw : packet.draws) {
        if (draw.texture == TextureId::MiniDroneAttack) {
            assert(draw.pipeline == PipelineClass::Textured);
            assert(draw.drawType == SceneDrawType::InstancedQuad);
            std::vector<PackedSceneInstance> result;
            for (std::size_t index = 0; index < draw.instanceCount; ++index) {
                const PackedSceneInstance& packed = packet.instances[draw.firstInstance + index];
                if (rocket::unpackSceneInstance(packed).textured) {
                    result.push_back(packed);
                }
            }
            assert(result.size() == 1U);
            return result;
        }
    }
    assert(false && "Expected the attack-drone logical texture draw.");
    return {};
}

rocket::MiningRunState miningState(double inactiveEnemyX, double activeEnemyX);

SceneInstance miningRigInstance(const ScenePacket& packet)
{
    const rocket::SceneAtlasUvRect expected = rocket::mapSceneAtlasUvRect(
        TextureId::MiningDrone, 0.0F, 0.0F, 1.0F, 1.0F);
    constexpr float tolerance = 2.0F / 65535.0F;
    assert(expected.valid);
    for (const SceneDraw& draw : packet.draws) {
        if (draw.drawType != SceneDrawType::InstancedQuad || draw.atlasPage != expected.page) {
            continue;
        }
        for (std::size_t index = 0; index < draw.instanceCount; ++index) {
            const SceneInstance instance = rocket::unpackSceneInstance(
                packet.instances[draw.firstInstance + index]);
            if (instance.textured
                && std::abs(instance.u0 - expected.u0) < tolerance
                && std::abs(instance.v0 - expected.v0) < tolerance
                && std::abs(instance.u1 - expected.u1) < tolerance
                && std::abs(instance.v1 - expected.v1) < tolerance) {
                return instance;
            }
        }
    }
    assert(false && "Expected the mining-rig texture instance.");
    return {};
}

void testMiningRigSlerpsVerticalDuringExtraction()
{
    rocket::MiningRunState mining = miningState(20.0, 20.0);
    RenderSnapshot snapshot = miningSnapshot(mining);
    snapshot.miningHullDirX = 1.0;
    snapshot.miningHullDirY = 0.0;

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F, -1.0F});
    composer.setTextureReady(TextureId::MiningDrone, true);

    composer.setPresentationTime(1.0);
    composer.compose(snapshot);

    snapshot.miningExtractionActive = true;
    snapshot.miningExtractionProgress = 0.20;
    composer.setPresentationTime(1.016);
    const SceneInstance start = miningRigInstance(composer.compose(snapshot));
    const float startLength = std::hypot(start.axisYx, start.axisYy);
    assert(start.axisYx / startLength < -0.99F);
    assert(std::abs(start.axisYy / startLength) < 0.02F);

    snapshot.miningExtractionProgress = 0.38;
    composer.setPresentationTime(1.032);
    const SceneInstance middle = miningRigInstance(composer.compose(snapshot));
    const float middleLength = std::hypot(middle.axisYx, middle.axisYy);
    assert(middle.axisYx / middleLength < -0.10F);
    assert(middle.axisYy / middleLength > 0.10F);

    snapshot.miningExtractionProgress = 0.48;
    composer.setPresentationTime(1.048);
    const SceneInstance end = miningRigInstance(composer.compose(snapshot));
    const float endLength = std::hypot(end.axisYx, end.axisYy);
    assert(std::abs(end.axisYx / endLength) < 0.02F);
    assert(end.axisYy / endLength > 0.99F);
}

rocket::MiningRunState miningState(double inactiveEnemyX, double activeEnemyX)
{
    rocket::MiningRunState mining;
    mining.terrain.width = 4;
    mining.terrain.height = 4;
    mining.terrain.cells.resize(16);
    mining.droneX = 1.0;
    mining.droneY = 1.0;
    mining.targetTipX = 1.0;
    mining.targetTipY = 2.0;

    rocket::MiningEnemy inactive;
    inactive.type = rocket::MiningEnemyType::Ant;
    inactive.x = inactiveEnemyX;
    inactive.y = 3.0;
    inactive.active = false;
    inactive.health = 1.0;
    inactive.maxHealth = 1.0;

    rocket::MiningEnemy active;
    active.type = rocket::MiningEnemyType::Ant;
    active.x = activeEnemyX;
    active.y = 1.0;
    active.active = true;
    active.health = 1.0;
    active.maxHealth = 1.0;
    mining.enemies = {inactive, active};

    rocket::MiningMiniDroneAgent attack;
    attack.role = rocket::MiniDroneRole::Attack;
    attack.behavior = rocket::MiningMiniDroneBehavior::Engaging;
    attack.x = 1.0;
    attack.y = 1.0;
    attack.targetEnemyIndex = 1;
    mining.miniDrones.push_back(attack);
    return mining;
}

void testFrameViewsKeepAuthoritativeEnemyIndices()
{
    rocket::MiningRunState base = miningState(-20.0, 3.0);
    const RenderSnapshot view = miningSnapshot(base);
    assert(view.miningEnemies.data() == base.enemies.data());
    assert(view.miningEnemies.size() == 2U);
    assert(view.miningMiniDrones.data() == base.miniDrones.data());
    assert(view.miningMiniDrones[0].targetEnemyIndex == 1);
    assert(&view.miningEnemies[1] == &base.enemies[1]);

    const std::vector<PackedSceneInstance> baseDrone = attackDroneInstances(view);
    assert(view.miningEnemies.data() == base.enemies.data());
    assert(view.miningMiniDrones.data() == base.miniDrones.data());

    // Moving an inactive enemy at original index zero cannot affect the
    // attack drone targeting original index one.
    rocket::MiningRunState movedInactive = miningState(200.0, 3.0);
    const RenderSnapshot movedInactiveView = miningSnapshot(movedInactive);
    const std::vector<PackedSceneInstance> movedInactiveDrone = attackDroneInstances(movedInactiveView);
    assert(baseDrone.size() == movedInactiveDrone.size());
    assert(std::memcmp(
        baseDrone.data(),
        movedInactiveDrone.data(),
        baseDrone.size() * sizeof(PackedSceneInstance)) == 0);

    // Moving the active enemy at original index one must rotate the sprite,
    // proving the unfiltered authoritative index was dereferenced.
    rocket::MiningRunState movedTarget = miningState(-20.0, 1.0);
    movedTarget.enemies[1].y = 3.0;
    const RenderSnapshot movedTargetView = miningSnapshot(movedTarget);
    const std::vector<PackedSceneInstance> movedTargetDrone = attackDroneInstances(movedTargetView);
    assert(baseDrone.size() == movedTargetDrone.size());
    assert(std::memcmp(
        baseDrone.data(),
        movedTargetDrone.data(),
        baseDrone.size() * sizeof(PackedSceneInstance)) != 0);
}

void testMiningTerrainPersistentStreamInvalidation()
{
    rocket::MiningRunState mining;
    mining.terrain.width = 4;
    mining.terrain.height = 4;
    mining.terrain.cells.resize(16);
    mining.droneX = 1.0;
    mining.droneY = 1.0;
    mining.targetTipX = 1.0;
    mining.targetTipY = 2.0;
    mining.returnZoneX = 2.0;
    mining.returnZoneY = 2.0;

    rocket::MiningCell& terrainCell = mining.terrain.cells[1];
    terrainCell.material = rocket::MiningCellMaterial::Regolith;
    terrainCell.maxToughness = 10.0;
    terrainCell.remainingToughness = 10.0;
    terrainCell.revealed = true;

    rocket::MiningMiniDroneAgent survey;
    survey.role = rocket::MiniDroneRole::Survey;
    survey.x = 3.0;
    survey.y = 3.0;
    mining.miniDrones.push_back(survey);

    RenderSnapshot snapshot = miningSnapshot(mining);
    snapshot.destinationTier = 1;
    snapshot.miningScannerRadius = 7.0;

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F, -1.0F});
    composer.setPresentationTime(1.0);
    const ScenePacket& first = composer.compose(snapshot);
    assertValidDrawRanges(first);
    assert(first.miningTerrainRevision > 0U);
    assert(first.miningTerrainVertices.empty());
    assert(!first.miningTerrainInstances.empty());
    const std::uint64_t stableRevision = first.miningTerrainRevision;
    const std::vector<PackedSceneInstance> stableInstances(
        first.miningTerrainInstances.begin(), first.miningTerrainInstances.end());
    std::size_t miningTerrainDraws = 0;
    for (const SceneDraw& draw : first.draws) {
        if (draw.drawType == SceneDrawType::InstancedQuad
            && draw.instanceStream == SceneInstanceStream::MiningTerrain) {
            ++miningTerrainDraws;
        }
    }
    // Fog and revealed base terrain retain their original, separated places
    // in the transparent submission order while sharing one persistent stream.
    assert(miningTerrainDraws == 2U);

    snapshot.animationTime = 9.0;
    composer.setPresentationTime(9.0);
    const ScenePacket& animationOnly = composer.compose(snapshot);
    assertValidDrawRanges(animationOnly);
    assert(animationOnly.miningTerrainRevision == stableRevision);
    assert(animationOnly.miningTerrainInstances.size() == stableInstances.size());
    assert(std::memcmp(
        animationOnly.miningTerrainInstances.data(),
        stableInstances.data(),
        stableInstances.size() * sizeof(PackedSceneInstance)) == 0);

    // Gate framing is an animated overlay and does not dirty the cached base.
    terrainCell.gateAssociated = true;
    const ScenePacket& overlayOnly = composer.compose(snapshot);
    assertValidDrawRanges(overlayOnly);
    assert(overlayOnly.miningTerrainRevision == stableRevision);

    snapshot.miningDroneX = 2.5;
    const ScenePacket& movedLight = composer.compose(snapshot);
    assertValidDrawRanges(movedLight);
    assert(movedLight.miningTerrainRevision != stableRevision);
    const std::uint64_t movedLightRevision = movedLight.miningTerrainRevision;

    terrainCell.remainingToughness = 4.0;
    const ScenePacket& damagedTerrain = composer.compose(snapshot);
    assertValidDrawRanges(damagedTerrain);
    assert(damagedTerrain.miningTerrainRevision != movedLightRevision);
    const std::uint64_t damagedTerrainRevision = damagedTerrain.miningTerrainRevision;

    mining.miniDrones[0].x = 1.5;
    snapshot.miningScannerPulse = 0.4;
    const ScenePacket& scannerLight = composer.compose(snapshot);
    assertValidDrawRanges(scannerLight);
    assert(scannerLight.miningTerrainRevision != damagedTerrainRevision);
}

} // namespace

int main()
{
    testPackedVertexConversion();
    testManifestAndLogicalTextureMapping();
    testCampaignIntroductionDrawsHeroicCapybara();
    testPolygonInstanceMatchesTriangleFan();
    testOrderedBatchingAndWideLineInstancing();
    testUniformAndGradientLineOrdering();
    testAtlasPageBatchingAcrossLogicalTextures();
    testMiningRigSlerpsVerticalDuringExtraction();
    testFrameViewsKeepAuthoritativeEnemyIndices();
    testMiningTerrainPersistentStreamInvalidation();
    return 0;
}
