#pragma once

#include "platform/AppServices.h"

namespace rocket {

class NativeUiBridge final : public IUiBridge {
public:
    void setPanelHtml(std::string_view) override {}
    void setRmlUiEnabled(bool) override {}
    void setModalOpen(bool) override {}
    void setControllerPresentation(bool, ControllerFamily) override {}
    void setControllerFocusVisible(bool) override {}
    void setControllerResumeBlocked(bool, bool) override {}
    void preferencesChanged(const AppPreferences&) override {}
};

} // namespace rocket
