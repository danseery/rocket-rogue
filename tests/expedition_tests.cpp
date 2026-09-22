#include "core/Content.h"
#include "core/ArtifactProgression.h"
#include "core/ScenarioSystem.h"
#include "core/ExpeditionPersistence.h"
#include "core/ExpeditionSystem.h"
#include "core/LaunchSimulation.h"
#include "core/SaveData.h"
#include "core/ResearchSystem.h"
#include "core/PayloadTransfer.h"
#include "core/MiningSystem.h"
#include "core/FlightSystem.h"
#include "core/SurfacePresentation.h"
#include "core/SolarProgression.h"
#include "core/StraylightSequence.h"
#include "core/PostSolarSystem.h"
#include "core/MissionGuidance.h"
#include "core/MiningPresentation.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <iostream>
#include <memory>

namespace
{
void check(bool condition, const char *message)
{
    if (!condition) {
        std::cerr << message << '\n';
        throw std::runtime_error(message);
    }
}

void straylightSequenceTests()
{
    using namespace rocket;
    using Stage = StraylightStage;
    const auto catalog = createDefaultContent();
    auto state = std::make_unique<GameState>(createNewGame(catalog, 77));
    auto& s = *state;
    initializeLiveExpedition(s,catalog);
    auto& e=s.run.expedition;
    check(!revealStraylightOnDelivery(s,catalog), "Undelivered final artifact must not reveal Straylight");
    check(plotSystemCourse(e,s.run.flight,solarSystemDefinition(),"straylight") == ExpeditionResult::InvalidTarget,
        "Unrevealed Straylight must reject waypoint selection");
    const auto* neptune=systemBody(solarSystemDefinition(),"neptune");
    const auto* ark=systemBody(solarSystemDefinition(),"straylight");
    check(std::abs(std::hypot(ark->position.x-neptune->position.x,ark->position.y-neptune->position.y)-18)<.00001,
        "Straylight must be eighteen units beyond Neptune");
    for (auto& b:e.batteries) b.owner=BatteryOwner::EarthStorage;
    MissionArtifact tritonArtifact;
    tritonArtifact.key = "solar:triton";
    tritonArtifact.scenarioId = content::scenario::neptuneDiscovery;
    tritonArtifact.stepId = "artifact";
    tritonArtifact.requiredDockId = "earth";
    tritonArtifact.artifact.id = content::protectedObjective::tritonSignalArtifact;
    tritonArtifact.artifact.originDestinationId = "triton";
    tritonArtifact.owner = ArtifactCustody::Banked;
    tritonArtifact.completed = true;
    e.artifacts.push_back(tritonArtifact);
    e.artifactCustodyLoaded = true;
    e.coursePlayerSelected=true;
    check(revealStraylightOnDelivery(s,catalog) && e.course.targetBodyId=="straylight" && !e.coursePlayerSelected,
        "Ship delivery immediately replaces manual course with Straylight");
    check(!s.meta.straylightDiscoveryAcknowledged && recommendedCampaignObjective(s,catalog).targetId=="straylight",
        "Earth banking must not steal first-contact guidance");
    check(!revealStraylightOnDelivery(s,catalog), "Reveal must be idempotent");
    const auto roundTrip = [&] {
        const auto saved=deserializeSaveData(serializeSaveData(captureSaveData(s)));
        check(saved.has_value(), "Every sequence boundary must serialize");
        auto restored=std::make_unique<GameState>(createNewGame(catalog,1));
        restoreSaveData(*restored,catalog,*saved);
        check(restored->meta.straylightStage==s.meta.straylightStage, "Reload must preserve the sequence stage");
        check(restored->run.expedition.batteries[0].owner==e.batteries[0].owner,
            "Reload must preserve beacon ownership");
    };
    roundTrip();
    s.meta.straylightStage=Stage::Reveal;
    check(applyStraylightAction(s,catalog,"skip") && s.meta.straylightStage==Stage::Invitation,
        "Skip ends only reveal animation");
    check(!applyStraylightAction(s,catalog,"skip"), "Skip cannot acknowledge a transmission");
    check(applyStraylightAction(s,catalog,"invitation"), "Invitation acknowledgement must enable approach");
    e.location={"solar","straylight",CoordinateFrame::Body,ark->dockOffset,{},0,"straylight.dock"};
    s.run.flight.active=false;
    s.meta.straylightStage=Stage::Docking;
    check(finishStraylightCinematic(s,catalog), "Docking ends at first contact");
    roundTrip();
    check(applyStraylightAction(s,catalog,"retrieve") && e.course.targetBodyId=="earth", "Beacon mission must target Earth explicitly");
    check(!applyStraylightAction(s,catalog,"online"), "Activation cannot bypass confirmation or beacon requirement");
    e.location={"solar","earth",CoordinateFrame::Body,{}, {},0,"earth.dock"};
    e.cargo.materials.common=999;
    e.batteries[0].owner=BatteryOwner::Wreck;
    e.batteries[0].wreckId=420;
    check(applyStraylightAction(s,catalog,"collect"), "One action collects all Earth beacons despite a full hold");
    check(e.batteries[0].owner==BatteryOwner::Wreck && straylightObjective(s)->targetId=="wreck:420",
        "Collection must not invent a missing wreck beacon");
    e.batteries[0].owner=BatteryOwner::Ship;
    e.batteries[0].wreckId=0;
    check(!applyStraylightAction(s,catalog,"collect"), "Repeated collection must not duplicate beacons");
    e.location={"solar","straylight",CoordinateFrame::Body,ark->dockOffset,{},0,"straylight.dock"};
    check(applyStraylightAction(s,catalog,"install"), "Install all carried beacons");
    check(!applyStraylightAction(s,catalog,"install"), "Repeated installation has no effect");
    roundTrip();
    check(applyStraylightAction(s,catalog,"prepare_online") && !e.arkActivated, "Confirmation must precede commitment");
    check(applyStraylightAction(s,catalog,"cancel_online") && !e.arkActivated, "Not yet preserves solar exploration");
    applyStraylightAction(s,catalog,"prepare_online");
    check(applyStraylightAction(s,catalog,"online") && e.homeBodyId=="straylight" && straylightCommitted(s),
        "Confirmed activation commits and sets the Ark as home");
    roundTrip();
    check(applyStraylightAction(s,catalog,"skip") && s.meta.straylightStage==Stage::Online, "Awakening skip cannot start evacuation");
    check(!applyStraylightAction(s,catalog,"depart"), "Departure requires evacuation and boarding");
    check(applyStraylightAction(s,catalog,"coordinate"), "Coordinate evacuation explicitly");
    roundTrip();
    check(applyStraylightAction(s,catalog,"boarding"), "Begin boarding explicitly");
    roundTrip();
    check(finishStraylightCinematic(s,catalog) && applyStraylightAction(s,catalog,"secure"), "Secure only after boarding");
    roundTrip();
    check(applyStraylightAction(s,catalog,"depart"), "Depart only from secured Ark");
    roundTrip();
    check(finishStraylightCinematic(s,catalog) && e.location.systemId==content::postSolarSystem::aaruVale &&
        s.meta.navigation.currentSystemId==e.location.systemId && !e.active && !s.run.flight.active &&
        !s.meta.ark.gravityWellDisaster && findPostSolarSystemRoster(s.meta,e.location.systemId),
        "Departure must physically arrive safely in generated Aaru Vale without Arkfall");
    roundTrip();
    check(applyStraylightAction(s,catalog,"arrived"), "Arrival waits for explicit acknowledgement");
    for (int value=static_cast<int>(Stage::RevealPending); value<=static_cast<int>(Stage::Complete); ++value) {
        s.meta.straylightStage=static_cast<Stage>(value);
        roundTrip();
    }
    // Exercise every combination of the four transferable ownership locations.
    const std::array owners{BatteryOwner::EarthStorage, BatteryOwner::Ship, BatteryOwner::Wreck, BatteryOwner::ArkSlot};
    for (int combination=0; combination<4096; ++combination) {
        s.meta.straylightStage=Stage::RetrieveBeacons;
        e.arkActivated=false;
        int mask=combination;
        std::array<BatteryOwner,6> before{};
        for (std::size_t i=0; i<6; ++i) {
            before[i]=e.batteries[i].owner=owners[mask%4]; mask/=4;
            e.batteries[i].wreckId=before[i]==BatteryOwner::Wreck ? 420 : 0;
        }
        e.location={"solar","earth",CoordinateFrame::Body,{}, {},0,"earth.dock"};
        applyStraylightAction(s,catalog,"collect");
        e.location={"solar","straylight",CoordinateFrame::Body,ark->dockOffset,{},0,"straylight.dock"};
        applyStraylightAction(s,catalog,"install");
        for (std::size_t i=0; i<6; ++i)
            check(e.batteries[i].owner==(before[i]==BatteryOwner::Wreck ? BatteryOwner::Wreck : BatteryOwner::ArkSlot),
                "Collection and partial installation must preserve every missing wreck beacon");
        const bool complete=std::none_of(before.begin(),before.end(),[](auto owner){return owner==BatteryOwner::Wreck;});
        check(applyStraylightAction(s,catalog,"prepare_online")==complete && !e.arkActivated,
            "Only six installed beacons enable confirmation, and confirmation never activates implicitly");
    }
    auto legacy=captureSaveData(s);
    legacy.straylightStage=Stage::Hidden;
    legacy.straylightPlacementVersion=0;
    auto restored=std::make_unique<GameState>(createNewGame(catalog,1));
    restoreSaveData(*restored,catalog,legacy);
    check(restored->meta.straylightStage==Stage::Complete, "Previously departed saves must not replay first contact");
    legacy.ark.firstJumpComplete=false;
    legacy.expedition.arkActivated=false;
    legacy.expedition.straylightRevealed=true;
    legacy.expedition.location={"solar","straylight",CoordinateFrame::Body,{1.07,.01},{},0,"straylight.dock"};
    restoreSaveData(*restored,catalog,legacy);
    check(restored->meta.straylightStage==Stage::FirstContact &&
        std::abs(restored->run.expedition.location.position.x-ark->dockOffset.x-.02)<.000001 &&
        std::abs(restored->run.expedition.location.position.y-ark->dockOffset.y-.01)<.000001,
        "Revealed legacy saves resume first contact and preserve their displacement from the relocated berth");
    legacy.expedition.arkActivated=true;
    restoreSaveData(*restored,catalog,legacy);
    check(restored->meta.straylightStage==Stage::Online, "Activated legacy saves never regress to beacon retrieval");
}

void orbitalObjectiveSafetyTests()
{
    using namespace rocket;
    const auto catalog=createDefaultContent();
    {
        auto state=createNewGame(catalog,1);
        std::array<int,6> sectors{};
        std::array<int,4> depths{};
        for(int seed=1;seed<=6000;++seed) {
            state.seed=seed;
            const auto sector=artifactSectorForBody(state,"solar","titan");
            ++sectors[sector.back()-'1'];
            ++depths[encounterArtifactDepth(state,"aaru_vale","encounter_17")-1];
            check(sector==artifactSectorForBody(state,"solar","titan"),"Sector rolls are repeatable");
        }
        for(int count:sectors) check(count>850 && count<1150,"All six sectors have an even seeded distribution");
        for(int count:depths) check(count>1300 && count<1700,"Encounter depths have an even seeded distribution");
        PersistentSiteState existing; existing.systemId="solar"; existing.bodyId="titan";
        existing.siteId="titan.beacon:zone_4"; existing.mining.artifact.present=true;
        state.run.expedition.sites.push_back(existing);
        check(artifactSectorForBody(state,"solar","titan")=="zone_4","Saved artifact location overrides the new roll");
        existing.mining.artifact.state=MiningArtifactState::Delivered;
        state.run.expedition.sites[0]=existing;
        check(artifactSectorForBody(state,"solar","titan")=="zone_4","Delivered artifacts do not reroll");
        state.run.expedition.moonTutorialZone="zone_6";
        check(artifactSectorForBody(state,"solar","moon")=="zone_6","Existing Moon tutorial sector remains stable");
        for(int seed=1;seed<=12;++seed) {
            auto encounter=createNewGame(catalog,seed);
            for(std::size_t i=0;i<catalog.destinations.size();++i)
                if(catalog.destinations[i].id==content::destination::nearbyStar) encounter.run.destinationIndex=static_cast<int>(i);
            startSurfaceExpedition(encounter,catalog);
            encounter.run.planetaryExpedition.miningSitePrepared=true;
            encounter.run.planetaryExpedition.prospectArtifacts=1;
            const auto beforeMining=std::make_unique<GameState>(encounter);
            check(startMiningRun(encounter,catalog,{MiningAct::ActTwo,4,static_cast<std::uint64_t>(seed),true,MiningGateType::HazardCocoon},false).applied,
                "Procedural encounter fixture starts");
            const auto& mining=encounter.run.mining;
            const int depth=encounterArtifactDepth(encounter,mining.postSolarSystemId,mining.bodyId);
            int count=mining.artifact.present?1:0;
            if(mining.artifact.present) check(mining.depthZone==depth,"Encounter artifact uses rolled depth");
            for(const auto& layer:mining.depthLayers) if(layer.artifact.present) {
                ++count; check(layer.depthZone==depth,"Cached encounter artifact uses rolled depth");
            }
            check(count==1,"Random depth must not duplicate the artifact at the entry layer");
            if(seed<=3) for(const auto& zone:planetLandingZones()) {
                auto sectorState=std::make_unique<GameState>(*beforeMining);
                check(startMiningRun(*sectorState,catalog,{MiningAct::ActTwo,4,static_cast<std::uint64_t>(seed),true,MiningGateType::HazardCocoon},false,zone.id).applied,
                    "Every procedural encounter sector remains landable");
                const auto& sectorMining=sectorState->run.mining;
                int sectorCount=sectorMining.artifact.present?1:0;
                for(const auto& layer:sectorMining.depthLayers) sectorCount+=layer.artifact.present?1:0;
                check(sectorCount==(zone.id==artifactSectorForBody(*sectorState,sectorMining.postSolarSystemId,sectorMining.bodyId)?1:0),
                    "Only the rolled procedural sector contains the artifact");
            }
        }
    }
    {
        MiningRunState legacy;
        legacy.terrain.width=legacy.terrain.height=20;
        legacy.terrain.cells.resize(400);
        for (auto& cell : legacy.terrain.cells) {
            cell.material=MiningCellMaterial::Regolith;
            cell.maxToughness=cell.remainingToughness=2;
        }
        legacy.artifact.present=true; legacy.artifact.state=MiningArtifactState::Embedded;
        legacy.artifact.x=legacy.artifact.y=10.5; legacy.artifact.health=.43;
        legacy.gate.active=true; legacy.gate.type=MiningGateType::HazardCocoon;
        legacy.gate.anchorX=legacy.gate.anchorY=10.5;
        legacy.gate.cocoonDefinitionId="thermal_intro_seal";
        legacy.gate.cocoonDefinitionVersion=2;
        legacy.gate.cocoonLayers.resize(1); legacy.gate.cocoonLayers[0].id="thermal";
        const auto at=[](auto& m,int x,int y)->auto& {return m.terrain.cells[y*20+x];};
        at(legacy,10,10).material=MiningCellMaterial::ArtifactCache;
        at(legacy,10,10).gateAssociated=true;
        for (const auto [x,y] : {std::pair{10,8},{12,10},{10,12},{8,10}}) {
            auto& cell=at(legacy,x,y); cell.material=MiningCellMaterial::HazardPocket;
            cell.hazard=true; cell.hazardAffinity=MiningElementalAffinity::Thermal;
            cell.gateAssociated=true; cell.cocoonLayer=0;
        }
        auto migrated=legacy;
        at(migrated,10,8)={}; // Already excavated.
        at(migrated,12,10).material=MiningCellMaterial::CommonOre;
        at(migrated,12,10).hazard=false; at(migrated,12,10).hazardAffinity=MiningElementalAffinity::None;
        at(migrated,12,10).remainingToughness=.6; at(migrated,12,10).revealed=true;
        migrateAdjacentCocoonTiles(migrated);
        check(migrated.gate.cocoonDefinitionVersion==3 && at(migrated,10,9).material==MiningCellMaterial::Empty &&
            at(migrated,10,8).material==MiningCellMaterial::Empty,"Migration preserves excavated locks and holes");
        check(at(migrated,11,10).material==MiningCellMaterial::CommonOre && at(migrated,11,10).remainingToughness==.6 &&
            at(migrated,11,10).revealed && migrated.artifact.health==.43,"Migration preserves treatment, damage and discovery");
        auto state=createNewGame(catalog,321); state.run.mining=migrated;
        {
            auto oldState=createNewGame(catalog,322); oldState.run.mining=legacy;
            PersistentSiteState oldSite; oldSite.mining=legacy;
            oldState.run.expedition.sites.push_back(oldSite);
            const auto oldSave=captureSaveData(oldState);
            restoreSaveData(oldState,catalog,oldSave);
            check(oldState.run.mining.gate.cocoonDefinitionVersion==3 &&
                oldState.run.expedition.sites.front().mining.gate.cocoonDefinitionVersion==3,
                "Loading v2 converts both current mining and persistent site copies");
        }
        for (int repeat=0;repeat<2;++repeat) {
            auto saved=deserializeSaveData(serializeSaveData(captureSaveData(state)));
            check(saved.has_value(),"Adjacent cocoon save parses");
            restoreSaveData(state,catalog,*saved);
            check(state.run.mining.gate.cocoonDefinitionVersion==3 && at(state.run.mining,11,10).remainingToughness==.6,
                "Repeated reload retains migrated tiles");
        }
        for (int blocked=0;blocked<7;++blocked) {
            auto copy=legacy;
            if(blocked==0) at(copy,11,10).material=MiningCellMaterial::CommonOre;
            if(blocked==1) at(copy,11,10).gateAssociated=true;
            if(blocked==2) {copy.droneX=11.5; copy.droneY=10.5; copy.rigDepthZone=copy.depthZone;}
            if(blocked==3) copy.artifact.tethered=true;
            if(blocked==4) copy.artifact.state=MiningArtifactState::Loose;
            if(blocked==5) copy.gate.cocoonDefinitionVersion=99;
            if(blocked==6) {MiningEnemy enemy; enemy.active=true; enemy.x=11.5; enemy.y=10.5; copy.enemies.push_back(enemy);}
            migrateAdjacentCocoonTiles(copy);
            check(copy.gate.cocoonDefinitionVersion==(blocked==5?99:2) && at(copy,12,10).cocoonLayer==0,
                "Unsafe or unsupported migration leaves old shape intact");
        }
        MiningDepthLayerState cached; cached.depthZone=2; cached.terrain=legacy.terrain;
        cached.artifact=legacy.artifact; cached.gate=legacy.gate;
        MiningRunState deep; deep.depthLayers.push_back(cached);
        migrateAdjacentCocoonTiles(deep);
        check(deep.depthLayers[0].gate.cocoonDefinitionVersion==3,"Cached depth-two seal migrates");
        for(auto& cell : legacy.terrain.cells) if(cell.cocoonLayer==0) cell={};
        migrateAdjacentCocoonTiles(legacy);
        check(legacy.gate.cocoonDefinitionVersion==3 && at(legacy,11,10).material==MiningCellMaterial::Empty,
            "Completed seal stays excavated");
    }
    {
        MiningRunState probe;
        probe.terrain.width = 64; probe.terrain.height = 32;
        probe.terrain.cells.resize(64 * 32);
        probe.artifact.present = true;
        probe.artifact.x = 40.5; probe.artifact.y = 16.5;
        // Center is clear of the shaft, but the left seal lies in its buffer.
        auto& seal = probe.terrain.cells[16 * 64 + 29];
        seal.gateAssociated = true; seal.cocoonLayer = 0;
        check(!orbitalShaftAvoidsProtectedObjectives(probe, 24),
            "Cocoon clearance must reject a bore even when the artifact center is clear");
        seal = {};
        check(orbitalShaftAvoidsProtectedObjectives(probe, 24),
            "Ordinary terrain outside the artifact buffer remains drillable");
        probe.depthLayers.emplace_back();
        probe.depthLayers.back().terrain = probe.terrain;
        probe.depthLayers.back().terrain.cells[16 * 64 + 29].cocoonLayer = 0;
        check(!orbitalShaftAvoidsProtectedObjectives(probe, 24),
            "Cached deeper seals receive the same clearance");
    }
    // The placement contract applies to complete prepared sites, not only the
    // active surface or the one seed that originally exposed the Io overlap.
    for (const auto& body : {std::pair{"moon","moon"},std::pair{"mars","mars"},std::pair{"io","jupiter"}})
        for (std::uint64_t seed : {7U,31U,319U,0x105CAU}) for (const auto& zone : planetLandingZones()) {
            auto state=createNewGame(catalog,seed);
            state.meta.unlockKeys.push_back(content::unlock::routeJupiter);
            state.run.expedition.travelInitialized=true;
            state.run.expedition.location.systemId="solar";
            state.run.expedition.location.bodyId=body.first;
            SurfaceLandingBuildRequest request;
            request.bodyId=body.first; request.destinationId=body.second;
            request.zoneId=zone.id; request.siteSeed=seed;
            request.allowScenarioObjectives=zone.id=="zone_1";
            auto prepared=prepareSurfaceLanding(state,catalog,request);
            check(prepared.valid,"Every seeded body/sector must prepare for bore safety checks");
            check(!prepared.laserBlocked && orbitalShaftAvoidsProtectedObjectives(prepared.miningTemplate,prepared.shaftX),
                "Fresh shafts must avoid every prebuilt objective and protected footprint");
            const auto repeated=prepareSurfaceLanding(state,catalog,request);
            check(repeated.shaftX==prepared.shaftX,"Safe shaft selection must be deterministic");
            check(prepareOrbitalSurvey(state,catalog,prepared,2),"Safety sweep must survey deeper layers");
            check(orbitalShaftAvoidsProtectedObjectives(prepared.miningTemplate,prepared.shaftX),
                "Survey-created layers must revalidate the complete shaft");
            excavateOrbitalShaft(prepared,2,.2);
            const int locked=prepared.shaftX;
            check(prepareOrbitalSurvey(state,catalog,prepared,3),"Later scans must preserve committed bore geometry");
            check(!prepared.shaftCommitted || prepared.shaftX==locked,"Started bore cannot silently switch columns");
        }

    // These Titan seeds previously put a fuel or oxygen pocket in the bore,
    // permanently setting laserBlocked. The orbital laser must now vaporize
    // ordinary terrain through the artifact layer.
    for (std::uint64_t seed : {11U,12U,24U,137U}) {
        auto titan=createNewGame(catalog,seed);
        titan.meta.unlockKeys.push_back(content::unlock::routeSaturn);
        titan.run.expedition.travelInitialized=true;
        titan.run.expedition.location.systemId="solar";
        titan.run.expedition.location.bodyId="titan";
        SurfaceLandingBuildRequest titanRequest;
        titanRequest.destinationId="saturn";
        titanRequest.bodyId="titan";
        titanRequest.zoneId=artifactSectorForBody(titan,"solar","titan");
        titanRequest.siteSeed=seed;
        auto titanBore=prepareSurfaceLanding(titan,catalog,titanRequest);
        check(titanBore.valid && prepareOrbitalSurvey(titan,catalog,titanBore,2),
            "Known Titan blocker seeds must prepare a depth-two bore");
        excavateOrbitalShaft(titanBore,2,10.0);
        check(titanBore.laserComplete && !titanBore.laserBlocked,
            "Ordinary Titan terrain must never stop the orbital laser");
        const int entry=titanBore.miningTemplate.entryDepthZone;
        for (int depth=entry; depth<=entry+2; ++depth) {
            const MiningTerrain* terrain=depth==titanBore.miningTemplate.depthZone
                ? &titanBore.miningTemplate.terrain : nullptr;
            for (const auto& layer : titanBore.miningTemplate.depthLayers)
                if (layer.depthZone==depth) {terrain=&layer.terrain; break;}
            check(terrain!=nullptr,"Every bored Titan depth must remain cached");
            const int firstRow=depth==entry ? 4 : 0;
            const int lastRow=terrain->height-(depth==entry+2 ? 4 : 1);
            for (int y=firstRow; y<=lastRow; ++y)
                for (int x=titanBore.shaftX-orbital_laser::shaftLeftCells;
                    x<=titanBore.shaftX+orbital_laser::shaftRightCells; ++x)
                    check(miningCellAt(*terrain,x,y)->material==MiningCellMaterial::Empty,
                        "The completed Titan bore must leave a continuous traversable shaft");
        }
    }

    auto state=createNewGame(catalog,0x105CA);
    state.meta.unlockKeys.push_back(content::unlock::routeJupiter);
    state.run.expedition.travelInitialized=true;
    state.run.expedition.location.systemId="solar";
    state.run.expedition.location.bodyId="io";
    SurfaceLandingBuildRequest request;
    request.destinationId="jupiter"; request.bodyId="io"; request.siteSeed=319;
    auto prepared=prepareSurfaceLanding(state,catalog,request);
    check(prepareOrbitalSurvey(state,catalog,prepared,2),"Io migration fixture must survey");
    prepared.surveyComplete=true;
    auto objectiveLayer = [](auto& mining) -> auto& {
        const auto found=std::find_if(mining.depthLayers.begin(),mining.depthLayers.end(),
            [](const auto& layer){return layer.artifact.present;});
        check(found!=mining.depthLayers.end(),"Fixture needs a cached protected artifact");
        return *found;
    };
    int generatedRepairs=0;
    for (std::uint64_t seed=1; seed<=16; ++seed) {
        auto generatedState=state;
        generatedState.seed=seed;
        auto generatedRequest=request;
        generatedRequest.siteSeed=seed;
        auto generated=prepareSurfaceLanding(generatedState,catalog,generatedRequest);
        check(prepareOrbitalSurvey(generatedState,catalog,generated,2),"Real Io terrain must survey for legacy repair");
        const auto& before=objectiveLayer(generated.miningTemplate);
        // Recreate a legacy bore crossing the real objective footprint while
        // leaving all generated lava, resources, tunnels and actors intact.
        generated.shaftX=static_cast<int>(std::floor(before.artifact.x))+4;
        generated.laserDepth=before.depthZone;
        generated.laserRow=static_cast<int>(std::floor(before.artifact.y))-4;
        generated.laserBlocked=true;
        PersistentSiteState old{"solar","io","io.beacon:zone_1",generated.expeditionTemplate,
            generated.miningTemplate,static_cast<const OrbitalSiteProgress&>(generated)};
        const auto restored=restoreSurfaceLanding(generatedState,catalog,generatedRequest,old);
        const auto& after=objectiveLayer(restored.miningTemplate);
        const bool moved=after.artifact.x!=before.artifact.x;
        generatedRepairs+=moved;
        check((moved && orbitalShaftAvoidsProtectedObjectives(restored.miningTemplate,restored.shaftX)) ||
            (!moved && restored.laserBlocked),"Real legacy overlap must safely relocate or retain its defensive block");
        check(after.artifact.y==before.artifact.y && after.artifact.health==before.artifact.health,
            "Real Io migration must preserve depth and artifact condition");
        for (std::size_t i=0;i<before.terrain.cells.size();++i) {
            const auto& cell=before.terrain.cells[i];
            if (cell.material==MiningCellMaterial::Empty || cell.material==MiningCellMaterial::CommonOre ||
                cell.material==MiningCellMaterial::RareOre || cell.material==MiningCellMaterial::ExoticVein ||
                cell.material==MiningCellMaterial::FuelPocket || cell.material==MiningCellMaterial::OxygenPocket)
                check(after.terrain.cells[i].material==cell.material,
                    "Real-terrain repair must never erase an existing tunnel, ore or supply pocket");
        }
    }
    std::cout << "Real Io legacy bore repairs: " << generatedRepairs << "/16\n";
    check(generatedRepairs>0,"Real Io repair audit must exercise at least one successful relocation");
    auto& objective=objectiveLayer(prepared.miningTemplate);
    check(objective.depthZone==2 && objective.gate.cocoonLayers.size()==1,"Io fixture must use its depth-two thermal seal");
    for(const auto& offset : catalog.findMiningSite(content::miningSite::thermalLayeredRecovery)->cocoon.layers.front().offsets) {
        const int x=static_cast<int>(objective.artifact.x)+offset.x;
        const int y=static_cast<int>(objective.artifact.y)+offset.y;
        check(std::abs(offset.x)+std::abs(offset.y)==1 && objective.terrain.cells[y*objective.terrain.width+x].cocoonLayer==0,
            "Generated Io depth-two seal must have actual hazard tiles touching the artifact");
    }
    // Supply ordinary virgin space for a deterministic migration, retaining
    // the real generated artifact and authored seal cells exactly as built.
    for (auto& cell : objective.terrain.cells) if (!cell.gateAssociated && cell.cocoonLayer<0) {
        cell={}; cell.material=MiningCellMaterial::Regolith;
        cell.maxToughness=cell.remainingToughness=2.0;
    }
    objective.enemies.clear(); objective.looseObjects.clear();
    const int oldX=static_cast<int>(std::floor(objective.artifact.x));
    const int oldY=static_cast<int>(std::floor(objective.artifact.y));
    const auto at=[](auto& terrain,int x,int y) -> auto& {return terrain.cells[static_cast<std::size_t>(y*terrain.width+x)];};
    // Retain a genuine v2 fixture to exercise old-shape bore repair when
    // adjacent resources prevent the independent compact-cocoon migration.
    for (const auto [dx,dy] : {std::pair{0,-1}, {1,0}, {0,1}, {-1,0}}) {
        std::swap(at(objective.terrain,oldX+dx,oldY+dy),at(objective.terrain,oldX+dx*2,oldY+dy*2));
        at(objective.terrain,oldX+dx,oldY+dy).material=MiningCellMaterial::CommonOre;
    }
    objective.gate.cocoonDefinitionVersion=2;
    // One seal tile is excavated, another partly treated/damaged. These are
    // recovery progress, not permission to recreate an intact seal on load.
    at(objective.terrain,oldX,oldY-2)={};
    at(objective.terrain,oldX,oldY-2).revealed=true;
    at(objective.terrain,oldX,oldY-2).gateAssociated=true;
    at(objective.terrain,oldX,oldY-2).cocoonLayer=0; // Legacy retained ownership on an excavated tile.
    auto& damaged=at(objective.terrain,oldX+2,oldY);
    damaged.material=MiningCellMaterial::CommonOre; // Already treated, not virgin hazard.
    damaged.hazard=false;
    damaged.hazardAffinity=MiningElementalAffinity::None;
    damaged.remainingToughness=damaged.maxToughness*.4;
    damaged.revealed=true;
    objective.artifact.health=.43;
    objective.artifact.embedStrength=.27;
    objective.artifact.revealed=true;
    objective.gate.cocoonLayers[0].remaining=3;
    objective.gate.cocoonLayers[0].revealed=true;
    objective.gate.shellTilesRemaining=objective.gate.outerShellTilesRemaining=3;
    objective.hasDownwardTransition=true;
    objective.downwardTransitionX=oldX;
    prepared.shaftX=oldX;
    prepared.laserDepth=objective.depthZone;
    prepared.laserRow=oldY-4;
    prepared.laserRowWork=.015;
    prepared.laserBlocked=true;
    at(objective.terrain,oldX,oldY-5)={}; // Existing bore remains where excavated.
    at(objective.terrain,oldX,oldY-5).feature=MiningCellFeature::MainTunnel;
    PersistentSiteState saved{"solar","io","io.beacon:zone_1",prepared.expeditionTemplate,
        prepared.miningTemplate,static_cast<const OrbitalSiteProgress&>(prepared)};
    const auto restore=[&](const PersistentSiteState& site){return restoreSurfaceLanding(state,catalog,request,site);};
    auto repaired=restore(saved);
    const auto& moved=objectiveLayer(repaired.miningTemplate);
    const int dx=static_cast<int>(moved.artifact.x-objective.artifact.x);
    check(dx!=0 && moved.artifact.y==objective.artifact.y && moved.depthZone==objective.depthZone,
        "Existing embedded objective must move only sideways at the same depth");
    check(orbitalShaftAvoidsProtectedObjectives(repaired.miningTemplate,repaired.shaftX) && !repaired.laserBlocked,
        "Repaired seal must clear the protected bore envelope and unblock a now-safe saved row");
    check(moved.artifact.id==objective.artifact.id && moved.artifact.health==.43 && moved.artifact.embedStrength==.27 &&
        moved.artifact.revealed && moved.gate.cocoonLayers[0].remaining==3 && moved.gate.shellTilesRemaining==3,
        "Migration must preserve physical damage, discovery, identity and partial gate progress");
    check(at(moved.terrain,oldX+dx,oldY-2).material==MiningCellMaterial::Empty &&
        !at(moved.terrain,oldX,oldY-2).gateAssociated && at(moved.terrain,oldX,oldY-2).cocoonLayer==-1 &&
        at(moved.terrain,oldX+2+dx,oldY).material==MiningCellMaterial::CommonOre &&
        at(moved.terrain,oldX+2+dx,oldY).remainingToughness==damaged.remainingToughness,
        "Removed and partly damaged seal members must retain their exact state at the new anchor");
    check(at(moved.terrain,oldX,oldY-5).material==MiningCellMaterial::Empty &&
        at(moved.terrain,oldX,oldY-5).feature==MiningCellFeature::MainTunnel &&
        at(moved.terrain,oldX,oldY).material==MiningCellMaterial::Empty &&
        moved.hasDownwardTransition && moved.downwardTransitionX==objective.downwardTransitionX &&
        repaired.shaftX==saved.orbital.shaftX && repaired.laserDepth==saved.orbital.laserDepth &&
        repaired.laserRow==saved.orbital.laserRow && repaired.laserRowWork==saved.orbital.laserRowWork,
        "Migration must not refill excavation, redirect portals or reset orbital work");
    for (int y=0; y<moved.terrain.height; ++y) for (int x=0; x<moved.terrain.width; ++x) {
        const auto member=[&](int cx){return (x==cx && (y==oldY || y==oldY-2 || y==oldY+2)) ||
            ((x==cx-2 || x==cx+2) && y==oldY);};
        if (member(oldX) || member(oldX+dx)) continue;
        const auto& before=at(objective.terrain,x,y); const auto& after=at(moved.terrain,x,y);
        check(before.material==after.material && before.remainingToughness==after.remainingToughness &&
            before.revealed==after.revealed && before.feature==after.feature,
            "Cells outside the old/new objective footprint must remain unchanged");
    }
    auto second=restore(saved);
    check(objectiveLayer(second.miningTemplate).artifact.x==moved.artifact.x,
        "Legacy repair must choose the same safe translation every time");
    state.run.expedition.sites={PersistentSiteState{"solar","io","io.beacon:zone_1",repaired.expeditionTemplate,
        repaired.miningTemplate,static_cast<const OrbitalSiteProgress&>(repaired)}};
    const auto decoded=deserializeExpedition(serializeExpedition(state.run.expedition));
    check(decoded.has_value(),"Repaired site must persist through the real expedition serializer");
    const auto reloaded=restore(decoded->sites.front());
    const auto& reloadArtifact=objectiveLayer(reloaded.miningTemplate).artifact;
    check(reloadArtifact.x==moved.artifact.x && reloadArtifact.health==moved.artifact.health &&
        reloadArtifact.embedStrength==moved.artifact.embedStrength && reloaded.shaftX==saved.orbital.shaftX,
        "Save/reload must be idempotent without re-healing or shifting the artifact again");
    {
        auto live = std::make_unique<GameState>(state);
        live->run.expedition.sites = {saved};
        live->run.expedition.location = {"solar","io",CoordinateFrame::Body,{}, {},0,saved.siteId};
        live->run.planetaryExpedition = saved.surface;
        live->run.planetaryExpedition.active = true;
        live->run.mining = saved.mining;
        check(activateLandingLayer(live->run.mining,objective.depthZone),"Load fixture must enter the artifact layer");
        live->run.mining.active = true;
        live->run.mining.droneX = 3.5;
        live->run.mining.droneY = 7.5;
        live->run.mining.rigDepthZone = objective.depthZone;
        live->run.mining.rigOxygen.current = 37;
        live->screen = Screen::Mining;
        double repairedX = 0;
        for (int reload=0; reload<2; ++reload) {
            const auto data = deserializeSaveData(serializeSaveData(captureSaveData(*live)));
            check(data.has_value(),"Active bore overlap must survive save serialization");
            restoreSaveData(*live,catalog,*data);
            const auto& current = live->run.mining;
            const auto& stored = live->run.expedition.sites.front();
            check(current.artifact.x!=objective.artifact.x &&
                orbitalShaftAvoidsProtectedObjectives(current,stored.orbital.shaftX),
                "Loading directly into mining must repair the visible artifact, without requiring a revisit");
            check(current.droneX==3.5 && current.droneY==7.5 && current.rigOxygen.current==37 &&
                current.artifact.health==.43 && current.artifact.embedStrength==.27 &&
                current.gate.shellTilesRemaining==3 && stored.orbital.shaftX==saved.orbital.shaftX,
                "Active-load repair preserves crew, oxygen, partial recovery and the existing bore");
            check(stored.mining.artifact.x==current.artifact.x,
                "Live and dormant site snapshots must agree after active-load repair");
            if (reload) check(current.artifact.x==repairedX,"Reloading a repaired active site must not move it again");
            repairedX = current.artifact.x;
        }
    }
    const double health=moved.artifact.health;
    excavateOrbitalShaft(repaired,2,5.0);
    check(objectiveLayer(repaired.miningTemplate).artifact.health==health,
        "Resumed orbital laser must never damage the shifted artifact");

    for (int excluded=0; excluded<6; ++excluded) {
        auto unsafe=saved;
        auto& layer=objectiveLayer(unsafe.mining);
        if (excluded==0) layer.artifact.tethered=true;
        if (excluded==1) layer.artifact.state=MiningArtifactState::Loose;
        if (excluded==2) layer.artifact.state=MiningArtifactState::Delivered;
        if (excluded==3) layer.artifact.state=MiningArtifactState::Destroyed;
        if (excluded==4) ++layer.gate.cocoonDefinitionVersion;
        if (excluded==5) for (auto& cell : layer.terrain.cells)
            if (!cell.gateAssociated && cell.cocoonLayer<0) {cell={}; cell.material=MiningCellMaterial::CommonOre;}
        const auto unchanged=restore(unsafe);
        const auto& result=objectiveLayer(unchanged.miningTemplate);
        check(result.artifact.x==layer.artifact.x && result.artifact.health==layer.artifact.health && unchanged.laserBlocked,
            "Unsafe, recovered, unsupported and no-room artifacts must not relocate or unblock the laser");
        for (std::size_t i=0;i<layer.terrain.cells.size();++i)
            check(result.terrain.cells[i].material==layer.terrain.cells[i].material &&
                result.terrain.cells[i].remainingToughness==layer.terrain.cells[i].remainingToughness,
                "Failed migration must be atomic, preserving all terrain and partial seal damage");
    }
    auto obstructed=saved;
    at(objectiveLayer(obstructed.mining).terrain,oldX,saved.orbital.laserRow).material=MiningCellMaterial::FuelPocket;
    auto guarded=restore(obstructed);
    check(objectiveLayer(guarded.miningTemplate).artifact.x!=objective.artifact.x && !guarded.laserBlocked,
        "Saved supply-pocket obstructions must no longer block a repaired bore");
    excavateOrbitalShaft(guarded,2,1.0);
    check(at(objectiveLayer(guarded.miningTemplate).terrain,oldX,guarded.laserRow-1).material==MiningCellMaterial::Empty,
        "The resumed orbital laser must vaporize a saved supply pocket");

    // A new protected object added to a committed preview must halt before any
    // excavation. Revalidation is defensive, not a relocation during flight.
    auto late=reloaded;
    auto& lateLayer=objectiveLayer(late.miningTemplate);
    at(lateLayer.terrain,late.shaftX,late.laserRow).gateAssociated=true;
    late.laserBlocked=false;
    const auto row=late.laserRow;
    excavateOrbitalShaft(late,2,1.0);
    check(late.laserBlocked && late.shaftX==saved.orbital.shaftX && late.laserRow==row &&
        at(lateLayer.terrain,late.shaftX,late.laserRow).gateAssociated,
        "Repeated validation must stop new protected overlaps before damage without moving a committed bore");
}
} // namespace
void campaignGuidanceTests()
{
    using namespace rocket;
    {
        MiningRunState site;
        site.terrain.width = 40; site.terrain.height = 24;
        site.terrain.cells.resize(40*24);
        site.returnZoneX = 20; site.returnZoneY = 4;
        for (int y=0; y<24; ++y) for (int x=0; x<40; ++x) {
            auto& cell = site.terrain.cells[y*40+x];
            const bool shaft = x>=18 && x<=22 && y<8;
            cell.material = y<4 || shaft ? MiningCellMaterial::Empty : MiningCellMaterial::Regolith;
            cell.remainingToughness = cell.material == MiningCellMaterial::Empty ? 0 : 10;
        }
        double rigX=0,rigY=0;
        check(surfaceLandingStaging(site,20,8,rigX,rigY) && rigY<4,
            "A ship at the bottom of a shaft must be able to deploy above its blocked side exits");
        const auto catalog = createDefaultContent();
        const auto state = createNewGame(catalog, 914);
        const auto model = expeditionFlightModel(state,catalog);
        auto flight = beginLaunchFlight(model,*catalog.findDestination("mars"));
        flight.active = flight.physicalFlight = true;
        flight.mode = FlightMode::Landing;
        flight.landing.altitude = -16.0;
        flight.landing.heading = 1.5707963267948966;
        flight.handoff.elapsed = flight_landing::handoffSeconds;
        const auto touchdown = updateLaunchFlight(flight,model,*catalog.findDestination("mars"),{},.02,&site);
        check(touchdown.reachedDestination && flight.phase == FlightPhase::Landed,
            "Stationary supported ship sixteen metres down a shaft must finish landing");
        check(positionSurfaceLandingTeam(site,flight.landing.touchdownGridX,flight.landing.touchdownGridY),
            "Committed shaft touchdown must retain a usable deployment position");
    }
    for (const double steer : {-1.0, 1.0}) {
        FlightRunState baseline;
        for (int frame = 0; frame < 60; ++frame) advanceFlightHeading(baseline, steer, .016, 0);
        for (int rank = 1; rank <= 3; ++rank) {
            FlightRunState upgraded;
            for (int frame = 0; frame < 60; ++frame) advanceFlightHeading(upgraded, steer, .016, rank);
            check(std::abs(upgraded.angularVelocity / baseline.angularVelocity - (1.0 + .15*rank)) < 1e-9 &&
                  std::abs(upgraded.heading / baseline.heading - (1.0 + .15*rank)) < 1e-9,
                "Flight Controls must improve turning response and sustained speed by fifteen percent per rank in both directions");
            const double before = upgraded.angularVelocity;
            advanceFlightHeading(upgraded, 0, .016, rank);
            check(std::abs(upgraded.angularVelocity / before - std::exp(-5.2*.016)) < 1e-9,
                "Upgraded steering must retain the same release damping");
        }
    }
    {
        const auto catalog = createDefaultContent();
        auto state = std::make_unique<GameState>(createNewGame(catalog, 918));
        initializeLiveExpedition(*state, catalog);
        state->meta.shipsLost = 1;
        state->run.expedition.location = {"solar", "earth", CoordinateFrame::Body, {}, {}, 0, "earth.dock"};
        state->screen = Screen::Hangar;
        state->incomingMessages = {};
        WreckState wreck; wreck.id = 1;
        wreck.location = {"solar", "", CoordinateFrame::System, {12,9}, {}, 0, {}};
        state->run.expedition.wrecks.push_back(wreck);
        state->run.expedition.nextWreckId = 2;
        reconcileCampaignGuidance(*state, catalog);
        check(state->incomingMessages.pending.size() == 1 &&
            state->incomingMessages.pending.front().messageId == "wreck_salvage_intro",
            "first ordinary ship loss explains salvage even without an artifact");
        check(acknowledgeIncomingMessage(state->incomingMessages, "tutorial.wreck_salvage").has_value(),
            "salvage introduction requires explicit acknowledgement");
        const auto saved = deserializeSaveData(serializeSaveData(captureSaveData(*state)));
        check(saved.has_value(), "salvage tutorial state saves");
        auto restored = std::make_unique<GameState>(createNewGame(catalog, 919));
        restoreSaveData(*restored, catalog, *saved);
        restored->meta.shipsLost = 2;
        reconcileCampaignGuidance(*restored, catalog);
        check(restored->incomingMessages.pending.empty(), "acknowledged salvage tutorial does not replay after reload or another loss");
    }
    {
        const auto catalog=createDefaultContent();
        auto state=std::make_unique<GameState>(createNewGame(catalog,811));
        auto& e=state->run.expedition;
        e.travelInitialized=true;
        e.location={"solar","earth",CoordinateFrame::Body,{}, {},0,"earth.dock"};
        state->screen=Screen::Hangar;
        WreckState wreck; wreck.id=7;
        wreck.location={"solar","",CoordinateFrame::System,{12,9},{.1,.2},0,{}};
        wreck.cargo.materials.common=20;
        e.wrecks.push_back(wreck);
        e.nextWreckId=8;
        e.batteries[1].owner=BatteryOwner::Wreck; e.batteries[1].wreckId=7;
        e.course.targetBodyId="mars";
        check(reconcileCampaignGuidance(*state,catalog),"dock should select artifact recovery");
        check(e.course.targetBodyId=="wreck:7" && state->incomingMessages.pending.size()==1,"recovery gets one notice and a wreck course");
        acknowledgeIncomingMessage(state->incomingMessages,"recovery.wreck.7");
        reconcileCampaignGuidance(*state,catalog);
        check(state->incomingMessages.pending.empty(),"acknowledged wreck notice must not replay");
        const auto saved=deserializeSaveData(serializeSaveData(captureSaveData(*state)));
        check(saved && saved->expedition.course.targetBodyId=="wreck:7","wreck target must survive save serialization");
        auto restored=std::make_unique<GameState>(createNewGame(catalog,1));
        restoreSaveData(*restored,catalog,*saved);
        check(courseWreck(restored->run.expedition,"wreck:7")!=nullptr,"restored target resolves actual wreck");
        check(wreckCarriesArtifact(restored->run.expedition, 7) &&
            wreckDisplayName(restored->run.expedition, 7) == "Artifact / Wreck 7",
            "artifact wreck presentation derives from saved ownership");
        const auto pose=courseTargetLocation(e,solarSystemDefinition(),"wreck:7");
        check(pose && pose->position.x==12 && pose->velocity.y==.2,"wreck guidance preserves position and velocity");
        e.coursePlayerSelected=true; e.course.targetBodyId="venus";
        reconcileCampaignGuidance(*state,catalog);
        check(e.coursePlayerSelected && e.course.targetBodyId=="venus","unvisited exploration override survives docking");
        e.location.bodyId="venus"; e.location.siteId.clear(); state->run.flight.orbit.captured=false;
        reconcileCampaignGuidance(*state,catalog);
        check(e.coursePlayerSelected,"mere influence crossing must not consume override");
        state->run.flight.orbit.captured=true;
        reconcileCampaignGuidance(*state,catalog);
        check(e.coursePlayerSelected && e.course.targetBodyId=="venus","manual waypoint persists until Return to mission, including after orbit capture");
        e.location=wreck.location;
        const auto salvagePose = e.location;
        const auto salvageFlight = state->run.flight;
        check(salvageWreck(e,7,solarSystemDefinition(),0)==ExpeditionResult::Applied,"artifact salvage succeeds even with full ore hold");
        reconcileCampaignGuidance(*state,catalog,true);
        check(e.location.position.x == salvagePose.position.x && e.location.position.y == salvagePose.position.y &&
              e.location.velocity.x == salvagePose.velocity.x && e.location.velocity.y == salvagePose.velocity.y &&
              state->run.flight.positionX == salvageFlight.positionX && state->run.flight.positionY == salvageFlight.positionY &&
              state->run.flight.velocityX == salvageFlight.velocityX && state->run.flight.velocityY == salvageFlight.velocityY &&
              state->run.flight.heading == salvageFlight.heading,
            "Artifact salvage and automatic retargeting must never move or rotate the ship");
        check(e.batteries[1].owner==BatteryOwner::Ship && e.course.targetBodyId=="earth" && !e.wrecks.empty(),
            "recovered artifact directs home while leftover ore remains optional");
        check(!wreckCarriesArtifact(e, 7) && wreckDisplayName(e, 7) == "Wreck 7",
            "partially salvaged ore wreck must immediately lose its artifact marker");
        e.batteries[1].owner=BatteryOwner::EarthStorage;
        check(recommendedCampaignObjective(*state,catalog).kind==CampaignObjectiveKind::SecureArtifact,"banked artifact waits for explicit mission hand-in");
        e.batteries[1].owner=BatteryOwner::Wreck; e.batteries[1].wreckId=999;
        e.course.targetBodyId="wreck:999";
        reconcileCampaignGuidance(*state,catalog,true);
        check(e.course.targetBodyId.empty() && e.batteries[1].wreckId==999 &&
            recommendedCampaignObjective(*state,catalog).kind==CampaignObjectiveKind::RecoveryUnavailable,
            "missing wreck invalidates navigation without erasing artifact ownership");
        e.batteries[1].owner=BatteryOwner::Ship; e.batteries[1].wreckId=0;
        e.active=true; e.coursePlayerSelected=true; e.course.targetBodyId="venus";
        check(loseExpedition(e,state->run.flight,solarSystemDefinition())==ExpeditionResult::Applied,
            "crash should preserve artifact in a new wreck");
        check(!e.coursePlayerSelected && e.batteries[1].owner==BatteryOwner::Wreck,
            "crash clears exploration override without losing artifact ownership");
        reconcileCampaignGuidance(*state,catalog);
        check(e.course.targetBodyId=="wreck:"+std::to_string(e.batteries[1].wreckId),
            "replacement dock must target the new artifact wreck");
        const auto recoveryNoticeCount=state->incomingMessages.pending.size();
        reconcileCampaignGuidance(*state,catalog);
        check(state->incomingMessages.pending.size()==recoveryNoticeCount,"new wreck recovery notice queues exactly once");
    }
}
void salvageSpeedBoundaryTests(rocket::PersistentExpeditionState& e, rocket::FlightRunState& flight,
    const rocket::SystemDefinition& system)
{
    using namespace rocket;
    e.location.position.x -= 0.002;
    e.location.velocity.x += expeditionSalvageSpeed;
    restoreSystemLocation(e.location, flight);
    check(canSalvageWreck(e, flight, system, 1),
          "Wreck salvage must allow relative speed exactly 1.0");
    e.location.velocity.x += 0.001;
    restoreSystemLocation(e.location, flight);
    check(!canSalvageWreck(e, flight, system, 1) &&
              salvageWreck(e, 1, system, 24) == ExpeditionResult::OutOfRange,
          "Wreck salvage UI and action must reject relative speed above 1.0");
}

void missionGuidanceTests()
{
    using namespace rocket;
    const auto catalog = createDefaultContent();
    for (const auto body : {"moon", "mars"}) {
        auto state = std::make_unique<GameState>(createNewGame(catalog, 915));
        auto& s = *state;
        initializeLiveExpedition(s, catalog);
        s.meta.unlockKeys.push_back(content::unlock::routeMars);
        auto& e = s.run.expedition;
        e.location.bodyId = body;
        e.trackedMissionId = body;
        s.screen = Screen::Flight;
        auto& f = s.run.flight;
        f.mode = FlightMode::Orbit; f.orbit.captured = true;
        const auto sector = artifactSectorForBody(s, "solar", body);
        auto& t = e.arrivalTutorials[arrivalTutorialIndex(body)];
        e.selectedOrbitZone = "wrong";
        OrbitalSiteProgress orbital; orbital.laserComplete = true;
        updateArrivalTutorial(s, catalog, f, true, &orbital);
        check(t.orbit && !t.scanned && !t.drilled, "Wrong sector cannot complete scan or drill tutorial");
        e.location.siteId = "site:wrong";
        recordTutorialTouchdown(s, catalog);
        check(!t.landed, "Wrong sector cannot complete tutorial arrival");
        e.selectedOrbitZone = sector;
        orbital.laserComplete = false;
        updateArrivalTutorial(s, catalog, f, true, &orbital);
        check(t.scanned && !t.landed, "Scan does not imply touchdown");
        e.location.siteId = "site:" + sector;
        recordTutorialTouchdown(s, catalog);
        check(arrivalBriefingRequired(s, body), "Touchdown requires acknowledgement");
        check(t.drillBypassed == (std::string_view(body) == "mars"), "Early Mars landing records bypass without completing drill");
        auto restored = deserializeExpedition(serializeExpedition(e));
        check(restored && restored->arrivalTutorials[arrivalTutorialIndex(body)].landed &&
            !restored->arrivalTutorials[arrivalTutorialIndex(body)].acknowledged, "Pending arrival survives save/load");
        t.acknowledged = true;
        check(!missionView(s, catalog, body).arrivalStage && !arrivalBriefingRequired(s, body), "Continue switches to recovery");
        recordTutorialTouchdown(s, catalog);
        check(!arrivalBriefingRequired(s, body), "Return visit does not replay briefing");
        auto encoded = serializeExpedition(e);
        encoded.resize(encoded.find(" arrivals1"));
        restored = deserializeExpedition(encoded);
        check(restored && !restored->arrivalTutorialsLoaded, "Older saves remain readable");
        e = *restored;
        s.run.mining.active = true; s.run.mining.bodyId = body;
        migrateArrivalTutorials(s, catalog);
        check(e.arrivalTutorials[arrivalTutorialIndex(body)].acknowledged, "Legacy mining resumes Recovery without a briefing");
    }
    for (const auto seed : {7, 32, 918}) {
        auto state = std::make_unique<GameState>(createNewGame(catalog, seed));
        auto& s = *state;
        initializeLiveExpedition(s, catalog);
        auto& e = s.run.expedition;
        auto view = trackedMissionView(s, catalog);
        check(view.id == "moon" && view.stepId == "travel", "Fresh campaign tracks the Moon journey");
        check(view.arrivalStage && view.trackerGoals.size() == 3 && !view.trackerGoals[0].complete &&
            !view.trackerGoals[1].complete && !view.trackerGoals[2].complete, "Arrival only exposes orbit, scan and landing");
        check(missionLog(s, catalog).size() == 1, "Unrevealed missions stay out of the log");
        e.location.bodyId = "moon"; e.location.siteId.clear();
        s.screen = Screen::Flight; s.run.flight.orbit.captured = true;
        const auto sector = artifactSectorForBody(s, "solar", "moon");
        e.selectedOrbitZone = sector;
        check(trackedMissionView(s, catalog).stepId == "survey", "Captured orbit asks for a scan");
        view = trackedMissionView(s, catalog, nullptr, true);
        check(view.sectorKnown && view.sectorId == sector && view.stepId == "land", "First survey identifies the actual artifact sector");
        check(firstMoonMissionInstructions(s, catalog).find(missionSectorName(sector)) != std::string::npos &&
            firstMoonMissionInstructions(s, catalog).find("brief you on recovery after touchdown") != std::string::npos,
            "First scan teaches arrival before recovery");
        s.screen = Screen::Mining; s.run.mining.active = true; s.run.mining.bodyId = "moon";
        e.location.siteId = "moon.surface:" + sector;
        performScenarioAction(s, catalog, content::scenario::lunarProspector, "briefing", ScenarioActionKind::AcknowledgeBriefing);
        s.run.mining.cargo = 20;
        view = trackedMissionView(s, catalog);
        check(view.stepId == "ore" && view.progress.front() == "Common Ore collected 0/20", "Carried ore does not count as ship collection");
        e.location.bodyId = "earth"; // Stale travel frame must not override active surface ownership.
        view = trackedMissionView(s, catalog);
        check(view.stepId == "ore" && !view.arrivalStage && !view.trackerGoals[0].complete,
            "Active mining shows recovery even with a stale travel location");
        e.location.bodyId = "moon";
        recordScenarioEvent(s, catalog, {ScenarioEventKind::SafeMaterialDelivered, content::scenario::lunarProspector,"delivery","moon","common",8,0});
        check(trackedMissionView(s, catalog).progress.front() == "Common Ore collected 8/20", "Tracker follows actual scenario collection counts");
        recordScenarioEvent(s, catalog, {ScenarioEventKind::SafeMaterialDelivered, content::scenario::lunarProspector,"delivery","moon","common",12,0});
        view = trackedMissionView(s, catalog);
        check(view.stepId == "scan_artifact", "Ore delivery unlocks the scanner instruction");
        check(view.trackerGoals[0].complete && !view.trackerGoals[1].complete,
            "Ore and artifact remain independent checklist goals");
        auto& artifact = s.run.mining.artifact;
        artifact.present = artifact.revealed = true;
        artifact.state = MiningArtifactState::Loose;
        check(trackedMissionView(s, catalog).progress.back() == "Artifact: collect artifact", "Exposed is not recovered");
        artifact.tethered = true;
        view = trackedMissionView(s, catalog);
        check(view.stepId == "carry" && !view.requirements[view.requirements.size()-2].complete, "Tethered artifact remains incomplete");
        e.batteries[0].owner = BatteryOwner::Ship;
        check(trackedMissionView(s, catalog).progress.back() == "Artifact: aboard ship", "Physical capture is shown as aboard ship");
        check(trackedMissionView(s, catalog).trackerGoals[1].complete, "Artifact checkbox requires physical delivery");
        e.batteries[0].owner = BatteryOwner::EarthStorage;
        e.location = {"solar", "earth", CoordinateFrame::Body, {}, {}, 0.0, "earth.dock"};
        s.run.flight.active = false;
        reconcileArtifactCustody(s, catalog);
        bankMissionArtifacts(s, catalog);
        check(trackedMissionView(s, catalog).progress.back() == "Artifact: secured at dock", "Banked ownership takes precedence over stale site snapshots");
        recordScenarioEvent(s,catalog,{ScenarioEventKind::ProtectedObjectiveExtracted, content::scenario::lunarProspector,"anomaly","moon",content::miningSite::lunarAnomalyCrevice,1,0});
        check(trackedMissionView(s, catalog).stepId == "claim", "Completed recovery waits for the explicit claim");
        performScenarioAction(s,catalog,content::scenario::lunarProspector,"anomaly",ScenarioActionKind::ClaimReward);
        check(trackedMissionView(s, catalog).id == "mars", "Claim advances the campaign tracker");
        s.run.mining.bodyId = "mars";
        const auto mars = trackedMissionView(s, catalog);
        check(mars.purpose.find("artifact is underground") != std::string::npos, "Mars teaches underground recovery");
        {
            auto orbitState = std::make_unique<GameState>(s);
            auto& test = *orbitState;
            test.screen = Screen::Flight;
            test.run.mining.active = false;
            test.run.flight.mode = FlightMode::Orbit;
            test.run.flight.orbit.captured = true;
            auto& expedition = test.run.expedition;
            expedition.location.bodyId = "mars";
            expedition.selectedOrbitZone = mars.sectorId;
            check(missionView(test, catalog, "mars", nullptr, true).stepId == "drill", "Scanned Mars recommends drilling");
            PersistentSiteState site;
            site.systemId = "solar"; site.bodyId = "mars"; site.siteId = "mars:" + mars.sectorId;
            site.orbital.surveyComplete = true; site.orbital.laserComplete = true;
            expedition.sites.push_back(site);
            check(missionView(test, catalog, "mars", nullptr, true).instruction.starts_with("Shaft ready"), "Completed Mars shaft recommends landing");
            expedition.sites.back().orbital.laserComplete = false;
            expedition.sites.back().orbital.laserBlocked = true;
            check(missionView(test, catalog, "mars", nullptr, true).instruction.starts_with("Protected terrain"), "Blocked Mars shaft recommends surface tools");
            expedition.selectedOrbitZone = "wrong-sector";
            check(missionView(test, catalog, "mars", nullptr, true).stepId == "mission_sector", "Wrong sector does not recommend drilling");
        }
        check(mars.stepId == "ore" && !mars.arrivalStage &&
            mars.trackerGoals[0].text == "Collect Common Ore 0/8" && !mars.trackerGoals[1].complete,
            "Mars surface checklist contains recovery goals only");
        const auto oldScenario = s.run.mining.scenarioId;
        s.run.mining.bodyId.clear(); s.run.mining.scenarioId = content::scenario::marsBayExpansion;
        check(trackedMissionView(s, catalog).stepId == "ore",
            "Legacy mining sessions without a body ID use their actual mission scenario");
        s.run.mining.scenarioId = oldScenario;
        s.run.mining.bodyId = "moon";
        e.batteries[0].owner = BatteryOwner::Ship;
        e.trackedMissionId = "moon";
        check(trackedMissionView(s, catalog).id == "mars", "Claim advances even before Earth banking");
        e.trackedMissionId = "venus"; e.course.targetBodyId = "mars"; e.coursePlayerSelected = true;
        reconcileCampaignGuidance(s, catalog);
        check(e.trackedMissionId == "venus" && e.course.targetBodyId == "mars", "Optional tracking preserves a manual exploration waypoint");
        for (const auto intro : {MissionScanIntro::Unseen, MissionScanIntro::Showing, MissionScanIntro::Complete}) {
            e.missionScanIntro = intro;
            const auto encoded = serializeExpedition(e);
            const auto restored = deserializeExpedition(encoded);
            check(restored && restored->trackedMissionId == "venus" && restored->missionScanIntro == intro,
                "Tracked mission and all scan introduction stages survive reload");
            const auto legacy = deserializeExpedition(encoded.substr(0, encoded.rfind(" missions1 ")));
            check(legacy && !legacy->missionGuidanceLoaded, "Legacy expeditions accept the absent mission extension");
        }
        e.trackedMissionId = "removed_mission";
        reconcileTrackedMission(s, catalog);
        check(e.trackedMissionId == "mars", "Unknown saved mission IDs fall back to current campaign progress");
    }
}

void artifactBankingAndPayloadTests()
{
    using namespace rocket;
    const auto catalog=createDefaultContent();
    auto state=std::make_unique<GameState>(createNewGame(catalog,72131));
    initializeLiveExpedition(*state,catalog);
    ensureScenarioInstances(*state,catalog);
    const auto* mission=solarMissionForBody(catalog,"moon");
    check(mission!=nullptr,"Moon must have a banking mission");
    auto* instance=findScenarioInstance(state->meta,mission->scenarioId);
    check(instance!=nullptr,"Moon scenario must exist");
    for (auto& p : instance->steps) if (p.id!=mission->claimStepId) {
        p.completed=p.claimed=p.briefingAcknowledged=true;
    }
    auto& e=state->run.expedition;
    e.location.bodyId="moon";e.location.siteId="moon:zone_1";
    ArtifactRecord record;record.id=mission->artifactId;record.originDestinationId="moon";
    registerArtifactAboard(*state,catalog,record,e.location.siteId);
    registerArtifactAboard(*state,catalog,record,e.location.siteId);
    check(e.artifacts.size()==1,"Duplicate pickup must not duplicate custody");
    const auto* definition=catalog.findScenario(mission->scenarioId);
    const auto* claim=findScenarioStepDefinition(*definition,mission->claimStepId);
    recordScenarioEvent(*state,catalog,{claim->completionEvent,mission->scenarioId,mission->claimStepId,
        claim->eventOriginId,claim->eventTargetId,1,0});
    check(!performScenarioAction(*state,catalog,mission->scenarioId,mission->claimStepId,ScenarioActionKind::ClaimReward).applied,
        "Pickup must not allow artifact mission rewards");
    check(!solarMissionClaimed(*state,catalog,*mission) && trackedMissionView(*state,catalog).targetId=="earth",
        "Unsecured artifact must direct the player home without completing the mission");
    e.active=true;state->run.flight.active=true;state->run.flight.hullRemaining=100;
    check(loseExpedition(e,state->run.flight,solarSystemDefinition())==ExpeditionResult::Applied,
        "Artifact crash fixture must create a wreck");
    check(e.artifacts.front().owner==ArtifactCustody::Wreck,"Crash must move artifact custody to wreck");
    const auto wreckId=e.artifacts.front().wreckId;
    const auto encoded=serializeExpedition(e);
    auto restored=deserializeExpedition(encoded);
    check(restored && restored->artifacts.front().wreckId==wreckId,"Wreck artifact custody must round trip");
    e=*restored;e.location=e.wrecks.back().location;
    e.cargo.materials.common=60;
    check(salvageWreck(e,wreckId,solarSystemDefinition(),60)==ExpeditionResult::Applied &&
        e.artifacts.front().owner==ArtifactCustody::Ship,"Full ore hold must not prevent artifact salvage");
    e.location.bodyId="earth";e.location.siteId="earth.dock";state->run.flight.active=false;
    bankMissionArtifacts(*state,catalog);
    check(e.artifacts.front().owner==ArtifactCustody::Banked && !e.artifacts.front().completed,
        "Banking must wait for explicit hand-in");
    check(performScenarioAction(*state,catalog,mission->scenarioId,mission->claimStepId,ScenarioActionKind::ClaimReward).applied,
        "Banked artifact must allow explicit hand-in");
    check(solarMissionClaimed(*state,catalog,*mission),"Hand-in must complete the mission");
    const auto drones=state->meta.ownedDroneIds;
    check(!performScenarioAction(*state,catalog,mission->scenarioId,mission->claimStepId,ScenarioActionKind::ClaimReward).applied &&
        state->meta.ownedDroneIds==drones,"Repeated hand-in must not replay rewards");
    auto& mining=state->run.mining;
    mining.deliveredOreUnits=25;mining.missionOreUnits=20;e.cargo.materials={5,0,0};
    const auto label=miningPayloadOwnershipText(*state,catalog);
    check(label.find("RIG ")!=std::string::npos && label.find("DRONES ")!=std::string::npos &&
        label.find("SHIP ")!=std::string::npos,
        "Payload ownership must show rig, drone, and ship totals");
    check(miningPayloadContractText(*state,catalog).find("Delivered 25 · Mission 20 · Cargo 5/60")!=std::string::npos,
        "Payload must distinguish delivery, contract allocation, and current hold");
    e.cargo.materials={1,2,1};
    check(miningPayloadContractText(*state,catalog).find("Cargo 9/60")!=std::string::npos,
        "Cargo must retain weighted material mass");
    const auto save=deserializeSaveData(serializeSaveData(captureSaveData(*state)));
    check(save && save->mining.deliveredOreUnits==25 && save->mining.missionOreUnits==20,
        "Delivery history must survive saving independently of cargo spending");
    mining.deliveredOreUnits=mining.missionOreUnits=-1;
    check(miningPayloadContractText(*state,catalog).find("Delivered")==std::string::npos &&
        miningPayloadContractText(*state,catalog).find("Mission")==std::string::npos,
        "Unknown legacy delivery history must not be fabricated");
}

void persistentExpeditionTests()
{
    artifactBankingAndPayloadTests();
    straylightSequenceTests();
    using namespace rocket;
    campaignGuidanceTests();
    missionGuidanceTests();
    orbitalObjectiveSafetyTests();
    {
        const auto catalog = createDefaultContent();
        const auto& system = solarSystemDefinition();
        PersistentExpeditionState expedition;
        expedition.location={"solar","",CoordinateFrame::System,{}, {},0.0,""};
        expedition.course.targetBodyId="io";
        expedition.cruise.active=true;
        FlightRunState ship;
        ship.heading=1.5;
        ship.heat=.97;
        auto input=cruiseInput(expedition,ship,system,{});
        check(expedition.cruise.active && expedition.cruise.cooling && input.throttle==0 && input.enginesCut,
            "Enabling cruise while hot must cut engines immediately, not cancel cruise");
        ship.heat=.55;
        input=cruiseInput(expedition,ship,system,{});
        check(input.throttle==0 && input.steer!=0,"Cruise must coast through cooldown while tracking the target");
        const auto encoded=serializeExpedition(expedition);
        const auto restored=deserializeExpedition(encoded);
        check(restored && restored->cruise.cooling && restored->cruise.active,
            "Reload during cooldown must preserve the engine-off latch");
        expedition=*restored;
        check(cruiseInput(expedition,ship,system,{}).throttle==0,"Reload must not restart a warm engine");
        const auto legacy=deserializeExpedition(encoded.substr(0,encoded.rfind(" cruise1 ")));
        check(legacy && legacy->cruise.active,"Pre-cooldown saves must still load");
        ship.heat=tuning::launch::cruiseCoolingResume;
        input=cruiseInput(expedition,ship,system,{});
        check(input.throttle==1 && !input.enginesCut && !expedition.cruise.cooling && input.steer!=0,
            "Cooled cruise must resume thrust and course correction automatically");
        ship.heat=tuning::launch::cruiseCoolingStart;
        check(cruiseInput(expedition,ship,system,{}).throttle==0,"Cutoff threshold must be inclusive");
        check(cruiseInput(expedition,ship,system,{},false).throttle==1 && !expedition.cruise.cooling,
            "Heat-disabled flight must not inherit a stale cooldown");
        for(const FlightInput manual : {FlightInput{.2,0,false,false}, FlightInput{0,-.5,false,true}, FlightInput{0,0,true,false}}) {
            expedition.cruise={true,true};
            input=cruiseInput(expedition,ship,system,manual);
            check(!expedition.cruise.active && !expedition.cruise.cooling && input.steer==manual.steer &&
                input.throttle==manual.throttle && input.enginesCut==manual.enginesCut,
                "Manual steering, braking, and engine cut must override cooldown immediately");
        }
        // Real heat/fuel integration across frame rates and cooling upgrades.
        // A remote target isolates thermal behavior without hiding collisions.
        SystemDefinition openSpace;
        openSpace.id="thermal-test";
        SystemBodyDefinition target;
        target.id="target"; target.position={10000,20};
        openSpace.bodies.push_back(target);
        for(int rank : {0,1,3}) for(double dt : {.016,.08,.5}) {
            auto game=createNewGame(catalog,702);
            auto model=expeditionFlightModel(game,catalog);
            model.heatEnabled=true; model.coolingRank=rank;
            model.asteroidsEnabled=false; model.trajectoryPreview=true;
            auto flight=beginLaunchFlight(model,catalog.destinations[1]);
            flight.active=flight.physicalFlight=true;
            flight.mode=FlightMode::Travel;
            flight.positionX=0; flight.positionY=20;
            flight.velocityX=flight.velocityY=flight.heading=flight.heat=0;
            flight.fuelRemaining=10000;
            PersistentExpeditionState e;
            e.active=true; e.cruise.active=true;
            e.location={openSpace.id,"",CoordinateFrame::System,{}, {},0,""};
            e.course.targetBodyId="target";
            int pauses=0, resumes=0;
            for(int tick=0;tick<2400;++tick) {
                const bool cooling=e.cruise.cooling;
                const double fuel=flight.fuelRemaining, heat=flight.heat;
                const auto step=advanceExpeditionFlight(e,flight,model,catalog.destinations[1],openSpace,{},dt);
                check(!step.failed && flight.heat<tuning::launch::temperatureCriticalThreshold && flight.heatFailureSeconds==0,
                    "Automatic cruise must never enter critical heat across repeated burn/cool cycles");
                if(e.cruise.cooling) check(flight.selectedThrottle==0 && flight.fuelRemaining==fuel && flight.heat<=heat,
                    "Cooldown must really stop fuel use and cool the engine");
                pauses+=!cooling && e.cruise.cooling;
                resumes+=cooling && !e.cruise.cooling;
            }
            check(pauses>=2 && resumes>=2 && e.cruise.active && flight.positionX>0,
                "Cruise must repeatedly cool and resume forward progress without user input");
        }
    }
    {
        const auto catalog = createDefaultContent();
        auto state = createNewGame(catalog, 0x105CA);
        state.meta.unlockKeys.push_back(content::unlock::routeJupiter);
        state.run.expedition.travelInitialized = true;
        state.run.expedition.location.systemId = "solar";
        state.run.expedition.location.bodyId = "io";
        check(!unresolvedProgressionArtifactOpportunity(state,catalog,"jupiter","io"),
            "Fixture must precede Io mission commissioning");
        SurfaceLandingBuildRequest request;
        request.destinationId="jupiter"; request.bodyId="io"; request.siteSeed=319;
        const auto rolledZone=artifactSectorForBody(state,"solar","io");
        request.zoneId=rolledZone;
        auto objective = prepareSurfaceLanding(state,catalog,request);
        check(objective.valid, "Io objective site must prepare before mission commissioning");
        check(std::any_of(objective.miningTemplate.depthLayers.begin(),objective.miningTemplate.depthLayers.end(),
            [](const auto& layer){return layer.depthZone==2 && layer.artifact.present && layer.gate.active;}),
            "Io must physically contain its protected artifact before the first scan");
        check(prepareOrbitalSurvey(state,catalog,objective,0), "Correct slice scan must succeed");
        check(std::any_of(objective.surveyLayers.begin(),objective.surveyLayers.end(),
            [](const auto& layer){return layer.artifact && layer.depth==2;}),
            "Io scan must list its real depth-two artifact even with a shallow scanner");
        check(objective.surveyedDepth==0,"Objective localization must not grant deeper drilling reach");
        check(!orbitalArtifactSignal(state,catalog,&objective).detected,
            "An unfinished first scan must not reveal the signal");
        objective.surveyComplete=true;
        const auto exact = orbitalArtifactSignal(state,catalog,&objective);
        check(exact.detected && exact.localized && exact.depth>=2 && exact.depth<3,
            "A correctly guessed first slice must immediately localize the physical artifact");
        for (const auto& zone : planetLandingZones()) {
            if (zone.id==rolledZone) continue;
            request.zoneId=zone.id; request.allowScenarioObjectives=false;
            auto other=prepareSurfaceLanding(state,catalog,request);
            check(prepareOrbitalSurvey(state,catalog,other,2),"Every Io slice must be scannable");
            check(std::none_of(other.surveyLayers.begin(),other.surveyLayers.end(),
                [](const auto& layer){return layer.artifact;}),
                "Wrong slices must not invent an artifact or leak its depth");
            other.surveyComplete=true;
            const auto coarse=orbitalArtifactSignal(state,catalog,&other);
            check(coarse.detected && !coarse.localized && coarse.depth==0 &&
                coarse.bearing==planetLandingZone(rolledZone)->centerBearing,
                "A first scan anywhere must reveal only the persistent purple sector signal");
        }
        state.run.expedition.sites.push_back({"solar","io","io.beacon:"+rolledZone,
            objective.expeditionTemplate,objective.miningTemplate,static_cast<const OrbitalSiteProgress&>(objective)});
        const auto saved=deserializeExpedition(serializeExpedition(state.run.expedition));
        check(saved.has_value(),"Artifact scan progress must serialize");
        state.run.expedition=*saved;
        const auto reloaded=orbitalArtifactSignal(state,catalog);
        check(reloaded.localized && std::abs(reloaded.depth-exact.depth)<.000001 &&
            std::abs(reloaded.bearing-exact.bearing)<.000001,"Exact artifact localization must survive reload");
        state.run.expedition.location.bodyId="moon";
        check(!orbitalArtifactSignal(state,catalog).detected,"Io scans must not reveal artifacts on another moon");
        state.run.expedition.location.bodyId="io";
        auto legacy=state.run.expedition.sites.front();
        legacy.mining.artifact={};
        for(auto& layer:legacy.mining.depthLayers) layer.artifact={};
        legacy.mining.terrain.cells[0].material=MiningCellMaterial::Empty;
        request.zoneId=rolledZone; request.allowScenarioObjectives=true;
        auto repaired=restoreSurfaceLanding(state,catalog,request,legacy);
        check(repaired.valid && repaired.miningTemplate.terrain.cells[0].material==MiningCellMaterial::Empty &&
            orbitalArtifactSignal(state,catalog,&repaired).localized,
            "Previously scanned empty sites must repair their objective without resetting excavation");
        for(auto& layer:repaired.miningTemplate.depthLayers)
            if(layer.artifact.present) layer.artifact.state=MiningArtifactState::Delivered;
        auto delivered=legacy;
        delivered.mining=repaired.miningTemplate;
        const auto revisit=restoreSurfaceLanding(state,catalog,request,delivered);
        check(!orbitalArtifactSignal(state,catalog,&revisit).detected,
            "Recovery must remove the signal and must not respawn the objective on revisit");
        check(!unresolvedProgressionArtifactOpportunity(state,catalog,"jupiter","io"),
            "Scanning and repairing must not accept or claim the mission");
    }
    {
        const auto catalog = createDefaultContent();
        const auto& system = solarSystemDefinition();
        const auto* mars = systemBody(system,"mars");
        const auto* jupiter = systemBody(system,"jupiter");
        check(std::hypot(mars->position.x,mars->position.y)<solarBeltInnerRadius &&
            std::hypot(jupiter->position.x,jupiter->position.y)>solarBeltOuterRadius,
            "Main belt must sit between Mars and Jupiter");
        check(!crossesSolarAsteroidBelt({9,3},{13,3}) &&
            crossesSolarAsteroidBelt({22,0},{30,0}) &&
            crossesSolarAsteroidBelt({30,0},{22,0}) &&
            crossesSolarAsteroidBelt({25,0},{25,0}),
            "Belt entry must handle both directions, swept crossings, and reload inside");
        check(approachingSolarAsteroidBelt({19,0},{1,0}) &&
              approachingSolarAsteroidBelt({33,0},{-1,0}) &&
              approachingSolarAsteroidBelt({21,0},{.01,0}) &&
              !approachingSolarAsteroidBelt({19,0},{-1,0}) &&
              !approachingSolarAsteroidBelt({33,0},{1,0}),
            "Belt warning must give six seconds of approach notice from either side without warning distant departing ships");
        double smallest = 10, largest = 0;
        for (const auto& asteroid : solarAsteroidBelt()) {
            const double r = std::hypot(asteroid.position.x, asteroid.position.y);
            check(r - asteroid.radius > solarBeltInnerRadius && r + asteroid.radius < solarBeltOuterRadius,
                "Jittered rocks must stay inside the physical belt envelope");
            check(std::abs(asteroid.radius - .12*asteroid.scale) < 1e-9,
                "Asteroid collision radius must match visual scale");
            check(std::hypot(asteroid.position.x-mars->position.x, asteroid.position.y-mars->position.y) -
                    asteroid.radius - .075 > mars->influenceRadius + 8.0,
                "Mars departures must have a full widest-zoom screen of collision-free travel beyond orbit");
            smallest = std::min(smallest, asteroid.scale);
            largest = std::max(largest, asteroid.scale);
        }
        check(smallest < .7 && largest > 1.3 && solarAsteroidBelt().size() > 500 && solarAsteroidBelt().size() < 640,
            "Belt variation and density must remain outside the local Mars clearing");
        auto state = createNewGame(catalog,0xB317);
        auto model = expeditionFlightModel(state,catalog);
        model.heatEnabled = false;
        model.trajectoryPreview = true;
        const auto& destination = *catalog.findDestination("moon");
        const auto rock = solarAsteroidBelt().front();
        double unarmoredDamage = 0;
        for (int rank : {0,3}) {
            model.hullRank = rank;
            auto flight = beginLaunchFlight(model,destination);
            flight.mode = FlightMode::Travel;
            flight.positionX = rock.position.x-.5;
            flight.positionY = rock.position.y;
            flight.velocityX = 20; flight.velocityY = 0;
            SystemLocation location{"solar","",CoordinateFrame::System,{}, {},0,""};
            const auto impact = updateLaunchFlight(flight,model,destination,{},.02,nullptr,&system,&location);
            check(impact.crossedAsteroidBelt && impact.asteroidHit && flight.hullRemaining<flight.hullMaximum,
                "Swept world-space belt collisions must damage the ship even with legacy asteroids disabled");
            const double damage = flight.hullMaximum-flight.hullRemaining;
            if (rank == 0) unarmoredDamage = damage;
            else check(damage<unarmoredDamage,"Hull Plating must reduce belt collision damage");
        }
        model.hullRank = 0;
        auto predicted = beginLaunchFlight(model,destination);
        predicted.mode = FlightMode::Travel;
        predicted.positionX = rock.position.x-.5; predicted.positionY = rock.position.y;
        predicted.velocityX = .7; predicted.velocityY = 0;
        PersistentExpeditionState expedition;
        expedition.location = {"solar","",CoordinateFrame::System,{}, {},0,""};
        refreshExpeditionTrajectory(expedition,predicted,model,destination,system);
        check(predicted.predictedImpact,"Trajectory must warn about nonfatal asteroid impacts too");
    }
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
              objective.actionLabel == "Complete Mission",
            "The Moon reward action must require banking before mission completion");
        performScenarioAction(state,catalog,content::scenario::lunarProspector,"anomaly",ScenarioActionKind::ClaimReward);
        check(state.meta.equippedDroneIds == std::vector<std::string>{content::drone::miningDrone},
            "First artifact recovery must grant and equip the Prospector");
        check(std::none_of(state.incomingMessages.pending.begin(), state.incomingMessages.pending.end(),
                [](const auto& message) { return message.messageId == "prospector_unlocked"; }),
            "The claim card introduces the Prospector without a duplicate reward popup");
        performScenarioAction(state,catalog,content::scenario::lunarProspector,"anomaly",ScenarioActionKind::ClaimReward);
        check(state.meta.equippedDroneIds.size()==1,"Repeated recovery must not duplicate the Prospector");
        performScenarioAction(state,catalog,content::scenario::marsBayExpansion,"briefing",ScenarioActionKind::AcknowledgeBriefing);
        recordScenarioEvent(state,catalog,{ScenarioEventKind::SafeMaterialDelivered,
            content::scenario::marsBayExpansion,"delivery","mars","common",tuning::research::marsBayCommonOreGoal,0});
        recordScenarioEvent(state,catalog,{ScenarioEventKind::ArtifactRecovered,
            content::scenario::marsBayExpansion,"artifact","mars",content::protectedObjective::marsSignalArtifact,1,0});
        objective = scenarioObjectiveForDestination(state, catalog, content::destination::mars);
        check(objective.state == ScenarioStepState::ReadyToClaim &&
              objective.actionLabel == "Complete Mission",
            "The Mars reward action must require banking before mission completion");
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
            {dock.x + expeditionDockRadius, dock.y}, {earth->velocity.x + 1.001, earth->velocity.y}, 0, {}};
        restoreSystemLocation(expedition.location, flight);
        flight.active = flight.physicalFlight = true;
        flight.mode = FlightMode::Travel;
        check(expeditionDockInRange(expedition, flight, system, "earth"),
            "Dock alert and action range must include the shared radius boundary");
        check(!canDockExpedition(expedition, flight, system),
            "Being in range must not bypass the matched-speed docking requirement");
        expedition.location.velocity = earth->velocity;
        expedition.location.velocity.x += 1.0;
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
        auto state = createNewGame(catalog, 0xD0C7ULL);
        check(initializeLiveExpedition(state, catalog), "Docking fixture must initialize live travel");
        const auto& system = solarSystemDefinition();
        const auto* earth = systemBody(system, "earth");
        check(earth != nullptr, "Docking fixture requires Earth");
        const auto dock = systemDockPosition(*earth);
        auto& expedition = state.run.expedition;
        auto& flight = state.run.flight;
        expedition.active = true;
        expedition.departureCount = 1;
        expedition.location = {system.id, "", CoordinateFrame::System,
            {dock.x + service_dock::approachRadius * .90, dock.y},
            {earth->velocity.x - 1.6, earth->velocity.y}, 0.0, {}};
        restoreSystemLocation(expedition.location, flight);
        flight.active = flight.physicalFlight = true;
        flight.mode = FlightMode::Travel;
        flight.docking = {};
        const auto model = expeditionFlightModel(state, catalog);
        (void)advanceExpeditionFlight(expedition, flight, model, expeditionEnvironment(state, catalog), system, {}, .01);
        check(earthDockingActive(flight) && !expedition.cruise.active,
            "Earth approach must enter the local docking maneuver at any relative speed");
        check(std::abs(flight.docking.positionX - .90 * service_dock::localUnitsPerSystemUnit) < 1e-6 &&
              std::abs(flight.docking.velocityX + service_dock::entryMaxSpeed) < 1e-6,
            "Dock handoff must preserve distance while capping fast arrivals for precision control");
        const auto entrySystemPose = convertSystemFrame(expedition.location, CoordinateFrame::System, "", system);
        check(std::abs(entrySystemPose.position.x - dock.x - .90) < 1e-6 &&
              std::abs(entrySystemPose.velocity.x - earth->velocity.x + service_dock::entryMaxSpeed / service_dock::localUnitsPerSystemUnit) < 1e-6,
            "Dock coordinates must preserve position and round-trip the reduced entry velocity");
        // A typical .1-system-unit/s approach must still have several seconds
        // before the nose reaches the rails after the full camera handoff.
        auto reactionFlight = flight;
        auto reactionExpedition = expedition;
        reactionFlight.heading = 3.14159265358979323846;
        for (int frame = 0; frame < 25; ++frame)
            (void)advanceExpeditionFlight(reactionExpedition, reactionFlight, model,
                expeditionEnvironment(state, catalog), system, {}, .05);
        const double clearance = reactionFlight.docking.positionX - service_dock::mouthY - service_dock::shipLength * .5;
        check(clearance / std::abs(reactionFlight.docking.velocityX) > 4.0 &&
              !reactionFlight.docking.enteredMouth && !reactionFlight.docking.contactEpisode,
            "Completed zoom must leave approach room rather than placing the nose inside the catch rails");
        check(flight.docking.rotationLocked && flight.docking.dockAngularVelocity == 0.0 &&
              std::abs(flightWrappedAngleDelta(flight.docking.dockHeading,
                  std::atan2(flight.docking.positionY, flight.docking.positionX))) < 1e-6,
            "Dock must aim at the ship center once on entry and lock immediately");
        const double entryDockHeading = flight.docking.dockHeading;
        flight.docking.positionX = 1.2 * std::cos(entryDockHeading);
        flight.docking.positionY = 1.2 * std::sin(entryDockHeading);
        flight.docking.velocityX = flight.docking.velocityY = 0.0;
        flight.docking.rotationLocked = false;
        flight.docking.dockAngularVelocity = .3;
        for (int frame = 0; frame < 20; ++frame)
            (void)advanceExpeditionFlight(expedition, flight, model,
                expeditionEnvironment(state, catalog), system, {}, .05);
        check(flight.docking.dockHeading == entryDockHeading && flight.docking.rotationLocked &&
              flight.docking.dockAngularVelocity == 0.0,
            "Dock must not track a circling ship or resume legacy angular motion");

        constexpr double pi = 3.14159265358979323846;
        for (const double approachHeading : {0.0, pi, pi / 2.0, -pi / 2.0, pi / 4.0, -pi / 4.0,
                                             3.0 * pi / 4.0, -3.0 * pi / 4.0}) {
            for (const double steer : {-1.0, 1.0}) {
                flight.docking.dockHeading = approachHeading;
                flight.docking.positionX = 1.2 * std::cos(approachHeading);
                flight.docking.positionY = 1.2 * std::sin(approachHeading);
                flight.heading = approachHeading + pi;
                flight.angularVelocity = 0.0;
                const double beforeHeading = flight.heading;
                FlightInput input;
                input.steer = steer;
                (void)advanceExpeditionFlight(expedition, flight, model,
                    expeditionEnvironment(state, catalog), system, input, .05);
                check((flight.heading - beforeHeading) * steer < 0.0 && flight.angularVelocity * steer < 0.0,
                    "Docking A/Left must rotate counterclockwise and D/Right clockwise from every approach side");
                check(flight.docking.dockHeading == approachHeading,
                    "Steering must never rotate the dock");
            }
        }
        for (const double shipHeading : {-.01, .01}) {
            flight.docking.dockHeading = pi / 2.0;
            flight.docking.positionX = 0.0;
            flight.docking.positionY = 1.2;
            flight.heading = shipHeading;
            flight.angularVelocity = 0.0;
            FlightInput input;
            input.steer = 1.0;
            (void)advanceExpeditionFlight(expedition, flight, model,
                expeditionEnvironment(state, catalog), system, input, .05);
            check(flight.angularVelocity < 0.0,
                "Docking clockwise steering must stay consistent through horizontal");
        }

        for (const double shipHeading : {0.0, pi / 2.0, pi, -pi / 2.0, pi / 4.0}) {
            for (const double strafe : {-1.0, 1.0}) {
                flight.docking.dockHeading = pi / 2.0;
                flight.docking.positionX = 0.0;
                flight.docking.positionY = 1.2;
                flight.docking.velocityX = flight.docking.velocityY = 0.0;
                flight.heading = shipHeading;
                flight.angularVelocity = 0.0;
                FlightInput input;
                input.strafe = strafe;
                (void)advanceExpeditionFlight(expedition, flight, model,
                    expeditionEnvironment(state, catalog), system, input, .01);
                const double forwardX = std::cos(shipHeading), forwardY = std::sin(shipHeading);
                const double rightX = forwardY, rightY = -forwardX;
                const double lateral = flight.docking.velocityX * rightX + flight.docking.velocityY * rightY;
                const double axial = flight.docking.velocityX * forwardX + flight.docking.velocityY * forwardY;
                check(lateral * strafe > 0.0 && std::abs(axial) < 1e-9 &&
                      flight.selectedThrottle == 0.0 && flight.heading == shipHeading,
                    "Docking strafe must apply only sideways ship-relative thrust without reverse thrust or rotation");
            }
        }
        const double forwardX = std::cos(flight.heading), forwardY = std::sin(flight.heading);
        const double rightX = forwardY, rightY = -forwardX;
        flight.docking.velocityX = rightX * .7 + forwardX * .2;
        flight.docking.velocityY = rightY * .7 + forwardY * .2;
        FlightInput counterThrust;
        counterThrust.strafe = -1.0;
        (void)advanceExpeditionFlight(expedition, flight, model,
            expeditionEnvironment(state, catalog), system, counterThrust, .01);
        const double remainingLateral = flight.docking.velocityX * rightX + flight.docking.velocityY * rightY;
        const double remainingAxial = flight.docking.velocityX * forwardX + flight.docking.velocityY * forwardY;
        check(remainingLateral > 0.0 && remainingLateral < .7 && std::abs(remainingAxial - .2) < 1e-9,
            "Ship-relative strafe must counter lateral drift without instantly cancelling momentum or reversing thrust");

        flight.docking.positionX = service_dock::exitRadius * service_dock::localUnitsPerSystemUnit + .01;
        flight.docking.positionY = 0.0;
        flight.docking.velocityX = 0.0;
        flight.docking.velocityY = 0.0;
        (void)advanceExpeditionFlight(expedition, flight, model, expeditionEnvironment(state, catalog), system, {}, .01);
        check(flight.mode == FlightMode::Orbit && flight.docking.reentrySuppressed,
            "Leaving the outer docking radius must return to ordinary flight and suppress immediate re-entry");

        expedition.location = {system.id, "", CoordinateFrame::System,
            {dock.x + .25, dock.y}, earth->velocity, 0.0, {}};
        restoreSystemLocation(expedition.location, flight);
        flight.active = true;
        (void)advanceExpeditionFlight(expedition, flight, model, expeditionEnvironment(state, catalog), system, {}, .01);
        check(flight.mode != FlightMode::Docking,
            "A ship backing away from Earth must leave the approach before it can re-enter");

        expedition.location.position = {dock.x + service_dock::exitRadius + .01, dock.y};
        restoreSystemLocation(expedition.location, flight);
        (void)advanceExpeditionFlight(expedition, flight, model, expeditionEnvironment(state, catalog), system, {}, .01);
        check(!flight.docking.reentrySuppressed,
            "Outer-radius clearance must re-arm future Earth docking approaches");

        expedition.location = {system.id, "", CoordinateFrame::System,
            {dock.x + service_dock::approachRadius - .01, dock.y},
            earth->velocity, 0.0, {}};
        restoreSystemLocation(expedition.location, flight);
        flight.active = flight.physicalFlight = true;
        flight.mode = FlightMode::Travel;
        flight.docking = {};
        refreshExpeditionTrajectory(expedition, flight, model,
            expeditionEnvironment(state, catalog), system);
        check(!flight.predictedTrajectory.empty(),
            "Earth approach must retain a global trajectory up to the local handoff");
        const auto forecastEnd = flight.predictedTrajectory.back();
        check(std::hypot(forecastEnd.x - dock.x, forecastEnd.y - dock.y) >=
                service_dock::approachRadius - .08 &&
              flight.predictedTrajectory.size() < 100,
            "Global trajectory must stop at the dock approach boundary instead of forecasting through the local berth");

        flight.mode = FlightMode::Docking;
        flight.active = true;
        flight.docking = {};
        flight.docking.active = true;
        flight.docking.dockId = "earth";
        flight.docking.dockHeading = 1.5707963267948966;
        flight.docking.positionY = service_dock::captureCenterY;
        flight.docking.enteredMouth = true;
        flight.heading = flight.docking.dockHeading + 3.14159265358979323846;
        flight.docking.velocityX = flight.docking.velocityY = 0.0;
        bool captured = false;
        for (int frame = 0; frame < 65 && !captured; ++frame) {
            const auto step = advanceExpeditionFlight(expedition, flight, model,
                expeditionEnvironment(state, catalog), system, {}, .05);
            captured = step.dockCaptured;
        }
        check(captured && flight.docking.settlementReady && flight.mode == FlightMode::Orbit,
            "A slow, aligned nose-first berth must capture before dock settlement");

        flight.mode = FlightMode::Docking;
        flight.docking = {};
        flight.docking.active = true;
        flight.docking.dockId = "retired-dock";
        (void)advanceExpeditionFlight(expedition, flight, model,
            expeditionEnvironment(state, catalog), system, {}, .01);
        check(flight.mode == FlightMode::Orbit && flight.docking.reentrySuppressed,
            "An invalid saved dock reference must recover to ordinary flight without an approach loop");

        flight.mode = FlightMode::Docking;
        flight.docking.active = true;
        flight.docking.dockId = "earth";
        flight.docking.positionX = .12;
        flight.docking.positionY = .34;
        flight.docking.velocityX = -.03;
        flight.docking.velocityY = .04;
        flight.docking.dockAngularVelocity = .12;
        flight.docking.handoffStartX = .40;
        flight.docking.handoffStartY = 1.70;
        flight.docking.rotationLocked = false;
        const double savedDockHeading = flight.docking.dockHeading;
        const auto saved = deserializeSaveData(serializeSaveData(captureSaveData(state)));
        check(saved && saved->flight.mode == FlightMode::Docking && saved->flight.docking.active &&
              saved->flight.docking.dockId == "earth" && saved->flight.docking.rotationLocked &&
              saved->flight.docking.dockAngularVelocity == 0.0 &&
              std::abs(saved->flight.docking.dockHeading - savedDockHeading) < 1e-6 &&
              std::abs(saved->flight.docking.handoffStartY - 1.70) < 1e-6,
            "An interrupted Earth docking maneuver must retain its local state across save/load");

        flight.mode = FlightMode::Orbit;
        flight.docking.active = false;
        flight.docking.settlementReady = true;
        expedition.location = {system.id, "earth", CoordinateFrame::Body,
            {dock.x - earth->position.x, dock.y - earth->position.y}, {}, 0.0, earth->siteId};
        restoreSystemLocation(expedition.location, flight);
        flight.active = true;
        const auto settled = advanceExpeditionFlight(expedition, flight, model,
            expeditionEnvironment(state, catalog), system, {}, .01);
        check(settled.dockCaptured,
            "A save restored after physical capture must resume dock settlement without a second approach");
    }

    {
        const auto catalog = createDefaultContent();
        auto state = createNewGame(catalog, 0xD0C202ULL);
        check(initializeLiveExpedition(state, catalog), "Dock feedback fixture initializes");
        auto& flight = state.run.flight;
        auto& expedition = state.run.expedition;
        const auto model = expeditionFlightModel(state, catalog);
        const auto& system = solarSystemDefinition();
        const auto& destination = expeditionEnvironment(state, catalog);
        expedition.undockReady = false;
        const auto resetDock = [&] {
            flight.active = flight.physicalFlight = true;
            flight.hullRemaining = 100;
            flight.mode = FlightMode::Docking;
            flight.docking = {};
            flight.docking.active = true;
            flight.docking.dockId = "earth";
            flight.docking.positionY = service_dock::captureCenterY;
            flight.docking.rotationLocked = true;
            flight.heading = flight.docking.dockHeading + 3.14159265358979323846;
        };
        const auto step = [&](double dt) {
            return advanceExpeditionFlight(expedition, flight, model, destination, system, {}, dt);
        };
        check(service_dock::captureHalfDepth * 2.0 == service_dock::shipLength &&
              service_dock::captureHalfWidth == service_dock::hullRadius,
            "Docking target must use the ship hull's rectangular proportions");
        for (const double side : {-1.0, 1.0}) {
            resetDock();
            flight.docking.enteredMouth = true;
            flight.docking.positionX = side * service_dock::captureHalfWidth * 1.049;
            flight.docking.positionY = service_dock::captureCenterY + .08;
            flight.docking.captureSeconds = .49;
            check(step(.02).dockSecuringStarted,
                "Ship inside the rectangular berth including five-percent edge grace must secure");
            resetDock();
            flight.docking.enteredMouth = true;
            flight.docking.positionX = side * service_dock::captureHalfWidth * 1.051;
            flight.docking.captureSeconds = .49;
            check(!step(.02).dockSecuringStarted && flight.docking.captureSeconds == 0.0,
                "Outside five-percent berth grace must not capture");
        }
        resetDock();
        flight.docking.positionX = .25;
        flight.docking.velocityX = .01;
        check(step(.01).dockBump && flight.hullRemaining == 100,
            "Harmless docking contact still emits feedback");
        for (int i = 0; i < 8; ++i) {
            flight.docking.positionX = .25;
            check(!step(.02).dockBump, "Sustained contact cannot repeat bump feedback");
        }
        flight.docking.positionX = 0;
        flight.docking.velocityX = 0;
        step(.08); step(.08);
        flight.docking.positionX = .25;
        check(step(.01).dockBump, "A clear interval rearms bump feedback");
        const auto contactSave = deserializeSaveData(serializeSaveData(captureSaveData(state)));
        check(contactSave && contactSave->flight.docking.contactEpisode &&
            contactSave->flight.docking.bumpAge >= service_dock::bumpFeedbackSeconds,
            "Contact episode persists without replaying its transient effect");
        resetDock();
        flight.docking.positionX = .25;
        flight.docking.velocityX = 2;
        const auto hard = step(.01);
        check(hard.dockBump && hard.dockImpactDamaging && flight.hullRemaining < 100,
            "Hard contact produces stronger feedback and existing damage");
        resetDock();
        flight.hullRemaining = .1;
        flight.docking.positionX = .25;
        flight.docking.velocityX = 20;
        check(step(.01).failed, "Fatal docking contact retains destruction handling");

        resetDock();
        flight.docking.enteredMouth = true;
        flight.docking.captureSeconds = .49;
        check(step(.02).dockSecuringStarted, "Stable capture starts the securing sequence");
        flight.docking.securingStartX = flight.docking.positionX = .08;
        flight.docking.securingStartY = flight.docking.positionY = service_dock::captureCenterY + .06;
        int lockEvents = 0;
        for (int i = 0; i < 39; ++i) {
            const auto result = advanceExpeditionFlight(expedition, flight, model, destination, system,
                {1, 1, false, true, 1}, .05);
            lockEvents += result.dockClampLocked;
            const double progress = service_dock::clampProgress(flight.docking.securingSeconds);
            check(std::abs(flight.docking.positionX - .08 * (1.0 - progress)) < 1e-6 &&
                  std::abs(flight.docking.positionY - (service_dock::captureCenterY + .06 * (1.0 - progress))) < 1e-6,
                "Ship holds until arms move, then settles with clamp closure, including after reload");
            check(!result.dockCaptured && !flight.docking.settlementReady,
                "Dock services cannot open before two seconds");
            check(flight.selectedThrottle == 0 && flight.velocityX == 0 && flight.velocityY == 0,
                "Securing suppresses thrust and motion");
            if (i == 3 || i == 11 || i == 22 || i == 36) {
                const auto saved = deserializeSaveData(serializeSaveData(captureSaveData(state)));
                check(saved && std::abs(saved->flight.docking.securingSeconds - flight.docking.securingSeconds) < 1e-6,
                    "Every arrival phase preserves its elapsed time across save/load");
                flight = saved->flight;
            }
        }
        check(lockEvents == 1, "Clamp lock is emitted once across repeated saves");
        check(step(.05).dockCaptured && flight.docking.settlementReady,
            "Two-second arrival completes with exactly one settlement handoff");
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
        check(!flight.landing.gateArmed, "Departure must explicitly disarm a previously armed landing gate");
        check(!landingGateCanRearm(.56,1.24) && !landingGateCanRearm(.54,1.25) &&
            landingGateCanRearm(.56,1.25), "Landing requires both spatial clearance and a completed handoff");
        SaveData departureSave;
        departureSave.flight=flight;
        departureSave.flight.handoff.elapsed=.3;
        const auto departureReload=deserializeSaveData(serializeSaveData(departureSave));
        check(departureReload && !departureReload->flight.landing.gateArmed &&
            departureReload->flight.handoff.from==FlightMode::Landing &&
            std::abs(departureReload->flight.handoff.elapsed-.3)<1e-8,
            "Reload must retain the committed departure and landing lock");
        for (double throttle : {-1.0,0.0,1.0}) {
            auto departing=beginLaunchFlight(landingModel,moon);
            departing.mode=FlightMode::Landing;
            departing.landing.altitude=flight_landing::departureAltitude;
            departing.landing.verticalVelocity=8.0;
            departing.landing.departureActive=true;
            departing.landing.gateArmed=true;
            check(surfacePresentationProgress(departing,false)==1.0,
                "Ascent framing must hold until the departure threshold commits");
            departing.landing.verticalVelocity=-.01;
            check(surfacePresentationProgress(departing,false)==1.0,
                "Braking must not reverse surface framing");
            departing.landing.verticalVelocity=8.0;
            leaveLocalLanding(departing);
            departing.orbit.captured=true; // Even an eligible landing cannot bypass the departure lock.
            for (int frame=0;frame<24 && departing.active;++frame) {
                const auto step=updateLaunchFlight(departing,landingModel,moon,{1.0,throttle,false,true},.05);
                check(departing.mode!=FlightMode::Landing && step.landingZoneId.empty(),
                    "Braking or turning during departure must never restart landing");
            }
        }
        double previousFraming=1.0;
        for (int sample=0;sample<=100;++sample) {
            flight.handoff.elapsed=flight_landing::handoffSeconds*sample/100.0;
            const double framing=surfacePresentationProgress(flight,false);
            check(framing<=previousFraming && (sample>40 || framing==1.0),
                "Departure framing must hold during terrain fade, then move only toward orbit");
            previousFraming=framing;
        }
        auto reentry=beginLaunchFlight(landingModel,moon);
        reentry.mode=FlightMode::Orbit;
        reentry.orbit.captured=true;
        reentry.landing.gateArmed=false;
        reentry.handoff={FlightMode::Landing,FlightMode::Orbit,flight_landing::handoffSeconds,0,.344,0};
        const double gateBearing=planetLandingZones()[0].centerBearing;
        for (int pass=0;pass<8;++pass) {
            const double r=pass%2 ? .405 : .395;
            reentry.positionX=r*std::cos(gateBearing); reentry.positionY=r*std::sin(gateBearing);
            reentry.velocityX=-.5*std::cos(gateBearing); reentry.velocityY=-.5*std::sin(gateBearing);
            const auto step=updateLaunchFlight(reentry,landingModel,moon,{},.05);
            check(reentry.mode==FlightMode::Orbit && step.landingZoneId.empty() && !reentry.landing.gateArmed,
                "Repeated boundary crossings without outer clearance must stay in orbit");
        }
        reentry.positionX=.56*std::cos(gateBearing); reentry.positionY=.56*std::sin(gateBearing);
        reentry.velocityX=reentry.velocityY=0;
        updateLaunchFlight(reentry,landingModel,moon,{},.001);
        check(reentry.landing.gateArmed,"Clearing the outer boundary after the handoff must rearm landing");
        reentry.positionX=.401*std::cos(gateBearing); reentry.positionY=.401*std::sin(gateBearing);
        reentry.velocityX=-.5*std::cos(gateBearing); reentry.velocityY=-.5*std::sin(gateBearing);
        const auto entered=updateLaunchFlight(reentry,landingModel,moon,{},.05);
        check(reentry.mode==FlightMode::Landing && entered.landingZoneId==planetLandingZones()[0].id,
            "A fresh eligible inward crossing after outer clearance must permit landing");
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
        first.miningTemplate.artifact.present = true;
        check(first.valid && prepareOrbitalSurvey(state,catalog,first,1), "Wedge survey must prepare normally");
        check(!first.surveyLayers.empty() && first.surveyLayers.front().artifact,
            "A prepared artifact must appear in the orbital scan manifest");
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
                failed.run.expedition.decision.pendingId=="opening_retry" && failed.run.expedition.wrecks.empty() &&
                failed.screen == Screen::Flight && !failed.run.flight.active,
                "An opening failure offers retry without creating a wreck or a dock visit");
            const auto retryPose = failed.run.expedition.location;
            check(recoverExpedition(failed,solarSystemDefinition())==ExpeditionResult::AlreadyApplied &&
                failed.run.expedition.sites.empty() && failed.run.expedition.wrecks.empty() &&
                failed.run.expedition.location.position.x == retryPose.position.x,
                "Repeated opening loss callbacks must preserve the one retry without creating a saved site");
            const auto retrySave=deserializeSaveData(serializeSaveData(captureSaveData(failed)));
            check(retrySave.has_value(),"Pending opening retry remains saveable");
            restoreSaveData(failed,catalog,*retrySave);
            check(retryOpeningMission(failed,catalog)==ExpeditionResult::Applied && failed.run.expedition.active &&
                failed.run.flight.active && failed.screen == Screen::Flight &&
                failed.run.expedition.departureCount == 1 && failed.run.flight.velocityX == earthLaunchSpeed &&
                failed.run.flight.hullRemaining==failed.run.flight.hullMaximum &&
                failed.run.flight.fuelRemaining==failed.run.flight.fuelCapacity && failed.incomingMessages.pending.empty(),
                "The single retry action must relaunch directly with the original resources and acknowledged guidance");
            check(retryOpeningMission(failed,catalog)==ExpeditionResult::InvalidState,"Retry cannot grant resources twice");
            for (const auto variant : {"tips", "crater", "tips", "tips"}) {
                check(failed.run.flight.active,"Retry must already be in live flight without a second Launch screen");
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
        check(dockGuidance.nextAction.find("service dock") != std::string::npos &&
                dockGuidance.nextAction.find("automatically") != std::string::npos,
            "Earth waypoint instructions must explain the automatic docking maneuver");
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
    e.batteries[0].researchEarned = true;
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
          "Wreck salvage must include the 3-unit radius boundary");
    e.location.position.x += 0.002;
    restoreSystemLocation(e.location, flight);
    check(!canSalvageWreck(e, flight, system, 1),
          "Wreck salvage must remain unavailable just beyond 3 units");
    salvageSpeedBoundaryTests(e, flight, system);
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
    {
        auto secondLoss = e;
        auto secondFlight = flight;
        secondLoss.active = true;
        restoreSystemLocation(secondLoss.location, secondFlight);
        check(loseExpedition(secondLoss, secondFlight, system) == ExpeditionResult::Applied,
            "An unresolved recovered build must remain recoverable after a second ship loss");
        const auto savedAgain = deserializeExpedition(serializeExpedition(secondLoss));
        check(savedAgain.has_value(), "A second wreck with conflicting graft alternatives must survive reload");
        secondLoss = *savedAgain;
        secondLoss.location = secondLoss.wrecks.back().location;
        check(salvageWreck(secondLoss, secondLoss.wrecks.back().id, system) == ExpeditionResult::Applied &&
            secondLoss.progression.pendingGraftConflicts.size() == 1 &&
            secondLoss.progression.pendingRunUpgradeChoices == 3,
            "Wreck recovery must restore unresolved graft choices without duplicating earned choices");
    }
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

    {
        auto failure = createNewGame(catalog, 0xD1EULL);
        check(initializeLiveExpedition(failure, catalog), "Recovery guard fixture must initialize live travel");
        auto& expedition = failure.run.expedition;
        auto& ship = failure.run.flight;
        expedition.active = true;
        expedition.location = {"solar", "mars", CoordinateFrame::Body, {1.0, 0}, {.2, .1}, .4,
            "mars.beacon:zone_2"};
        restoreSystemLocation(expedition.location, ship);
        expedition.cruise.active = true;
        expedition.undockReady = true;
        expedition.cargo.materials.common = 8;
        expedition.progression.pendingRunUpgradeChoices = 2;
        expedition.progression.runRigUpgradeRanks = {{content::surfaceUpgrade::highTorqueMotor, 2}};
        failure.run.mining.geologySeed = 9123;
        ship.active = ship.physicalFlight = true;
        ship.phase = FlightPhase::Impact;
        ship.mode = FlightMode::Landing;
        ship.failureCause = LaunchFailureCause::ThermalRunaway;
        ship.heatFailureSeconds = 99;
        ship.fuelFailureSeconds = ship.courseFailureSeconds = 99;
        ship.hullRemaining = 0;
        ship.hullDamageTaken = 100;
        ship.selectedThrottle = ship.angularVelocity = ship.burnRatePerSecond = 1;
        ship.contactEpisode = ship.predictedImpact = true;
        ship.landing.siteCommitted = ship.landing.departureActive = true;
        ship.orbit.captured = true;
        failure.meta.unlockKeys.push_back(content::unlock::routeJupiter);
        const auto unlocks = failure.meta.unlockKeys;
        const auto lossesBefore = failure.meta.shipsLost;
        check(recoverExpedition(failure, solarSystemDefinition()) == ExpeditionResult::Applied &&
            failure.screen == Screen::Hangar && operationalHomeDocked(expedition),
            "Ordinary ship loss must recover directly to a real service dock");
        check(failure.meta.shipsLost == lossesBefore + 1 && failure.meta.unlockKeys == unlocks &&
            expedition.wrecks.size() == 1 && expedition.wrecks.front().cargo.materials.common == 8 &&
            expedition.wrecks.front().build.pendingRunUpgradeChoices == 2 &&
            expedition.wrecks.front().build.runRigUpgradeRanks.front().rank == 2,
            "Loss must record one replacement, preserve campaign unlocks and escrow cargo and the earned build");
        check(!ship.active && !expedition.undockReady && !expedition.cruise.active &&
            ship.phase == FlightPhase::Transfer && ship.failureCause == LaunchFailureCause::None &&
            ship.heatFailureSeconds == 0 && ship.fuelFailureSeconds == 0 && ship.courseFailureSeconds == 0 &&
            ship.selectedThrottle == 0 && ship.angularVelocity == 0 && ship.burnRatePerSecond == 0 &&
            !ship.contactEpisode && !ship.predictedImpact && !ship.landing.siteCommitted && !ship.orbit.captured &&
            ship.fuelRemaining == ship.fuelCapacity && ship.hullRemaining == ship.hullMaximum &&
            failure.run.mining.geologySeed == 0,
            "A replacement must clear failed-flight and surface runtime before the player resumes");
        check(expedition.sites.size() == 1 && expedition.sites.front().mining.geologySeed == 9123,
            "Death must preserve the departed terrain before clearing the live site");
        const auto stableReplacement = serializeSaveData(captureSaveData(failure));
        check(recoverExpedition(failure, solarSystemDefinition()) == ExpeditionResult::AlreadyApplied &&
            serializeSaveData(captureSaveData(failure)) == stableReplacement,
            "Repeated recovery or Abandon callbacks at the replacement dock cannot mutate state or duplicate wrecks");
        const auto dockLocation = expedition.location;
        advanceExpeditionFlight(expedition, ship, expeditionFlightModel(failure, catalog),
            expeditionEnvironment(failure, catalog), solarSystemDefinition(), {1, 1, false, true}, .25);
        check(expedition.location.siteId == dockLocation.siteId && ship.positionX == dockLocation.position.x &&
            ship.positionY == dockLocation.position.y && !ship.active,
            "Inactive docked or failure flight cannot advance or convert its frame before an explicit departure");
        check(departHome(failure, catalog) == ExpeditionResult::Applied &&
            departHome(failure, catalog) == ExpeditionResult::AlreadyApplied,
            "Service departure may arm once and must not restart on repeated clicks");
        expedition.undockReady = false;
        expedition.location.bodyId = "mars";
        expedition.location.siteId = "mars.dock";
        check(departHome(failure, catalog) == ExpeditionResult::NotDocked && !expedition.undockReady,
            "A remote body cannot enter a fake service departure through a dock-looking site id");
        expedition.location.bodyId = "straylight";
        expedition.location.siteId = "straylight.dock";
        expedition.active = true;
        expedition.straylightRevealed = true;
        check(departHome(failure, catalog) == ExpeditionResult::NotDocked &&
            departDock(expedition, ship) == ExpeditionResult::Applied,
            "The revealed derelict permits physical release without becoming a service Hangar");
    }

    {
        auto recalled = createNewGame(catalog, 0xCA11ULL);
        check(initializeLiveExpedition(recalled, catalog), "Live recall fixture must initialize travel");
        recalled.run.expedition.active = true;
        recalled.run.expedition.location = {"solar", "moon", CoordinateFrame::Body,
            {.5, 0}, {}, 0, "moon.beacon:zone_1"};
        SurfaceLandingBuildRequest request;
        request.destinationId = request.bodyId = "moon";
        request.siteSeed = 0xCA11ULL;
        request.landingOrdinal = 1;
        auto preparedSite = prepareSurfaceLanding(recalled, catalog, request);
        check(preparedSite.valid && commitPreparedSurfaceLanding(recalled, std::move(preparedSite), 5.0),
            "Live recall fixture requires a committed physical mining site");
        auto& mining = recalled.run.mining;
        auto& ship = recalled.run.flight;
        ship.landing.siteCommitted = true;
        ship.mode = FlightMode::Landing;
        ship.phase = FlightPhase::Landed;
        ship.fuelRemaining = 6;
        ship.hullRemaining = 82;
        mining.active = true;
        recalled.screen = Screen::Mining;
        mining.shipDepthZone = 0;
        check(prepareLandingLayers(recalled, catalog, mining, 1) && activateLandingLayer(mining, 1),
            "Live recall fixture must allow mining below the surviving ship");
        mining.rigDepthZone = 1;
        mining.droneX = 12.5; mining.droneY = 15.5;
        mining.operatorPresent = mining.rigDisabled = mining.failurePending = true;
        mining.operatorMode = MiningOperatorMode::Jetpack;
        mining.operatorIntegrity = mining.droneHealth = 0;
        mining.temporaryMaterials.common = mining.cargo = 3;
        mining.stowedMaterials.common = mining.stowedCargo = 5;
        recalled.run.expedition.cargo.materials.common = 5;
        recalled.run.expedition.progression.expeditionLevel = 4;
        recalled.run.expedition.progression.pendingRunUpgradeChoices = 2;
        recalled.run.expedition.progression.runRigUpgradeRanks = {{content::surfaceUpgrade::wideDrillHead, 2}};
        mining.artifact.present = true;
        mining.artifact.id = "recall_test_artifact";
        mining.artifact.state = MiningArtifactState::Loose;
        mining.artifact.x = 14.5; mining.artifact.y = 15.5;
        mining.artifact.tethered = true;
        const auto geologySeed = mining.geologySeed;
        const double fuelBefore = mining.rigFuel.current;
        const auto recalledResult = finishMiningRun(recalled, catalog, true);
        check(recalledResult.applied && recalled.screen == Screen::Mining && mining.active &&
            mining.depthZone == mining.shipDepthZone && miningAtReturnZone(mining) &&
            !mining.failurePending && !mining.rigDisabled && mining.operatorMode == MiningOperatorMode::Rig,
            "EVA death or live emergency recall must return directly to the surviving ship without legacy menus");
        check(ship.landing.siteCommitted && ship.fuelRemaining == 6 && ship.hullRemaining == 82 &&
            recalled.run.expedition.wrecks.empty() && recalled.meta.shipsLost == 0 &&
            recalled.run.expedition.progression.expeditionLevel == 4 &&
            recalled.run.expedition.progression.pendingRunUpgradeChoices == 2 &&
            recalled.run.expedition.cargo.materials.common == 5 && mining.stowedMaterials.common == 5 &&
            mining.temporaryMaterials.common == 0 && mining.rigFuel.current == fuelBefore && mining.geologySeed == geologySeed,
            "Local recovery must preserve the ship, fuel, banked payload, terrain and entire expedition build");
        const auto lostLayer = std::find_if(mining.depthLayers.begin(), mining.depthLayers.end(),
            [](const auto& layer) { return layer.depthZone == 1; });
        check(lostLayer != mining.depthLayers.end() && lostLayer->artifact.present &&
            lostLayer->artifact.id == "recall_test_artifact" && !lostLayer->artifact.tethered &&
            lostLayer->artifact.x == 14.5 && !lostLayer->looseObjects.empty(),
            "Recall must leave the dropped artifact and unbanked ore in the excavated layer for recovery");
        const auto stableRecall = serializeSaveData(captureSaveData(recalled));
        check(!finishMiningRun(recalled, catalog, true).applied &&
            serializeSaveData(captureSaveData(recalled)) == stableRecall,
            "A repeated recovery action at the ship must not reset actors or settle payload again");
        const auto recallSave = deserializeSaveData(stableRecall);
        check(recallSave.has_value(), "Direct ship recovery must remain saveable");
        restoreSaveData(recalled, catalog, *recallSave);
        check(recalled.screen == Screen::Mining && mining.active && miningAtReturnZone(mining) &&
            finishMiningRun(recalled, catalog, false).applied,
            "After recall and reload, the surviving ship must offer immediate physical departure");
    }

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
        const auto& departureSystem = solarSystemDefinition();
        const auto departureOrigin = convertSystemFrame(expedition.location, CoordinateFrame::System, "", departureSystem);
        const auto departureTarget = courseTargetLocation(expedition, departureSystem, expedition.course.targetBodyId);
        const double expectedDepartureHeading = std::atan2(
            departureTarget->position.y - departureOrigin.position.y,
            departureTarget->position.x - departureOrigin.position.x);
        check(departHome(journey, catalog) == ExpeditionResult::Applied, "Earth departure must enter live flight");
        check(expedition.undockReady && !ship.active && !expedition.active,"Departure waits attached to the dock");
        check(std::abs(flightWrappedAngleDelta(ship.heading, expectedDepartureHeading)) < 1e-9 &&
                  std::abs(flightWrappedAngleDelta(expedition.location.heading, expectedDepartureHeading)) < 1e-9,
              "Attached ship and Earth dock departure pose must face the selected waypoint");
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
        const auto dockApproach = systemDockPosition(*earth);
        check(pilotTo({dockApproach.x + service_dock::exitRadius + .01, dockApproach.y}),
            "Starter pilot fixture must be able to return to the outer Earth docking boundary");
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
        const auto redepartResult=departHome(journey,catalog);
        check(redepartResult==ExpeditionResult::Applied,"Banked expedition can depart again");
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
