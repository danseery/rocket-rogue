#pragma once

#include "platform/AppServices.h"

#include <string>

namespace rocket {

class WebGamepadSource;

class WebPreferenceStore final : public IPreferenceStore {
public:
    WebPreferenceStore();

    AppPreferences load() override;
    bool store(const AppPreferences& preferences) override;
    std::uint64_t revision() const override;
    std::string lastError() const override;

private:
    AppPreferences cached_;
    std::uint64_t observedRevision_ = 0;
    bool loaded_ = false;
    std::string lastError_;
};

class WebPlatformHost final : public IPlatformHost {
public:
    explicit WebPlatformHost(WebGamepadSource& gamepads);

    bool createGraphicsContext();

    double monotonicSeconds() const override;
    ViewportMetrics viewportMetrics() override;
    bool focused() const override;
    bool visible() const override;
    bool fullscreenAvailable() const override;
    bool fullscreen() const override;
    bool setFullscreen(bool enabled) override;
    void log(PlatformLogLevel level, std::string_view message) override;
    bool haptic(double durationSeconds, double weakMagnitude, double strongMagnitude) override;
    void present() override;
    OpenGlDialect openGlDialect() const override;

private:
    WebGamepadSource& gamepads_;
    ViewportMetrics cachedViewportMetrics_;
    std::uint64_t viewportRevision_ = 0;
};

class WebTextureSource final : public ITextureSource {
public:
    void request(std::string_view key, std::string_view relativePath) override;
    TextureStatus status(std::string_view key) const override;
    bool uploadToOpenGl(std::string_view key, unsigned int texture, int& width, int& height) override;
    std::string lastError() const override;

private:
    mutable std::string lastError_;
};

class WebUiBridge final : public IUiBridge {
public:
    void setPanelHtml(std::string_view html) override;
    void setRealtimeHudState(const RealtimeHudState& state) override;
    void setRmlUiEnabled(bool enabled) override;
    void setModalOpen(bool open) override;
    void setControllerPresentation(bool active, ControllerFamily family) override;
    void setControllerFocusVisible(bool visible) override;
    void setControllerResumeBlocked(bool blocked, bool connected) override;
    bool navigate(UiDirection direction) override;
    bool activate() override;
    bool cancel() override;
    bool scroll(double amount) override;
    bool modalOpen() const override;
    bool openModal(std::string_view id) override;
    void closeModal() override;
    std::string focusedId() const override;
    void preferencesChanged(const AppPreferences& preferences) override;
    void setPerformanceStats(std::string_view html, bool visible) override;
};

} // namespace rocket
