#include "core/SystemContent.h"
#include "core/GameTypes.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rocket
{
const std::vector<SystemAsteroid>& solarAsteroidBelt()
{
    static const auto rocks = [] {
        std::vector<SystemAsteroid> result;
        const auto* mars = systemBody(solarSystemDefinition(), "mars");
        // At the widest normal travel scale (.25), a screen spans eight
        // world units vertically. Keep that much real clearance beyond
        // Mars's orbital region, including rock and ship collision radii.
        const double marsClearance = mars ? mars->influenceRadius + 8.0 : 0.0;
        constexpr int perRing = 160;
        // Fixed hash: irregular, but identical for rendering, collision and saves.
        const auto jitter = [](std::uint32_t seed) {
            seed ^= seed >> 16; seed *= 0x7feb352dU;
            seed ^= seed >> 15; seed *= 0x846ca68bU; seed ^= seed >> 16;
            return static_cast<double>(seed & 0xffffU) / 65535.0;
        };
        for (int row = 0; row < 4; ++row) for (int index = 0; index < perRing; ++index) {
            const auto seed = static_cast<std::uint32_t>(row * perRing + index + 1);
            const double angle = (index + row*.37 + .70*(jitter(seed*3)-.5))*6.283185307179586/perRing;
            const double radius = 24.35 + row*.85 + .60*(jitter(seed*3+1)-.5);
            const double scale = .65 + .70*jitter(seed*3+2);
            const SystemVector position {radius*std::cos(angle),radius*std::sin(angle)};
            if (mars && std::hypot(position.x-mars->position.x, position.y-mars->position.y) <=
                    marsClearance + .12*scale + .075) continue;
            result.push_back({position, .12*scale, scale});
        }
        return result;
    }();
    return rocks;
}

bool crossesSolarAsteroidBelt(SystemVector from, SystemVector to)
{
    const double dx = to.x-from.x, dy = to.y-from.y;
    const double lengthSquared = dx*dx+dy*dy;
    const double t = lengthSquared > 1e-12
        ? std::clamp(-(from.x*dx+from.y*dy)/lengthSquared,0.0,1.0) : 0.0;
    const double nearest = std::hypot(from.x+dx*t,from.y+dy*t);
    const double farthest = std::max(std::hypot(from.x,from.y),std::hypot(to.x,to.y));
    return nearest <= solarBeltOuterRadius && farthest >= solarBeltInnerRadius;
}

bool approachingSolarAsteroidBelt(SystemVector position, SystemVector velocity)
{
    // Six seconds of coasting lookahead, plus a spatial buffer at low speed.
    const double radius = std::hypot(position.x, position.y);
    return (radius >= solarBeltInnerRadius - 3.0 && radius <= solarBeltOuterRadius + 3.0) ||
        crossesSolarAsteroidBelt(position, {position.x + velocity.x * 6.0, position.y + velocity.y * 6.0});
}

double systemBodyApproachBlend(const SystemBodyDefinition &body, double radius)
{
    const double nearRadius = std::max(.52, body.radius * 2.0);
    const double farRadius = std::max(nearRadius + .9, body.influenceRadius * 1.5);
    const double t = std::clamp((farRadius-radius)/(farRadius-nearRadius), 0.0, 1.0);
    return t*t*t*(t*(t*6.0-15.0)+10.0);
}

double systemBodyDisplayRadius(const SystemBodyDefinition &body)
{
    if (body.displayRadius > 0.0) return body.displayRadius;
    return body.kind == SystemBodyKind::Star ? body.radius : body.radius * 1.25;
}

double systemFlightTimeScale(const SystemDefinition &system, SystemVector position)
{
    double scale = 1.0;
    for (const auto &body : system.bodies) {
        if (body.influenceRadius <= 0.0) continue;
        const double radius = std::hypot(position.x-body.position.x, position.y-body.position.y);
        if (body.id == "earth" || body.kind == SystemBodyKind::Star || body.kind == SystemBodyKind::Station) {
            // Preserve the authored opening and non-orbit encounters.
            const double start = body.id == "earth" ? .60 : 1.0;
            const double t = std::clamp((radius/body.influenceRadius-start)/.50, 0.0, 1.0);
            const double blend = t*t*t*(t*(t*6.0-15.0)+10.0);
            scale = std::min(scale, std::lerp(.4, 1.0, blend));
        } else {
            // Camera scale blends geometrically. Release the close-range clock
            // on that same envelope, rather than accelerating after zoom-out.
            scale = std::min(scale, std::pow(.4, systemBodyApproachBlend(body, radius)));
        }
    }
    return scale;
}

const SystemDefinition &solarSystemDefinition()
{
    // Coordinates are authored in orbit units. Parentage is descriptive; the
    // transforms below are system-space transforms, including for moons.
    static const SystemDefinition solar = [] {
    SystemDefinition result{
        "solar",
        {
            {"sun", "", "Sun", SystemBodyKind::Star, {-3, -1}, {}, 0.9, 2.2, 4, "", "", "Fatal solar impact"},
            {"mercury",
             "sun",
             "Mercury",
             SystemBodyKind::Terrestrial,
             {4.5, -3},
             {},
             .16,
             1.42,
             1,
             "mercury.surface",
             "Optional exploration",
             "Solar heat"},
            {"venus",
             "sun",
             "Venus",
             SystemBodyKind::Terrestrial,
             {-6, -4.5},
             {},
             .16,
             1.42,
             1,
             "venus.surface",
             "Optional exploration",
             "Heat"},
            {"earth",
             "sun",
             "Earth",
             SystemBodyKind::Terrestrial,
             {9, 3},
             {},
             .45,
             1.42,
             1,
             "earth.dock",
             "Service ship and secure cargo",
             "Surface impact",
             true},
            {"moon",
             "earth",
             "Moon",
             SystemBodyKind::Moon,
             {13, 3},
             {},
             .16,
             1.42,
             1,
             "moon.beacon",
             "Lunar contract and beacon battery",
             "Surface impact"},
            {"mars",
             "sun",
             "Mars",
             SystemBodyKind::Terrestrial,
             {21, -4.5},
             {},
             .16,
             1.42,
             1,
             "mars.beacon",
             "Mars contract and beacon battery",
             "Surface impact"},
            {"jupiter",
             "sun",
             "Jupiter",
             SystemBodyKind::Giant,
             {30, 9},
             {},
             .45,
             1.8,
             2.5,
             "",
             "Gravity assist",
             "Fatal atmospheric entry"},
            {"io",
             "jupiter",
             "Io",
             SystemBodyKind::Moon,
             {34, 9},
             {},
             .16,
             1.42,
             1,
             "io.beacon",
             "Hazard Drone and beacon battery",
             "Thermal terrain"},
            {"saturn",
             "sun",
             "Saturn",
             SystemBodyKind::Giant,
             {-33, 18},
             {},
             .40,
             1.8,
             2,
             "",
             "Titan beacon expedition",
             "Ring debris"},
            {"titan",
             "saturn",
             "Titan",
             SystemBodyKind::Moon,
             {-29, 18},
             {},
             .16,
             1.42,
             1,
             "saturn.beacon",
             "Saturn beacon battery",
             "Cold terrain"},
            {"uranus",
             "sun",
             "Uranus",
             SystemBodyKind::Giant,
             {-40.5, -21},
             {},
             .35,
             1.8,
             1.8,
             "",
             "Titania beacon expedition",
             "Fatal atmospheric entry"},
            {"titania",
             "uranus",
             "Titania",
             SystemBodyKind::Moon,
             {-36.5, -21},
             {},
             .16,
             1.42,
             1,
             "uranus.beacon",
             "Uranus beacon battery",
             "Long return leg"},
            {"neptune",
             "sun",
             "Neptune",
             SystemBodyKind::Giant,
             {40.5, -30},
             {},
             .35,
             1.8,
             1.8,
             "",
             "Triton beacon expedition",
             "Fatal atmospheric entry"},
            {"triton",
             "neptune",
             "Triton",
             SystemBodyKind::Moon,
             {44.5, -30},
             {},
             .16,
             1.42,
             1,
             "neptune.beacon",
             "Neptune beacon battery",
             "Long return leg"},
            {"straylight",
             "sun",
             "Straylight",
             SystemBodyKind::Station,
             {40.5 + 18.0 * 40.5 / std::hypot(40.5, 30.0), -30.0 - 18.0 * 30.0 / std::hypot(40.5, 30.0)},
             {},
             .10,
             1.42,
             0,
             "straylight.dock",
             "Install six beacon batteries",
             "Derelict: no servicing",
             true},
        }};
    for (auto &body : result.bodies) {
        // Deliberately compressed relative sizes: recognizable and playful,
        // while restoring the visual hierarchy of star, giants, terrestrials,
        // and moons. Physical radii continue to own gameplay.
        if (body.id == "sun") body.displayRadius = 1.35;
        else if (body.id == "jupiter") body.displayRadius = .72;
        body.dockOffset = {body.id == "earth" ? body.influenceRadius * 1.1 + .40 : body.radius + .95, 0};
        if (body.id == "straylight") {
            body.displayRadius = 2.5;
            body.dockOffset = {1.9, -0.6};
        }
        if (body.id == "earth") {
            // Keep the service berth clear of the opening Earth-to-Moon lane.
            constexpr double angle = 0.7853981633974483;
            const double radius = body.dockOffset.x;
            body.dockOffset = {radius * std::cos(angle), radius * std::sin(angle)};
        }
        body.environmentId = body.id;
        if (body.id == "io") body.environmentId = "jupiter";
        if (body.id == "titan") body.environmentId = "saturn";
        if (body.id == "titania") body.environmentId = "uranus";
        if (body.id == "triton") body.environmentId = "neptune";
        // Optional bodies reuse existing geology; spatial identity stays distinct.
        if (body.id == "mercury") body.environmentId = "moon";
        if (body.id == "venus") body.environmentId = "mars";
        if (body.id == "mercury" || body.id == "venus") body.authoredObjectives = true;
    }
    return result;
    }();
    return solar;
}
const SystemBodyDefinition *bodyForEnvironment(const SystemDefinition &system, std::string_view id)
{
    for (const auto &body : system.bodies)
        if (body.environmentId == id && !body.siteId.empty() && !body.dock &&
            body.id != "mercury" && body.id != "venus") return &body;
    return systemBody(system, id);
}
SystemVector earthLaunchPosition()
{
    return {.63, 0};
}
SystemVector systemDockPosition(const SystemBodyDefinition &body)
{
    return {body.position.x + body.dockOffset.x, body.position.y + body.dockOffset.y};
}
SystemVector systemNavigationPosition(const SystemBodyDefinition &body)
{
    return body.dock ? systemDockPosition(body) : body.position;
}
double systemBodyGravityAcceleration(const SystemBodyDefinition &body, double radius)
{
    if (body.influenceRadius <= 0 || radius >= body.influenceRadius * 1.1) return 0;
    const double r = std::max(.0001, radius);
    const double t = std::clamp((r / body.influenceRadius - 1.0) / .1, 0.0, 1.0);
    const double fade = 1.0 - t*t*t*(t*(t*6.0-15.0)+10.0);
    return std::min(.52, .095/(r*r)) * body.gravityScale * fade;
}
const SystemBodyDefinition *systemBody(const SystemDefinition &system, std::string_view id)
{
    const auto found = std::find_if(system.bodies.begin(), system.bodies.end(),
                                    [&](const auto &body) { return body.id == id; });
    return found == system.bodies.end() ? nullptr : &*found;
}
SystemDefinition systemDefinitionForRoster(const PostSolarSystemRoster &roster)
{
    SystemDefinition system{roster.systemId, {}};
    const double count = static_cast<double>(std::max<std::size_t>(1, roster.bodies.size()));
    for (std::size_t i = 0; i < roster.bodies.size(); ++i)
    {
        const auto &source = roster.bodies[i];
        const double angle = 6.283185307179586 * static_cast<double>(i) / count;
        const double radius = 6.0 + static_cast<double>(i) * 3.0;
        SystemBodyDefinition body;
        body.id = source.id;
        body.parentId = source.parentId;
        body.name = source.name;
        body.position = {std::cos(angle) * radius, std::sin(angle) * radius};
        body.kind = source.kind == PostSolarBodyKind::Giant       ? SystemBodyKind::Giant
                    : source.kind == PostSolarBodyKind::Moon      ? SystemBodyKind::Moon
                    : source.kind == PostSolarBodyKind::MinorBody ? SystemBodyKind::MinorBody
                                                                  : SystemBodyKind::Terrestrial;
        if (source.mineable && body.kind != SystemBodyKind::Giant)
            body.siteId = source.id + ".surface";
        body.purpose = source.mineable ? "Expedition site" : "Orbital exploration";
        body.hazard = source.hazardBias != MiningElementalAffinity::None ? "Local environmental hazards"
                                                                         : "Surface impact";
        system.bodies.push_back(std::move(body));
    }
    system.bodies.push_back({"straylight",
                             "",
                             "Straylight",
                             SystemBodyKind::Station,
                             {2, 2},
                             {},
                             .10,
                             1.42,
                             0,
                             "straylight.dock",
                             "Operational home",
                             "",
                             true});
    for (auto& body : system.bodies) body.dockOffset = {body.radius + .95, 0};
    return system;
}
} // namespace rocket
