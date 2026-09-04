#pragma once

#include "core/Content.h"
#include "core/GameState.h"
#include "core/LaunchSimulation.h"
#include "core/MiningSystem.h"
#include "core/Random.h"
#include "game/SceneTransition.h"
#include "input/GameInputRouter.h"
#include "platform/AppServices.h"
#include "render/RenderSnapshot.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rocket {

struct PanelRenderContext;

enum class ControllerHapticCue {
    None = 0,
    Confirmation,
    MiningHardContact,
    Damage,
    Failure,
    Arrival,
    LevelUp
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
    void setMiningDrillMode(MiningDrillMode mode);
    void setFirstTimeIntroductionsEnabled(bool enabled);
    const ControllerPreferences& controllerPreferences() const;
    void setActiveInputSource(InputSource source);
    InputContext inputContext() const;
    std::string controllerDebugStatusJson() const;
    ControllerHapticCue consumePendingControllerHapticCue();
    std::vector<GameAudioEvent> consumePendingAudioEvents();
    std::uint64_t deterministicStateHash() const;

    void prepareForLaunch();
    void startLaunch();
    void launchMove(double steerAxis, double throttleAxis);
    void returnHome();
    void arrivalOps();
    void acknowledgeStoryBriefing();
    void beginStraylightApproach();
    void cutEngines();
    void next();
    void attemptFrontierTransfer();
    void openNavigation();
    void arkJump();
    void selectNavigationDestination(int index);
    void selectRefitOffer(int index);
    void buyOffer(int index);
    void rerollOffers();
    void acknowledgeApproachIntroduction();
    void acknowledgeJupiterWindow();
    void openJupiterRefit();
    void beginTransferAssist(std::string_view definitionId);
    void continueTransferAssist();
    void beginJupiterSlingshot();
    void continueJupiterSlingshot();
    void runArrivalFlyby();
    void flybyMove(double xAxis, double yAxis);
    void flybyAbort();
    void flybyContinue();
    void enterArrivalOrbit();
    void orbitMove(double xAxis, double yAxis);
    void orbitAbort();
    void orbitContinue();
    void attemptArrivalLanding();
    void departCapturedOrbit();
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
    void extractSurface();
    void selectSurfaceUpgrade(int index);
    void openDroneOps();
    void backToSurfaceOps();
    void equipDrone(int index);
    void unequipDroneSlot(int slotIndex);
    void upgradeDroneSlot();
    void miningMove(double xAxis, double yAxis);
    void miningAim(double normalizedX, double normalizedY);
    void miningPointerAim(double viewportX, double viewportY);
    void miningFire(bool active);
    void miningDrill(bool active);
    void miningKeyboardDrill(bool active);
    void miningOperatorToggle();
    void miningOperatorToggleProgress(double progress);
    void miningScanner();
    void miningTether();
    void miningRepairDrill();
    void miningRepairDrone();
    void miningStow();
    void miningWaitForDrones();
    void miningDepart();
    void deploySurfaceTeam();
    void departSurfaceUndeployed();
    void miningAbort();
    void miningFailureAck();
    void debugStartMining();
    void debugStartCombatMining();
    void debugStartSwarmArena();
    void debugStartMiningArena(
        int act,
        int difficulty,
        std::uint64_t seed,
        int loadoutMode,
        int gateOverride = -1,
        int destinationTierOverride = -1,
        int postSolarSystemOverride = 0,
        int bodyIndex = 0);
    std::string debugMiningArenaPreview(int act, int difficulty, int gateOverride = -1) const;
    std::string debugPostSolarBodyPreview(
        int postSolarSystemOverride,
        int bodyIndex,
        std::uint64_t seed) const;
    void debugShowTitle();
    void debugShowHangar();
    void debugShowJupiterOptions(int mode);
    void debugShowResults();
    void debugShowArrivalCelebration();
    void debugShowResearch();
    void debugShowRefit();
    void debugShowSurfaceUpgrade();
    void debugShowDroneOps();
    void debugShowNavigation();
    void debugStartActOneFlow();
    void debugPreviousActOneCheckpoint();
    void debugNextActOneCheckpoint();
    int debugActOneCheckpoint() const;
    void debugStartLaunchLesson(int lessonIndex);
    void debugStartSurfaceArrival(int destinationIndex, int phaseIndex);
    void debugExit();
    void repairShip();
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
    void disableDebugToolsForFreshCampaign();

    struct ReturnTripState {
        double elapsed = 0.0;
        double duration = 2.4;
        double burnMultiplier = 1.0;
        double startTravelProgress = 0.0;
    };

    struct FlightControlState {
        bool returnDriftHome = false;
        FlightActionState actions;
    };

    struct ResultViewState {
        bool usesTravelProgress = false;
        double travelProgress = 0.0;
        double elapsed = 0.0;
    };

    enum class SurfaceBaySequenceKind {
        None,
        Deploy,
        Extract,
        ShipOnlyDepart
    };

    struct SurfaceBaySequenceState {
        SurfaceBaySequenceKind kind = SurfaceBaySequenceKind::None;
        bool handoffQueued = false;
        double elapsed = 0.0;

        bool active() const noexcept
        {
            return kind != SurfaceBaySequenceKind::None;
        }

        void reset()
        {
            *this = {};
        }
    };

    enum class SurfaceArrivalPhase {
        None,
        SurfaceReveal,
        Touchdown,
        AwaitingCommand,
        Deploying,
        UndeployedTakeoff,
        Complete
    };

    struct SurfaceArrivalSequenceState {
        SurfaceArrivalPhase phase = SurfaceArrivalPhase::None;
        std::optional<PreparedSurfaceLanding> prepared;
        double elapsed = 0.0;
        bool deployQueued = false;
        bool landingCommitted = false;
        bool rigImpactFeedbackPlayed = false;
        bool surfaceReadyFeedbackPlayed = false;
        std::uint32_t audioCueMask = 0;
        int droneAudioCueCount = 0;

        bool active() const noexcept
        {
            return phase == SurfaceArrivalPhase::Touchdown ||
                phase == SurfaceArrivalPhase::AwaitingCommand ||
                phase == SurfaceArrivalPhase::Deploying ||
                phase == SurfaceArrivalPhase::UndeployedTakeoff;
        }

        void reset()
        {
            *this = {};
        }
    };

    struct MiningEvaDeathPresentationState {
        enum class Phase {
            None,
            Impact,
            FadingOut,
            FadingIn,
            Complete
        };

        Phase phase = Phase::None;
        double elapsed = 0.0;
    };

    enum class MiningSceneHandoff {
        None,
        EnterMining,
        DepartPlanet,
        AbortMining
    };

    struct ArrivalFanfareState {
        bool active = false;
        double elapsed = 0.0;
    };

    struct LevelUpSessionState {
        bool fanfareActive = false;
        double elapsed = 0.0;
        double activationFenceSeconds = 0.0;
        int batchChoices = 0;
        bool resolving = false;
        double resolveElapsed = 0.0;
        int selectedOfferIndex = -1;
    };

    struct FlightDestructionCinematicState {
        bool active = false;
        double elapsed = 0.0;
        double burnMultiplier = 1.0;
        LaunchFailureCause failureCause = LaunchFailureCause::None;
    };

    struct LaunchSessionState {
        PreparedLaunch preparedLaunch;
        FlightRunState& flight;
        bool flightArmed = false;
        bool launchQueued = false;
        double preflightElapsed = 0.0;
        double elapsed = 0.0;
        double autosaveElapsed = 0.0;
        double currentMultiplier = 1.0;
        double peakWarning = 0.0;
        double steerInput = 0.0;
        double throttleInput = 0.0;
        double asteroidImpactFeedbackSeconds = 0.0;
        FlightDestructionCinematicState destruction;
        ReturnTripState returnTrip;
        FlightControlState controls;
        ResultViewState result;
        ArrivalFanfareState arrivalFanfare;

        explicit LaunchSessionState(FlightRunState& authoritativeFlight)
            : flight(authoritativeFlight)
        {
        }

        void reset()
        {
            preparedLaunch = {};
            flight = {};
            flightArmed = false;
            launchQueued = false;
            preflightElapsed = 0.0;
            elapsed = 0.0;
            autosaveElapsed = 0.0;
            currentMultiplier = 1.0;
            peakWarning = 0.0;
            steerInput = 0.0;
            throttleInput = 0.0;
            asteroidImpactFeedbackSeconds = 0.0;
            destruction = {};
            returnTrip = {};
            controls = {};
            result = {};
            arrivalFanfare = {};
        }
    };

    void completeLaunch(
        double burnMultiplier,
        RecoveryMethod method,
        LaunchFailureCause failureCause = LaunchFailureCause::None);
    void prepareSurfaceArrivalIfNeeded(const Destination& destination);
    bool commitSurfaceTouchdown(const Destination& destination, bool hardTouchdown);
    void advanceSurfaceArrival(double deltaSeconds);
    void completeSurfaceDeployment();
    void completeUndeployedTakeoff();
    void beginSurfaceDeploymentSequence();
    void beginFlightDestructionCinematic(LaunchFailureCause failureCause);
    void beginArrivalFanfare();
    void finishArrivalFanfare();
    void maybeOpenLevelUpDraft();
    void finishLevelUpSelection();
    bool levelUpActivationLocked() const;
    void observeExpeditionExperience();
    void loadSavedGameOrDefault(bool showTitleScreen);
    void beginDebugSandbox(const std::string& statusLine);
    void seedDebugDroneLoadout();
    void captureDebugDroneLoadout();
    void applyDebugDroneLoadout();
    void applyDebugActOneCheckpoint();
    void save();
    void beginTitleLaunch(bool newCampaign);
    void completeTitleLaunch();
    void finishTitleLaunch();
    void beginSceneFadeToBlack(double durationSeconds);
    void beginSceneFadeFromBlack(double durationSeconds);
    void queueMiningSceneHandoff(MiningSceneHandoff handoff);
    bool advanceMiningSceneHandoff(double deltaSeconds);
    void completeMiningSceneHandoff();
    void beginMiningEvaDeathPresentation();
    bool advanceMiningEvaDeathPresentation(double deltaSeconds);
    bool miningEvaDeathModalReady() const noexcept;
    void startMiningRunAfterFade();
    void startNewGame();
    bool restoreCheckpoint();
    bool stateCanBecomeCheckpoint() const;
    bool validateProgressionStateOrRestore(const GameState& previous, std::string_view action);
    PanelRenderContext panelRenderContext(const PreparedLaunch& flightModel) const;
    void refreshPanel();
    void refreshRealtimeHud();
    void runUiAction(const std::string& action);
    bool runScenarioUiAction(std::string_view action);
    RenderSnapshot snapshot() const;
    PreparedLaunch currentFlightModel() const;
    void recordTelemetryPeak(const TelemetryEvent& event);
    void beginLaunchSession(PreparedLaunch preparedLaunch);
    void consumeNextLaunchBoost();
    void clearFlightControls();
    void clearResultView();
    void beginSurfaceExpeditionOrRefit();
    void finishArrivalVisit(std::string statusLine);
    bool openRefitIfAvailable(bool regenerateOffers = true);
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
    void queueAudioCue(GameAudioCue cue, double pitch = 1.0);

    struct RealtimeInputState {
        double moveX = 0.0;
        double moveY = 0.0;
        double aimX = 0.0;
        double aimY = 0.0;
        bool firing = false;
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
    SurfaceBaySequenceState surfaceBaySequence_;
    SurfaceArrivalSequenceState surfaceArrival_;
    MiningEvaDeathPresentationState miningEvaDeathPresentation_;
    MiningSceneHandoff miningSceneHandoff_ = MiningSceneHandoff::None;
    bool miningSceneHandoffCommitted_ = false;
    LevelUpSessionState levelUp_;
    RealtimeInputState keyboardRealtimeInput_;
    RealtimeInputState controllerRealtimeInput_;
    MiningDrillMode miningDrillMode_ = MiningDrillMode::Toggle;
    bool firstTimeIntroductionsEnabled_ = true;
    bool keyboardDrillPressed_ = false;
    PauseReason pauseReason_ = PauseReason::None;
    Screen lastInputScreen_ = Screen::Hangar;
    bool controllerWasConnected_ = false;
    bool controllerClaimedInput_ = false;
    bool controllerConnected_ = false;
    bool controllerResumeNeutralRequired_ = false;
    std::string lastControllerAction_ = "none";
    ControllerHapticCue pendingHapticCue_ = ControllerHapticCue::None;
    std::vector<GameAudioEvent> pendingAudioEvents_;
    double lastMiningContactIntensity_ = 0.0;
    double lastMiningDroneHealth_ = 1.0;
    bool lastMiningFailurePending_ = false;
    double lastControllerInputSeconds_ = 0.0;
    double visualTimeSeconds_ = 0.0;
    double miningOperatorToggleConfirmationSeconds_ = 0.0;
    double expeditionXpPulseSeconds_ = 0.0;
    int observedExpeditionLevel_ = 1;
    double observedExpeditionExperience_ = 0.0;
    bool expeditionXpObservationInitialized_ = false;
    bool titleScreenActive_ = true;
    bool titleLaunchActive_ = false;
    bool titleLaunchStartsNewCampaign_ = false;
    double titleLaunchElapsedSeconds_ = 0.0;
    SceneTransition sceneTransition_;
    bool hasSavedGame_ = false;
    bool checkpointRecoveryAvailable_ = false;
    std::string titleNotice_;
    bool panelDirty_ = true;
    bool realtimeHudDirty_ = true;
    int selectedRefitOfferIndex_ = 0;
    std::uint64_t panelStructureKey_ = 0;
    RealtimeHudState realtimeHudState_;
    bool debugSessionActive_ = false;
    int debugActOneCheckpoint_ = -1;
    struct DebugDroneLoadout {
        bool configured = false;
        std::vector<std::string> equippedDroneIds;
        std::vector<RunDroneRank> droneRanks;
    };
    DebugDroneLoadout debugDroneLoadout_;
};

} // namespace rocket
