#pragma once

#include "input/ControllerInput.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>

namespace rocket {

struct UiFocusRect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

inline std::optional<std::size_t> directionalFocusTarget(
    std::span<const UiFocusRect> targets,
    std::size_t currentIndex,
    UiDirection direction)
{
    if (currentIndex >= targets.size() || direction == UiDirection::Count) {
        return std::nullopt;
    }

    const UiFocusRect& current = targets[currentIndex];
    const float currentCenterX = (current.left + current.right) * 0.5f;
    const float currentCenterY = (current.top + current.bottom) * 0.5f;
    const bool horizontal = direction == UiDirection::Left || direction == UiDirection::Right;
    const float directionSign = direction == UiDirection::Left || direction == UiDirection::Up ? -1.0f : 1.0f;

    std::optional<std::size_t> bestIndex;
    float bestScore = std::numeric_limits<float>::max();
    for (std::size_t index = 0; index < targets.size(); ++index) {
        if (index == currentIndex) {
            continue;
        }

        const UiFocusRect& candidate = targets[index];
        const float candidateCenterX = (candidate.left + candidate.right) * 0.5f;
        const float candidateCenterY = (candidate.top + candidate.bottom) * 0.5f;
        const float primary = directionSign * (horizontal
            ? candidateCenterX - currentCenterX
            : candidateCenterY - currentCenterY);
        if (primary <= 1.0f) {
            continue;
        }

        const float secondary = std::abs(horizontal
            ? candidateCenterY - currentCenterY
            : candidateCenterX - currentCenterX);
        const float currentSecondarySize = horizontal
            ? std::max(0.0f, current.bottom - current.top)
            : std::max(0.0f, current.right - current.left);
        const float candidateSecondarySize = horizontal
            ? std::max(0.0f, candidate.bottom - candidate.top)
            : std::max(0.0f, candidate.right - candidate.left);

        // Do not let a tiny offset on the requested axis jump focus to a
        // control that is visually in another row or column.
        const float coneAllowance =
            primary * 1.5f + (currentSecondarySize + candidateSecondarySize) * 0.25f + 8.0f;
        if (secondary > coneAllowance) {
            continue;
        }

        const float secondaryGap = horizontal
            ? std::max({0.0f, current.top - candidate.bottom, candidate.top - current.bottom})
            : std::max({0.0f, current.left - candidate.right, candidate.left - current.right});
        // Horizontal movement stays inside a visible row. Vertical movement
        // may bridge staggered card columns and centered footer actions.
        const float laneGapAllowance = horizontal ? 12.0f : 160.0f;
        if (secondaryGap > laneGapAllowance) {
            continue;
        }
        const float score = primary + secondaryGap * 4.0f + secondary * 0.25f;
        if (score < bestScore) {
            bestScore = score;
            bestIndex = index;
        }
    }
    return bestIndex;
}

} // namespace rocket
