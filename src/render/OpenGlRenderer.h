#pragma once

#include "core/GameTypes.h"
#include "platform/AppServices.h"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

namespace rocket {

struct Color {
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
    float a = 1.0F;
};

struct MiningArtifactSnapshot {
    bool present = false;
    double x = 0.0;
    double y = 0.0;
    double health = 0.0;
    double maxHealth = 1.0;
    int kind = 0;
    int rewardType = 0;
    int state = 0;
    bool tethered = false;
    bool revealed = false;
    int gateType = 0;
    int gateState = 0;
};

struct RenderSnapshot {
    Screen screen = Screen::Hangar;
    // Presentation-only startup state. This does not participate in gameplay
    // or save data; it selects the lightweight title composition below RmlUi.
    bool titleScreen = false;
    LaunchResultType lastResult = LaunchResultType::None;
    double currentMultiplier = 1.0;
    double targetMultiplier = 1.5;
    double travelProgress = 0.0;
    double heat = 0.0;
    double warning = 0.0;
    double shipDamage = 0.0;
    int destinationTier = 0;
    int debugActOneCheckpoint = -1;
    ArkCondition arkCondition = ArkCondition::NotFound;
    bool frontierTransfer = false;
    bool returningHome = false;
    bool poweredFlight = false;
    bool preflightActive = false;
    double preflightProgress = 1.0;
    double launchShake = 0.0;
    double returnTurnProgress = 1.0;
    std::array<double, 12> telemetry {};
    std::array<double, 12> heatTelemetry {};
    int telemetryCount = 0;
    double animationTime = 0.0;
    int miningWidth = 0;
    int miningHeight = 0;
    double miningDroneX = 0.0;
    double miningDroneY = 0.0;
    double miningTargetX = 0.0;
    double miningTargetY = 0.0;
    double miningHeat = 0.0;
    double miningDrillIntegrity = 1.0;
    double miningDroneHealth = 1.0;
    double miningReturnZoneX = 0.0;
    double miningReturnZoneY = 0.0;
    bool miningAtReturnZone = false;
    double miningLoad = 0.0;
    double miningLoadSpeedMultiplier = 1.0;
    double miningContactIntensity = 0.0;
    double miningScannerPulse = 0.0;
    double miningScannerRadius = 5.5;
    double miningFailurePulse = 0.0;
    double miningRecoilX = 0.0;
    double miningRecoilY = 0.0;
    double miningMoveX = 0.0;
    double miningMoveY = 0.0;
    double miningHullDirX = 0.0;
    double miningHullDirY = 1.0;
    double miningBounce = 0.0;
    double miningBounceRelief = 0.0;
    bool miningTargetDrillable = false;
    bool miningDrilling = false;
    int miningCargo = 0;
    int miningStowedCargo = 0;
    bool miningExtractionActive = false;
    double miningExtractionProgress = 0.0;
    MaterialInventory miningMaterials;
    MaterialInventory miningStowedMaterials;
    MiningArtifactSnapshot miningArtifact;
    std::span<const MiningGateMarker> miningGateMarkers;
    std::span<const MiningCell> miningCells;
    std::span<const MiningEnemy> miningEnemies;
    std::span<const MiningMiniDroneAgent> miningMiniDrones;
    std::span<const MiningProjectileVisual> miningProjectiles;
    std::span<const MiningDamageNumber> miningDamageNumbers;
    bool flybyCompleted = false;
    int flybyZone = 0;
    int flybyResult = 0;
    double flybyShipX = 0.0;
    double flybyShipY = 0.0;
    double flybyVelocityX = 0.0;
    double flybyVelocityY = 0.0;
    double flybyInputY = 0.0;
    double flybyDestinationX = 0.0;
    double flybyDestinationY = 0.0;
    double flybyGoodBand = 0.0;
    double flybyPerfectBand = 0.0;
    std::span<const FlybyTrailPoint> flybyTrailPoints;
    bool orbitCompleted = false;
    int orbitZone = 0;
    int orbitResult = 0;
    double orbitProgress = 0.0;
    double orbitShipX = 0.0;
    double orbitShipY = 0.0;
    double orbitVelocityX = 0.0;
    double orbitVelocityY = 0.0;
    double orbitInputX = 0.0;
    double orbitInputY = 0.0;
    double orbitPlanetRadius = 0.0;
    double orbitTargetRadius = 0.0;
    double orbitGoodBand = 0.0;
    double orbitPerfectBand = 0.0;
    std::span<const FlybyTrailPoint> orbitTrailPoints;
    bool surfaceScanBusted = false;
    int surfaceScanPulses = 0;
    int surfaceScanMaxPulses = 1;
    double surfaceScanSignal = 0.0;
    double surfaceScanInterference = 0.0;
    double surfaceScanBustRisk = 0.0;
    MaterialInventory surfaceScanMaterials;
    int surfaceScanArtifacts = 0;
    std::vector<MiningCellMaterial> surfaceScanPreviewMarkers;
    std::vector<int> surfaceScanPreviewDepthOffsets;
    bool surfacePushBusted = false;
    int surfacePushSteps = 0;
    int surfacePushMaxSteps = 1;
    double surfacePushPressure = 0.0;
    double surfacePushCollapseRisk = 0.0;
    MaterialInventory surfacePushMaterials;
    int surfacePushArtifacts = 0;
    std::vector<MiningCellMaterial> surfacePushRewardMarkers;
    std::vector<int> surfacePushRewardDepthOffsets;
    std::vector<MiningCellMaterial> surfacePushForecastMarkers;
    std::vector<int> surfacePushForecastDepthOffsets;

    // These views are consumed synchronously by IGameRenderer::render(). The
    // authoritative run state must therefore outlive that call and must not be
    // mutated while the renderer is traversing the collections.
    void bindMiningFrameViews(const MiningRunState& mining) noexcept
    {
        miningGateMarkers = mining.gate.markers;
        miningCells = mining.terrain.cells;
        miningEnemies = mining.enemies;
        miningMiniDrones = mining.miniDrones;
        miningProjectiles = mining.combatProjectiles;
        miningDamageNumbers = mining.damageNumbers;
    }
};

class OpenGlRenderer final : public IGameRenderer {
public:
    OpenGlRenderer(IPlatformHost& host, ITextureSource& textures);

    bool initialize() override;
    void render(const RenderSnapshot& snapshot) override;
    void shutdown() override;
    void setPreferences(const AppPreferences& preferences) override;
    RendererDiagnostics diagnostics() const override;
    std::string_view description() const override;

private:
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
    std::vector<float>& scratchVertices(std::size_t reserveCount);
    void appendRect(std::vector<float>& vertices, float cx, float cy, float w, float h, Color color);
    void appendLine(std::vector<float>& vertices, float ax, float ay, float bx, float by, Color color);
    bool textureReady(int assetIndex);
    void warmTextures();
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
        const std::vector<float>& vertices,
        int primitive,
        bool textured = false,
        unsigned int texture = 0,
        bool worldSpace = true,
        int effectMode = 0,
        Color effectColor = {},
        std::array<float, 4> effectParams = {},
        std::array<float, 2> effectSize = {},
        float lineWidth = 1.0F);
    void submitLines(const std::vector<float>& vertices, float width, bool worldSpace = true);
    void flushCommands();

    IPlatformHost& host_;
    ITextureSource& textures_;
    unsigned int program_ = 0;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    int useTextureUniform_ = -1;
    int samplerUniform_ = -1;
    int effectModeUniform_ = -1;
    int effectColorUniform_ = -1;
    int effectParamsUniform_ = -1;
    int effectSizeUniform_ = -1;
    int positionScaleUniform_ = -1;
    int positionOffsetUniform_ = -1;
    struct TextureAsset {
        const char* key = nullptr;
        const char* path = nullptr;
        unsigned int texture = 0;
        int width = 0;
        int height = 0;
        bool requested = false;
        bool ready = false;
    };

    struct DrawCommand {
        std::size_t firstVertex = 0;
        std::size_t vertexCount = 0;
        int primitive = 0;
        bool textured = false;
        unsigned int texture = 0;
        bool worldSpace = true;
        float lineWidth = 1.0F;
        int effectMode = 0;
        Color effectColor;
        std::array<float, 4> effectParams {};
        std::array<float, 2> effectSize {};
    };

    struct MiningPickupBurst {
        float x = 0.0F;
        float y = 0.0F;
        int material = 0;
        int amount = 0;
        double startedAt = 0.0;
        float textOffsetX = 0.0F;
    };

    std::array<TextureAsset, 33> assets_ {};
    std::vector<float> vertices_;
    std::vector<float> frameVertices_;
    std::vector<DrawCommand> drawCommands_;
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
    RendererDiagnostics diagnostics_;
    bool cameraShakeEnabled_ = true;
    bool initialized_ = false;
};

} // namespace rocket
