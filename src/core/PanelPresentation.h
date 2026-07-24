#pragma once

#include "core/GameTypes.h"

#include <string>
#include <string_view>
#include <utility>

namespace rocket {

enum class PanelLayoutMode {
    ControlPanel,
    PhaseBoard,
    Fullscreen
};

enum class PanelVisualFamily {
    Management,
    Decision,
    LiveHud,
    SurfaceMinigame,
    MiningHud,
    Selection,
    ResultsModal,
    Fullscreen
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
    case Screen::DroneOps:
    case Screen::StoryBriefing:
        return PanelLayoutMode::Fullscreen;
    default:
        break;
    }
    return screen == Screen::Launch || screen == Screen::ArrivalFanfare || screen == Screen::Flyby || screen == Screen::Orbit
        ? PanelLayoutMode::ControlPanel
        : PanelLayoutMode::PhaseBoard;
}

inline bool usesPhaseBoard(Screen screen)
{
    return panelLayoutMode(screen) == PanelLayoutMode::PhaseBoard;
}

inline constexpr PanelVisualFamily panelVisualFamily(Screen screen) noexcept
{
    switch (screen) {
    case Screen::Hangar:
    case Screen::Navigation:
    case Screen::Research:
        return PanelVisualFamily::Management;
    case Screen::ArrivalOps:
    case Screen::SurfaceExpedition:
        return PanelVisualFamily::Decision;
    case Screen::Launch:
    case Screen::Flyby:
    case Screen::Orbit:
        return PanelVisualFamily::LiveHud;
    case Screen::SurfaceScan:
    case Screen::SurfacePush:
        return PanelVisualFamily::SurfaceMinigame;
    case Screen::Mining:
        return PanelVisualFamily::MiningHud;
    case Screen::SurfaceUpgrade:
    case Screen::Upgrade:
        return PanelVisualFamily::Selection;
    case Screen::Results:
    case Screen::ArrivalFanfare:
        return PanelVisualFamily::ResultsModal;
    case Screen::DroneOps:
        return PanelVisualFamily::Fullscreen;
    default:
        return PanelVisualFamily::Fullscreen;
    }
}

inline constexpr std::string_view panelVisualFamilyName(PanelVisualFamily family) noexcept
{
    switch (family) {
    case PanelVisualFamily::Management: return "management";
    case PanelVisualFamily::Decision: return "decision";
    case PanelVisualFamily::LiveHud: return "live-hud";
    case PanelVisualFamily::SurfaceMinigame: return "surface-minigame";
    case PanelVisualFamily::MiningHud: return "mining";
    case PanelVisualFamily::Selection: return "selection";
    case PanelVisualFamily::ResultsModal: return "results-modal";
    case PanelVisualFamily::Fullscreen: return "fullscreen";
    }
    return "fullscreen";
}

} // namespace rocket
