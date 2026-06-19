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
    bool frontierTransfer = false;
    bool returningHome = false;
    bool poweredFlight = false;
    double returnTurnProgress = 1.0;
    std::array<double, 12> telemetry {};
    std::array<double, 12> heatTelemetry {};
    int telemetryCount = 0;
    double animationTime = 0.0;
};

class WebGLRenderer {
public:
    bool initialize();
    void render(const RenderSnapshot& snapshot);

private:
    void beginFrame(const RenderSnapshot& snapshot);
    void drawRect(float cx, float cy, float w, float h, Color color);
    void drawLine(float ax, float ay, float bx, float by, Color color, float width = 1.0F);
    void drawTriangle(float ax, float ay, float bx, float by, float cx, float cy, Color color);
    void drawCircle(float cx, float cy, float radius, Color color, int segments = 36);
    void drawSprite(float cx, float cy, float w, float h, Color tint, int assetIndex, int frameIndex = 0, int frameCount = 1);
    std::vector<float>& scratchVertices(std::size_t reserveCount);
    void appendRect(std::vector<float>& vertices, float cx, float cy, float w, float h, Color color);
    void appendLine(std::vector<float>& vertices, float ax, float ay, float bx, float by, Color color);
    bool textureReady(int assetIndex);
    void warmTextures();
    void drawTelemetry(const RenderSnapshot& snapshot);
    void drawRocket(const RenderSnapshot& snapshot);
    void drawBackdrop(const RenderSnapshot& snapshot);
    void drawStars();
    void drawRoute(const RenderSnapshot& snapshot);
    void drawEllipseLine(float cx, float cy, float rx, float ry, Color color, int segments, float start, float end);
    void submit(const std::vector<float>& vertices, int primitive, bool textured = false, unsigned int texture = 0);
    void submitLines(const std::vector<float>& vertices, float width);

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
    std::array<TextureAsset, 6> assets_ {};
    std::vector<float> vertices_;
    bool initialized_ = false;
};

} // namespace rocket
