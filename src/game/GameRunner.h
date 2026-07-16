#pragma once

#include "game/RocketGameApp.h"
#include "platform/AppServices.h"

namespace rocket {

class GameRunner {
public:
    explicit GameRunner(AppServices& services);

    bool initialize();
    void frame();
    void shutdown();

    RocketGameApp& app();
    const RocketGameApp& app() const;

private:
    void dispatchPendingHaptic();

    AppServices& services_;
    RocketGameApp app_;
    double lastFrameSeconds_ = 0.0;
    bool initialized_ = false;
};

} // namespace rocket
