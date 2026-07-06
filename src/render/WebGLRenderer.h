#pragma once

#include "core/GameTypes.h"

#include <array>
#include <vector>

namespace rocket {

struct Color {
    float r = 1.0F;
    float g = 1.0F;
    float b = 1.0F;
    float a = 1.0F;
};

struct MiningCellSnapshot {
    int x = 0;
    int y = 0;
    int material = 0;
    double integrity = 1.0;
    bool revealed = false;
    bool hazard = false;
};

struct MiningEnemySnapshot {
    double x = 0.0;
    double y = 0.0;
    int type = 0;
    int affinity = 0;
    double health = 1.0;
    double maxHealth = 1.0;
    double effectRadius = 0.0;
    double attackCooldownSeconds = 0.0;
    bool active = true;
};

struct MiningProjectileSnapshot {
    double startX = 0.0;
    double startY = 0.0;
    double endX = 0.0;
    double endY = 0.0;
    double age = 0.0;
    double lifetime = 0.35;
    int team = 0;
    int sourceType = 0;
    int affinity = 0;
    bool critical = false;
};

struct MiningDamageNumberSnapshot {
    double x = 0.0;
    double y = 0.0;
    double amount = 0.0;
    double age = 0.0;
    double lifetime = 0.90;
    int team = 0;
    int kind = 0;
    bool critical = false;
    bool rigDamage = false;
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
};

struct FlybyTrailPointSnapshot {
    double x = 0.0;
    double y = 0.0;
};

struct RenderSnapshot {
    Screen screen = Screen::Hangar;
    LaunchResultType lastResult = LaunchResultType::None;
    double currentMultiplier = 1.0;
    double targetMultiplier = 1.5;
    double travelProgress = 0.0;
    double heat = 0.0;
    double warning = 0.0;
    double shipDamage = 0.0;
    int destinationTier = 0;
    int currentFrontierTier = 0;
    ArkCondition arkCondition = ArkCondition::NotFound;
    bool frontierTransfer = false;
    bool returningHome = false;
    bool poweredFlight = false;
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
    double miningDrillDirX = 0.0;
    double miningDrillDirY = 1.0;
    double miningHeat = 0.0;
    double miningOxygenSeconds = 15.0;
    double miningFuelBurnSeconds = 0.0;
    double miningDrillIntegrity = 1.0;
    double miningHazardDelta = 0.0;
    double miningContactIntensity = 0.0;
    double miningScannerPulse = 0.0;
    double miningScannerRadius = 5.5;
    double miningFailurePulse = 0.0;
    double miningRecoilX = 0.0;
    double miningRecoilY = 0.0;
    double miningBounce = 0.0;
    bool miningInputDrilling = false;
    bool miningTargetDrillable = false;
    bool miningDrilling = false;
    int miningSharedFuel = 0;
    int miningSharedFuelCapacity = 0;
    int miningCargo = 0;
    int miningSupportDroneCount = 0;
    int miningAttackDroneCount = 0;
    int miningDefenseDroneCount = 0;
    int miningSynergyCount = 0;
    int miningSignatureTier = 0;
    int miningSignatureStyle = 0;
    int miningTunedDroneCount = 0;
    std::vector<int> miningDroneRoles;
    std::vector<int> miningDroneUpgradeLevels;
    bool miningShieldActive = false;
    MaterialInventory miningMaterials;
    MiningArtifactSnapshot miningArtifact;
    std::vector<MiningCellSnapshot> miningCells;
    std::vector<MiningEnemySnapshot> miningEnemies;
    std::vector<MiningProjectileSnapshot> miningProjectiles;
    std::vector<MiningDamageNumberSnapshot> miningDamageNumbers;
    bool flybyActive = false;
    bool flybyCompleted = false;
    int flybyZone = 0;
    int flybyResult = 0;
    double flybyElapsed = 0.0;
    double flybyDuration = 1.0;
    double flybyShipX = 0.0;
    double flybyShipY = 0.0;
    double flybyVelocityX = 0.0;
    double flybyVelocityY = 0.0;
    double flybyInputX = 0.0;
    double flybyInputY = 0.0;
    double flybyDestinationX = 0.0;
    double flybyDestinationY = 0.0;
    double flybyIdealRadius = 0.0;
    double flybyGoodBand = 0.0;
    double flybyPerfectBand = 0.0;
    std::vector<FlybyTrailPointSnapshot> flybyTrailPoints;
    bool orbitActive = false;
    bool orbitCompleted = false;
    int orbitZone = 0;
    int orbitResult = 0;
    double orbitElapsed = 0.0;
    double orbitDuration = 1.0;
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
    std::vector<FlybyTrailPointSnapshot> orbitTrailPoints;
    bool surfaceScanActive = false;
    bool surfaceScanCompleted = false;
    bool surfaceScanBusted = false;
    int surfaceScanPulses = 0;
    int surfaceScanMaxPulses = 1;
    double surfaceScanSignal = 0.0;
    double surfaceScanInterference = 0.0;
    double surfaceScanBustRisk = 0.0;
    int surfaceScanCargo = 0;
    MaterialInventory surfaceScanMaterials;
    int surfaceScanArtifacts = 0;
    std::vector<MiningCellMaterial> surfaceScanPreviewMarkers;
    std::vector<int> surfaceScanPreviewDepthOffsets;
    bool surfacePushActive = false;
    bool surfacePushCompleted = false;
    bool surfacePushBusted = false;
    int surfacePushSteps = 0;
    int surfacePushMaxSteps = 1;
    int surfacePushDepthGain = 0;
    double surfacePushPressure = 0.0;
    double surfacePushCollapseRisk = 0.0;
    int surfacePushCargo = 0;
    MaterialInventory surfacePushMaterials;
    int surfacePushArtifacts = 0;
    std::vector<MiningCellMaterial> surfacePushRewardMarkers;
    std::vector<int> surfacePushRewardDepthOffsets;
    std::vector<MiningCellMaterial> surfacePushForecastMarkers;
    std::vector<int> surfacePushForecastDepthOffsets;
};

class WebGLRenderer {
public:
    bool initialize();
    void render(const RenderSnapshot& snapshot);

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
    void drawSprite(float cx, float cy, float w, float h, Color tint, int assetIndex, int frameIndex = 0, int frameCount = 1, bool worldSpace = true);
    void drawSpriteRotated(float cx, float cy, float w, float h, float forwardX, float forwardY, Color tint, int assetIndex, int frameIndex = 0, int frameCount = 1, bool worldSpace = true);
    std::vector<float>& scratchVertices(std::size_t reserveCount);
    void appendRect(std::vector<float>& vertices, float cx, float cy, float w, float h, Color color);
    void appendLine(std::vector<float>& vertices, float ax, float ay, float bx, float by, Color color);
    bool textureReady(int assetIndex);
    void warmTextures();
    void drawTelemetry(const RenderSnapshot& snapshot);
    void drawRocket(const RenderSnapshot& snapshot);
    void drawBackdrop(const RenderSnapshot& snapshot);
    void drawFlyby(const RenderSnapshot& snapshot);
    void drawOrbit(const RenderSnapshot& snapshot);
    void drawMining(const RenderSnapshot& snapshot);
    void drawSurfaceScan(const RenderSnapshot& snapshot);
    void drawSurfacePush(const RenderSnapshot& snapshot);
    void drawSolarBackground(const RenderSnapshot& snapshot, float alpha);
    void drawRoute(const RenderSnapshot& snapshot);
    void drawEllipseLine(float cx, float cy, float rx, float ry, Color color, int segments, float start, float end);
    void submit(const std::vector<float>& vertices, int primitive, bool textured = false, unsigned int texture = 0, bool worldSpace = true);
    void submitLines(const std::vector<float>& vertices, float width, bool worldSpace = true);

    unsigned int program_ = 0;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    int useTextureUniform_ = -1;
    int samplerUniform_ = -1;
    struct TextureAsset {
        const char* key = nullptr;
        const char* path = nullptr;
        unsigned int texture = 0;
        int width = 0;
        int height = 0;
        bool requested = false;
        bool ready = false;
    };

    struct MiningPickupBurst {
        float x = 0.0F;
        float y = 0.0F;
        int material = 0;
        int amount = 0;
        double startedAt = 0.0;
        float textOffsetX = 0.0F;
    };

    std::array<TextureAsset, 17> assets_ {};
    std::vector<float> vertices_;
    std::vector<float> projectedVertices_;
    std::vector<int> previousMiningMaterials_;
    MaterialInventory previousMiningInventory_;
    int previousMiningCargo_ = 0;
    std::vector<MiningPickupBurst> miningPickupBursts_;
    int previousMiningWidth_ = 0;
    int previousMiningHeight_ = 0;
    bool previousMiningActive_ = false;
    float sceneCssWidth_ = 1280.0F;
    float sceneCssHeight_ = 720.0F;
    float scenePixelLeft_ = 0.0F;
    float scenePixelRight_ = 1280.0F;
    float scenePixelCenterX_ = 640.0F;
    float scenePixelCenterY_ = 360.0F;
    float sceneWorldUnit_ = 360.0F;
    float sceneAspect_ = 16.0F / 9.0F;
    bool initialized_ = false;
};

} // namespace rocket
