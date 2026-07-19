#pragma once

#include "input/ControllerInput.h"
#include "platform/AppServices.h"

#include <functional>
#include <string>
#include <vector>

namespace Rml {
class Element;
}

namespace rocket {

class IRmlRenderHost;

struct RmlButtonBinding {
    std::string focusId;
    std::string label;
    std::string action;
    std::string modal;
    std::string helpDismiss;
    std::string controllerSetting;
    bool close = false;
    bool helpToggle = false;
    bool cameraShakeToggle = false;
    bool desktopFullscreenToggle = false;
    bool debugToolsToggle = false;
    bool performanceStatsToggle = false;
};

enum class RmlPanelMode {
    Title,
    StoryBriefing,
    Control,
    PhaseBoard,
    ArrivalFanfare,
    MissionStamp,
    MiningFullscreen
};

class GameRmlUi final : public IGameUi {
public:
    GameRmlUi(
        IPreferenceStore& preferences,
        IPlatformHost& host,
        IUiBridge& uiBridge,
        IRmlRenderHost& renderHost);

    bool initialize(ActionHandler actionHandler) override;
    void setPanelHtml(const std::string& html) override;
    void setRealtimeHudState(const RealtimeHudState& state) override;
    void render() override;

    bool mouseMove(int x, int y) override;
    bool mouseDown(int x, int y, int button) override;
    bool mouseUp(int x, int y, int button) override;
    bool mouseWheel(int x, int y, double deltaY) override;
    bool hitTest(int x, int y) const override;
    bool navigate(UiDirection direction) override;
    bool activateFocused() override;
    bool cancel() override;
    bool scroll(float amount) override;
    bool modalOpen() const override;
    void setControllerPresentation(bool active, ControllerFamily family) override;
    void setControllerFocusVisible(bool visible) override;
    void setControllerResumeBlocked(bool blocked, bool controllerConnected) override;
    std::string focusedId() const override;
    void openModal(const std::string& id) override;
    void closeModal() override;
    void dismissHelp(const std::string& topic) override;
    void dispatchAction(const std::string& action) override;
    void refresh() override;
    bool activateButtonLabel(const std::string& label) override;
    void setPerformanceStats(const PerformanceStats& stats, bool visible) override;
    UiDiagnostics diagnostics() const override;
    void shutdown() override;

private:
    void rebuildDocument();

    IPreferenceStore& preferences_;
    IPlatformHost& host_;
    IUiBridge& uiBridge_;
    IRmlRenderHost& renderHost_;
    ActionHandler actionHandler_;
    std::string panelHtml_;
    std::string openModalId_;
    std::vector<std::string> modalStack_;
    std::vector<std::string> modalFocusStack_;
    std::vector<RmlButtonBinding> buttonBindings_;
    std::string focusedId_;
    std::string modalReturnFocusId_;
    std::string performanceStatsHtml_;
    float lastFocusCenterX_ = 0.0f;
    float lastFocusCenterY_ = 0.0f;
    bool hasLastFocusCenter_ = false;
    bool controllerPresentationActive_ = false;
    bool controllerFocusVisible_ = false;
    bool controllerResumeBlocked_ = false;
    bool controllerResumeConnected_ = false;
    bool performanceStatsVisible_ = false;
    ControllerFamily controllerFamily_ = ControllerFamily::Generic;
    Rml::Element* pressedButton_ = nullptr;
    double pressedButtonAtSeconds_ = 0.0;
    RmlPanelMode panelMode_ = RmlPanelMode::Control;
    int layoutViewportWidth_ = 0;
    int layoutViewportHeight_ = 0;
    int pendingDocumentRebuilds_ = 0;
    int pendingPanelRebuilds_ = 0;
    int pendingHudPatches_ = 0;
    UiDiagnostics uiDiagnostics_;
    bool initialized_ = false;
};

} // namespace rocket
