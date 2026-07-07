#pragma once

#include <functional>
#include <string>
#include <vector>

namespace Rml {
class Element;
}

namespace rocket {

struct RmlButtonBinding {
    std::string label;
    std::string action;
    std::string modal;
    std::string helpDismiss;
    bool close = false;
    bool helpToggle = false;
    bool cameraShakeToggle = false;
    bool debugToolsToggle = false;
};

enum class RmlPanelMode {
    Control,
    PhaseBoard,
    MiningFullscreen
};

class GameRmlUi {
public:
    using ActionHandler = std::function<void(const std::string&)>;

    bool initialize(ActionHandler actionHandler);
    void setPanelHtml(const std::string& html);
    void render();

    bool mouseMove(int x, int y);
    bool mouseDown(int x, int y, int button);
    bool mouseUp(int x, int y, int button);
    bool mouseWheel(int x, int y, double deltaY);
    bool hitTest(int x, int y) const;
    void openModal(const std::string& id);
    void closeModal();
    void dismissHelp(const std::string& topic);
    void dispatchAction(const std::string& action);
    void refresh();
    bool activateButtonLabel(const std::string& label);

private:
    void rebuildDocument();

    ActionHandler actionHandler_;
    std::string panelHtml_;
    std::string openModalId_;
    std::vector<RmlButtonBinding> buttonBindings_;
    Rml::Element* pressedButton_ = nullptr;
    RmlPanelMode panelMode_ = RmlPanelMode::Control;
    bool initialized_ = false;
};

} // namespace rocket
