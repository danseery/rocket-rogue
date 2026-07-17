#pragma once

#include "core/Content.h"
#include "core/GameState.h"
#include "core/LaunchSimulation.h"
#include "core/Random.h"
#include "input/GameInputRouter.h"
#include "platform/AppServices.h"
#include "render/OpenGlRenderer.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rocket {

struct PanelRenderContext;

enum class ControllerHapticCue {
    None = 0,
    Confirmation,
    MiningHardContact,
    Damage,
    Failure
};

class RocketGameApp {
public:
    explicit RocketGameApp(AppServices& services);

    bool initialize();
    void shutdown();
    void inputFrame(const ControllerFrame& frame, double realTimeSeconds);
    void tick(double deltaSeconds);
    void renderScene();
    void renderUi();
    int currentScreen() const;
    void setControllerPreferences(const ControllerPreferences& preferences);
    const ControllerPreferences& controllerPreferences() const;
    void setActiveInputSource(InputSource source);
    InputContext inputContext() const;
    std::string controllerDebugStatusJson() const;
    ControllerHapticCue consumePendingControllerHapticCue();

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
    void debugStartMiningArena(int act, int difficulty, std::uint64_t seed, int loadoutMode, int gateOverride = -1);
    std::string debugMiningArenaPreview(int act, int difficulty, int gateOverride = -1) const;
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
    void debugStartActOneFlow();
    void debugPreviousActOneCheckpoint();
    void debugNextActOneCheckpoint();
    int debugActOneCheckpoint() const;
    void debugExit();
    void repairShip();
    void recruitCrew();
    void recruitCrew(int candidateIndex);
    void trainCrew();
    void restCrew();
    void newGame();
    void continueGame();
    void resetSave();
    bool uiMouseMove(int x, int y);
    bool uiMouseDown(int x, int y, int button);
    bool uiMouseUp(int x, int y, int button);
    bool uiMouseWheel(int x, int y, double deltaY);
    bool uiHitTest(int x, int y) const;
    bool uiNavigate(UiDirection direction);
    bool uiActivateFocused();
    bool uiCancel();

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

    struct MiningExtractionState {
        bool active = false;
        bool showUpgradeDraft = false;
        double elapsed = 0.0;
    };

    struct ArrivalFanfareState {
        bool active = false;
        double elapsed = 0.0;
    };

    struct LaunchSessionState {
        PreparedLaunch preparedLaunch;
        bool flightArmed = false;
        bool launchQueued = false;
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
    void loadSavedGameOrDefault(bool showTitleScreen);
    void beginDebugSandbox(const std::string& statusLine);
    void seedDebugDroneLoadout();
    void captureDebugDroneLoadout();
    void applyDebugDroneLoadout();
    void applyDebugActOneCheckpoint();
    void save();
    PanelRenderContext panelRenderContext(const PreparedLaunch& flightModel) const;
    void refreshPanel();
    void refreshRealtimeHud();
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
    void applyRealtimeInputs();
    void releaseRealtimeInputs(bool releaseKeyboard);
    void dispatchControllerInput(InputContext context, const RoutedGameInput& input);
    void dispatchControllerAction(InputContext context, GameInputAction action);
    void previewSyntheticControllerInput(const ControllerFrame& frame, double realTimeSeconds);
    void openControllerSystemMenu(PauseReason reason);
    void clearControllerPause();
    bool realtimeControllerContext(InputContext context) const;
    InputContext gameplayInputContext() const;
    void updateControllerHapticState();
    void queueControllerHapticCue(ControllerHapticCue cue);

    struct RealtimeInputState {
        double moveX = 0.0;
        double moveY = 0.0;
        bool drilling = false;
    };

    AppServices& services_;
    ContentCatalog catalog_;
    GameState state_;
    Random rng_;
    GameInputRouter inputRouter_;
    GameInputRouter syntheticInputRouter_;
    ControllerPreferences controllerPreferences_;
    InputSource activeInputSource_ = InputSource::None;
    LaunchSessionState session_;
    MiningExtractionState miningExtraction_;
    RealtimeInputState keyboardRealtimeInput_;
    RealtimeInputState controllerRealtimeInput_;
    PauseReason pauseReason_ = PauseReason::None;
    Screen lastInputScreen_ = Screen::Hangar;
    bool controllerWasConnected_ = false;
    bool controllerClaimedInput_ = false;
    bool controllerConnected_ = false;
    bool controllerResumeNeutralRequired_ = false;
    std::string lastControllerAction_ = "none";
    ControllerHapticCue pendingHapticCue_ = ControllerHapticCue::None;
    double lastMiningContactIntensity_ = 0.0;
    double lastMiningDroneHealth_ = 1.0;
    bool lastMiningFailurePending_ = false;
    double lastControllerInputSeconds_ = 0.0;
    double visualTimeSeconds_ = 0.0;
    bool titleScreenActive_ = true;
    bool hasSavedGame_ = false;
    std::string titleNotice_;
    bool panelDirty_ = true;
    bool realtimeHudDirty_ = true;
    std::uint64_t panelStructureKey_ = 0;
    RealtimeHudState realtimeHudState_;
    bool debugSessionActive_ = false;
    int debugActOneCheckpoint_ = -1;
    struct DebugDroneLoadout {
        bool configured = false;
        std::vector<std::string> equippedDroneIds;
        std::vector<DroneUpgradeRecord> droneUpgrades;
    };
    DebugDroneLoadout debugDroneLoadout_;
};

} // namespace rocket
