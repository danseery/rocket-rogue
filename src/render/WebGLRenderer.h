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
    double returnTurnProgress = 1.0;
    std::array<double, 12> telemetry {};
    std::array<double, 12> heatTelemetry {};
    int telemetryCount = 0;
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
    void drawTelemetry(const RenderSnapshot& snapshot);
    void drawRocket(const RenderSnapshot& snapshot);
    void drawBackdrop(const RenderSnapshot& snapshot);
    void submit(const std::vector<float>& vertices, int primitive);

    unsigned int program_ = 0;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    bool initialized_ = false;
};

} // namespace rocket
