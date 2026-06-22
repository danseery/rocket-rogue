#include "game/RocketGameApp.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <memory>

namespace {

std::unique_ptr<rocket::RocketGameApp> g_app;
double g_lastTimeMs = 0.0;

#ifdef __EMSCRIPTEN__
void mainLoop()
{
    const double now = emscripten_get_now();
    const double delta = g_lastTimeMs <= 0.0 ? 1.0 / 60.0 : (now - g_lastTimeMs) / 1000.0;
    g_lastTimeMs = now;

    if (g_app) {
        g_app->tick(delta);
        g_app->render();
    }
}
#endif

} // namespace

extern "C" {

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
void rr_eject_now()
{
    if (g_app) {
        g_app->ejectNow();
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
void rr_arrival_ops()
{
    if (g_app) {
        g_app->arrivalOps();
    }
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
void rr_pressure_relief()
{
    if (g_app) {
        g_app->pressureReliefValve();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_close_relief_valve()
{
    if (g_app) {
        g_app->closePressureReliefValve();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_jettison_cargo()
{
    if (g_app) {
        g_app->jettisonCargo();
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
void rr_buy_offer(int index)
{
    if (g_app) {
        g_app->buyOffer(index);
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
void rr_arrival_flyby()
{
    if (g_app) {
        g_app->runArrivalFlyby();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_arrival_orbit()
{
    if (g_app) {
        g_app->enterArrivalOrbit();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_arrival_landing()
{
    if (g_app) {
        g_app->attemptArrivalLanding();
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
void rr_survey_surface()
{
    if (g_app) {
        g_app->surveySurface();
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
void rr_push_surface()
{
    if (g_app) {
        g_app->pushSurface();
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
void rr_repair_ship()
{
    if (g_app) {
        g_app->repairShip();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_recruit_crew()
{
    if (g_app) {
        g_app->recruitCrew();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_train_crew()
{
    if (g_app) {
        g_app->trainCrew();
    }
}

#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void rr_rest_crew()
{
    if (g_app) {
        g_app->restCrew();
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

} // extern "C"

int main()
{
    g_app = std::make_unique<rocket::RocketGameApp>();
    g_app->initialize();

#ifdef __EMSCRIPTEN__
    g_lastTimeMs = emscripten_get_now();
    emscripten_set_main_loop(mainLoop, 0, 1);
#endif

    return 0;
}
