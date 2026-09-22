#include "core/ArtifactProgression.h"

#include "core/ScenarioSystem.h"
#include "core/SolarProgression.h"
#include "core/Tuning.h"
#include "core/FlightSystem.h"
#include "core/MiningSystem.h"
#include "core/ExpeditionSystem.h"
#include "core/ResearchSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>

namespace rocket {

bool artifactCompletionStep(ScenarioEventKind event) {
    return event == ScenarioEventKind::ArtifactRecovered ||
        event == ScenarioEventKind::ProtectedObjectiveExtracted ||
        event == ScenarioEventKind::ArtifactBanked;
}

bool artifactCompletionStep(const ScenarioStepDefinition& step) {
    return artifactCompletionStep(step.completionEvent);
}

const MissionArtifact* missionArtifact(const GameState& s, std::string_view scenario, std::string_view step) {
    for (const auto& a : s.run.expedition.artifacts)
        if (a.scenarioId == scenario && a.stepId == step) return &a;
    return nullptr;
}

void registerArtifactAboard(GameState& s, const ContentCatalog& c, const ArtifactRecord& record,
    std::string_view site, std::string_view scenario, std::string_view step) {
    MissionArtifact a;
    a.artifact = record; a.sourceSiteId = site;
    // Mining handoffs can arrive before a scenario instance has been rebuilt
    // (for example while restoring a partially initialized expedition). Keep
    // the authored identity supplied by the caller as the authoritative
    // fallback instead of degrading the artifact into a generic entry.
    a.scenarioId = scenario;
    a.stepId = step;
    for (const auto& instance : s.meta.scenarios) {
        const auto* definition = c.findScenario(instance.definitionId.empty() ? instance.id : instance.definitionId);
        if (!definition) continue;
        const auto resolved = resolveScenarioDefinition(*definition,instance);
        for (const auto& candidate : resolved.steps) {
            if (!artifactCompletionStep(candidate)) continue;
            const auto* miningSite = findMiningSiteDefinition(c,candidate.miningSiteDefinitionId);
            const bool matches = (candidate.eventTargetId == record.id &&
                (candidate.eventOriginId.empty() || candidate.eventOriginId == record.originDestinationId)) ||
                (miningSite && miningSite->cocoon.protectedObjective.id == record.id &&
                 (site == candidate.miningSiteDefinitionId || scenario == instance.id));
            if (matches) { a.scenarioId=instance.id; a.stepId=candidate.id; }
        }
    }
    const auto* solar = solarMissionForBody(c, record.originDestinationId);
    if (solar && solar->artifactId == record.id) {
        a.key = "solar:" + solar->bodyId;
        a.scenarioId = solar->scenarioId; a.stepId = solar->claimStepId; a.requiredDockId = "earth";
    } else {
        a.key = s.run.expedition.location.systemId + ":" +
            (site.empty() ? s.run.expedition.location.siteId : std::string(site)) +
            ":" + a.scenarioId + ":" + record.id;
        a.requiredDockId = s.run.expedition.homeBodyId;
    }
    // Re-registering a scenario's same physical objective after departure must
    // not manufacture a second entry under the surface template's site ID.
    if (!a.scenarioId.empty() && !a.stepId.empty() && missionArtifact(s,a.scenarioId,a.stepId)) return;
    if (std::none_of(s.run.expedition.artifacts.begin(), s.run.expedition.artifacts.end(),
        [&](const auto& existing) { return existing.key == a.key; }))
        s.run.expedition.artifacts.push_back(std::move(a));
}

bool artifactHandInAvailable(const GameState& s, const MissionArtifact& a) {
    const auto& e = s.run.expedition;
    return a.owner == ArtifactCustody::Banked && !a.completed && !s.run.flight.active &&
        e.location.bodyId == a.requiredDockId && e.location.siteId.ends_with(".dock") &&
        (a.requiredDockId == "earth" || (a.requiredDockId == "straylight" && e.arkActivated));
}

void reconcileArtifactCustody(GameState& s, const ContentCatalog& c) {
    auto& e = s.run.expedition;
    // Battery ownership is stronger evidence than legacy permanent-inventory entries.
    for (const auto& b : e.batteries) {
        const auto* m = solarMissionForBody(c, b.id);
        if (!m || b.owner == BatteryOwner::Site) continue;
        ArtifactRecord record; record.id = m->artifactId; record.originDestinationId = m->bodyId;
        const auto old = std::find_if(s.meta.artifacts.begin(), s.meta.artifacts.end(), [&](const auto& item) {
            return item.id == record.id && item.originDestinationId == record.originDestinationId;
        });
        if (old != s.meta.artifacts.end()) record = *old;
        registerArtifactAboard(s,c,record,b.sourceSiteId);
        auto* a = const_cast<MissionArtifact*>(missionArtifact(s,m->scenarioId,m->claimStepId));
        if (!a) continue;
        a->owner = b.owner == BatteryOwner::Ship ? ArtifactCustody::Ship :
            b.owner == BatteryOwner::Wreck ? ArtifactCustody::Wreck : ArtifactCustody::Banked;
        a->wreckId = b.wreckId;
        if (a->owner == ArtifactCustody::Banked)
            a->bankedAt = b.owner == BatteryOwner::ArkSlot ? "straylight" : "earth";
        const auto* instance = findScenarioInstance(s.meta,m->scenarioId);
        const auto* progress = instance ? findScenarioStepProgress(*instance,m->claimStepId) : nullptr;
        // Mission hand-in is permanent; physical custody can subsequently move
        // through ship, wreck and Ark while transporting the same beacon.
        // Clearing completion here makes the dock immediately bank a collected
        // beacon again during the next guidance reconciliation.
        a->completed = a->completed || (progress && progress->claimed);
        a->experienceAwarded = old != s.meta.artifacts.end();
        a->objectiveExperienceAwarded = progress && progress->completed;
    }
    for (const auto& record : s.meta.artifacts) {
        if (std::any_of(e.artifacts.begin(),e.artifacts.end(),[&](const auto& a) {
            return a.artifact.id == record.id && a.artifact.originDestinationId == record.originDestinationId;
        })) continue;
        registerArtifactAboard(s,c,record,"legacy");
        auto found = std::find_if(e.artifacts.begin(),e.artifacts.end(),[&](const auto& item) {
            return item.artifact.id == record.id && item.artifact.originDestinationId == record.originDestinationId;
        });
        if (found == e.artifacts.end()) continue;
        auto& a = *found;
        a.owner = ArtifactCustody::Banked; a.bankedAt = a.requiredDockId;
        const auto* instance = findScenarioInstance(s.meta,a.scenarioId);
        const auto* p = instance ? findScenarioStepProgress(*instance,a.stepId) : nullptr;
        a.completed = a.scenarioId.empty() || (p && p->claimed);
        a.experienceAwarded = true; a.objectiveExperienceAwarded = p && p->completed;
    }
    const auto pending = [&](const auto& records, std::string_view site, std::string_view scenario, std::string_view step) {
        for (const auto& record : records) registerArtifactAboard(s,c,record,site,scenario,step);
    };
    pending(s.run.mining.stowedArtifacts,e.location.siteId,s.run.mining.scenarioId,s.run.mining.scenarioStepId);
    pending(s.run.planetaryExpedition.temporaryArtifacts,e.location.siteId,{},{});
    e.artifactCustodyLoaded = true;
}

void bankMissionArtifacts(GameState& s, const ContentCatalog& c) {
    reconcileArtifactCustody(s,c);
    auto& e = s.run.expedition;
    if (s.run.flight.active || !e.location.siteId.ends_with(".dock")) return;
    if (e.location.bodyId != "earth" && !(e.location.bodyId == "straylight" && e.arkActivated)) return;
    for (auto& a : e.artifacts) {
        if (a.owner == ArtifactCustody::Wreck || a.completed || a.requiredDockId != e.location.bodyId) continue;
        a.owner = ArtifactCustody::Banked; a.bankedAt = e.location.bodyId; a.wreckId = 0;
        // Keep the legacy battery projection synchronized before dispatching
        // the event; reconciliation treats that projection as authoritative
        // when loading older saves.
        for (auto& battery : e.batteries) {
            if (battery.id != a.artifact.originDestinationId || battery.owner != BatteryOwner::Ship) continue;
            battery.owner = e.location.bodyId == "straylight" ? BatteryOwner::ArkSlot : BatteryOwner::EarthStorage;
        }
        if (!a.scenarioId.empty()) recordScenarioEvent(s,c,{ScenarioEventKind::ArtifactBanked,
            a.scenarioId,a.stepId,a.artifact.originDestinationId,a.artifact.id,1,0});
    }
}

bool completeBankedArtifact(GameState& s, const ContentCatalog& c, std::string_view key) {
    for (auto& a : s.run.expedition.artifacts) {
        if (a.key != key || !artifactHandInAvailable(s,a)) continue;
        if (!a.scenarioId.empty()) {
            const auto* instance = findScenarioInstance(s.meta,a.scenarioId);
            const auto* p = instance ? findScenarioStepProgress(*instance,a.stepId) : nullptr;
            if (!p || !p->claimed) return false;
        }
        grantBankedArtifactRewards(s,c,a);
        a.completed = true;
        for (auto& battery : s.run.expedition.batteries)
            if (battery.id == a.artifact.originDestinationId) battery.researchEarned = true;
        return true;
    }
    return false;
}

namespace {

std::uint64_t mixHash(std::uint64_t value, std::uint64_t mix)
{
    value ^= mix + 0x9e3779b97f4a7c15ULL + (value << 6U) + (value >> 2U);
    value ^= value >> 30U;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27U;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

std::uint64_t textHash(std::string_view text)
{
    std::uint64_t value = 1469598103934665603ULL;
    for (const unsigned char ch : text) {
        value ^= ch;
        value *= 1099511628211ULL;
    }
    return value;
}

int seededChoice(std::uint64_t seed, std::uint64_t lane, int choices)
{
    if (choices <= 1) {
        return 0;
    }
    return static_cast<int>(mixHash(seed, lane) % static_cast<std::uint64_t>(choices));
}

bool stepDefinesProgressionArtifact(
    const ContentCatalog& catalog,
    const ScenarioStepDefinition& step)
{
    if (step.completionEvent == ScenarioEventKind::ArtifactRecovered) {
        return true;
    }
    if (step.completionEvent != ScenarioEventKind::ProtectedObjectiveExtracted ||
        step.miningSiteDefinitionId.empty()) {
        return false;
    }
    const MiningSiteDefinition* site = findMiningSiteDefinition(
        catalog,
        step.miningSiteDefinitionId);
    return site != nullptr &&
        site->cocoon.protectedObjective.kind == ProtectedObjectiveKind::Artifact &&
        !site->cocoon.protectedObjective.id.empty();
}

bool hasPermanentArtifactFrom(
    const GameState& state,
    std::string_view destinationId)
{
    // Once recovered, custody (including wrecks) owns the unique objective.
    // Do not regenerate a second artifact while its mission awaits banking.
    if (std::any_of(state.run.expedition.artifacts.begin(),state.run.expedition.artifacts.end(),
        [&](const auto& a) {return a.artifact.originDestinationId == destinationId;})) return true;
    return std::any_of(
        state.meta.artifacts.begin(),
        state.meta.artifacts.end(),
        [&](const ArtifactRecord& artifact) {
            return artifact.originDestinationId == destinationId;
        });
}

} // namespace

std::string artifactSectorForBody(const GameState& state, std::string_view systemId, std::string_view bodyId)
{
    const auto& location=state.run.expedition.location;
    if (location.systemId==systemId && location.bodyId==bodyId) {
        bool hasArtifact=state.run.mining.artifact.present;
        for (const auto& layer : state.run.mining.depthLayers) hasArtifact|=layer.artifact.present;
        const auto separator=location.siteId.rfind(':');
        if (hasArtifact && separator!=std::string::npos && planetLandingZone(std::string_view(location.siteId).substr(separator+1)))
            return location.siteId.substr(separator+1);
    }
    // Materialized artifacts, including delivered ones, retain their sector.
    for (const auto& site : state.run.expedition.sites) {
        if (site.systemId!=systemId || site.bodyId!=bodyId) continue;
        bool hasArtifact=site.mining.artifact.present;
        for (const auto& layer : site.mining.depthLayers) hasArtifact|=layer.artifact.present;
        if (!hasArtifact) continue;
        const auto separator=site.siteId.rfind(':');
        if (separator!=std::string::npos && planetLandingZone(std::string_view(site.siteId).substr(separator+1)))
            return site.siteId.substr(separator+1);
    }
    if (bodyId=="moon" && !state.run.expedition.moonTutorialZone.empty()) return state.run.expedition.moonTutorialZone;
    const auto seed=mixHash(mixHash(state.seed,textHash(systemId)),textHash(bodyId));
    return "zone_"+std::to_string(1+seededChoice(seed,0x5EC70FULL,6));
}

int encounterArtifactDepth(const GameState& state, std::string_view systemId, std::string_view bodyId)
{
    const auto seed=mixHash(mixHash(state.seed,textHash(systemId)),textHash(bodyId));
    return 1+seededChoice(seed,0xDE970ULL,tuning::surfaceDepthProgression::maximumDepthRating);
}

bool destinationHasAuthoredProgressionArtifact(
    const ContentCatalog& catalog,
    std::string_view destinationId)
{
    return std::any_of(
        catalog.scenarios.begin(),
        catalog.scenarios.end(),
        [&](const ScenarioDefinition& scenario) {
            if (!scenario.instantiateByDefault || scenario.destinationId != destinationId) {
                return false;
            }
            return std::any_of(
                scenario.steps.begin(),
                scenario.steps.end(),
                [&](const ScenarioStepDefinition& step) {
                    return stepDefinesProgressionArtifact(catalog, step) &&
                        (step.eventOriginId.empty() || step.eventOriginId == destinationId);
                });
        });
}

int recoveredProgressionArtifactDestinationCount(
    const GameState& state,
    const ContentCatalog& catalog)
{
    std::set<std::string> recoveredDestinations;
    for (const ArtifactRecord& artifact : state.meta.artifacts) {
        if (!artifact.originDestinationId.empty() &&
            destinationHasAuthoredProgressionArtifact(
                catalog,
                artifact.originDestinationId)) {
            recoveredDestinations.insert(artifact.originDestinationId);
        }
    }
    return static_cast<int>(recoveredDestinations.size());
}

std::optional<ProgressionArtifactOpportunity> unresolvedProgressionArtifactOpportunity(
    const GameState& state,
    const ContentCatalog& catalog,
    std::string_view destinationId,
    std::string_view bodyId,
    bool requireActiveStep)
{
    const std::string_view physicalBody = bodyId.empty() ? destinationId : bodyId;
    if (destinationId.empty() || hasPermanentArtifactFrom(state, physicalBody)) {
        return std::nullopt;
    }

    if (const SolarMissionDefinition* mission = solarMissionForBody(catalog, physicalBody)) {
        if (!solarMissionAvailable(state, *mission) || solarMissionClaimed(state, catalog, *mission)) {
            return std::nullopt;
        }
        const ScenarioDefinition* definition = catalog.findScenario(mission->scenarioId);
        const ScenarioStepDefinition* step = definition == nullptr
            ? nullptr
            : findScenarioStepDefinition(*definition, mission->claimStepId);
        const ScenarioStepState stepState = scenarioStepState(
            state, catalog, mission->scenarioId, mission->claimStepId);
        if (step == nullptr ||
            (requireActiveStep && stepState != ScenarioStepState::Active && stepState != ScenarioStepState::ReadyToClaim)) {
            return std::nullopt;
        }
        return ProgressionArtifactOpportunity {
            std::string(destinationId), std::string(physicalBody), mission->artifactId,
            mission->scenarioId, mission->claimStepId, step->miningSiteDefinitionId,
            step->miningSiteDefinitionId.empty()
                ? mission->scenarioId + ":" + mission->claimStepId
                : step->miningSiteDefinitionId};
    }

    const PlanetaryExpeditionState& expedition = state.run.planetaryExpedition;
    if (expedition.destinationId == destinationId &&
        !expedition.pendingScenarioId.empty() &&
        !expedition.pendingScenarioStepId.empty()) {
        const ScenarioInstance* pendingInstance = findScenarioInstance(
            state.meta,
            expedition.pendingScenarioId);
        const ScenarioDefinition* pendingDefinition = scenarioDefinitionForRuntimeId(
            state,
            catalog,
            expedition.pendingScenarioId);
        if (pendingInstance != nullptr && pendingDefinition != nullptr) {
            const ScenarioDefinition resolved = resolveScenarioDefinition(
                *pendingDefinition,
                *pendingInstance);
            const ScenarioStepDefinition* pendingStep = findScenarioStepDefinition(
                resolved,
                expedition.pendingScenarioStepId);
            if (resolved.destinationId == destinationId &&
                pendingStep != nullptr &&
                stepDefinesProgressionArtifact(catalog, *pendingStep)) {
                return ProgressionArtifactOpportunity {
                    std::string(destinationId),
                    std::string(physicalBody),
                    {},
                    expedition.pendingScenarioId,
                    expedition.pendingScenarioStepId,
                    pendingStep->miningSiteDefinitionId,
                    pendingStep->miningSiteDefinitionId.empty()
                        ? expedition.pendingScenarioId + ":" + expedition.pendingScenarioStepId
                        : pendingStep->miningSiteDefinitionId
                };
            }
        }
    }

    for (const ScenarioInstance& instance : state.meta.scenarios) {
        const std::string_view definitionId = instance.definitionId.empty()
            ? std::string_view(instance.id)
            : std::string_view(instance.definitionId);
        const ScenarioDefinition* definition = findScenarioDefinition(catalog, definitionId);
        if (definition == nullptr) {
            continue;
        }
        const ScenarioDefinition resolved = resolveScenarioDefinition(*definition, instance);
        if (resolved.destinationId != destinationId) {
            continue;
        }
        for (const ScenarioStepDefinition& step : resolved.steps) {
            if (!stepDefinesProgressionArtifact(catalog, step) ||
                (!step.eventOriginId.empty() && step.eventOriginId != destinationId)) {
                continue;
            }
            const ScenarioStepState stateForStep = scenarioStepState(
                state,
                catalog,
                instance.id,
                step.id);
            if (stateForStep != ScenarioStepState::Active &&
                stateForStep != ScenarioStepState::ReadyToClaim) {
                continue;
            }
            ProgressionArtifactOpportunity opportunity;
            opportunity.destinationId = std::string(destinationId);
            opportunity.bodyId = std::string(physicalBody);
            opportunity.scenarioId = instance.id;
            opportunity.stepId = step.id;
            opportunity.miningSiteDefinitionId = step.miningSiteDefinitionId;
            opportunity.siteIdentity = step.miningSiteDefinitionId.empty()
                ? instance.id + ":" + step.id
                : step.miningSiteDefinitionId;
            return opportunity;
        }
    }
    return std::nullopt;
}

OrbitalArtifactSignal orbitalArtifactSignal(const GameState& state, const ContentCatalog& catalog,
    const PreparedSurfaceLanding* prepared)
{
    OrbitalArtifactSignal signal;
    const auto& expedition = state.run.expedition;
    const auto* body = systemBody(solarSystemDefinition(), expedition.location.bodyId);
    if (!body || !unresolvedProgressionArtifactOpportunity(state, catalog,
            body->environmentId, body->id, false)) return signal;
    const std::string zoneId = artifactSectorForBody(state,expedition.location.systemId,body->id);
    const auto* zone = planetLandingZone(zoneId);
    if (!zone) return signal;
    signal.bearing = zone->centerBearing;
    bool delivered = false;
    const auto inspect = [&](std::string_view siteZone, const MiningRunState& mining, bool scanned) {
        signal.detected |= scanned;
        if (siteZone != zoneId) return;
        const auto inspectArtifact = [&](const MiningArtifactObject& artifact, int depth, int height) {
            if (!artifact.present) return;
            delivered |= artifact.state == MiningArtifactState::Delivered;
            if (!scanned || delivered) return;
            signal.localized = true;
            signal.bearing = landingZoneSiteBearing(*zone, artifact.x, mining.returnZoneX, mining.terrain.width);
            signal.depth = depth - mining.entryDepthZone + artifact.y / std::max(1, height);
        };
        inspectArtifact(mining.artifact, mining.depthZone, mining.terrain.height);
        for (const auto& layer : mining.depthLayers)
            inspectArtifact(layer.artifact, layer.depthZone, layer.terrain.height);
    };
    for (const auto& site : expedition.sites) {
        if (site.systemId != expedition.location.systemId || site.bodyId != body->id ||
            (prepared && site.siteId == prepared->persistentSiteId)) continue;
        const auto separator = site.siteId.rfind(':');
        if (separator != std::string::npos)
            inspect(std::string_view(site.siteId).substr(separator+1), site.mining, site.orbital.surveyComplete);
    }
    if (prepared && prepared->expeditionTemplate.bodyId == body->id)
        inspect(prepared->request.zoneId, prepared->miningTemplate, prepared->surveyComplete);
    if (delivered) return {};
    return signal;
}

ProgressionArtifactPlacement resolveProgressionArtifactPlacement(
    const GameState& state,
    const ContentCatalog& catalog,
    const Destination& destination,
    int /*miningDifficulty*/,
    std::string_view siteIdentity)
{
    ProgressionArtifactPlacement placement;
    const std::string_view bodyId = state.run.planetaryExpedition.bodyId.empty()
        ? std::string_view(destination.id)
        : std::string_view(state.run.planetaryExpedition.bodyId);
    if (const SolarMissionDefinition* mission = solarMissionForBody(catalog, bodyId)) {
        placement.artifactId = mission->artifactId;
        placement.ordinal = mission->optional ? 0 : mission->progressionOrdinal;
    } else {
        placement.ordinal = recoveredProgressionArtifactDestinationCount(state, catalog);
    }

    if (!solarMissionForBody(catalog,bodyId) && !state.run.planetaryExpedition.postSolarSystemId.empty()) {
        placement.targetDepth=encounterArtifactDepth(state,state.run.planetaryExpedition.postSolarSystemId,bodyId);
        placement.withinDepthSlot=1;
        placement.verticalOffset=10;
        return placement;
    }
    if (placement.ordinal <= 0) {
        placement.targetDepth = 1;
        placement.withinDepthSlot = 0;
        if (const SolarMissionDefinition* mission = solarMissionForBody(catalog, bodyId);
            mission && !mission->optional) {
            // Move the introductory shaft's top reference two cells closer to
            // the rig. Its artifact anchor gets the additional one-cell
            // shortening in the authored passage geometry.
            placement.verticalOffset = 8;
        }
        return placement;
    }

    // Keep the first recovery centered, then deepen the authored route in a
    // readable staircase: Mars stays reachable on the first layer, Io and
    // Titan occupy depth two, Titania depth three, and Triton depth four.
    // Within a shared layer the second artifact sits lower. Lateral variation
    // grows with mission stage without ever spending the vertical distance
    // budget, which previously allowed Mars to appear near the layer entry.
    const int uncappedDepth = placement.ordinal < 4
        ? 1 + placement.ordinal / 2
        : placement.ordinal - 1;
    placement.targetDepth = std::min(
        uncappedDepth,
        tuning::surfaceDepthProgression::maximumDepthRating);
    placement.withinDepthSlot = 1 + placement.ordinal % 2;
    placement.verticalOffset = 10 + (placement.ordinal % 2) * 4;

    std::uint64_t seed = mixHash(state.seed, textHash(destination.id));
    seed = mixHash(seed, static_cast<std::uint64_t>(placement.ordinal + 1));
    seed = mixHash(seed, textHash(siteIdentity));
    const int horizontalMagnitude = placement.ordinal + seededChoice(
        seed,
        0x51EEDULL,
        placement.ordinal + 1);
    const int side = seededChoice(seed, 0x51DEULL, 2) == 0 ? -1 : 1;
    placement.horizontalOffset = horizontalMagnitude * side;
    placement.manhattanDistance = placement.verticalOffset + horizontalMagnitude;
    return placement;
}

} // namespace rocket
