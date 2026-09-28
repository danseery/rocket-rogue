#pragma once

#include "core/PanelDocumentPresentation.h"
#include "input/ControllerInput.h"
#include "platform/AppServices.h"

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
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
    std::string controllerSetting;
    bool close = false;
    bool helpToggle = false;
    bool cameraShakeToggle = false;
    bool incomingNoticeToggle = false;
    bool desktopFullscreenToggle = false;
    bool debugToolsToggle = false;
    bool performanceStatsToggle = false;
};

enum class RmlPanelMode {
    Title,
    StoryBriefing,
    Results,
    DroneWorkspace,
    Workspace,
    Control,
    PhaseBoard,
    ArrivalFanfare,
    MissionStamp,
    MiningFullscreen
};

enum class SettingsTab { Display, Controls, Gameplay };

class GameRmlUi final : public IGameUi {
public:
    GameRmlUi(
        IPreferenceStore& preferences,
        IPlatformHost& host,
        IUiBridge& uiBridge,
        IRmlRenderHost& renderHost,
        std::string assetRoot = {});

    bool initialize(ActionHandler actionHandler) override;
    void setPanelPresentation(const PanelDocumentPresentation& presentation) override;
    void setRealtimeHudState(const RealtimeHudState& state) override;
    void setInteractionAnchors(const SceneInteractionAnchors& anchors) override;
    void render() override;

    bool mouseMove(int x, int y) override;
    bool mouseDown(int x, int y, int button) override;
    bool mouseUp(int x, int y, int button) override;
    bool mouseWheel(int x, int y, double deltaY) override;
    bool hitTest(int x, int y) const override;
    bool navigate(UiDirection direction) override;
    bool activateFocused() override;
    bool cancel() override;
    bool cancelChildModal() override;
    bool scroll(float amount) override;
    bool modalOpen() const override;
    void setControllerPresentation(bool active, ControllerFamily family) override;
    void setControllerConfirmCancelSwapped(bool swapped) override;
    void setControllerFocusVisible(bool visible) override;
    void setControllerResumeBlocked(bool blocked, bool controllerConnected) override;
    std::string focusedId() const override;
    FocusedControllerAction focusedControllerAction() const override;
    void requestFocus(std::string_view id) override;
    void openModal(const std::string& id) override;
    void closeModal() override;
    void dispatchAction(const std::string& action) override;
    void emitUiSound(const std::string& name);
    void refresh() override;
    bool activateButtonLabel(const std::string& label) override;
    void setPerformanceStats(const PerformanceStats& stats, bool visible) override;
    UiDiagnostics diagnostics() const override;
    void shutdown() override;

private:
    bool navigateImpl(UiDirection direction);
    std::string audioHoverId_;
    void rebuildDocument();
    void refreshPersistentHosts(
        bool rebuildPanel,
        bool rebuildPanelShell,
        bool rebuildModal,
        bool rebuildOverlay,
        bool rebuildPrompt,
        bool rebuildPerformance);
    bool applyDocumentPresentationState();
    bool rebuildPanelHost(bool rebuildShell);
    bool rebuildModalHost();
    void applySettingsTabSelection();
    bool rebuildOverlayHost();
    bool rebuildPromptHost();
    bool rebuildPerformanceHost();
    void rebindAndRestoreFocus(bool restoreFocus);
    void updateControllerConfirmGlyphs();
    bool applyPendingFocusIfAvailable();
    void applyPendingPointerActivation();
    void applyPendingModalOpen();
    void openModalImmediately(const std::string& id);

    IPreferenceStore& preferences_;
    IPlatformHost& host_;
    IUiBridge& uiBridge_;
    IRmlRenderHost& renderHost_;
    std::string assetRoot_;
    ActionHandler actionHandler_;
    PanelDocumentPresentation presentation_;
    std::string externalRcss_;
    std::string openModalId_;
    SettingsTab settingsTab_ = SettingsTab::Display;
    std::string pendingModalOpenId_;
    std::vector<RmlButtonBinding> pendingPointerActivations_;
    std::string renderedModalId_;
    std::vector<std::string> modalStack_;
    std::vector<std::string> modalFocusStack_;
    std::vector<bool> modalExplicitFocusStack_;
    std::unordered_map<std::string, float> modalScrollPositions_;
    std::vector<RmlButtonBinding> buttonBindings_;
    std::string focusedId_;
    std::string pendingFocusId_;
    std::string modalReturnFocusId_;
    std::string performanceStatsHtml_;
    float lastFocusCenterX_ = 0.0f;
    float lastFocusCenterY_ = 0.0f;
    bool hasLastFocusCenter_ = false;
    bool controllerFocusExplicit_ = false;
    bool modalReturnFocusExplicit_ = false;
    bool controllerPresentationActive_ = false;
    bool controllerFocusVisible_ = false;
    bool controllerConfirmCancelSwapped_ = false;
    bool controllerResumeBlocked_ = false;
    bool controllerResumeConnected_ = false;
    bool missionTrackerExpanded_ = true;
    bool renderedIncomingNoticesAsModals_ = false;
    bool performanceStatsVisible_ = false;
    bool deferModalOpen_ = false;
    ControllerFamily controllerFamily_ = ControllerFamily::Generic;
    SceneInteractionAnchors interactionAnchors_;
    bool contextPlacementActive_ = false;
    bool contextOnRight_ = true;
    float contextLeft_ = 0.0F;
    float contextTop_ = 0.0F;
    double contextPlacementSeconds_ = 0.0;
    bool shipServicesPlacementActive_ = false;
    bool shipServicesOnRight_ = true;
    float shipServicesLeft_ = 0.0F;
    float shipServicesTop_ = 0.0F;
    double shipServicesPlacementSeconds_ = 0.0;
    Rml::Element* pressedButton_ = nullptr;
    bool orbitalPointerHeld_ = false;
    double pressedButtonAtSeconds_ = 0.0;
    RmlPanelMode panelMode_ = RmlPanelMode::Control;
    int layoutViewportWidth_ = 0;
    int layoutViewportHeight_ = 0;
    int pendingDocumentRebuilds_ = 0;
    int pendingPanelRebuilds_ = 0;
    int pendingHudPatches_ = 0;
    UiDiagnostics uiDiagnostics_;
    bool renderHostInitialized_ = false;
    bool rmlInitialized_ = false;
    bool initialized_ = false;
};

} // namespace rocket
