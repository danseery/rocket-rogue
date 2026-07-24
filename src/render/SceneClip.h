#pragma once

#include "core/UiViewportLayout.h"

#include <algorithm>
#include <cmath>

namespace rocket {

// Framebuffer-pixel clip with a top-left origin. Vulkan consumes this directly;
// OpenGL only needs to flip the Y origin through openGlSceneScissorY().
struct FramebufferSceneClip {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool operator==(const FramebufferSceneClip&) const = default;

    bool empty() const noexcept
    {
        return width <= 0 || height <= 0;
    }
};

// Scale logical, top-left-origin scene geometry to the actual framebuffer.
// Independent X/Y ratios handle high-density and non-uniform drawable sizes.
// Bounds round outward so fractional density never clips the scene's edge.
inline FramebufferSceneClip resolveSceneFramebufferClip(
    UiRect logicalClip,
    int logicalWidth,
    int logicalHeight,
    int framebufferWidth,
    int framebufferHeight) noexcept
{
    const int safeLogicalWidth = std::max(0, logicalWidth);
    const int safeLogicalHeight = std::max(0, logicalHeight);
    const int safeFramebufferWidth = std::max(0, framebufferWidth);
    const int safeFramebufferHeight = std::max(0, framebufferHeight);
    if (safeLogicalWidth == 0 || safeLogicalHeight == 0
        || safeFramebufferWidth == 0 || safeFramebufferHeight == 0) {
        return {};
    }

    logicalClip = clampUiRect(logicalClip, safeLogicalWidth, safeLogicalHeight);
    const double scaleX = static_cast<double>(safeFramebufferWidth)
        / static_cast<double>(safeLogicalWidth);
    const double scaleY = static_cast<double>(safeFramebufferHeight)
        / static_cast<double>(safeLogicalHeight);
    const int left = std::clamp(
        static_cast<int>(std::floor(static_cast<double>(logicalClip.x) * scaleX)),
        0,
        safeFramebufferWidth);
    const int top = std::clamp(
        static_cast<int>(std::floor(static_cast<double>(logicalClip.y) * scaleY)),
        0,
        safeFramebufferHeight);
    const int right = std::clamp(
        static_cast<int>(std::ceil(static_cast<double>(uiRectRight(logicalClip)) * scaleX)),
        left,
        safeFramebufferWidth);
    const int bottom = std::clamp(
        static_cast<int>(std::ceil(static_cast<double>(uiRectBottom(logicalClip)) * scaleY)),
        top,
        safeFramebufferHeight);
    return {left, top, right - left, bottom - top};
}

inline int openGlSceneScissorY(
    const FramebufferSceneClip& clip,
    int framebufferHeight) noexcept
{
    return std::clamp(
        std::max(0, framebufferHeight) - clip.y - clip.height,
        0,
        std::max(0, framebufferHeight));
}

} // namespace rocket
