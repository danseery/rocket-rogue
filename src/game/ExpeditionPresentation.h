#pragma once
#include "game/GamePanel.h"
namespace rocket {
// Shared physical-mission status for live flight, mining, and Drone Ops.
std::string hazardDroneMissionMarkup(const PanelRenderContext&, bool includeDroneOps = true,
    bool defaultFocus = false);
void appendExpeditionPresentation(const PanelRenderContext&, PanelDocumentPresentation&);
void appendMissionPresentation(const PanelRenderContext&, PanelDocumentPresentation&);
}
