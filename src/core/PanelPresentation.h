#pragma once

#include "core/GameTypes.h"

#include <string>
#include <string_view>
#include <utility>

namespace rocket {

enum class PanelLayoutMode {
    ControlPanel,
    PhaseBoard
};

struct PanelMetricPresentation {
    std::string label;
    std::string value;
};

struct PanelButtonPresentation {
    std::string label;
    std::string actionId;
    std::string cssClass;
    bool enabled = false;
};

inline PanelMetricPresentation panelMetric(std::string_view label, std::string value)
{
    return {std::string(label), std::move(value)};
}

inline PanelButtonPresentation panelActionButton(std::string_view label, std::string_view actionId, std::string cssClass = "")
{
    return {std::string(label), std::string(actionId), std::move(cssClass), true};
}

inline PanelButtonPresentation disabledPanelButton(std::string_view label)
{
    return {std::string(label), {}, {}, false};
}

inline PanelLayoutMode panelLayoutMode(Screen screen)
{
    return screen == Screen::Launch ? PanelLayoutMode::ControlPanel : PanelLayoutMode::PhaseBoard;
}

inline bool usesPhaseBoard(Screen screen)
{
    return panelLayoutMode(screen) == PanelLayoutMode::PhaseBoard;
}

} // namespace rocket
