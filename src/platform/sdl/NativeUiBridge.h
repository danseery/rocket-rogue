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
    bool navigate(UiDirection) override { return false; }
    bool activate() override { return false; }
    bool cancel() override { return false; }
    bool scroll(double) override { return false; }
    bool modalOpen() const override { return false; }
    bool openModal(std::string_view) override { return false; }
    void closeModal() override {}
    std::string focusedId() const override { return {}; }
    void preferencesChanged(const AppPreferences&) override {}
};

} // namespace rocket
