#pragma once

#include "core/PanelPresentation.h"

#include <string>
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
};

struct PanelDocumentPresentation {
    PanelTemplateKind templateKind = PanelTemplateKind::LegacyRaw;
    PanelPresentationMetadata metadata;
    PanelRuntimeHints runtime;
    std::string contentMarkup;
    std::vector<ModalPresentation> modals;
};

} // namespace rocket
