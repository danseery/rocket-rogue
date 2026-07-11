#pragma once

#include "core/Content.h"
#include "core/GameState.h"
#include "core/LaunchSimulation.h"
#include "core/Random.h"
#include "game/GameRmlUi.h"
#include "render/WebGLRenderer.h"

#include <string>

namespace rocket {

class RocketGameApp {
public:
    bool initialize();
    void tick(double deltaSeconds);
    void render();
    int currentScreen() const;

    void prepareForLaunch();
    void startLaunch();
    void returnHome();
    void arrivalOps();
    void skipArrivalFanfare();
    void cutEngines();
    void pressureReliefValve();
    void closePressureReliefValve();
    void jettisonCargo();
    void ejectNow();
    void next();
    void attemptFrontierTransfer();
    void openNavigation();
    void arkJump();
    void selectNavigationDestination(int index);
    void buyOffer(int index);
    void rerollOffers();
    void runArrivalFlyby();
    void flybyMove(double xAxis, double yAxis);
    void flybyAbort();
    void flybyContinue();
    void enterArrivalOrbit();
    void orbitMove(double xAxis, double yAxis);
    void orbitAbort();
    void orbitContinue();
    void attemptArrivalLanding();
    void selectResearchProject(int index);
    void skipResearch();
    void surveySurface();
    void mineSurface();
    void pushSurface();
    void scanSurfacePulse();
    void scanSurfaceBank();
    void scanSurfaceAbort();
    void pushSurfaceStep();
    void pushSurfaceBank();
    void pushSurfaceAbort();
    void extractSurface();
    void selectSurfaceUpgrade(int index);
    void openDroneOps();
    void backToSurfaceOps();
    void equipDrone(int index);
    void unequipDroneSlot(int slotIndex);
    void upgradeDrone(int index);
    void upgradeDroneSlot();
    void miningMove(double xAxis, double yAxis);
    void miningAim(double normalizedX, double normalizedY);
    void miningDrill(bool active);
    void miningScanner();
    void miningTether();
    void miningRepairDrill();
    void miningRepairDrone();
    void miningStow();
    void miningAbort();
    void miningFailureAck();
    void debugStartMining();
    void debugStartCombatMining();
    void debugStartSurfaceScan();
    void debugStartSurfacePush();
    void debugStartFlyby();
    void debugStartOrbit();
    void debugShowHangar();
    void debugShowResults();
    void debugShowArrivalOps();
    void debugShowResearch();
    void debugShowSurfaceUpgrade();
    void debugShowDroneOps();
    void debugShowNavigation();
    void debugExit();
    void repairShip();
    void recruitCrew();
    void recruitCrew(int candidateIndex);
    void trainCrew();
    void restCrew();
    void resetSave();
    bool uiMouseMove(int x, int y);
    bool uiMouseDown(int x, int y, int button);
    bool uiMouseUp(int x, int y, int button);
    bool uiMouseWheel(int x, int y, double deltaY);
    bool uiHitTest(int x, int y) const;

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

    struct ArrivalFanfareState {
        bool active = false;
        double elapsed = 0.0;
    };

    struct LaunchSessionState {
        PreparedLaunch preparedLaunch;
        bool flightArmed = false;
        double preflightElapsed = 0.0;
        double elapsed = 0.0;
        double currentMultiplier = 1.0;
        double peakWarning = 0.0;
        double peakAbortRisk = 0.0;
        ReturnTripState returnTrip;
        FlightControlState controls;
        ResultViewState result;
        ArrivalFanfareState arrivalFanfare;
    };

    void completeLaunch(double burnMultiplier, RecoveryMethod method);
    void beginArrivalFanfare();
    void finishArrivalFanfare();
    void loadSavedGameOrDefault();
    void beginDebugSandbox(const std::string& statusLine);
    void save();
    void refreshPanel();
    void runUiAction(const std::string& action);
    RenderSnapshot snapshot() const;
    PreparedLaunch currentFlightModel() const;
    void recordTelemetryPeak(const TelemetryEvent& event);
    void beginLaunchSession(PreparedLaunch preparedLaunch);
    void consumeNextLaunchBoost();
    void clearFlightControls();
    void clearResultView();
    void beginSurfaceExpeditionOrRefit();
    double liveBurnMultiplier() const;

    ContentCatalog catalog_;
    GameState state_;
    Random rng_;
    WebGLRenderer renderer_;
    GameRmlUi rmlUi_;
    LaunchSessionState session_;
    double visualTimeSeconds_ = 0.0;
    bool panelDirty_ = true;
    bool debugSessionActive_ = false;
};

} // namespace rocket
