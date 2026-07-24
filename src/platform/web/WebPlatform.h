#pragma once

#include "core/UiViewportLayout.h"
#include "platform/AppServices.h"

#include <string>
#include <unordered_map>
#include <vector>

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

private:
    WebGamepadSource& gamepads_;
    ViewportMetrics cachedViewportMetrics_;
    std::uint64_t viewportRevision_ = 0;
};

class WebTextureSource final : public ITextureSource {
public:
    void request(std::string_view key, std::string_view relativePath) override;
    TextureStatus status(std::string_view key) const override;
    std::optional<DecodedImageView> decodedImage(std::string_view key) const override;
    void releaseDecodedImage(std::string_view key) override;
    std::string lastError() const override;

private:
    struct DecodedImage {
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> rgba;
    };

    mutable std::unordered_map<std::string, DecodedImage> decodedImages_;
    mutable std::string lastError_;
};

class WebUiBridge final : public IUiBridge {
public:
    void setUiViewportLayout(const UiViewportLayout& layout);
    UiSurfaceKind viewportSurfaceKind() const noexcept;
    void setPanelHtml(std::string_view html) override;
    void setRealtimeHudState(const RealtimeHudState& state) override;
    void setRmlUiEnabled(bool enabled) override;
    void setModalOpen(bool open) override;
    void setControllerPresentation(bool active, ControllerFamily family) override;
    void setControllerFocusVisible(bool visible) override;
    void setControllerResumeBlocked(bool blocked, bool connected) override;
    void preferencesChanged(const AppPreferences& preferences) override;
    void setPerformanceStats(std::string_view html, bool visible) override;

private:
    UiSurfaceKind viewportSurfaceKind_ = UiSurfaceKind::Fullscreen;
};

} // namespace rocket
