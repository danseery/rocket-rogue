#pragma once

#include "platform/AppServices.h"

#include <optional>
#include <string>

namespace rocket {

inline constexpr const char* controllerPreferencesStorageKey = "rocket_rogue_controller_preferences_v1";

ControllerPreferences loadWebControllerPreferences();
void storeWebControllerPreferences(const ControllerPreferences& preferences);

class WebGamepadSource final : public IControllerSource {
public:
    WebGamepadSource();

    ControllerFrame sampleFrame(double realTimeSeconds) override;
    const ControllerFrame& lastFrame() const;
    std::optional<ControllerFrame> syntheticPreviewFrame() const override;
    InputSource activeSource() const override;
    void reset() override;
    std::string debugStatusJson() const;

    const ControllerPreferences& preferences() const;
    void setPreferences(const ControllerPreferences& preferences);
    void reloadPreferences();

    bool playHaptic(double durationSeconds, double weakMagnitude, double strongMagnitude) const;

    void setSyntheticSnapshot(const RawControllerSnapshot& snapshot);
    void clearSyntheticSnapshot();

private:
    ControllerTracker tracker_;
    ControllerTracker syntheticTracker_;
    InputSourceArbiter sourceArbiter_;
    ControllerPreferences preferences_;
    ControllerFrame lastFrame_;
    std::optional<ControllerFrame> syntheticPreviewFrame_;
    std::optional<RawControllerSnapshot> syntheticSnapshot_;
    double nextPreferenceRefreshSeconds_ = 0.0;
};

} // namespace rocket
