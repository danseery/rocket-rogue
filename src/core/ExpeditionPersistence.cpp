#include "core/ExpeditionPersistence.h"
#include "core/ExpeditionSystem.h"
#include "core/SaveData.h"
#include <cmath>
#include <algorithm>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace rocket
{
namespace
{
constexpr std::size_t maxSites = 4096, maxWrecks = 4096;
std::string hex(std::string_view s)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(s.size() * 2);
    for (unsigned char c : s)
    {
        out += digits[c >> 4];
        out += digits[c & 15];
    }
    return out;
}
std::optional<std::string> unhex(std::string_view s)
{
    if (s.size() % 2)
        return std::nullopt;
    const auto digit = [](char c)
    {
        return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    };
    std::string out;
    out.reserve(s.size() / 2);
    for (std::size_t i = 0; i < s.size(); i += 2)
    {
        int a = digit(s[i]), b = digit(s[i + 1]);
        if (a < 0 || b < 0)
            return std::nullopt;
        out += static_cast<char>(a * 16 + b);
    }
    return out;
}
void writeLocation(std::ostream &out, const SystemLocation &p)
{
    out << std::quoted(p.systemId) << ' ' << std::quoted(p.bodyId) << ' ' << static_cast<int>(p.frame) << ' '
        << p.position.x << ' ' << p.position.y << ' ' << p.velocity.x << ' ' << p.velocity.y << ' '
        << p.heading << ' ' << std::quoted(p.siteId) << ' ';
}
bool readLocation(std::istream &in, SystemLocation &p)
{
    int frame = 0;
    if (!(in >> std::quoted(p.systemId) >> std::quoted(p.bodyId) >> frame >> p.position.x >> p.position.y >>
          p.velocity.x >> p.velocity.y >> p.heading >> std::quoted(p.siteId)))
        return false;
    p.frame = static_cast<CoordinateFrame>(frame);
    return frame >= 0 && frame <= 1 && !p.systemId.empty() && std::isfinite(p.position.x) &&
           std::isfinite(p.position.y) && std::isfinite(p.velocity.x) && std::isfinite(p.velocity.y) &&
           std::isfinite(p.heading);
}
void writeCargo(std::ostream &out, const ExpeditionCargo &c)
{
    out << c.materials.common << ' ' << c.materials.rare << ' ' << c.materials.exotic << ' '
        << c.shipPropellant << ' ' << c.shipRepair << ' ';
}
bool readCargo(std::istream &in, ExpeditionCargo &c)
{
    if (!(in >> c.materials.common >> c.materials.rare >> c.materials.exotic >> c.shipPropellant >>
          c.shipRepair))
        return false;
    return c.materials.common >= 0 && c.materials.rare >= 0 && c.materials.exotic >= 0 &&
           std::isfinite(c.shipPropellant) && std::isfinite(c.shipRepair) && c.shipPropellant >= 0 &&
           c.shipRepair >= 0;
}
void writeBuild(std::ostream& out, const ExpeditionProgressionState& build)
{
    out << build.expeditionLevel << ' ' << build.expeditionExperience << ' '
        << build.pendingRunUpgradeChoices << ' ' << build.runUpgradeDraftCount << ' '
        << build.wideDrillHeadOffered << ' ' << build.sideCuttersOffered << ' ';
    out << build.runRigUpgradeRanks.size() << ' ';
    for (const auto& rank : build.runRigUpgradeRanks) out << std::quoted(rank.upgradeId) << ' ' << rank.rank << ' ';
    out << build.runDroneRanks.size() << ' ';
    for (const auto& rank : build.runDroneRanks) out << std::quoted(rank.droneId) << ' ' << rank.rank << ' ';
    out << build.selectedSynergyIds.size() << ' ';
    for (const auto& id : build.selectedSynergyIds) out << std::quoted(id) << ' ';
    out << build.droneModuleAssignments.size() << ' ';
    for (const auto& graft : build.droneModuleAssignments)
        out << graft.equippedFrame << ' ' << std::quoted(graft.primaryDroneId) << ' ' << static_cast<int>(graft.module) << ' ';
}
bool readBuild(std::istream& in, ExpeditionProgressionState& build)
{
    std::size_t count = 0;
    if (!(in >> build.expeditionLevel >> build.expeditionExperience >> build.pendingRunUpgradeChoices
        >> build.runUpgradeDraftCount >> build.wideDrillHeadOffered >> build.sideCuttersOffered) ||
        build.expeditionLevel < 1 || !std::isfinite(build.expeditionExperience) || build.expeditionExperience < 0 ||
        build.pendingRunUpgradeChoices < 0 || build.runUpgradeDraftCount < 0) return false;
    if (!(in >> count) || count > 4096) return false;
    for (std::size_t i=0;i<count;++i) { RunRigUpgradeRank rank; if (!(in >> std::quoted(rank.upgradeId) >> rank.rank) || rank.upgradeId.empty() || rank.rank<1 || rank.rank>3) return false; build.runRigUpgradeRanks.push_back(std::move(rank)); }
    if (!(in >> count) || count > 4096) return false;
    for (std::size_t i=0;i<count;++i) { RunDroneRank rank; if (!(in >> std::quoted(rank.droneId) >> rank.rank) || rank.droneId.empty() || rank.rank<1 || rank.rank>3) return false; build.runDroneRanks.push_back(std::move(rank)); }
    if (!(in >> count) || count > 4096) return false;
    for (std::size_t i=0;i<count;++i) { std::string id; if (!(in >> std::quoted(id)) || id.empty()) return false; build.selectedSynergyIds.push_back(std::move(id)); }
    if (!(in >> count) || count > 4096) return false;
    for (std::size_t i=0;i<count;++i) { DroneFrameModuleAssignment graft; int module=0; if (!(in >> graft.equippedFrame >> std::quoted(graft.primaryDroneId) >> module) || graft.equippedFrame<0 || module<0 || module>static_cast<int>(DroneModuleKind::HazardScreen)) return false; graft.module=static_cast<DroneModuleKind>(module); build.droneModuleAssignments.push_back(std::move(graft)); }
    return true;
}
} // namespace
std::string serializeExpedition(const PersistentExpeditionState &e)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10);
    out << e.active << ' ' << e.arkActivated << ' ' << std::quoted(e.homeBodyId) << ' ';
    writeLocation(out, e.location);
    writeCargo(out, e.cargo);
    out << std::quoted(e.course.targetBodyId) << ' ' << e.coursePlayerSelected << ' ' << e.straylightRevealed << ' '
        << e.cruise.active << ' ' << e.nextWreckId << ' ';
    for (const auto &b : e.batteries)
        out << std::quoted(b.id) << ' ' << std::quoted(b.sourceSiteId) << ' ' << static_cast<int>(b.owner)
            << ' ' << b.wreckId << ' ' << b.discovered << ' ' << b.researchEarned << ' ';
    out << e.wrecks.size() << ' ';
    for (const auto &w : e.wrecks)
    {
        out << w.id << ' ';
        writeLocation(out, w.location);
        writeCargo(out, w.cargo);
        out << w.buildRecoverable << ' ';
        writeBuild(out, w.build);
    }
    out << std::quoted(e.decision.pendingId) << ' ' << e.decision.awaitingAscent << ' '
        << e.decision.acknowledgedIds.size() << ' ';
    for (const auto &id : e.decision.acknowledgedIds)
        out << std::quoted(id) << ' ';
    out << e.sites.size() << ' ';
    for (const auto &site : e.sites)
    {
        SaveData snapshot;
        snapshot.planetaryExpedition = site.surface;
        snapshot.mining = site.mining;
        // Reuse the existing terrain/object codec. Nested site payloads cannot
        // carry another expedition, so parsing is bounded to one level.
        std::string payload = serializeSaveData(snapshot);
        const auto start = payload.find("persistentExpedition=");
        if (start != std::string::npos)
            payload.erase(start, payload.find('\n', start) - start + 1);
        out << std::quoted(site.systemId) << ' ' << std::quoted(site.bodyId) << ' '
            << std::quoted(site.siteId) << ' ' << std::quoted(hex(payload)) << ' ';
    }
    out << " travel1 " << e.travelInitialized << ' ' << e.discoveredBodies.size();
    for (const auto &id : e.discoveredBodies) out << ' ' << std::quoted(id);
    out << ' ' << e.rigFuel.current << ' ' << e.rigFuel.capacity << ' ' << e.course.estimateValid;
    out << " economy1 " << e.cargo.credits << ' ' << e.wrecks.size();
    for (const auto& wreck : e.wrecks) out << ' ' << wreck.id << ' ' << wreck.cargo.credits;
    out << " opening1 " << e.openingInitialized << ' ' << e.departureHistoryKnown << ' ' << e.departureCount << ' ' << e.undockReady;
    out << " wedges1 " << std::quoted(e.selectedOrbitBody) << ' ' << std::quoted(e.selectedOrbitZone)
        << ' ' << std::quoted(e.moonTutorialZone) << ' ' << e.sites.size();
    for (const auto& site : e.sites) {
        const auto& p = site.orbital;
        out << ' ' << p.surveyedDepth << ' ' << p.laserDepth << ' ' << p.laserRow << ' ' << p.shaftX
            << ' ' << p.laserRowWork << ' ' << p.surveyElapsed << ' ' << p.laserBlocked
            << ' ' << p.laserComplete << ' ' << p.surveyComplete << ' ' << p.surveyLayers.size();
        for (const auto& layer : p.surveyLayers)
            out << ' ' << layer.depth << ' ' << layer.common << ' ' << layer.rare << ' ' << layer.exotic
                << ' ' << layer.artifact << ' ' << layer.thermal << ' ' << layer.cryo
                << ' ' << layer.radiation << ' ' << layer.toxic;
    }
    out << " cruise1 " << e.cruise.cooling;
    return out.str();
}
std::optional<PersistentExpeditionState> deserializeExpedition(std::string_view input)
{
    std::istringstream in{std::string(input)};
    in.imbue(std::locale::classic());
    PersistentExpeditionState e;
    if (!(in >> e.active >> e.arkActivated >> std::quoted(e.homeBodyId)) || !readLocation(in, e.location) ||
        !readCargo(in, e.cargo))
        return std::nullopt;
    if (!(in >> std::quoted(e.course.targetBodyId) >> e.coursePlayerSelected >> e.straylightRevealed >> e.cruise.active >> e.nextWreckId))
        return std::nullopt;
    for (auto &b : e.batteries)
    {
        const auto expectedId = b.id, expectedSite = b.sourceSiteId;
        int owner = 0;
        if (!(in >> std::quoted(b.id) >> std::quoted(b.sourceSiteId) >> owner >> b.wreckId >> b.discovered >>
              b.researchEarned))
            return std::nullopt;
        if (b.id != expectedId || b.sourceSiteId != expectedSite)
            return std::nullopt;
        b.owner = static_cast<BatteryOwner>(owner);
    }
    std::size_t count = 0;
    if (!(in >> count) || count > maxWrecks)
        return std::nullopt;
    for (std::size_t i = 0; i < count; ++i)
    {
        WreckState w;
        if (!(in >> w.id) || !readLocation(in, w.location) || !readCargo(in, w.cargo) ||
            !(in >> w.buildRecoverable) || !readBuild(in, w.build))
            return std::nullopt;
        e.wrecks.push_back(std::move(w));
    }
    if (!(in >> std::quoted(e.decision.pendingId) >> e.decision.awaitingAscent >> count) || count > 4096)
        return std::nullopt;
    for (std::size_t i = 0; i < count; ++i)
    {
        std::string id;
        if (!(in >> std::quoted(id)))
            return std::nullopt;
        e.decision.acknowledgedIds.push_back(std::move(id));
    }
    if (!(in >> count) || count > maxSites)
        return std::nullopt;
    for (std::size_t i = 0; i < count; ++i)
    {
        PersistentSiteState site;
        std::string encoded;
        if (!(in >> std::quoted(site.systemId) >> std::quoted(site.bodyId) >> std::quoted(site.siteId) >>
              std::quoted(encoded)))
            return std::nullopt;
        auto payload = unhex(encoded);
        if (!payload || payload->find("persistentExpedition=") != std::string::npos)
            return std::nullopt;
        auto saved = deserializeSaveData(*payload);
        if (!saved)
            return std::nullopt;
        site.surface = std::move(saved->planetaryExpedition);
        site.mining = std::move(saved->mining);
        for (const auto &old : e.sites)
            if (old.systemId == site.systemId && old.bodyId == site.bodyId && old.siteId == site.siteId)
                return std::nullopt;
        e.sites.push_back(std::move(site));
    }
    in >> std::ws;
    if (!in.eof()) {
        std::string extension;
        if (!(in >> extension >> e.travelInitialized >> count) || extension != "travel1" || count > 4096)
            return std::nullopt;
        for (std::size_t i = 0; i < count; ++i) {
            std::string id;
            if (!(in >> std::quoted(id)) || id.empty()) return std::nullopt;
            e.discoveredBodies.push_back(std::move(id));
        }
        if (!(in >> e.rigFuel.current >> e.rigFuel.capacity) || !std::isfinite(e.rigFuel.current) ||
            !std::isfinite(e.rigFuel.capacity) || e.rigFuel.current < 0 || e.rigFuel.current > e.rigFuel.capacity) return std::nullopt;
        in >> std::ws;
        if (!in.eof() && !(in >> e.course.estimateValid)) return std::nullopt;
        in >> std::ws;
        if (!in.eof()) {
            std::string economy;
            if (!(in >> economy >> e.cargo.credits >> count) || economy != "economy1" || count != e.wrecks.size() || !std::isfinite(e.cargo.credits) || e.cargo.credits < 0) return std::nullopt;
            std::vector<std::uint64_t> seen;
            for (std::size_t i=0;i<count;++i) {
                std::uint64_t id;
                double credits;
                if (!(in >> id >> credits) || !std::isfinite(credits) || credits<0 || std::find(seen.begin(),seen.end(),id)!=seen.end()) return std::nullopt;
                auto found = std::find_if(e.wrecks.begin(),e.wrecks.end(),[&](const auto& w) { return w.id==id; });
                if (found==e.wrecks.end()) return std::nullopt;
                found->cargo.credits=credits;
                seen.push_back(id);
            }
            in >> std::ws;
        }
    }
    if (!in.eof()) {
        std::string opening;
        if (!(in >> opening >> e.openingInitialized >> e.departureHistoryKnown >> e.departureCount >> e.undockReady) || opening != "opening1") return std::nullopt;
        in >> std::ws;
    }
    if (!in.eof()) {
        std::string extension;
        if (!(in >> extension >> std::quoted(e.selectedOrbitBody) >> std::quoted(e.selectedOrbitZone)
            >> std::quoted(e.moonTutorialZone) >> count) || extension != "wedges1" || count != e.sites.size()) return std::nullopt;
        const auto validZone = [](const std::string& z) { return z.size()==6 && z.starts_with("zone_") && z.back()>='1' && z.back()<='6'; };
        if (!validZone(e.selectedOrbitZone) || (!e.moonTutorialZone.empty() && !validZone(e.moonTutorialZone))) return std::nullopt;
        for (auto& site : e.sites) {
            auto& p = site.orbital;
            std::size_t layers;
            if (!(in >> p.surveyedDepth >> p.laserDepth >> p.laserRow >> p.shaftX
                >> p.laserRowWork >> p.surveyElapsed >> p.laserBlocked >> p.laserComplete >> p.surveyComplete >> layers)
                || layers>128 || p.surveyedDepth < -1 || p.laserDepth<0 || p.laserRow<0 || p.shaftX<0
                || !std::isfinite(p.laserRowWork) || p.laserRowWork<0
                || !std::isfinite(p.surveyElapsed) || p.surveyElapsed<0 || p.surveyElapsed>2.0) return std::nullopt;
            for (std::size_t i=0;i<layers;++i) {
                OrbitalSurveyLayer layer;
                if (!(in >> layer.depth >> layer.common >> layer.rare >> layer.exotic >> layer.artifact
                    >> layer.thermal >> layer.cryo >> layer.radiation >> layer.toxic)) return std::nullopt;
                p.surveyLayers.push_back(layer);
            }
        }
        in >> std::ws;
    }
    if (!in.eof()) {
        std::string extension;
        if (!(in >> extension >> e.cruise.cooling) || extension != "cruise1") return std::nullopt;
        in >> std::ws;
    }
    if (e.undockReady && (e.active || !e.location.siteId.ends_with(".dock"))) return std::nullopt;
    if (!in.eof() || !validBatteryOwnership(e))
        return std::nullopt;
    if (e.homeBodyId != "earth" && e.homeBodyId != "straylight")
        return std::nullopt;
    if (e.homeBodyId == "straylight" && !e.arkActivated)
        return std::nullopt;
    return e;
}
} // namespace rocket
