#pragma once

#include "platform/AppServices.h"

#include <optional>
#include <string>

namespace rocket {

ControllerPreferences loadWebControllerPreferences();
void storeWebControllerPreferences(const ControllerPreferences& preferences);

class WebGamepadSource final : public IControllerSource {
public:
    WebGamepadSource();

    ControllerFrame sampleFrame(double realTimeSeconds) override;
    std::optional<ControllerFrame> syntheticPreviewFrame() const override;
    void setPreferences(const ControllerPreferences& preferences) override;
    InputSource activeSource() const override;
    void reset() override;
    std::string debugStatusJson() const;

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
};

} // namespace rocket
