#pragma once

#include "core/Content.h"
#include "core/GameState.h"
#include "core/LaunchSimulation.h"
#include "core/Random.h"
#include "render/WebGLRenderer.h"

#include <string>

namespace rocket {

class RocketGameApp {
public:
    bool initialize();
    void tick(double deltaSeconds);
    void render();

    void startLaunch();
    void returnHome();
    void cutEngines();
    void pressureReliefValve();
    void closePressureReliefValve();
    void jettisonCargo();
    void ejectNow();
    void next();
    void attemptFrontierTransfer();
    void buyOffer(int index);
    void repairShip();
    void recruitCrew();
    void trainCrew();
    void restCrew();
    void resetSave();

private:
    void completeLaunch(double burnMultiplier, RecoveryMethod method);
    void save();
    void refreshPanel();
    RenderSnapshot snapshot() const;
    PreparedLaunch currentFlightModel() const;
    void recordTelemetryPeak(const TelemetryEvent& event);

    ContentCatalog catalog_;
    GameState state_;
    Random rng_;
    WebGLRenderer renderer_;
    PreparedLaunch activeLaunch_;
    double launchElapsed_ = 0.0;
    double currentMultiplier_ = 1.0;
    double peakWarning_ = 0.0;
    double peakAbortRisk_ = 0.0;
    bool returningHome_ = false;
    bool returnDriftHome_ = false;
    bool cutEnginesActive_ = false;
    bool pressureReliefUsed_ = false;
    bool pressureReliefOpen_ = false;
    bool pressureReliefFailed_ = false;
    bool cargoJettisoned_ = false;
    double returnElapsed_ = 0.0;
    double returnDuration_ = 2.4;
    double returnBurnMultiplier_ = 1.0;
    double returnStartTravelProgress_ = 0.0;
    bool resultUsesTravelProgress_ = false;
    double resultTravelProgress_ = 0.0;
    double resultElapsed_ = 0.0;
    bool panelDirty_ = true;
};

} // namespace rocket
