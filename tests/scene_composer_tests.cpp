#include "core/UiViewportLayout.h"
#include "core/Tuning.h"
#include "core/FlightInstrumentLayout.h"
#include "core/FlightSystem.h"
#include "core/ExpeditionSystem.h"
#include "render/SceneAtlas.h"
#include "render/SceneClip.h"
#include "render/SceneComposer.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_MSC_VER) && defined(_DEBUG)
#include <crtdbg.h>
#endif

// Keep native CTest runs non-interactive: the standard MSVC debug assertion
// dialog blocks the entire suite and hides the failing source location.
#undef assert
#define assert(condition) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "FAILED assertion at %s:%d: %s\\n", __FILE__, __LINE__, #condition); \
            std::exit(3); \
        } \
    } while (false)

namespace rocket {

struct SceneComposerTestAccess {
    static std::pair<float, float> frameCenter(
        SceneComposer& composer,
        const RenderSnapshot& snapshot)
    {
        composer.beginFrame(snapshot);
        return {composer.scenePixelCenterX_, composer.scenePixelCenterY_};
    }

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

    static ScenePacket beginFramePacket(SceneComposer& composer, const RenderSnapshot& snapshot)
    {
        composer.beginFrame(snapshot);
        composer.finalizePacket();
        return composer.packet_;
    }





    static std::size_t miningPickupBurstCount(const SceneComposer& composer)
    {
        return composer.miningPickupBursts_.size();
    }

    static ScenePacket flightInstrumentPacket(
        SceneComposer& composer,
        const RenderSnapshot& snapshot)
    {
        composer.beginFrame(snapshot);
        composer.drawFlightInstruments(snapshot);
        composer.finalizePacket();
        return composer.packet_;
    }


    static ScenePacket rocketPacket(
        SceneComposer& composer,
        const RenderSnapshot& snapshot)
    {
        composer.beginFrame(snapshot);
        composer.drawRocket(snapshot);
        composer.finalizePacket();
        return composer.packet_;
    }
};

} // namespace rocket

namespace {

using rocket::Color;
using rocket::CoordinateSpace;
using rocket::FramebufferSceneClip;
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
using rocket::UiLayoutClass;
using rocket::UiRect;
using rocket::UiSurfaceKind;
using rocket::UiViewportLayout;

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

void assertRect(const UiRect& actual, const UiRect& expected)
{
    assert(actual == expected);
}

void assertLayoutInvariants(const UiViewportLayout& layout, int width, int height)
{
    const UiRect viewport {0, 0, width, height};
    assert(rocket::uiRectContains(viewport, layout.sceneRect));
    assert(rocket::uiRectContains(viewport, layout.panelRect));
    assert(rocket::uiRectContains(viewport, layout.topPanelRect));
    assert(rocket::uiRectContains(viewport, layout.hudSafeRect));
    assert(rocket::uiRectContains(layout.sceneRect, layout.hudSafeRect));
    assert(!rocket::uiRectsIntersect(layout.sceneRect, layout.panelRect));
    assert(!rocket::uiRectsIntersect(layout.sceneRect, layout.topPanelRect));
    if (layout.layoutClass == UiLayoutClass::MiningHud) {
        assert(layout.topPanelRect.width > 0);
        assert(layout.topPanelRect.height > 0);
    } else {
        assert(layout.topPanelRect == UiRect {});
    }
}

void testUiViewportLayoutGeometry()
{
    const UiViewportLayout stress = rocket::resolveUiViewportLayout(1024, 768, UiSurfaceKind::PersistentPanel);
    assert(stress.layoutClass == UiLayoutClass::LandscapeRail);
    assertRect(stress.panelRect, {12, 12, 280, 744});
    assertRect(stress.sceneRect, {304, 12, 708, 744});
    assertRect(stress.hudSafeRect, {316, 24, 684, 720});
    assertLayoutInvariants(stress, 1024, 768);

    const UiViewportLayout minimum = rocket::resolveUiViewportLayout(1280, 720, UiSurfaceKind::PersistentPanel);
    assert(minimum.layoutClass == UiLayoutClass::LandscapeRail);
    assertRect(minimum.panelRect, {12, 12, 307, 696});
    assertRect(minimum.sceneRect, {331, 12, 937, 696});
    assertRect(minimum.hudSafeRect, {343, 24, 913, 672});
    assertLayoutInvariants(minimum, 1280, 720);

    const UiViewportLayout deck = rocket::resolveUiViewportLayout(1280, 800, UiSurfaceKind::PersistentPanel);
    assert(deck.layoutClass == UiLayoutClass::LandscapeRail);
    assertRect(deck.panelRect, {12, 12, 307, 776});
    assertRect(deck.sceneRect, {331, 12, 937, 776});
    assertRect(deck.hudSafeRect, {343, 24, 913, 752});
    assertLayoutInvariants(deck, 1280, 800);

    const UiViewportLayout fullHd = rocket::resolveUiViewportLayout(1920, 1080, UiSurfaceKind::PersistentPanel);
    assert(fullHd.layoutClass == UiLayoutClass::LandscapeRail);
    assertRect(fullHd.panelRect, {16, 16, 340, 1048});
    assertRect(fullHd.sceneRect, {372, 16, 1532, 1048});
    assertRect(fullHd.hudSafeRect, {388, 32, 1500, 1016});
    assertLayoutInvariants(fullHd, 1920, 1080);

    const UiViewportLayout quadHd = rocket::resolveUiViewportLayout(2560, 1440, UiSurfaceKind::PersistentPanel);
    assert(quadHd.layoutClass == UiLayoutClass::LandscapeRail);
    assertRect(quadHd.panelRect, {16, 16, 340, 1408});
    assertRect(quadHd.sceneRect, {372, 16, 2172, 1408});
    assertRect(quadHd.hudSafeRect, {388, 32, 2140, 1376});
    assertLayoutInvariants(quadHd, 2560, 1440);

    const UiViewportLayout fourK = rocket::resolveUiViewportLayout(3840, 2160, UiSurfaceKind::PersistentPanel);
    assert(fourK.layoutClass == UiLayoutClass::LandscapeRail);
    assertRect(fourK.panelRect, {16, 16, 340, 2128});
    assertRect(fourK.sceneRect, {372, 16, 3452, 2128});
    assertRect(fourK.hudSafeRect, {388, 32, 3420, 2096});
    assertLayoutInvariants(fourK, 3840, 2160);

    const UiViewportLayout narrow = rocket::resolveUiViewportLayout(900, 600, UiSurfaceKind::PersistentPanel);
    assert(narrow.layoutClass == UiLayoutClass::BottomDock);
    assertRect(narrow.panelRect, {12, 420, 876, 168});
    assertRect(narrow.sceneRect, {12, 12, 876, 396});
    assertRect(narrow.hudSafeRect, {24, 24, 852, 372});
    assertLayoutInvariants(narrow, 900, 600);

    const UiViewportLayout fullscreen = rocket::resolveUiViewportLayout(1280, 800, UiSurfaceKind::Fullscreen);
    assert(fullscreen.layoutClass == UiLayoutClass::Fullscreen);
    assertRect(fullscreen.sceneRect, {0, 0, 1280, 800});
    assertRect(fullscreen.panelRect, {});
    assertRect(fullscreen.hudSafeRect, {12, 12, 1256, 776});
    assertLayoutInvariants(fullscreen, 1280, 800);
}

void testMiningViewportReservesBothHudLanes()
{
    const UiViewportLayout mining = rocket::resolveUiViewportLayout(1280, 800, UiSurfaceKind::Mining);
    assert(mining.layoutClass == UiLayoutClass::MiningHud);
    assertRect(mining.sceneRect, {12, 104, 1256, 552});
    assertRect(mining.panelRect, {12, 668, 1256, 120});
    assertRect(mining.topPanelRect, {12, 12, 1256, 80});
    assertRect(mining.hudSafeRect, {24, 116, 1232, 528});
    assertLayoutInvariants(mining, 1280, 800);

    assert(!rocket::uiRectsIntersect(mining.topPanelRect, mining.sceneRect));
    assert(!rocket::uiRectsIntersect(mining.panelRect, mining.sceneRect));
    assert(!rocket::uiRectsIntersect(mining.topPanelRect, mining.panelRect));
    assert(rocket::uiRectBottom(mining.topPanelRect) < mining.sceneRect.y);
    assert(rocket::uiRectBottom(mining.sceneRect) < mining.panelRect.y);

    const UiViewportLayout wideMining = rocket::resolveUiViewportLayout(1920, 1080, UiSurfaceKind::Mining);
    assertRect(wideMining.topPanelRect, {16, 16, 1888, 88});
    assertRect(wideMining.sceneRect, {16, 120, 1888, 800});
    assertRect(wideMining.panelRect, {16, 936, 1888, 128});
    assertRect(wideMining.hudSafeRect, {32, 136, 1856, 768});
    assertLayoutInvariants(wideMining, 1920, 1080);
}

void testScreenSurfaceMapping()
{
    assert(rocket::uiSurfaceKindForScreen(rocket::Screen::Results) == UiSurfaceKind::Fullscreen);
    assert(rocket::uiSurfaceKindForScreen(rocket::Screen::ArrivalFanfare) == UiSurfaceKind::Fullscreen);
    assert(rocket::uiSurfaceKindForScreen(rocket::Screen::StoryBriefing) == UiSurfaceKind::Fullscreen);
    assert(rocket::uiSurfaceKindForScreen(rocket::Screen::DroneOps) == UiSurfaceKind::Fullscreen);
    assert(rocket::uiSurfaceKindForScreen(rocket::Screen::Mining) == UiSurfaceKind::Mining);

    constexpr std::array workspaceScreens {
        rocket::Screen::Hangar,
        rocket::Screen::ArrivalOps,
        rocket::Screen::Research,
        rocket::Screen::SurfaceExpedition,
        rocket::Screen::SurfaceUpgrade,
        rocket::Screen::Upgrade,
        rocket::Screen::Legacy,
        rocket::Screen::Navigation
    };
    for (const rocket::Screen screen : workspaceScreens) {
        assert(rocket::uiSurfaceKindForScreen(screen) == UiSurfaceKind::Fullscreen);
    }

    constexpr std::array persistentScreens {
        rocket::Screen::Flight,
    };
    for (const rocket::Screen screen : persistentScreens) {
        assert(rocket::uiSurfaceKindForScreen(screen) == UiSurfaceKind::PersistentPanel);
    }
}

void testSceneComposerUsesResolvedSceneRect()
{
    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});

    RenderSnapshot snapshot;
    snapshot.screen = rocket::Screen::Hangar;
    const ScenePacket workspacePacket =
        rocket::SceneComposerTestAccess::beginFramePacket(composer, snapshot);
    const rocket::SceneTransform workspaceTransform = workspacePacket.transform;
    assert(std::abs(workspaceTransform.pixelCenterX - 640.0F) < 0.001F);
    assert(std::abs(workspaceTransform.pixelCenterY - 400.0F) < 0.001F);
    assert(std::abs(workspaceTransform.worldUnitX - 368.0F) < 0.001F);
    assert(std::abs(workspaceTransform.worldUnitY - 368.0F) < 0.001F);
    assertRect(workspacePacket.logicalSceneClip, {0, 0, 1280, 800});

    snapshot.screen = rocket::Screen::Mining;
    const ScenePacket miningPacket =
        rocket::SceneComposerTestAccess::beginFramePacket(composer, snapshot);
    const rocket::SceneTransform miningTransform = miningPacket.transform;
    assert(std::abs(miningTransform.pixelCenterX - 640.0F) < 0.001F);
    assert(std::abs(miningTransform.pixelCenterY - 420.0F) < 0.001F);
    assert(std::abs(miningTransform.worldUnitX - 276.0F) < 0.001F);
    assert(std::abs(miningTransform.worldUnitY - 276.0F) < 0.001F);
    assertRect(miningPacket.logicalSceneClip, {12, 104, 1256, 552});

    snapshot.titleScreen = true;
    const ScenePacket titlePacket =
        rocket::SceneComposerTestAccess::beginFramePacket(composer, snapshot);
    const rocket::SceneTransform titleTransform = titlePacket.transform;
    assert(std::abs(titleTransform.pixelCenterX - 640.0F) < 0.001F);
    assert(std::abs(titleTransform.pixelCenterY - 400.0F) < 0.001F);
    assert(std::abs(titleTransform.worldUnitX - 368.0F) < 0.001F);
    assertRect(titlePacket.logicalSceneClip, {0, 0, 1280, 800});

    snapshot.titleScreen = false;
    snapshot.screen = rocket::Screen::Results;
    const ScenePacket resultsPacket =
        rocket::SceneComposerTestAccess::beginFramePacket(composer, snapshot);
    const rocket::SceneTransform resultsTransform = resultsPacket.transform;
    assert(std::abs(resultsTransform.pixelCenterX - 640.0F) < 0.001F);
    assert(std::abs(resultsTransform.pixelCenterY - 400.0F) < 0.001F);
    assert(std::abs(resultsTransform.worldUnitX - 368.0F) < 0.001F);
    assertRect(resultsPacket.logicalSceneClip, {0, 0, 1280, 800});

    snapshot.screen = rocket::Screen::DroneOps;
    const ScenePacket droneOpsPacket =
        rocket::SceneComposerTestAccess::beginFramePacket(composer, snapshot);
    const rocket::SceneTransform droneOpsTransform = droneOpsPacket.transform;
    assert(std::abs(droneOpsTransform.pixelCenterX - 640.0F) < 0.001F);
    assert(std::abs(droneOpsTransform.pixelCenterY - 400.0F) < 0.001F);
    assert(std::abs(droneOpsTransform.worldUnitX - 368.0F) < 0.001F);
    assertRect(droneOpsPacket.logicalSceneClip, {0, 0, 1280, 800});

    composer.setViewport({900, 600, 900, 600, 1.0F});
    snapshot.screen = rocket::Screen::Hangar;
    const ScenePacket compactWorkspacePacket =
        rocket::SceneComposerTestAccess::beginFramePacket(composer, snapshot);
    const rocket::SceneTransform compactWorkspaceTransform = compactWorkspacePacket.transform;
    assert(std::abs(compactWorkspaceTransform.pixelCenterX - 450.0F) < 0.001F);
    assert(std::abs(compactWorkspaceTransform.pixelCenterY - 300.0F) < 0.001F);
    assert(std::abs(compactWorkspaceTransform.worldUnitX - 276.0F) < 0.001F);
    assertRect(compactWorkspacePacket.logicalSceneClip, {0, 0, 900, 600});
}

void testLogicalSceneClipScalesToFramebuffer()
{
    const UiRect miningLogicalClip {12, 104, 1256, 552};
    const FramebufferSceneClip miningAtOneX = rocket::resolveSceneFramebufferClip(
        miningLogicalClip,
        1280,
        800,
        1280,
        800);
    assert(miningAtOneX == FramebufferSceneClip({12, 104, 1256, 552}));
    assert(rocket::openGlSceneScissorY(miningAtOneX, 800) == 144);

    const FramebufferSceneClip miningAtOneAndQuarterX = rocket::resolveSceneFramebufferClip(
        miningLogicalClip,
        1280,
        800,
        1600,
        1000);
    assert(miningAtOneAndQuarterX == FramebufferSceneClip({15, 130, 1570, 690}));
    assert(rocket::openGlSceneScissorY(miningAtOneAndQuarterX, 1000) == 180);

    const FramebufferSceneClip miningAtTwoX = rocket::resolveSceneFramebufferClip(
        miningLogicalClip,
        1280,
        800,
        2560,
        1600);
    assert(miningAtTwoX == FramebufferSceneClip({24, 208, 2512, 1104}));
    assert(rocket::openGlSceneScissorY(miningAtTwoX, 1600) == 288);

    const FramebufferSceneClip dock = rocket::resolveSceneFramebufferClip(
        {12, 12, 876, 396},
        900,
        600,
        1800,
        1200);
    assert(dock == FramebufferSceneClip({24, 24, 1752, 792}));
    assert(rocket::openGlSceneScissorY(dock, 1200) == 384);

    const FramebufferSceneClip mining = rocket::resolveSceneFramebufferClip(
        {12, 104, 1256, 552},
        1280,
        800,
        2560,
        1200);
    assert(mining == FramebufferSceneClip({24, 156, 2512, 828}));
    assert(rocket::openGlSceneScissorY(mining, 1200) == 216);

    // Fractional, asymmetric density must round outward on every edge.
    const FramebufferSceneClip fractional = rocket::resolveSceneFramebufferClip(
        {1, 1, 1, 1},
        3,
        3,
        10,
        8);
    assert(fractional == FramebufferSceneClip({3, 2, 4, 4}));
    assert(rocket::openGlSceneScissorY(fractional, 8) == 2);

    const FramebufferSceneClip clamped = rocket::resolveSceneFramebufferClip(
        {-10, -5, 20, 10},
        100,
        50,
        200,
        100);
    assert(clamped == FramebufferSceneClip({0, 0, 20, 10}));
    assert(rocket::resolveSceneFramebufferClip({}, 1280, 800, 2560, 1600).empty());
    assert(rocket::resolveSceneFramebufferClip({0, 0, 10, 10}, 0, 800, 2560, 1600).empty());
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

void testLaunchDestinationGateUsesCorridorEndpoints()
{
    struct LineSegment {
        SceneVertex start;
        SceneVertex end;
    };
    const auto lineSegmentsWithColor = [](
        const ScenePacket& packet,
        float red,
        float green,
        float blue,
        float alpha) {
        std::vector<LineSegment> result;
        for (const PackedSceneInstance& packed : packet.instances) {
            const SceneInstance instance = rocket::unpackSceneInstance(packed);
            if (instance.shape == SceneInstanceShape::Rectangle
                && std::abs(instance.color.r - red) < 0.01F
                && std::abs(instance.color.g - green) < 0.01F
                && std::abs(instance.color.b - blue) < 0.01F
                && std::abs(instance.color.a - alpha) < 0.01F) {
                result.push_back({
                    {instance.centerX - instance.axisYx, instance.centerY - instance.axisYy},
                    {instance.centerX + instance.axisYx, instance.centerY + instance.axisYy}
                });
            }
        }
        return result;
    };

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    for (int destinationTier = 0; destinationTier <= 3; ++destinationTier) {
        RenderSnapshot snapshot;
        snapshot.screen = rocket::Screen::Flight;
        snapshot.destinationTier = destinationTier;
        snapshot.launchCourseLimit = 1.2;

        const ScenePacket& packet = composer.compose(snapshot);
        const std::vector<LineSegment> destinationGate =
            lineSegmentsWithColor(packet, 1.0F, 0.25F, 0.20F, 0.82F);
        const std::vector<LineSegment> lostCourseBoundary =
            lineSegmentsWithColor(packet, 1.0F, 0.25F, 0.20F, 0.32F);
        assert(destinationGate.size() == 1U);
        assert(!lostCourseBoundary.empty());

        for (const SceneVertex& gateEndpoint : {
                 destinationGate.front().start,
                 destinationGate.front().end}) {
            const bool touchesBoundary = std::any_of(
                lostCourseBoundary.begin(),
                lostCourseBoundary.end(),
                [&](const LineSegment& boundary) {
                    const auto touches = [&](const SceneVertex& boundaryVertex) {
                        return std::abs(gateEndpoint.x - boundaryVertex.x) < 0.001F
                            && std::abs(gateEndpoint.y - boundaryVertex.y) < 0.001F;
                    };
                    return touches(boundary.start) || touches(boundary.end);
                });
            assert(touchesBoundary);
        }

        snapshot.travelProgress = snapshot.launchMissionTargetProgress;
        const ScenePacket& crossedPacket = composer.compose(snapshot);
        const std::vector<LineSegment> crossedGate =
            lineSegmentsWithColor(crossedPacket, 0.35F, 0.92F, 0.62F, 0.82F);
        assert(crossedGate.size() == 1U);
        assert(std::abs(crossedGate.front().start.x - destinationGate.front().start.x) < 0.001F);
        assert(std::abs(crossedGate.front().start.y - destinationGate.front().start.y) < 0.001F);
        assert(std::abs(crossedGate.front().end.x - destinationGate.front().end.x) < 0.001F);
        assert(std::abs(crossedGate.front().end.y - destinationGate.front().end.y) < 0.001F);

        snapshot.travelProgress = 0.25;
        snapshot.returningHome = true;
        snapshot.launchMissionTargetReached = true;
        const ScenePacket& returnPacket = composer.compose(snapshot);
        const std::vector<LineSegment> returnGate =
            lineSegmentsWithColor(returnPacket, 0.35F, 0.92F, 0.62F, 0.82F);
        assert(returnGate.size() == 1U);

        snapshot.launchMissionTargetReached = false;
        const ScenePacket& earlyReturnPacket = composer.compose(snapshot);
        const std::vector<LineSegment> earlyReturnGate =
            lineSegmentsWithColor(earlyReturnPacket, 1.0F, 0.25F, 0.20F, 0.82F);
        assert(earlyReturnGate.size() == 1U);

        snapshot.returningHome = false;
        snapshot.launchMissionTargetReached = false;
        snapshot.launchCourseOffset = 0.0;
        const ScenePacket& goldBandPacket = composer.compose(snapshot);
        assert(!lineSegmentsWithColor(goldBandPacket, 1.0F, 0.78F, 0.24F, 0.92F).empty());

        snapshot.launchCourseOffset =
            (rocket::tuning::launch::pilotingCourseSafe + snapshot.launchCourseLimit) * 0.5;
        const ScenePacket& greenBandPacket = composer.compose(snapshot);
        assert(!lineSegmentsWithColor(greenBandPacket, 0.35F, 0.92F, 0.62F, 0.92F).empty());

        snapshot.launchCourseOffset = snapshot.launchCourseLimit + 0.01;
        const ScenePacket& redBandPacket = composer.compose(snapshot);
        assert(!lineSegmentsWithColor(redBandPacket, 1.0F, 0.25F, 0.20F, 0.92F).empty());
    }
}

void testTransferAssistLaunchUsesItsSourceBody()
{
    const auto hasTexture = [](const ScenePacket& packet, TextureId texture) {
        const rocket::SceneAtlasUvRect expected = rocket::mapSceneAtlasUvRect(texture, 0.0F, 0.0F, 1.0F, 1.0F);
        return std::any_of(packet.instances.begin(), packet.instances.end(), [&](const PackedSceneInstance& packed) {
            const SceneInstance instance = rocket::unpackSceneInstance(packed);
            return instance.textured &&
                std::abs(instance.u0 - expected.u0) < 0.001F &&
                std::abs(instance.v0 - expected.v0) < 0.001F &&
                std::abs(instance.u1 - expected.u1) < 0.001F &&
                std::abs(instance.v1 - expected.v1) < 0.001F;
        });
    };

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    composer.setTextureReady(TextureId::Earth, true);
    composer.setTextureReady(TextureId::Mars, true);
    composer.setTextureReady(TextureId::Jupiter, true);

    RenderSnapshot snapshot;
    snapshot.screen = rocket::Screen::Flight;
    snapshot.destinationTier = 3;
    snapshot.frontierTransfer = true;
    snapshot.launchOriginTier = 2;
    const ScenePacket& packet = composer.compose(snapshot);
    assert(hasTexture(packet, TextureId::Mars));
    assert(!hasTexture(packet, TextureId::Earth));
}

void testJupiterSaturnLaunchKeepsJupiterVisibleBesideShip()
{
    const auto findTexture = [](const ScenePacket& packet, TextureId texture) {
        const rocket::SceneAtlasUvRect expected = rocket::mapSceneAtlasUvRect(
            texture, 0.0F, 0.0F, 1.0F, 1.0F);
        const auto found = std::find_if(
            packet.instances.begin(),
            packet.instances.end(),
            [&](const PackedSceneInstance& packed) {
                const SceneInstance instance = rocket::unpackSceneInstance(packed);
                return instance.textured &&
                    std::abs(instance.u0 - expected.u0) < 0.001F &&
                    std::abs(instance.v0 - expected.v0) < 0.001F &&
                    std::abs(instance.u1 - expected.u1) < 0.001F &&
                    std::abs(instance.v1 - expected.v1) < 0.001F;
            });
        assert(found != packet.instances.end());
        return rocket::unpackSceneInstance(*found);
    };

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    composer.setTextureReady(TextureId::Jupiter, true);
    composer.setTextureReady(TextureId::Saturn, true);
    composer.setTextureReady(TextureId::RocketClosed, true);

    RenderSnapshot snapshot;
    snapshot.screen = rocket::Screen::Flight;
    snapshot.destinationTier = 4;
    snapshot.frontierTransfer = true;
    snapshot.launchOriginTier = 3;
    snapshot.travelProgress = 0.0;

    const ScenePacket& packet = composer.compose(snapshot);
    const SceneInstance jupiter = findTexture(packet, TextureId::Jupiter);
    const SceneInstance saturn = findTexture(packet, TextureId::Saturn);
    const SceneInstance ship = findTexture(packet, TextureId::RocketClosed);

    const float sourceSeparation = std::hypot(
        jupiter.centerX - ship.centerX,
        jupiter.centerY - ship.centerY);
    assert(sourceSeparation > 0.20F);
    assert(std::hypot(
        saturn.centerX - ship.centerX,
        saturn.centerY - ship.centerY) > sourceSeparation);
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

    const rocket::SceneAtlasTexture& jetpackCapybara =
        rocket::kSceneAtlasTextures[rocket::textureIndex(TextureId::JetpackCapybara)];
    assert(jetpackCapybara.sourceWidth >= 512U);
    assert(jetpackCapybara.sourceHeight == jetpackCapybara.sourceWidth);
    assert(jetpackCapybara.frameCount == 1U);

    const rocket::SceneAtlasTexture& instruments =
        rocket::kSceneAtlasTextures[rocket::textureIndex(TextureId::FlightInstrumentCluster)];
    assert(instruments.sourceWidth == 1821U);
    assert(instruments.sourceHeight == 864U);
    assert(instruments.frameCount == 1U);
}

bool packetHasTextureFrame(const ScenePacket& packet, TextureId texture, int frame, int frameCount)
{
    const float sourceU0 = static_cast<float>(frame) / static_cast<float>(frameCount);
    const float sourceU1 = static_cast<float>(frame + 1) / static_cast<float>(frameCount);
    const rocket::SceneAtlasUvRect expected = rocket::mapSceneAtlasUvRect(
        texture, sourceU0, 0.0F, sourceU1, 1.0F);
    constexpr float tolerance = 0.0015F;
    return std::any_of(packet.instances.begin(), packet.instances.end(), [&](const PackedSceneInstance& packed) {
        const SceneInstance instance = rocket::unpackSceneInstance(packed);
        if (!instance.textured) {
            return false;
        }
        const bool normal = std::abs(instance.u0 - expected.u0) < tolerance
            && std::abs(instance.u1 - expected.u1) < tolerance;
        const bool mirrored = std::abs(instance.u0 - expected.u1) < tolerance
            && std::abs(instance.u1 - expected.u0) < tolerance;
        return (normal || mirrored)
            && std::abs(instance.v0 - expected.v0) < tolerance
            && std::abs(instance.v1 - expected.v1) < tolerance;
    });
}

RenderSnapshot miningSnapshot(rocket::MiningRunState& mining);

void testEnemyThemesAndAnimationPriorityUseTheSharedSpriteContract()
{
    rocket::MiningRunState mining;
    mining.terrain.width = 4;
    mining.terrain.height = 4;
    mining.terrain.cells.resize(16);
    mining.droneX = 1.0;
    mining.droneY = 1.0;
    mining.targetTipX = 1.0;
    mining.targetTipY = 2.0;
    mining.enemyTheme = rocket::MiningEnemyTheme::Lava;

    rocket::MiningEnemy enemy;
    enemy.type = rocket::MiningEnemyType::Ant;
    enemy.x = 2.0;
    enemy.y = 2.0;
    enemy.active = true;
    enemy.health = 1.0;
    enemy.maxHealth = 1.0;
    enemy.attackAnimationSeconds = rocket::tuning::mining::enemyAttackAnimationSeconds;
    enemy.hitAnimationSeconds = rocket::tuning::mining::enemyHitAnimationSeconds;
    mining.enemies.push_back(enemy);

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    composer.setPresentationTime(1.0);
    composer.setTextureReady(TextureId::EnemyAntLava, true);
    composer.setTextureReady(TextureId::EnemyAntToxic, true);

    RenderSnapshot snapshot = miningSnapshot(mining);
    snapshot.miningEnemyTheme = mining.enemyTheme;
    const ScenePacket hitPacket = composer.compose(snapshot);
    assert(packetHasTextureFrame(hitPacket, TextureId::EnemyAntLava, 12, 20));
    assert(!packetHasTextureFrame(hitPacket, TextureId::EnemyAntLava, 8, 20));

    mining.enemies.front().hitAnimationSeconds = 0.0;
    mining.enemies.front().affinity = rocket::MiningElementalAffinity::Toxic;
    mining.enemies.front().elite = true;
    snapshot = miningSnapshot(mining);
    snapshot.miningEnemyTheme = mining.enemyTheme;
    const ScenePacket attackPacket = composer.compose(snapshot);
    assert(packetHasTextureFrame(attackPacket, TextureId::EnemyAntToxic, 8, 20));

    mining.enemies.front().active = false;
    mining.enemies.front().attackAnimationSeconds = 0.0;
    mining.enemies.front().defeatAnimationSeconds = rocket::tuning::mining::enemyDefeatAnimationSeconds;
    snapshot = miningSnapshot(mining);
    snapshot.miningEnemyTheme = mining.enemyTheme;
    const ScenePacket defeatPacket = composer.compose(snapshot);
    assert(packetHasTextureFrame(defeatPacket, TextureId::EnemyAntToxic, 16, 20));
}

void testFlightInstrumentClusterUsesAtlasNeedlesAndBlinkingWarning()
{
    const auto horizontalCenter = [](const rocket::flight_instrument_layout::Rect& rect) {
        return rect.left + rect.width * 0.5F;
    };
    assert(std::abs(horizontalCenter(rocket::flight_instrument_layout::kTemperatureLabel)
        - rocket::flight_instrument_layout::kTemperatureDialCenterX) < 0.0001F);
    assert(std::abs(horizontalCenter(rocket::flight_instrument_layout::kSpeedLabel)
        - rocket::flight_instrument_layout::kSpeedDialCenterX) < 0.0001F);
    assert(std::abs(horizontalCenter(rocket::flight_instrument_layout::kFuelLabel)
        - rocket::flight_instrument_layout::kFuelDialCenterX) < 0.0001F);
    assert(std::abs(horizontalCenter(rocket::flight_instrument_layout::kTemperatureReadout)
        - rocket::flight_instrument_layout::kTemperatureReadoutCenterX) < 0.0001F);
    assert(std::abs(horizontalCenter(rocket::flight_instrument_layout::kSpeedReadout)
        - rocket::flight_instrument_layout::kSpeedDialCenterX) < 0.0001F);
    assert(std::abs(horizontalCenter(rocket::flight_instrument_layout::kFuelReadout)
        - rocket::flight_instrument_layout::kFuelReadoutCenterX) < 0.0001F);
    assert(rocket::flight_instrument_layout::kSpeedLabel.top
        <= rocket::flight_instrument_layout::kTemperatureLabel.top - 0.035F);
    assert(rocket::flight_instrument_layout::kTemperatureReadout.top
        >= rocket::flight_instrument_layout::kSpeedReadout.top);
    assert(rocket::flight_instrument_layout::kFuelReadout.top
        >= rocket::flight_instrument_layout::kSpeedReadout.top);
    assert(rocket::flight_instrument_layout::kTemperatureReadout.top
        + rocket::flight_instrument_layout::kTemperatureReadout.height
        < rocket::flight_instrument_layout::kThrottleTray.top);
    assert(rocket::flight_instrument_layout::kFuelReadout.top
        + rocket::flight_instrument_layout::kFuelReadout.height
        < rocket::flight_instrument_layout::kThrottleTray.top);

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    composer.setTextureReady(TextureId::FlightInstrumentCluster, true);

    RenderSnapshot snapshot;
    snapshot.screen = rocket::Screen::Flight;
    snapshot.flightInstrumentsVisible = true;
    snapshot.instrumentSpeed = 0.0;
    snapshot.instrumentTemperature = 0.5;
    snapshot.instrumentFuel = 0.75;
    snapshot.instrumentThrottle = 0.0;
    ScenePacket lowPacket = rocket::SceneComposerTestAccess::flightInstrumentPacket(composer, snapshot);
    assert(std::any_of(lowPacket.draws.begin(), lowPacket.draws.end(), [](const SceneDraw& draw) {
        return draw.texture == TextureId::FlightInstrumentCluster;
    }));
    assert(lowPacket.instances.size() == 17U);
    // The textured bezel is the first instance; each needle then emits a line
    // followed by its hub. The speed line is therefore the fourth primitive.
    const SceneInstance lowSpeedNeedle = rocket::unpackSceneInstance(lowPacket.instances[3]);

    snapshot.instrumentSpeed = 1.0;
    ScenePacket highPacket = rocket::SceneComposerTestAccess::flightInstrumentPacket(composer, snapshot);
    assert(highPacket.instances.size() == 17U);
    const SceneInstance highSpeedNeedle = rocket::unpackSceneInstance(highPacket.instances[3]);
    assert(lowSpeedNeedle.axisYx * highSpeedNeedle.axisYx < -0.0001F);

    snapshot.instrumentThrottle = 1.0;
    ScenePacket throttleFull = rocket::SceneComposerTestAccess::flightInstrumentPacket(composer, snapshot);
    assert(throttleFull.instances.size() == 17U);

    snapshot.instrumentThrottle = 0.46;
    ScenePacket throttlePartial = rocket::SceneComposerTestAccess::flightInstrumentPacket(composer, snapshot);
    const auto coloredThrottleSegments = [](const ScenePacket& packet, const Color& color) {
        return std::count_if(packet.instances.begin() + 7, packet.instances.end(), [&color](const PackedSceneInstance& packed) {
            const SceneInstance instance = rocket::unpackSceneInstance(packed);
            return std::abs(instance.color.r - color.r) < 0.01F
                && std::abs(instance.color.g - color.g) < 0.01F
                && std::abs(instance.color.b - color.b) < 0.01F;
        });
    };
    assert(coloredThrottleSegments(throttlePartial, {0.22F, 0.92F, 1.0F, 1.0F}) == 5);

    snapshot.instrumentThrottle = 1.0;
    ScenePacket throttleHigh = rocket::SceneComposerTestAccess::flightInstrumentPacket(composer, snapshot);
    assert(coloredThrottleSegments(throttleHigh, {0.22F, 0.92F, 1.0F, 1.0F}) == 8);
    assert(coloredThrottleSegments(throttleHigh, {1.0F, 0.45F, 0.10F, 1.0F}) == 2);

    snapshot.instrumentOffCourse = true;
    snapshot.animationTime = 0.0;
    ScenePacket warningOn = rocket::SceneComposerTestAccess::flightInstrumentPacket(composer, snapshot);
    assert(warningOn.instances.size() == 18U);
    snapshot.animationTime = 0.40;
    ScenePacket warningOff = rocket::SceneComposerTestAccess::flightInstrumentPacket(composer, snapshot);
    assert(warningOff.instances.size() == 17U);

    SceneComposer compactComposer;
    compactComposer.setViewport({900, 600, 900, 600, 1.0F});
    compactComposer.setTextureReady(TextureId::FlightInstrumentCluster, true);
    snapshot.instrumentOffCourse = false;
    ScenePacket compactPacket = rocket::SceneComposerTestAccess::flightInstrumentPacket(
        compactComposer,
        snapshot);
    const SceneInstance compactCluster = rocket::unpackSceneInstance(compactPacket.instances.front());
    const float compactCenterPixelsX = (compactCluster.centerX + 1.0F) * 0.5F * 900.0F;
    const float compactCenterPixelsY = (1.0F - compactCluster.centerY) * 0.5F * 600.0F;
    assert(compactCenterPixelsX >= static_cast<float>(compactPacket.logicalSceneClip.x + compactPacket.logicalSceneClip.width / 2));
    assert(compactCenterPixelsX <= static_cast<float>(rocket::uiRectRight(compactPacket.logicalSceneClip)));
    assert(compactCenterPixelsY >= static_cast<float>(compactPacket.logicalSceneClip.y + compactPacket.logicalSceneClip.height / 2));
    assert(compactCenterPixelsY <= static_cast<float>(rocket::uiRectBottom(compactPacket.logicalSceneClip)));
}

void testLaunchUsesAttachedAnimatedSideFlames()
{
    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    composer.setTextureReady(TextureId::RocketClosed, true);
    composer.setTextureReady(TextureId::Thrust, true);

    RenderSnapshot snapshot;
    snapshot.screen = rocket::Screen::Flight;
    snapshot.poweredFlight = true;
    snapshot.launchManualControlsEnabled = true;
    snapshot.launchThrottle = 0.6;
    snapshot.currentMultiplier = 1.2;
    snapshot.targetMultiplier = 2.0;

    const auto orangeVertices = [](const ScenePacket& packet) {
        std::vector<SceneVertex> vertices;
        for (const PackedSceneVertex& packed : packet.vertices) {
            const SceneVertex vertex = rocket::unpackSceneVertex(packed);
            if (std::abs(vertex.r - 1.0F) < 0.01F
                && std::abs(vertex.g - 0.42F) < 0.01F
                && std::abs(vertex.b - 0.06F) < 0.01F) {
                vertices.push_back(vertex);
            }
        }
        return vertices;
    };

    const ScenePacket powered = rocket::SceneComposerTestAccess::rocketPacket(composer, snapshot);
    assert(orangeVertices(powered).empty());
    assert(std::any_of(powered.draws.begin(), powered.draws.end(), [](const SceneDraw& draw) {
        return draw.texture == TextureId::Thrust;
    }));

    snapshot.launchPhysicalFlight = true;
    snapshot.launchLandingBlend = 0;
    for (double heading : {0.0,1.57,3.14}) for (double steer : {-1.0,1.0}) {
        snapshot.launchHeading = heading;
        snapshot.launchStrafeInput = steer;
        float previousU = -1, previousV = -1;
        for (int frame=0;frame<6;++frame) {
            snapshot.animationTime = (frame+.1)/18.0;
            const auto steering = rocket::SceneComposerTestAccess::rocketPacket(composer,snapshot);
            assert(orangeVertices(steering).empty());
            assert(steering.instances.size()==3); // Main flame, side flame, ship.
            const auto main = rocket::unpackSceneInstance(steering.instances[0]);
            const auto flame = rocket::unpackSceneInstance(steering.instances[1]);
            const auto ship = rocket::unpackSceneInstance(steering.instances[2]);
            assert(flame.textured && (flame.u0!=previousU || flame.v0!=previousV));
            previousU=flame.u0; previousV=flame.v0;
            assert(flame.v0>main.v0); // Leading transparent rows were cropped.
            const float shipHalfWidth=std::hypot(ship.axisXx,ship.axisXy);
            const float rightX=ship.axisXx/shipHalfWidth,rightY=ship.axisXy/shipHalfWidth;
            const float rootX=flame.centerX+flame.axisYx;
            const float rootY=flame.centerY+flame.axisYy;
            const float rootSide=(rootX-ship.centerX)*rightX+(rootY-ship.centerY)*rightY;
            assert(rootSide*steer<0); // Exhaust is opposite the steering direction.
            assert(std::abs(std::abs(rootSide)/shipHalfWidth-.28F)<.015F);
            assert(std::hypot(flame.axisYx,flame.axisYy)<std::hypot(main.axisYx,main.axisYy));
        }
    }
    snapshot.poweredFlight=false;
    auto coasting=rocket::SceneComposerTestAccess::rocketPacket(composer,snapshot);
    assert(coasting.instances.size()==2); // Steering works without the main engine.
    snapshot.launchStrafeInput=0;
    auto neutral=rocket::SceneComposerTestAccess::rocketPacket(composer,snapshot);
    assert(neutral.instances.size()==1);
    snapshot.launchSteerInput=1;
    auto rotating=rocket::SceneComposerTestAccess::rocketPacket(composer,snapshot);
    assert(rotating.instances.size()==3); // Opposing fore/aft jets plus the ship.
}

void testFlightPointerMatchesRenderedShip()
{
    for (const int width : {1280,1920}) for (const double landing : {0.0,0.4,1.0})
    for (const double departure : {-1.0,0.0,0.4,0.9})
    for (const double heading : {-3.13,-1.57,0.0,1.57,3.13}) {
        SceneComposer composer;
        composer.setViewport({width,800,width,800,1});
        composer.setTextureReady(TextureId::RocketClosed,true);
        RenderSnapshot snapshot;
        snapshot.screen = rocket::Screen::Flight;
        snapshot.launchPhysicalFlight = true;
        snapshot.launchPositionX = 0.1; snapshot.launchPositionY = 0.25;
        snapshot.launchHeading = heading;
        snapshot.launchLandingBlend = landing;
        snapshot.launchApproachBlend = 0.5;
        snapshot.launchLandingBasisAngle = 0.9;
        snapshot.launchLandingAltitude = 12;
        snapshot.surfaceArrivalPrepared = landing > 0;
        snapshot.launchLandingLocalFrame = landing == 1;
        snapshot.miningWidth = 64; snapshot.miningHeight = 30;
        snapshot.miningReturnZoneX = snapshot.launchLandingPadX = 30;
        snapshot.miningReturnZoneY = snapshot.launchLandingPadY = 2;
        if (departure >= 0.0) {
            snapshot.surfaceArrivalPrepared = true;
            snapshot.launchLandingLocalFrame = false;
            snapshot.launchHandoffFrom = static_cast<int>(rocket::FlightMode::Landing);
            snapshot.launchHandoffProgress = departure;
            snapshot.manualAscentCameraProgress = departure;
        }
        composer.compose(snapshot);
        const auto packet = rocket::SceneComposerTestAccess::rocketPacket(composer,snapshot);
        const auto pose = packet.flightPointer;
        assert(pose.active);
        const auto ship = rocket::unpackSceneInstance(packet.instances.back());
        const auto& t = packet.transform;
        assert(std::abs(pose.shipX-(t.pixelCenterX+ship.centerX*t.worldUnitX))<1.0);
        assert(std::abs(pose.shipY-(800-t.pixelCenterY-ship.centerY*t.worldUnitY))<1.0);
        assert(!pose.angleTo(pose.shipX,pose.shipY));
        assert(!pose.angleTo(-10,-10));
        // Remove clipping only for directional math; some transition shots
        // intentionally place part of the ship outside the scene.
        auto unbounded = pose; unbounded.viewport = {-10000,-10000,20000,20000};
        const double x = pose.shipX+pose.forwardX*100, y=pose.shipY-pose.forwardY*100;
        assert(std::abs(*unbounded.angleTo(x,y))<0.00001);
        for (const double angle : {-3.13,-1.57,1.57,3.13}) {
            const double dx=std::cos(angle)*pose.forwardX-std::sin(angle)*pose.forwardY;
            const double dy=std::sin(angle)*pose.forwardX+std::cos(angle)*pose.forwardY;
            assert(std::abs(*unbounded.angleTo(pose.shipX+dx*100,pose.shipY-dy*100)-angle)<0.00001);
        }
    }
    // The feedback law converges through +/-pi without snapping or overshoot.
    double heading=3.0, velocity=0;
    const double target=-3.0, dt=1.0/60.0;
    for(int i=0;i<600;++i) {
        const double error=std::remainder(target-heading,2*3.141592653589793);
        const double steer=rocket::flightPointerSteer(error,velocity);
        velocity-=steer*rocket::flight_controls::turnAcceleration*dt;
        velocity*=std::exp(-5.2*dt);
        heading+=velocity*dt;
    }
    assert(std::abs(std::remainder(target-heading,2*3.141592653589793))<0.001);
}

void testMiningSkyAndTunnelBackdrop()
{
    for (int width : {800, 1600}) for (int depth : {0, 2}) for (int shipFall : {0, 4}) {
        SceneComposer composer;
        composer.setViewport({width, 800, width, 800, 1.0F});
        composer.setTextureReady(TextureId::LocalSolarBackground, true);
        composer.setTextureReady(TextureId::MiningTunnelBackdrop, true);
        RenderSnapshot snapshot;
        snapshot.screen = rocket::Screen::Mining;
        snapshot.miningWidth = 64;
        snapshot.miningHeight = 40;
        snapshot.miningReturnZoneX = snapshot.miningDroneX = 32;
        snapshot.miningReturnZoneY = snapshot.miningDroneY = 16;
        snapshot.miningSurfaceRow = 16;
        snapshot.miningReturnZoneY += shipFall;
        snapshot.miningActiveDepth = depth;
        snapshot.miningShipPresent = true;
        std::vector<rocket::MiningCell> cells(64 * 40);
        snapshot.miningCells = cells;
        const auto packet = composer.compose(snapshot);
        if (depth == 0) {
            const auto& camera = packet.surfaceCamera;
            const float ringTop = camera.top - static_cast<float>(snapshot.miningReturnZoneY -
                rocket::tuning::mining::returnZoneCenterHeightCells -
                rocket::tuning::mining::returnZoneRadiusCells * 1.08) * camera.cellHeight;
            const float ringTopPixels = 800 - packet.transform.pixelCenterY - ringTop * packet.transform.worldUnitY;
            assert(ringTopPixels >= packet.logicalSceneClip.y + 24.0F);
        }
        const auto uv = rocket::mapSceneAtlasUvRect(TextureId::MiningTunnelBackdrop, 0, 0, 1, 1);
        float highest = -10000, lowest = 10000;
        int wallQuads = 0;
        bool stars = false;
        for (const auto& draw : packet.draws) {
            if (draw.texture == TextureId::LocalSolarBackground) stars = true;
            if (draw.atlasPage != uv.page) continue;
            for (std::size_t i=0; i<draw.instanceCount; ++i) {
                const auto instance = rocket::unpackSceneInstance(packet.instances[draw.firstInstance+i]);
                if (!instance.textured || instance.u0 < uv.u0-.001F || instance.u1 > uv.u1+.001F ||
                    instance.v0 < uv.v0-.001F || instance.v1 > uv.v1+.001F) continue;
                ++wallQuads;
                highest = std::max(highest, instance.centerY + instance.axisYy);
                lowest = std::min(lowest, instance.centerY - instance.axisYy);
                assert(instance.color.a > .99F); // No stars bleeding through the tunnel.
            }
        }
        assert(stars && wallQuads > 0 && wallQuads < 100);
        const auto& t = packet.transform;
        const float topPixel = 800 - t.pixelCenterY - highest*t.worldUnitY;
        const float bottomPixel = 800 - t.pixelCenterY - lowest*t.worldUnitY;
        if (depth == 0) {
            const auto& camera = packet.surfaceCamera;
            const float terrainHorizon = camera.top -
                static_cast<float>(snapshot.miningSurfaceRow) * camera.cellHeight;
            assert(std::abs(highest - terrainHorizon) < .001F);
        }
        if (depth == 0) assert(topPixel > packet.logicalSceneClip.y + 20);
        else assert(std::abs(topPixel - packet.logicalSceneClip.y) < 1);
        assert(std::abs(bottomPixel - packet.logicalSceneClip.y - packet.logicalSceneClip.height) < 1);
    }
}

void testDistantEarthMarkerUsesViewportEdgeAndPixelSize()
{
    for (const auto size : {std::pair{800, 600}, std::pair{1280, 800}, std::pair{1920, 1080}}) {
        for (const auto position : {rocket::SystemVector{100, 0}, rocket::SystemVector{-100, 0},
                rocket::SystemVector{0, 100}, rocket::SystemVector{0, -100}}) {
            SceneComposer composer;
            composer.setViewport({size.first, size.second, size.first, size.second, 1.0F});
            composer.setTextureReady(TextureId::Earth, true);
            RenderSnapshot snapshot;
            snapshot.screen = rocket::Screen::Flight;
            snapshot.systemTravel = snapshot.launchPhysicalFlight = true;
            snapshot.system = rocket::solarSystemDefinition();
            std::erase_if(snapshot.system.bodies, [](const auto& body) { return body.id != "earth"; });
            snapshot.systemLocation.frame = rocket::CoordinateFrame::System;
            snapshot.launchPositionX = position.x;
            snapshot.launchPositionY = position.y;
            snapshot.flightGuidance.targetId = "io";
            snapshot.flightGuidance.targetPosition = position;
            const ScenePacket packet = composer.compose(snapshot);
            const auto uv = rocket::mapSceneAtlasUvRect(TextureId::Earth, 0, 0, 1, 1);
            int markers = 0;
            for (const auto& packed : packet.instances) {
                const auto instance = rocket::unpackSceneInstance(packed);
                if (!instance.textured || std::abs(instance.u0 - uv.u0) > .001F ||
                    std::abs(instance.v0 - uv.v0) > .001F ||
                    std::abs(instance.u1 - uv.u1) > .001F || std::abs(instance.v1 - uv.v1) > .001F) continue;
                ++markers;
                const auto& t = packet.transform;
                const float x = t.pixelCenterX + instance.centerX * t.worldUnitX;
                const float y = size.second - t.pixelCenterY - instance.centerY * t.worldUnitY;
                const auto& clip = packet.logicalSceneClip;
                const float edgeDistance = std::min({x - clip.x, clip.x + clip.width - x,
                    y - clip.y, clip.y + clip.height - y});
                assert(std::abs(edgeDistance - 28.0F) < 1.0F);
                assert(std::abs(2 * instance.axisXx * t.worldUnitX - 20.0F) < .2F);
                assert(std::abs(2 * instance.axisYy * t.worldUnitY - 20.0F) < .2F);
            }
            assert(markers == 1);
        }
    }
}

void testPhysicalMoonFlightStartsOnScreenAtEarthDeparture()
{
    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    composer.setTextureReady(TextureId::RocketClosed, true);

    RenderSnapshot snapshot;
    snapshot.screen = rocket::Screen::Flight;
    snapshot.destinationTier = 1;
    snapshot.frontierTransfer = true;
    snapshot.launchPhysicalFlight = true;
    snapshot.launchPositionX = -3.40;
    snapshot.launchPositionY = 1.10;
    snapshot.launchHeading = 0.0;

    const ScenePacket packet =
        rocket::SceneComposerTestAccess::rocketPacket(composer, snapshot);
    assert(!packet.instances.empty());
    const SceneInstance ship = rocket::unpackSceneInstance(packet.instances.back());

    // The physical starting coordinate is presented as an Earth departure,
    // rather than being translated past the top edge around the Moon.
    assert(ship.centerX > -1.0F && ship.centerX < 1.0F);
    assert(ship.centerY > -1.0F && ship.centerY < 1.0F);
    assert(std::abs(ship.centerX - -0.18F) < 0.01F);
    assert(std::abs(ship.centerY - -0.70F) < 0.01F);

    // The same transform must rotate the ship with its trajectory so the
    // vehicle points away from Earth along the visible transfer direction.
    const float forwardLength = std::hypot(ship.axisYx, ship.axisYy);
    assert(forwardLength > 0.0F);
    assert(ship.axisYx / forwardLength > 0.0F);
    assert(ship.axisYy / forwardLength > 0.0F);
}

void testPhysicalApproachZoomBeginsContinuouslyAtThreeQuarters()
{
    const auto texturedInstance = [](const ScenePacket& packet, TextureId texture) {
        const rocket::SceneAtlasUvRect expected =
            rocket::mapSceneAtlasUvRect(texture, 0.0F, 0.0F, 1.0F, 1.0F);
        const auto found = std::find_if(
            packet.instances.begin(),
            packet.instances.end(),
            [&](const PackedSceneInstance& packed) {
                const SceneInstance instance = rocket::unpackSceneInstance(packed);
                return instance.textured &&
                    std::abs(instance.u0 - expected.u0) < 0.001F &&
                    std::abs(instance.v0 - expected.v0) < 0.001F &&
                    std::abs(instance.u1 - expected.u1) < 0.001F &&
                    std::abs(instance.v1 - expected.v1) < 0.001F;
            });
        assert(found != packet.instances.end());
        return rocket::unpackSceneInstance(*found);
    };
    const auto distance = [](const SceneInstance& first, const SceneInstance& second) {
        return std::hypot(first.centerX - second.centerX, first.centerY - second.centerY);
    };
    const auto spriteWidth = [](const SceneInstance& instance) {
        return 2.0F * std::hypot(instance.axisXx, instance.axisXy);
    };

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    composer.setTextureReady(TextureId::Earth, true);
    composer.setTextureReady(TextureId::Moon, true);
    composer.setTextureReady(TextureId::RocketClosed, true);

    RenderSnapshot snapshot;
    snapshot.screen = rocket::Screen::Flight;
    snapshot.destinationTier = 1;
    snapshot.frontierTransfer = true;
    snapshot.launchPhysicalFlight = true;
    snapshot.launchHeading = 0.0;

    // Just before three-quarters distance, the original transfer framing is
    // untouched. Just after it, smoothstep's zero derivative keeps both the
    // camera target and scale visually continuous.
    snapshot.launchPositionX = -3.40 * 0.251;
    snapshot.launchPositionY = 1.10 * 0.251;
    const ScenePacket beforePacket = composer.compose(snapshot);
    const SceneInstance moonBefore = texturedInstance(beforePacket, TextureId::Moon);

    snapshot.launchPositionX = -3.40 * 0.249;
    snapshot.launchPositionY = 1.10 * 0.249;
    const ScenePacket afterPacket = composer.compose(snapshot);
    const SceneInstance moonAfter = texturedInstance(afterPacket, TextureId::Moon);
    assert(distance(moonBefore, moonAfter) < 0.001F);
    assert(std::abs(spriteWidth(moonBefore) - spriteWidth(moonAfter)) < 0.001F);

    snapshot.launchPositionX = -3.40 * 0.25;
    snapshot.launchPositionY = 1.10 * 0.25;
    const ScenePacket thresholdPacket = composer.compose(snapshot);
    const SceneInstance moonThreshold = texturedInstance(thresholdPacket, TextureId::Moon);
    const SceneInstance shipThreshold = texturedInstance(thresholdPacket, TextureId::RocketClosed);

    // By the outer orbit band, the camera has completed its pan to the
    // planet-centered view and enlarged the readable actors without warping
    // the ship into the landing frame.
    constexpr double departureDistance = 3.5735136770411273;
    constexpr double outerOrbitBand = 0.44 + 0.075;
    const double remainingFraction = outerOrbitBand / departureDistance;
    snapshot.launchPositionX = -3.40 * remainingFraction;
    snapshot.launchPositionY = 1.10 * remainingFraction;
    const ScenePacket closePacket = composer.compose(snapshot);
    const SceneInstance moonClose = texturedInstance(closePacket, TextureId::Moon);
    const SceneInstance shipClose = texturedInstance(closePacket, TextureId::RocketClosed);
    assert(moonClose.centerX < moonThreshold.centerX - 0.20F);
    assert(moonClose.centerY < moonThreshold.centerY - 0.20F);
    assert(std::abs(moonClose.centerX) < 0.01F);
    assert(std::abs(moonClose.centerY) < 0.01F);
    assert(spriteWidth(moonClose) > spriteWidth(moonThreshold) * 1.35F);
    assert(std::abs(spriteWidth(shipClose) - spriteWidth(shipThreshold)) < 0.0001F);
    assert(distance(shipClose, moonClose) <= 0.47F);
    assert(!snapshot.launchLandingLocalFrame);
}

void testPhysicalApproachCameraAppliesToMarsAndLaterDestinations()
{
    const auto texturedInstance = [](const ScenePacket& packet, TextureId texture) {
        const rocket::SceneAtlasUvRect expected =
            rocket::mapSceneAtlasUvRect(texture, 0.0F, 0.0F, 1.0F, 1.0F);
        const auto found = std::find_if(
            packet.instances.begin(),
            packet.instances.end(),
            [&](const PackedSceneInstance& packed) {
                const SceneInstance instance = rocket::unpackSceneInstance(packed);
                return instance.textured &&
                    std::abs(instance.u0 - expected.u0) < 0.001F &&
                    std::abs(instance.v0 - expected.v0) < 0.001F &&
                    std::abs(instance.u1 - expected.u1) < 0.001F &&
                    std::abs(instance.v1 - expected.v1) < 0.001F;
            });
        assert(found != packet.instances.end());
        return rocket::unpackSceneInstance(*found);
    };

    constexpr double departureDistance = 3.5735136770411273;
    constexpr double outerOrbitBand = 0.44 + 0.075;
    const std::array<std::pair<int, TextureId>, 2> destinations {{
        {2, TextureId::Mars},
        {4, TextureId::Saturn},
    }};
    for (const auto& [tier, texture] : destinations) {
        SceneComposer composer;
        composer.setViewport({1280, 800, 1280, 800, 1.0F});
        composer.setTextureReady(texture, true);
        composer.setTextureReady(TextureId::RocketClosed, true);

        RenderSnapshot snapshot;
        snapshot.screen = rocket::Screen::Flight;
        snapshot.destinationTier = tier;
        snapshot.frontierTransfer = true;
        snapshot.launchPhysicalFlight = true;
        snapshot.launchHeading = 0.0;

        snapshot.launchPositionX = -3.40 * 0.251;
        snapshot.launchPositionY = 1.10 * 0.251;
        const SceneInstance before = texturedInstance(composer.compose(snapshot), texture);
        snapshot.launchPositionX = -3.40 * 0.249;
        snapshot.launchPositionY = 1.10 * 0.249;
        const SceneInstance after = texturedInstance(composer.compose(snapshot), texture);
        assert(std::hypot(
            before.centerX - after.centerX,
            before.centerY - after.centerY) < 0.001F);

        // A grazing transfer can be three quarters complete while remaining
        // outside the radial trigger. Route progress must still begin the
        // camera move continuously and finish the flyby framing.
        snapshot.launchPositionX = -1.05;
        snapshot.launchPositionY = 0.35;
        snapshot.travelProgress = 0.749;
        const SceneInstance grazingBefore = texturedInstance(composer.compose(snapshot), texture);
        snapshot.travelProgress = 0.751;
        const SceneInstance grazingAfter = texturedInstance(composer.compose(snapshot), texture);
        assert(std::hypot(
            grazingBefore.centerX - grazingAfter.centerX,
            grazingBefore.centerY - grazingAfter.centerY) < 0.02F);
        snapshot.travelProgress = 0.95;
        const SceneInstance grazingComplete = texturedInstance(composer.compose(snapshot), texture);
        assert(std::abs(grazingComplete.centerX) < 0.01F);
        assert(std::abs(grazingComplete.centerY) < 0.01F);

        const double remainingFraction = outerOrbitBand / departureDistance;
        snapshot.travelProgress = 0.0;
        snapshot.launchPositionX = -3.40 * remainingFraction;
        snapshot.launchPositionY = 1.10 * remainingFraction;
        const SceneInstance orbit = texturedInstance(composer.compose(snapshot), texture);
        assert(std::abs(orbit.centerX) < 0.01F);
        assert(std::abs(orbit.centerY) < 0.01F);
    }
}

void testPhysicalLandingCameraBlendsWithoutTeleportingUnauthorizedImpacts()
{
    const auto ship = [](SceneComposer& composer, const RenderSnapshot& snapshot) {
        const ScenePacket packet = rocket::SceneComposerTestAccess::rocketPacket(composer, snapshot);
        assert(!packet.instances.empty());
        return rocket::unpackSceneInstance(packet.instances.back());
    };
    const auto distance = [](const SceneInstance& first, const SceneInstance& second) {
        return std::hypot(first.centerX - second.centerX, first.centerY - second.centerY);
    };

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    composer.setTextureReady(TextureId::RocketClosed, true);

    RenderSnapshot snapshot;
    snapshot.screen = rocket::Screen::Flight;
    snapshot.destinationTier = 1;
    snapshot.frontierTransfer = true;
    snapshot.launchPhysicalFlight = true;
    snapshot.launchOrbitCaptured = true;
    snapshot.launchLandingAuthorized = true;
    snapshot.launchLandingVerticalVelocity = -1.0;
    snapshot.launchPositionY = 0.0;

    snapshot.launchPositionX = 0.441;
    const SceneInstance beforeDescent = ship(composer, snapshot);
    snapshot.launchPositionX = 0.439;
    const SceneInstance afterDescent = ship(composer, snapshot);
    assert(distance(beforeDescent, afterDescent) < 0.01F);

    // A wide later-world orbit remains renderable through the shared flight
    // camera; the old presentation-only horizon has been retired in favor of
    // the prepared Mining environment.
    RenderSnapshot wideOrbit = snapshot;
    wideOrbit.launchOrbitTargetRadius = 0.90;
    wideOrbit.launchOrbitGoodBand = 0.12;
    wideOrbit.launchPositionX = 0.65;
    const ScenePacket wideOrbitPacket = composer.compose(wideOrbit);
    assert(!wideOrbitPacket.instances.empty());

    snapshot.launchPositionX = 0.309;
    snapshot.launchLandingLocalFrame = true;
    const SceneInstance localLanding = ship(composer, snapshot);
    assert(std::abs(localLanding.centerX) < 0.01F);
    assert(std::abs(localLanding.centerY + 0.30F) < 0.01F);

    RenderSnapshot unauthorized = snapshot;
    unauthorized.launchLandingAuthorized = false;
    unauthorized.launchLandingLocalFrame = false;
    unauthorized.launchPositionX = 0.309;
    const SceneInstance impact = ship(composer, unauthorized);
    assert(std::hypot(impact.centerX, impact.centerY + 0.30F) > 0.08F);
}

void testPhysicalLandingCameraDoesNotRetainTransferBodies()
{
    const auto hasTexture = [](const ScenePacket& packet, TextureId texture) {
        const rocket::SceneAtlasUvRect expected =
            rocket::mapSceneAtlasUvRect(texture, 0.0F, 0.0F, 1.0F, 1.0F);
        return std::any_of(
            packet.instances.begin(),
            packet.instances.end(),
            [&](const PackedSceneInstance& packed) {
                const SceneInstance instance = rocket::unpackSceneInstance(packed);
                return instance.textured &&
                    std::abs(instance.u0 - expected.u0) < 0.001F &&
                    std::abs(instance.v0 - expected.v0) < 0.001F &&
                    std::abs(instance.u1 - expected.u1) < 0.001F &&
                    std::abs(instance.v1 - expected.v1) < 0.001F;
            });
    };

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    composer.setTextureReady(TextureId::Earth, true);
    composer.setTextureReady(TextureId::Moon, true);
    composer.setTextureReady(TextureId::RocketClosed, true);

    RenderSnapshot snapshot;
    snapshot.screen = rocket::Screen::Flight;
    snapshot.destinationTier = 1;
    snapshot.frontierTransfer = true;
    snapshot.launchPhysicalFlight = true;
    snapshot.launchLandingLocalFrame = true;
    snapshot.launchPositionX = 0.24;
    snapshot.launchPositionY = 0.0;
    snapshot.launchLandingAltitude = 8.0;

    const ScenePacket& packet = composer.compose(snapshot);
    assert(!hasTexture(packet, TextureId::Earth));
    assert(!hasTexture(packet, TextureId::Moon));
    assert(hasTexture(packet, TextureId::RocketClosed));
}

void testExistingOrbitalShaftsRemainVisibleOutsideTheirActiveWedge()
{
    RenderSnapshot snapshot;
    snapshot.screen = rocket::Screen::Flight;
    snapshot.destinationTier = 2;
    snapshot.frontierTransfer = true;
    snapshot.launchPhysicalFlight = true;
    snapshot.launchApproachBlend = 1.0;
    snapshot.launchPositionX = 0.44;
    snapshot.launchPositionY = 0.0;
    snapshot.orbitalOverlay = 1.0;
    snapshot.landingZones = rocket::planetLandingZones();
    snapshot.orbitalZone = snapshot.landingZones[3];

    SceneComposer baselineComposer;
    baselineComposer.setViewport({1280, 800, 1280, 800, 1.0F});
    const ScenePacket baseline = baselineComposer.compose(snapshot);

    snapshot.orbitalExistingShafts.push_back({
        snapshot.landingZones[0].centerBearing,
        2.4});
    SceneComposer shaftComposer;
    shaftComposer.setViewport({1280, 800, 1280, 800, 1.0F});
    const ScenePacket withShaft = shaftComposer.compose(snapshot);

    // A stored shaft contributes two radial strokes and two mouth rings even
    // while a different wedge owns the active survey and landing guidance.
    assert(withShaft.instances.size() >= baseline.instances.size() + 4U);

    snapshot.orbitalExistingShafts.clear();
    snapshot.orbitalOverlay = 0.0;
    snapshot.orbitalZoneSurveyed = false;
    snapshot.orbitalArtifactHint = true;
    snapshot.orbitalArtifactBearing = snapshot.landingZones[0].centerBearing;
    const auto artifactGlows = [](const ScenePacket& packet) {
        std::vector<SceneInstance> result;
        for (const auto& packed : packet.instances) {
            const auto instance = rocket::unpackSceneInstance(packed);
            if (instance.shape == SceneInstanceShape::RadialGlow && instance.color.r > 0.70F &&
                instance.color.r < 0.85F && instance.color.g < 0.65F && instance.color.b > 0.98F)
                result.push_back(instance);
        }
        return result;
    };
    const auto hint = artifactGlows(shaftComposer.compose(snapshot));
    assert(hint.size() == 2);
    snapshot.orbitalZone = snapshot.landingZones[2];
    const auto changedSlice = artifactGlows(shaftComposer.compose(snapshot));
    assert(changedSlice.size() == hint.size());
    assert(std::abs(changedSlice[0].centerX - hint[0].centerX) < 0.0001F);
    assert(std::abs(changedSlice[0].centerY - hint[0].centerY) < 0.0001F);
    snapshot.orbitalArtifactHint = false;
    assert(artifactGlows(shaftComposer.compose(snapshot)).empty());
    const auto withoutMission = shaftComposer.compose(snapshot).instances.size();
    snapshot.missionSectorVisible = true;
    snapshot.missionSector = snapshot.landingZones[0];
    snapshot.missionSectorLabel = "MISSION LANDING SITE / Sector 1";
    const ScenePacket mission = shaftComposer.compose(snapshot);
    assert(mission.instances.size() > withoutMission + 26U);
    // Selecting another scan wedge must not move the mission outline.
    snapshot.orbitalZone = snapshot.landingZones[4];
    const ScenePacket otherWedge = shaftComposer.compose(snapshot);
    assert(otherWedge.instances.size() == mission.instances.size());
    for (std::size_t i = 0; i < mission.instances.size(); ++i) {
        const auto a = rocket::unpackSceneInstance(mission.instances[i]);
        const auto b = rocket::unpackSceneInstance(otherWedge.instances[i]);
        assert(std::abs(a.centerX-b.centerX) < .00001F && std::abs(a.centerY-b.centerY) < .00001F);
    }
}


void testIoScanAndOrbitShareArtAndArtifactSignal()
{
    const auto hasTexture=[](const ScenePacket& packet,TextureId texture) {
        return std::any_of(packet.draws.begin(),packet.draws.end(),[&](const auto& draw){return draw.texture==texture;});
    };
    for (int width : {800,1600}) {
        RenderSnapshot snapshot;
        snapshot.screen=rocket::Screen::Flight;
        snapshot.launchPhysicalFlight=true;
        snapshot.systemTravel=true;
        snapshot.system=rocket::solarSystemDefinition();
        std::erase_if(snapshot.system.bodies,[](const auto& body){return body.id!="io";});
        snapshot.systemLocation.bodyId="io";
        snapshot.systemLocation.frame=rocket::CoordinateFrame::Body;
        snapshot.destinationTier=3; // Io deliberately shares Jupiter's environment.
        snapshot.launchPositionX=.7;
        snapshot.launchOrbitCaptured=true;
        snapshot.launchApproachBlend=1.0;
        snapshot.orbitalArtifactHint=true;
        snapshot.orbitalArtifactBearing=rocket::planetLandingZones()[0].centerBearing;
        snapshot.landingZones=rocket::planetLandingZones();
        for (const auto& zone:snapshot.landingZones) {
            snapshot.orbitalZone=zone;
            for (double overlay : {0.0,1.0}) {
                snapshot.orbitalOverlay=overlay;
                SceneComposer composer;
                composer.setViewport({width,900,width,900,1.0F});
                composer.setTextureReady(TextureId::Moon,true);
                composer.setTextureReady(TextureId::Jupiter,true);
                const auto& packet=composer.compose(snapshot);
                assert(hasTexture(packet,TextureId::Moon));
                assert(!hasTexture(packet,TextureId::Jupiter));
                int glows=0;
                for(const auto& packed:packet.instances) {
                    const auto instance=rocket::unpackSceneInstance(packed);
                    if(instance.shape==SceneInstanceShape::RadialGlow && instance.color.r>.70F &&
                        instance.color.r<.85F && instance.color.g<.65F && instance.color.b>.98F) ++glows;
                }
                assert(glows==2);
            }
        }
    }
}

void testUndiscoveredStraylightIsForeshadowedBehindNeptuneOnly()
{
    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    composer.setTextureReady(TextureId::Neptune, true);
    composer.setTextureReady(TextureId::ArkOperational, true);

    RenderSnapshot neptune;
    neptune.screen = rocket::Screen::Flight;
    neptune.destinationTier = 6;
    neptune.frontierTransfer = true;
    neptune.arkCondition = rocket::ArkCondition::NotFound;
    const ScenePacket& neptunePacket = composer.compose(neptune);
    assert(std::none_of(
        neptunePacket.draws.begin(),
        neptunePacket.draws.end(),
        [](const SceneDraw& draw) { return draw.texture == TextureId::ArkOperational; }));

    RenderSnapshot uranus = neptune;
    uranus.destinationTier = 5;
    const ScenePacket& uranusPacket = composer.compose(uranus);
    assert(std::none_of(
        uranusPacket.draws.begin(),
        uranusPacket.draws.end(),
        [](const SceneDraw& draw) {
            return draw.texture == TextureId::ArkOperational;
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
    composer.setViewport({1280, 800, 2560, 1600, 2.0F});
    composer.setPresentationTime(0.0);
    composer.setTextureReady(TextureId::LocalSolarBackground, true);
    composer.setTextureReady(TextureId::Earth, true);
    composer.setTextureReady(TextureId::Moon, true);

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
    assert(packet.draws.size() >= 2U);
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
    const SceneDraw& firstWorldDraw = packet.draws[1];
    assert(firstWorldDraw.drawType == SceneDrawType::InstancedQuad);
    assert(firstWorldDraw.pipeline == PipelineClass::Textured);
    assert(firstWorldDraw.coordinateSpace == CoordinateSpace::World);
    assert(firstWorldDraw.atlasPage == rocket::sceneAtlasPageForTexture(TextureId::Moon));
    assert(firstWorldDraw.instanceCount >= 82U);
    assert(std::any_of(packet.draws.begin() + 1, packet.draws.end(), [](const SceneDraw& draw) {
        return draw.drawType == SceneDrawType::InstancedQuad
            && draw.pipeline == PipelineClass::Textured
            && draw.coordinateSpace == CoordinateSpace::World
            && draw.atlasPage == rocket::sceneAtlasPageForTexture(TextureId::Earth);
    }));

    std::array<SceneInstance, 2> titleBodies {};
    std::size_t bodyCount = 0;
    for (std::size_t drawIndex = 1; drawIndex < packet.draws.size(); ++drawIndex) {
        const SceneDraw& draw = packet.draws[drawIndex];
        for (std::size_t index = 0; index < draw.instanceCount; ++index) {
            const SceneInstance instance = rocket::unpackSceneInstance(
                packet.instances[draw.firstInstance + index]);
            if (instance.textured) {
                titleBodies[bodyCount++] = instance;
            }
        }
    }
    // At t=0 the moon is on the far (upper) half of its tilted orbit. It
    // must be submitted first so Earth occludes it, and both bodies must be
    // fully opaque where their source pixels are opaque.
    assert(bodyCount == titleBodies.size());
    assert(titleBodies[0].centerY > titleBodies[1].centerY);
    assert(titleBodies[0].color.a > 0.99F);
    assert(titleBodies[1].color.a > 0.99F);

    const SceneInstance radialGlow = rocket::unpackSceneInstance(
        packet.instances[firstWorldDraw.firstInstance]);
    assert(radialGlow.shape == SceneInstanceShape::RadialGlow);
    assert(radialGlow.segments == 64U);

    // drawTitleBackdrop emits a 64-segment radial glow followed by an
    // 80-segment, one-physical-pixel orbit line. The line instance axes use
    // the same physical-pixel perpendicular math as the triangle fallback.
    const SceneInstance orbitLine = rocket::unpackSceneInstance(
        packet.instances[firstWorldDraw.firstInstance + 1U]);
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
    composer.setViewport({1280, 800, 2560, 1200, 1.5F});

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
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    composer.setPresentationTime(0.0);
    composer.setTextureReady(TextureId::RocketOpen, true);
    composer.setTextureReady(TextureId::MiningDrone, true);
    composer.setTextureReady(TextureId::RocketClosed, true);

    RenderSnapshot snapshot;
    snapshot.screen = rocket::Screen::Flight;
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
    std::array<const SceneDraw*, logicalTextures.size()> logicalDraws {};
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
            logicalDraws[logicalTextureIndex] = &draw;
            ++logicalTextureIndex;
        }
    }
    assert(logicalTextureIndex == logicalTextures.size());
    assert(logicalDraws[0] != nullptr && logicalDraws[1] != nullptr && logicalDraws[2] != nullptr);
    assert(logicalDraws[0]->atlasPage == rocket::sceneAtlasPageForTexture(TextureId::RocketOpen));
    assert(logicalDraws[1]->atlasPage == rocket::sceneAtlasPageForTexture(TextureId::MiningDrone));
    assert(logicalDraws[2]->atlasPage == rocket::sceneAtlasPageForTexture(TextureId::RocketClosed));
    const auto samePage = [](TextureId left, TextureId right) {
        return rocket::sceneAtlasPageForTexture(left) == rocket::sceneAtlasPageForTexture(right);
    };
    assert((logicalDraws[0] == logicalDraws[1])
        == samePage(logicalTextures[0], logicalTextures[1]));
    assert((logicalDraws[1] == logicalDraws[2])
        == samePage(logicalTextures[1], logicalTextures[2]));
    // Equal pages can merge only when no intervening page creates an order
    // barrier. The concrete packing is intentionally free to change as the
    // authored atlas grows.
    if (samePage(logicalTextures[0], logicalTextures[2])
        && !samePage(logicalTextures[0], logicalTextures[1])) {
        assert(logicalDraws[0] != logicalDraws[2]);
    }
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
    snapshot.miningShipPresent = mining.depthZone == mining.entryDepthZone;
    snapshot.bindMiningFrameViews(mining);
    return snapshot;
}


void testTetheredArtifactAuraHasNoRectangularOverlay()
{
    rocket::MiningRunState mining;
    mining.active = true;
    mining.terrain.width = 3;
    mining.terrain.height = 3;
    mining.terrain.cells.resize(9);
    mining.droneX = 1.5;
    mining.droneY = 1.5;
    mining.targetTipX = mining.droneX;
    mining.targetTipY = mining.droneY;

    RenderSnapshot snapshot = miningSnapshot(mining);
    snapshot.miningShipPresent = false;
    snapshot.miningArtifact = {
        true,
        1.5,
        1.5,
        1.0,
        1.0,
        0,
        0,
        static_cast<int>(rocket::MiningArtifactState::Loose),
        true,
        true
    };

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    const ScenePacket& packet = composer.compose(snapshot);

    const auto artifactGlow = std::find_if(
        packet.instances.begin(),
        packet.instances.end(),
        [](const PackedSceneInstance& packed) {
            const SceneInstance instance = rocket::unpackSceneInstance(packed);
            return !instance.textured
                && instance.shape == SceneInstanceShape::RadialGlow
                && instance.color.r > 0.70F
                && instance.color.b > 0.90F
                && instance.color.a > 0.20F;
        });
    assert(artifactGlow != packet.instances.end());

    const SceneInstance glow = rocket::unpackSceneInstance(*artifactGlow);
    const bool hasArtifactRectangle = std::any_of(
        packet.instances.begin(),
        packet.instances.end(),
        [&](const PackedSceneInstance& packed) {
            const SceneInstance instance = rocket::unpackSceneInstance(packed);
            return !instance.textured
                && instance.shape == SceneInstanceShape::Rectangle
                && std::abs(instance.centerX - glow.centerX) < 0.002F
                && std::abs(instance.centerY - glow.centerY) < 0.002F;
        });
    assert(!hasArtifactRectangle);
}

void testTriangulationUsesOneThreeSliceAuraAndHidesArtifactGlow()
{
    rocket::MiningRunState mining;
    mining.active = true;
    mining.terrain.width = 20;
    mining.terrain.height = 20;
    mining.terrain.cells.resize(400);
    mining.droneX = 10.0;
    mining.droneY = 4.0;
    mining.targetTipX = 10.0;
    mining.targetTipY = 5.0;
    mining.gate.active = true;
    mining.gate.type = rocket::MiningGateType::SurveyTriangulation;
    mining.gate.state = rocket::MiningGateState::Locked;
    mining.gate.anchorX = 10.5;
    mining.gate.anchorY = 10.5;
    mining.gate.surveyComplete = false;
    mining.gate.markers = {
        {14.5, 10.5, true},
        {8.5, 13.96, false},
        {8.5, 7.04, false}
    };

    RenderSnapshot snapshot = miningSnapshot(mining);
    snapshot.miningArtifact = {
        true,
        10.5,
        10.5,
        1.0,
        1.0,
        0,
        0,
        static_cast<int>(rocket::MiningArtifactState::Embedded),
        false,
        false,
        static_cast<int>(rocket::MiningGateType::SurveyTriangulation),
        static_cast<int>(rocket::MiningGateState::Locked)
    };
    assert(snapshot.miningTriangulation.active);
    assert(snapshot.miningTriangulation.completed[0]);
    assert(!snapshot.miningTriangulation.completed[1]);
    assert(!snapshot.miningTriangulation.completed[2]);
    assert(snapshot.miningTriangulation.radius > 4.0);

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    const ScenePacket& packet = composer.compose(snapshot);
    const bool hasPurpleArtifactGlow = std::any_of(
        packet.instances.begin(),
        packet.instances.end(),
        [](const PackedSceneInstance& packed) {
            const SceneInstance instance = rocket::unpackSceneInstance(packed);
            return instance.shape == SceneInstanceShape::RadialGlow &&
                instance.color.r > 0.74F && instance.color.r < 0.82F &&
                instance.color.g > 0.48F && instance.color.g < 0.60F &&
                instance.color.b > 0.94F &&
                instance.color.a > 0.25F && instance.color.a < 0.35F;
        });
    assert(!hasPurpleArtifactGlow);
    assert(!packet.vertices.empty());

    for (rocket::MiningGateMarker& marker : mining.gate.markers) {
        marker.activated = true;
    }
    mining.gate.surveyComplete = true;
    RenderSnapshot completed = miningSnapshot(mining);
    assert(!completed.miningTriangulation.active);
}

std::size_t countInstanceShape(const ScenePacket& packet, SceneInstanceShape shape)
{
    return static_cast<std::size_t>(std::count_if(
        packet.instances.begin(),
        packet.instances.end(),
        [shape](const PackedSceneInstance& packed) {
            return rocket::unpackSceneInstance(packed).shape == shape;
        }));
}

std::vector<PackedSceneInstance> attackDroneInstances(const RenderSnapshot& snapshot)
{
    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
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

SceneInstance spriteInstance(
    const ScenePacket& packet,
    TextureId texture,
    float sourceU0,
    float sourceV0,
    float sourceU1,
    float sourceV1)
{
    const rocket::SceneAtlasUvRect expected = rocket::mapSceneAtlasUvRect(
        texture, sourceU0, sourceV0, sourceU1, sourceV1);
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
    assert(false && "Expected the requested textured scene instance.");
    return {};
}

SceneInstance miningRigInstance(const ScenePacket& packet)
{
    return spriteInstance(packet, TextureId::MiningDrone, 0.0F, 0.0F, 1.0F, 1.0F);
}

SceneInstance miningDrillBitInstance(const ScenePacket& packet)
{
    return spriteInstance(packet, TextureId::DrillBit, 0.0F, 0.0F, 1.0F / 6.0F, 1.0F);
}

struct ScenePoint {
    float x = 0.0F;
    float y = 0.0F;
};

ScenePoint miningDrillCollar(const SceneInstance& drill)
{
    // The bit is drawn with forward = -drillDirection, so its positive axis-Y
    // endpoint is the authored collar/root at the rig mount.
    return {drill.centerX + drill.axisYx, drill.centerY + drill.axisYy};
}

void assertMiningDrillMounted(const SceneInstance& rig, const SceneInstance& drill)
{
    const float rigHalfHeight = std::hypot(rig.axisYx, rig.axisYy);
    assert(rigHalfHeight > 0.001F);
    const float forwardX = -rig.axisYx / rigHalfHeight;
    const float forwardY = -rig.axisYy / rigHalfHeight;
    const ScenePoint collar = miningDrillCollar(drill);
    const float offsetX = collar.x - rig.centerX;
    const float offsetY = collar.y - rig.centerY;
    const float forwardOffset = offsetX * forwardX + offsetY * forwardY;
    const float perpendicularOffset = offsetX * forwardY - offsetY * forwardX;
    assert(std::abs(perpendicularOffset) < 0.0005F);
    // Packed scene instances quantize positions independently. A half-cell
    // actor-anchor correction can move the two values across adjacent packing
    // steps without changing the authored drill mount.
    assert(std::abs(forwardOffset - rigHalfHeight * 2.0F * 0.18F) < 0.00075F);
}

std::vector<PackedSceneInstance> nonTexturedFrameInstances(const ScenePacket& packet)
{
    std::vector<PackedSceneInstance> result;
    for (const PackedSceneInstance& packed : packet.instances) {
        if (!rocket::unpackSceneInstance(packed).textured) {
            result.push_back(packed);
        }
    }
    return result;
}

bool samePackedInstances(
    const std::vector<PackedSceneInstance>& left,
    const std::vector<PackedSceneInstance>& right)
{
    return left.size() == right.size()
        && (left.empty() || std::memcmp(
            left.data(),
            right.data(),
            left.size() * sizeof(PackedSceneInstance)) == 0);
}

std::uint8_t miningMaterialMarkerSegments(rocket::MiningCellMaterial material)
{
    switch (material) {
    case rocket::MiningCellMaterial::CommonOre:
        return 6U;
    case rocket::MiningCellMaterial::RareOre:
        return 4U;
    case rocket::MiningCellMaterial::ExoticVein:
        return 3U;
    default:
        return 0U;
    }
}

bool containsMiningMaterialMarker(
    const ScenePacket& packet,
    rocket::MiningCellMaterial material,
    Color expectedColor)
{
    const std::uint8_t expectedSegments = miningMaterialMarkerSegments(material);
    for (const PackedSceneInstance& packed : packet.instances) {
        const SceneInstance instance = rocket::unpackSceneInstance(packed);
        if (!instance.textured
            && instance.shape == SceneInstanceShape::Polygon
            && instance.segments == expectedSegments
            && std::abs(instance.color.r - expectedColor.r) < 0.015F
            && std::abs(instance.color.g - expectedColor.g) < 0.015F
            && std::abs(instance.color.b - expectedColor.b) < 0.015F) {
            return true;
        }
    }
    return false;
}

void testMiningEVAUsesDedicatedTextureWithoutFallback()
{
    rocket::MiningRunState mining = miningState(20.0, 20.0);
    RenderSnapshot snapshot = miningSnapshot(mining);
    snapshot.miningShipPresent = false;
    snapshot.miningOperatorPresent = true;
    snapshot.miningOperatorActive = true;
    snapshot.miningOperatorX = 2.0;
    snapshot.miningOperatorY = 2.0;
    snapshot.miningOperatorAimX = 1.0;
    snapshot.miningOperatorAimY = 0.0;
    snapshot.miningAnchorValid = true;
    snapshot.miningAnchorX = snapshot.miningOperatorX;
    snapshot.miningAnchorY = snapshot.miningOperatorY;

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    composer.setTextureReady(TextureId::MiningDrone, true);
    composer.setTextureReady(TextureId::JetpackCapybara, true);
    const ScenePacket& packet = composer.compose(snapshot);
    assertValidDrawRanges(packet);
    const SceneInstance rig = miningRigInstance(packet);
    const SceneInstance suit = spriteInstance(
        packet,
        TextureId::JetpackCapybara,
        0.0F,
        0.0F,
        1.0F,
        1.0F);
    assert(std::abs(rig.centerX - suit.centerX) > 0.01F);
    assert(std::abs(rig.centerY - suit.centerY) > 0.01F);

    SceneComposer missingTextureComposer;
    missingTextureComposer.setViewport({1280, 800, 1280, 800, 1.0F});
    missingTextureComposer.setTextureReady(TextureId::MiningDrone, true);
    const std::vector<PackedSceneInstance> missingTextureSolids =
        nonTexturedFrameInstances(missingTextureComposer.compose(snapshot));

    snapshot.miningOperatorPresent = false;
    SceneComposer absentOperatorComposer;
    absentOperatorComposer.setViewport({1280, 800, 1280, 800, 1.0F});
    absentOperatorComposer.setTextureReady(TextureId::MiningDrone, true);
    const ScenePacket& absentPacket = absentOperatorComposer.compose(snapshot);
    const std::vector<PackedSceneInstance> absentOperatorSolids =
        nonTexturedFrameInstances(absentPacket);
    assert(samePackedInstances(missingTextureSolids, absentOperatorSolids));
    assert(std::none_of(absentPacket.draws.begin(), absentPacket.draws.end(), [](const SceneDraw& draw) {
        return draw.texture == TextureId::JetpackCapybara;
    }));
}

void testMiningEvaDeathAddsPresentationWithoutReplacingTheSuit()
{
    rocket::MiningRunState mining = miningState(20.0, 20.0);
    RenderSnapshot snapshot = miningSnapshot(mining);
    snapshot.miningShipPresent = false;
    snapshot.miningOperatorPresent = true;
    snapshot.miningOperatorActive = true;
    snapshot.miningOperatorX = 2.0;
    snapshot.miningOperatorY = 2.0;
    snapshot.miningOperatorAimX = 1.0;
    snapshot.miningOperatorAimY = 0.0;
    snapshot.miningAnchorValid = true;
    snapshot.miningAnchorX = snapshot.miningOperatorX;
    snapshot.miningAnchorY = snapshot.miningOperatorY;

    SceneComposer baselineComposer;
    baselineComposer.setViewport({1280, 800, 1280, 800, 1.0F});
    baselineComposer.setTextureReady(TextureId::MiningDrone, true);
    baselineComposer.setTextureReady(TextureId::JetpackCapybara, true);
    const ScenePacket& baselinePacket = baselineComposer.compose(snapshot);
    assertValidDrawRanges(baselinePacket);
    const SceneInstance baselineSuit = spriteInstance(
        baselinePacket,
        TextureId::JetpackCapybara,
        0.0F,
        0.0F,
        1.0F,
        1.0F);
    const std::size_t baselineSolidCount = nonTexturedFrameInstances(baselinePacket).size();

    snapshot.miningEvaDeathActive = true;
    snapshot.miningEvaDeathProgress = 0.55;
    snapshot.miningFailurePulse = 0.8;
    SceneComposer deathComposer;
    deathComposer.setViewport({1280, 800, 1280, 800, 1.0F});
    deathComposer.setTextureReady(TextureId::MiningDrone, true);
    deathComposer.setTextureReady(TextureId::JetpackCapybara, true);
    const ScenePacket& deathPacket = deathComposer.compose(snapshot);
    assertValidDrawRanges(deathPacket);
    const SceneInstance deathSuit = spriteInstance(
        deathPacket,
        TextureId::JetpackCapybara,
        0.0F,
        0.0F,
        1.0F,
        1.0F);

    assert(std::abs(deathSuit.centerY - baselineSuit.centerY) > 0.01F);
    assert(std::abs(deathSuit.axisXx - baselineSuit.axisXx) > 0.01F
        || std::abs(deathSuit.axisXy - baselineSuit.axisXy) > 0.01F);
    assert(deathSuit.color.r > deathSuit.color.g);
    assert(nonTexturedFrameInstances(deathPacket).size() > baselineSolidCount);
}

void testMiningActiveAnchorOwnsDefenseEffects()
{
    rocket::MiningRunState mining;
    mining.terrain.width = 4;
    mining.terrain.height = 4;
    mining.terrain.cells.resize(16);
    mining.droneX = 0.5;
    mining.droneY = 0.5;
    mining.targetTipX = 2.0;
    mining.targetTipY = 2.0;
    rocket::MiningMiniDroneAgent defense;
    defense.role = rocket::MiniDroneRole::Defense;
    defense.behavior = rocket::MiningMiniDroneBehavior::Guarding;
    defense.x = 3.0;
    defense.y = 1.5;
    defense.shieldCharge = 1.0;
    mining.miniDrones.push_back(defense);

    RenderSnapshot snapshot = miningSnapshot(mining);
    snapshot.miningShipPresent = false;
    snapshot.miningOperatorActive = true;
    snapshot.miningOperatorPresent = false;
    snapshot.miningOperatorX = 1.25;
    snapshot.miningOperatorY = 1.25;
    snapshot.miningAnchorValid = true;
    snapshot.miningAnchorX = snapshot.miningOperatorX;
    snapshot.miningAnchorY = snapshot.miningOperatorY;

    const auto composeSolids = [](const RenderSnapshot& source) {
        SceneComposer composer;
        composer.setViewport({1280, 800, 1280, 800, 1.0F});
        composer.setTextureReady(TextureId::MiningDrone, true);
        composer.setTextureReady(TextureId::MiniDroneDefense, true);
        return nonTexturedFrameInstances(composer.compose(source));
    };

    const std::vector<PackedSceneInstance> base = composeSolids(snapshot);
    snapshot.miningDroneX = 2.75;
    snapshot.miningDroneY = 2.75;
    const std::vector<PackedSceneInstance> movedRig = composeSolids(snapshot);
    assert(samePackedInstances(base, movedRig));

    snapshot.miningOperatorX = 2.25;
    snapshot.miningOperatorY = 2.25;
    snapshot.miningAnchorX = snapshot.miningOperatorX;
    snapshot.miningAnchorY = snapshot.miningOperatorY;
    const std::vector<PackedSceneInstance> movedAnchor = composeSolids(snapshot);
    assert(!samePackedInstances(base, movedAnchor));
}

void testMiningLooseObjectsAreVisibleWorldEntities()
{
    rocket::MiningRunState mining;
    mining.terrain.width = 4;
    mining.terrain.height = 4;
    mining.terrain.cells.resize(16);
    mining.droneX = 1.0;
    mining.droneY = 1.0;
    mining.targetTipX = 1.0;
    mining.targetTipY = 2.0;

    RenderSnapshot baselineSnapshot = miningSnapshot(mining);
    baselineSnapshot.miningShipPresent = false;
    SceneComposer baselineComposer;
    baselineComposer.setViewport({1280, 800, 1280, 800, 1.0F});
    const ScenePacket& baseline = baselineComposer.compose(baselineSnapshot);
    const std::size_t baselineVertices = baseline.vertices.size();
    const std::size_t baselineInstances = baseline.instances.size();

    const std::array<std::pair<rocket::MiningCellMaterial, Color>, 3> materials {{
        {rocket::MiningCellMaterial::CommonOre, {0.74F, 0.78F, 0.84F, 1.0F}},
        {rocket::MiningCellMaterial::RareOre, {1.0F, 0.74F, 0.24F, 1.0F}},
        {rocket::MiningCellMaterial::ExoticVein, {0.78F, 0.42F, 1.0F, 1.0F}},
    }};
    for (const auto& [material, color] : materials) {
        mining.looseObjects.clear();
        rocket::MiningLooseObject chunk;
        chunk.material = material;
        chunk.x = 2.0;
        chunk.y = 2.0;
        chunk.velocityX = 0.4;
        chunk.velocityY = -0.2;
        chunk.cargoValue = 2;
        mining.looseObjects.push_back(chunk);
        RenderSnapshot chunkSnapshot = miningSnapshot(mining);
        chunkSnapshot.miningShipPresent = false;
        SceneComposer chunkComposer;
        chunkComposer.setViewport({1280, 800, 1280, 800, 1.0F});
        const ScenePacket& withChunk = chunkComposer.compose(chunkSnapshot);
        assert(withChunk.vertices.size() >= baselineVertices);
        assert(withChunk.instances.size() > baselineInstances);
        assert(containsMiningMaterialMarker(withChunk, material, color));
    }
}

void testMiningLooseObjectsUseContinuousWorldCoordinates()
{
    rocket::MiningRunState mining;
    mining.terrain.width = 4;
    mining.terrain.height = 4;
    mining.terrain.cells.resize(16);
    mining.droneX = 1.0;
    mining.droneY = 1.0;
    mining.targetTipX = 1.0;
    mining.targetTipY = 2.0;

    rocket::MiningLooseObject chunk;
    chunk.material = rocket::MiningCellMaterial::CommonOre;
    chunk.x = 2.5;
    chunk.y = 2.5;
    mining.looseObjects.push_back(chunk);

    RenderSnapshot snapshot = miningSnapshot(mining);
    snapshot.miningShipPresent = false;
    snapshot.miningArtifact = {
        true,
        chunk.x,
        chunk.y,
        1.0,
        1.0,
        0,
        0,
        static_cast<int>(rocket::MiningArtifactState::Loose),
        true,
        false
    };

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    const ScenePacket& packet = composer.compose(snapshot);

    const auto artifactGlow = std::find_if(
        packet.instances.begin(),
        packet.instances.end(),
        [](const PackedSceneInstance& packed) {
            const SceneInstance instance = rocket::unpackSceneInstance(packed);
            return !instance.textured
                && instance.shape == SceneInstanceShape::RadialGlow
                && instance.color.r > 0.70F
                && instance.color.b > 0.90F
                && instance.color.a > 0.20F;
        });
    const auto oreMarker = std::find_if(
        packet.instances.begin(),
        packet.instances.end(),
        [](const PackedSceneInstance& packed) {
            const SceneInstance instance = rocket::unpackSceneInstance(packed);
            return !instance.textured
                && instance.shape == SceneInstanceShape::Polygon
                && instance.segments == miningMaterialMarkerSegments(
                    rocket::MiningCellMaterial::CommonOre)
                && std::abs(instance.color.r - 0.74F) < 0.015F
                && std::abs(instance.color.g - 0.78F) < 0.015F
                && std::abs(instance.color.b - 0.84F) < 0.015F;
        });

    assert(artifactGlow != packet.instances.end());
    assert(oreMarker != packet.instances.end());
    const SceneInstance artifact = rocket::unpackSceneInstance(*artifactGlow);
    const SceneInstance ore = rocket::unpackSceneInstance(*oreMarker);
    assert(std::abs(ore.centerX - artifact.centerX) < 0.002F);
    assert(std::abs(ore.centerY - artifact.centerY) < 0.002F);
}

void testMiningCellsAndScannerMarksUseMaterialSilhouettes()
{
    const std::array<std::pair<rocket::MiningCellMaterial, Color>, 3> materials {{
        {rocket::MiningCellMaterial::CommonOre, {0.74F, 0.78F, 0.84F, 1.0F}},
        {rocket::MiningCellMaterial::RareOre, {1.0F, 0.74F, 0.24F, 1.0F}},
        {rocket::MiningCellMaterial::ExoticVein, {0.78F, 0.42F, 1.0F, 1.0F}},
    }};
    for (const auto& [material, color] : materials) {
        rocket::MiningRunState mining;
        mining.terrain.width = 4;
        mining.terrain.height = 4;
        mining.terrain.cells.resize(16);
        mining.droneX = 0.5;
        mining.droneY = 0.5;
        mining.targetTipX = 0.5;
        mining.targetTipY = 1.5;
        mining.terrain.cells[5].material = rocket::MiningCellMaterial::Empty;
        mining.terrain.cells[5].revealed = true;
        mining.terrain.cells[6].material = material;
        mining.terrain.cells[6].maxToughness = 1.0;
        mining.terrain.cells[6].remainingToughness = 1.0;

        SceneComposer scannerComposer;
        scannerComposer.setViewport({1280, 800, 1280, 800, 1.0F});
        RenderSnapshot hiddenScannerSnapshot = miningSnapshot(mining);
        hiddenScannerSnapshot.miningShipPresent = false;
        hiddenScannerSnapshot.miningScannerPulse = rocket::tuning::mining::scannerPulseSeconds;
        const ScenePacket& hiddenScannerPacket = scannerComposer.compose(hiddenScannerSnapshot);
        assert(!containsMiningMaterialMarker(hiddenScannerPacket, material, color));

        // The simulation is authoritative for discovery. Once it marks the
        // material revealed, scanner and terrain presentation may use its
        // resource silhouette; an unrevealed cell must never leak through
        // renderer-side adjacency caching.
        mining.terrain.cells[6].revealed = true;
        RenderSnapshot scannerSnapshot = miningSnapshot(mining);
        scannerSnapshot.miningShipPresent = false;
        scannerSnapshot.miningScannerPulse = rocket::tuning::mining::scannerPulseSeconds;
        const ScenePacket& scannerPacket = scannerComposer.compose(scannerSnapshot);
        assert(containsMiningMaterialMarker(scannerPacket, material, color));

        RenderSnapshot cellSnapshot = miningSnapshot(mining);
        cellSnapshot.miningShipPresent = false;
        SceneComposer cellComposer;
        cellComposer.setViewport({1280, 800, 1280, 800, 1.0F});
        const ScenePacket& cellPacket = cellComposer.compose(cellSnapshot);
        assert(containsMiningMaterialMarker(cellPacket, material, color));
    }
}

void testCocoonHasNoConnectingArms()
{
    rocket::MiningRunState mining;
    mining.terrain.width = 9;
    mining.terrain.height = 9;
    mining.terrain.cells.resize(81);
    mining.artifact.present = true;
    mining.artifact.x = 4.5;
    mining.artifact.y = 4.5;
    mining.artifact.state = rocket::MiningArtifactState::Embedded;
    mining.gate.type = rocket::MiningGateType::HazardCocoon;
    for (const auto [x, y] : {std::pair{4, 2}, {6, 4}, {4, 6}, {2, 4}}) {
        auto& cell = mining.terrain.cells[y * 9 + x];
        cell.material = rocket::MiningCellMaterial::HazardPocket;
        cell.revealed = true;
        cell.gateAssociated = true;
        cell.cocoonLayer = 0;
    }
    const auto jawVertices = [&](RenderSnapshot snapshot) {
        snapshot.miningArtifact.present = mining.artifact.present;
        snapshot.miningArtifact.x = mining.artifact.x;
        snapshot.miningArtifact.y = mining.artifact.y;
        snapshot.miningArtifact.state = static_cast<int>(mining.artifact.state);
        snapshot.miningArtifact.tethered = mining.artifact.tethered;
        SceneComposer composer;
        composer.setViewport({1280, 800, 1280, 800, 1.0F});
        const auto& packet = composer.compose(snapshot);
        return std::count_if(packet.vertices.begin(), packet.vertices.end(), [](const auto& packed) {
            const auto v = rocket::unpackSceneVertex(packed);
            return std::abs(v.r - 0.12F) < 0.005F && std::abs(v.g - 0.08F) < 0.005F &&
                std::abs(v.b - 0.16F) < 0.005F;
        });
    };
    assert(jawVertices(miningSnapshot(mining)) == 0);
    mining.terrain.cells[2 * 9 + 4].material = rocket::MiningCellMaterial::Empty;
    assert(jawVertices(miningSnapshot(mining)) == 0);
    mining.artifact.tethered = true;
    assert(jawVertices(miningSnapshot(mining)) == 0);
    mining.artifact.tethered = false;
    mining.artifact.state = rocket::MiningArtifactState::Loose;
    assert(jawVertices(miningSnapshot(mining)) == 0);
}

void testSceneTransitionFadesEverySceneToBlack()
{
    const auto hasBlackOverlay = [](const ScenePacket& packet, float opacity) {
        return std::any_of(packet.instances.begin(), packet.instances.end(), [opacity](const PackedSceneInstance& packed) {
            const SceneInstance instance = rocket::unpackSceneInstance(packed);
            return !instance.textured
                && instance.shape == SceneInstanceShape::Rectangle
                && std::abs(instance.color.r) < 0.001F
                && std::abs(instance.color.g) < 0.001F
                && std::abs(instance.color.b) < 0.001F
                && std::abs(instance.color.a - opacity) < 0.02F;
        });
    };

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    RenderSnapshot snapshot;
    snapshot.screen = rocket::Screen::Mining;
    assert(!hasBlackOverlay(composer.compose(snapshot), 0.50F));

    snapshot.sceneFadeToBlack = 0.50;
    const ScenePacket& halfFade = composer.compose(snapshot);
    assert(hasBlackOverlay(halfFade, 0.50F));
    assert(!halfFade.draws.empty());
    const SceneDraw& finalDraw = halfFade.draws.back();
    assert(finalDraw.drawType == SceneDrawType::InstancedQuad);
    assert(finalDraw.fullViewport);
    const SceneInstance finalInstance = rocket::unpackSceneInstance(
        halfFade.instances[finalDraw.firstInstance + finalDraw.instanceCount - 1U]);
    assert(!finalInstance.textured);
    assert(finalInstance.shape == SceneInstanceShape::Rectangle);
    assert(std::abs(finalInstance.color.r) < 0.001F);
    assert(std::abs(finalInstance.color.g) < 0.001F);
    assert(std::abs(finalInstance.color.b) < 0.001F);
    assert(std::abs(finalInstance.color.a - 0.50F) < 0.02F);
}

Color miningTerrainMaterialColor(rocket::MiningCellMaterial material)
{
    rocket::MiningRunState mining;
    mining.terrain.width = 4;
    mining.terrain.height = 4;
    mining.terrain.cells.resize(16);
    mining.droneX = 0.5;
    mining.droneY = 0.5;
    mining.targetTipX = 0.5;
    mining.targetTipY = 1.5;
    rocket::MiningCell& cell = mining.terrain.cells[5];
    cell.material = material;
    cell.maxToughness = 1.0;
    cell.remainingToughness = 1.0;
    cell.revealed = true;

    RenderSnapshot snapshot = miningSnapshot(mining);
    snapshot.destinationTier = 3;
    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    const ScenePacket& packet = composer.compose(snapshot);
    assert(!packet.miningTerrainInstances.empty());
    return rocket::unpackSceneInstance(
        packet.miningTerrainInstances.back()).color;
}


void testMiningPickupHistoryDoesNotReplayAfterLevelUp()
{
    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    RenderSnapshot mining;
    mining.screen = rocket::Screen::Mining;
    mining.miningWidth = 4;
    mining.miningHeight = 4;
    mining.animationTime = 1.0;

    std::vector<rocket::MiningPickupEvent> pickupEvents;
    mining.miningPickupEvents = pickupEvents;
    (void)composer.compose(mining);
    assert(rocket::SceneComposerTestAccess::miningPickupBurstCount(composer) == 0U);

    pickupEvents.push_back({1, rocket::MiningPickupKind::CommonOre, 1, 1.5, 1.5});
    mining.miningPickupEvents = pickupEvents;
    mining.miningPickupEventSequence = 1;
    mining.animationTime = 1.1;
    (void)composer.compose(mining);
    assert(rocket::SceneComposerTestAccess::miningPickupBurstCount(composer) == 1U);

    RenderSnapshot levelUp = mining;
    levelUp.screen = rocket::Screen::SurfaceUpgrade;
    levelUp.animationTime = 2.0;
    (void)composer.compose(levelUp);
    assert(rocket::SceneComposerTestAccess::miningPickupBurstCount(composer) == 0U);

    mining.animationTime = 2.1;
    (void)composer.compose(mining);
    assert(rocket::SceneComposerTestAccess::miningPickupBurstCount(composer) == 0U);

    pickupEvents.push_back({2, rocket::MiningPickupKind::RareOre, 1, 2.5, 1.5});
    mining.miningPickupEvents = pickupEvents;
    mining.miningPickupEventSequence = 2;
    mining.animationTime = 2.2;
    (void)composer.compose(mining);
    assert(rocket::SceneComposerTestAccess::miningPickupBurstCount(composer) == 1U);
}

void testMiningRigSlerpsVerticalDuringExtraction()
{
    rocket::MiningRunState mining = miningState(20.0, 20.0);
    RenderSnapshot snapshot = miningSnapshot(mining);
    snapshot.miningHullDirX = 1.0;
    snapshot.miningHullDirY = 0.0;

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
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

    // The rig remains visible in the open bay after its travel finishes; the
    // closing shuttle, rather than a hard disappearance, is what hides it.
    snapshot.miningExtractionProgress = 0.62;
    composer.setPresentationTime(1.064);
    const SceneInstance docked = miningRigInstance(composer.compose(snapshot));
    const float dockedLength = std::hypot(docked.axisYx, docked.axisYy);
    assert(std::abs(docked.axisYx / dockedLength) < 0.02F);
    assert(docked.axisYy / dockedLength > 0.99F);
    assert(docked.color.a > 0.10F);
}

void testMiningDepartureFlameTracksShip()
{
    rocket::MiningRunState mining = miningState(20.0, 20.0);
    RenderSnapshot snapshot = miningSnapshot(mining);
    snapshot.miningExtractionActive = true;
    snapshot.animationTime = 1.0;

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    composer.setTextureReady(TextureId::RocketOpen, true);
    composer.setTextureReady(TextureId::Thrust, true);

    snapshot.miningExtractionProgress = 0.76;
    const ScenePacket earlyPacket = composer.compose(snapshot);
    const SceneInstance earlyShip = spriteInstance(
        earlyPacket, TextureId::RocketOpen, 0.0F, 0.0F, 1.0F, 1.0F);
    const SceneInstance earlyFlame = spriteInstance(
        earlyPacket, TextureId::Thrust, 0.0F, 0.27F, 1.0F / 6.0F, 1.0F);

    snapshot.miningExtractionProgress = 0.94;
    const ScenePacket latePacket = composer.compose(snapshot);
    const SceneInstance lateShip = spriteInstance(
        latePacket, TextureId::RocketOpen, 0.0F, 0.0F, 1.0F, 1.0F);
    const SceneInstance lateFlame = spriteInstance(
        latePacket, TextureId::Thrust, 0.0F, 0.27F, 1.0F / 6.0F, 1.0F);

    const float shipLift = lateShip.centerY - earlyShip.centerY;
    const float flameLift = lateFlame.centerY - earlyFlame.centerY;
    const auto visibleFlameHeadY = [](const SceneInstance& flame) {
        return flame.centerY + std::abs(flame.axisYy);
    };
    const auto shipNozzleY = [](const SceneInstance& ship) {
        return ship.centerY - std::abs(ship.axisYy) * 2.0F * 0.465F;
    };
    assert(shipLift > 0.10F);
    assert(std::abs(visibleFlameHeadY(earlyFlame) - shipNozzleY(earlyShip)) < 0.001F);
    assert(std::abs(visibleFlameHeadY(lateFlame) - shipNozzleY(lateShip)) < 0.001F);
    assert(std::abs(
        (visibleFlameHeadY(lateFlame) - visibleFlameHeadY(earlyFlame)) -
        shipLift) < 0.001F);
    assert(flameLift > 0.0F);
}

void testCuttingFeedbackComesFromEveryActiveHead()
{
    auto mining = miningState(20.0, 20.0);
    auto snapshot = miningSnapshot(mining);
    snapshot.miningSideCutterReach = 1.5;
    snapshot.miningDrilling = true;
    snapshot.miningTargetDrillable = true;
    snapshot.miningContactIntensity = 0.8;
    snapshot.miningDrillContacts = {{17.0, 22.0}, {23.0, 22.0}};
    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    const auto sparkCounts = [&](const ScenePacket& packet) {
        std::array<int, 2> counts {};
        for (const auto& packed : packet.instances) {
            const auto instance = rocket::unpackSceneInstance(packed);
            if (instance.textured || instance.shape != SceneInstanceShape::Rectangle ||
                instance.color.r < 0.70F || instance.color.g < 0.45F || instance.color.b > 0.40F) continue;
            for (std::size_t i = 0; i < counts.size(); ++i) {
                const auto& contact = snapshot.miningDrillContacts[i];
                const float x = packet.surfaceCamera.left + contact[0] * packet.surfaceCamera.cellWidth;
                const float y = packet.surfaceCamera.top - contact[1] * packet.surfaceCamera.cellHeight;
                if (std::hypot((instance.centerX - x) / packet.surfaceCamera.cellWidth,
                        (instance.centerY - y) / packet.surfaceCamera.cellHeight) < 2.0F) ++counts[i];
            }
        }
        return counts;
    };
    const auto cutting = sparkCounts(composer.compose(snapshot));
    assert(cutting[0] >= 20 && cutting[1] >= 20);
    snapshot.miningDrilling = false;
    snapshot.miningContactIntensity = 0.0;
    const auto idle = sparkCounts(composer.compose(snapshot));
    assert(idle[0] == 0 && idle[1] == 0);
}

void testBlockedDrillHasFeedbackWithoutCuttingParticles()
{
    auto mining = miningState(20.0, 20.0);
    auto snapshot = miningSnapshot(mining);
    snapshot.miningDrilling = snapshot.miningTargetDrillable = false;
    snapshot.miningContactIntensity = 0;
    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    const auto idleCount = composer.compose(snapshot).instances.size();
    snapshot.miningDrillFeedback = {rocket::MiningDrillContactKind::Bedrock, 20.0, 22.0};
    const auto& blocked = composer.compose(snapshot);
    assertValidDrawRanges(blocked);
    assert(blocked.instances.size() > idleCount);
    snapshot.miningDrillFeedback = {};
    assert(composer.compose(snapshot).instances.size() == idleCount);
}

void testReturnRingIsCenteredOnTheShip()
{
    auto mining = miningState(20.0, 20.0);
    auto snapshot = miningSnapshot(mining);
    snapshot.miningShipPresent = true;
    snapshot.miningAtReturnZone = false;
    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    composer.setTextureReady(TextureId::RocketClosed, true);
    const auto& packet = composer.compose(snapshot);
    const auto ship = spriteInstance(packet, TextureId::RocketClosed, 0, 0, 1, 1);
    bool found = false;
    for (const auto& packed : packet.instances) {
        const auto glow = rocket::unpackSceneInstance(packed);
        if (glow.shape != SceneInstanceShape::RadialGlow ||
            std::abs(glow.color.a - 0.026F) > 1.0F / 255.0F ||
            std::abs(glow.color.r - 0.28F) > 1.0F / 255.0F) continue;
        assert(std::abs(glow.centerX - ship.centerX) < 0.0001F);
        assert(std::abs(glow.centerY - ship.centerY) < 0.0001F);
        found = true;
    }
    assert(found);
    assert(rocket::tuning::mining::returnZoneRadiusCells == 6.0);
}

void testMiningRigStaysVisibleAndTracksHeading()
{
    rocket::MiningRunState mining = miningState(20.0, 20.0);
    RenderSnapshot snapshot = miningSnapshot(mining);
    snapshot.miningHullDirX = 1.0;
    snapshot.miningHullDirY = 0.0;
    snapshot.miningMoveX = 1.0;

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    composer.setTextureReady(TextureId::MiningDrone, true);
    composer.setTextureReady(TextureId::DrillBit, true);

    composer.setPresentationTime(1.0);
    const ScenePacket& firstPacket = composer.compose(snapshot);
    const SceneInstance first = miningRigInstance(firstPacket);
    const SceneInstance firstDrill = miningDrillBitInstance(firstPacket);
    const auto pointer = firstPacket.flightPointer;
    assert(pointer.active);
    assert(std::abs(pointer.shipX - (firstPacket.transform.pixelCenterX + first.centerX * firstPacket.transform.worldUnitX)) < 1.0);
    assert(std::abs(pointer.shipY - (800 - firstPacket.transform.pixelCenterY - first.centerY * firstPacket.transform.worldUnitY)) < 1.0);
    assert(!pointer.angleTo(pointer.shipX, pointer.shipY));
    assert(std::abs(*pointer.angleTo(pointer.shipX + 40, pointer.shipY)) < 0.001);
    assert(std::isfinite(first.centerX) && std::isfinite(first.centerY));
    assert(first.textured);
    assert(first.shape == SceneInstanceShape::Rectangle);
    assert(first.color.a > 0.99F);
    assert(std::hypot(first.axisXx, first.axisXy) > 0.01F);
    assert(std::hypot(first.axisYx, first.axisYy) > 0.01F);
    const float sceneAspect = static_cast<float>(firstPacket.logicalSceneClip.width)
        / static_cast<float>(std::max(1, firstPacket.logicalSceneClip.height));
    const float cellW = firstPacket.surfaceCamera.cellWidth;
    const float cellH = firstPacket.surfaceCamera.cellHeight;
    assert(std::abs(first.centerX - (firstPacket.surfaceCamera.left + static_cast<float>(snapshot.miningDroneX) * cellW)) < 0.0005F);
    assert(std::abs(first.centerY - (firstPacket.surfaceCamera.top - static_cast<float>(snapshot.miningDroneY) * cellH)) < 0.0005F);
    assert(first.axisYx < -0.01F);
    assert(std::abs(first.axisYy) < 0.001F);
    const float firstLength = std::hypot(first.axisYx, first.axisYy);
    const float firstDrillLength = std::hypot(firstDrill.axisYx, firstDrill.axisYy);
    assert((first.axisYx * firstDrill.axisYx + first.axisYy * firstDrill.axisYy)
        / (firstLength * firstDrillLength) > 0.999F);
    assertMiningDrillMounted(first, firstDrill);

    const rocket::SceneAtlasUvRect drillUv = rocket::mapSceneAtlasUvRect(
        TextureId::DrillBit, 0.0F, 0.0F, 1.0F / 6.0F, 1.0F);
    // The side sprites must use cell dimensions at every rank and heading.
    // A pixel-sized minimum applied in scene units made them span the screen.
    for (int viewportWidth : {720, 1280}) {
        for (int rank = 1; rank <= 3; ++rank) {
            for (int heading = 0; heading < 8; ++heading) {
                RenderSnapshot upgraded = snapshot;
                const double angle = heading * 3.141592653589793 / 4.0;
                upgraded.miningHullDirX = std::cos(angle);
                upgraded.miningHullDirY = std::sin(angle);
                upgraded.miningDrillHeadWidthScale = 1.0 + rank * 0.25;
                upgraded.miningSideCutterReach = rank * 0.5;
                SceneComposer upgradedComposer;
                upgradedComposer.setViewport({viewportWidth, 800, viewportWidth, 800, 1.0F});
                upgradedComposer.setTextureReady(TextureId::DrillBit, true);
                const ScenePacket& upgradedPacket = upgradedComposer.compose(upgraded);
                std::vector<SceneInstance> parts;
                for (const SceneDraw& draw : upgradedPacket.draws) {
                    if (draw.drawType != SceneDrawType::InstancedQuad || draw.atlasPage != drillUv.page) continue;
                    for (std::size_t index = 0; index < draw.instanceCount; ++index) {
                        const SceneInstance instance = rocket::unpackSceneInstance(
                            upgradedPacket.instances[draw.firstInstance + index]);
                        if (instance.textured && std::abs(instance.u0-drillUv.u0) < 0.00004F &&
                            std::abs(instance.v0-drillUv.v0) < 0.00004F &&
                            std::abs(instance.u1-drillUv.u1) < 0.00004F &&
                            std::abs(instance.v1-drillUv.v1) < 0.00004F) parts.push_back(instance);
                    }
                }
                assert(parts.size() == 3);
                const float cellSize = std::min(upgradedPacket.surfaceCamera.cellWidth,
                    upgradedPacket.surfaceCamera.cellHeight);
                const auto& head = parts[0];
                const float headLength = std::hypot(head.axisYx, head.axisYy);
                for (std::size_t i = 1; i < parts.size(); ++i) {
                    const auto& cutter = parts[i];
                    const float widthCells = 2.0F * std::hypot(cutter.axisXx, cutter.axisXy) / cellSize;
                    assert(std::abs(widthCells - upgraded.miningSideCutterReach) < 0.01F);
                    const float cutterLength = std::hypot(cutter.axisYx, cutter.axisYy);
                    assert(cutterLength > 0.0F && cutterLength < headLength);
                    assert((head.axisYx * cutter.axisYx + head.axisYy * cutter.axisYy)
                        / (headLength * cutterLength) > 0.999F);
                    assert(std::hypot(cutter.centerX - head.centerX, cutter.centerY - head.centerY)
                        < headLength * 2.0F + cellSize * upgraded.miningSideCutterReach);
                }
            }
        }
    }

    // A large presentation-time step snaps to the new heading, avoiding the
    // intentional short steering Slerp while checking the opposite direction.
    snapshot.miningHullDirX = -1.0;
    snapshot.miningMoveX = -1.0;
    composer.setPresentationTime(1.5);
    const ScenePacket& reversedPacket = composer.compose(snapshot);
    const SceneInstance reversed = miningRigInstance(reversedPacket);
    const SceneInstance reversedDrill = miningDrillBitInstance(reversedPacket);
    assert(std::isfinite(reversed.centerX) && std::isfinite(reversed.centerY));
    assert(reversed.textured);
    assert(reversed.shape == SceneInstanceShape::Rectangle);
    assert(reversed.color.a > 0.99F);
    assert(std::hypot(reversed.axisXx, reversed.axisXy) > 0.01F);
    assert(std::hypot(reversed.axisYx, reversed.axisYy) > 0.01F);
    assert(reversed.axisYx > 0.01F);
    assert(std::abs(reversed.axisYy) < 0.001F);
    const float reversedLength = std::hypot(reversed.axisYx, reversed.axisYy);
    const float reversedDrillLength = std::hypot(reversedDrill.axisYx, reversedDrill.axisYy);
    assert(std::abs(reversedLength - firstLength) < 0.001F);
    assert(std::abs(
        std::hypot(reversed.axisXx, reversed.axisXy)
        - std::hypot(first.axisXx, first.axisXy)) < 0.001F);
    assert((reversed.axisYx * reversedDrill.axisYx + reversed.axisYy * reversedDrill.axisYy)
        / (reversedLength * reversedDrillLength) > 0.999F);
    assertMiningDrillMounted(reversed, reversedDrill);

    // Diagonal steering must rotate the body and drill together rather than
    // leaving the body on either cardinal orientation.
    snapshot.miningHullDirX = 1.0;
    snapshot.miningHullDirY = 1.0;
    snapshot.miningMoveX = 1.0;
    snapshot.miningMoveY = 1.0;
    composer.setPresentationTime(2.0);
    const ScenePacket& diagonalPacket = composer.compose(snapshot);
    const SceneInstance diagonal = miningRigInstance(diagonalPacket);
    const SceneInstance diagonalDrill = miningDrillBitInstance(diagonalPacket);
    assert(std::abs(diagonal.axisYx) > 0.01F);
    assert(std::abs(diagonal.axisYy) > 0.01F);
    const float diagonalLength = std::hypot(diagonal.axisYx, diagonal.axisYy);
    const float diagonalDrillLength = std::hypot(diagonalDrill.axisYx, diagonalDrill.axisYy);
    assert((diagonal.axisYx * diagonalDrill.axisYx + diagonal.axisYy * diagonalDrill.axisYy)
        / (diagonalLength * diagonalDrillLength) > 0.999F);
    assertMiningDrillMounted(diagonal, diagonalDrill);
}

void testMiningCollisionIndicatorMarksTheContactedEdge()
{
    rocket::MiningRunState mining = miningState(20.0, 20.0);
    RenderSnapshot snapshot = miningSnapshot(mining);
    snapshot.miningContactIndicatorSeconds = rocket::tuning::mining::contactIndicatorSeconds;
    snapshot.miningContactIndicatorDirX = 0.0;
    snapshot.miningContactIndicatorDirY = 1.0;

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    composer.setTextureReady(TextureId::MiningDrone, true);
    const ScenePacket& packet = composer.compose(snapshot);
    assertValidDrawRanges(packet);

    const bool foundBumpBarrier = std::any_of(
        packet.instances.begin(),
        packet.instances.end(),
        [](const PackedSceneInstance& packed) {
            const SceneInstance instance = rocket::unpackSceneInstance(packed);
            return !instance.textured && instance.shape == SceneInstanceShape::Rectangle &&
                std::abs(instance.color.r - 1.0F) < 0.01F &&
                std::abs(instance.color.g - 0.22F) < 0.01F &&
                std::abs(instance.color.b - 0.14F) < 0.01F &&
                instance.color.a > 0.55F;
        });
    assert(foundBumpBarrier);
}

void testMiningSurveyPulseRechargeRingPersistsWhenReady()
{
    const auto cyanArcLength = [](const ScenePacket& packet) {
        float result = 0.0F;
        for (const PackedSceneInstance& packed : packet.instances) {
            const SceneInstance instance = rocket::unpackSceneInstance(packed);
            if (instance.shape == SceneInstanceShape::Rectangle
                && std::abs(instance.color.r - 0.18F) < 0.01F
                && std::abs(instance.color.g - 0.96F) < 0.01F
                && std::abs(instance.color.b - 1.0F) < 0.01F
                && std::abs(instance.color.a - 0.96F) < 0.01F) {
                result += 2.0F * std::hypot(instance.axisYx, instance.axisYy);
            }
        }
        return result;
    };
    const auto cyanTrackCount = [](const ScenePacket& packet) {
        return static_cast<int>(std::count_if(
            packet.instances.begin(),
            packet.instances.end(),
            [](const PackedSceneInstance& packed) {
                const SceneInstance instance = rocket::unpackSceneInstance(packed);
                return instance.shape == SceneInstanceShape::Rectangle
                    && std::abs(instance.color.r - 0.025F) < 0.01F
                    && std::abs(instance.color.g - 0.14F) < 0.01F
                    && std::abs(instance.color.b - 0.18F) < 0.01F
                    && std::abs(instance.color.a - 0.72F) < 0.01F;
            }));
    };

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    RenderSnapshot snapshot;
    snapshot.screen = rocket::Screen::Mining;
    snapshot.miningWidth = 16;
    snapshot.miningHeight = 12;
    snapshot.miningDroneX = 8.0;
    snapshot.miningDroneY = 6.0;
    snapshot.miningRigPresent = true;
    snapshot.miningScannerRechargeProgress = 0.0;
    snapshot.miningScannerPulse = 0.64;
    const ScenePacket pulsing = composer.compose(snapshot);
    assert(cyanTrackCount(pulsing) == 0 && cyanArcLength(pulsing) < 0.001F);

    snapshot.miningScannerPulse = 0.0;
    const ScenePacket empty = composer.compose(snapshot);
    assert(cyanTrackCount(empty) > 0);
    assert(cyanArcLength(empty) < 0.001F);

    snapshot.miningScannerRechargeProgress = 0.5;
    const ScenePacket partial = composer.compose(snapshot);
    const float partialCyanLength = cyanArcLength(partial);
    assert(partialCyanLength > 0.0F);

    snapshot.miningScannerRechargeProgress = 1.0;
    const ScenePacket ready = composer.compose(snapshot);
    assert(cyanArcLength(ready) > partialCyanLength * 1.9F);

    snapshot.miningExtractionActive = true;
    const ScenePacket extracting = composer.compose(snapshot);
    assert(cyanTrackCount(extracting) == 0 && cyanArcLength(extracting) < 0.001F);
}

void testMiningControllerReticleFollowsScannerRing()
{
    for (const auto size : {std::pair{1280, 800}, std::pair{1920, 1080}}) {
        SceneComposer composer;
        composer.setViewport({size.first, size.second, size.first, size.second, 1.0F});
        RenderSnapshot snapshot;
        snapshot.screen = rocket::Screen::Mining;
        snapshot.miningWidth = 16; snapshot.miningHeight = 12;
        snapshot.miningDroneX = snapshot.miningOperatorX = 8;
        snapshot.miningDroneY = snapshot.miningOperatorY = 6;
        snapshot.miningRigPresent = snapshot.miningOperatorPresent = true;
        snapshot.miningControllerAimVisible = true;
        snapshot.miningScannerRechargeProgress = 1.0;
        for (bool eva : {false, true}) for (double pulse : {0.0, 0.5}) {
            snapshot.miningOperatorActive = eva;
            snapshot.miningScannerPulse = pulse;
            snapshot.miningOperatorFirePulse = pulse;
            for (int direction = 0; direction < 8; ++direction) {
                const double angle = direction * 3.141592653589793 / 4;
                snapshot.miningControllerAimX = snapshot.miningOperatorAimX = std::cos(angle);
                snapshot.miningControllerAimY = snapshot.miningOperatorAimY = std::sin(angle);
                const ScenePacket packet = composer.compose(snapshot);
                const float cell = std::min(packet.surfaceCamera.cellWidth, packet.surfaceCamera.cellHeight);
                const float radius = eva ? cell * 2.45F * .68F
                    : packet.surfaceCamera.cellWidth * snapshot.miningOreAttractionRadius;
                const float expectedX = packet.surfaceCamera.left + 8 * packet.surfaceCamera.cellWidth + radius * std::cos(angle);
                const float expectedY = packet.surfaceCamera.top - 6 * packet.surfaceCamera.cellHeight - radius * std::sin(angle);
                float centerX = 0, centerY = 0, extent = 0;
                int segments = 0;
                for (const auto& packed : packet.instances) {
                    const auto instance = rocket::unpackSceneInstance(packed);
                    if (std::abs(instance.color.r - 1.0F) > .001F ||
                        std::abs(instance.color.g - 209.0F / 255.0F) > .001F ||
                        std::abs(instance.color.b - 71.0F / 255.0F) > .001F) continue;
                    ++segments;
                    const float stroke = 2 * std::hypot(instance.axisXx * packet.transform.worldUnitX,
                        instance.axisXy * packet.transform.worldUnitY);
                    assert(std::abs(stroke - (segments <= 24 ? .75F : 1.2F)) < .01F);
                    centerX += instance.centerX; centerY += instance.centerY;
                    extent = std::max(extent, std::max(std::abs(instance.centerX - expectedX),
                        std::abs(instance.centerY - expectedY)));
                    assert(std::abs(instance.color.a - (.70F + (eva ? pulse * .25F : 0))) < .002F);
                }
                assert(segments == 28); // One 24-segment circle and four crosshair arms.
                assert(std::abs(centerX / segments - expectedX) < .001F);
                assert(std::abs(centerY / segments - expectedY) < .001F);
                assert(std::abs(extent - cell * (.18F + .34F / 2) * .75F) < .001F);
            }
        }
    }
}

void testMiningSurveyPulseWaveReachesItsRealRadiusThenFades()
{
    struct WaveMetrics {
        float radiusCells = 0.0F;
        float alpha = 0.0F;
    };
    const auto waveMetrics = [](const ScenePacket& packet) {
        const float cellH = packet.surfaceCamera.cellHeight;
        const float sceneAspect = static_cast<float>(packet.logicalSceneClip.width)
            / static_cast<float>(std::max(1, packet.logicalSceneClip.height));
        const float cellW = packet.surfaceCamera.cellWidth;
        const float originX = packet.surfaceCamera.left + 8.0F * cellW;
        const float originY = packet.surfaceCamera.top - 6.0F * cellH;
        WaveMetrics result;
        for (const PackedSceneInstance& packed : packet.instances) {
            const SceneInstance instance = rocket::unpackSceneInstance(packed);
            if (instance.shape != SceneInstanceShape::Rectangle
                || std::abs(instance.color.r - 0.18F) > 0.01F
                || std::abs(instance.color.g - 0.96F) > 0.01F
                || std::abs(instance.color.b - 1.0F) > 0.01F
                || instance.color.a < 0.005F) {
                continue;
            }
            const float dx = (instance.centerX - originX) / cellW;
            const float dy = (instance.centerY - originY) / cellH;
            result.radiusCells = std::max(result.radiusCells, std::hypot(dx, dy));
            result.alpha = std::max(result.alpha, instance.color.a);
        }
        return result;
    };

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    RenderSnapshot snapshot;
    snapshot.screen = rocket::Screen::Mining;
    snapshot.miningWidth = 16;
    snapshot.miningHeight = 12;
    snapshot.miningDroneX = 8.0;
    snapshot.miningDroneY = 6.0;
    snapshot.miningScannerRadius = 5.0;

    snapshot.miningScannerPulse = 0.64;
    const WaveMetrics launch = waveMetrics(composer.compose(snapshot));
    snapshot.miningScannerPulse = 0.30;
    const WaveMetrics maximum = waveMetrics(composer.compose(snapshot));
    snapshot.miningScannerPulse = 0.08;
    const WaveMetrics fading = waveMetrics(composer.compose(snapshot));

    assert(launch.radiusCells > 0.0F);
    assert(maximum.radiusCells > launch.radiusCells * 4.0F);
    assert(maximum.radiusCells > 4.7F && maximum.radiusCells <= 5.1F);
    assert(fading.radiusCells > 4.7F && fading.radiusCells <= 5.1F);
    assert(fading.alpha > 0.0F && fading.alpha < maximum.alpha * 0.30F);
}

void testMiningSurveyPulseProgressivelyRevealsNewTerrain()
{
    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    RenderSnapshot snapshot;
    snapshot.screen = rocket::Screen::Mining;
    snapshot.miningWidth = 16;
    snapshot.miningHeight = 12;
    snapshot.miningDroneX = 2.5;
    snapshot.miningDroneY = 6.5;
    snapshot.miningScannerRadius = 5.5;
    std::vector<rocket::MiningCell> cells(16U * 12U);
    snapshot.miningCells = cells;
    const std::size_t alreadyIndex = 6U * 16U + 2U;
    const std::size_t nearIndex = 6U * 16U + 3U;
    const std::size_t wavefrontIndex = 6U * 16U + 5U;
    const std::size_t farIndex = 6U * 16U + 7U;
    const std::size_t surveyIndex = 6U * 16U + 12U;
    for (const std::size_t index : {alreadyIndex, nearIndex, wavefrontIndex, farIndex, surveyIndex}) {
        cells[index].material = rocket::MiningCellMaterial::CommonOre;
        cells[index].maxToughness = 1.0;
        cells[index].remainingToughness = 1.0;
    }
    cells[alreadyIndex].revealed = true;
    std::vector<rocket::MiningMiniDroneAgent> miniDrones(1);
    miniDrones[0].role = rocket::MiniDroneRole::Survey;
    miniDrones[0].x = 11.5;
    miniDrones[0].y = 6.5;
    snapshot.miningMiniDrones = miniDrones;
    composer.compose(snapshot);

    const auto materialAlpha = [](const ScenePacket& packet, int cellX, int cellY) {
        const float cellH = packet.surfaceCamera.cellHeight;
        const float sceneAspect = static_cast<float>(packet.logicalSceneClip.width)
            / static_cast<float>(std::max(1, packet.logicalSceneClip.height));
        const float cellW = packet.surfaceCamera.cellWidth;
        const float centerX = packet.surfaceCamera.left + static_cast<float>(cellX) * cellW + cellW * 0.5F;
        const float centerY = packet.surfaceCamera.top - static_cast<float>(cellY) * cellH - cellH * 0.5F;
        float alpha = 0.0F;
        for (const PackedSceneInstance& packed : packet.miningTerrainInstances) {
                const SceneInstance instance = rocket::unpackSceneInstance(packed);
                const float width = 2.0F * std::hypot(instance.axisXx, instance.axisXy);
                if (instance.shape == SceneInstanceShape::Rectangle
                    && std::abs(instance.centerX - centerX) < 0.001F
                    && std::abs(instance.centerY - centerY) < 0.001F
                    && std::abs(width - cellW * 0.96F) < 0.002F) {
                    alpha = std::max(alpha, instance.color.a);
                }
        }
        return alpha;
    };

    cells[nearIndex].revealed = true;
    cells[wavefrontIndex].revealed = true;
    cells[farIndex].revealed = true;
    cells[surveyIndex].revealed = true;
    snapshot.miningScannerPulse = 0.64;
    const ScenePacket start = composer.compose(snapshot);
    assert(materialAlpha(start, 2, 6) > 0.02F);
    assert(materialAlpha(start, 3, 6) < 0.02F);
    assert(materialAlpha(start, 7, 6) < 0.02F);
    assert(materialAlpha(start, 12, 6) < 0.02F);

    snapshot.miningScannerPulse = 0.47;
    const ScenePacket halfway = composer.compose(snapshot);
    const float nearAlpha = materialAlpha(halfway, 3, 6);
    const float wavefrontAlpha = materialAlpha(halfway, 5, 6);
    assert(nearAlpha > 0.20F);
    assert(wavefrontAlpha > 0.01F && wavefrontAlpha < nearAlpha * 0.5F);
    assert(materialAlpha(halfway, 7, 6) < 0.02F);
    assert(materialAlpha(halfway, 12, 6) > 0.20F);

    snapshot.miningScannerPulse = 0.30;
    const ScenePacket expanded = composer.compose(snapshot);
    assert(materialAlpha(expanded, 3, 6) > 0.20F);
    assert(materialAlpha(expanded, 7, 6) > 0.20F);

    snapshot.miningScannerPulse = 0.10;
    const ScenePacket fading = composer.compose(snapshot);
    assert(materialAlpha(fading, 3, 6) > 0.20F);
    assert(materialAlpha(fading, 7, 6) > 0.20F);
}

void testMiningRigDrillStaysMountedThroughRecoilAndExtension()
{
    rocket::MiningRunState mining = miningState(20.0, 20.0);
    RenderSnapshot snapshot = miningSnapshot(mining);
    snapshot.miningHullDirX = 0.0;
    snapshot.miningHullDirY = 1.0;
    snapshot.miningMoveY = 1.0;
    snapshot.miningTargetDrillable = false;

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    composer.setTextureReady(TextureId::MiningDrone, true);
    composer.setTextureReady(TextureId::DrillBit, true);

    composer.setPresentationTime(1.0);
    const ScenePacket& restingPacket = composer.compose(snapshot);
    const SceneInstance restingRig = miningRigInstance(restingPacket);
    const SceneInstance restingDrill = miningDrillBitInstance(restingPacket);
    const ScenePoint restingCollar = miningDrillCollar(restingDrill);
    assertMiningDrillMounted(restingRig, restingDrill);

    snapshot.miningBounce = 1.0;
    snapshot.miningRecoilX = -1.0;
    composer.setPresentationTime(1.016);
    const ScenePacket& recoilPacket = composer.compose(snapshot);
    const SceneInstance recoiledRig = miningRigInstance(recoilPacket);
    const SceneInstance recoiledDrill = miningDrillBitInstance(recoilPacket);
    const ScenePoint recoiledCollar = miningDrillCollar(recoiledDrill);
    const float rigDeltaX = recoiledRig.centerX - restingRig.centerX;
    const float rigDeltaY = recoiledRig.centerY - restingRig.centerY;
    assert(std::hypot(rigDeltaX, rigDeltaY) < 0.001F); // Effects cannot displace the physical Rig.
    assert(std::abs((recoiledCollar.x - restingCollar.x) - rigDeltaX) < 0.0005F);
    assert(std::abs((recoiledCollar.y - restingCollar.y) - rigDeltaY) < 0.0005F);
    assertMiningDrillMounted(recoiledRig, recoiledDrill);

    snapshot.miningBounce = 0.0;
    snapshot.miningRecoilX = 0.0;
    composer.setPresentationTime(2.0);
    const ScenePacket& shortPacket = composer.compose(snapshot);
    const SceneInstance shortRig = miningRigInstance(shortPacket);
    const SceneInstance shortDrill = miningDrillBitInstance(shortPacket);
    const ScenePoint shortCollar = miningDrillCollar(shortDrill);

    snapshot.miningTargetDrillable = true;
    snapshot.miningTargetX = 1;
    snapshot.miningTargetY = 32;
    composer.setPresentationTime(2.016);
    const ScenePacket& extendedPacket = composer.compose(snapshot);
    const SceneInstance extendedRig = miningRigInstance(extendedPacket);
    const SceneInstance extendedDrill = miningDrillBitInstance(extendedPacket);
    const ScenePoint extendedCollar = miningDrillCollar(extendedDrill);
    assert(std::abs(std::hypot(extendedDrill.axisYx, extendedDrill.axisYy)
        - std::hypot(shortDrill.axisYx, shortDrill.axisYy)) < 0.001F);
    assert(std::abs(extendedCollar.x - shortCollar.x) < 0.0005F);
    assert(std::abs(extendedCollar.y - shortCollar.y) < 0.0005F);
    assertMiningDrillMounted(shortRig, shortDrill);
    assertMiningDrillMounted(extendedRig, extendedDrill);
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

void testHazardDroneTransitShimmerAndAssistantBeams()
{
    rocket::MiningRunState mining;
    mining.terrain.width = 5;
    mining.terrain.height = 3;
    mining.terrain.cells.resize(15);
    for (rocket::MiningCell& cell : mining.terrain.cells) {
        cell.material = rocket::MiningCellMaterial::Empty;
        cell.revealed = true;
    }
    rocket::MiningCell& solid = mining.terrain.cells[6];
    solid.material = rocket::MiningCellMaterial::HardRock;
    solid.maxToughness = 10.0;
    solid.remainingToughness = 10.0;

    rocket::MiningCell& target = mining.terrain.cells[8];
    target.material = rocket::MiningCellMaterial::HazardPocket;
    target.maxToughness = 10.0;
    target.remainingToughness = 10.0;
    target.hazard = true;
    target.hazardAffinity = rocket::MiningElementalAffinity::Thermal;

    mining.droneX = 2.5;
    mining.droneY = 2.5;
    mining.targetTipX = 2.5;
    mining.targetTipY = 1.5;
    mining.returnZoneX = 0.5;
    mining.returnZoneY = 0.5;

    rocket::MiningMiniDroneAgent hazard;
    hazard.role = rocket::MiniDroneRole::Hazard;
    hazard.roleIndex = 0;
    hazard.upgradeLevel = 1;
    hazard.behavior = rocket::MiningMiniDroneBehavior::Traveling;
    hazard.x = 1.5;
    hazard.y = 1.5;
    hazard.velocityX = 1.0;
    hazard.targetCellX = 3;
    hazard.targetCellY = 1;
    mining.miniDrones.push_back(hazard);

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    composer.setPresentationTime(1.0);
    composer.setTextureReady(TextureId::MiniDroneHazard, true);

    RenderSnapshot snapshot = miningSnapshot(mining);
    const ScenePacket& solidTransit = composer.compose(snapshot);
    assertValidDrawRanges(solidTransit);
    const std::size_t solidTransitGlows =
        countInstanceShape(solidTransit, SceneInstanceShape::RadialGlow);

    mining.miniDrones[0].x = 2.5;
    snapshot = miningSnapshot(mining);
    const ScenePacket& openTransit = composer.compose(snapshot);
    assertValidDrawRanges(openTransit);
    const std::size_t openTransitGlows =
        countInstanceShape(openTransit, SceneInstanceShape::RadialGlow);
    assert(solidTransitGlows == openTransitGlows + 1U);

    mining.miniDrones[0].behavior = rocket::MiningMiniDroneBehavior::Working;
    mining.miniDrones[0].x = 3.5;
    mining.miniDrones[0].y = 0.8;
    snapshot = miningSnapshot(mining);
    const ScenePacket& singleWorker = composer.compose(snapshot);
    assertValidDrawRanges(singleWorker);
    const std::size_t singleWorkerGlows =
        countInstanceShape(singleWorker, SceneInstanceShape::RadialGlow);

    rocket::MiningMiniDroneAgent assistant = mining.miniDrones[0];
    assistant.roleIndex = 1;
    assistant.x = 4.2;
    assistant.y = 1.5;
    mining.miniDrones.push_back(assistant);
    snapshot = miningSnapshot(mining);
    const ScenePacket& assisted = composer.compose(snapshot);
    assertValidDrawRanges(assisted);
    const std::size_t assistedGlows =
        countInstanceShape(assisted, SceneInstanceShape::RadialGlow);
    assert(assistedGlows == singleWorkerGlows + 1U);
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
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
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
    // Only base terrain remains in the persistent stream; the opaque black
    // backdrop was replaced by the separately rendered sky and tunnel wall.
    assert(miningTerrainDraws == 1U);

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

void testMiningTerrainUsesDestinationTilesAndMaterialFrames()
{
    constexpr int tileFrameCount = 19;
    const std::array<std::pair<int, TextureId>, 8> destinations {{
        {1, TextureId::MiningTilesMoon},
        {2, TextureId::MiningTilesMars},
        {3, TextureId::MiningTilesIo},
        {4, TextureId::MiningTilesSaturn},
        {5, TextureId::MiningTilesUranus},
        {6, TextureId::MiningTilesNeptune},
        {7, TextureId::MiningTilesKhepriPrime},
        {8, TextureId::MiningTilesRiftBelt},
    }};

    rocket::MiningRunState mining;
    mining.terrain.width = 5;
    mining.terrain.height = 3;
    mining.terrain.cells.resize(15);
    mining.droneX = 1.0;
    mining.droneY = 1.0;
    mining.targetTipX = 1.0;
    mining.targetTipY = 2.0;
    mining.returnZoneX = 0.0;
    mining.returnZoneY = 0.0;

    const auto reveal = [&](int index, rocket::MiningCellMaterial material) -> rocket::MiningCell& {
        rocket::MiningCell& cell = mining.terrain.cells[static_cast<std::size_t>(index)];
        cell.material = material;
        cell.maxToughness = 10.0;
        cell.remainingToughness = 10.0;
        cell.revealed = true;
        return cell;
    };
    reveal(1, rocket::MiningCellMaterial::Regolith);
    reveal(2, rocket::MiningCellMaterial::HardRock);
    rocket::MiningCell& radiation = reveal(3, rocket::MiningCellMaterial::HazardPocket);
    radiation.hazard = true;
    radiation.hazardAffinity = rocket::MiningElementalAffinity::Radiation;
    reveal(4, rocket::MiningCellMaterial::CommonOre);

    const auto matchesFrame = [](const SceneInstance& instance, TextureId texture, int frame) {
        const rocket::SceneAtlasUvRect expected = rocket::mapSceneAtlasUvRect(
            texture,
            static_cast<float>(frame) / static_cast<float>(tileFrameCount),
            0.0F,
            static_cast<float>(frame + 1) / static_cast<float>(tileFrameCount),
            1.0F);
        constexpr float tolerance = 0.0006F;
        return expected.valid
            && std::abs(instance.u0 - expected.u0) < tolerance
            && std::abs(instance.v0 - expected.v0) < tolerance
            && std::abs(instance.u1 - expected.u1) < tolerance
            && std::abs(instance.v1 - expected.v1) < tolerance;
    };

    for (const auto& [tier, texture] : destinations) {
        RenderSnapshot snapshot = miningSnapshot(mining);
        snapshot.destinationTier = tier;
        SceneComposer composer;
        composer.setViewport({1280, 800, 1280, 800, 1.0F});
        composer.setTextureReady(texture, true);
        const ScenePacket& packet = composer.compose(snapshot);
        assertValidDrawRanges(packet);

        const SceneDraw* terrainDraw = nullptr;
        for (const SceneDraw& draw : packet.draws) {
            if (draw.instanceStream == SceneInstanceStream::MiningTerrain
                && draw.texture == texture) {
                assert(terrainDraw == nullptr);
                terrainDraw = &draw;
            }
        }
        assert(terrainDraw != nullptr);
        assert(terrainDraw->pipeline == PipelineClass::Textured);
        assert(terrainDraw->instanceCount >= 4U);
        assert(terrainDraw->atlasPage == rocket::sceneAtlasPageForTexture(texture));

        // Exterior bedrock is submitted before the four authored fixture cells.
        const std::size_t first = terrainDraw->firstInstance + terrainDraw->instanceCount - 4U;
        const SceneInstance regolith = rocket::unpackSceneInstance(packet.miningTerrainInstances[first]);
        const SceneInstance hardRock = rocket::unpackSceneInstance(packet.miningTerrainInstances[first + 1U]);
        const SceneInstance hazard = rocket::unpackSceneInstance(packet.miningTerrainInstances[first + 2U]);
        const SceneInstance common = rocket::unpackSceneInstance(packet.miningTerrainInstances[first + 3U]);
        assert(regolith.textured);
        assert(hardRock.textured);
        assert(hazard.textured);
        assert(common.textured);
        assert(matchesFrame(regolith, texture, 0)
            || matchesFrame(regolith, texture, 1)
            || matchesFrame(regolith, texture, 2));
        assert(matchesFrame(hardRock, texture, 3)
            || matchesFrame(hardRock, texture, 4)
            || matchesFrame(hardRock, texture, 5));
        assert(matchesFrame(hazard, texture, 17));
        assert(matchesFrame(common, texture, 9));
    }

    RenderSnapshot fallbackSnapshot = miningSnapshot(mining);
    fallbackSnapshot.destinationTier = 1;
    SceneComposer fallbackComposer;
    fallbackComposer.setViewport({1280, 800, 1280, 800, 1.0F});
    const ScenePacket& fallback = fallbackComposer.compose(fallbackSnapshot);
    const std::uint64_t fallbackRevision = fallback.miningTerrainRevision;
    assert(std::none_of(fallback.draws.begin(), fallback.draws.end(), [](const SceneDraw& draw) {
        return draw.instanceStream == SceneInstanceStream::MiningTerrain
            && draw.pipeline == PipelineClass::Textured;
    }));

    fallbackComposer.setTextureReady(TextureId::MiningTilesMoon, true);
    const ScenePacket& loaded = fallbackComposer.compose(fallbackSnapshot);
    assert(loaded.miningTerrainRevision != fallbackRevision);
    assert(std::any_of(loaded.draws.begin(), loaded.draws.end(), [](const SceneDraw& draw) {
        return draw.instanceStream == SceneInstanceStream::MiningTerrain
            && draw.texture == TextureId::MiningTilesMoon
            && draw.pipeline == PipelineClass::Textured;
    }));

    RenderSnapshot postSolar = miningSnapshot(mining);
    postSolar.destinationTier = 7;
    postSolar.miningPostSolarGeologyRow = 22;
    postSolar.miningGeologySeed = 0x123456789abcdef0ULL;
    SceneComposer postSolarComposer;
    postSolarComposer.setViewport({1280, 800, 1280, 800, 1.0F});
    const ScenePacket& postSolarFallback = postSolarComposer.compose(postSolar);
    assert(std::none_of(postSolarFallback.draws.begin(), postSolarFallback.draws.end(), [](const SceneDraw& draw) {
        return draw.instanceStream == SceneInstanceStream::MiningTerrain
            && draw.pipeline == PipelineClass::Textured;
    }));
    const std::uint64_t postSolarFallbackRevision = postSolarFallback.miningTerrainRevision;
    postSolarComposer.setTextureReady(TextureId::MiningTilesPostSolarLibrary, true);
    const ScenePacket& postSolarLoaded = postSolarComposer.compose(postSolar);
    assert(postSolarLoaded.miningTerrainRevision != postSolarFallbackRevision);
    const auto postSolarDraw = std::find_if(postSolarLoaded.draws.begin(), postSolarLoaded.draws.end(), [](const SceneDraw& draw) {
        return draw.instanceStream == SceneInstanceStream::MiningTerrain
            && draw.texture == TextureId::MiningTilesPostSolarLibrary
            && draw.pipeline == PipelineClass::Textured;
    });
    assert(postSolarDraw != postSolarLoaded.draws.end());
    assert(postSolarDraw->instanceCount >= 4U);
    const SceneInstance postSolarCommon = rocket::unpackSceneInstance(
        postSolarLoaded.miningTerrainInstances[postSolarDraw->firstInstance + postSolarDraw->instanceCount - 1U]);
    const rocket::SceneAtlasUvRect expectedCommon = rocket::mapSceneAtlasUvRect(
        TextureId::MiningTilesPostSolarLibrary,
        9.0F / 19.0F,
        22.0F / 32.0F,
        10.0F / 19.0F,
        23.0F / 32.0F);
    constexpr float tolerance = 0.0006F;
    assert(expectedCommon.valid);
    assert(std::abs(postSolarCommon.u0 - expectedCommon.u0) < tolerance);
    assert(std::abs(postSolarCommon.v0 - expectedCommon.v0) < tolerance);
    assert(std::abs(postSolarCommon.u1 - expectedCommon.u1) < tolerance);
    assert(std::abs(postSolarCommon.v1 - expectedCommon.v1) < tolerance);
    const std::uint64_t stablePostSolarRevision = postSolarLoaded.miningTerrainRevision;
    postSolar.miningPostSolarGeologyRow = 23;
    const ScenePacket& changedGeology = postSolarComposer.compose(postSolar);
    assert(changedGeology.miningTerrainRevision != stablePostSolarRevision);
}

void testLevelUpFanfareGeometryAndAccessibleShake()
{
    RenderSnapshot fanfare;
    fanfare.screen = rocket::Screen::SurfaceUpgrade;
    fanfare.levelUpFanfare = 1.0;
    fanfare.animationTime = 0.0;

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    const ScenePacket& first = composer.compose(fanfare);
    assert(!first.vertices.empty() || !first.instances.empty());
    assert(first.vertices.size() < 20000);
    assert(first.instances.size() < 20000);

    fanfare.levelUpFanfare = 0.35;
    const ScenePacket& mid = composer.compose(fanfare);
    assert(!mid.vertices.empty() || !mid.instances.empty());
    assert(mid.vertices.size() < 20000);
    assert(mid.instances.size() < 20000);
    const std::size_t midVertexCount = mid.vertices.size();
    const std::size_t midInstanceCount = mid.instances.size();

    SceneComposer shaken;
    shaken.setViewport({1280, 800, 1280, 800, 1.0F});
    const auto shakenCenter = rocket::SceneComposerTestAccess::frameCenter(shaken, fanfare);
    SceneComposer stable;
    stable.setViewport({1280, 800, 1280, 800, 1.0F});
    stable.setCameraShakeEnabled(false);
    const auto stableCenter = rocket::SceneComposerTestAccess::frameCenter(stable, fanfare);
    assert(std::hypot(shakenCenter.first - stableCenter.first, shakenCenter.second - stableCenter.second) > 0.001F);

    fanfare.levelUpFanfare = 0.0;
    const ScenePacket& quiet = composer.compose(fanfare);
    assert(quiet.vertices.size() < midVertexCount || quiet.instances.size() < midInstanceCount);
}

void testArrivalCelebrationRestoresImpactAndRadialBursts()
{
    RenderSnapshot arrival;
    arrival.screen = rocket::Screen::ArrivalFanfare;
    arrival.destinationTier = 2;
    arrival.travelProgress = 1.0;
    arrival.animationTime = 0.02;

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    const ScenePacket& celebration = composer.compose(arrival);
    const std::size_t celebrationVertexCount = celebration.vertices.size();
    const std::size_t celebrationInstanceCount = celebration.instances.size();
    const Color celebrationClear = celebration.clearColor;

    RenderSnapshot ordinaryApproach = arrival;
    ordinaryApproach.screen = rocket::Screen::ArrivalOps;
    const ScenePacket& ordinary = composer.compose(ordinaryApproach);
    const std::size_t ordinaryGeometryCount = ordinary.vertices.size() + ordinary.instances.size();
    assert(celebrationVertexCount + celebrationInstanceCount > ordinaryGeometryCount);
    assert(celebrationClear.r > ordinary.clearColor.r);

    SceneComposer shaken;
    shaken.setViewport({1280, 800, 1280, 800, 1.0F});
    const auto shakenCenter = rocket::SceneComposerTestAccess::frameCenter(shaken, arrival);
    SceneComposer stable;
    stable.setViewport({1280, 800, 1280, 800, 1.0F});
    stable.setCameraShakeEnabled(false);
    const auto stableCenter = rocket::SceneComposerTestAccess::frameCenter(stable, arrival);
    assert(std::hypot(
        shakenCenter.first - stableCenter.first,
        shakenCenter.second - stableCenter.second) > 1.0F);

    // Reduced-motion mode removes the contact jolt but preserves the rings
    // and outward line bursts, so the celebration is not erased with shake.
    const ScenePacket& accessibleCelebration = stable.compose(arrival);
    assert(accessibleCelebration.vertices.size() + accessibleCelebration.instances.size() > ordinaryGeometryCount);

    // The physical jolt settles quickly while the visual ceremony continues
    // for the remainder of the automatic two-second beat.
    arrival.animationTime = 0.90;
    SceneComposer settled;
    settled.setViewport({1280, 800, 1280, 800, 1.0F});
    const auto settledCenter = rocket::SceneComposerTestAccess::frameCenter(settled, arrival);
    SceneComposer settledStable;
    settledStable.setViewport({1280, 800, 1280, 800, 1.0F});
    settledStable.setCameraShakeEnabled(false);
    const auto settledStableCenter = rocket::SceneComposerTestAccess::frameCenter(settledStable, arrival);
    assert(std::hypot(
        settledCenter.first - settledStableCenter.first,
        settledCenter.second - settledStableCenter.second) < 0.001F);
    const ScenePacket& settledCelebration = settled.compose(arrival);
    assert(settledCelebration.vertices.size() + settledCelebration.instances.size() > ordinaryGeometryCount);

    RenderSnapshot touchdown;
    touchdown.screen = rocket::Screen::Flight;
    touchdown.launchTouchdownCelebration = true;
    touchdown.launchTouchdownFeedbackScale = 1.0;
    touchdown.launchTouchdownCelebrationProgress = 0.01;
    touchdown.surfaceArrivalHardLanding = true;
    SceneComposer touchdownShaken;
    touchdownShaken.setViewport({1280, 800, 1280, 800, 1.0F});
    const auto touchdownShakenCenter =
        rocket::SceneComposerTestAccess::frameCenter(touchdownShaken, touchdown);
    SceneComposer touchdownStable;
    touchdownStable.setViewport({1280, 800, 1280, 800, 1.0F});
    touchdownStable.setCameraShakeEnabled(false);
    const auto touchdownStableCenter =
        rocket::SceneComposerTestAccess::frameCenter(touchdownStable, touchdown);
    const float touchdownOffset = std::hypot(
        touchdownShakenCenter.first - touchdownStableCenter.first,
        touchdownShakenCenter.second - touchdownStableCenter.second);
    assert(touchdownOffset > 0.5F && touchdownOffset < 8.0F);

    // Touchdown progress spans the two-second celebration, so 0.20 is 0.40
    // seconds after contact and must be fully settled.
    touchdown.launchTouchdownCelebrationProgress = 0.20;
    SceneComposer touchdownSettled;
    touchdownSettled.setViewport({1280, 800, 1280, 800, 1.0F});
    const auto touchdownSettledCenter =
        rocket::SceneComposerTestAccess::frameCenter(touchdownSettled, touchdown);
    SceneComposer touchdownSettledStable;
    touchdownSettledStable.setViewport({1280, 800, 1280, 800, 1.0F});
    touchdownSettledStable.setCameraShakeEnabled(false);
    const auto touchdownSettledStableCenter =
        rocket::SceneComposerTestAccess::frameCenter(touchdownSettledStable, touchdown);
    assert(std::hypot(
        touchdownSettledCenter.first - touchdownSettledStableCenter.first,
        touchdownSettledCenter.second - touchdownSettledStableCenter.second) < 0.001F);
}

void testFlightDestructionCinematicUsesExplosionFramesAndAccessibleShake()
{
    const auto hasTextureFrame = [](const ScenePacket& packet, TextureId texture, int frame, int frameCount = 1) {
        const rocket::SceneAtlasUvRect expected = rocket::mapSceneAtlasUvRect(
            texture,
            static_cast<float>(frame) / static_cast<float>(frameCount),
            0.0F,
            static_cast<float>(frame + 1) / static_cast<float>(frameCount),
            1.0F);
        if (!expected.valid) {
            return false;
        }
        constexpr float tolerance = 2.0F / 65535.0F;
        for (const SceneDraw& draw : packet.draws) {
            if (draw.drawType != SceneDrawType::InstancedQuad) {
                continue;
            }
            for (std::size_t index = 0; index < draw.instanceCount; ++index) {
                const SceneInstance instance = rocket::unpackSceneInstance(
                    packet.instances[draw.firstInstance + index]);
                if (instance.textured &&
                    std::abs(instance.u0 - expected.u0) <= tolerance &&
                    std::abs(instance.v0 - expected.v0) <= tolerance &&
                    std::abs(instance.u1 - expected.u1) <= tolerance &&
                    std::abs(instance.v1 - expected.v1) <= tolerance) {
                    return true;
                }
            }
        }
        return false;
    };
    const auto explosionFrame = [&](const ScenePacket& packet) {
        for (int frame = 0; frame < 8; ++frame) {
            if (hasTextureFrame(packet, TextureId::Explosion, frame, 8)) {
                return frame;
            }
        }
        return -1;
    };

    RenderSnapshot impact;
    impact.screen = rocket::Screen::Flight;
    impact.destinationTier = 1;
    impact.travelProgress = 1.0;
    impact.launchDestructionActive = true;
    impact.launchDestructionElapsed = 0.04;
    impact.launchDestructionCause = rocket::LaunchFailureCause::LunarImpact;
    impact.animationTime = impact.launchDestructionElapsed;

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    composer.setTextureReady(TextureId::RocketClosed, true);
    composer.setTextureReady(TextureId::Explosion, true);
    const ScenePacket& contact = composer.compose(impact);
    assert(hasTextureFrame(contact, TextureId::RocketClosed, 0));
    assert(explosionFrame(contact) < 0);
    const std::size_t contactDrawCount = contact.draws.size();

    impact.launchDestructionElapsed = 0.09;
    impact.animationTime = impact.launchDestructionElapsed;
    const ScenePacket& firstBlast = composer.compose(impact);
    assert(!hasTextureFrame(firstBlast, TextureId::RocketClosed, 0));
    assert(explosionFrame(firstBlast) == 0);
    const std::size_t firstBlastDrawCount = firstBlast.draws.size();
    assert(firstBlastDrawCount > contactDrawCount);

    const double frameDuration =
        (rocket::tuning::session::flightDestructionExplosionEndSeconds -
            rocket::tuning::session::flightDestructionHoldSeconds) /
        8.0;
    for (const auto cause : {rocket::LaunchFailureCause::LunarImpact,
                            rocket::LaunchFailureCause::ThermalRunaway,
                            rocket::LaunchFailureCause::HullBreach}) {
        impact.launchDestructionCause = cause;
        for (int frame = 0; frame < 8; ++frame) {
            impact.launchDestructionElapsed =
                rocket::tuning::session::flightDestructionHoldSeconds +
                (static_cast<double>(frame) + 0.5) * frameDuration;
            impact.animationTime = impact.launchDestructionElapsed;
            const ScenePacket& blastFrame = composer.compose(impact);
            assert(explosionFrame(blastFrame) == frame);
        }
        impact.launchDestructionElapsed = rocket::tuning::session::flightDestructionSequenceSeconds;
        assert(explosionFrame(composer.compose(impact)) == 7);
    }

    RenderSnapshot still = impact;
    still.launchDestructionElapsed = 0.12;
    still.animationTime = still.launchDestructionElapsed;
    SceneComposer shakeComposer;
    shakeComposer.setViewport({1280, 800, 1280, 800, 1.0F});
    const auto shakenCenter = rocket::SceneComposerTestAccess::frameCenter(shakeComposer, still);
    SceneComposer accessibleComposer;
    accessibleComposer.setViewport({1280, 800, 1280, 800, 1.0F});
    accessibleComposer.setCameraShakeEnabled(false);
    const auto stableCenter = rocket::SceneComposerTestAccess::frameCenter(accessibleComposer, still);
    const float lunarShakeDistance = std::hypot(
        shakenCenter.first - stableCenter.first,
        shakenCenter.second - stableCenter.second);
    assert(lunarShakeDistance > 8.0F);
    RenderSnapshot ordinaryImpact = still;
    ordinaryImpact.launchDestructionActive = false;
    ordinaryImpact.launchShake = 1.0;
    SceneComposer ordinaryComposer;
    ordinaryComposer.setViewport({1280, 800, 1280, 800, 1.0F});
    const auto ordinaryCenter = rocket::SceneComposerTestAccess::frameCenter(
        ordinaryComposer,
        ordinaryImpact);
    assert(lunarShakeDistance > std::hypot(
        ordinaryCenter.first - stableCenter.first,
        ordinaryCenter.second - stableCenter.second));
    accessibleComposer.setTextureReady(TextureId::Explosion, true);
    const ScenePacket& accessibleBlast = accessibleComposer.compose(still);
    assert(explosionFrame(accessibleBlast) >= 0);

    RenderSnapshot lunarResult;
    lunarResult.screen = rocket::Screen::Results;
    lunarResult.lastResult = rocket::LaunchResultType::Destroyed;
    lunarResult.lastLaunchFailureCause = rocket::LaunchFailureCause::LunarImpact;
    lunarResult.animationTime = 0.20;
    const ScenePacket& resolvedLunarImpact = composer.compose(lunarResult);
    assert(explosionFrame(resolvedLunarImpact) < 0);
    assert(!hasTextureFrame(resolvedLunarImpact, TextureId::RocketClosed, 0));

    lunarResult.lastLaunchFailureCause = rocket::LaunchFailureCause::ThermalRunaway;
    const ScenePacket& resolvedThermalRunaway = composer.compose(lunarResult);
    assert(explosionFrame(resolvedThermalRunaway) < 0);
    assert(!hasTextureFrame(resolvedThermalRunaway, TextureId::RocketClosed, 0));

    lunarResult.lastLaunchFailureCause = rocket::LaunchFailureCause::HullBreach;
    assert(explosionFrame(composer.compose(lunarResult)) < 0);

    lunarResult.lastLaunchFailureCause = rocket::LaunchFailureCause::FuelExhausted;
    const ScenePacket& genericDestroyed = composer.compose(lunarResult);
    assert(explosionFrame(genericDestroyed) >= 0);
}

void testSolarBeltRendering()
{
    using namespace rocket;
    SceneComposer composer;
    composer.setViewport({1600,900,1600,900,1.0F});
    composer.setTextureReady(TextureId::Asteroid,true);
    RenderSnapshot snapshot;
    snapshot.screen=Screen::Flight;
    snapshot.launchPhysicalFlight=snapshot.systemTravel=true;
    snapshot.system=solarSystemDefinition();
    snapshot.systemLocation.frame=CoordinateFrame::System;
    snapshot.launchPositionX=solarAsteroidBelt().front().position.x;
    snapshot.launchPositionY=0;
    snapshot.launchLandingBlend=0;
    snapshot.flightGuidance.targetPosition={30,9};
    // This path must not depend on the disabled legacy corridor asteroid flag.
    snapshot.launchAsteroidsEnabled=false;
    const auto asteroid=spriteInstance(composer.compose(snapshot),TextureId::Asteroid,0,0,1,1);
    assert(std::hypot(asteroid.axisXx,asteroid.axisXy)>0);
}

void testArtifactWreckMarkerUsesOwnership()
{
    using namespace rocket;
    SceneComposer composer;
    composer.setViewport({1280,800,1280,800,1.0F});
    RenderSnapshot snapshot;
    snapshot.screen = Screen::Flight;
    snapshot.launchPhysicalFlight = snapshot.systemTravel = true;
    snapshot.system = solarSystemDefinition();
    snapshot.systemLocation.frame = CoordinateFrame::System;
    snapshot.launchPositionX = 10;
    WreckState wreck; wreck.id = 420;
    wreck.location = {"solar", "", CoordinateFrame::System, {12,0}, {}, 0, {}};
    snapshot.wrecks.push_back(wreck);
    const auto purpleCount = [](const ScenePacket& packet) {
        return std::count_if(packet.instances.begin(), packet.instances.end(), [](const auto& packed) {
            const auto color = unpackSceneInstance(packed).color;
            return std::abs(color.r-.78F)<.015F && std::abs(color.g-.38F)<.015F && color.b>.99F;
        });
    };
    const auto ordinary = purpleCount(composer.compose(snapshot));
    snapshot.artifactWreckIds.push_back(420);
    assert(purpleCount(composer.compose(snapshot)) >= ordinary + 4);
    snapshot.artifactWreckIds.clear();
    assert(purpleCount(composer.compose(snapshot)) == ordinary);
}

void testMiningKeepsContinuousExplorationShadows()
{
    using namespace rocket;
    MiningRunState mining;
    mining.active = true;
    mining.terrain.width = 64;
    mining.terrain.height = 40;
    mining.terrain.cells.resize(64*40);
    mining.droneX = 32;
    mining.droneY = 24;
    mining.returnZoneX = 32;
    mining.returnZoneY = 16;
    for (auto& cell : mining.terrain.cells) {
        cell.material = MiningCellMaterial::Regolith;
        cell.revealed = true;
    }
    SceneComposer composer;
    composer.setViewport({1280,800,1280,800,1.0F});
    const auto fogWeight = [](const ScenePacket& packet) {
        float sum=0;
        for (const auto& vertex : packet.vertices)
            if (vertex.r==4 && vertex.g==6 && vertex.b==11) sum+=unpackSceneVertex(vertex).a;
        return sum;
    };
    const float explored = fogWeight(composer.compose(miningSnapshot(mining)));
    for (auto& cell : mining.terrain.cells) cell.revealed = false;
    const auto& hidden = composer.compose(miningSnapshot(mining));
    assertValidDrawRanges(hidden);
    const float unexplored = fogWeight(hidden);
    assert(unexplored > explored + 1.0F);
    assert(unexplored > 0.0F);
    for (auto& cell : mining.terrain.cells) cell.revealed = true;
    const float revealedAgain = fogWeight(composer.compose(miningSnapshot(mining)));
    assert(std::abs(revealedAgain-explored)<.001F);
}

void testCommittedDepartureRendering()
{
    using namespace rocket;
    const auto atmosphereAlphas = [](const ScenePacket& packet) {
        std::vector<float> alphas;
        for (const auto& packed : packet.vertices) {
            // Packed RGB of the shared continuous atmospheric veil.
            if (packed.r == 4 && packed.g == 6 && packed.b == 11)
                alphas.push_back(unpackSceneVertex(packed).a);
        }
        return alphas;
    };
    std::vector<MiningCell> cells(64*40);
    for (int y=16;y<40;++y) for (int x=0;x<64;++x) {
        auto& cell=cells[y*64+x];
        cell.material=MiningCellMaterial::Regolith;
        cell.revealed=true;
    }
    for (const auto& zone : planetLandingZones()) for (int width : {800,1600}) {
        SceneComposer composer;
        composer.setViewport({width,900,width,900,1.0F});
        composer.setTextureReady(TextureId::RocketClosed,true);
        RenderSnapshot snapshot;
        snapshot.screen=Screen::Flight;
        snapshot.launchPhysicalFlight=true;
        snapshot.surfaceArrivalPrepared=true;
        snapshot.launchApproachBlend=1.0;
        snapshot.launchLandingLocalFrame=true;
        snapshot.launchLandingBlend=1.0;
        snapshot.surfaceFramingProgress=1.0;
        snapshot.launchLandingAltitude=flight_landing::departureAltitude;
        snapshot.launchLandingBasisAngle=zone.centerBearing;
        snapshot.launchHeading=zone.centerBearing;
        snapshot.launchOrbitTargetRadius=.7;
        snapshot.launchOrbitGoodBand=.1;
        snapshot.miningWidth=64; snapshot.miningHeight=40;
        snapshot.miningCells=cells;
        snapshot.miningReturnZoneX=snapshot.launchLandingPadX=32;
        snapshot.miningReturnZoneY=snapshot.launchLandingPadY=16;
        const double radius=flight_geometry::bodyRadius+flight_landing::departureAltitude/flight_landing::metersPerOrbitUnit;
        snapshot.launchPositionX=snapshot.launchHandoffX=radius*std::cos(zone.centerBearing);
        snapshot.launchPositionY=snapshot.launchHandoffY=radius*std::sin(zone.centerBearing);
        auto ship=spriteInstance(composer.compose(snapshot),TextureId::RocketClosed,0,0,1,1);
        const auto local=ship;
        const auto fogAtDeparture=atmosphereAlphas(composer.compose(snapshot));
        assert(!fogAtDeparture.empty());
        assert(fogAtDeparture.size()<=32*24*6);
        assert(std::any_of(fogAtDeparture.begin(),fogAtDeparture.end(),
            [](float alpha) { return alpha>0.05F && alpha<0.95F; }));
        snapshot.launchLandingLocalFrame=false;
        snapshot.launchHandoffFrom=static_cast<int>(FlightMode::Landing);
        for (int sample=0;sample<=100;++sample) {
            snapshot.launchHandoffProgress=sample/100.0;
            snapshot.launchLandingBlend=1.0-departureCameraProgress(snapshot.launchHandoffProgress);
            snapshot.surfaceFramingProgress=snapshot.launchLandingBlend;
            const auto& packet=composer.compose(snapshot);
            const auto fog=atmosphereAlphas(packet);
            if (sample>=40) assert(fog.empty());
            else {
                const double fade=std::clamp(snapshot.launchHandoffProgress/.4,0.0,1.0);
                const double opacity=1.0-fade*fade*fade*(fade*(fade*6.0-15.0)+10.0);
                for (float alpha : fog) assert(alpha<=opacity+.005);
            }
            const auto next=spriteInstance(packet,TextureId::RocketClosed,0,0,1,1);
            assert(std::hypot(next.centerX-ship.centerX,next.centerY-ship.centerY)<.06F);
            assert(std::abs(std::hypot(next.axisYx,next.axisYy)-std::hypot(ship.axisYx,ship.axisYy))<.025F);
            const double turn=flightWrappedAngleDelta(std::atan2(ship.axisYy,ship.axisYx),
                std::atan2(next.axisYy,next.axisYx));
            assert(std::abs(turn)<.16);
            if (sample<=40) {
                assert(std::hypot(next.centerX-local.centerX,next.centerY-local.centerY)<.002F);
                assert(std::abs(flightWrappedAngleDelta(std::atan2(local.axisYy,local.axisYx),
                    std::atan2(next.axisYy,next.axisYx)))<.002);
            }
            if (sample>=40) assert(packet.miningTerrainInstances.empty());
            ship=next;
        }
        // Rebuilding presentation at a saved mid-handoff pose must agree with
        // a running renderer; no hidden camera history is needed after reload.
        snapshot.launchHandoffProgress=.7;
        snapshot.launchLandingBlend=1.0-departureCameraProgress(.7);
        snapshot.surfaceFramingProgress=snapshot.launchLandingBlend;
        const auto continued=spriteInstance(composer.compose(snapshot),TextureId::RocketClosed,0,0,1,1);
        SceneComposer reloaded;
        reloaded.setViewport({width,900,width,900,1.0F});
        reloaded.setTextureReady(TextureId::RocketClosed,true);
        const auto restored=spriteInstance(reloaded.compose(snapshot),TextureId::RocketClosed,0,0,1,1);
        assert(std::hypot(continued.centerX-restored.centerX,continued.centerY-restored.centerY)<.002F);
        // Underground departures retain visibility immediately around the
        // ship, even below the opaque surface-depth fog.
        snapshot.launchLandingLocalFrame=true;
        snapshot.launchLandingBlend=snapshot.surfaceFramingProgress=1.0;
        snapshot.launchLandingAltitude=-24.0;
        const auto& underground=composer.compose(snapshot);
        const auto& camera=underground.surfaceCamera;
        const float shipGridY=16.0F+24.0F/flight_landing::metersPerCell-3.0F;
        bool checkedNearby=false;
        for (const auto& packed : underground.vertices) {
            if (packed.r!=4 || packed.g!=6 || packed.b!=11) continue;
            const auto vertex=unpackSceneVertex(packed);
            const float gridX=(vertex.x-camera.left)/camera.cellWidth;
            const float gridY=(camera.top-vertex.y)/camera.cellHeight;
            if (std::hypot(gridX-32.5F,gridY-shipGridY)<3.0F) {
                assert(vertex.a==0.0F);
                checkedNearby=true;
            }
        }
        assert(checkedNearby);
        snapshot.screen=Screen::Mining;
        assert(!atmosphereAlphas(composer.compose(snapshot)).empty());
    }
    // Io uses the existing moon artwork in the system scene, not the Jupiter
    // environment artwork. The handoff must not swap either art or scale.
    SceneComposer bodyComposer;
    bodyComposer.setViewport({1600,900,1600,900,1.0F});
    bodyComposer.setTextureReady(TextureId::Moon,true);
    bodyComposer.setTextureReady(TextureId::Jupiter,true);
    RenderSnapshot bodySnapshot;
    bodySnapshot.screen=Screen::Flight;
    bodySnapshot.launchPhysicalFlight=true;
    bodySnapshot.systemTravel=true;
    bodySnapshot.system.id="solar";
    bodySnapshot.systemLocation.frame=CoordinateFrame::Body;
    bodySnapshot.systemLocation.bodyId="io";
    SystemBodyDefinition io;
    io.id="io"; io.name="Io"; io.displayRadius=.13;
    bodySnapshot.system.bodies.push_back(io);
    bodySnapshot.destinationTier=3;
    bodySnapshot.surfaceArrivalPrepared=true;
    bodySnapshot.launchHandoffFrom=static_cast<int>(FlightMode::Landing);
    bodySnapshot.launchPositionY=.4;
    bodySnapshot.launchHandoffProgress=.90;
    bodySnapshot.launchLandingBlend=1.0-departureCameraProgress(.90);
    const auto outgoing=spriteInstance(bodyComposer.compose(bodySnapshot),TextureId::Moon,0,0,1,1);
    bodySnapshot.launchHandoffProgress=1.0;
    bodySnapshot.launchLandingBlend=0.0;
    const auto orbital=spriteInstance(bodyComposer.compose(bodySnapshot),TextureId::Moon,0,0,1,1);
    assert(std::abs(outgoing.axisXx-orbital.axisXx)<.02F);
}

void testServiceDockTracksShipBeforeApproach()
{
    using namespace rocket;
    RenderSnapshot snapshot;
    snapshot.screen = Screen::Flight;
    snapshot.launchPhysicalFlight = snapshot.systemTravel = true;
    snapshot.system = solarSystemDefinition();
    snapshot.systemLocation.frame = CoordinateFrame::System;
    const auto dockPosition = systemDockPosition(*systemBody(snapshot.system, "earth"));
    snapshot.flightGuidance.targetPosition = dockPosition;
    snapshot.flightGuidance.targetId = "earth";
    SceneComposer composer;
    composer.setViewport({1600, 900, 1600, 900, 1.0F});
    composer.setTextureReady(TextureId::RocketClosed, true);
    composer.setTextureReady(TextureId::ServiceDock, true);
    for (const double angle : {0.0, 1.1, 2.8, -1.4}) {
        snapshot.launchPositionX = dockPosition.x + 2.0 * std::cos(angle);
        snapshot.launchPositionY = dockPosition.y + 2.0 * std::sin(angle);
        snapshot.launchHeading = angle;
        for (const bool stagedDeparture : {false, true}) {
            snapshot.launchUndockReady = stagedDeparture;
            const auto& packet = composer.compose(snapshot);
            const auto dock = spriteInstance(packet, TextureId::ServiceDock, 0, 0, 1, 1);
            const auto ship = spriteInstance(packet, TextureId::RocketClosed, 0, 0, 1, 1);
            const float dx = ship.centerX - dock.centerX, dy = ship.centerY - dock.centerY;
            const float norm = std::hypot(dx, dy) * std::hypot(dock.axisYx, dock.axisYy);
            assert(norm > 0.0F);
            assert((dx * dock.axisYx + dy * dock.axisYy) / norm > .999F);
        }
    }
}

void testServiceDockUsesBalancedScaleAndDeterministicHandoff()
{
    using namespace rocket;
    RenderSnapshot snapshot;
    snapshot.screen = Screen::Flight;
    snapshot.launchPhysicalFlight = true;
    snapshot.systemTravel = true;
    snapshot.system = solarSystemDefinition();
    snapshot.launchDockingActive = true;
    snapshot.launchDockHeading = 1.10;
    snapshot.launchHeading = snapshot.launchDockHeading + 3.14159265358979323846;
    snapshot.launchPositionX = .16;
    snapshot.launchPositionY = 1.05;
    snapshot.launchDockHandoffX = .40;
    snapshot.launchDockHandoffY = 1.86;
    snapshot.launchDockHandoffProgress = 1.0;

    SceneComposer composer;
    composer.setViewport({1600, 900, 1600, 900, 1.0F});
    composer.setTextureReady(TextureId::RocketClosed, true);
    composer.setTextureReady(TextureId::ServiceDock, true);
    composer.setTextureReady(TextureId::Earth, true);
    const auto& packet = composer.compose(snapshot);
    const auto ship = spriteInstance(packet, TextureId::RocketClosed, 0, 0, 1, 1);
    const auto dock = spriteInstance(packet, TextureId::ServiceDock, 0, 0, 1, 1);
    const float shipLength = 2.0F * std::hypot(ship.axisYx, ship.axisYy);
    const float dockLength = 2.0F * std::hypot(dock.axisYx, dock.axisYy);
    assert(shipLength / dockLength > .35F && shipLength / dockLength < .42F);
    snapshot.launchPositionX = 0.0;
    snapshot.launchPositionY = service_dock::approachRadius * service_dock::localUnitsPerSystemUnit;
    const auto& widePacket = composer.compose(snapshot);
    const auto distantShip = spriteInstance(widePacket, TextureId::RocketClosed, 0, 0, 1, 1);
    const auto distantDock = spriteInstance(widePacket, TextureId::ServiceDock, 0, 0, 1, 1);
    const float distantRatio = std::hypot(distantShip.axisYx, distantShip.axisYy) /
        std::hypot(distantDock.axisYx, distantDock.axisYy);
    assert(std::abs(distantRatio - shipLength / dockLength) < .002F);
    assert(std::abs(distantShip.centerY) < .9F && std::abs(distantDock.centerY) < .9F);
    snapshot.launchPositionX = .16;
    snapshot.launchPositionY = 1.05;
    assert(service_dock::captureCenterY - service_dock::guideHalfDepth > service_dock::backstopY);
    assert(service_dock::captureCenterY + service_dock::guideHalfDepth < service_dock::mouthY);

    snapshot.launchDockHandoffProgress = .52;
    const auto running = spriteInstance(composer.compose(snapshot), TextureId::RocketClosed, 0, 0, 1, 1);
    SceneComposer restored;
    restored.setViewport({1600, 900, 1600, 900, 1.0F});
    restored.setTextureReady(TextureId::RocketClosed, true);
    restored.setTextureReady(TextureId::ServiceDock, true);
    restored.setTextureReady(TextureId::Earth, true);
    const auto reloaded = spriteInstance(restored.compose(snapshot), TextureId::RocketClosed, 0, 0, 1, 1);
    assert(std::hypot(running.centerX - reloaded.centerX, running.centerY - reloaded.centerY) < .002F);
}

} // namespace

int main() try
{
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    testUiViewportLayoutGeometry();
    testFlightPointerMatchesRenderedShip();
    testSolarBeltRendering();
    testArtifactWreckMarkerUsesOwnership();
    testCommittedDepartureRendering();
    testMiningViewportReservesBothHudLanes();
    testScreenSurfaceMapping();
    testSceneComposerUsesResolvedSceneRect();
    testLogicalSceneClipScalesToFramebuffer();
    testPackedVertexConversion();
    testLaunchDestinationGateUsesCorridorEndpoints();
    testTransferAssistLaunchUsesItsSourceBody();
    testJupiterSaturnLaunchKeepsJupiterVisibleBesideShip();
    testManifestAndLogicalTextureMapping();
    testEnemyThemesAndAnimationPriorityUseTheSharedSpriteContract();
    testFlightInstrumentClusterUsesAtlasNeedlesAndBlinkingWarning();
    testLaunchUsesAttachedAnimatedSideFlames();
    testServiceDockTracksShipBeforeApproach();
    testServiceDockUsesBalancedScaleAndDeterministicHandoff();
    testMiningSkyAndTunnelBackdrop();
    testDistantEarthMarkerUsesViewportEdgeAndPixelSize();
    testPhysicalMoonFlightStartsOnScreenAtEarthDeparture();
    testPhysicalApproachZoomBeginsContinuouslyAtThreeQuarters();
    testPhysicalApproachCameraAppliesToMarsAndLaterDestinations();
    testPhysicalLandingCameraBlendsWithoutTeleportingUnauthorizedImpacts();
    testPhysicalLandingCameraDoesNotRetainTransferBodies();
    testExistingOrbitalShaftsRemainVisibleOutsideTheirActiveWedge();
    testIoScanAndOrbitShareArtAndArtifactSignal();
    testUndiscoveredStraylightIsForeshadowedBehindNeptuneOnly();
    testPolygonInstanceMatchesTriangleFan();
    testOrderedBatchingAndWideLineInstancing();
    testUniformAndGradientLineOrdering();
    testAtlasPageBatchingAcrossLogicalTextures();
    testMiningEVAUsesDedicatedTextureWithoutFallback();
    testMiningEvaDeathAddsPresentationWithoutReplacingTheSuit();
    testMiningActiveAnchorOwnsDefenseEffects();
    testMiningLooseObjectsAreVisibleWorldEntities();
    testMiningLooseObjectsUseContinuousWorldCoordinates();
    testMiningCellsAndScannerMarksUseMaterialSilhouettes();
    testCocoonHasNoConnectingArms();
    testSceneTransitionFadesEverySceneToBlack();
    testTetheredArtifactAuraHasNoRectangularOverlay();
    testTriangulationUsesOneThreeSliceAuraAndHidesArtifactGlow();
    testMiningPickupHistoryDoesNotReplayAfterLevelUp();
    testMiningRigSlerpsVerticalDuringExtraction();
    testMiningDepartureFlameTracksShip();
    testMiningRigStaysVisibleAndTracksHeading();
    testCuttingFeedbackComesFromEveryActiveHead();
    testBlockedDrillHasFeedbackWithoutCuttingParticles();
    testReturnRingIsCenteredOnTheShip();
    testMiningCollisionIndicatorMarksTheContactedEdge();
    testMiningSurveyPulseRechargeRingPersistsWhenReady();
    testMiningControllerReticleFollowsScannerRing();
    testMiningSurveyPulseWaveReachesItsRealRadiusThenFades();
    testMiningSurveyPulseProgressivelyRevealsNewTerrain();
    testMiningRigDrillStaysMountedThroughRecoilAndExtension();
    testFrameViewsKeepAuthoritativeEnemyIndices();
    testHazardDroneTransitShimmerAndAssistantBeams();
    testMiningTerrainPersistentStreamInvalidation();
    testMiningKeepsContinuousExplorationShadows();
    testMiningTerrainUsesDestinationTilesAndMaterialFrames();

    testLevelUpFanfareGeometryAndAccessibleShake();
    testArrivalCelebrationRestoresImpactAndRadialBursts();
    testFlightDestructionCinematicUsesExplosionFramesAndAccessibleShake();
    return 0;
}
catch (const std::exception& error) {
    std::fprintf(stderr, "Scene composer exception: %s\n", error.what());
    return 1;
}
