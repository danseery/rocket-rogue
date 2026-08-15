#pragma once

#include "core/PanelPresentation.h"

#include <string>
#include <string_view>
#include <vector>

namespace rocket {

// RmlUi templates own stable screen geometry. Dynamic content remains markup
// produced by the typed presentation helpers in GamePanel.
enum class PanelTemplateKind {
    LegacyRaw,
    Workspace,
    ControlPanel,
    SurfaceMinigame,
    Mining,
    Takeover,
    Results
};

enum class PanelSurfaceKind {
    None,
    SurfaceOps,
    SurfaceUpgrade,
    SurfaceScan,
    SurfacePush,
    Mining,
    DroneOps
};

enum class PanelInteractionMode {
    Standard,
    Realtime,
    Takeover
};

enum class PanelOverlayKind {
    None,
    PreflightLaunch,
    TelemetryLegend,
    SurfaceScanReadout
};

enum class ModalTone {
    Neutral,
    Positive,
    Warning,
    Negative
};

constexpr std::string_view modalToneCssClass(ModalTone tone) noexcept
{
    switch (tone) {
    case ModalTone::Positive: return "modal-tone-positive";
    case ModalTone::Warning: return "modal-tone-warning";
    case ModalTone::Negative: return "modal-tone-negative";
    case ModalTone::Neutral:
    default: return {};
    }
}

struct PanelPresentationMetadata {
    Screen screen = Screen::Hangar;
    PanelVisualFamily visualFamily = PanelVisualFamily::Fullscreen;
    PanelLayoutMode layoutMode = PanelLayoutMode::Fullscreen;
    PanelSurfaceKind surface = PanelSurfaceKind::None;
    PanelInteractionMode interaction = PanelInteractionMode::Standard;
    PanelOverlayKind overlay = PanelOverlayKind::None;
    bool legacyContentOwnsLaneGeometry = false;
    std::string variant;
};

struct PanelRuntimeHints {
    bool titleScreen = false;
    bool responsiveViewport = false;
    bool gameplayInputHelper = false;
    bool preflightReady = true;
    bool launchQueued = false;
    bool miningEvaActive = false;
    bool miningTetherAvailable = false;
    bool miningStowAvailable = false;
    bool miningAbortAvailable = false;
    std::string overlayValue;
};

struct ModalPresentation {
    std::string id;
    std::string title;
    std::string bodyMarkup;
    std::string closeAction;
    bool autoOpen = false;
    bool dismissible = true;
    bool showClose = true;
    ModalTone tone = ModalTone::Neutral;
};

struct PanelDocumentPresentation {
    PanelTemplateKind templateKind = PanelTemplateKind::LegacyRaw;
    PanelPresentationMetadata metadata;
    PanelRuntimeHints runtime;
    std::string contentMarkup;
    std::vector<ModalPresentation> modals;
};

} // namespace rocket
