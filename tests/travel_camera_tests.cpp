#include "render/TravelCameraPresentation.h"
#include "render/SceneComposer.h"
#include "core/FlightSystem.h"
#include "core/ExpeditionSystem.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace rocket;
namespace rocket {
struct SceneComposerTestAccess {
    static auto camera(const SceneComposer& c) { return c.flightCameraPresentation_.current; }
    static bool active(const SceneComposer& c) { return c.flightCameraPresentation_.travel.active; }
};
}
namespace {
void check(bool ok, const char* message) {
    if (!ok) { std::fprintf(stderr,"FAILED: %s\n",message); std::exit(1); }
}
bool near(double a,double b,double tolerance=1e-5) { return std::abs(a-b)<tolerance; }
RenderSnapshot fixture() {
    RenderSnapshot s;
    s.screen=Screen::Flight; s.launchPhysicalFlight=s.systemTravel=true;
    s.system.id=s.systemLocation.systemId="solar"; s.systemLocation.frame=CoordinateFrame::System;
    s.launchApproachBlend=s.launchLandingBlend=0;
    s.launchHeading=0; s.flightGuidance.targetId="test";
    s.flightGuidance.targetPosition={100,100};
    return s;
}
void screenVelocity(RenderSnapshot& s,double x,double y) {
    constexpr double angle=3.141592653589793/3;
    s.launchVelocityX=(std::cos(angle)*x+std::sin(angle)*y)/flight_geometry::velocityToMetersPerSecond;
    s.launchVelocityY=(-std::sin(angle)*x+std::cos(angle)*y)/flight_geometry::velocityToMetersPerSecond;
}
travel_camera::Offset shipPoint(SceneComposer& c,const RenderSnapshot& s) {
    const auto& p=c.compose(s);
    return {(p.flightPointer.shipX-p.logicalSceneClip.x)/p.logicalSceneClip.width,
        (p.flightPointer.shipY-p.logicalSceneClip.y)/p.logicalSceneClip.height};
}
travel_camera::Offset settle(SceneComposer& c,RenderSnapshot& s,int fps,double seconds) {
    auto p=shipPoint(c,s);
    for(int i=0;i<static_cast<int>(seconds*fps);++i) {
        s.animationTime+=1.0/fps;
        s.launchPositionX+=s.launchVelocityX/fps;
        s.launchPositionY+=s.launchVelocityY/fps;
        p=shipPoint(c,s);
    }
    return p;
}
void placementAndTiming() {
    for (auto size : {std::pair{1600,900},std::pair{1280,800},std::pair{800,1000}})
        for(auto velocity : {std::pair{20.,0.},std::pair{-20.,0.},std::pair{0.,20.},std::pair{0.,-20.},
                std::pair{20.,20.},std::pair{-20.,-20.}}) {
            SceneComposer c; c.setViewport({size.first,size.second,size.first,size.second,1});
            auto s=fixture(); screenVelocity(s,velocity.first,velocity.second);
            const auto initial=shipPoint(c,s);
            check(near(initial.x,.5)&&near(initial.y,.5),"Fresh travel starts centered");
            const auto early=settle(c,s,60,1.0);
            check(near(early.x,.5)&&near(early.y,.5),"Brief travel cannot acquire lead");
            const auto p=settle(c,s,60,18);
            const auto rect=resolveUiViewportLayout(size.first,size.second,uiSurfaceKindForScreen(Screen::Flight)).sceneRect;
            const auto target=travel_camera::targetOffset(velocity.first,velocity.second,rect.width,rect.height,30);
            check(near(p.x,.5+target.x,.0001)&&near(p.y,.5-target.y,.0001),"Rendered ship uses the golden ray in the scene viewport");
            const auto before=p;
            s.launchHeading+=2.5;
            s.flightGuidance.targetId="behind"; s.flightGuidance.targetPosition={-200,-200};
            const auto retarget=shipPoint(c,s);
            check(near(before.x,retarget.x)&&near(before.y,retarget.y),"Heading or waypoint cannot steal velocity framing");
            c.setPresentationTime(999);
            const auto paused=shipPoint(c,s);
            check(near(paused.x,retarget.x)&&near(paused.y,retarget.y),"Wall-clock time cannot advance paused lead");
        }
    double reference=0;
    for(int fps : {30,60,120}) {
        SceneComposer c; auto s=fixture(); screenVelocity(s,20,0);
        const auto p=settle(c,s,fps,6);
        check(near(p.x,.5-(.5-travel_camera::goldenInset)*(1-std::exp(-3.0)),.0001),"95 percent settle takes 4.5 seconds after acquisition");
        if(reference) check(near(reference,p.x,.00001),"Camera easing must be frame-rate independent");
        reference=p.x;
    }
}
void brakingAndReversal() {
    travel_camera::State state;
    for(int i=0;i<600;++i) state.update(1./60,1,0,20,true,1000,800,25);
    check(state.active&&state.offset.x<-.11,"Sustained travel acquires lead");
    for(int i=0;i<60;++i) state.update(1./60,1,0,4,true,1000,800,25);
    check(state.active,"3 to 5 m/s retains active lead");
    const auto before=state.offset.x;
    state.update(1./60,1,0,2,true,1000,800,25);
    check(!state.active&&state.offset.x>before&&state.offset.x<-.1,"Braking gently recenters, never snaps");
    for(int i=0;i<30;++i) state.update(1./60,-1,0,20,true,1000,800,25);
    check(!state.active&&state.offset.x<0,"Reversal must reacquire while easing toward center");
    for(int i=0;i<600;++i) state.update(1./60,-1,0,20,true,1000,800,25);
    check(state.active&&state.offset.x>.11,"Reversed momentum moves lead to the opposite side");
    const auto compact=travel_camera::targetOffset(1,0,100,80,30);
    check(compact.x==0,"Ship padding clamps an impossibly small viewport to center");
}
void frameAndDockContinuity(std::string dockId = "earth", int width=1600, int height=900) {
    auto c=std::make_unique<SceneComposer>(); auto s=fixture(); screenVelocity(s,20,0);
    c->setViewport({width,height,width,height,1});
    s.system=solarSystemDefinition(); s.system.bodies.clear(); // isolate camera frame conversion
    const auto* earth=systemBody(solarSystemDefinition(),dockId);
    s.system.bodies.push_back(*earth);
    const auto dock=systemDockPosition(*earth);
    s.launchPositionX=dock.x+1; s.launchPositionY=dock.y;
    settle(*c,s,60,10);
    const auto systemPoint=shipPoint(*c,s);
    s.systemLocation.frame=CoordinateFrame::Body; s.systemLocation.bodyId=dockId;
    s.launchPositionX-=earth->position.x; s.launchPositionY-=earth->position.y;
    s.launchVelocityX-=earth->velocity.x; s.launchVelocityY-=earth->velocity.y;
    s.flightGuidance.targetPosition.x-=earth->position.x;
    s.flightGuidance.targetPosition.y-=earth->position.y;
    const auto bodyPoint=shipPoint(*c,s);
    check(near(systemPoint.x,bodyPoint.x,.0001)&&near(systemPoint.y,bodyPoint.y,.0001),"Body-frame conversion preserves lead");
    s.launchDockId=dockId;
    s.launchDockingActive=true; s.launchDockHandoffProgress=0;
    s.launchPositionX=(s.launchPositionX+earth->position.x-dock.x)*service_dock::localUnitsPerSystemUnit;
    s.launchPositionY=(s.launchPositionY+earth->position.y-dock.y)*service_dock::localUnitsPerSystemUnit;
    s.launchDockHandoffX=s.launchPositionX; s.launchDockHandoffY=s.launchPositionY;
    const auto entry=shipPoint(*c,s);
    check(near(entry.x,bodyPoint.x,.0001)&&near(entry.y,bodyPoint.y,.0001),"Dock entry starts at actual displayed ship position");
    const auto source=SceneComposerTestAccess::camera(*c);
    for(int i=1;i<=75;++i) { s.animationTime+=1./60; s.launchDockHandoffProgress=i/75.; shipPoint(*c,s); }
    check(!SceneComposerTestAccess::active(*c),"Docking never acquires travel lead");
    const auto beforeAbort=shipPoint(*c,s);
    s.launchDockingActive=false; s.systemLocation.frame=CoordinateFrame::System; s.systemLocation.bodyId.clear();
    s.launchPositionX=dock.x+s.launchPositionX/service_dock::localUnitsPerSystemUnit;
    s.launchPositionY=dock.y+s.launchPositionY/service_dock::localUnitsPerSystemUnit;
    const auto abort=shipPoint(*c,s);
    check(near(abort.x,beforeAbort.x,.0001)&&near(abort.y,beforeAbort.y,.0001),"Dock abort rebases the outgoing camera without a cut");
    check(source[4]>0,"Converted docking camera retains positive scale");
    s.screen=Screen::Hangar; c->compose(s);
    s=fixture(); screenVelocity(s,20,0);
    check(near(shipPoint(*c,s).x,.5),"New session does not inherit old camera lead");
}
void planetApproachAndBeltZoom() {
    SceneComposer c; auto s=fixture(); s.system=solarSystemDefinition();
    const auto* mars=systemBody(s.system,"mars");
    s.flightGuidance.targetId="mars"; s.flightGuidance.targetPosition=mars->position;
    s.launchPositionX=mars->position.x+8; s.launchPositionY=mars->position.y;
    s.launchVelocityX=-.8; s.launchVelocityY=0;
    auto previous=shipPoint(c,s);
    for(int i=0;i<540;++i) {
        s.animationTime+=1./60; s.launchPositionX-=.8/60;
        const auto p=shipPoint(c,s);
        check(std::hypot(p.x-previous.x,p.y-previous.y)<.025,"Planet approach must not drop the travel offset abruptly");
        previous=p;
        if(i==500) {
            // Same pose on either side of the real system/body frame change.
            s.systemLocation.frame=CoordinateFrame::Body; s.systemLocation.bodyId="mars";
            s.launchPositionX-=mars->position.x; s.launchPositionY-=mars->position.y;
            s.flightGuidance.targetPosition={0,0};
            const auto local=shipPoint(c,s);
            check(near(local.x,p.x,.0001)&&near(local.y,p.y,.0001),"Approach framing is continuous across frame ownership");
        }
    }
    s.launchOrbitCaptured=true;
    const auto orbit=shipPoint(c,s);
    s.launchLandingLocalFrame=true; s.launchLandingBasisAngle=0;
    s.launchLandingBlend=0;
    const auto entry=shipPoint(c,s);
    check(near(entry.x,orbit.x)&&near(entry.y,orbit.y),"Landing handoff starts from the displayed orbit view");
    previous=entry;
    for(int i=1;i<=75;++i) {
        s.animationTime+=1./60; s.launchLandingBlend=i/75.;
        const auto p=shipPoint(c,s);
        check(std::hypot(p.x-previous.x,p.y-previous.y)<.04,"Landing camera remains continuous"); previous=p;
    }
    s.launchLandingLocalFrame=false;
    for(int i=74;i>=0;--i) {
        s.animationTime+=1./60; s.launchLandingBlend=i/75.;
        const auto p=shipPoint(c,s);
        check(std::hypot(p.x-previous.x,p.y-previous.y)<.04,"Departure camera remains continuous"); previous=p;
    }
    SceneComposer belt, centered;
    auto b=fixture(); b.system=solarSystemDefinition();
    b.launchPositionX=solarBeltInnerRadius-2; b.launchPositionY=0;
    screenVelocity(b,0,20); settle(belt,b,60,10);
    const auto lead=SceneComposerTestAccess::camera(belt);
    shipPoint(centered,b);
    check(near(lead[4],SceneComposerTestAccess::camera(centered)[4]),"Velocity framing does not alter asteroid preview zoom");
}
}
void boardedShipKeepsCloseUpScale()
{
    for (bool departing : {false,true}) for (const auto size : {std::pair{1600,900},std::pair{1280,800},std::pair{900,1200}}) {
        SceneComposer composer;
        composer.setViewport({size.first,size.second,size.first,size.second,1});
        auto s=fixture();
        s.surfaceArrivalPrepared=s.launchLandingLocalFrame=true;
        s.manualSurfaceDeparture=departing;
        s.launchLandingBlend=1;
        s.manualAscentCameraProgress=1;
        s.miningWidth=64; s.miningHeight=600; s.miningFrameHeight=40;
        s.miningReturnZoneX=s.launchLandingPadX=32;
        s.miningReturnZoneY=s.launchLandingPadY=12;
        std::vector<MiningCell> cells(s.miningWidth*s.miningHeight);
        for(int x=0;x<s.miningWidth;++x) cells[590*s.miningWidth+x].material=MiningCellMaterial::Bedrock;
        s.miningCells=cells;
        double firstScale=0;
        // Board, descend toward an abandoned rig, ascend again, and load deep
        // into a shaft. No nearby floor is available to anchor the camera.
        for(double row : {12.,40.,120.,400.,120.,40.}) {
            s.animationTime+=.1;
            s.launchLandingAltitude=(s.launchLandingPadY-row)*flight_landing::metersPerCell;
            const auto& packet=composer.compose(s);
            const auto& camera=packet.surfaceCamera;
            check(camera.active && camera.cellHeight>.02,"Manual shaft flight retains readable terrain and ship scale");
            if(firstScale) check(near(firstScale,camera.cellHeight),"Distant shaft floors must not shrink a boarded ship");
            firstScale=camera.cellHeight;
            check(packet.flightPointer.shipY>packet.logicalSceneClip.y &&
                packet.flightPointer.shipY<packet.logicalSceneClip.y+packet.logicalSceneClip.height,
                "The close-up follows the boarded ship through deep shafts");
        }
        // Missing floor uses the old pad, which may now be far above the ship.
        for(auto& cell:cells) cell.material=MiningCellMaterial::Empty;
        s.launchLandingAltitude=-1200;
        const auto& packet=composer.compose(s);
        check(packet.surfaceCamera.cellHeight>=firstScale,"An empty shaft cannot collapse the close-up");
        check(packet.flightPointer.shipY>packet.logicalSceneClip.y &&
            packet.flightPointer.shipY<packet.logicalSceneClip.y+packet.logicalSceneClip.height,
            "Old parked pad cannot leave the descending ship offscreen");
    }
}

int main() {
    boardedShipKeepsCloseUpScale();
    placementAndTiming(); brakingAndReversal(); frameAndDockContinuity(); frameAndDockContinuity("straylight");
    frameAndDockContinuity("straylight",800,600); planetApproachAndBeltZoom();
    std::puts("Travel camera: placement, timing, momentum, pause, frame and docking regressions passed");
}
