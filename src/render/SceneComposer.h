#pragma once

#include "render/RenderSnapshot.h"
#include "render/ScenePacket.h"

#include <array>
#include <cstddef>
#include <vector>

namespace rocket {

struct SceneViewport {
    int logicalWidth = 1280;
    int logicalHeight = 800;
    int drawableWidth = 1280;
    int drawableHeight = 800;
    float densityRatio = 1.0F;
};

// Stateful presentation frontend. It retains visual interpolation/burst state,
// but reads gameplay exclusively through RenderSnapshot and emits no API calls.
class SceneComposer {
public:
    SceneComposer();
    void setViewport(SceneViewport viewport) noexcept;
    void setPresentationTime(double seconds) noexcept;
    void setCameraShakeEnabled(bool enabled) noexcept;
    void setTextureReady(TextureId texture, bool ready) noexcept;
    const ScenePacket& compose(const RenderSnapshot& snapshot);
    void reset();

private:
    friend struct SceneComposerTestAccess;

    void beginFrame(const RenderSnapshot& snapshot);
    void drawRect(float cx, float cy, float w, float h, Color color, bool worldSpace = true);
    void drawLine(float ax, float ay, float bx, float by, Color color, float width = 1.0F, bool worldSpace = true);
    void drawTriangle(float ax, float ay, float bx, float by, float cx, float cy, Color color, bool worldSpace = true);
    void drawCircle(float cx, float cy, float radius, Color color, int segments = 36, bool worldSpace = true);
    void drawRadialGlow(float cx, float cy, float radius, Color centerColor, int segments = 48, bool worldSpace = true);
    void drawMiningOreSparkle(float cx, float cy, float unitSize, int material, float animationTime, float phaseSeed, float alphaScale = 1.0F);
    void drawMiningOreSparkleColor(float cx, float cy, float unitSize, Color glow, float animationTime, float phaseSeed, float alphaScale = 1.0F);
    void drawMiningPickupText(float cx, float cy, float unitSize, int material, int amount, float age);
    void drawMiningCombatText(float cx, float cy, float unitSize, int amount, float age, bool allied, bool critical, bool rigDamage, int kind);
    void drawMiningBankedText(float cx, float cy, float unitSize, float age);
    void drawSprite(float cx, float cy, float w, float h, Color tint, int assetIndex, int frameIndex = 0, int frameCount = 1, bool worldSpace = true);
    void drawSpriteRotated(float cx, float cy, float w, float h, float forwardX, float forwardY, Color tint, int assetIndex, int frameIndex = 0, int frameCount = 1, bool worldSpace = true);
    std::vector<SceneVertex>& scratchVertices(std::size_t reserveCount);
    void appendRect(std::vector<SceneVertex>& vertices, float cx, float cy, float w, float h, Color color);
    void appendLine(std::vector<SceneVertex>& vertices, float ax, float ay, float bx, float by, Color color);
    bool textureReady(int assetIndex) const noexcept;
    void drawTelemetry(const RenderSnapshot& snapshot);
    void drawRocket(const RenderSnapshot& snapshot);
    void drawTitleBackdrop(const RenderSnapshot& snapshot);
    void drawBackdrop(const RenderSnapshot& snapshot);
    void drawFlyby(const RenderSnapshot& snapshot);
    void drawOrbit(const RenderSnapshot& snapshot);
    void drawMining(const RenderSnapshot& snapshot);
    void drawSurfaceScan(const RenderSnapshot& snapshot);
    void drawSurfacePush(const RenderSnapshot& snapshot);
    void drawSolarBackground(const RenderSnapshot& snapshot, float alpha, bool animateFrames = true);
    void drawRoute(const RenderSnapshot& snapshot);
    void drawEllipseLine(float cx, float cy, float rx, float ry, Color color, int segments, float start, float end);
    void submit(
        const std::vector<SceneVertex>& vertices,
        TextureId texture = TextureId::None,
        CoordinateSpace coordinateSpace = CoordinateSpace::World,
        PipelineClass pipeline = PipelineClass::Solid,
        Color effectColor = {},
        std::array<float, 4> effectParams = {},
        std::array<float, 2> effectSize = {});
    void submitMiningTerrainRange(std::uint32_t firstVertex, std::uint32_t vertexCount);
    void submitInstance(
        const SceneInstance& instance,
        TextureId texture = TextureId::None,
        CoordinateSpace coordinateSpace = CoordinateSpace::World,
        PipelineClass pipeline = PipelineClass::Solid);
    void submitMiningTerrainInstanceRange(std::uint32_t firstInstance, std::uint32_t instanceCount);
    void appendDrawCommand(SceneDraw draw);
    void submitLines(const std::vector<SceneVertex>& vertices, float width, bool worldSpace = true);
    bool makeUniformLineInstance(
        SceneInstance& output,
        const SceneVertex& a,
        const SceneVertex& b,
        float width,
        CoordinateSpace coordinateSpace) const;
    bool calculateLineOffset(
        const SceneVertex& a,
        const SceneVertex& b,
        float width,
        CoordinateSpace coordinateSpace,
        float& offsetX,
        float& offsetY) const;
    void appendLineTriangles(
        std::vector<SceneVertex>& output,
        const SceneVertex& a,
        const SceneVertex& b,
        float width,
        CoordinateSpace coordinateSpace) const;
    void finalizePacket();

    struct MiningPickupBurst {
        float x = 0.0F;
        float y = 0.0F;
        int material = 0;
        int amount = 0;
        double startedAt = 0.0;
        float textOffsetX = 0.0F;
    };

    struct MiningTerrainCellPresentationState {
        MiningCellMaterial material = MiningCellMaterial::Empty;
        double maxToughness = 0.0;
        double remainingToughness = 0.0;
        MiningElementalAffinity hazardAffinity = MiningElementalAffinity::None;
        bool revealed = false;
        bool hazard = false;

        bool operator==(const MiningTerrainCellPresentationState&) const = default;
    };

    struct MiningTerrainScannerPresentationState {
        double x = 0.0;
        double y = 0.0;

        bool operator==(const MiningTerrainScannerPresentationState&) const = default;
    };

    struct MiningTerrainPresentationKey {
        int width = 0;
        int height = 0;
        int destinationTier = 0;
        float sceneAspect = 0.0F;
        double droneX = 0.0;
        double droneY = 0.0;
        float scannerPulse = 0.0F;
        float scannerRevealRadius = 0.0F;
        float scannerSweepRadius = 0.0F;

        bool operator==(const MiningTerrainPresentationKey&) const = default;
    };

    SceneViewport viewport_;
    ScenePacket packet_;
    std::array<bool, textureIndex(TextureId::Count)> textureReady_ {};
    std::vector<SceneVertex> vertices_;
    std::vector<SceneVertex> lineVertices_;
    std::vector<SceneVertex> frameVertices_;
    std::vector<PackedSceneVertex> packedFrameVertices_;
    std::vector<SceneVertex> miningTerrainVertices_;
    std::vector<PackedSceneVertex> packedMiningTerrainVertices_;
    std::vector<PackedSceneInstance> frameInstances_;
    std::vector<PackedSceneInstance> packedMiningTerrainInstances_;
    std::vector<SceneDraw> drawCommands_;
    std::vector<MiningTerrainCellPresentationState> miningTerrainCellStates_;
    std::vector<MiningTerrainScannerPresentationState> miningTerrainScannerStates_;
    MiningTerrainPresentationKey miningTerrainPresentationKey_;
    std::uint32_t miningBackdropFogInstanceCount_ = 0;
    std::uint32_t miningBaseTerrainInstanceCount_ = 0;
    std::uint64_t miningTerrainRevision_ = 0;
    bool miningTerrainCacheValid_ = false;
    bool miningTerrainStreamUsed_ = false;
    std::vector<int> currentMiningMaterials_;
    std::vector<int> previousMiningMaterials_;
    MaterialInventory previousMiningInventory_;
    MaterialInventory previousMiningStowedInventory_;
    int previousMiningCargo_ = 0;
    int previousMiningStowedCargo_ = 0;
    std::vector<const MiningMiniDroneAgent*> miningSurveyDrones_;
    std::vector<MiningPickupBurst> miningPickupBursts_;
    std::vector<MiningPickupBurst> miningPickupBurstScratch_;
    int previousMiningWidth_ = 0;
    int previousMiningHeight_ = 0;
    bool previousMiningActive_ = false;
    float miningVisualHeadingX_ = 0.0F;
    float miningVisualHeadingY_ = -1.0F;
    float miningVisualRecoilX_ = 0.0F;
    float miningVisualRecoilY_ = 0.0F;
    double miningVisualHeadingTime_ = -1.0;
    bool miningVisualHeadingInitialized_ = false;
    float sceneCssWidth_ = 1280.0F;
    float sceneCssHeight_ = 720.0F;
    float scenePixelLeft_ = 0.0F;
    float scenePixelRight_ = 1280.0F;
    float scenePixelCenterX_ = 640.0F;
    float scenePixelCenterY_ = 360.0F;
    float sceneWorldUnit_ = 360.0F;
    float sceneWorldUnitX_ = 360.0F;
    float sceneWorldUnitY_ = 360.0F;
    float sceneAspect_ = 16.0F / 9.0F;
    double presentationTimeSeconds_ = -1.0;
    bool cameraShakeEnabled_ = true;
};

} // namespace rocket
