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
    void arrivalOps();
    void cutEngines();
    void pressureReliefValve();
    void closePressureReliefValve();
    void jettisonCargo();
    void ejectNow();
    void next();
    void attemptFrontierTransfer();
    void buyOffer(int index);
    void rerollOffers();
    void runArrivalFlyby();
    void enterArrivalOrbit();
    void attemptArrivalLanding();
    void selectResearchProject(int index);
    void skipResearch();
    void surveySurface();
    void mineSurface();
    void pushSurface();
    void extractSurface();
    void miningMove(double xAxis, double yAxis);
    void miningAim(double normalizedX, double normalizedY);
    void miningDrill(bool active);
    void miningScanner();
    void miningStow();
    void miningAbort();
    void repairShip();
    void recruitCrew();
    void trainCrew();
    void restCrew();
    void resetSave();

private:
    struct ReturnTripState {
        double elapsed = 0.0;
        double duration = 2.4;
        double burnMultiplier = 1.0;
        double startTravelProgress = 0.0;
    };

    struct FlightControlState {
        bool returnDriftHome = false;
        bool pressureReliefUsed = false;
        FlightActionState actions;
    };

    struct ResultViewState {
        bool usesTravelProgress = false;
        double travelProgress = 0.0;
        double elapsed = 0.0;
    };

    struct LaunchSessionState {
        PreparedLaunch preparedLaunch;
        double elapsed = 0.0;
        double currentMultiplier = 1.0;
        double peakWarning = 0.0;
        double peakAbortRisk = 0.0;
        ReturnTripState returnTrip;
        FlightControlState controls;
        ResultViewState result;
    };

    void completeLaunch(double burnMultiplier, RecoveryMethod method);
    void save();
    void refreshPanel();
    RenderSnapshot snapshot() const;
    PreparedLaunch currentFlightModel() const;
    void recordTelemetryPeak(const TelemetryEvent& event);
    void beginLaunchSession(PreparedLaunch preparedLaunch);
    void clearFlightControls();
    void clearResultView();
    void beginSurfaceExpeditionOrRefit();
    double liveBurnMultiplier() const;

    ContentCatalog catalog_;
    GameState state_;
    Random rng_;
    WebGLRenderer renderer_;
    LaunchSessionState session_;
    bool panelDirty_ = true;
};

} // namespace rocket
