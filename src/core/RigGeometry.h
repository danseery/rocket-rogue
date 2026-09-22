#pragma once

#include "core/GameTypes.h"
#include "core/Tuning.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace rocket::rig_geometry {
// World-cell dimensions fitted to the opaque body and drill artwork. Antennae,
// exhaust and the scanner ring are deliberately outside the physical body.
inline constexpr double spriteSize = 3.25;
inline constexpr double bodyRadius = 1.05;
inline constexpr double drillLength = tuning::mining::drillRangeCells;
inline constexpr double drillSpriteLength = drillLength / .94;
inline constexpr double drillMount = spriteSize * .18;
inline constexpr double drillBase = drillMount + drillSpriteLength * .04;
inline constexpr double drillTip = drillBase + drillLength;
inline constexpr double drillHalfWidth = drillSpriteLength * .88 * .25;
inline constexpr double skin = .00001;
struct Point { double x = 0, y = 0; };
struct Contact { int x = 0, y = 0; double depth = 0; Point normal; bool passage = false; };
struct Profile {
    double headWidthScale = 1.0;
    double sideCutterReach = 0.0;
    double minimumY = 0.0;
    const MiningTerrain* above = nullptr;
    const MiningTerrain* below = nullptr;
};
inline Profile surfaceProfile(const MiningRunState& mining, double headWidthScale, double sideCutterReach) {
    const double padY = mining.surfaceOriginBound ? mining.surfacePadY : mining.returnZoneY;
    const double ceiling = mining.depthZone == 0
        ? std::min(0.0, padY - tuning::mining::returnZoneCenterHeightCells - tuning::mining::returnZoneRadiusCells)
        : 0.0;
    Profile profile{headWidthScale, sideCutterReach, ceiling};
    if (mining.surfaceOriginBound) for (const auto& layer : mining.depthLayers) {
        if (layer.depthZone == mining.depthZone - 1) profile.above = &layer.terrain;
        if (layer.depthZone == mining.depthZone + 1) profile.below = &layer.terrain;
    }
    return profile;
}
// Coordinates remain relative to the active layer, including contacts across
// either seam. Missing neighbors and the actual world boundaries stay solid.
inline const MiningCell* cellAt(const MiningTerrain& terrain, int x, int y, Profile profile = {}) {
    const MiningTerrain* source = &terrain;
    if (y < 0 && profile.above) { source = profile.above; y += source->height; }
    else if (y >= terrain.height && profile.below) { source = profile.below; y -= terrain.height; }
    if (x < 0 || x >= source->width || y < 0 || y >= source->height) return nullptr;
    const auto index = static_cast<std::size_t>(y * source->width + x);
    return index < source->cells.size() ? &source->cells[index] : nullptr;
}
inline double effectiveHalfWidth(Profile profile) {
    return drillHalfWidth * std::max(1.0, profile.headWidthScale) +
        std::max(0.0, profile.sideCutterReach);
}
inline std::array<Point, 3> triangle(double x, double y, double dx, double dy, Profile profile = {}) {
    const double len = std::hypot(dx,dy);
    if (len < .0001) { dx=0; dy=1; } else { dx/=len; dy/=len; }
    const double halfWidth = effectiveHalfWidth(profile);
    return {{{x+dx*drillBase-dy*halfWidth,y+dy*drillBase+dx*halfWidth},
        {x+dx*drillBase+dy*halfWidth,y+dy*drillBase-dx*halfWidth},
        {x+dx*drillTip,y+dy*drillTip}}};
}
inline Contact triangleContact(const std::array<Point,3>& p, int x, int y, double margin=0) {
    Contact hit{x,y,1e9,{}};
    std::array<Point,5> axes{{{1,0},{0,1}}};
    for (int i=0;i<3;++i) {
        const auto a=p[i], b=p[(i+1)%3]; const double len=std::hypot(b.x-a.x,b.y-a.y);
        axes[i+2]={(a.y-b.y)/len,(b.x-a.x)/len};
    }
    for (auto axis:axes) {
        double low=1e9, high=-1e9;
        for (auto v:p) { const double q=v.x*axis.x+v.y*axis.y; low=std::min(low,q); high=std::max(high,q); }
        const double center=(x+.5)*axis.x+(y+.5)*axis.y;
        const double extent=(std::abs(axis.x)+std::abs(axis.y))*(.5+margin);
        const double negative=high-(center-extent), positive=center+extent-low;
        if (negative<=skin || positive<=skin) return {x,y,0,{}};
        if (std::min(negative,positive)<hit.depth) {
            hit.depth=std::min(negative,positive);
            const double sign=negative<positive ? -1.0:1.0;
            hit.normal={axis.x*sign,axis.y*sign};
        }
    }
    return hit;
}
inline Contact circleContact(double cx,double cy,int x,int y) {
    const double px=std::clamp(cx,static_cast<double>(x),x+1.0);
    const double py=std::clamp(cy,static_cast<double>(y),y+1.0);
    const double dx=cx-px,dy=cy-py, distance=std::hypot(dx,dy);
    if (distance>=bodyRadius-skin) return {x,y,0,{}};
    if (distance>1e-8) return {x,y,bodyRadius-distance,{dx/distance,dy/distance}};
    const std::array<double,4> edges{{cx-x,x+1.0-cx,cy-y,y+1.0-cy}};
    const int i=static_cast<int>(std::min_element(edges.begin(),edges.end())-edges.begin());
    const std::array<Point,4> normals{{{-1,0},{1,0},{0,-1},{0,1}}};
    return {x,y,bodyRadius+edges[i],normals[i]};
}
inline std::vector<Contact> contacts(const MiningTerrain& terrain,double x,double y,double dx,double dy, Profile profile = {}) {
    const auto bit=triangle(x,y,dx,dy,profile);
    double left=x-bodyRadius,right=x+bodyRadius,top=y-bodyRadius,bottom=y+bodyRadius;
    for(auto p:bit) { left=std::min(left,p.x);right=std::max(right,p.x);top=std::min(top,p.y);bottom=std::max(bottom,p.y); }
    std::vector<Contact> result;
    // Surface airspace extends above the finite terrain grid. Keep an exact
    // ceiling plane, including the drill tip, rather than an invisible row-zero wall.
    if (profile.minimumY < 0.0 && top < profile.minimumY - skin)
        result.push_back({static_cast<int>(std::floor(x)), static_cast<int>(std::floor(profile.minimumY)) - 1,
            profile.minimumY - top, {0, 1}});
    for(int cy=static_cast<int>(std::floor(top));cy<=static_cast<int>(std::floor(bottom));++cy)
        for(int cx=static_cast<int>(std::floor(left));cx<=static_cast<int>(std::floor(right));++cx) {
            if (profile.minimumY < 0.0 && cy < 0 && cx >= 0 && cx < terrain.width) continue;
            const MiningCell* cell=cellAt(terrain,cx,cy,profile);
            if(cell && cell->material==MiningCellMaterial::Empty && !cell->suitOnlyPassage) continue;
            auto body=circleContact(x,y,cx,cy), drill=triangleContact(bit,cx,cy);
            auto hit=body.depth>drill.depth ? body:drill;
            if(hit.depth>skin) {hit.passage=cell && cell->suitOnlyPassage;result.push_back(hit);}
        }
    return result;
}
// Invalid legacy poses may escape, but cannot acquire new overlapping cells or
// deepen an existing contact. Valid poses must remain entirely clear.
inline bool improves(const std::vector<Contact>& before,const std::vector<Contact>& after) {
    if(after.empty()) return true;
    double oldDepth=0,newDepth=0;
    for(auto c:before) oldDepth+=c.depth;
    for(auto c:after) {
        auto old=std::find_if(before.begin(),before.end(),[&](auto p){return p.x==c.x && p.y==c.y;});
        if(old==before.end() || c.depth>old->depth+1e-7) return false;
        newDepth+=c.depth;
    }
    return newDepth<oldDepth-1e-8;
}
struct Sweep { double fraction=1; Contact contact; };
inline Sweep sweep(const MiningTerrain& terrain,double x,double y,double angle,
    double targetX,double targetY,double targetAngle, Profile profile = {}) {
    const double rotation=std::remainder(targetAngle-angle,6.283185307179586);
    const double rotationRadius = std::max(drillTip, effectiveHalfWidth(profile));
    const int steps=std::max(1,static_cast<int>(std::ceil(
        (std::hypot(targetX-x,targetY-y)+std::abs(rotation)*rotationRadius)/.04)));
    auto at=[&](double t){return contacts(terrain,std::lerp(x,targetX,t),std::lerp(y,targetY,t),
        std::cos(angle+rotation*t),std::sin(angle+rotation*t),profile);};
    auto previous=at(0); double accepted=0;
    for(int i=1;i<=steps;++i) {
        const double t=static_cast<double>(i)/steps;
        auto next=at(t);
        if(next.empty() || improves(previous,next)) {previous=std::move(next);accepted=t;continue;}
        double lo=accepted,hi=t;
        for(int n=0;n<12;++n) {
            const double mid=(lo+hi)*.5;auto probe=at(mid);
            if(probe.empty() || improves(previous,probe)) lo=mid; else hi=mid;
        }
        const auto strongest=std::max_element(next.begin(),next.end(),[](auto a,auto b){return a.depth<b.depth;});
        return {lo,strongest==next.end()?Contact{}:*strongest};
    }
    return {};
}
inline void recoverOverlap(const MiningTerrain& terrain,double& x,double& y,double angle, Profile profile = {}) {
    if(contacts(terrain,x,y,std::cos(angle),std::sin(angle),profile).empty()) return;
    const double recoveryRadius = 2.0 * std::max(bodyRadius, effectiveHalfWidth(profile));
    for(double radius=.05;radius<=recoveryRadius+.001;radius+=.05)
        for(int i=0;i<64;++i) {
            const double a=i*6.283185307179586/64;
            const double tx=x+std::cos(a)*radius,ty=y+std::sin(a)*radius;
            if(!contacts(terrain,tx,ty,std::cos(angle),std::sin(angle),profile).empty()) continue;
            if(sweep(terrain,x,y,angle,tx,ty,angle,profile).fraction>=1.0) {x=tx;y=ty;return;}
        }
}
}
