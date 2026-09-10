#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace rocket
{
struct SystemVector
{
    double x = 0.0, y = 0.0;
};
enum class SystemBodyKind
{
    Star,
    Terrestrial,
    Giant,
    Moon,
    Station,
    MinorBody
};
struct SystemBodyDefinition
{
    std::string id;
    std::string parentId;
    std::string name;
    SystemBodyKind kind = SystemBodyKind::Terrestrial;
    SystemVector position;
    SystemVector velocity;
    double radius = 0.16;
    double influenceRadius = 1.42;
    double gravityScale = 1.0;
    std::string siteId;
    std::string purpose;
    std::string hazard;
    bool dock = false;
    std::string environmentId = {};
    bool authoredObjectives = true;
    SystemVector dockOffset = {};
    // Presentation size is independent from gravity, collision, and encounter
    // geometry so the solar-system view can use readable arcade proportions.
    double displayRadius = 0.0;
};
struct SystemDefinition
{
    std::string id;
    std::vector<SystemBodyDefinition> bodies;
};
const SystemDefinition &solarSystemDefinition();
const SystemBodyDefinition *systemBody(const SystemDefinition &, std::string_view id);
const SystemBodyDefinition *bodyForEnvironment(const SystemDefinition &, std::string_view id);
// Authored opening launch berth and departure impulse in Earth-relative units.
inline constexpr double earthLaunchSpeed = .60;
SystemVector earthLaunchPosition();
SystemVector systemDockPosition(const SystemBodyDefinition &);
// Waypoints for dock-bearing bodies lead to the service berth, not the
// collision body at the center of its gravity field.
SystemVector systemNavigationPosition(const SystemBodyDefinition &);
// Shared orbit/travel zoom envelope, independent of coordinate-frame ownership.
double systemBodyApproachBlend(const SystemBodyDefinition &, double radius);
double systemBodyDisplayRadius(const SystemBodyDefinition &);
double systemFlightTimeScale(const SystemDefinition &, SystemVector position);
double systemBodyGravityAcceleration(const SystemBodyDefinition &, double radius);
struct PostSolarSystemRoster;
SystemDefinition systemDefinitionForRoster(const PostSolarSystemRoster &);
} // namespace rocket
