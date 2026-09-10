#include "core/Content.h"
#include "core/ScenarioSystem.h"
#include "core/ExpeditionPersistence.h"
#include "core/ExpeditionSystem.h"
#include "core/LaunchSimulation.h"
#include "core/SaveData.h"
#include "core/ResearchSystem.h"
#include "core/PayloadTransfer.h"
#include "core/MiningSystem.h"
#include "core/FlightSystem.h"
#include "core/SolarProgression.h"
#include <cmath>
#include <stdexcept>
#include <iostream>

namespace
{
void check(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        throw std::runtime_error(message);
    }
}
} // namespace
void persistentExpeditionTests()
{
    using namespace rocket;
    {
        const auto catalog = createDefaultContent();
        auto state = createNewGame(catalog, 0x5A17ULL);
        const auto& system = solarSystemDefinition();
        const auto revealed = [&](std::string_view id) {
            const auto* body = systemBody(system, id);
            return body && solarBodyRevealed(state, catalog, id);
        };
        check(revealed("sun") && revealed("earth") && revealed("moon"),
            "The opening chart must contain only the known Earth-Moon system");
        check(!revealed("venus") && !revealed("mars") && !revealed("straylight"),
            "Uncharted worlds and the Ark must remain absent from the system map");
        state.meta.unlockKeys.push_back(content::unlock::routeMars);
        check(revealed("mars") && !revealed("jupiter"),
            "A route unlock must reveal its destination without leaking the next route");
        check(revealed("venus"), "The Moon claim route must reveal both optional inner worlds");
        state.meta.campaignMilestone = CampaignMilestone::ArkDiscovered;
        check(revealed("straylight"),
            "The Ark must appear only after the saved Triton-claim milestone");
    }
    {
        const auto catalog = createDefaultContent();
        auto state = createNewGame(catalog, 776);
        performScenarioAction(state,catalog,content::scenario::lunarProspector,"briefing",ScenarioActionKind::AcknowledgeBriefing);
        recordScenarioEvent(state,catalog,{ScenarioEventKind::SafeMaterialDelivered,
            content::scenario::lunarProspector,"delivery","moon","common",20,0});
        check(state.meta.ownedDroneIds.empty(), "Ore delivery must not grant the Prospector");
        recordScenarioEvent(state,catalog,{ScenarioEventKind::ProtectedObjectiveExtracted,
            content::scenario::lunarProspector,"anomaly","moon",content::miningSite::lunarAnomalyCrevice,1,0});
        auto objective = scenarioObjectiveForDestination(state, catalog, content::destination::moon);
        check(objective.state == ScenarioStepState::ReadyToClaim &&
              objective.actionLabel == "Confirm Recovery",
            "The Moon reward action must describe claiming the recovered artifact, not locking Mars");
        performScenarioAction(state,catalog,content::scenario::lunarProspector,"anomaly",ScenarioActionKind::ClaimReward);
        check(state.meta.equippedDroneIds == std::vector<std::string>{content::drone::miningDrone},
            "First artifact recovery must grant and equip the Prospector");
        check(!state.incomingMessages.pending.empty() && state.incomingMessages.pending.back().messageId=="prospector_unlocked",
            "Prospector reward must introduce the helper with an incoming message");
        performScenarioAction(state,catalog,content::scenario::lunarProspector,"anomaly",ScenarioActionKind::ClaimReward);
        check(state.meta.equippedDroneIds.size()==1,"Repeated recovery must not duplicate the Prospector");
        performScenarioAction(state,catalog,content::scenario::marsBayExpansion,"briefing",ScenarioActionKind::AcknowledgeBriefing);
        recordScenarioEvent(state,catalog,{ScenarioEventKind::SafeMaterialDelivered,
            content::scenario::marsBayExpansion,"delivery","mars","common",tuning::research::marsBayCommonOreGoal,0});
        recordScenarioEvent(state,catalog,{ScenarioEventKind::ArtifactRecovered,
            content::scenario::marsBayExpansion,"artifact","mars",content::protectedObjective::marsSignalArtifact,1,0});
        objective = scenarioObjectiveForDestination(state, catalog, content::destination::mars);
        check(objective.state == ScenarioStepState::ReadyToClaim &&
              objective.actionLabel == "Claim Mars Mission",
            "The Mars reward action must describe claiming the recovered artifact, not locking Jupiter");
    }

    {
        const auto catalog = createDefaultContent();
        auto state = createNewGame(catalog, 0xD0C6ULL);
        check(initializeLiveExpedition(state, catalog), "Dock-range fixture must initialize live travel");
        check(departHome(state, catalog) == ExpeditionResult::Applied,
            "Dock-range fixture must enter physical flight");
        const auto& system = solarSystemDefinition();
        const auto* earth = systemBody(system, "earth");
        check(earth != nullptr, "Dock-range fixture requires Earth");
        const auto dock = systemDockPosition(*earth);
        auto& expedition = state.run.expedition;
        auto& flight = state.run.flight;
        expedition.location = {system.id, "", CoordinateFrame::System,
            {dock.x + expeditionDockRadius, dock.y}, {earth->velocity.x + 1.0, earth->velocity.y}, 0, {}};
        restoreSystemLocation(expedition.location, flight);
        flight.active = flight.physicalFlight = true;
        flight.mode = FlightMode::Travel;
        check(expeditionDockInRange(expedition, flight, system, "earth"),
            "Dock alert and action range must include the shared radius boundary");
        check(!canDockExpedition(expedition, flight, system),
            "Being in range must not bypass the matched-speed docking requirement");
        expedition.location.velocity = earth->velocity;
        restoreSystemLocation(expedition.location, flight);
        check(canDockExpedition(expedition, flight, system),
            "Matching dock speed at the shared boundary must allow docking");
        expedition.location.position.x += 0.001;
        restoreSystemLocation(expedition.location, flight);
        check(!expeditionDockInRange(expedition, flight, system, "earth") &&
              !canDockExpedition(expedition, flight, system),
            "Dock alert and docking eligibility must end together outside the shared radius");
    }

    {
        const auto catalog = createDefaultContent();
        auto fixture = [&]() {
            auto state = createNewGame(catalog, 912);
            auto& m = state.run.mining;
            m.active = true;
            m.terrain.width = m.terrain.height = 24;
            m.terrain.cells.resize(24*24);
            m.droneX = m.droneY = 10.5;
            m.returnZoneX = m.returnZoneY = 2.5;
            m.gravityStrength = 0;
            m.rigOxygen.current = m.rigOxygen.capacity = 100;
            m.droneHealth = 100;
            MiningLooseObject ore;
            ore.kind = MiningLooseObjectKind::Material;
            ore.x = 12.5; ore.y = 10.5;
            m.looseObjects.push_back(ore);
            return state;
        };
        auto state = fixture();
        for (int i=0;i<40;++i) updateMiningRun(state,catalog,.025);
        check(state.run.mining.temporaryMaterials.common == 1,
            "Rig must pull nearby loose ore inward and collect it once");
        auto drillReach = fixture();
        drillReach.run.mining.looseObjects.front().x =
            drillReach.run.mining.droneX + tuning::mining::drillRangeCells + 0.65;
        const double drillReachStartX = drillReach.run.mining.looseObjects.front().x;
        updateMiningRun(drillReach,catalog,.025);
        check(drillReach.run.mining.looseObjects.front().x < drillReachStartX,
            "Base attraction must cover the Rig's full mounted drill reach");
        auto full = fixture();
        full.run.mining.cargo = miningRigCargoCapacityMass();
        full.run.mining.temporaryMaterials.common = miningRigCargoCapacityMass();
        updateMiningRun(full,catalog,.025);
        check(full.run.mining.looseObjects.size()==1 && full.run.mining.looseObjects.front().x==12.5,
            "Full Rig must leave loose ore untouched");
        auto blocked = fixture();
        *miningCellAt(blocked.run.mining.terrain,11,10) =
            {MiningCellMaterial::CommonOre,4,4,false,false};
        updateMiningRun(blocked,catalog,.025);
        check(blocked.run.mining.looseObjects.size()==1 &&
            blocked.run.mining.looseObjects.front().x == 12.5,
            "Ore attraction must not pull through rock");
        auto upgraded = fixture();
        upgraded.run.mining.looseObjects.front().x = 14.5;
        updateMiningRun(upgraded,catalog,.025);
        check(upgraded.run.mining.looseObjects.front().x == 14.5,
            "Ore outside base reach must remain outside collection");
        upgraded.run.expedition.progression.runRigUpgradeRanks = {{"ore_magnet",3}};
        updateMiningRun(upgraded,catalog,.025);
        check(upgraded.run.mining.looseObjects.front().x < 14.5,
            "Ore Magnet ranks must expand attraction reach");

        auto falling = fixture();
        auto& fallingMining = falling.run.mining;
        fallingMining.cargo = miningRigCargoCapacityMass();
        fallingMining.temporaryMaterials.common = miningRigCargoCapacityMass();
        fallingMining.gravityDirectionX = 0.0;
        fallingMining.gravityDirectionY = 1.0;
        fallingMining.gravityStrength = 8.0;
        for (int x = 0; x < fallingMining.terrain.width; ++x) {
            *miningCellAt(fallingMining.terrain, x, 12) =
                {MiningCellMaterial::Bedrock, 4, 4, false, false};
        }
        for (int i = 0; i < 160; ++i) {
            updateMiningRun(falling, catalog, .025);
        }
        check(fallingMining.looseObjects.size() == 1,
            "Uncollected drill ore must remain in the tunnel");
        const MiningLooseObject& settledOre = fallingMining.looseObjects.front();
        const MiningCell* settledCell = miningCellAt(
            fallingMining.terrain,
            static_cast<int>(std::floor(settledOre.x)),
            static_cast<int>(std::floor(settledOre.y)));
        check(settledCell != nullptr && !miningMaterialSolid(settledCell->material),
            "Gravity-driven drill ore must remain in open terrain");
        check(settledOre.y > 11.0 && settledOre.y < 11.9,
            "Gravity-driven drill ore must settle above solid rock");
    }
    {
        const auto catalog = createDefaultContent();
        auto state = createNewGame(catalog, 8421);
        state.run.expedition.travelInitialized = true;
        state.run.expedition.location = {"solar", "moon", CoordinateFrame::Body, {}, {}, 0, ""};
        for (const auto& zone : planetLandingZones()) {
            check(zone.enabled && enabledLandingZoneAt(zone.centerBearing)->id == zone.id,
                "Each fixed wedge must select itself at orbit capture");
            int memberships = 0;
            for (const auto& candidate : planetLandingZones())
                memberships += landingZoneContains(candidate, zone.centerBearing+zone.halfAngle);
            check(memberships == 1, "Shared wedge boundaries must select exactly one site");
        }
        auto flight = state.run.flight;
        flight.mode = FlightMode::Landing;
        flight.orbit.captured = flight.orbit.rewardAwarded = true;
        flight.landing.altitude = flight_landing::departureAltitude;
        flight.landing.verticalVelocity = 8.0;
        flight.landing.lateralVelocity = 3.0;
        const double ascentSpeed = std::hypot(
            flight.landing.verticalVelocity, flight.landing.lateralVelocity);
        leaveLocalLanding(flight);
        check(!flight.orbit.captured && flight.orbit.rewardAwarded,
            "Ascent must permit a new wedge capture without resetting its reward latch");
        check(std::abs(
            std::hypot(flight.velocityX, flight.velocityY) *
                flight_geometry::velocityToMetersPerSecond -
            ascentSpeed) < 1e-9,
            "Ascent handoff must preserve the displayed local flight speed");
        auto landingModel = expeditionFlightModel(state, catalog);
        landingModel.orbitRequired = true;
        landingModel.heatEnabled = landingModel.asteroidsEnabled = false;
        const auto& moon = *catalog.findDestination("moon");
        for (const auto& zone : planetLandingZones()) {
            auto descending = beginLaunchFlight(landingModel, moon);
            descending.active = descending.physicalFlight = true;
            descending.mode = FlightMode::Orbit;
            descending.orbit.captured = descending.orbit.rewardAwarded = true;
            descending.positionX = .7 * std::cos(zone.centerBearing);
            descending.positionY = .7 * std::sin(zone.centerBearing);
            // Match the Land command: cancel coast velocity, then fall inward.
            descending.velocityX = descending.velocityY = descending.selectedThrottle = 0;
            descending.landing.gateArmed = true;
            LaunchFlightStep step;
            for (int i=0;i<1000 && descending.mode != FlightMode::Landing && !step.failed;++i)
                step = updateLaunchFlight(descending,landingModel,moon,{},.05);
            check(!step.failed && descending.mode == FlightMode::Landing && step.landingZoneId == zone.id,
                "Land must enter local descent in each selected sector despite its non-orbital coast path");
        }
        SurfaceLandingBuildRequest request;
        request.destinationId = "moon"; request.zoneId = "zone_3";
        request.siteSeed = 8421; request.landingOrdinal = 1; request.allowScenarioObjectives = false;
        auto first = prepareSurfaceLanding(state, catalog, request);
        check(first.valid && prepareOrbitalSurvey(state,catalog,first,1), "Wedge survey must prepare normally");
        excavateOrbitalShaft(first,1,.7);
        first.surveyElapsed = 1.2;
        const auto seed = first.miningTemplate.arenaMetadata.seed;
        request.landingOrdinal = 9;
        const auto same = prepareSurfaceLanding(state,catalog,request);
        check(same.miningTemplate.arenaMetadata.seed == seed, "Visit count must not reseed a wedge");
        auto& expedition = state.run.expedition;
        expedition.selectedOrbitBody = "moon"; expedition.selectedOrbitZone = "zone_3";
        expedition.moonTutorialZone = "zone_3";
        expedition.sites.push_back({"solar","moon","moon.surface:zone_3",first.expeditionTemplate,
            first.miningTemplate,static_cast<const OrbitalSiteProgress&>(first)});
        request.zoneId = "zone_5";
        const auto other = prepareSurfaceLanding(state,catalog,request);
        check(other.miningTemplate.arenaMetadata.seed != seed, "Different wedges need distinct initial seeds");
        expedition.sites.push_back({"solar","moon","moon.surface:zone_5",other.expeditionTemplate,other.miningTemplate,{}});
        const auto loaded = deserializeExpedition(serializeExpedition(expedition));
        check(loaded && loaded->sites.size()==2 && loaded->selectedOrbitZone=="zone_3" && loaded->moonTutorialZone=="zone_3",
            "Selected wedge and unique tutorial assignment must survive reload");
        check(loaded->sites[0].orbital.laserRow==first.laserRow && loaded->sites[0].orbital.surveyElapsed==1.2,
            "Partial survey and drilling must survive reload independently");
        auto stored = loaded->sites[0];
        stored.mining.terrain.cells[0].material = MiningCellMaterial::Empty;
        stored.mining.terrain.cells[0].remainingToughness = 0;
        stored.mining.artifact.state = MiningArtifactState::Delivered;
        stored.mining.cargo = 12; stored.mining.temporaryMaterials.common = 12;
        request.zoneId = "zone_3";
        const auto restored = restoreSurfaceLanding(state,catalog,request,stored);
        check(restored.valid && restored.miningTemplate.terrain.cells[0].material==MiningCellMaterial::Empty &&
            restored.miningTemplate.artifact.state==MiningArtifactState::Delivered && restored.miningTemplate.cargo==0,
            "Revisit must retain excavation and delivered artifacts without restoring carried cargo");
    }
    {
        const auto& solar = solarSystemDefinition();
        const auto& earth = *systemBody(solar, "earth");
        const auto dock = systemDockPosition(earth);
        for (const auto& body : solar.bodies) {
            const double r = body.influenceRadius;
            check(std::abs(systemBodyGravityAcceleration(body, r*.5) -
                std::min(.52,.095/(r*r*.25))*body.gravityScale) < 1e-10,
                "Local gravity must preserve the inner inverse-square field");
            check(std::abs(systemBodyGravityAcceleration(body,r-1e-7)-systemBodyGravityAcceleration(body,r+1e-7)) < 1e-6,
                "Gravity must be continuous at fade entry");
            check(systemBodyGravityAcceleration(body,r*1.1)==0 &&
                systemBodyGravityAcceleration(body,r*1.1-1e-7)<1e-8,
                "Gravity must smoothly reach zero at the outer boundary");
            check(std::hypot(dock.x-body.position.x,dock.y-body.position.y) > r*1.1,
                "The Earth dock marker must lie outside every gravity region");
        }
        // Departure must not add a late clock boost after the camera has
        // mostly zoomed out. Exercise the real Mars/Moon handoff envelope.
        for (const auto* id : {"mars", "moon"}) {
            const auto& body = *systemBody(solar, id);
            const SystemDefinition isolated {"coast", {body}};
            double previousApparentSpeed = 1.0;
            for (int i=0; i<=200; ++i) {
                const double radius = .52 + (body.influenceRadius*1.5-.52)*i/200.0;
                const double blend = systemBodyApproachBlend(body, radius);
                const double clock = systemFlightTimeScale(isolated, {body.position.x+radius,body.position.y});
                const double cameraScale = std::exp(std::lerp(std::log(.25),std::log(.46/.52),blend));
                const double apparentSpeed = clock*cameraScale;
                check(apparentSpeed <= previousApparentSpeed+1e-9,
                    "Orbit departure must not accelerate screen motion as zoom releases");
                if (i>0) check(previousApparentSpeed-apparentSpeed < .003,
                    "Orbit departure scale must converge without a visible step");
                previousApparentSpeed = apparentSpeed;
            }
            const double boundary = body.influenceRadius*1.1;
            check(std::abs(systemFlightTimeScale(isolated,{body.position.x+boundary-1e-6,body.position.y})-
                systemFlightTimeScale(isolated,{body.position.x+boundary+1e-6,body.position.y})) < 1e-5,
                "Changing orbit ownership must not change the flight clock");
        }
        const auto coast = integrateSystemCoast({dock.x,dock.y,0,0},1,solar);
        check(coast.x==dock.x && coast.y==dock.y && coast.vx==0 && coast.vy==0,
            "The service dock must have no accumulated distant gravity");
        auto extended=solar;
        auto distant=earth; distant.id="distant"; distant.position={1000,1000};
        extended.bodies.push_back(distant);
        const auto a=integrateSystemCoast({9,2,.1,.2},.05,solar);
        const auto b=integrateSystemCoast({9,2,.1,.2},.05,extended);
        check(a.x==b.x && a.y==b.y && a.vx==b.vx && a.vy==b.vy,
            "Adding a distant body must not change a lunar approach");
        const auto catalog=createDefaultContent();
        for (bool ready : {false,true}) {
            auto state=createNewGame(catalog,812);
            initializeLiveExpedition(state,catalog);
            state.run.expedition.location={"solar","earth",CoordinateFrame::Body,{1.11,0},{},.3,"earth.dock"};
            state.run.expedition.active=false;
            state.run.expedition.undockReady=ready;
            state.run.flight.active=false;
            state.run.flight.mode=FlightMode::Orbit;
            state.run.flight.positionX=1.11;
            state.run.flight.heading=.3;
            state.run.flight.fuelRemaining=3;
            auto save=captureSaveData(state);
            auto restored=createNewGame(catalog,1);
            restoreSaveData(restored,catalog,save);
            check(std::abs(restored.run.flight.positionX-earth.dockOffset.x)<1e-10 &&
                restored.run.flight.fuelRemaining==3 && restored.run.flight.heading==.3 &&
                restored.run.expedition.undockReady==ready,"Attached saves must follow the dock without resource or heading changes");
            save.expedition.active=true; save.expedition.undockReady=false;
            save.expedition.location.siteId.clear(); save.flight.active=true;
            restoreSaveData(restored,catalog,save);
            check(restored.run.flight.positionX==1.11,"Free-flight saves must not follow a relocated dock");
        }
    }
    {
        const auto catalog = createDefaultContent();
        auto opening = createNewGame(catalog, 812);
        check(initializeLiveExpedition(opening,catalog),"Opening can initialize from a new campaign");
        check(beginEarthOpening(opening,catalog),"Unstarted campaign must begin at Earth launch berth");
        auto& e=opening.run.expedition;
        auto& f=opening.run.flight;
        check(earthLaunchReady(e) && e.course.targetBodyId=="moon" && !e.cruise.active,"Opening holds at Earth with manual Moon guidance");
        check(f.positionX==earthLaunchPosition().x && f.positionY==0 && f.velocityX==0 && f.velocityY==0 && !f.active,"Ship waits at Earth until Launch");
        check(encounteredBody(e.location,solarSystemDefinition())->id=="earth","Opening belongs to actual Earth");
        check(!f.orbit.captured && e.departureCount==0,"Preflight neither captures orbit nor starts an expedition");
        check(opening.incomingMessages.pending.size()==1,"Opening instruction is queued before flight");
        auto saved=deserializeSaveData(serializeSaveData(captureSaveData(opening)));
        check(saved && earthLaunchReady(saved->expedition) && !saved->flight.active,"Held Earth launch state persists in v21");
        check(saved->incomingMessages.pending.size()==1,"Opening pause survives save/load");
        acknowledgeIncomingMessage(opening.incomingMessages,"campaign.lunar_approach");
        saved=deserializeSaveData(serializeSaveData(captureSaveData(opening)));
        check(saved && saved->incomingMessages.pending.empty() && saved->incomingMessages.acknowledgedMessages.size()==1,"Opening acknowledgement persists");
        check(!beginEarthOpening(opening,catalog),"Opening initialization is idempotent");
        const auto fuel=f.fuelRemaining;
        check(launchEarthOpening(opening,catalog)==ExpeditionResult::Applied,"Launch releases the Earth berth");
        check(e.active && e.departureCount==1 && e.location.siteId.empty() && f.velocityX==earthLaunchSpeed,"Launch starts one expedition and the authored departure impulse");
        check(f.fuelRemaining==fuel && !f.orbit.captured && f.predictedTrajectory.size()>2,"Launch keeps allocated resources and exposes live coast prediction");
        check(launchEarthOpening(opening,catalog)==ExpeditionResult::InvalidState && e.departureCount==1,"Repeated launch cannot add momentum or expedition departures");
        {
            auto failed=opening;
            failed.run.flight.hullRemaining=0;
            check(recoverExpedition(failed,solarSystemDefinition())==ExpeditionResult::Applied &&
                failed.run.expedition.decision.pendingId=="opening_retry" && failed.run.expedition.wrecks.empty(),
                "An opening failure offers retry without creating a wreck or a dock visit");
            const auto retrySave=deserializeSaveData(serializeSaveData(captureSaveData(failed)));
            check(retrySave.has_value(),"Pending opening retry remains saveable");
            restoreSaveData(failed,catalog,*retrySave);
            check(retryOpeningMission(failed,catalog)==ExpeditionResult::Applied && earthLaunchReady(failed.run.expedition) &&
                failed.run.flight.hullRemaining==failed.run.flight.hullMaximum &&
                failed.run.flight.fuelRemaining==failed.run.flight.fuelCapacity && failed.incomingMessages.pending.empty(),
                "Retry restores the original launch and retains the instruction acknowledgement");
            check(retryOpeningMission(failed,catalog)==ExpeditionResult::InvalidState,"Retry cannot grant resources twice");
            for (const auto variant : {"tips", "crater", "tips", "tips"}) {
                check(launchEarthOpening(failed,catalog)==ExpeditionResult::Applied,"Retry launches remain available");
                recoverExpedition(failed,solarSystemDefinition());
                const auto repeatedSave=deserializeSaveData(serializeSaveData(captureSaveData(failed)));
                check(repeatedSave.has_value(),"Repeated crash dialogue remains saveable");
                restoreSaveData(failed,catalog,*repeatedSave);
                check(openingRetryMessageVariant(failed)==variant,"Crash dialogue follows tips, crater, then permanent tips across reloads");
                auto overheated = failed;
                overheated.run.flight.failureCause = LaunchFailureCause::ThermalRunaway;
                const auto heatSave = deserializeSaveData(serializeSaveData(captureSaveData(overheated)));
                check(heatSave.has_value(), "Opening heat failure remains saveable");
                restoreSaveData(overheated, catalog, *heatSave);
                check(openingRetryMessageVariant(overheated)=="heat_tips", "Overheating selects cooling advice after reload");
                check(retryOpeningMission(failed,catalog)==ExpeditionResult::Applied,"Each crash message keeps its retry action");
            }
            failed=opening;
            failed.run.expedition.cargo.materials.common=1;
            check(!openingMissionRetryEligible(failed),"Carried cargo must retain ordinary recovery rules");
        }
        saved=deserializeSaveData(serializeSaveData(captureSaveData(opening)));
        check(saved && saved->flight.active && saved->expedition.departureCount==1 && saved->flight.velocityX==earthLaunchSpeed,"Released Earth flight survives save/load");
        const auto model=expeditionFlightModel(opening,catalog);
        for(int i=0;i<20;++i) advanceExpeditionFlight(e,f,model,expeditionEnvironment(opening,catalog),solarSystemDefinition(),{},.05);
        check(f.positionX>earthLaunchPosition().x && f.active && f.hullRemaining==f.hullMaximum,"Launch climbs away from Earth without immediate impact");
        const auto g=expeditionGuidance(opening);
        check(g.targetName=="Moon" && g.orbitBodyId=="moon" && g.targetDistance>0,"Guidance supplies target and approach bands");
        auto dockApproach = opening;
        auto& dockExpedition = dockApproach.run.expedition;
        auto& dockFlight = dockApproach.run.flight;
        dockExpedition.location = {"solar", "earth", CoordinateFrame::Body,
            {.30, 0.0}, {}, 0.0, {}};
        dockExpedition.course.targetBodyId = "earth";
        restoreSystemLocation(dockExpedition.location, dockFlight);
        dockFlight.active = dockFlight.physicalFlight = true;
        dockFlight.courseNoticeSeconds = 0.0;
        dockFlight.predictedImpact = false;
        const auto dockGuidance = expeditionGuidance(dockApproach);
        const auto* earth = systemBody(solarSystemDefinition(), "earth");
        check(earth && dockGuidance.targetName == "Earth Dock",
            "Earth waypoint labels must name the orbital dock");
        check(std::abs(dockGuidance.targetPosition.x-earth->dockOffset.x) < 1e-12 &&
            std::abs(dockGuidance.targetPosition.y-earth->dockOffset.y) < 1e-12,
            "Earth waypoint geometry must lead to the dock instead of the collision body");
        check(dockGuidance.nextAction.find("dock marker") != std::string::npos,
            "Earth waypoint instructions must direct the player to the dock marker");
        dockExpedition.location.position = earth->dockOffset;
        restoreSystemLocation(dockExpedition.location, dockFlight);
        const auto dockCourse = previewSystemCourse(
            dockExpedition.location, dockFlight, solarSystemDefinition(), "earth", "earth", &model);
        check(dockCourse.estimateValid && dockCourse.approachFuel < 1e-9,
            "A ship already at the Earth dock must not receive a route back into Earth");
        auto coastExpedition=e;
        auto coastFlight=f;
        auto coastModel=model;
        coastModel.trajectoryPreview=true;
        for(int i=0;i<2400 && coastFlight.active && coastExpedition.location.bodyId!="moon";++i)
            advanceExpeditionFlight(coastExpedition,coastFlight,coastModel,expeditionEnvironment(opening,catalog),solarSystemDefinition(),{},.05);
        check(coastExpedition.location.bodyId=="moon" && coastFlight.active && !coastFlight.orbit.captured,
              "Authored Earth launch must clear Earth and reach the lunar encounter while leaving capture manual");
        auto returned=createNewGame(catalog,813);
        initializeLiveExpedition(returned,catalog);
        returned.run.expedition.departureCount=1;
        check(!beginEarthOpening(returned,catalog),"Returned home must not replay the opening");
        auto unknown=createNewGame(catalog,814);
        initializeLiveExpedition(unknown,catalog);
        unknown.meta.destinationAttempts[1]=1;
        check(!beginEarthOpening(unknown,catalog),"Historical mission evidence prevents relocation");
        auto spent=createNewGame(catalog,815);
        initializeLiveExpedition(spent,catalog);
        spent.run.flight.fuelRemaining-=1;
        check(!beginEarthOpening(spent,catalog),"Unexplained resource use must preserve an uncertain save");
    }
    auto system = solarSystemDefinition();
    system.bodies[3].velocity = {.125, -.375};
    SystemLocation local{
        "solar", "earth", CoordinateFrame::Body, {.7123456789123, -.3123456789123}, {.42, -.25}, 1.7, ""};
    const auto global = convertSystemFrame(local, CoordinateFrame::System, "", system);
    const auto round = convertSystemFrame(global, CoordinateFrame::Body, "earth", system);
    check(std::abs(round.position.x - local.position.x) < 1e-14 &&
              std::abs(round.position.y - local.position.y) < 1e-14,
          "Frame round trip must preserve position");
    check(std::abs(round.velocity.x - local.velocity.x) < 1e-14 &&
              std::abs(round.velocity.y - local.velocity.y) < 1e-14 && round.heading == local.heading,
          "Moving frame round trip must preserve velocity and heading");
    PersistentExpeditionState e;
    e.location = local;
    FlightRunState flight;
    flight.fuelRemaining = 4.321;
    flight.hullRemaining = 73;
    flight.fuelCapacity = 10;
    flight.hullMaximum = 100;
    check(plotSystemCourse(e, flight, system, "neptune") == ExpeditionResult::Applied,
          "An outer destination must not require a route key");
    check(plotSystemCourse(e, flight, system, "moon") == ExpeditionResult::Applied &&
              flight.fuelRemaining == 4.321 && flight.hullRemaining == 73,
          "Changing targets must not refill or repair the ship");
    for (const auto &b : system.bodies)
        check(plotSystemCourse(e, flight, system, b.id) == ExpeditionResult::Applied,
              "Every region must accept guidance");
    e.course.targetBodyId = "moon";
    e.cruise.active = true;
    restoreSystemLocation(e.location, flight);
    auto input = cruiseInput(e, flight, system, {});
    check(input.throttle == 1 && e.cruise.active, "Cruise must deliberately keep burning");
    input = cruiseInput(e, flight, system, {.2, 0, false, false});
    check(!e.cruise.active && input.steer == .2, "Manual steering must cancel cruise immediately");

    const auto catalog = createDefaultContent();
    Random rng(35);
    auto game = createNewGame(catalog, 35);
    auto prepared = prepareLaunch(game, catalog, rng);
    prepared.heatEnabled = false;
    prepared.asteroidsEnabled = false;
    flight = beginLaunchFlight(prepared, catalog.destinations[1]);
    flight.active = true;
    flight.physicalFlight = true;
    flight.positionX = systemBody(system,"sun")->position.x - 1.1;
    flight.positionY = systemBody(system,"sun")->position.y;
    flight.velocityX = 4;
    flight.velocityY = 0;
    SystemLocation sunPath{"solar", "", CoordinateFrame::System, {}, {}, 0, ""};
    bool hit = false;
    for (int i = 0; i < 20 && !hit; ++i)
        hit =
            updateLaunchFlight(flight, prepared, catalog.destinations[1], {}, .05, nullptr, &system, &sunPath)
                .failed;
    check(hit && flight.hullRemaining == 0,
          "Unselected Sun collision must use ordinary fatal collision rules");

    flight = beginLaunchFlight(prepared, catalog.destinations[1]);
    flight.active = true;
    flight.physicalFlight = true;
    flight.positionX = systemBody(system,"sun")->position.x - 3;
    flight.positionY = systemBody(system,"sun")->position.y;
    flight.velocityX = 0;
    flight.velocityY = 0;
    flight.heading = 0;
    flight.fuelRemaining = 100;
    e = {};
    e.active = true;
    e.location = {"solar", "", CoordinateFrame::System, {}, {}, 0, ""};
    e.course.targetBodyId = "sun";
    e.cruise.active = true;
    hit = false;
    for (int i = 0; i < 1200 && !hit; ++i)
        hit = advanceExpeditionFlight(e, flight, prepared, catalog.destinations[1], system, {}, .05).failed;
    check(hit && flight.hullRemaining == 0 && e.cruise.active,
          "Unattended cruise must not disengage or protect the ship from solar impact");

    e = {};
    e.active = true;
    flight = {};
    flight.fuelCapacity = 10;
    flight.hullMaximum = 100;
    flight.fuelRemaining = 3;
    flight.hullRemaining = 75;
    e.location = {"solar", "moon", CoordinateFrame::Body, {.5, 0}, {}, 0, "moon.beacon"};
    check(recoverSiteBattery(e, "moon") == ExpeditionResult::Applied,
          "Site delivery must transfer battery to ship");
    check(recoverSiteBattery(e, "moon") == ExpeditionResult::AlreadyApplied,
          "Repeated delivery must not duplicate battery");
    e.cargo.materials.common = 7;
    e.progression.expeditionLevel = 4;
    e.progression.expeditionExperience = 9.0;
    e.progression.pendingRunUpgradeChoices = 1;
    e.progression.runRigUpgradeRanks = {{content::surfaceUpgrade::highTorqueMotor, 2}};
    e.progression.selectedSynergyIds = {"recovered_synergy"};
    e.progression.droneModuleAssignments = {{0, "recovered_drone", DroneModuleKind::CombatDrill}};
    e.location = {"solar", "", CoordinateFrame::System, systemBody(system,"sun")->position, {2, 0}, 0, ""};
    restoreSystemLocation(e.location, flight);
    check(loseExpedition(e, flight, system) == ExpeditionResult::Applied && e.wrecks.size() == 1,
          "Loss must create one wreck");
    check(e.batteries[0].owner == BatteryOwner::Wreck && e.cargo.materials.common == 0,
          "Loss must transfer ownership, not copy it");
    check(std::hypot(e.wrecks[0].location.position.x-systemBody(system,"sun")->position.x,
              e.wrecks[0].location.position.y-systemBody(system,"sun")->position.y) >
              system.bodies[0].radius + .65,
          "Sun impact salvage must have replacement-ship clearance");
    const auto persisted = deserializeExpedition(serializeExpedition(e));
    check(persisted.has_value() && validBatteryOwnership(*persisted),
          "Wreck ownership must survive save/load");
    e = *persisted;
    e.progression.expeditionLevel = 3;
    e.progression.expeditionExperience = 12.0;
    e.progression.pendingRunUpgradeChoices = 2;
    e.progression.runRigUpgradeRanks = {{content::surfaceUpgrade::highTorqueMotor, 1}, {content::surfaceUpgrade::wideDrillHead, 1}};
    e.progression.droneModuleAssignments = {{0, "active_drone", DroneModuleKind::SpectrumFilter}};
    e.location = e.wrecks[0].location;
    e.location.position.x += expeditionSalvageRadius - 0.001;
    e.location.velocity = e.wrecks[0].location.velocity;
    restoreSystemLocation(e.location, flight);
    flight.active = flight.physicalFlight = true;
    flight.mode = FlightMode::Travel;
    check(canSalvageWreck(e, flight, system, 1),
          "Wreck salvage must include the tripled radius boundary");
    e.location.position.x += 0.002;
    restoreSystemLocation(e.location, flight);
    check(!canSalvageWreck(e, flight, system, 1),
          "Wreck salvage must remain unavailable just beyond the tripled radius");
    e.location = e.wrecks[0].location;
    e.cargo.materials.common = 24;
    check(salvageWreck(e, 1, system, 24) == ExpeditionResult::Applied &&
              e.batteries[0].owner == BatteryOwner::Ship && e.cargo.materials.common == 24 &&
              e.progression.expeditionLevel == 4 && e.progression.expeditionExperience == 9.0 &&
              e.progression.pendingRunUpgradeChoices == 3 && e.progression.runRigUpgradeRanks.size() == 2 &&
              e.progression.runRigUpgradeRanks.front().rank == 2 &&
              e.progression.selectedSynergyIds == std::vector<std::string>{"recovered_synergy"} &&
              e.progression.pendingGraftConflicts.size() == 1 && e.wrecks.size() == 1,
          "Full-hold salvage must restore the build once while leaving ore in the wreck");
    check(resolveRecoveredGraftConflict(e, 0, true) == ExpeditionResult::Applied &&
              e.progression.pendingGraftConflicts.empty() &&
              e.progression.droneModuleAssignments.front().primaryDroneId == "recovered_drone" &&
              e.progression.droneModuleAssignments.front().module == DroneModuleKind::CombatDrill,
          "Conflicting recovered grafts must wait for and obey an explicit installed-graft choice");
    const int recoveredChoices = e.progression.pendingRunUpgradeChoices;
    e.cargo.materials = {};
    check(salvageWreck(e, 1, system, 24) == ExpeditionResult::Applied && e.cargo.materials.common == 7 &&
              e.progression.pendingRunUpgradeChoices == recoveredChoices,
          "Later cargo salvage must not grant the recovered build twice");
    check(salvageWreck(e, 1, system) == ExpeditionResult::AlreadyApplied && e.cargo.materials.common == 7,
          "Salvage must be idempotent");
    e.location = {"solar", "earth", CoordinateFrame::Body, {.5, 0}, {}, 0, ""};
    restoreSystemLocation(e.location, flight);
    // This fixture's Earth moves: match its velocity in the system frame.
    check(dockExpedition(e, flight, system) == ExpeditionResult::Applied &&
              e.batteries[0].owner == BatteryOwner::EarthStorage,
          "Earth docking must store batteries");
    check(loadEarthBattery(e, "moon") == ExpeditionResult::Applied && e.batteries[0].researchEarned,
          "Loading must remove storage ownership and retain research");
    e.location = {"solar", "straylight", CoordinateFrame::Body, {.5, 0}, {}, 0, "straylight.dock"};
    check(activateStraylight(e) == ExpeditionResult::MissingBatteries,
          "Early Ark discovery must not activate home");
    check(installArkBattery(e, "moon") == ExpeditionResult::Applied, "Ark must accept partial delivery");
    check(!e.arkActivated && e.homeBodyId == "earth", "Partial delivery must not replace Earth home");
    for (std::size_t i = 1; i < e.batteries.size(); ++i)
    {
        e.location.siteId = e.batteries[i].sourceSiteId;
        check(recoverSiteBattery(e, e.batteries[i].id) == ExpeditionResult::Applied,
              "Each authored objective has one battery");
        e.location.siteId = "straylight.dock";
        check(installArkBattery(e, e.batteries[i].id) == ExpeditionResult::Applied,
              "All six distinct batteries must install");
        const auto saved = deserializeExpedition(serializeExpedition(e));
        check(saved.has_value(), "Each battery transition must round trip");
        e = *saved;
    }
    check(batteryResearchRank(e) == 3, "Four ever-banked batteries must retain rank III research");
    check(activateStraylight(e) == ExpeditionResult::Applied && e.homeBodyId == "straylight",
          "Explicit activation must establish Ark home");
    check(activateStraylight(e) == ExpeditionResult::AlreadyApplied, "Activation must not grant twice");
    auto corrupt = e;
    corrupt.batteries[0].owner = BatteryOwner::Wreck;
    corrupt.batteries[0].wreckId = 99;
    check(!deserializeExpedition(serializeExpedition(corrupt)),
          "Orphaned battery ownership must be rejected");

    game.run.expedition = e;
    game.run.expedition.progression.expeditionLevel = 4;
    game.run.expedition.location = local;
    game.run.mining.geologySeed = 673;
    storeVisitedSite(game, "moon.beacon");
    game.run.mining.geologySeed = 998;
    check(restoreVisitedSite(game, "moon.beacon") && game.run.mining.geologySeed == 673 &&
              game.run.expedition.progression.expeditionLevel == 4,
          "Site restoration must retain site identity without rolling back the build");
    const auto saved = deserializeSaveData(serializeSaveData(captureSaveData(game)));
    check(saved.has_value() && saved->expedition.sites.size() == 1 &&
              saved->expedition.progression.expeditionLevel == 4,
          "Full save must round trip registry and expedition build together");
    check(saved->expedition.location.position.x == local.position.x,
          "Persistent location must serialize at round-trip precision");
    {
        auto boundary = createNewGame(catalog, 71);
        check(initializeLiveExpedition(boundary, catalog),
              "Boundary fixture must initialize live travel");
        auto& expedition = boundary.run.expedition;
        auto& ship = boundary.run.flight;
        expedition.active = true;
        expedition.location = {"solar", "moon", CoordinateFrame::Body,
            {1.80, 0.0}, {0.08, 0.0}, 0.0, "moon.beacon:zone_2"};
        expedition.progression.expeditionLevel = 3;
        expedition.cargo.materials.common = 7;
        restoreSystemLocation(expedition.location, ship);
        ship.active = ship.physicalFlight = true;
        ship.mode = FlightMode::Travel;
        ship.phase = FlightPhase::Transfer;
        ship.orbit.enteredInfluence = true;
        const double speedBefore = std::hypot(ship.velocityX, ship.velocityY);
        const auto step = advanceExpeditionFlight(
            expedition, ship, expeditionFlightModel(boundary, catalog),
            expeditionEnvironment(boundary, catalog), solarSystemDefinition(), {}, .01);
        check(!step.flyby && ship.active && ship.phase != FlightPhase::Flyby,
              "Leaving a live body influence must keep physical Flight active");
        check(expedition.location.frame == CoordinateFrame::System && expedition.location.bodyId.empty(),
              "Influence exit must convert the same pose into system space");
        check(std::abs(std::hypot(ship.velocityX, ship.velocityY) - speedBefore) < .01 &&
                  expedition.progression.expeditionLevel == 3 && expedition.cargo.materials.common == 7,
              "Influence exit must preserve perceived speed, cargo, and expedition progression");
    }
    {
        auto journey = createNewGame(catalog, 72);
        check(initializeLiveExpedition(journey, catalog), "New campaign must initialize at Earth dock");
        auto& expedition = journey.run.expedition;
        auto& ship = journey.run.flight;
        check(operationalHomeDocked(expedition), "Initial Earth home must be operational");
        const auto homePose = expedition.location;
        journey.run.credits = 100;
        check(canInstallLaunchUpgrade(journey, catalog, LaunchUpgradeKind::FlightControls), "Rank I controls need no lesson gate");
        check(canInstallSurfaceDepthUpgrade(journey, catalog, SurfaceDepthUpgradeKind::BoreSystem), "Rank I bore needs no blueprint gate");
        check(departHome(journey, catalog) == ExpeditionResult::Applied, "Earth departure must enter live flight");
        check(expedition.undockReady && !ship.active && !expedition.active,"Departure waits attached to the dock");
        const auto dockFuel=ship.fuelRemaining;
        const auto dockModel=expeditionFlightModel(journey,catalog);
        advanceExpeditionFlight(expedition,ship,dockModel,expeditionEnvironment(journey,catalog),solarSystemDefinition(),{},.05);
        check(ship.positionX==homePose.position.x && ship.fuelRemaining==dockFuel,"Waiting at dock cannot fall or consume fuel");
        check(expedition.location.position.x == homePose.position.x && ship.fuelRemaining == ship.fuelCapacity,
              "Departure must retain dock position and fuel");
        const double dockHeading = ship.heading;
        for (int frame = 0; frame < 12; ++frame)
            advanceExpeditionFlight(expedition,ship,dockModel,expeditionEnvironment(journey,catalog),solarSystemDefinition(),{1,0,false,true},.05);
        check(ship.heading != dockHeading && expedition.location.heading == ship.heading,
              "Steering at the dock must rotate the ship without requiring velocity");
        check(expedition.undockReady && !ship.active && ship.fuelRemaining == dockFuel,
              "Pre-launch rotation must remain attached and consume no fuel");
        expedition.progression.expeditionLevel = 4;
        expedition.cargo.materials.common = 9;
        auto model = expeditionFlightModel(journey, catalog);
        advanceExpeditionFlight(expedition,ship,model,expeditionEnvironment(journey,catalog),solarSystemDefinition(),{0,1,false,true},.05);
        check(ship.active && !expedition.undockReady && ship.fuelRemaining<dockFuel,"Forward thrust undocks and burns in the same step");
        check(expedition.location.frame == CoordinateFrame::System && ship.mode == FlightMode::Travel,
              "Earth undocking must immediately use system flight outside the encounter boundary");
        const auto& solar = solarSystemDefinition();
        const auto* moon = systemBody(solar,"moon");
        const auto* mars = systemBody(solar,"mars");
        const auto* earth = systemBody(solar,"earth");
        const auto target = SystemVector{moon->position.x-1.1,moon->position.y};
        const auto pilotTo = [&](SystemVector destination) {
            for (int frame = 0; frame < 12000; ++frame) {
                captureSystemLocation(expedition.location, ship);
                const auto p = convertSystemFrame(expedition.location, CoordinateFrame::System, "", solar);
                const double dx = destination.x-p.position.x, dy = destination.y-p.position.y;
                const double range = std::hypot(dx,dy), speed = std::hypot(p.velocity.x,p.velocity.y);
                if (range < .08 && speed < .08) return true;
                const auto gravity = integrateSystemCoast({p.position.x,p.position.y,0,0}, .001, solar);
                const double clock = systemFlightTimeScale(solar, p.position);
                // Bound transfer speed so the fixture can turn and brake before
                // crossing a local gravity fade. This is test piloting only.
                const double desiredSpeed = std::min(.15, range*.25);
                const double ax = (dx/std::max(range,.001)*desiredSpeed-p.velocity.x)*.7-gravity.vx/.001*clock;
                const double ay = (dy/std::max(range,.001)*desiredSpeed-p.velocity.y)*.7-gravity.vy/.001*clock;
                const double desired = std::atan2(ay,ax);
                double error = flightWrappedAngleDelta(ship.heading,desired);
                double sign = 1;
                if (std::abs(error)>1.5707963267948966) { sign=-1; error=flightWrappedAngleDelta(ship.heading,desired+3.141592653589793); }
                const FlightInput manual{std::clamp(-error*2.0,-1.0,1.0), std::abs(error)<.25 && ship.heat<.45 ? sign*std::min(.60,std::hypot(ax,ay)/.23) : 0.0, false, true};
                if (advanceExpeditionFlight(expedition,ship,model,expeditionEnvironment(journey,catalog),solar,manual,.05).failed)
                    throw std::runtime_error("Pilot toward " + std::to_string(destination.x) + "," + std::to_string(destination.y) + " failed cause " + std::to_string(static_cast<int>(ship.failureCause)) + " fuel " + std::to_string(ship.fuelRemaining) + " at " + std::to_string(p.position.x) + "," + std::to_string(p.position.y) + " heading " + std::to_string(ship.heading) + " demand " + std::to_string(manual.throttle));
            }
            throw std::runtime_error("Pilot toward " + std::to_string(destination.x) + "," + std::to_string(destination.y) + " timed out at " + std::to_string(ship.positionX) + "," + std::to_string(ship.positionY) + " fuel " + std::to_string(ship.fuelRemaining));
        };
        check(pilotTo(target), "Starter ship must physically reach lunar approach using ordinary controls");
        check(expedition.location.bodyId == "moon", "Actual lunar encounter must change frame");
        auto arrivalFixture=journey;
        arrivalFixture.run.flight.phase=FlightPhase::Landed;
        Random payoutRandom(17);
        const auto arrival=resolveLaunch(model,catalog,arrivalFixture,expeditionEnvironment(journey,catalog).targetMultiplier,RecoveryMethod::TransferArrival,payoutRandom,{true});
        recordExpeditionArrival(arrivalFixture,catalog,arrival);
        const double payout=arrivalFixture.run.expedition.cargo.credits;
        check(payout>=22 && payout==arrival.payout-arrival.recoveryCost,"Existing Moon arrival payout must support a Rank I installation after banking");
        storeVisitedSite(arrivalFixture,"moon.beacon");
        recordExpeditionArrival(arrivalFixture,catalog,arrival);
        check(arrivalFixture.run.expedition.cargo.credits==payout,"Revisiting a paid site must not duplicate its arrival payout");
        expedition.cargo.credits=payout;
        const double afterMoon = ship.fuelRemaining;
        check(pilotTo({moon->position.x-1.3,moon->position.y-2.0}), "Continuation pilot must clear the Moon before turning toward Mars");
        check(pilotTo({mars->position.x-1.1,mars->position.y}), "Manual continuation must reach Mars without per-leg initialization");
        check(expedition.location.bodyId == "mars" && ship.fuelRemaining < afterMoon,
              "Mars encounter must retain real fuel use");
        check(expedition.progression.expeditionLevel == 4 && expedition.cargo.materials.common == 9,
              "Cross-body travel must preserve XP and cargo");
        check(pilotTo({earth->position.x+2.1,earth->position.y-2.0}), "Return pilot must brake outside Earth's approach");
        check(pilotTo(systemDockPosition(*earth)), "Starter pilot fixture must be able to return to Earth dock");
        check(canDockExpedition(expedition,ship,solar), "Dock eligibility must match physical rendezvous");
        const int bankBefore = journey.meta.materials.common;
        check(dockExpedition(journey,solar) == ExpeditionResult::Applied && journey.meta.materials.common == bankBefore+9,
              "Home docking must bank exactly the carried manifest");
        check(dockExpedition(journey,solar) == ExpeditionResult::AlreadyApplied && journey.meta.materials.common == bankBefore+9,
              "Repeated docking must be idempotent");
        check(expedition.progression.expeditionLevel == 4, "Home docking must preserve the expedition build");
        check(journey.run.credits==100+payout && expedition.cargo.credits==0,"Arrival payout banks exactly once at home");
        auto persistedJourney = deserializeSaveData(serializeSaveData(captureSaveData(journey)));
        check(persistedJourney && persistedJourney->expedition.travelInitialized && persistedJourney->expedition.rigFuel.capacity > 0,
              "Live expedition initialization and Rig allotment must persist in v23");
        check(departHome(journey,catalog)==ExpeditionResult::Applied,"Banked expedition can depart again");
        advanceExpeditionFlight(expedition,ship,model,expeditionEnvironment(journey,catalog),solar,{0,1,false,true},.05);
        const int bankedMaterials=journey.meta.materials.common;
        const auto oreTransfer=planPayloadTransfer({7,0,0},{},shipHoldMaterials(journey),shipHoldCapacity(journey,catalog));
        applyPayloadTransferPlan(journey,catalog,"moon",oreTransfer);
        expedition.cargo.credits=17.5;
        check(journey.meta.materials.common==bankedMaterials && expedition.cargo.materials.common==7,
              "Parked-ship delivery must remain expedition cargo, not banked material");
        check(recoverExpedition(journey,solar)==ExpeditionResult::Applied && expedition.wrecks.back().cargo.materials.common==7,
              "Delivered but unbanked ore must move to the loss wreck");
        check(ship.positionX==systemDockPosition(*systemBody(solar,"earth")).x-systemBody(solar,"earth")->position.x,
              "Replacement must be at the operational dock marker");
        const auto lost=deserializeSaveData(serializeSaveData(captureSaveData(journey)));
        check(lost && lost->expedition.wrecks.back().cargo.credits==17.5,"Unbanked payout must survive wreck save/load");
        ship.failureCause=LaunchFailureCause::FuelExhausted;
        plotSystemCourse(expedition,ship,solar,"moon",&model);
        check(expedition.course.trajectory.size()>2,"Replacement ship must have a live departure forecast despite the prior loss record");
        expedition.location.bodyId="mercury";
        expedition.location.siteId="mercury.surface";
        SurfaceLandingBuildRequest optionalSite;
        optionalSite.destinationId="moon";
        optionalSite.siteSeed=17;
        optionalSite.landingOrdinal=1;
        optionalSite.allowScenarioObjectives=false;
        const auto prepared=prepareSurfaceLanding(journey,catalog,optionalSite);
        check(prepared.valid && prepared.miningTemplate.miningSiteDefinitionId.empty() && !prepared.miningTemplate.progressionCreditEligible,
              "Optional geology reuse must not duplicate lunar campaign objectives");
        queueExpeditionDecision(journey,catalog);
        check(expedition.decision.pendingId.empty(),"An ordinary visit must not create a completion decision");
    }
}
