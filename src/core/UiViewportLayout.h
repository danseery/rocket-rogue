#pragma once

#include "core/GameTypes.h"

#include <algorithm>
#include <cstdint>

namespace rocket {

// UI viewport geometry uses logical pixels with a top-left origin. Keeping the
// contract platform-neutral lets native RmlUi, the browser shell, rendering,
// and hit testing agree on one protected scene region.
struct UiRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool operator==(const UiRect&) const = default;
};

enum class UiLayoutClass {
    Fullscreen,
    LandscapeRail,
    BottomDock,
    MiningHud
};

enum class UiSurfaceKind {
    Fullscreen,
    PersistentPanel,
    Mining
};

struct UiViewportLayout {
    UiRect sceneRect;
    UiRect panelRect;
    // Mining alone reserves a second information lane above the scene. Other
    // surfaces leave this empty so the existing rail/dock panel contract is
    // unchanged.
    UiRect topPanelRect;
    UiRect hudSafeRect;
    UiLayoutClass layoutClass = UiLayoutClass::Fullscreen;
};

inline constexpr int kUiCompactMargin = 12;
inline constexpr int kUiWideMargin = 16;
inline constexpr int kUiWideViewportThreshold = 1600;
inline constexpr int kUiMinimumSceneWidth = 640;
inline constexpr int kUiRailWidthPercent = 24;
inline constexpr int kUiRailMinWidth = 280;
inline constexpr int kUiRailMaxWidth = 340;
inline constexpr int kUiDockHeightPercent = 28;
inline constexpr int kUiDockMinHeight = 168;
inline constexpr int kUiDockMaxHeight = 220;

// Mining reserves a compact status lane above the terrain and a larger
// command lane below it. These bounded percentages keep both useful from the
// 720p baseline through 4K without allowing either lane to dominate the view.
inline constexpr int kUiMiningTopHudHeightPercent = 10;
inline constexpr int kUiMiningTopHudMinHeight = 64;
inline constexpr int kUiMiningTopHudMaxHeight = 88;
inline constexpr int kUiMiningBottomHudHeightPercent = 15;
inline constexpr int kUiMiningBottomHudMinHeight = 96;
inline constexpr int kUiMiningBottomHudMaxHeight = 128;

inline constexpr int uiRectRight(const UiRect& rect) noexcept
{
    return rect.x + rect.width;
}

inline constexpr int uiRectBottom(const UiRect& rect) noexcept
{
    return rect.y + rect.height;
}

inline constexpr bool uiRectsIntersect(const UiRect& left, const UiRect& right) noexcept
{
    return left.width > 0 && left.height > 0 && right.width > 0 && right.height > 0
        && left.x < uiRectRight(right) && uiRectRight(left) > right.x
        && left.y < uiRectBottom(right) && uiRectBottom(left) > right.y;
}

inline constexpr bool uiRectContains(const UiRect& outer, const UiRect& inner) noexcept
{
    return inner.x >= outer.x && inner.y >= outer.y
        && uiRectRight(inner) <= uiRectRight(outer)
        && uiRectBottom(inner) <= uiRectBottom(outer);
}

inline constexpr UiRect clampUiRect(UiRect rect, int viewportWidth, int viewportHeight) noexcept
{
    const int width = std::max(0, viewportWidth);
    const int height = std::max(0, viewportHeight);
    const std::int64_t rawRight = static_cast<std::int64_t>(rect.x) + std::max(0, rect.width);
    const std::int64_t rawBottom = static_cast<std::int64_t>(rect.y) + std::max(0, rect.height);
    const int left = std::clamp(rect.x, 0, width);
    const int top = std::clamp(rect.y, 0, height);
    const int right = static_cast<int>(std::clamp<std::int64_t>(rawRight, left, width));
    const int bottom = static_cast<int>(std::clamp<std::int64_t>(rawBottom, top, height));
    return {left, top, right - left, bottom - top};
}

inline constexpr UiRect insetUiRect(UiRect rect, int inset) noexcept
{
    const int safeInsetX = std::min(std::max(0, inset), rect.width / 2);
    const int safeInsetY = std::min(std::max(0, inset), rect.height / 2);
    return {
        rect.x + safeInsetX,
        rect.y + safeInsetY,
        rect.width - safeInsetX * 2,
        rect.height - safeInsetY * 2
    };
}

inline constexpr int roundedPercent(int value, int percent) noexcept
{
    const std::int64_t positiveValue = std::max(0, value);
    return static_cast<int>((positiveValue * percent + 50) / 100);
}

inline constexpr UiSurfaceKind uiSurfaceKindForScreen(Screen screen) noexcept
{
    switch (screen) {
    case Screen::Hangar:
    case Screen::Navigation:
    case Screen::Research:
    case Screen::ArrivalOps:
    case Screen::SurfaceExpedition:
    case Screen::SurfaceUpgrade:
    case Screen::Upgrade:
    case Screen::Legacy:
    case Screen::Results:
    case Screen::ArrivalFanfare:
    case Screen::StoryBriefing:
    case Screen::DroneOps:
        return UiSurfaceKind::Fullscreen;
    case Screen::Mining:
        return UiSurfaceKind::Mining;
    default:
        return UiSurfaceKind::PersistentPanel;
    }
}

inline constexpr UiViewportLayout resolveUiViewportLayout(
    int viewportWidth,
    int viewportHeight,
    UiSurfaceKind surface) noexcept
{
    const int width = std::max(0, viewportWidth);
    const int height = std::max(0, viewportHeight);
    const UiRect viewport {0, 0, width, height};
    const int margin = width < kUiWideViewportThreshold ? kUiCompactMargin : kUiWideMargin;
    const int gap = margin;

    if (surface == UiSurfaceKind::Fullscreen) {
        return {
            viewport,
            {},
            {},
            insetUiRect(viewport, margin),
            UiLayoutClass::Fullscreen
        };
    }

    if (surface == UiSurfaceKind::Mining) {
        // Mining owns two compact HUD lanes. panelRect identifies the bottom
        // command lane and topPanelRect identifies the status lane above the
        // scene. The terrain and its hit targets live wholly in between.
        const int topHudHeight = std::clamp(
            roundedPercent(height, kUiMiningTopHudHeightPercent),
            kUiMiningTopHudMinHeight,
            kUiMiningTopHudMaxHeight);
        const int bottomHudHeight = std::clamp(
            roundedPercent(height, kUiMiningBottomHudHeightPercent),
            kUiMiningBottomHudMinHeight,
            kUiMiningBottomHudMaxHeight);
        UiRect scene {
            margin,
            margin + topHudHeight + gap,
            width - margin * 2,
            height - margin * 2 - topHudHeight - bottomHudHeight - gap * 2
        };
        UiRect panel {
            margin,
            height - margin - bottomHudHeight,
            width - margin * 2,
            bottomHudHeight
        };
        UiRect topPanel {
            margin,
            margin,
            width - margin * 2,
            topHudHeight
        };
        scene = clampUiRect(scene, width, height);
        panel = clampUiRect(panel, width, height);
        topPanel = clampUiRect(topPanel, width, height);
        return {
            scene,
            panel,
            topPanel,
            insetUiRect(scene, margin),
            UiLayoutClass::MiningHud
        };
    }

    const int railWidth = std::clamp(
        roundedPercent(width, kUiRailWidthPercent),
        kUiRailMinWidth,
        kUiRailMaxWidth);
    const int remainingSceneWidth = width - margin * 2 - gap - railWidth;
    if (remainingSceneWidth >= kUiMinimumSceneWidth) {
        UiRect panel {margin, margin, railWidth, height - margin * 2};
        UiRect scene {
            margin + railWidth + gap,
            margin,
            remainingSceneWidth,
            height - margin * 2
        };
        panel = clampUiRect(panel, width, height);
        scene = clampUiRect(scene, width, height);
        return {
            scene,
            panel,
            {},
            insetUiRect(scene, margin),
            UiLayoutClass::LandscapeRail
        };
    }

    const int dockHeight = std::clamp(
        roundedPercent(height, kUiDockHeightPercent),
        kUiDockMinHeight,
        kUiDockMaxHeight);
    UiRect panel {
        margin,
        height - margin - dockHeight,
        width - margin * 2,
        dockHeight
    };
    UiRect scene {
        margin,
        margin,
        width - margin * 2,
        height - margin * 2 - gap - dockHeight
    };
    panel = clampUiRect(panel, width, height);
    scene = clampUiRect(scene, width, height);
    return {
        scene,
        panel,
        {},
        insetUiRect(scene, margin),
        UiLayoutClass::BottomDock
    };
}

} // namespace rocket
