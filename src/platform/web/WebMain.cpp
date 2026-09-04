#include "core/UiViewportLayout.h"
#include "game/GameRmlUi.h"
#include "game/GameRunner.h"
#include "platform/web/WebGamepadSource.h"
#include "platform/web/WebPlatform.h"
#include "platform/web/WebSaveStore.h"
#include "render/OpenGlRenderer.h"
#include "render/GlRmlRenderHost.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>

namespace {

rocket::RocketGameApp* g_app = nullptr;
std::unique_ptr<rocket::WebGamepadSource> g_gamepadSource;
std::unique_ptr<rocket::WebSaveStore> g_saveStore;
std::unique_ptr<rocket::WebPreferenceStore> g_preferenceStore;
std::unique_ptr<rocket::WebPlatformHost> g_platformHost;
std::unique_ptr<rocket::WebTextureSource> g_textureSource;
std::unique_ptr<rocket::WebUiBridge> g_uiBridge;
std::unique_ptr<rocket::WebGlGraphicsBackend> g_renderer;
std::unique_ptr<rocket::WebGlRmlRenderHost> g_rmlRenderHost;
std::unique_ptr<rocket::IGameUi> g_ui;
std::unique_ptr<rocket::IGameAudio> g_audio;
std::unique_ptr<rocket::AppServices> g_services;
std::unique_ptr<rocket::GameRunner> g_runner;
std::string g_debugMiningPreview;
std::string g_controllerDebugStatus;
std::string g_controllerAppDebugStatus;
int g_domLayoutWidth = -1;
int g_domLayoutHeight = -1;
rocket::UiSurfaceKind g_domLayoutSurface = rocket::UiSurfaceKind::Fullscreen;

#ifdef __EMSCRIPTEN__
class WebAutoPowerPacer {
public:
    bool shouldRun(
        const rocket::AppPreferences& preferences,
        rocket::WebPlatformHost& host,
        double nowMilliseconds)
    {
        if (preferences.frameLimitMode != rocket::FrameLimitMode::AutoPower) {
            reset();
            return true;
        }
        const rocket::FrameLimitResolution resolution = rocket::resolveFrameLimit(
            preferences.frameLimitMode,
            host.displayRefreshRateHz(),
            false,
            host.autoPowerEnvironment());
        if (!resolution.refreshRateKnown || resolution.refreshDivisor <= 1U
            || resolution.targetFramesPerSecond <= 0.0) {
            reset();
            return true;
        }

        const double intervalMilliseconds = 1000.0 / resolution.targetFramesPerSecond;
        if (nextFrameMilliseconds_ <= 0.0 || std::abs(intervalMilliseconds - intervalMilliseconds_) > 0.1) {
            intervalMilliseconds_ = intervalMilliseconds;
            nextFrameMilliseconds_ = nowMilliseconds + intervalMilliseconds_;
            return true;
        }
        if (nowMilliseconds + 0.2 < nextFrameMilliseconds_) return false;
        const double missedIntervals = std::floor(
            (nowMilliseconds - nextFrameMilliseconds_) / intervalMilliseconds_);
        nextFrameMilliseconds_ += intervalMilliseconds_ * std::max(1.0, missedIntervals + 1.0);
        return true;
    }

private:
    void reset()
    {
        nextFrameMilliseconds_ = 0.0;
        intervalMilliseconds_ = 0.0;
    }

    double nextFrameMilliseconds_ = 0.0;
    double intervalMilliseconds_ = 0.0;
};

WebAutoPowerPacer g_autoPowerPacer;

EM_JS(int, rr_controller_debug_tools_enabled, (), {
    try {
        return globalThis.localStorage.getItem("rocket_rogue_debug_tools") === "1" ? 1 : 0;
    } catch (error) {
        return 0;
    }
});

EM_JS(int, rr_web_play_audio_cue, (const char* cuePath, double pitch), {
    try {
        const path = UTF8ToString(cuePath);
        const audio = globalThis.__orebitGameAudio || (globalThis.__orebitGameAudio = {
            context: null,
            buffers: new Map(),
            pending: new Map(),
            failures: new Set()
        });
        const AudioContextType = globalThis.AudioContext || globalThis.webkitAudioContext;
        if (!AudioContextType) return 0;
        if (!audio.context) audio.context = new AudioContextType();
        const play = (buffer) => {
            const source = audio.context.createBufferSource();
            source.buffer = buffer;
            source.playbackRate.value = Math.max(0.75, Math.min(1.25, pitch));
            source.connect(audio.context.destination);
            audio.context.resume().catch(() => {});
            source.start();
        };
        if (audio.buffers.has(path)) {
            play(audio.buffers.get(path));
            return 1;
        }
        let pending = audio.pending.get(path);
        if (!pending) {
            pending = fetch(path)
                .then((response) => {
                    if (!response.ok) throw new Error(`HTTP ${response.status}`);
                    return response.arrayBuffer();
                })
                .then((bytes) => audio.context.decodeAudioData(bytes))
                .then((buffer) => {
                    audio.buffers.set(path, buffer);
                    audio.pending.delete(path);
                    return buffer;
                })
                .catch((error) => {
                    audio.pending.delete(path);
                    if (!audio.failures.has(path)) {
                        audio.failures.add(path);
                        console.warn(`OREBIT surface audio cue unavailable: ${path}`, error);
                    }
                    return null;
                });
            audio.pending.set(path, pending);
        }
        pending.then((buffer) => { if (buffer) play(buffer); });
        return 1;
    } catch (error) {
        return 0;
    }
});

class WebGameAudio final : public rocket::IGameAudio {
public:
    bool playOneShot(const rocket::GameAudioEvent& event) override
    {
        const std::string path = "assets/audio/surface/" + filename(event.cue);
        return rr_web_play_audio_cue(path.c_str(), event.pitch) != 0;
    }

private:
    static std::string filename(rocket::GameAudioCue cue)
    {
        switch (cue) {
        case rocket::GameAudioCue::SafeTouchdown: return "safe_touchdown.wav";
        case rocket::GameAudioCue::HardTouchdown: return "hard_touchdown.wav";
        case rocket::GameAudioCue::BayOpen: return "bay_open.wav";
        case rocket::GameAudioCue::RigEjection: return "rig_ejection.wav";
        case rocket::GameAudioCue::ArrestingBurst: return "arresting_burst.wav";
        case rocket::GameAudioCue::RigImpact: return "rig_impact.wav";
        case rocket::GameAudioCue::DroneLaunch: return "drone_launch.wav";
        case rocket::GameAudioCue::BayClose: return "bay_close.wav";
        case rocket::GameAudioCue::SurfaceReady: return "surface_ready.wav";
        case rocket::GameAudioCue::TakeoffIgnition: return "takeoff_ignition.wav";
        }
        return {};
    }
};

void syncDomViewportLayout()
{
    if (!g_platformHost || !g_uiBridge) return;
    const rocket::ViewportMetrics viewport = g_platformHost->viewportMetrics();
    const rocket::UiSurfaceKind surface = g_uiBridge->viewportSurfaceKind();
    if (viewport.logicalWidth == g_domLayoutWidth
        && viewport.logicalHeight == g_domLayoutHeight
        && surface == g_domLayoutSurface) {
        return;
    }

    g_uiBridge->setUiViewportLayout(rocket::resolveUiViewportLayout(
        viewport.logicalWidth,
        viewport.logicalHeight,
        surface));
    g_domLayoutWidth = viewport.logicalWidth;
    g_domLayoutHeight = viewport.logicalHeight;
    g_domLayoutSurface = surface;
}

void mainLoop()
{
    if (!g_platformHost) return;
    const double nowMilliseconds = emscripten_get_now();
    g_platformHost->observeAnimationFrame(nowMilliseconds);
    if (g_runner && g_preferenceStore
        && g_autoPowerPacer.shouldRun(g_preferenceStore->load(), *g_platformHost, nowMilliseconds)) {
        g_runner->frame();
    }
    syncDomViewportLayout();
}
#endif

} // namespace

extern "C" {

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_new_game()
{
    if (g_app) {
        g_app->newGame();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_continue_game()
{
    if (g_app) {
        g_app->continueGame();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_start_launch()
{
    if (g_app) {
        g_app->startLaunch();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_launch_move(double steerAxis, double throttleAxis)
{
    if (g_app) {
        g_app->launchMove(steerAxis, throttleAxis);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_deploy_surface_team()
{
    if (g_app) {
        g_app->deploySurfaceTeam();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_orbital_work(int held) { if (g_app) g_app->orbitalWorkInput(held != 0); }

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_resume_orbital_flight() { if (g_app) g_app->resumeOrbitalFlight(); }

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_depart_surface_undeployed()
{
    if (g_app) {
        g_app->departSurfaceUndeployed();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int rr_surface_arrival_active()
{
    return g_app && g_app->inputContext() == rocket::InputContext::SurfaceArrival ? 1 : 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_prepare_launch()
{
    if (g_app) {
        g_app->prepareForLaunch();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_return_home()
{
    if (g_app) {
        g_app->returnHome();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int rr_current_screen()
{
    return g_app ? g_app->currentScreen() : -1;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_cut_engines()
{
    if (g_app) {
        g_app->cutEngines();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_next()
{
    if (g_app) {
        g_app->next();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_attempt_frontier()
{
    if (g_app) {
        g_app->attemptFrontierTransfer();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_open_navigation()
{
    if (g_app) {
        g_app->openNavigation();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_ark_jump()
{
    if (g_app) {
        g_app->arkJump();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_select_navigation(int index)
{
    if (g_app) {
        g_app->selectNavigationDestination(index);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_buy_offer(int index)
{
    if (g_app) {
        g_app->buyOffer(index);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_select_refit_offer(int index)
{
    if (g_app) {
        g_app->selectRefitOffer(index);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_reroll_offers()
{
    if (g_app) {
        g_app->rerollOffers();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_acknowledge_approach_introduction()
{
    if (g_app) {
        g_app->acknowledgeApproachIntroduction();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_research_project(int index)
{
    if (g_app) {
        g_app->selectResearchProject(index);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_skip_research()
{
    if (g_app) {
        g_app->skipResearch();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_mine_surface()
{
    if (g_app) {
        g_app->mineSurface();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_extract_surface()
{
    if (g_app) {
        g_app->extractSurface();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_surface_upgrade(int index)
{
    if (g_app) {
        g_app->selectSurfaceUpgrade(index);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_drone_ops()
{
    if (g_app) {
        g_app->openDroneOps();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_equip_drone(int index)
{
    if (g_app) {
        g_app->equipDrone(index);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_unequip_drone_slot(int slotIndex)
{
    if (g_app) {
        g_app->unequipDroneSlot(slotIndex);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_upgrade_drone_slot()
{
    if (g_app) {
        g_app->upgradeDroneSlot();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_mining_move(double xAxis, double yAxis)
{
    if (g_app) {
        g_app->miningMove(xAxis, yAxis);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_mining_aim(double normalizedX, double normalizedY)
{
    if (g_app) {
        g_app->miningAim(normalizedX, normalizedY);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_mining_pointer_aim(double viewportX, double viewportY)
{
    if (g_app) {
        g_app->miningPointerAim(viewportX, viewportY);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_mining_fire(int active)
{
    if (g_app) {
        g_app->miningFire(active != 0);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_mining_drill(int active)
{
    if (g_app) {
        g_app->miningDrill(active != 0);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_mining_keyboard_drill(int active)
{
    if (g_app) {
        g_app->miningKeyboardDrill(active != 0);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_mining_scanner()
{
    if (g_app) {
        g_app->miningScanner();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_mining_tether()
{
    if (g_app) {
        g_app->miningTether();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_mining_operator_toggle()
{
    if (g_app) {
        g_app->miningOperatorToggle();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_mining_operator_toggle_progress(double progress)
{
    if (g_app) {
        g_app->miningOperatorToggleProgress(progress);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_mining_repair_drill()
{
    if (g_app) {
        g_app->miningRepairDrill();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_mining_repair_drone()
{
    if (g_app) {
        g_app->miningRepairDrone();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_mining_stow()
{
    if (g_app) {
        g_app->miningStow();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_mining_wait_for_drones()
{
    if (g_app) {
        g_app->miningWaitForDrones();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_mining_abort()
{
    if (g_app) {
        g_app->miningAbort();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_mining_failure_ack()
{
    if (g_app) {
        g_app->miningFailureAck();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_debug_mining()
{
    if (g_app) {
        g_app->debugStartMining();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_debug_combat_mining()
{
    if (g_app) {
        g_app->debugStartCombatMining();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_debug_swarm_arena()
{
    if (g_app) {
        g_app->debugStartSwarmArena();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_debug_mining_arena(
    int act,
    int difficulty,
    double seed,
    int loadoutMode,
    int gateOverride,
    int destinationTierOverride,
    int postSolarSystemOverride,
    int bodyIndex)
{
    if (g_app) {
        g_app->debugStartMiningArena(
            act,
            difficulty,
            static_cast<std::uint64_t>(std::max(1.0, seed)),
            loadoutMode,
            gateOverride,
            destinationTierOverride,
            postSolarSystemOverride,
            bodyIndex);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
const char* rr_debug_post_solar_body_preview(int systemOverride, int bodyIndex, double seed)
{
    g_debugMiningPreview = g_app
        ? g_app->debugPostSolarBodyPreview(
            systemOverride,
            bodyIndex,
            static_cast<std::uint64_t>(std::max(1.0, seed)))
        : std::string {};
    return g_debugMiningPreview.c_str();
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
const char* rr_debug_mining_preview(int act, int difficulty, int gateOverride)
{
    g_debugMiningPreview = g_app
        ? g_app->debugMiningArenaPreview(act, difficulty, gateOverride)
        : std::string {};
    return g_debugMiningPreview.c_str();
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_debug_hangar()
{
    if (g_app) {
        g_app->debugShowHangar();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_debug_jupiter_options(int mode)
{
    if (g_app) {
        g_app->debugShowJupiterOptions(mode);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_debug_results()
{
    if (g_app) {
        g_app->debugShowResults();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_debug_research()
{
    if (g_app) {
        g_app->debugShowResearch();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_debug_refit()
{
    if (g_app) {
        g_app->debugShowRefit();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_debug_surface_upgrade()
{
    if (g_app) {
        g_app->debugShowSurfaceUpgrade();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_debug_drone_ops()
{
    if (g_app) {
        g_app->debugShowDroneOps();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_debug_navigation()
{
    if (g_app) {
        g_app->debugShowNavigation();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_debug_act_one_start()
{
    if (g_app) {
        g_app->debugStartActOneFlow();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_debug_act_one_previous()
{
    if (g_app) {
        g_app->debugPreviousActOneCheckpoint();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_debug_act_one_next()
{
    if (g_app) {
        g_app->debugNextActOneCheckpoint();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int rr_debug_act_one_checkpoint()
{
    return g_app ? g_app->debugActOneCheckpoint() : -1;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_debug_launch_lesson(int lessonIndex)
{
    if (g_app) {
        g_app->debugStartLaunchLesson(lessonIndex);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_debug_surface_arrival(int destinationIndex, int phaseIndex)
{
    if (g_app) {
        g_app->debugStartSurfaceArrival(destinationIndex, phaseIndex);
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_debug_exit()
{
    if (g_app) {
        g_app->debugExit();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_repair_ship()
{
    if (g_app) {
        g_app->repairShip();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_reset_save()
{
    if (g_app) {
        g_app->resetSave();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int rr_rml_mouse_move(int x, int y)
{
    return g_app && g_app->uiMouseMove(x, y) ? 1 : 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int rr_rml_mouse_down(int x, int y, int button)
{
    return g_app && g_app->uiMouseDown(x, y, button) ? 1 : 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int rr_rml_mouse_up(int x, int y, int button)
{
    return g_app && g_app->uiMouseUp(x, y, button) ? 1 : 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int rr_rml_wheel(int x, int y, double deltaY)
{
    return g_app && g_app->uiMouseWheel(x, y, deltaY) ? 1 : 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int rr_rml_hit_test(int x, int y)
{
    return g_app && g_app->uiHitTest(x, y) ? 1 : 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int rr_ui_navigate(int direction)
{
    if (!g_app || direction < static_cast<int>(rocket::UiDirection::Up) ||
        direction > static_cast<int>(rocket::UiDirection::Right)) {
        return 0;
    }
    return g_app->uiNavigate(static_cast<rocket::UiDirection>(direction)) ? 1 : 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int rr_ui_activate_focused()
{
    return g_app && g_app->uiActivateFocused() ? 1 : 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int rr_ui_cancel()
{
    return g_app && g_app->uiCancel() ? 1 : 0;
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
const char* rr_controller_debug_status()
{
    g_controllerDebugStatus = g_gamepadSource
        ? g_gamepadSource->debugStatusJson()
        : "{\"connected\":false,\"index\":-1,\"family\":\"generic\",\"id\":\"\"}";
    return g_controllerDebugStatus.c_str();
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
const char* rr_controller_app_debug_status()
{
    g_controllerAppDebugStatus = g_app
        ? g_app->controllerDebugStatusJson()
        : "{\"context\":\"ui\",\"focusId\":\"\",\"lastAction\":\"\",\"pauseReason\":\"none\"}";
    return g_controllerAppDebugStatus.c_str();
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_debug_controller_set(
    int connected,
    double leftX,
    double leftY,
    double rightX,
    double rightY,
    unsigned int buttonMask)
{
    if (!g_gamepadSource) {
        return;
    }
    if (connected == 0) {
        g_gamepadSource->clearSyntheticSnapshot();
        return;
    }
#ifdef __EMSCRIPTEN__
    if (rr_controller_debug_tools_enabled() == 0) {
        return;
    }
#endif

    rocket::RawControllerSnapshot snapshot;
    snapshot.connected = true;
    snapshot.standardMapping = true;
    snapshot.family = rocket::ControllerFamily::Generic;
    snapshot.id = "Controller Lab Synthetic Gamepad";
    snapshot.leftX = std::clamp(leftX, -1.0, 1.0);
    snapshot.leftY = std::clamp(leftY, -1.0, 1.0);
    snapshot.rightX = std::clamp(rightX, -1.0, 1.0);
    snapshot.rightY = std::clamp(rightY, -1.0, 1.0);
    for (std::size_t index = 0; index < rocket::controllerButtonCount; ++index) {
        snapshot.buttons[index] = (buttonMask & (1u << index)) != 0 ? 1.0 : 0.0;
    }
    g_gamepadSource->setSyntheticSnapshot(snapshot);
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_debug_controller_clear()
{
    if (g_gamepadSource) {
        g_gamepadSource->clearSyntheticSnapshot();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
int rr_controller_haptic(double durationSeconds, double weakMagnitude, double strongMagnitude)
{
    return g_gamepadSource
        && g_gamepadSource->playHaptic(durationSeconds, weakMagnitude, strongMagnitude)
        ? 1
        : 0;
}

} // extern "C"

int main()
{
#ifdef __EMSCRIPTEN__
    g_gamepadSource = std::make_unique<rocket::WebGamepadSource>();
    g_saveStore = std::make_unique<rocket::WebSaveStore>();
    g_preferenceStore = std::make_unique<rocket::WebPreferenceStore>();
    g_platformHost = std::make_unique<rocket::WebPlatformHost>(*g_gamepadSource);
    if (!g_platformHost->createGraphicsContext()) return 1;
    g_textureSource = std::make_unique<rocket::WebTextureSource>();
    g_uiBridge = std::make_unique<rocket::WebUiBridge>();
    g_renderer = std::make_unique<rocket::WebGlGraphicsBackend>(*g_platformHost, *g_textureSource);
    g_rmlRenderHost = std::make_unique<rocket::WebGlRmlRenderHost>();
    g_ui = std::make_unique<rocket::GameRmlUi>(
        *g_preferenceStore, *g_platformHost, *g_uiBridge, *g_rmlRenderHost);
    g_audio = std::make_unique<WebGameAudio>();
    g_services = std::make_unique<rocket::AppServices>(rocket::AppServices {
        *g_saveStore, *g_preferenceStore, *g_platformHost, *g_gamepadSource,
        *g_textureSource, *g_renderer, *g_ui, *g_uiBridge, g_audio.get()
    });
    g_runner = std::make_unique<rocket::GameRunner>(*g_services);
    if (!g_runner->initialize()) return 1;
    g_app = &g_runner->app();
    syncDomViewportLayout();
    emscripten_set_main_loop(mainLoop, 0, 1);
#endif

    return 0;
}
