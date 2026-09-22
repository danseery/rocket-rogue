#include "game/RocketGameApp.h"
#include "core/ExpeditionSystem.h"
#include "core/ArtifactProgression.h"
#include "core/MissionGuidance.h"
#include "core/StraylightSequence.h"
#include "core/PayloadTransfer.h"
#include "core/ResearchSystem.h"
#include "core/SolarProgression.h"
#include <charconv>

namespace rocket {
void RocketGameApp::toggleCruiseControl() {
    if (!state_.run.expedition.travelInitialized || state_.screen != Screen::Flight || services_.ui.modalOpen() ||
        (session_.flight.mode == FlightMode::Landing || session_.flight.mode == FlightMode::Docking) || !session_.flight.active) return;
    const auto result = toggleCruise(state_.run.expedition);
    queueAudioCue(result == ExpeditionResult::InvalidTarget ? GameAudioCue::UiError : GameAudioCue::EngineToggle);
    state_.statusLine = result == ExpeditionResult::InvalidTarget ? "Set a waypoint on the map first." :
        state_.run.expedition.cruise.active ? "CRUISE ACTIVE - manual approach required. C / L3 to cancel." : "CRUISE OFF - manual flight.";
    save();
    panelDirty_ = true;
}
bool RocketGameApp::runExpeditionAction(const std::string& action) {
    if (!action.starts_with("expedition:")) return false;
    if (serviceDockingActive(session_.flight) && session_.flight.docking.securing) return true;
    auto& e = state_.run.expedition;
    if (!e.travelInitialized) return true;
    if (action == "expedition:missions" || action == "expedition:mission_history") {
        if (action == "expedition:mission_history") showCompletedMissions_ = !showCompletedMissions_;
        releaseRealtimeInputs(true);
        if (action == "expedition:missions") services_.ui.closeModal();
        refreshPanel();
        services_.ui.openModal("missions");
        return true;
    }
    if (action == "expedition:missions_close") {
        services_.ui.closeModal();
        releaseRealtimeInputs(true);
        clearControllerPause();
        return true;
    }
    if (action.starts_with("expedition:straylight:")) {
        if (applyStraylightAction(state_, catalog_, std::string_view(action).substr(22))) {
            if (action == "expedition:straylight:install")
                reconcileCampaignGuidance(state_,catalog_,true);
            services_.ui.closeModal();
            releaseRealtimeInputs(true);
            clearControllerPause();
            straylightElapsed_ = 0;
            session_.flightArmed = state_.run.flight.active || e.undockReady;
            save(); refreshPanel();
        }
        return true;
    }
    if (action.starts_with("expedition:artifact_handin:")) {
        if (completeBankedArtifact(state_,catalog_,action.substr(27))) {
            state_.statusLine = "Artifact secured. Mission complete.";
            queueAudioCue(GameAudioCue::Deposit);
            reconcileCampaignGuidance(state_,catalog_);
        }
        save(); panelDirty_=true; refreshPanel(); return true;
    }
    if (straylightCommitted(state_) || (straylightOwnsPresentation(state_) &&
        !(state_.meta.straylightStage == StraylightStage::RetrieveBeacons && action == "expedition:depart"))) return true;
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
        if (!courseWreck(e,id) && (!body || !solarBodyRevealed(state_, catalog_, body->id))) {
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
    if (action == "expedition:follow_mission") {
        reconcileCampaignGuidance(state_,catalog_,true);
        close(); save(); panelDirty_=true; refreshPanel(); return true;
    }
    if (action.starts_with("expedition:track:")) {
        const auto mission = missionView(state_, catalog_, std::string_view(action).substr(17));
        if (mission.available && !mission.complete) {
            e.trackedMissionId = mission.id;
            if (!e.coursePlayerSelected) reconcileCampaignGuidance(state_, catalog_, true);
            close(); save(); panelDirty_ = true; refreshPanel();
        }
        return true;
    }
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
        services_.ui.closeModal();
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
            state_.statusLine = operationalHomeDocked(e)
                ? courseTargetName(e,solarPresentationSystem(state_),e.course.targetBodyId) + " waypoint set. Depart dock when ready."
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
        if (state_.meta.straylightStage == StraylightStage::RetrieveBeacons && e.location.bodyId == "straylight") {
            e.undockReady = true;
            state_.screen = Screen::Flight;
            session_.flightArmed = true;
            if (departDock(e, session_.flight) == ExpeditionResult::Applied) {
                close(); save(); refreshPanel();
            }
            return true;
        }
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
                (waypoint ? " toward " + courseTargetName(e,solarPresentationSystem(state_),waypoint->id) : std::string{}) +
                ". The waypoint marks direction; flight remains manual.";
        } else state_.statusLine = "Departure is available only from an operational dock.";
    } else if (action == "expedition:dock") {
        const bool earthSettlementReady = session_.flight.docking.settlementReady &&
            e.location.bodyId == session_.flight.docking.dockId && e.location.siteId == e.location.bodyId + ".dock";
        if (session_.flight.active && service_dock::supported(e.location.bodyId) && !earthSettlementReady) {
            state_.statusLine = "Docking approach engages automatically. Align with the berth.";
            panelDirty_ = true;
            return true;
        }
        reconcileArtifactCustody(state_,catalog_);
        state_.run.flight = session_.flight;
        const auto cargo = e.cargo.materials;
        const auto payout = static_cast<int>(e.cargo.credits);
        const auto batteriesBeforeDock = e.batteries;
        if (dockExpedition(state_, solarSystemDefinition()) == ExpeditionResult::Applied) {
            session_.flight = state_.run.flight;
            session_.flight.docking = {};
            bankMissionArtifacts(state_,catalog_);
            queueAudioCue(GameAudioCue::Deposit);
            close();
            session_.flight.landing = {};
            if (e.location.bodyId == "straylight" && state_.meta.straylightStage == StraylightStage::Approach) {
                state_.meta.straylightStage = StraylightStage::FirstContact;
                e.undockReady = false;
                session_.flightArmed = false;
                straylightElapsed_ = 0;
            } else if (e.location.bodyId == "straylight" && state_.meta.straylightStage == StraylightStage::RetrieveBeacons) {
                e.undockReady = false;
                session_.flightArmed = false;
            } else if (operationalHomeDocked(e)) {
                state_.screen = Screen::Hangar;
                state_.run.shipDamage = 0;
                state_.run.mining = {};
                state_.run.planetaryExpedition = {};
                session_.flightArmed = false;
                reconcileCampaignGuidance(state_,catalog_);
                const std::string nextWaypoint=courseTargetName(e,solarPresentationSystem(state_),e.course.targetBodyId);
                state_.statusLine = "DOCKED - Secured " + std::to_string(cargo.common) + " common / " + std::to_string(cargo.rare) +
                    " rare / " + std::to_string(cargo.exotic) + " exotic and " + std::to_string(payout) +
                    " credits. Ship serviced." + (nextWaypoint.empty() ? std::string{} : " Next waypoint: " + nextWaypoint + ".");
                std::string bankedNames;
                for (std::size_t i = 0; i < e.batteries.size(); ++i) {
                    const auto& b = e.batteries[i];
                    if (batteriesBeforeDock[i].owner != BatteryOwner::Ship ||
                        (b.owner != BatteryOwner::EarthStorage && b.owner != BatteryOwner::ArkSlot)) continue;
                    const auto* origin = systemBody(solarSystemDefinition(), b.id);
                    if (!bankedNames.empty()) bankedNames += ", ";
                    bankedNames += origin ? origin->name : b.id;
                }
                if (!bankedNames.empty())
                    state_.statusLine = "ARTIFACT SECURED AT DOCK - " + bankedNames + ". " + state_.statusLine;
            } else {
                state_.screen = Screen::Flight;
                e.undockReady = true;
                session_.flight.physicalFlight = true;
                session_.flight.phase = FlightPhase::Transfer;
                session_.flight.mode = FlightMode::Orbit;
                session_.flightArmed = true;
                state_.statusLine = "Derelict dock reached. No servicing available. Thrust to release.";
            }
        } else state_.statusLine = "Approach the dock and slow down.";
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
            const bool artifactOnWreck=wreckCarriesArtifact(e,id);
            const bool following=!e.coursePlayerSelected || e.course.targetBodyId=="wreck:"+std::to_string(id);
            const auto result = salvageWreck(e, id, solarSystemDefinition(), shipHoldCapacity(state_, catalog_));
            const bool beaconMission = state_.meta.straylightStage == StraylightStage::RetrieveBeacons;
            if (result==ExpeditionResult::Applied && (following || (artifactOnWreck && beaconMission)))
                reconcileCampaignGuidance(state_,catalog_,true);
            state_.statusLine = result == ExpeditionResult::Applied
                ? (artifactOnWreck ? (beaconMission ? "Beacon recovered. Recovery waypoint updated. Remaining ore stays salvageable." : "Artifact recovered — return to the Earth dock to complete the mission. Remaining ore stays salvageable.") : "Wreck recovered. Upgrades restored; remaining cargo stays salvageable.")
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
    save(action == "expedition:dock" && !session_.flight.active && !e.location.siteId.empty());
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
void RocketGameApp::debugStartDockArrival(bool bump) {
    beginDebugSandbox("Dock feedback preview. No campaign save writes.");
    initializeLiveExpedition(state_, catalog_);
    state_.screen = Screen::Flight;
    state_.meta.campaignIntroductionAcknowledged = true;
    state_.incomingMessages = {};
    state_.incomingMessages.acknowledgedMessages.push_back("earth_dock_intro");
    earthDockIntroEligibleAfterReload_ = false;
    auto& expedition = state_.run.expedition;
    expedition.active = true;
    expedition.undockReady = false;
    expedition.departureCount = 1;
    expedition.course.targetBodyId = "earth";
    auto& flight = session_.flight;
    flight.active = flight.physicalFlight = true;
    flight.mode = FlightMode::Docking;
    flight.hullRemaining = flight.hullMaximum = 100;
    flight.fuelRemaining = flight.fuelCapacity = 10;
    flight.docking = {};
    flight.docking.active = flight.docking.rotationLocked = true;
    flight.docking.dockId = "earth";
    flight.docking.positionX = 0.0;
    flight.docking.positionY = service_dock::captureCenterY;
    flight.docking.velocityX = bump ? .4 : 0.0;
    flight.docking.enteredMouth = !bump;
    flight.heading = flight.docking.dockHeading + 3.141592653589793;
    flight.positionX = flight.docking.positionX;
    flight.positionY = flight.docking.positionY;
    const auto& system = solarSystemDefinition();
    const auto* earth = systemBody(system, "earth");
    const auto dock = systemDockPosition(*earth);
    expedition.location = convertSystemFrame({system.id, {}, CoordinateFrame::System,
        {dock.x + flight.positionX, dock.y + flight.positionY}, earth->velocity, flight.heading, {}},
        CoordinateFrame::Body, earth->id, system);
    session_.preparedLaunch = expeditionFlightModel(state_, catalog_);
    session_.flightArmed = true;
    services_.ui.closeModal();
    clearControllerPause();
    refreshPanel();
}
void RocketGameApp::debugStartStraylight(int requested) {
    beginDebugSandbox("Straylight sequence preview. No campaign save writes.");
    initializeLiveExpedition(state_,catalog_);
    auto& e=state_.run.expedition;
    e.straylightRevealed=true;
    state_.meta.campaignMilestone=CampaignMilestone::ArkDiscovered;
    state_.meta.ark.condition=ArkCondition::DerelictOperable;
    const auto* ark=systemBody(solarSystemDefinition(),"straylight");
    e.location={"solar","straylight",CoordinateFrame::Body,ark->dockOffset,{},0,"straylight.dock"};
    state_.run.flight.active=false;
    state_.run.flight.fuelRemaining=state_.run.flight.fuelCapacity=50;
    state_.run.flight.hullRemaining=state_.run.flight.hullMaximum=100;
    const auto stage=requested==1 ? StraylightStage::Docking : requested==2 ? StraylightStage::RetrieveBeacons :
        requested==3 ? StraylightStage::Awakening : requested==4 ? StraylightStage::Boarding :
        requested==5 ? StraylightStage::Departing : requested>=6 ? StraylightStage::Approach : StraylightStage::Reveal;
    state_.meta.straylightStage=stage;
    for (auto& b:e.batteries) b.owner=requested>=3 && requested<=5 ? BatteryOwner::ArkSlot : BatteryOwner::Ship;
    if (requested>=3 && requested<=5) { e.arkActivated=true; e.homeBodyId="straylight"; }
    e.active=false;
    e.undockReady=false;
    state_.screen=Screen::Flight;
    if (requested>=6) {
        e.location.siteId.clear();
        e.location.position.x-=1;
        e.location.position.y-=.4;
        restoreSystemLocation(e.location,state_.run.flight);
        e.active=true;
        state_.run.flight.active=true;
        session_.flightArmed=true;
        session_.preparedLaunch=expeditionFlightModel(state_,catalog_);
        plotSystemCourse(e,state_.run.flight,solarSystemDefinition(),"straylight");
        if (requested>=7) {
            auto& flight=state_.run.flight;
            flight.mode=FlightMode::Docking;
            flight.docking={};
            flight.docking.active=flight.docking.rotationLocked=true;
            flight.docking.dockId="straylight";
            flight.docking.dockHeading=service_dock::arkHeading;
            flight.docking.positionY=requested==8 ? .34 : .85;
            flight.docking.positionX=requested==8 ? .04 : -.10;
            flight.heading=0;
            flight.positionX=flight.docking.positionX;
            flight.positionY=flight.docking.positionY;
            flight.velocityX=flight.velocityY=0;
            flight.handoff={};
            state_.incomingMessages={};
        }
    }
    straylightElapsed_=0;
    services_.ui.closeModal();
    releaseRealtimeInputs(true);
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
