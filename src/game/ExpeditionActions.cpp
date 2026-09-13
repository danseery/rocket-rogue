#include "game/RocketGameApp.h"
#include "core/ExpeditionSystem.h"
#include "core/PayloadTransfer.h"
#include "core/ResearchSystem.h"
#include "core/SolarProgression.h"
#include <charconv>

namespace rocket {
void RocketGameApp::toggleCruiseControl() {
    if (!state_.run.expedition.travelInitialized || state_.screen != Screen::Flight || services_.ui.modalOpen() ||
        session_.flight.mode == FlightMode::Landing || !session_.flight.active) return;
    const auto result = toggleCruise(state_.run.expedition);
    queueAudioCue(result == ExpeditionResult::InvalidTarget ? GameAudioCue::UiError : GameAudioCue::EngineToggle);
    state_.statusLine = result == ExpeditionResult::InvalidTarget ? "Set a waypoint on the map first." :
        state_.run.expedition.cruise.active ? "CRUISE ACTIVE - manual approach required. C / L3 to cancel." : "CRUISE OFF - manual flight.";
    save();
    panelDirty_ = true;
}
bool RocketGameApp::runExpeditionAction(const std::string& action) {
    if (!action.starts_with("expedition:")) return false;
    auto& e = state_.run.expedition;
    if (!e.travelInitialized) return true;
    if (surfaceBaySequence_.active() || sceneTransition_.active() || session_.destruction.active) return true;
    const auto release = [&] {
        releaseRealtimeInputs(true);
        messageMoveReleaseRequired_ = messageDrillReleaseRequired_ = messageFireReleaseRequired_ = messageControllerNeutralRequired_ = true;
    };
    const auto close = [&] {
        services_.ui.closeModal();
        session_.waypointPreviewCourse = {};
        if (pauseReason_ == PauseReason::BlockingModal || pauseReason_ == PauseReason::ControllerUiFocus) clearControllerPause();
        release();
    };
    const auto plot = [&](std::string_view id, bool playerSelected = false) {
        const auto* body = systemBody(solarSystemDefinition(), id);
        if (!body || !solarBodyRevealed(state_, catalog_, body->id)) {
            state_.statusLine = "That destination has not been discovered.";
            return ExpeditionResult::InvalidTarget;
        }
        const auto model = expeditionFlightModel(state_, catalog_);
        const auto result = plotSystemCourse(e, session_.flight, solarSystemDefinition(), id, &model);
        if (result == ExpeditionResult::Applied) e.coursePlayerSelected = playerSelected;
        return result;
    };
    const auto preview = [&](std::string_view id) {
        const auto* body = systemBody(solarSystemDefinition(), id);
        if (!body || !solarBodyRevealed(state_, catalog_, body->id)) {
            state_.statusLine = "That destination has not been discovered.";
            return ExpeditionResult::InvalidTarget;
        }
        auto position = e.location;
        captureSystemLocation(position, session_.flight);
        const auto model = expeditionFlightModel(state_, catalog_);
        session_.waypointPreviewCourse = previewSystemCourse(
            position, session_.flight, solarSystemDefinition(), id, e.homeBodyId, &model);
        return session_.waypointPreviewCourse.targetBodyId.empty()
            ? ExpeditionResult::InvalidTarget
            : ExpeditionResult::Applied;
    };
    if (action == "expedition:retry_opening") {
        if (retryOpeningMission(state_,catalog_) == ExpeditionResult::Applied) {
            resetExpeditionSessionAfterRecovery();
            close();
            beginSceneFadeFromBlack(1.0);
            save();
            panelDirty_=realtimeHudDirty_=true;
            refreshPanel();
        }
        return true;
    }
    if (action == "expedition:map") {
        session_.waypointPreviewCourse = {};
        if (!e.course.targetBodyId.empty()) preview(e.course.targetBodyId);
        release();
        refreshPanel();
        services_.ui.openModal("map");
        return true;
    }
    if (action == "expedition:close") { close(); }
    else if (action.starts_with("expedition:preview:")) {
        preview(action.substr(19));
        refreshPanel();
        return true;
    } else if (action.starts_with("expedition:plot:")) {
        if (plot(action.substr(16), true) == ExpeditionResult::Applied) {
            queueAudioCue(GameAudioCue::Orbit);
            close();
            const auto* waypoint = systemBody(solarSystemDefinition(), e.course.targetBodyId);
            state_.statusLine = operationalHomeDocked(e)
                ? (waypoint ? waypoint->name : std::string("Destination")) + " waypoint set. Depart dock when ready."
                : e.cruise.active ? "Waypoint set / CRUISE ACTIVE" : "Waypoint set / manual flight";
            state_.run.flight.courseNoticeSeconds = 3.0;
        }
    } else if (action.starts_with("expedition:decision:")) {
        const auto separator = action.find(':', 20);
        if (separator == std::string::npos) return true;
        const auto selection = action.substr(20, separator - 20);
        const auto occurrence = action.substr(separator + 1);
        if (!acknowledgeExpeditionDecision(state_, occurrence)) return true;
        close();
        if (selection != "map") plot(selection == "home" ? e.homeBodyId : recommendedExpeditionLead(state_, catalog_));
        save();
        if (selection == "map") return runExpeditionAction("expedition:map");
    } else if (action == "expedition:depart") {
        if (departHome(state_, catalog_) == ExpeditionResult::Applied) {
            queueAudioCue(GameAudioCue::TakeoffIgnition);
            session_.preparedLaunch = expeditionFlightModel(state_, catalog_);
            session_.flightArmed = true;
            session_.preflightElapsed = tuning::session::preflightBoardingSeconds;
            session_.orbitalWork = {};
            surfaceArrival_.reset();
            landingSiteView_.reset();
            close();
            const auto* waypoint = systemBody(solarSystemDefinition(), e.course.targetBodyId);
            state_.statusLine = "Thrust to undock" +
                (waypoint ? " toward " + waypoint->name : std::string{}) +
                ". The waypoint marks direction; flight remains manual.";
        } else state_.statusLine = "Departure is available only from an operational dock.";
    } else if (action == "expedition:dock") {
        const auto cargo = e.cargo.materials;
        const auto payout = static_cast<int>(e.cargo.credits);
        if (dockExpedition(state_, solarSystemDefinition()) == ExpeditionResult::Applied) {
            queueAudioCue(GameAudioCue::Deposit);
            close();
            session_.flight.landing = {};
            if (operationalHomeDocked(e)) {
                state_.screen = Screen::Hangar;
                state_.run.shipDamage = 0;
                state_.run.mining = {};
                state_.run.planetaryExpedition = {};
                session_.flightArmed = false;
                std::string nextWaypoint;
                if (!e.coursePlayerSelected || e.course.targetBodyId.empty() || e.course.targetBodyId == e.homeBodyId) {
                    const std::string lead = recommendedExpeditionLead(state_, catalog_);
                    const auto* body = systemBody(solarSystemDefinition(), lead);
                    if (body && body->id != e.homeBodyId && solarBodyRevealed(state_, catalog_, body->id) &&
                        plot(lead) == ExpeditionResult::Applied)
                        nextWaypoint = body->name;
                }
                state_.statusLine = "DOCKED - Banked " + std::to_string(cargo.common) + " common / " + std::to_string(cargo.rare) +
                    " rare / " + std::to_string(cargo.exotic) + " exotic and " + std::to_string(payout) +
                    " credits. Ship serviced." + (nextWaypoint.empty() ? std::string{} : " Next waypoint: " + nextWaypoint + ".");
            } else {
                state_.screen = Screen::Flight;
                e.undockReady = true;
                session_.flight.physicalFlight = true;
                session_.flight.phase = FlightPhase::Transfer;
                session_.flight.mode = FlightMode::Orbit;
                session_.flightArmed = true;
                state_.statusLine = "Derelict dock reached. No servicing available. Thrust to release.";
            }
        } else state_.statusLine = "Match the dock marker and slow below 0.20 relative speed.";
    } else if (action == "expedition:abandon") {
        if (!e.active) return true;
        refreshPanel();
        services_.ui.openModal("expedition_abandon");
        release();
        return true;
    } else if (action == "expedition:confirm_abandon") {
        close();
        if (recoverExpedition(state_, solarSystemDefinition()) == ExpeditionResult::Applied) {
            resetExpeditionSessionAfterRecovery();
        }
    } else if (action.starts_with("expedition:recover:")) {
        std::uint64_t id = 0;
        const auto text = std::string_view(action).substr(19);
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), id);
        captureSystemLocation(e.location, session_.flight);
        if (parsed.ec == std::errc() && parsed.ptr == text.data() + text.size()) {
            const auto result = salvageWreck(e, id, solarSystemDefinition(), shipHoldCapacity(state_, catalog_));
            state_.statusLine = result == ExpeditionResult::Applied
                ? "Wreck recovered. Upgrades restored; remaining cargo stays salvageable."
                : "Rendezvous with the wreck and match its speed to recover cargo and upgrades.";
        }
    } else if (action == "expedition:graft_conflict:keep" || action == "expedition:graft_conflict:recovered") {
        const bool recovered = action.ends_with(":recovered");
        if (resolveRecoveredGraftConflict(e, 0, recovered) == ExpeditionResult::Applied) {
            close();
            state_.statusLine = recovered ? "Recovered graft installed." : "Current graft retained.";
        }
    } else if (action.starts_with("expedition:install:")) {
        const auto* module = catalog_.findModule(action.substr(19));
        if (!module || !operationalHomeDocked(e)) return true;
        const bool installed = module->launchUpgradeKind != LaunchUpgradeKind::None ?
            (module->launchUpgradeRank == launchUpgradeRank(state_, module->launchUpgradeKind) + 1 && installLaunchUpgrade(state_, catalog_, module->launchUpgradeKind)) :
            (module->surfaceDepthUpgradeRank == surfaceDepthUpgradeRank(state_, module->surfaceDepthUpgradeKind) + 1 && installSurfaceDepthUpgrade(state_, catalog_, module->surfaceDepthUpgradeKind));
        if (installed) {
            queueAudioCue(GameAudioCue::Upgrade);
            state_.statusLine = "Installed " + module->name + ".";
            const auto model = expeditionFlightModel(state_, catalog_);
            session_.flight.fuelCapacity = session_.flight.fuelRemaining = model.fuelCapacity;
            session_.flight.hullMaximum = session_.flight.hullRemaining = tuning::launch::hullBaseIntegrity + model.hullRank * tuning::launch::hullIntegrityPerRank;
        } else {
            queueAudioCue(GameAudioCue::UiError);
        }
    } else if (action == "expedition:cruise") { toggleCruiseControl(); return true; }
    else return true;
    save();
    panelDirty_ = realtimeHudDirty_ = true;
    refreshPanel();
    return true;
}
void RocketGameApp::debugStartExpedition() {
    beginDebugSandbox("Expedition sandbox. No campaign save writes.");
    state_.screen = Screen::Hangar;
    initializeLiveExpedition(state_, catalog_);
    state_.run.credits = 100;
    session_.preparedLaunch = expeditionFlightModel(state_, catalog_);
    refreshPanel();
}
void RocketGameApp::debugStartMoonApproach(bool acknowledge) {
    beginDebugSandbox("Earth launch sandbox. No campaign save writes.");
    state_.screen = Screen::Hangar;
    initializeLiveExpedition(state_,catalog_);
    beginEarthOpening(state_,catalog_);
    if (acknowledge) {
        acknowledgeIncomingMessage(state_.incomingMessages,"campaign.lunar_approach");
        launchEarthOpening(state_,catalog_);
    }
    session_.preparedLaunch = expeditionFlightModel(state_,catalog_);
    session_.flightArmed = acknowledge;
    session_.preflightElapsed = tuning::session::preflightBoardingSeconds;
    releaseRealtimeInputs(true);
    refreshPanel();
}
} // namespace rocket
