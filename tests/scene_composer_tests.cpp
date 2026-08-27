#include "core/UiViewportLayout.h"
#include "core/Tuning.h"
#include "core/FlightInstrumentLayout.h"
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

    static ScenePacket surfacePushPacketWithAspect(
        SceneComposer& composer,
        const RenderSnapshot& snapshot,
        float aspect)
    {
        composer.beginFrame(snapshot);
        composer.sceneAspect_ = aspect;
        composer.drawSurfacePush(snapshot);
        composer.finalizePacket();
        return composer.packet_;
    }

    static ScenePacket poiGuidancePacket(
        SceneComposer& composer,
        std::string_view label,
        PoiGuidanceKind kind,
        double animationTime,
        float directionY)
    {
        RenderSnapshot snapshot;
        composer.beginFrame(snapshot);
        composer.drawPoiGuidance(
            0.0F,
            0.0F,
            0.0F,
            directionY,
            0.20F,
            label,
            kind,
            animationTime);
        composer.finalizePacket();
        return composer.packet_;
    }

    static Color miningOreSparkleColor(
        SceneComposer& composer,
        MiningCellMaterial material)
    {
        RenderSnapshot snapshot;
        composer.beginFrame(snapshot);
        composer.drawMiningOreSparkle(
            0.0F,
            0.0F,
            0.10F,
            static_cast<int>(material),
            0.0F,
            0.0F);
        composer.finalizePacket();
        if (!composer.packet_.instances.empty()) {
            return unpackSceneInstance(composer.packet_.instances.front()).color;
        }
        assert(!composer.packet_.vertices.empty());
        const SceneVertex vertex =
            unpackSceneVertex(composer.packet_.vertices.front());
        return {vertex.r, vertex.g, vertex.b, vertex.a};
    }

    static ScenePacket miningPickupTextPacket(
        SceneComposer& composer,
        MiningPickupKind kind,
        int amount,
        float age)
    {
        RenderSnapshot snapshot;
        composer.beginFrame(snapshot);
        composer.drawMiningPickupText(0.0F, 0.0F, 0.10F, kind, amount, age);
        composer.finalizePacket();
        return composer.packet_;
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

    static ScenePacket flybyPacket(
        SceneComposer& composer,
        const RenderSnapshot& snapshot)
    {
        composer.beginFrame(snapshot);
        composer.drawFlyby(snapshot);
        composer.finalizePacket();
        return composer.packet_;
    }

    static ScenePacket orbitPacket(
        SceneComposer& composer,
        const RenderSnapshot& snapshot)
    {
        composer.beginFrame(snapshot);
        composer.drawOrbit(snapshot);
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
        rocket::Screen::Launch,
        rocket::Screen::Flyby,
        rocket::Screen::Orbit,
        rocket::Screen::SurfaceScan,
        rocket::Screen::SurfacePush
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

void testCompletedFlybyAndOrbitUseFullscreenSceneSurface()
{
    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});

    RenderSnapshot snapshot;
    for (const rocket::Screen screen : {rocket::Screen::Flyby, rocket::Screen::Orbit}) {
        snapshot = {};
        snapshot.screen = screen;
        const ScenePacket activePacket =
            rocket::SceneComposerTestAccess::beginFramePacket(composer, snapshot);
        assertRect(activePacket.logicalSceneClip, {331, 12, 937, 776});
        assert(std::abs(activePacket.transform.pixelCenterX - 799.5F) < 0.001F);
        assert(std::abs(activePacket.transform.pixelCenterY - 400.0F) < 0.001F);
        if (screen == rocket::Screen::Flyby) {
            // The authored Flyby finish gate must fit inside the active
            // side-panel viewport, including the outer Good boundary and
            // a small visible margin.
            const float tangentX = static_cast<float>(
                rocket::tuning::flyby::endX - rocket::tuning::flyby::control2X);
            const float tangentY = static_cast<float>(
                rocket::tuning::flyby::endY - rocket::tuning::flyby::control2Y);
            const float tangentLength = std::max(0.0001F, std::hypot(tangentX, tangentY));
            const float normalX = -tangentY / tangentLength;
            const float normalY = tangentX / tangentLength;
            constexpr float finishRadius = 0.028F;
            constexpr float finishMargin = 0.060F;
            const float finishHalfWidth = static_cast<float>(rocket::tuning::flyby::goodBand)
                + finishRadius + finishMargin;
            const float goalX = std::abs(static_cast<float>(rocket::tuning::flyby::endX))
                + std::abs(normalX) * finishHalfWidth;
            const float goalY = std::abs(static_cast<float>(rocket::tuning::flyby::endY))
                + std::abs(normalY) * finishHalfWidth;
            const float visibleHalfWidth = static_cast<float>(activePacket.logicalSceneClip.width)
                * 0.5F / activePacket.transform.worldUnitX;
            const float visibleHalfHeight = static_cast<float>(activePacket.logicalSceneClip.height)
                * 0.5F / activePacket.transform.worldUnitY;
            assert(goalX <= visibleHalfWidth);
            assert(goalY <= visibleHalfHeight);
        } else {
            const float expectedOrbitWorldUnit = 776.0F * 0.5F * 0.92F * 1.66F;
            assert(std::abs(activePacket.transform.worldUnitX - expectedOrbitWorldUnit) < 0.001F);
            assert(std::abs(activePacket.transform.worldUnitY - expectedOrbitWorldUnit) < 0.001F);
        }

        snapshot.flybyCompleted = screen == rocket::Screen::Flyby;
        snapshot.orbitCompleted = screen == rocket::Screen::Orbit;
        const ScenePacket completedPacket =
            rocket::SceneComposerTestAccess::beginFramePacket(composer, snapshot);
        assertRect(completedPacket.logicalSceneClip, {0, 0, 1280, 800});
        assert(std::abs(completedPacket.transform.pixelCenterX - 640.0F) < 0.001F);
        assert(std::abs(completedPacket.transform.pixelCenterY - 400.0F) < 0.001F);
        const float expectedCompletedWorldUnit = screen == rocket::Screen::Flyby ? 368.0F * 1.50F : 368.0F;
        assert(std::abs(completedPacket.transform.worldUnitX - expectedCompletedWorldUnit) < 0.001F);
        assert(std::abs(completedPacket.transform.worldUnitY - expectedCompletedWorldUnit) < 0.001F);
    }
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
        snapshot.screen = rocket::Screen::Launch;
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

void testOrbitGuideBandsHighlightActiveZone()
{
    const auto hasLineColor = [](
        const ScenePacket& packet,
        float red,
        float green,
        float blue,
        float alpha) {
        return std::any_of(
            packet.instances.begin(),
            packet.instances.end(),
            [&](const PackedSceneInstance& packed) {
                const SceneInstance instance = rocket::unpackSceneInstance(packed);
                return instance.shape == SceneInstanceShape::Rectangle
                    && std::abs(instance.color.r - red) < 0.01F
                    && std::abs(instance.color.g - green) < 0.01F
                    && std::abs(instance.color.b - blue) < 0.01F
                    && std::abs(instance.color.a - alpha) < 0.01F;
            });
    };

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    RenderSnapshot snapshot;
    snapshot.screen = rocket::Screen::Orbit;
    snapshot.orbitPlanetRadius = 0.20;
    snapshot.orbitTargetRadius = 0.62;
    snapshot.orbitGoodBand = 0.08;
    snapshot.orbitPerfectBand = 0.04;
    snapshot.orbitShipX = snapshot.orbitTargetRadius;
    snapshot.orbitVelocityY = 1.0;

    snapshot.orbitZone = 2;
    const ScenePacket& perfectPacket = composer.compose(snapshot);
    assert(hasLineColor(perfectPacket, 1.0F, 0.80F, 0.24F, 0.92F));

    snapshot.orbitZone = 1;
    const ScenePacket& goodPacket = composer.compose(snapshot);
    assert(hasLineColor(goodPacket, 0.35F, 0.92F, 0.62F, 0.92F));

    snapshot.orbitZone = 0;
    const ScenePacket& missedPacket = composer.compose(snapshot);
    assert(hasLineColor(missedPacket, 1.0F, 0.25F, 0.20F, 0.92F));
}

void testFlybyGuideBandsHighlightActiveZone()
{
    const auto hasLineColor = [](
        const ScenePacket& packet,
        float red,
        float green,
        float blue,
        float alpha) {
        return std::any_of(
            packet.instances.begin(),
            packet.instances.end(),
            [&](const PackedSceneInstance& packed) {
                const SceneInstance instance = rocket::unpackSceneInstance(packed);
                return instance.shape == SceneInstanceShape::Rectangle
                    && std::abs(instance.color.r - red) < 0.01F
                    && std::abs(instance.color.g - green) < 0.01F
                    && std::abs(instance.color.b - blue) < 0.01F
                    && std::abs(instance.color.a - alpha) < 0.01F;
            });
    };

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    RenderSnapshot snapshot;
    snapshot.screen = rocket::Screen::Flyby;
    snapshot.destinationTier = 1;
    snapshot.flybyDestinationX = rocket::tuning::flyby::destinationX;
    snapshot.flybyDestinationY = rocket::tuning::flyby::destinationY;
    snapshot.flybyGoodBand = rocket::tuning::flyby::goodBand;
    snapshot.flybyPerfectBand = rocket::tuning::flyby::perfectBand;
    snapshot.flybyVelocityX = 1.0;

    snapshot.flybyZone = 2;
    const ScenePacket& perfectPacket = composer.compose(snapshot);
    assert(hasLineColor(perfectPacket, 1.0F, 0.82F, 0.28F, 0.92F));

    snapshot.flybyZone = 1;
    const ScenePacket& goodPacket = composer.compose(snapshot);
    assert(hasLineColor(goodPacket, 0.35F, 0.92F, 0.62F, 0.92F));

    snapshot.flybyZone = 0;
    const ScenePacket& missedPacket = composer.compose(snapshot);
    assert(hasLineColor(missedPacket, 1.0F, 0.25F, 0.20F, 0.92F));
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
    snapshot.screen = rocket::Screen::Launch;
    snapshot.destinationTier = 3;
    snapshot.frontierTransfer = true;
    snapshot.launchOriginTier = 2;
    const ScenePacket& packet = composer.compose(snapshot);
    assert(hasTexture(packet, TextureId::Mars));
    assert(!hasTexture(packet, TextureId::Earth));
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
    snapshot.screen = rocket::Screen::Launch;
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

void testFlybySteeringTriangleAndThrustFlameRemainDistinct()
{
    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    composer.setTextureReady(TextureId::RocketClosed, true);
    composer.setTextureReady(TextureId::Thrust, true);

    RenderSnapshot snapshot;
    snapshot.screen = rocket::Screen::Flyby;
    snapshot.flybyVelocityX = 1.0;
    snapshot.flybyVelocityY = 0.0;
    snapshot.flybyDestinationX = 0.72;
    snapshot.flybyDestinationY = 0.0;
    snapshot.flybyGoodBand = 0.28;
    snapshot.flybyPerfectBand = 0.14;
    snapshot.instrumentThrottle = 0.0;

    const ScenePacket coasting = rocket::SceneComposerTestAccess::flybyPacket(composer, snapshot);
    int orangeVertices = 0;
    for (const PackedSceneVertex& packed : coasting.vertices) {
        const SceneVertex vertex = rocket::unpackSceneVertex(packed);
        if (std::abs(vertex.r - 1.0F) < 0.01F
            && std::abs(vertex.g - 0.42F) < 0.01F
            && std::abs(vertex.b - 0.06F) < 0.01F) {
            ++orangeVertices;
        }
    }
    assert(orangeVertices == 0);
    assert(std::none_of(coasting.draws.begin(), coasting.draws.end(), [](const SceneDraw& draw) {
        return draw.texture == TextureId::Thrust;
    }));

    snapshot.flybyInputX = 1.0;
    const ScenePacket steering = rocket::SceneComposerTestAccess::flybyPacket(composer, snapshot);
    int steeringOrangeVertices = 0;
    float steeringTipY = -1.0F;
    for (const PackedSceneVertex& packed : steering.vertices) {
        const SceneVertex vertex = rocket::unpackSceneVertex(packed);
        if (std::abs(vertex.r - 1.0F) < 0.01F
            && std::abs(vertex.g - 0.42F) < 0.01F
            && std::abs(vertex.b - 0.06F) < 0.01F) {
            steeringTipY = std::max(steeringTipY, vertex.y);
            ++steeringOrangeVertices;
        }
    }
    assert(steeringOrangeVertices == 3);
    assert(steeringTipY > 0.080F);
    assert(steeringTipY < 0.083F);

    snapshot.flybyInputX = 0.0;
    snapshot.instrumentThrottle = 0.5;
    const ScenePacket powered = rocket::SceneComposerTestAccess::flybyPacket(composer, snapshot);
    assert(std::any_of(powered.draws.begin(), powered.draws.end(), [](const SceneDraw& draw) {
        return draw.texture == TextureId::Thrust;
    }));
}

void testLaunchUsesAttachedFlameAndSideSteeringTriangleOnly()
{
    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    composer.setTextureReady(TextureId::RocketClosed, true);
    composer.setTextureReady(TextureId::Thrust, true);

    RenderSnapshot snapshot;
    snapshot.screen = rocket::Screen::Launch;
    snapshot.poweredFlight = true;
    snapshot.launchManualControlsEnabled = true;
    snapshot.launchThrottle = 0.6;
    snapshot.currentMultiplier = 1.2;
    snapshot.targetMultiplier = 2.0;

    const auto orangeVertexCount = [](const ScenePacket& packet) {
        return static_cast<int>(std::count_if(
            packet.vertices.begin(),
            packet.vertices.end(),
            [](const PackedSceneVertex& packed) {
                const SceneVertex vertex = rocket::unpackSceneVertex(packed);
                return std::abs(vertex.r - 1.0F) < 0.01F
                    && std::abs(vertex.g - 0.42F) < 0.01F
                    && std::abs(vertex.b - 0.06F) < 0.01F;
            }));
    };

    const ScenePacket powered = rocket::SceneComposerTestAccess::rocketPacket(composer, snapshot);
    assert(orangeVertexCount(powered) == 0);
    assert(std::any_of(powered.draws.begin(), powered.draws.end(), [](const SceneDraw& draw) {
        return draw.texture == TextureId::Thrust;
    }));

    snapshot.launchSteerInput = 1.0;
    const ScenePacket steering = rocket::SceneComposerTestAccess::rocketPacket(composer, snapshot);
    assert(orangeVertexCount(steering) == 3);
}

void testFlightPlumesScaleContinuouslyWithThrottle()
{
    const auto thrustSize = [](const ScenePacket& packet, float expectedAlpha) {
        // Atlas-page batching can combine the rocket and thrust into one draw;
        // distinguish the plume by its deliberate white translucent tint.
        const auto instanceIt = std::find_if(
            packet.instances.begin(),
            packet.instances.end(),
            [expectedAlpha](const PackedSceneInstance& packed) {
                const SceneInstance instance = rocket::unpackSceneInstance(packed);
                return instance.shape == SceneInstanceShape::Rectangle
                    && std::abs(instance.color.r - 1.0F) < 0.01F
                    && std::abs(instance.color.g - 1.0F) < 0.01F
                    && std::abs(instance.color.b - 1.0F) < 0.01F
                    && std::abs(instance.color.a - expectedAlpha) < 0.01F;
            });
        assert(instanceIt != packet.instances.end());
        const SceneInstance instance = rocket::unpackSceneInstance(*instanceIt);
        return std::pair {
            std::hypot(instance.axisXx, instance.axisXy),
            std::hypot(instance.axisYx, instance.axisYy)
        };
    };

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    composer.setTextureReady(TextureId::RocketClosed, true);
    composer.setTextureReady(TextureId::Thrust, true);

    RenderSnapshot flyby;
    flyby.screen = rocket::Screen::Flyby;
    flyby.flybyVelocityX = 1.0;
    flyby.flybyDestinationX = 0.72;
    flyby.flybyGoodBand = 0.28;
    flyby.flybyPerfectBand = 0.14;
    flyby.instrumentThrottle = 0.15;
    const auto flybyLow = thrustSize(rocket::SceneComposerTestAccess::flybyPacket(composer, flyby), 0.96F);
    flyby.instrumentThrottle = 0.85;
    const auto flybyHigh = thrustSize(rocket::SceneComposerTestAccess::flybyPacket(composer, flyby), 0.96F);
    assert(flybyHigh.first > flybyLow.first);
    assert(flybyHigh.second > flybyLow.second);

    RenderSnapshot orbit;
    orbit.screen = rocket::Screen::Orbit;
    orbit.orbitShipX = 0.62;
    orbit.orbitVelocityY = 1.0;
    orbit.orbitPlanetRadius = 0.20;
    orbit.orbitTargetRadius = 0.62;
    orbit.orbitGoodBand = 0.08;
    orbit.orbitPerfectBand = 0.04;
    orbit.instrumentThrottle = 0.15;
    const auto orbitLow = thrustSize(rocket::SceneComposerTestAccess::orbitPacket(composer, orbit), 0.96F);
    orbit.instrumentThrottle = 0.85;
    const auto orbitHigh = thrustSize(rocket::SceneComposerTestAccess::orbitPacket(composer, orbit), 0.96F);
    assert(orbitHigh.first > orbitLow.first);
    assert(orbitHigh.second > orbitLow.second);

    RenderSnapshot launch;
    launch.screen = rocket::Screen::Launch;
    launch.poweredFlight = true;
    launch.currentMultiplier = 1.2;
    launch.targetMultiplier = 2.0;
    launch.launchThrottle = 0.15;
    const auto launchLow = thrustSize(rocket::SceneComposerTestAccess::rocketPacket(composer, launch), 0.98F);
    launch.launchThrottle = 0.85;
    const auto launchHigh = thrustSize(rocket::SceneComposerTestAccess::rocketPacket(composer, launch), 0.98F);
    assert(launchHigh.first > launchLow.first);
    assert(launchHigh.second > launchLow.second);
}

void testCampaignIntroductionDrawsHeroicCapybara()
{
    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
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
    assert(packet.draws.size() == 3U);
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
    assert(packet.draws[1].atlasPage == rocket::sceneAtlasPageForTexture(TextureId::Moon));
    assert(packet.draws[1].instanceCount >= 82U);
    assert(packet.draws[2].drawType == SceneDrawType::InstancedQuad);
    assert(packet.draws[2].pipeline == PipelineClass::Textured);
    assert(packet.draws[2].coordinateSpace == CoordinateSpace::World);
    assert(packet.draws[2].atlasPage == rocket::sceneAtlasPageForTexture(TextureId::Earth));

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
    assert(logicalDraws[0] != logicalDraws[1]);
    assert(logicalDraws[1] != logicalDraws[2]);
    // RocketOpen and RocketClosed share a page, but the intervening rig page
    // is an order barrier, so the renderer must not merge them out of order.
    assert(logicalDraws[0] != logicalDraws[2]);
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

void testMiningLooseChunksAreVisibleWorldEntities()
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
        mining.looseChunks.clear();
        rocket::MiningLooseChunk chunk;
        chunk.material = material;
        chunk.x = 2.0;
        chunk.y = 2.0;
        chunk.velocityX = 0.4;
        chunk.velocityY = -0.2;
        chunk.cargoValue = 2;
        mining.looseChunks.push_back(chunk);
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

void testSurfaceScannerMarksUseMaterialSilhouettes()
{
    RenderSnapshot snapshot;
    snapshot.screen = rocket::Screen::SurfaceScan;
    snapshot.animationTime = 1.0;
    snapshot.surfaceScanPulses = 1;
    snapshot.surfaceScanMaxPulses = 3;
    snapshot.surfaceScanPreviewMarkers = {
        rocket::MiningCellMaterial::CommonOre,
        rocket::MiningCellMaterial::RareOre,
        rocket::MiningCellMaterial::ExoticVein,
    };
    snapshot.surfaceScanPreviewDepthOffsets = {0, 1, 2};

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    const ScenePacket& packet = composer.compose(snapshot);
    assert(containsMiningMaterialMarker(
        packet,
        rocket::MiningCellMaterial::CommonOre,
        {0.74F, 0.78F, 0.84F, 1.0F}));
    assert(containsMiningMaterialMarker(
        packet,
        rocket::MiningCellMaterial::RareOre,
        {1.0F, 0.74F, 0.24F, 1.0F}));
    assert(containsMiningMaterialMarker(
        packet,
        rocket::MiningCellMaterial::ExoticVein,
        {0.95F, 0.28F, 0.78F, 1.0F}));
}

void testSurfaceScanSuccessFanfareRespectsCameraShake()
{
    RenderSnapshot snapshot;
    snapshot.screen = rocket::Screen::SurfaceScan;
    snapshot.animationTime = 0.35;
    snapshot.surfaceScanSuccessFanfare = 1.0;
    snapshot.surfaceScanLastPulseGrade = rocket::SurfaceScanPulseGrade::Perfect;

    SceneComposer shakeComposer;
    shakeComposer.setViewport({1280, 800, 1280, 800, 1.0F});
    const auto shakenCenter = rocket::SceneComposerTestAccess::frameCenter(shakeComposer, snapshot);
    SceneComposer accessibleComposer;
    accessibleComposer.setViewport({1280, 800, 1280, 800, 1.0F});
    accessibleComposer.setCameraShakeEnabled(false);
    const auto stableCenter = rocket::SceneComposerTestAccess::frameCenter(accessibleComposer, snapshot);
    const float successShake = std::hypot(
        shakenCenter.first - stableCenter.first,
        shakenCenter.second - stableCenter.second);
    assert(successShake > 1.0F);

    const ScenePacket& packet = shakeComposer.compose(snapshot);
    assert(packet.draws.size() > 0);

    snapshot.surfaceScanSuccessFanfare = 0.0;
    snapshot.surfaceScanMissFanfare = 1.0;
    snapshot.surfaceScanLastPulseGrade = rocket::SurfaceScanPulseGrade::Miss;
    SceneComposer missComposer;
    missComposer.setViewport({1280, 800, 1280, 800, 1.0F});
    const auto missCenter = rocket::SceneComposerTestAccess::frameCenter(missComposer, snapshot);
    const float missShake = std::hypot(
        missCenter.first - stableCenter.first,
        missCenter.second - stableCenter.second);
    assert(missShake > successShake);
    const ScenePacket& missPacket = missComposer.compose(snapshot);
    assert(std::any_of(missPacket.instances.begin(), missPacket.instances.end(), [](const PackedSceneInstance& packed) {
        const Color color = rocket::unpackSceneInstance(packed).color;
        return std::abs(color.r - 0.72F) < 0.025F
            && std::abs(color.g - 0.015F) < 0.025F
            && std::abs(color.b - 0.015F) < 0.025F;
    }));
}

void testSurfaceScanUsesGoldForPerfectAndGreenForGood()
{
    const auto hasColor = [](const ScenePacket& packet, Color expected) {
        const auto matches = [expected](Color actual) {
            return std::abs(actual.r - expected.r) < 0.025F
                && std::abs(actual.g - expected.g) < 0.025F
                && std::abs(actual.b - expected.b) < 0.025F;
        };
        return std::any_of(packet.vertices.begin(), packet.vertices.end(), [matches](const PackedSceneVertex& packed) {
            const SceneVertex vertex = rocket::unpackSceneVertex(packed);
            return matches({vertex.r, vertex.g, vertex.b, vertex.a});
        }) || std::any_of(packet.instances.begin(), packet.instances.end(), [matches](const PackedSceneInstance& packed) {
            return matches(rocket::unpackSceneInstance(packed).color);
        });
    };

    RenderSnapshot snapshot;
    snapshot.screen = rocket::Screen::SurfaceScan;
    snapshot.animationTime = 0.35;
    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    const ScenePacket& windows = composer.compose(snapshot);
    assert(hasColor(windows, {0.18F, 0.92F, 0.40F, 1.0F}));
    assert(hasColor(windows, {1.0F, 0.74F, 0.16F, 1.0F}));

    snapshot.surfaceScanSuccessFanfare = 1.0;
    snapshot.surfaceScanLastPulseGrade = rocket::SurfaceScanPulseGrade::Perfect;
    const ScenePacket& perfect = composer.compose(snapshot);
    assert(hasColor(perfect, {1.0F, 0.80F, 0.24F, 1.0F}));

    snapshot.surfaceScanLastPulseGrade = rocket::SurfaceScanPulseGrade::Good;
    const ScenePacket& good = composer.compose(snapshot);
    assert(hasColor(good, {0.28F, 1.0F, 0.48F, 1.0F}));
}

void testSurfaceScanUsesDestinationAppropriateCompanions()
{
    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    composer.setTextureReady(TextureId::Earth, true);
    composer.setTextureReady(TextureId::Moon, true);
    composer.setTextureReady(TextureId::Jupiter, true);

    RenderSnapshot jupiter;
    jupiter.screen = rocket::Screen::SurfaceScan;
    jupiter.destinationTier = 3;
    const ScenePacket& jupiterPacket = composer.compose(jupiter);
    assert(std::any_of(jupiterPacket.draws.begin(), jupiterPacket.draws.end(), [](const SceneDraw& draw) {
        return draw.texture == TextureId::Jupiter;
    }));
    assert(std::any_of(jupiterPacket.draws.begin(), jupiterPacket.draws.end(), [](const SceneDraw& draw) {
        return draw.texture == TextureId::Moon;
    }));
    assert(std::none_of(jupiterPacket.draws.begin(), jupiterPacket.draws.end(), [](const SceneDraw& draw) {
        return draw.texture == TextureId::Earth;
    }));

    RenderSnapshot moon;
    moon.screen = rocket::Screen::SurfaceScan;
    moon.destinationTier = 1;
    const ScenePacket& moonPacket = composer.compose(moon);
    assert(std::any_of(moonPacket.draws.begin(), moonPacket.draws.end(), [](const SceneDraw& draw) {
        return draw.texture == TextureId::Earth;
    }));
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

void testMiningOrePaletteMakesCommonSilverAndRareGold()
{
    const Color commonTerrain =
        miningTerrainMaterialColor(rocket::MiningCellMaterial::CommonOre);
    const Color rareTerrain =
        miningTerrainMaterialColor(rocket::MiningCellMaterial::RareOre);
    assert(commonTerrain.b >= commonTerrain.g
        && commonTerrain.g >= commonTerrain.r
        && commonTerrain.b - commonTerrain.r < 0.12F);
    assert(rareTerrain.r > rareTerrain.g
        && rareTerrain.g > rareTerrain.b
        && rareTerrain.r - rareTerrain.b > 0.30F);

    SceneComposer commonComposer;
    const Color commonShimmer =
        rocket::SceneComposerTestAccess::miningOreSparkleColor(
            commonComposer,
            rocket::MiningCellMaterial::CommonOre);
    SceneComposer rareComposer;
    const Color rareShimmer =
        rocket::SceneComposerTestAccess::miningOreSparkleColor(
            rareComposer,
            rocket::MiningCellMaterial::RareOre);
    assert(std::abs(commonShimmer.r - commonShimmer.g) < 0.08F
        && std::abs(commonShimmer.g - commonShimmer.b) < 0.08F);
    assert(rareShimmer.r > rareShimmer.g
        && rareShimmer.g > rareShimmer.b
        && rareShimmer.r - rareShimmer.b > 0.50F);
}

void testMiningPickupTextUsesTypedColorsAndTwoSecondLifetime()
{
    const auto hasColor = [](const ScenePacket& packet, Color expected) {
        for (const PackedSceneInstance& packed : packet.instances) {
            const Color actual = rocket::unpackSceneInstance(packed).color;
            if (std::abs(actual.r - expected.r) < 0.025F
                && std::abs(actual.g - expected.g) < 0.025F
                && std::abs(actual.b - expected.b) < 0.025F
                && actual.a > 0.20F) {
                return true;
            }
        }
        return false;
    };
    const std::array<std::pair<rocket::MiningPickupKind, Color>, 5> expected {{
        {rocket::MiningPickupKind::CommonOre, {0.74F, 0.78F, 0.84F, 1.0F}},
        {rocket::MiningPickupKind::RareOre, {1.0F, 0.74F, 0.24F, 1.0F}},
        {rocket::MiningPickupKind::ExoticOre, {0.78F, 0.42F, 1.0F, 1.0F}},
        {rocket::MiningPickupKind::Fuel, {1.0F, 0.34F, 0.04F, 1.0F}},
        {rocket::MiningPickupKind::Oxygen, {0.48F, 0.88F, 1.0F, 1.0F}}
    }};
    for (const auto& [kind, color] : expected) {
        SceneComposer composer;
        const ScenePacket packet = rocket::SceneComposerTestAccess::miningPickupTextPacket(
            composer, kind, 1, 0.30F);
        assertValidDrawRanges(packet);
        assert(!packet.instances.empty());
        assert(hasColor(packet, color));
    }

    SceneComposer lateComposer;
    const ScenePacket late = rocket::SceneComposerTestAccess::miningPickupTextPacket(
        lateComposer, rocket::MiningPickupKind::CommonOre, 1, 1.99F);
    assert(!late.instances.empty());

    SceneComposer expiredComposer;
    const ScenePacket expired = rocket::SceneComposerTestAccess::miningPickupTextPacket(
        expiredComposer, rocket::MiningPickupKind::CommonOre, 1, 2.01F);
    assert(expired.instances.empty() && expired.vertices.empty());
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
    assert(std::isfinite(first.centerX) && std::isfinite(first.centerY));
    assert(first.textured);
    assert(first.shape == SceneInstanceShape::Rectangle);
    assert(first.color.a > 0.99F);
    assert(std::hypot(first.axisXx, first.axisXy) > 0.01F);
    assert(std::hypot(first.axisYx, first.axisYy) > 0.01F);
    const float sceneAspect = static_cast<float>(firstPacket.logicalSceneClip.width)
        / static_cast<float>(std::max(1, firstPacket.logicalSceneClip.height));
    const float cellW = sceneAspect * 2.0F / static_cast<float>(snapshot.miningWidth);
    const float cellH = 1.82F / static_cast<float>(snapshot.miningHeight);
    assert(std::abs(first.centerX - (-sceneAspect + static_cast<float>(snapshot.miningDroneX) * cellW)) < 0.0005F);
    assert(std::abs(first.centerY - (0.82F - static_cast<float>(snapshot.miningDroneY) * cellH)) < 0.0005F);
    assert(first.axisYx < -0.01F);
    assert(std::abs(first.axisYy) < 0.001F);
    const float firstLength = std::hypot(first.axisYx, first.axisYy);
    const float firstDrillLength = std::hypot(firstDrill.axisYx, firstDrill.axisYy);
    assert((first.axisYx * firstDrill.axisYx + first.axisYy * firstDrill.axisYy)
        / (firstLength * firstDrillLength) > 0.999F);
    assertMiningDrillMounted(first, firstDrill);

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

void testMiningSurveyPulseWaveReachesItsRealRadiusThenFades()
{
    struct WaveMetrics {
        float radiusCells = 0.0F;
        float alpha = 0.0F;
    };
    const auto waveMetrics = [](const ScenePacket& packet) {
        constexpr float cellH = 1.82F / 12.0F;
        const float sceneAspect = static_cast<float>(packet.logicalSceneClip.width)
            / static_cast<float>(std::max(1, packet.logicalSceneClip.height));
        const float cellW = (sceneAspect * 2.0F) / 16.0F;
        const float originX = -sceneAspect + 8.0F * cellW;
        const float originY = 0.82F - 6.0F * cellH;
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
        constexpr float cellH = 1.82F / 12.0F;
        const float sceneAspect = static_cast<float>(packet.logicalSceneClip.width)
            / static_cast<float>(std::max(1, packet.logicalSceneClip.height));
        const float cellW = (sceneAspect * 2.0F) / 16.0F;
        const float centerX = -sceneAspect + static_cast<float>(cellX) * cellW + cellW * 0.5F;
        const float centerY = 0.82F - static_cast<float>(cellY) * cellH - cellH * 0.5F;
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
    assert(std::hypot(rigDeltaX, rigDeltaY) > 0.001F);
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
    assert(std::hypot(extendedDrill.axisYx, extendedDrill.axisYy)
        > std::hypot(shortDrill.axisYx, shortDrill.axisYy) + 0.001F);
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

void testPoiGuidanceUsesOneDynamicBouncingArrow()
{
    const auto arrowInstance = [](const ScenePacket& packet) {
        const rocket::SceneAtlasUvRect expected = rocket::mapSceneAtlasUvRect(
            TextureId::PoiGuidanceArrow, 0.0F, 0.0F, 1.0F, 1.0F);
        assert(expected.valid);
        constexpr float tolerance = 2.0F / 65535.0F;
        for (const SceneDraw& draw : packet.draws) {
            if (draw.texture != TextureId::PoiGuidanceArrow) {
                continue;
            }
            assert(draw.drawType == SceneDrawType::InstancedQuad);
            for (std::size_t index = 0; index < draw.instanceCount; ++index) {
                const SceneInstance instance =
                    rocket::unpackSceneInstance(packet.instances[draw.firstInstance + index]);
                if (instance.textured &&
                    std::abs(instance.u0 - expected.u0) <= tolerance &&
                    std::abs(instance.v0 - expected.v0) <= tolerance &&
                    std::abs(instance.u1 - expected.u1) <= tolerance &&
                    std::abs(instance.v1 - expected.v1) <= tolerance) {
                    return instance;
                }
            }
        }
        assert(false && "Expected POI guidance arrow draw.");
        return SceneInstance{};
    };

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    composer.setTextureReady(TextureId::PoiGuidanceArrow, true);
    const ScenePacket atRest = rocket::SceneComposerTestAccess::poiGuidancePacket(
        composer, "ARTIFACT", rocket::PoiGuidanceKind::Artifact, 0.0, -1.0F);
    const SceneInstance restArrow = arrowInstance(atRest);
    assert(atRest.vertices.size() > 200U);

    const ScenePacket quarterSecond = rocket::SceneComposerTestAccess::poiGuidancePacket(
        composer, "BOSS", rocket::PoiGuidanceKind::Boss, 0.25, -1.0F);
    const SceneInstance bouncedArrow = arrowInstance(quarterSecond);
    assert(bouncedArrow.centerY > restArrow.centerY + 0.01F);

    const ScenePacket upward = rocket::SceneComposerTestAccess::poiGuidancePacket(
        composer, "SHIP", rocket::PoiGuidanceKind::Ship, 0.0, 1.0F);
    const SceneInstance upwardArrow = arrowInstance(upward);
    assert(restArrow.axisYy > 0.0F);
    assert(upwardArrow.axisYy < 0.0F);

    RenderSnapshot push;
    push.screen = rocket::Screen::SurfacePush;
    push.animationTime = 0.5;
    push.surfacePushSteps = 2;
    push.surfacePushMaxSteps = 4;
    push.surfacePushRewardMarkers = {
        rocket::MiningCellMaterial::CommonOre,
        rocket::MiningCellMaterial::RareOre,
        rocket::MiningCellMaterial::ExoticVein,
        rocket::MiningCellMaterial::ArtifactCache
    };
    push.surfacePushRewardDepthOffsets = {1, 1, 2, 2};
    push.surfacePushForecastMarkers = {
        rocket::MiningCellMaterial::CommonOre,
        rocket::MiningCellMaterial::RareOre
    };
    push.surfacePushForecastDepthOffsets = {3, 4};
    const ScenePacket pushPacket = composer.compose(push);
    (void)arrowInstance(pushPacket);
    const auto hasMarkerColor = [&](float red, float green, float blue) {
        return std::any_of(
            pushPacket.vertices.begin(),
            pushPacket.vertices.end(),
            [&](const PackedSceneVertex& packed) {
                const SceneVertex vertex = rocket::unpackSceneVertex(packed);
                return std::abs(vertex.r - red) < 0.02F &&
                    std::abs(vertex.g - green) < 0.02F &&
                    std::abs(vertex.b - blue) < 0.02F;
            });
    };
    assert(hasMarkerColor(0.74F, 0.78F, 0.84F));
    assert(hasMarkerColor(1.0F, 0.58F, 0.18F));
    assert(hasMarkerColor(0.78F, 0.52F, 1.0F));
}

void testSurfacePushHostileStepCountsStayBounded()
{
    // Gameplay can only produce 0..6 Dig steps. These values model a damaged
    // native snapshot and must not turn the renderer into an unbounded packet
    // producer before it can present the next frame.
    const std::array<std::pair<int, int>, 3> hostileCounts {{
        {std::numeric_limits<int>::max(), 4},
        {2, std::numeric_limits<int>::max()},
        {std::numeric_limits<int>::min(), std::numeric_limits<int>::min()}
    }};

    for (const auto [steps, maxSteps] : hostileCounts) {
        SceneComposer composer;
        composer.setViewport({1280, 800, 1280, 800, 1.0F});

        // Establish the normal pre-Dig frame first so the hostile positive
        // step case also exercises the post-action burst path.
        RenderSnapshot baseline;
        baseline.screen = rocket::Screen::SurfacePush;
        baseline.animationTime = 1.0;
        baseline.surfacePushSteps = 1;
        baseline.surfacePushMaxSteps = 4;
        (void)composer.compose(baseline);

        RenderSnapshot hostile = baseline;
        hostile.animationTime += 1.0 / 60.0;
        hostile.surfacePushSteps = steps;
        hostile.surfacePushMaxSteps = maxSteps;
        hostile.destinationTier = std::numeric_limits<int>::max();
        const ScenePacket& packet = composer.compose(hostile);

        assertValidDrawRanges(packet);
        assert(packet.surfacePushInputClamped);
        assert(packet.surfacePushRawSteps == steps);
        assert(packet.surfacePushRawMaxSteps == maxSteps);
        assert(packet.droppedFrameInstances == 0U);
        assert(packet.instances.size() <= 1024U);
        assert(packet.vertices.size() <= 4096U);
    }
}

void testSurfacePushSecondDigFrameCompletes()
{
    // GCC Release previously optimized the reached/pending rung color branch
    // into a loop that repeated rung 2 forever. Reproduce the exact Deck state
    // transition so this test times out instead of silently regressing.
    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});

    RenderSnapshot push;
    push.screen = rocket::Screen::SurfacePush;
    push.destinationTier = 1;
    push.animationTime = 2.0;
    push.surfacePushSteps = 1;
    push.surfacePushMaxSteps = 4;
    push.surfacePushMaterials = {1, 1, 0};
    (void)composer.compose(push);

    push.animationTime = 2.29953163;
    push.surfacePushSteps = 2;
    push.surfacePushMaterials = {1, 2, 0};
    push.surfacePushArtifacts = 1;
    const ScenePacket& secondDig = composer.compose(push);

    assertValidDrawRanges(secondDig);
    assert(!secondDig.surfacePushInputClamped);
    assert(secondDig.surfacePushRawSteps == 2);
    assert(secondDig.surfacePushRawMaxSteps == 4);
    assert(secondDig.droppedFrameInstances == 0U);
    assert(secondDig.instances.size() <= 1024U);
    assert(secondDig.vertices.size() <= 27'000U);
}

void testSurfacePushTerrainGuardBoundsInvalidAspect()
{
    const std::array<float, 2> hostileAspects {{
        1'000'000.0F,
        std::numeric_limits<float>::infinity()
    }};

    for (const float aspect : hostileAspects) {
        SceneComposer composer;
        composer.setViewport({1280, 800, 1280, 800, 1.0F});
        RenderSnapshot snapshot;
        snapshot.screen = rocket::Screen::SurfacePush;
        snapshot.surfacePushSteps = 1;
        snapshot.surfacePushMaxSteps = 4;

        const ScenePacket packet = rocket::SceneComposerTestAccess::surfacePushPacketWithAspect(
            composer,
            snapshot,
            aspect);
        assertValidDrawRanges(packet);
        // A 14:1 visual aspect tops out below 27k terrain vertices. This
        // makes a malformed viewport incapable of requesting the old massive
        // scratch-vector reserve.
        assert(packet.vertices.size() <= 27'000U);
        assert(packet.instances.size() <= 1024U);
    }
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

void testLunarImpactCinematicUsesExplosionFramesAndAccessibleShake()
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
    impact.screen = rocket::Screen::Launch;
    impact.destinationTier = 1;
    impact.travelProgress = 1.0;
    impact.launchLunarImpactActive = true;
    impact.launchLunarImpactElapsed = 0.04;
    impact.animationTime = impact.launchLunarImpactElapsed;

    SceneComposer composer;
    composer.setViewport({1280, 800, 1280, 800, 1.0F});
    composer.setTextureReady(TextureId::RocketClosed, true);
    composer.setTextureReady(TextureId::Explosion, true);
    const ScenePacket& contact = composer.compose(impact);
    assert(hasTextureFrame(contact, TextureId::RocketClosed, 0));
    assert(explosionFrame(contact) < 0);
    const std::size_t contactDrawCount = contact.draws.size();

    impact.launchLunarImpactElapsed = 0.09;
    impact.animationTime = impact.launchLunarImpactElapsed;
    const ScenePacket& firstBlast = composer.compose(impact);
    assert(!hasTextureFrame(firstBlast, TextureId::RocketClosed, 0));
    assert(explosionFrame(firstBlast) == 0);
    const std::size_t firstBlastDrawCount = firstBlast.draws.size();
    assert(firstBlastDrawCount > contactDrawCount);

    const double frameDuration =
        (rocket::tuning::session::lunarImpactExplosionEndSeconds -
            rocket::tuning::session::lunarImpactHoldSeconds) /
        8.0;
    for (int frame = 0; frame < 8; ++frame) {
        impact.launchLunarImpactElapsed =
            rocket::tuning::session::lunarImpactHoldSeconds +
            (static_cast<double>(frame) + 0.5) * frameDuration;
        impact.animationTime = impact.launchLunarImpactElapsed;
        const ScenePacket& blastFrame = composer.compose(impact);
        assert(explosionFrame(blastFrame) == frame);
    }

    RenderSnapshot still = impact;
    still.launchLunarImpactElapsed = 0.12;
    still.animationTime = still.launchLunarImpactElapsed;
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
    ordinaryImpact.launchLunarImpactActive = false;
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

    lunarResult.lastLaunchFailureCause = rocket::LaunchFailureCause::FuelExhausted;
    const ScenePacket& genericDestroyed = composer.compose(lunarResult);
    assert(explosionFrame(genericDestroyed) >= 0);
}

} // namespace

int main()
{
#if defined(_MSC_VER) && defined(_DEBUG)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    testUiViewportLayoutGeometry();
    testMiningViewportReservesBothHudLanes();
    testScreenSurfaceMapping();
    testSceneComposerUsesResolvedSceneRect();
    testCompletedFlybyAndOrbitUseFullscreenSceneSurface();
    testLogicalSceneClipScalesToFramebuffer();
    testPackedVertexConversion();
    testLaunchDestinationGateUsesCorridorEndpoints();
    testOrbitGuideBandsHighlightActiveZone();
    testFlybyGuideBandsHighlightActiveZone();
    testTransferAssistLaunchUsesItsSourceBody();
    testManifestAndLogicalTextureMapping();
    testEnemyThemesAndAnimationPriorityUseTheSharedSpriteContract();
    testFlightInstrumentClusterUsesAtlasNeedlesAndBlinkingWarning();
    testFlybySteeringTriangleAndThrustFlameRemainDistinct();
    testLaunchUsesAttachedFlameAndSideSteeringTriangleOnly();
    testFlightPlumesScaleContinuouslyWithThrottle();
    testCampaignIntroductionDrawsHeroicCapybara();
    testPolygonInstanceMatchesTriangleFan();
    testOrderedBatchingAndWideLineInstancing();
    testUniformAndGradientLineOrdering();
    testAtlasPageBatchingAcrossLogicalTextures();
    testMiningEVAUsesDedicatedTextureWithoutFallback();
    testMiningActiveAnchorOwnsDefenseEffects();
    testMiningLooseChunksAreVisibleWorldEntities();
    testMiningCellsAndScannerMarksUseMaterialSilhouettes();
    testSurfaceScannerMarksUseMaterialSilhouettes();
    testSurfaceScanSuccessFanfareRespectsCameraShake();
    testSurfaceScanUsesGoldForPerfectAndGreenForGood();
    testSurfaceScanUsesDestinationAppropriateCompanions();
    testSceneTransitionFadesEverySceneToBlack();
    testMiningOrePaletteMakesCommonSilverAndRareGold();
    testMiningPickupTextUsesTypedColorsAndTwoSecondLifetime();
    testMiningRigSlerpsVerticalDuringExtraction();
    testMiningRigStaysVisibleAndTracksHeading();
    testMiningCollisionIndicatorMarksTheContactedEdge();
    testMiningSurveyPulseRechargeRingPersistsWhenReady();
    testMiningSurveyPulseWaveReachesItsRealRadiusThenFades();
    testMiningSurveyPulseProgressivelyRevealsNewTerrain();
    testMiningRigDrillStaysMountedThroughRecoilAndExtension();
    testFrameViewsKeepAuthoritativeEnemyIndices();
    testHazardDroneTransitShimmerAndAssistantBeams();
    testMiningTerrainPersistentStreamInvalidation();
    testPoiGuidanceUsesOneDynamicBouncingArrow();
    testSurfacePushHostileStepCountsStayBounded();
    testSurfacePushSecondDigFrameCompletes();
    testSurfacePushTerrainGuardBoundsInvalidAspect();
    testLevelUpFanfareGeometryAndAccessibleShake();
    testLunarImpactCinematicUsesExplosionFramesAndAccessibleShake();
    return 0;
}
