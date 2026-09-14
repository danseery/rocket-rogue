#include "core/ArtifactProgression.h"

#include "core/ScenarioSystem.h"
#include "core/SolarProgression.h"
#include "core/Tuning.h"
#include "core/FlightSystem.h"
#include "core/MiningSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>

namespace rocket {

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
