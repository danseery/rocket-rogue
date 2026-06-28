#include "core/ResearchSystem.h"
#include "core/ContentIds.h"
#include "core/GameFormat.h"
#include "core/GameText.h"
#include "core/Tuning.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace rocket {

namespace {

const Destination* currentResearchDestination(const GameState& state, const ContentCatalog& catalog)
{
    if (state.run.surfaceExpedition.active) {
        return catalog.findDestination(state.run.surfaceExpedition.destinationId);
    }
    if (state.run.arrivalOps.active) {
        return catalog.findDestination(state.run.arrivalOps.destinationId);
    }
    if (state.run.destinationIndex >= 0 && state.run.destinationIndex < static_cast<int>(catalog.destinations.size())) {
        return &catalog.destinations[static_cast<std::size_t>(state.run.destinationIndex)];
    }
    return nullptr;
}

bool projectUnlockedForDestination(const ResearchProject& project, const MetaProgress& meta, const Destination& destination)
{
    return project.requiredDestinationTier <= destination.tier && hasUnlock(meta, project.unlockKey);
}

MaterialInventory halvedMaterials(const MaterialInventory& materials)
{
    return {
        std::max(0, materials.common / 2),
        std::max(0, materials.rare / tuning::research::extractionRareLossDivisor),
        0
    };
}

int materialCargo(const MaterialInventory& materials)
{
    return std::max(0, materials.common) + std::max(0, materials.rare) * 2 + std::max(0, materials.exotic) * 4;
}

bool hasTag(const std::vector<std::string>& tags, const std::string& tag)
{
    return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

ArtifactRecord* firstUnidentifiedArtifact(GameState& state)
{
    auto artifact = std::find_if(state.meta.artifacts.begin(), state.meta.artifacts.end(), [](const ArtifactRecord& record) {
        return !record.identified;
    });
    if (artifact == state.meta.artifacts.end()) {
        return nullptr;
    }
    return &(*artifact);
}

std::string artifactId(const SurfaceExpeditionState& expedition)
{
    std::ostringstream out;
    out << expedition.destinationId << "_artifact_" << expedition.depth;
    return out.str();
}

SurfaceActionOutcome spendSupply(SurfaceExpeditionState& expedition, int amount)
{
    SurfaceActionOutcome outcome;
    if (!expedition.active || expedition.supply < amount) {
        return outcome;
    }

    expedition.supply -= amount;
    outcome.applied = true;
    outcome.supplyDelta = -amount;
    return outcome;
}

double surfaceHazardChance(double hazard, double scale, double relief)
{
    return std::clamp(
        hazard * scale - relief,
        tuning::research::surfaceHazardChanceMinimum,
        tuning::research::surfaceHazardChanceMaximum);
}

SurfaceSiteProfile generatedSurfaceSiteProfile(const GameState& state, const Destination& destination, Random* rng)
{
    if (rng != nullptr) {
        return static_cast<SurfaceSiteProfile>(rng->rangeInt(0, 2));
    }
    return static_cast<SurfaceSiteProfile>((state.seed + static_cast<std::uint64_t>(destination.tier)) % 3);
}

void ensureDestinationHistory(std::vector<int>& values, const ContentCatalog& catalog)
{
    if (values.size() < catalog.destinations.size()) {
        values.resize(catalog.destinations.size(), 0);
    }
}

int destinationIndexForId(const ContentCatalog& catalog, std::string_view destinationId)
{
    for (std::size_t i = 0; i < catalog.destinations.size(); ++i) {
        if (catalog.destinations[i].id == destinationId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void addDestinationHistoryValue(std::vector<int>& values, const ContentCatalog& catalog, std::string_view destinationId)
{
    const int index = destinationIndexForId(catalog, destinationId);
    if (index < 0) {
        return;
    }
    ensureDestinationHistory(values, catalog);
    values[static_cast<std::size_t>(index)] += 1;
}

bool isMoonTutorialLanding(const Destination& destination)
{
    return destination.id == content::destination::moon;
}

double landingReconHazardPenalty(const GameState& state, const ContentCatalog& catalog, const Destination& destination)
{
    const int flybys = destinationHistoryValue(state.meta.destinationFlybys, catalog, destination.id);
    const int orbits = destinationHistoryValue(state.meta.destinationOrbits, catalog, destination.id);
    double penalty = 0.0;
    if (flybys <= 0) {
        penalty += 0.08;
    }
    if (orbits <= 0) {
        penalty += 0.12;
    }
    return penalty;
}

void applySurfaceHazard(
    SurfaceExpeditionState& expedition,
    SurfaceActionOutcome& outcome,
    Random& rng,
    double scale,
    double relief,
    std::string_view message,
    int supplyLoss,
    int cargoLoss,
    double hazardIncrease)
{
    if (!rng.chance(surfaceHazardChance(expedition.hazard, scale, relief))) {
        return;
    }

    const int actualSupplyLoss = std::min(std::max(0, supplyLoss), std::max(0, expedition.supply));
    const int actualCargoLoss = std::min(std::max(0, cargoLoss), std::max(0, expedition.cargo));
    expedition.supply -= actualSupplyLoss;
    expedition.cargo -= actualCargoLoss;
    expedition.hazard += hazardIncrease;

    outcome.hazardTriggered = true;
    outcome.hazardMessage = std::string(message);
    outcome.supplyDelta -= actualSupplyLoss;
    outcome.cargoDelta -= actualCargoLoss;
    outcome.hazardDelta += hazardIncrease;
}

bool hasSurfaceTooling(const MetaProgress& meta)
{
    return hasUnlock(meta, content::unlock::surfaceProbes)
        || hasUnlock(meta, content::unlock::surfaceDrills)
        || hasUnlock(meta, content::unlock::cargoRigs)
        || hasUnlock(meta, content::unlock::perimeterDrones);
}

void applyEnemyContact(SurfaceExpeditionState& expedition, SurfaceActionOutcome& outcome, double encounterChance, Random& rng)
{
    if (!expedition.enemyEncountersEnabled || !rng.chance(encounterChance)) {
        return;
    }

    const int actualSupplyLoss = std::min(tuning::research::surfaceEnemySupplyLoss, std::max(0, expedition.supply));
    const int actualCargoLoss = std::min(tuning::research::surfaceEnemyCargoLoss, std::max(0, expedition.cargo));
    expedition.supply -= actualSupplyLoss;
    expedition.cargo -= actualCargoLoss;
    expedition.hazard += tuning::research::surfaceEnemyHazardIncrease;

    outcome.eventType = SurfaceEventType::EnemyContact;
    outcome.eventMessage = std::string(text::status::surfaceEnemyContact);
    outcome.supplyDelta -= actualSupplyLoss;
    outcome.cargoDelta -= actualCargoLoss;
    outcome.hazardDelta += tuning::research::surfaceEnemyHazardIncrease;
    outcome.enemyEncounter = true;
}

void applySurfaceEvent(GameState& state, SurfaceActionOutcome& outcome, Random& rng)
{
    SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    if (!outcome.applied || outcome.hazardTriggered) {
        return;
    }

    applyEnemyContact(expedition, outcome, surfaceEnemyEncounterChance(state), rng);
    if (outcome.eventType == SurfaceEventType::EnemyContact) {
        return;
    }

    const double eventChance = std::clamp(
        tuning::research::surfaceEventChanceBase + expedition.hazard * tuning::research::surfaceEventChanceHazardScale,
        0.0,
        tuning::research::surfaceEventChanceMaximum);
    if (!rng.chance(eventChance)) {
        return;
    }

    const double failureShare = std::max(
        tuning::research::surfaceEquipmentFailureMinimumShare,
        tuning::research::surfaceEquipmentFailureShare - (hasSurfaceTooling(state.meta) ? tuning::research::surfaceToolFailureRelief : 0.0));
    const double roll = rng.next01();
    if (roll < failureShare) {
        const int actualSupplyLoss = std::min(tuning::research::surfaceEquipmentFailureSupplyLoss, std::max(0, expedition.supply));
        expedition.supply -= actualSupplyLoss;
        expedition.hazard += tuning::research::surfaceEquipmentFailureHazardIncrease;
        outcome.eventType = SurfaceEventType::EquipmentFailure;
        outcome.eventMessage = std::string(text::status::surfaceEquipmentFailure);
        outcome.supplyDelta -= actualSupplyLoss;
        outcome.hazardDelta += tuning::research::surfaceEquipmentFailureHazardIncrease;
        return;
    }

    if (roll < failureShare + tuning::research::surfaceUnexpectedDepositShare) {
        MaterialInventory gain {.common = tuning::research::surfaceDepositCommonGain};
        if (rng.chance(tuning::research::surfaceDepositRareChance + surfaceSiteProfileEffects(expedition.siteProfile).mineRareChanceBonus)) {
            gain.rare += 1;
        }
        expedition.temporaryMaterials.common = std::max(0, expedition.temporaryMaterials.common + gain.common);
        expedition.temporaryMaterials.rare = std::max(0, expedition.temporaryMaterials.rare + gain.rare);
        expedition.temporaryMaterials.exotic = std::max(0, expedition.temporaryMaterials.exotic + gain.exotic);
        expedition.cargo += materialCargo(gain);
        outcome.eventType = SurfaceEventType::UnexpectedDeposit;
        outcome.eventMessage = std::string(text::status::surfaceUnexpectedDeposit);
        outcome.materialDelta.common += gain.common;
        outcome.materialDelta.rare += gain.rare;
        outcome.materialDelta.exotic += gain.exotic;
        outcome.cargoDelta += materialCargo(gain);
        return;
    }

    state.meta.blueprintProgress += tuning::research::surfaceCrewDiscoveryBlueprintGain;
    outcome.eventType = SurfaceEventType::CrewDiscovery;
    outcome.eventMessage = std::string(text::status::surfaceCrewDiscovery);
    outcome.blueprintDelta = tuning::research::surfaceCrewDiscoveryBlueprintGain;
}

void appendSurfaceLog(SurfaceExpeditionState& expedition, std::string entry)
{
    if (entry.empty()) {
        return;
    }
    expedition.logEntries.push_back(std::move(entry));
    const int overflow = static_cast<int>(expedition.logEntries.size()) - tuning::research::surfaceLogEntryLimit;
    if (overflow > 0) {
        expedition.logEntries.erase(expedition.logEntries.begin(), expedition.logEntries.begin() + overflow);
    }
}

void finalizeSurfaceAction(GameState& state, SurfaceActionOutcome& outcome, Random& rng, double extractionRiskBefore)
{
    applySurfaceEvent(state, outcome, rng);
    outcome.extractionRiskDelta = surfaceExtractionRisk(state) - extractionRiskBefore;
    appendSurfaceLog(state.run.surfaceExpedition, surfaceActionSummary(outcome));
}

std::string signedWhole(int value)
{
    return value > 0 ? "+" + std::to_string(value) : std::to_string(value);
}

std::string signedPercent(double value)
{
    return (value > 0.0 ? "+" : "") + display::percent(value);
}

void addDelta(std::vector<std::string>& parts, int value, std::string_view label)
{
    if (value != 0) {
        parts.push_back(signedWhole(value) + " " + std::string(label));
    }
}

void addLoss(std::vector<std::string>& parts, int value, std::string_view label)
{
    if (value > 0) {
        parts.push_back("Lost " + std::to_string(value) + " " + std::string(label));
    }
}

std::string joinParts(const std::vector<std::string>& parts)
{
    std::string summary;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            summary += "; ";
        }
        summary += parts[i];
    }
    return summary;
}

std::string surfaceDeltaSummary(const SurfaceActionOutcome& outcome)
{
    std::vector<std::string> parts;
    addDelta(parts, outcome.supplyDelta, text::labels::supply);
    addDelta(parts, outcome.materialDelta.common, text::labels::commonMaterials);
    addDelta(parts, outcome.materialDelta.rare, text::labels::rareMaterials);
    addDelta(parts, outcome.materialDelta.exotic, text::labels::exoticMaterials);
    addLoss(parts, outcome.materialLost.common, text::labels::commonMaterials);
    addLoss(parts, outcome.materialLost.rare, text::labels::rareMaterials);
    addLoss(parts, outcome.materialLost.exotic, text::labels::exoticMaterials);
    addDelta(parts, outcome.cargoDelta, text::labels::cargo);
    addDelta(parts, outcome.blueprintDelta, text::labels::blueprints);
    if (outcome.artifactFound) {
        parts.push_back("+1 " + std::string(text::labels::artifacts));
    }
    addLoss(parts, outcome.artifactsLost, text::labels::artifacts);
    if (outcome.extractionRisk > 0.0) {
        parts.push_back(display::percent(outcome.extractionRisk) + " " + std::string(text::labels::extractionRisk));
    }
    if (std::abs(outcome.extractionRiskDelta) >= 0.005) {
        parts.push_back(signedPercent(outcome.extractionRiskDelta) + " " + std::string(text::labels::extractionRisk));
    }
    if (std::abs(outcome.hazardDelta) > 0.0001) {
        parts.push_back(signedPercent(outcome.hazardDelta) + " " + std::string(text::labels::hazard));
    }

    return joinParts(parts);
}

struct FlybyPathSample {
    double distance = 0.0;
    double progress = 0.0;
};

struct FlybyTravelSample {
    double progress = 0.0;
    int bestZone = 0;
    int worstZone = 2;
    bool finished = false;
};

struct FlybyFinishHit {
    bool hit = false;
    double t = 1.0;
};

double cubic(double a, double b, double c, double d, double t)
{
    const double u = 1.0 - t;
    return u * u * u * a + 3.0 * u * u * t * b + 3.0 * u * t * t * c + t * t * t * d;
}

FlybyPathSample nearestFlybyPathSample(double shipX, double shipY)
{
    constexpr int sampleCount = 240;
    double bestDistanceSq = std::numeric_limits<double>::max();
    double bestProgress = 0.0;
    double previousX = tuning::flyby::startX;
    double previousY = tuning::flyby::startY;

    for (int i = 1; i <= sampleCount; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(sampleCount);
        const double currentX = cubic(tuning::flyby::startX, tuning::flyby::control1X, tuning::flyby::control2X, tuning::flyby::endX, t);
        const double currentY = cubic(tuning::flyby::startY, tuning::flyby::control1Y, tuning::flyby::control2Y, tuning::flyby::endY, t);
        const double segmentX = currentX - previousX;
        const double segmentY = currentY - previousY;
        const double segmentLengthSq = segmentX * segmentX + segmentY * segmentY;
        double segmentShare = 0.0;
        if (segmentLengthSq > 0.000001) {
            segmentShare = std::clamp(((shipX - previousX) * segmentX + (shipY - previousY) * segmentY) / segmentLengthSq, 0.0, 1.0);
        }
        const double projectedX = previousX + segmentX * segmentShare;
        const double projectedY = previousY + segmentY * segmentShare;
        const double dx = shipX - projectedX;
        const double dy = shipY - projectedY;
        const double distanceSq = dx * dx + dy * dy;
        if (distanceSq < bestDistanceSq) {
            bestDistanceSq = distanceSq;
            bestProgress = (static_cast<double>(i - 1) + segmentShare) / static_cast<double>(sampleCount);
        }
        previousX = currentX;
        previousY = currentY;
    }

    return {std::sqrt(bestDistanceSq), bestProgress};
}

std::pair<double, double> flybyEndTangent()
{
    const double dx = 3.0 * (tuning::flyby::endX - tuning::flyby::control2X);
    const double dy = 3.0 * (tuning::flyby::endY - tuning::flyby::control2Y);
    const double length = std::max(0.001, std::hypot(dx, dy));
    return {dx / length, dy / length};
}

double flybyFinishPlaneValue(double shipX, double shipY)
{
    const auto [tangentX, tangentY] = flybyEndTangent();
    return (shipX - tuning::flyby::endX) * tangentX + (shipY - tuning::flyby::endY) * tangentY;
}

int flybyZoneForDistance(double distance)
{
    if (distance <= tuning::flyby::perfectBand) {
        return 2;
    }
    if (distance <= tuning::flyby::goodBand) {
        return 1;
    }
    return 0;
}

int flybyZoneAt(double shipX, double shipY)
{
    return flybyZoneForDistance(nearestFlybyPathSample(shipX, shipY).distance);
}

void addFlybyTravelSample(FlybyTravelSample& result, const FlybyPathSample& sample)
{
    const int zone = flybyZoneForDistance(sample.distance);
    result.progress = std::max(result.progress, sample.progress);
    result.bestZone = std::max(result.bestZone, zone);
    result.worstZone = std::min(result.worstZone, zone);
}

FlybyFinishHit flybyFinishLineHit(double startX, double startY, double endX, double endY)
{
    const double startValue = flybyFinishPlaneValue(startX, startY);
    const double endValue = flybyFinishPlaneValue(endX, endY);
    if (startValue >= 0.0) {
        return {true, 0.0};
    }
    if (endValue >= 0.0 && endValue > startValue) {
        return {true, std::clamp(startValue / (startValue - endValue), 0.0, 1.0)};
    }
    return {};
}

FlybyTravelSample sampleFlybyTravel(double startX, double startY, double endX, double endY)
{
    const double distance = std::hypot(endX - startX, endY - startY);
    const int sampleCount = std::clamp(
        static_cast<int>(std::ceil(distance / std::max(0.001, tuning::flyby::perfectBand * 0.15))),
        2,
        96);

    FlybyTravelSample result;
    addFlybyTravelSample(result, nearestFlybyPathSample(startX, startY));
    const FlybyFinishHit finishHit = flybyFinishLineHit(startX, startY, endX, endY);
    if (finishHit.hit && finishHit.t <= 0.0) {
        result.progress = std::max(result.progress, tuning::flyby::finishProgress);
        result.finished = true;
        return result;
    }

    const double sampleEndT = finishHit.hit ? std::clamp(finishHit.t, 0.0, 1.0) : 1.0;

    for (int i = 0; i <= sampleCount; ++i) {
        const double t = std::min(sampleEndT, static_cast<double>(i) / static_cast<double>(sampleCount));
        if (t <= 0.0) {
            continue;
        }
        const double x = startX + (endX - startX) * t;
        const double y = startY + (endY - startY) * t;
        const FlybyPathSample sample = nearestFlybyPathSample(x, y);

        addFlybyTravelSample(result, sample);
        if (t >= sampleEndT) {
            break;
        }
    }

    if (finishHit.hit) {
        result.progress = std::max(result.progress, tuning::flyby::finishProgress);
        result.finished = true;
    }
    return result;
}

double flybyGravityForDestination(const Destination& destination)
{
    if (destination.tier <= 2) {
        return tuning::flyby::gravityEasy;
    }
    if (destination.tier == 3) {
        return tuning::flyby::gravityMedium;
    }
    if (destination.tier == 4) {
        return tuning::flyby::gravityLarge;
    }
    return tuning::flyby::gravityDeep;
}

double flybyPlanetColliderRadius(const Destination& destination)
{
    return tuning::flyby::planetColliderBaseRadius
        + static_cast<double>(std::min(4, destination.tier)) * tuning::flyby::planetColliderTierRadius
        + tuning::flyby::planetColliderPadding;
}

bool flybyShipIntersectsPlanet(const FlybyRunState& flyby)
{
    const double speed = std::hypot(flyby.velocityX, flyby.velocityY);
    const double forwardX = speed > 0.001 ? flyby.velocityX / speed : 1.0;
    const double forwardY = speed > 0.001 ? flyby.velocityY / speed : 0.0;
    const double rightX = forwardY;
    const double rightY = -forwardX;
    const double dx = tuning::flyby::destinationX - flyby.shipX;
    const double dy = tuning::flyby::destinationY - flyby.shipY;
    const double localX = std::clamp(dx * forwardX + dy * forwardY, -tuning::flyby::shipColliderHalfLength, tuning::flyby::shipColliderHalfLength);
    const double localY = std::clamp(dx * rightX + dy * rightY, -tuning::flyby::shipColliderHalfWidth, tuning::flyby::shipColliderHalfWidth);
    const double closestX = flyby.shipX + forwardX * localX + rightX * localY;
    const double closestY = flyby.shipY + forwardY * localX + rightY * localY;
    return std::hypot(tuning::flyby::destinationX - closestX, tuning::flyby::destinationY - closestY) <= flyby.planetColliderRadius;
}

std::pair<double, double> flybyShipNosePoint(double shipX, double shipY, double velocityX, double velocityY)
{
    const double speed = std::hypot(velocityX, velocityY);
    if (speed <= 0.001) {
        return {shipX, shipY};
    }
    return {
        shipX + (velocityX / speed) * tuning::flyby::shipColliderHalfLength,
        shipY + (velocityY / speed) * tuning::flyby::shipColliderHalfLength
    };
}

double flybyBaseReward(const Destination& destination)
{
    return std::max(tuning::flyby::goodRewardFloor, destination.baseReward * tuning::flyby::goodRewardFactor);
}

double flybySpeedScale(const FlybyRunState& flyby)
{
    const double speed = std::hypot(flyby.velocityX, flyby.velocityY);
    const double baselineSpeed = std::hypot(tuning::flyby::startVelocityX, tuning::flyby::startVelocityY);
    const double range = std::max(0.001, tuning::flyby::maxSpeed - baselineSpeed);
    const double fastShare = std::clamp((speed - baselineSpeed) / range, 0.0, 1.0);
    return 1.0 + fastShare * (tuning::flyby::slingshotMaxSpeedScale - 1.0);
}

double flybyCompletionBonusScale(const FlybyRunState& flyby)
{
    const double completionWindow = std::max(0.01, flyby.durationSeconds - tuning::flyby::minimumFinishSeconds);
    const double remainingWindow = std::clamp(flyby.durationSeconds - flyby.elapsedSeconds, 0.0, completionWindow);
    const double fastShare = std::clamp(remainingWindow / completionWindow, 0.0, 1.0);
    return 1.0 + fastShare * (tuning::flyby::completionRewardMaxScale - 1.0);
}

void populateFlybyRewardPreview(FlybyRunState& flyby, const Destination* destination)
{
    const bool existingSlingshotAwarded = flyby.slingshotAwarded;
    const double existingFuelBoost = flyby.slingshotFuelBoost;
    const double existingSpeedBoost = flyby.slingshotSpeedBoost;
    const double existingSpeedScale = flyby.slingshotSpeedScale;

    flyby.rewardCredits = 0.0;
    flyby.blueprintGain = 0;
    flyby.rewardBonusScale = 1.0;
    flyby.slingshotAwarded = false;
    flyby.slingshotFuelBoost = 0.0;
    flyby.slingshotSpeedBoost = 0.0;
    flyby.slingshotSpeedScale = 1.0;

    if (flyby.result == FlybyGrade::Perfect) {
        flyby.slingshotAwarded = true;
        if (existingSlingshotAwarded) {
            flyby.slingshotSpeedScale = existingSpeedScale;
            flyby.slingshotFuelBoost = existingFuelBoost;
            flyby.slingshotSpeedBoost = existingSpeedBoost;
        } else {
            flyby.slingshotSpeedScale = flybySpeedScale(flyby);
            flyby.slingshotFuelBoost = tuning::flyby::slingshotFuelBoost * flyby.slingshotSpeedScale;
            flyby.slingshotSpeedBoost = tuning::flyby::slingshotSpeedBoost * flyby.slingshotSpeedScale;
        }
    }

    if (flyby.result == FlybyGrade::Miss || flyby.result == FlybyGrade::Active || destination == nullptr) {
        return;
    }

    const double baseReward = flybyBaseReward(*destination);
    flyby.rewardBonusScale = flybyCompletionBonusScale(flyby);
    flyby.rewardCredits = (flyby.result == FlybyGrade::Perfect
        ? baseReward * tuning::flyby::perfectRewardMultiplier
        : baseReward) * flyby.rewardBonusScale;
    flyby.blueprintGain = tuning::flyby::goodBlueprintGain;
}

void pushFlybyTrailPoint(FlybyRunState& flyby, double x, double y)
{
    constexpr std::size_t maxTrailPoints = 96;
    if (!flyby.trailPoints.empty()) {
        const FlybyTrailPoint& last = flyby.trailPoints.back();
        if (std::hypot(last.x - x, last.y - y) < 0.012) {
            return;
        }
    }
    flyby.trailPoints.push_back({x, y});
    if (flyby.trailPoints.size() > maxTrailPoints) {
        flyby.trailPoints.erase(flyby.trailPoints.begin());
    }
}

} // namespace

bool destinationSupportsResearch(const Destination& destination)
{
    return destination.tier >= tuning::research::firstResearchTier;
}

bool destinationSupportsSurface(const Destination& destination)
{
    return destination.tier >= 1;
}

bool destinationAllowsEnemyEncounters(const Destination& destination)
{
    return destination.tier >= tuning::research::enemyEncounterTier;
}

int destinationHistoryValue(const std::vector<int>& values, const ContentCatalog& catalog, std::string_view destinationId)
{
    const int index = destinationIndexForId(catalog, destinationId);
    if (index < 0 || index >= static_cast<int>(values.size())) {
        return 0;
    }
    return values[static_cast<std::size_t>(index)];
}

bool shouldOpenArrivalOps(const LaunchOutcome& outcome, const ContentCatalog& catalog)
{
    if (outcome.type != LaunchResultType::MissionComplete) {
        return false;
    }

    const Destination* destination = catalog.findDestination(outcome.destinationId);
    return destination != nullptr
        && destination->tier >= 1
        && (outcome.frontierTransfer || outcome.recoveryMethod == RecoveryMethod::TransferArrival);
}

bool shouldOpenPostArrivalPhases(const LaunchOutcome& outcome, const ContentCatalog& catalog)
{
    if (outcome.type != LaunchResultType::MissionComplete) {
        return false;
    }

    const Destination* destination = catalog.findDestination(outcome.destinationId);
    return destination != nullptr
        && destinationSupportsResearch(*destination)
        && (outcome.frontierTransfer || outcome.recoveryMethod == RecoveryMethod::TransferArrival);
}

bool canRunArrivalFlyby(const GameState& state, const ContentCatalog& catalog)
{
    return currentResearchDestination(state, catalog) != nullptr;
}

bool canEnterArrivalOrbit(const GameState& state, const ContentCatalog& catalog)
{
    const Destination* destination = currentResearchDestination(state, catalog);
    if (destination == nullptr) {
        return false;
    }
    if (!isMoonTutorialLanding(*destination)) {
        return true;
    }
    return destinationHistoryValue(state.meta.destinationFlybys, catalog, destination->id) > 0;
}

bool canAttemptArrivalLanding(const GameState& state, const ContentCatalog& catalog)
{
    const Destination* destination = currentResearchDestination(state, catalog);
    if (destination == nullptr || !destinationSupportsSurface(*destination)) {
        return false;
    }
    if (!isMoonTutorialLanding(*destination)) {
        return true;
    }
    return destinationHistoryValue(state.meta.destinationFlybys, catalog, destination->id) > 0
        && destinationHistoryValue(state.meta.destinationOrbits, catalog, destination->id) > 0;
}

std::string arrivalOperationBlockReason(const GameState& state, const ContentCatalog& catalog, std::string_view operation)
{
    const Destination* destination = currentResearchDestination(state, catalog);
    if (destination == nullptr || !isMoonTutorialLanding(*destination)) {
        return {};
    }
    if (operation == "orbit" && !canEnterArrivalOrbit(state, catalog)) {
        return std::string(text::status::moonFlybyRequired);
    }
    if (operation == "landing" && destinationHistoryValue(state.meta.destinationFlybys, catalog, destination->id) <= 0) {
        return std::string(text::status::moonFlybyRequired);
    }
    if (operation == "landing" && destinationHistoryValue(state.meta.destinationOrbits, catalog, destination->id) <= 0) {
        return std::string(text::status::moonOrbitRequired);
    }
    return {};
}

void clearResearchAndExpeditionState(GameState& state)
{
    state.run.researchProjectIds = {};
    state.run.arrivalOps = {};
    state.run.flyby = {};
    state.run.surfaceExpedition = {};
}

void startArrivalOps(GameState& state, const LaunchOutcome& outcome)
{
    state.run.arrivalOps = {true, outcome.destinationId};
}

void completeArrivalFlyby(GameState& state, const ContentCatalog& catalog)
{
    applyFlybyReward(state, catalog, FlybyGrade::Good);
    state.run.arrivalOps = {};
}

void startArrivalFlybyRun(GameState& state, const ContentCatalog& catalog)
{
    const Destination* destination = currentResearchDestination(state, catalog);
    if (destination == nullptr || !canRunArrivalFlyby(state, catalog)) {
        return;
    }

    FlybyRunState flyby;
    flyby.active = true;
    flyby.destinationId = destination->id;
    flyby.durationSeconds = tuning::flyby::durationSeconds;
    flyby.shipX = tuning::flyby::startX;
    flyby.shipY = tuning::flyby::startY;
    flyby.velocityX = tuning::flyby::startVelocityX;
    flyby.velocityY = tuning::flyby::startVelocityY;
    flyby.gravityStrength = flybyGravityForDestination(*destination);
    flyby.planetColliderRadius = flybyPlanetColliderRadius(*destination);
    const auto [scoreX, scoreY] = flybyShipNosePoint(flyby.shipX, flyby.shipY, flyby.velocityX, flyby.velocityY);
    flyby.pathProgress = nearestFlybyPathSample(scoreX, scoreY).progress;
    flyby.currentZone = flybyZoneAt(scoreX, scoreY);
    flyby.worstZone = flyby.currentZone;
    pushFlybyTrailPoint(flyby, flyby.shipX, flyby.shipY);
    state.run.flyby = flyby;
    state.run.arrivalOps = {true, destination->id};
    state.screen = Screen::Flyby;
}

void setFlybyMove(GameState& state, double xAxis, double yAxis)
{
    if (!state.run.flyby.active || state.run.flyby.completed) {
        return;
    }
    state.run.flyby.inputX = std::clamp(xAxis, -1.0, 1.0);
    state.run.flyby.inputY = std::clamp(yAxis, -1.0, 1.0);
}

FlybyGrade flybyGrade(const FlybyRunState& flyby)
{
    if (!flyby.completed) {
        return FlybyGrade::Active;
    }
    if (flyby.collidedWithBody) {
        return FlybyGrade::Miss;
    }
    if (flyby.worstZone <= 0) {
        return FlybyGrade::Miss;
    }
    if (flyby.pathProgress < tuning::flyby::finishProgress) {
        return FlybyGrade::Miss;
    }
    if (flyby.worstZone >= 2) {
        return FlybyGrade::Perfect;
    }
    if (flyby.worstZone >= 1) {
        return FlybyGrade::Good;
    }
    return FlybyGrade::Miss;
}

void updateFlybyRun(GameState& state, double deltaSeconds)
{
    FlybyRunState& flyby = state.run.flyby;
    if (!flyby.active || flyby.completed) {
        return;
    }

    const auto concludeFlybyImpact = [&]() {
        flyby.collidedWithBody = true;
        flyby.completed = true;
        flyby.result = FlybyGrade::Miss;
        flyby.velocityX = 0.0;
        flyby.velocityY = 0.0;
        flyby.inputX = 0.0;
        flyby.inputY = 0.0;
        state.run.shipDamage = std::clamp(
            state.run.shipDamage + tuning::flyby::impactHullDamage,
            0,
            tuning::damage::destroyedShipDamage);
    };
    if (flybyShipIntersectsPlanet(flyby)) {
        concludeFlybyImpact();
        return;
    }

    const double dt = std::clamp(deltaSeconds, 0.0, tuning::launch::maxFrameStepSeconds);
    const double previousX = flyby.shipX;
    const double previousY = flyby.shipY;
    const double previousVelocityX = flyby.velocityX;
    const double previousVelocityY = flyby.velocityY;
    double speed = std::hypot(flyby.velocityX, flyby.velocityY);
    double headingX = speed > 0.001 ? flyby.velocityX / speed : 1.0;
    double headingY = speed > 0.001 ? flyby.velocityY / speed : 0.0;

    const double turnRadians = std::clamp(flyby.inputX, -1.0, 1.0) * tuning::flyby::turnRateRadians * dt;
    if (std::abs(turnRadians) > 0.000001) {
        const double c = std::cos(turnRadians);
        const double s = std::sin(turnRadians);
        const double rotatedX = headingX * c - headingY * s;
        const double rotatedY = headingX * s + headingY * c;
        headingX = rotatedX;
        headingY = rotatedY;
    }

    const double throttle = std::clamp(flyby.inputY, -1.0, 1.0);
    if (throttle > 0.0) {
        speed += throttle * tuning::flyby::thrustAcceleration * dt;
    } else if (throttle < 0.0) {
        speed += throttle * tuning::flyby::brakeAcceleration * dt;
    }
    speed = std::clamp(speed, tuning::flyby::minSpeed, tuning::flyby::maxSpeed);
    flyby.velocityX = headingX * speed;
    flyby.velocityY = headingY * speed;

    const double gravityDx = tuning::flyby::destinationX - flyby.shipX;
    const double gravityDy = tuning::flyby::destinationY - flyby.shipY;
    const double gravityDistance = std::max(0.001, std::hypot(gravityDx, gravityDy));
    const double gravityAcceleration = std::min(
        tuning::flyby::maxGravityAcceleration,
        flyby.gravityStrength / (gravityDistance * gravityDistance + tuning::flyby::gravitySoftening));
    flyby.velocityX += (gravityDx / gravityDistance) * gravityAcceleration * dt;
    flyby.velocityY += (gravityDy / gravityDistance) * gravityAcceleration * dt;

    const double drag = std::max(0.0, 1.0 - tuning::flyby::driftDrag * dt);
    flyby.velocityX *= drag;
    flyby.velocityY *= drag;
    const double postGravitySpeed = std::hypot(flyby.velocityX, flyby.velocityY);
    if (postGravitySpeed > tuning::flyby::maxSpeed) {
        const double limit = tuning::flyby::maxSpeed / postGravitySpeed;
        flyby.velocityX *= limit;
        flyby.velocityY *= limit;
    }

    flyby.shipX += flyby.velocityX * dt;
    flyby.shipY += flyby.velocityY * dt;
    pushFlybyTrailPoint(flyby, flyby.shipX, flyby.shipY);

    const auto [previousScoreX, previousScoreY] = flybyShipNosePoint(previousX, previousY, previousVelocityX, previousVelocityY);
    const auto [currentScoreX, currentScoreY] = flybyShipNosePoint(flyby.shipX, flyby.shipY, flyby.velocityX, flyby.velocityY);
    const FlybyTravelSample travelSample = sampleFlybyTravel(previousScoreX, previousScoreY, currentScoreX, currentScoreY);
    flyby.pathProgress = std::max(flyby.pathProgress, travelSample.progress);
    flyby.worstZone = std::min(flyby.worstZone, travelSample.worstZone);
    flyby.currentZone = travelSample.worstZone;
    if (flyby.currentZone >= 2) {
        flyby.perfectSeconds += dt;
        flyby.currentMissStreak = 0.0;
    } else if (flyby.currentZone == 1) {
        flyby.goodSeconds += dt;
        flyby.currentMissStreak = 0.0;
    } else {
        flyby.missSeconds += dt;
        flyby.currentMissStreak += dt;
        flyby.longestMissStreak = std::max(flyby.longestMissStreak, flyby.currentMissStreak);
    }

    flyby.elapsedSeconds += dt;
    if (flybyShipIntersectsPlanet(flyby)) {
        concludeFlybyImpact();
        flyby.missSeconds += dt;
        return;
    }

    if (flyby.currentZone <= 0) {
        flyby.completed = true;
        flyby.result = FlybyGrade::Miss;
        flyby.velocityX = 0.0;
        flyby.velocityY = 0.0;
        flyby.inputX = 0.0;
        flyby.inputY = 0.0;
        return;
    }

    if (travelSample.finished) {
        flyby.elapsedSeconds = std::min(flyby.elapsedSeconds, flyby.durationSeconds);
        flyby.completed = true;
        flyby.result = flybyGrade(flyby);
        populateFlybyRewardPreview(flyby, nullptr);
        flyby.velocityX = 0.0;
        flyby.velocityY = 0.0;
        flyby.inputX = 0.0;
        flyby.inputY = 0.0;
        return;
    }

    const double maxX = std::max(0.35, 1.0 + tuning::flyby::boundaryPadding);
    const double maxY = std::max(0.35, 1.0 + tuning::flyby::boundaryPadding);
    if (flyby.shipX < -maxX || flyby.shipX > maxX || flyby.shipY < -maxY || flyby.shipY > maxY) {
        flyby.shipX = std::clamp(flyby.shipX, -maxX, maxX);
        flyby.shipY = std::clamp(flyby.shipY, -maxY, maxY);
        flyby.completed = true;
        flyby.result = FlybyGrade::Miss;
        flyby.worstZone = 0;
        flyby.currentZone = 0;
        flyby.velocityX = 0.0;
        flyby.velocityY = 0.0;
        flyby.inputX = 0.0;
        flyby.inputY = 0.0;
        flyby.missSeconds += dt;
        return;
    }

    if (flyby.elapsedSeconds >= flyby.durationSeconds) {
        flyby.elapsedSeconds = flyby.durationSeconds;
        flyby.completed = true;
        flyby.result = FlybyGrade::Miss;
        flyby.velocityX = 0.0;
        flyby.velocityY = 0.0;
        flyby.inputX = 0.0;
        flyby.inputY = 0.0;
    }
}

void applyFlybyReward(GameState& state, const ContentCatalog& catalog, FlybyGrade grade)
{
    if (grade == FlybyGrade::Active || grade == FlybyGrade::Miss) {
        return;
    }

    const Destination* destination = currentResearchDestination(state, catalog);
    if (destination == nullptr && !state.run.flyby.destinationId.empty()) {
        destination = catalog.findDestination(state.run.flyby.destinationId);
    }
    if (destination == nullptr) {
        return;
    }

    addDestinationHistoryValue(state.meta.destinationFlybys, catalog, destination->id);
    populateFlybyRewardPreview(state.run.flyby, destination);
    const int blueprintGain = state.run.flyby.blueprintGain;
    const double reward = state.run.flyby.rewardCredits;
    state.meta.blueprintProgress += blueprintGain;
    state.run.credits += reward;
    if (grade == FlybyGrade::Perfect) {
        state.run.nextLaunchFuelBoost = std::max(state.run.nextLaunchFuelBoost, state.run.flyby.slingshotFuelBoost);
        state.run.nextLaunchSpeedBoost = std::max(state.run.nextLaunchSpeedBoost, state.run.flyby.slingshotSpeedBoost);
    }
    unlockFromBlueprints(state);
}

void completeFlybyRun(GameState& state, const ContentCatalog& catalog)
{
    if (!state.run.flyby.active || !state.run.flyby.completed) {
        return;
    }

    FlybyRunState flyby = state.run.flyby;
    const FlybyGrade grade = flyby.result == FlybyGrade::Active ? flybyGrade(flyby) : flyby.result;
    switch (grade) {
    case FlybyGrade::Miss:
        state.meta.totalFlybyMisses += 1;
        break;
    case FlybyGrade::Good:
        state.meta.totalFlybyGoods += 1;
        break;
    case FlybyGrade::Perfect:
        state.meta.totalFlybyPerfects += 1;
        break;
    case FlybyGrade::Active:
        break;
    }
    applyFlybyReward(state, catalog, grade);
    const Destination* destination = catalog.findDestination(flyby.destinationId);
    state.run.arrivalOps = {true, destination == nullptr ? flyby.destinationId : destination->id};
    state.run.flyby = {};
    state.screen = Screen::ArrivalOps;
}

void abortFlybyRun(GameState& state)
{
    if (!state.run.flyby.active || state.run.flyby.completed) {
        return;
    }

    const std::string destinationId = state.run.flyby.destinationId;
    state.run.arrivalOps = {true, destinationId};
    state.run.flyby = {};
    state.screen = Screen::ArrivalOps;
}

void acknowledgeFlybyResult(GameState& state)
{
    if (!state.run.flyby.active || !state.run.flyby.completed) {
        return;
    }
    state.run.arrivalOps = {true, state.run.flyby.destinationId};
}

void completeArrivalOrbit(GameState& state, const ContentCatalog& catalog)
{
    const Destination* destination = currentResearchDestination(state, catalog);
    if (destination == nullptr) {
        return;
    }
    addDestinationHistoryValue(state.meta.destinationOrbits, catalog, destination->id);
    state.meta.blueprintProgress += destinationSupportsResearch(*destination) ? 2 : 1;
    state.run.credits += std::max(18.0, destination->baseReward * 0.55);
    unlockFromBlueprints(state);
    state.run.arrivalOps = {};
}

void generateResearchProjects(GameState& state, const ContentCatalog& catalog, Random& rng)
{
    state.run.researchProjectIds = {};
    const Destination* destination = currentResearchDestination(state, catalog);
    if (destination == nullptr || !destinationSupportsResearch(*destination)) {
        return;
    }

    std::vector<const ResearchProject*> available;
    for (const ResearchProject& project : catalog.researchProjects) {
        if (projectUnlockedForDestination(project, state.meta, *destination)) {
            available.push_back(&project);
        }
    }

    for (std::size_t slot = 0; slot < state.run.researchProjectIds.size() && !available.empty(); ++slot) {
        const int picked = rng.rangeInt(0, static_cast<int>(available.size()) - 1);
        state.run.researchProjectIds[slot] = available[static_cast<std::size_t>(picked)]->id;
        available.erase(available.begin() + picked);
    }
}

void addMaterials(MaterialInventory& owned, const MaterialInventory& delta)
{
    owned.common = std::max(0, owned.common + delta.common);
    owned.rare = std::max(0, owned.rare + delta.rare);
    owned.exotic = std::max(0, owned.exotic + delta.exotic);
}

int identifiedArtifactCount(const MetaProgress& meta)
{
    return static_cast<int>(std::count_if(meta.artifacts.begin(), meta.artifacts.end(), [](const ArtifactRecord& artifact) {
        return artifact.identified;
    }));
}

int researchFacilityBlueprintBonus(const MetaProgress& meta)
{
    return hasUnlock(meta, content::unlock::analysisLab) ? tuning::research::analysisLabBlueprintBonus : 0;
}

int artifactInsightBlueprintBonus(const MetaProgress& meta)
{
    return std::min(
        tuning::research::artifactInsightBlueprintMaximum,
        identifiedArtifactCount(meta) * tuning::research::artifactInsightBlueprintPerIdentified);
}

int researchBlueprintGain(const MetaProgress& meta, const ResearchProject& project)
{
    return project.blueprintGain + researchFacilityBlueprintBonus(meta) + artifactInsightBlueprintBonus(meta);
}

SurfaceToolEffects surfaceToolEffects(const MetaProgress& meta)
{
    SurfaceToolEffects effects;
    if (hasUnlock(meta, content::unlock::surfaceProbes)) {
        effects.supplyBonus += tuning::research::probeSupplyBonus;
        effects.surveyCommonBonus += tuning::research::probeSurveyCommonBonus;
    }
    if (hasUnlock(meta, content::unlock::surfaceDrills)) {
        effects.mineCommonBonus += tuning::research::drillMineCommonBonus;
        effects.mineRareChanceBonus += tuning::research::drillRareChanceBonus;
    }
    if (hasUnlock(meta, content::unlock::cargoRigs)) {
        effects.extractionRiskRelief += tuning::research::cargoRigExtractionRiskRelief;
        effects.cargoRiskRelief += tuning::research::cargoRigCargoRiskRelief;
    }
    if (hasUnlock(meta, content::unlock::perimeterDrones)) {
        effects.enemyEncounterRelief += tuning::research::perimeterDroneEnemyRelief;
    }
    return effects;
}

SurfaceCrewEffects surfaceCrewEffects(const GameState& state)
{
    SurfaceCrewEffects effects;
    const Astronaut* astronaut = activeAstronaut(state);
    if (astronaut == nullptr) {
        effects.summary = "No field specialist assigned.";
        return effects;
    }

    const int training = std::max(0, effectiveTrainingLevel(*astronaut));
    const double trainedRiskRelief = static_cast<double>(training) * 0.003;

    if (astronaut->trait == tuning::traits::beastMode) {
        effects.supplyBonus = 1;
        effects.hazardRelief = std::min(0.08, 0.025 + trainedRiskRelief);
        effects.extractionRiskRelief = std::min(0.10, 0.035 + static_cast<double>(training) * 0.004);
        effects.summary = "Capybara survival: extra action kits, lower hazard, and safer extraction.";
    } else if (astronaut->trait == tuning::traits::hardReboot) {
        effects.hazardRelief = std::min(0.09, 0.040 + static_cast<double>(training) * 0.004);
        effects.summary = "Beaver resilience: fewer equipment scares and steadier surface operations.";
    } else if (astronaut->trait == tuning::traits::outtaHere) {
        effects.extractionRiskRelief = std::min(0.11, 0.055 + static_cast<double>(training) * 0.005);
        effects.summary = "Fox navigation: cleaner extraction routes when the payload gets heavy.";
    } else if (astronaut->trait == tuning::traits::deepFocus) {
        effects.surveyCommonBonus = 1 + training / 5;
        effects.artifactChanceBonus = std::min(0.12, 0.030 + static_cast<double>(training) * 0.006);
        effects.summary = "Prairie Dog scouting: better surveys and sharper reads on buried anomalies.";
    } else if (astronaut->trait == tuning::traits::rummageSale) {
        effects.mineCommonBonus = training >= 4 ? 1 : 0;
        effects.mineRareChanceBonus = std::min(0.22, 0.120 + static_cast<double>(training) * 0.010);
        effects.summary = "Squirrel hoarding: better odds of rare materials while mining.";
    } else if (astronaut->trait == tuning::traits::phaseShift) {
        effects.supplyBonus = 1;
        effects.hazardRelief = std::min(0.08, 0.025 + static_cast<double>(training) * 0.005);
        effects.summary = "Chipmunk exploration: faster site work with fewer field hazards.";
    } else if (astronaut->trait == tuning::traits::fieldInstincts) {
        effects.extractionRiskRelief = std::min(0.06, 0.020 + trainedRiskRelief);
        effects.summary = "Field instincts: modestly safer extractions.";
    } else {
        effects.summary = astronaut->background.empty() ? astronaut->trait : astronaut->background;
    }

    return effects;
}

SurfaceSiteProfileEffects surfaceSiteProfileEffects(SurfaceSiteProfile profile)
{
    SurfaceSiteProfileEffects effects;
    switch (profile) {
    case SurfaceSiteProfile::SurveyBasin:
        effects.surveyCommonBonus += tuning::research::siteSurveyBasinSurveyBonus;
        effects.hazardDelta -= tuning::research::siteSurveyBasinHazardRelief;
        break;
    case SurfaceSiteProfile::OreShelf:
        effects.mineCommonBonus += tuning::research::siteOreShelfMineBonus;
        effects.mineRareChanceBonus += tuning::research::siteOreShelfRareChanceBonus;
        effects.hazardDelta += tuning::research::siteOreShelfHazardIncrease;
        break;
    case SurfaceSiteProfile::FractureField:
        effects.hazardDelta += tuning::research::siteFractureFieldHazardIncrease;
        effects.extractionRiskDelta += tuning::research::siteFractureFieldExtractionRiskIncrease;
        effects.artifactChanceBonus += tuning::research::siteFractureFieldArtifactChanceBonus;
        break;
    }
    return effects;
}

SurfaceUpgradeEffects surfaceUpgradeEffects(const GameState& state, const ContentCatalog& catalog)
{
    SurfaceUpgradeEffects effects;
    for (const std::string& upgradeId : state.run.surfaceUpgradeIds) {
        const SurfaceUpgrade* upgrade = catalog.findSurfaceUpgrade(upgradeId);
        if (upgrade == nullptr) {
            continue;
        }
        effects.drillPower += upgrade->stats.drillPower;
        effects.drillCooling += upgrade->stats.drillCooling;
        effects.drillDurability += upgrade->stats.drillDurability;
        effects.hardRockBounceRelief += upgrade->stats.hardRockBounceRelief;
        effects.oreYieldChance += upgrade->stats.oreYieldChance;
        effects.scannerRadius += upgrade->stats.scannerRadius;
        effects.hazardRelief += upgrade->stats.hazardRelief;
        effects.droneSpeed += upgrade->stats.droneSpeed;
        effects.oxygenSeconds += upgrade->stats.oxygenSeconds;
        effects.extractionRiskRelief += upgrade->stats.extractionRiskRelief;
        effects.names.push_back(upgrade->name);
    }
    effects.hardRockBounceRelief = std::clamp(effects.hardRockBounceRelief, 0.0, 0.35);
    return effects;
}

bool droneBayUnlocked(const GameState& state)
{
    return hasUnlock(state.meta, content::unlock::droneBay);
}

MaterialInventory droneSlotUpgradeCost(int nextSlot)
{
    switch (nextSlot) {
    case 2:
        return {.common = 4};
    case 3:
        return {.common = 6, .rare = 2};
    case 4:
        return {.rare = 5};
    case 5:
        return {.rare = 7, .exotic = 2};
    case 6:
        return {.rare = 10, .exotic = 4};
    default:
        return {};
    }
}

void ensureDroneBayState(GameState& state, const ContentCatalog& catalog)
{
    if (!droneBayUnlocked(state)) {
        state.meta.droneBaySlots = 0;
        state.meta.ownedDroneIds.clear();
        state.meta.equippedDroneIds.clear();
        return;
    }

    state.meta.droneBaySlots = std::clamp(state.meta.droneBaySlots <= 0 ? 1 : state.meta.droneBaySlots, 1, 6);

    const std::array<std::string_view, 4> starterDrones {
        content::drone::miningDrone,
        content::drone::resourceDrone,
        content::drone::surveyDrone,
        content::drone::stabilizerDrone
    };
    for (std::string_view droneId : starterDrones) {
        if (catalog.findMiniDrone(droneId) != nullptr
            && std::find(state.meta.ownedDroneIds.begin(), state.meta.ownedDroneIds.end(), droneId) == state.meta.ownedDroneIds.end()) {
            state.meta.ownedDroneIds.emplace_back(droneId);
        }
    }
    for (const MiniDrone& drone : catalog.miniDrones) {
        if (isMiniDroneUnlocked(state.meta, drone)
            && std::find(state.meta.ownedDroneIds.begin(), state.meta.ownedDroneIds.end(), drone.id) == state.meta.ownedDroneIds.end()) {
            state.meta.ownedDroneIds.push_back(drone.id);
        }
    }

    state.meta.ownedDroneIds.erase(
        std::remove_if(
            state.meta.ownedDroneIds.begin(),
            state.meta.ownedDroneIds.end(),
            [&](const std::string& id) {
                const MiniDrone* drone = catalog.findMiniDrone(id);
                return drone == nullptr || !isMiniDroneUnlocked(state.meta, *drone);
            }),
        state.meta.ownedDroneIds.end());

    state.meta.equippedDroneIds.erase(
        std::remove_if(
            state.meta.equippedDroneIds.begin(),
            state.meta.equippedDroneIds.end(),
            [&](const std::string& id) {
                return std::find(state.meta.ownedDroneIds.begin(), state.meta.ownedDroneIds.end(), id) == state.meta.ownedDroneIds.end();
            }),
        state.meta.equippedDroneIds.end());
    if (state.meta.equippedDroneIds.size() > static_cast<std::size_t>(state.meta.droneBaySlots)) {
        state.meta.equippedDroneIds.resize(static_cast<std::size_t>(state.meta.droneBaySlots));
    }
}

bool canUpgradeDroneSlot(const GameState& state)
{
    if (!droneBayUnlocked(state) || state.meta.droneBaySlots >= 6) {
        return false;
    }
    return canAffordMaterials(state.meta.materials, droneSlotUpgradeCost(state.meta.droneBaySlots + 1));
}

bool upgradeDroneSlot(GameState& state, const ContentCatalog& catalog)
{
    ensureDroneBayState(state, catalog);
    if (!droneBayUnlocked(state) || state.meta.droneBaySlots >= 6) {
        state.statusLine = "Drone Bay is already at maximum capacity.";
        return false;
    }

    const MaterialInventory cost = droneSlotUpgradeCost(state.meta.droneBaySlots + 1);
    if (!spendMaterials(state.meta.materials, cost)) {
        state.statusLine = "Recovered materials are short for the next Drone Bay slot.";
        return false;
    }

    state.meta.droneBaySlots += 1;
    state.statusLine = "Drone Bay expanded to " + std::to_string(state.meta.droneBaySlots) + " slots.";
    return true;
}

bool equipMiniDrone(GameState& state, const ContentCatalog& catalog, int index)
{
    ensureDroneBayState(state, catalog);
    if (!droneBayUnlocked(state) || index < 0 || index >= static_cast<int>(catalog.miniDrones.size())) {
        return false;
    }

    const MiniDrone& drone = catalog.miniDrones[static_cast<std::size_t>(index)];
    const bool owned = std::find(state.meta.ownedDroneIds.begin(), state.meta.ownedDroneIds.end(), drone.id) != state.meta.ownedDroneIds.end();
    if (!owned || !isMiniDroneUnlocked(state.meta, drone)) {
        state.statusLine = drone.name + " is still locked.";
        return false;
    }

    auto equipped = std::find(state.meta.equippedDroneIds.begin(), state.meta.equippedDroneIds.end(), drone.id);
    if (equipped != state.meta.equippedDroneIds.end()) {
        state.meta.equippedDroneIds.erase(equipped);
        state.statusLine = drone.name + " returned to standby.";
        return true;
    }

    if (state.meta.equippedDroneIds.size() >= static_cast<std::size_t>(state.meta.droneBaySlots)) {
        state.statusLine = "Drone slots full. Unequip a drone or expand the bay.";
        return false;
    }

    state.meta.equippedDroneIds.push_back(drone.id);
    state.statusLine = drone.name + " assigned to the next mining run.";
    return true;
}

MiniDroneLoadoutEffects miniDroneLoadoutEffects(const GameState& state, const ContentCatalog& catalog)
{
    MiniDroneLoadoutEffects effects;
    if (!droneBayUnlocked(state)) {
        return effects;
    }

    for (const std::string& droneId : state.meta.equippedDroneIds) {
        const MiniDrone* drone = catalog.findMiniDrone(droneId);
        if (drone == nullptr || !isMiniDroneUnlocked(state.meta, *drone)) {
            continue;
        }
        effects.passiveMiningRate += drone->stats.passiveMiningRate;
        effects.oxygenSeconds += drone->stats.oxygenSeconds;
        effects.scannerRadius += drone->stats.scannerRadius;
        effects.drillIntegrityRelief += drone->stats.drillIntegrityRelief;
        effects.hardRockBounceRelief += drone->stats.hardRockBounceRelief;
        effects.extractionRiskRelief += drone->stats.extractionRiskRelief;
        effects.enemyEncounterRelief += drone->stats.enemyEncounterRelief;
        effects.names.push_back(drone->name);
    }

    effects.passiveMiningRate = std::clamp(effects.passiveMiningRate, 0.0, 0.40);
    effects.scannerRadius = std::clamp(effects.scannerRadius, 0.0, 5.0);
    effects.drillIntegrityRelief = std::clamp(effects.drillIntegrityRelief, 0.0, 0.35);
    effects.hardRockBounceRelief = std::clamp(effects.hardRockBounceRelief, 0.0, 0.55);
    effects.extractionRiskRelief = std::clamp(effects.extractionRiskRelief, 0.0, 0.08);
    effects.enemyEncounterRelief = std::clamp(effects.enemyEncounterRelief, 0.0, 0.18);
    return effects;
}

std::string_view surfaceSiteProfileName(SurfaceSiteProfile profile)
{
    switch (profile) {
    case SurfaceSiteProfile::SurveyBasin:
        return text::panel::surfaceSites::surveyBasin;
    case SurfaceSiteProfile::OreShelf:
        return text::panel::surfaceSites::oreShelf;
    case SurfaceSiteProfile::FractureField:
        return text::panel::surfaceSites::fractureField;
    }
    return text::panel::surfaceSites::surveyBasin;
}

std::string_view surfaceSiteProfileDetail(SurfaceSiteProfile profile)
{
    switch (profile) {
    case SurfaceSiteProfile::SurveyBasin:
        return text::panel::surfaceSites::surveyBasinDetail;
    case SurfaceSiteProfile::OreShelf:
        return text::panel::surfaceSites::oreShelfDetail;
    case SurfaceSiteProfile::FractureField:
        return text::panel::surfaceSites::fractureFieldDetail;
    }
    return text::panel::surfaceSites::surveyBasinDetail;
}

std::string researchOutcomeSummary(const ResearchOutcome& outcome)
{
    if (!outcome.completed) {
        return std::string(text::status::researchCompleted);
    }

    std::vector<std::string> parts;
    parts.push_back(std::string(text::status::researchCompleted));
    if (outcome.blueprintGain > 0) {
        parts.push_back(text::panel::blueprintGain(outcome.blueprintGain));
    }
    if (outcome.materialCost.common > 0 || outcome.materialCost.rare > 0 || outcome.materialCost.exotic > 0) {
        parts.push_back("Spent " + text::panel::materialSummary(
            outcome.materialCost.common,
            outcome.materialCost.rare,
            outcome.materialCost.exotic));
    }
    if (outcome.unlockedReward && !outcome.rewardUnlockKey.empty()) {
        parts.push_back(text::panel::unlocksFamily(unlockDisplayName(outcome.rewardUnlockKey)));
    }
    if (outcome.identifiedArtifact) {
        parts.push_back("Decoded " + outcome.artifactId);
    }
    return joinParts(parts);
}

std::string surfaceActionSummary(const SurfaceActionOutcome& outcome)
{
    if (!outcome.applied) {
        return std::string(text::status::surfaceSupplyBlocked);
    }
    std::string status = outcome.message;
    if (outcome.hazardTriggered && !outcome.hazardMessage.empty()) {
        status += " " + outcome.hazardMessage;
    }
    if (outcome.eventType != SurfaceEventType::None && !outcome.eventMessage.empty()) {
        status += " " + outcome.eventMessage;
    }
    const std::string deltaSummary = surfaceDeltaSummary(outcome);
    if (!deltaSummary.empty()) {
        status += " (" + deltaSummary + ")";
    }
    return status;
}

ResearchOutcome completeResearchProject(GameState& state, const ContentCatalog& catalog, int index)
{
    ResearchOutcome outcome;
    if (index < 0 || index >= static_cast<int>(state.run.researchProjectIds.size())) {
        return outcome;
    }

    const std::string& projectId = state.run.researchProjectIds[static_cast<std::size_t>(index)];
    const ResearchProject* project = catalog.findResearchProject(projectId);
    const Destination* destination = currentResearchDestination(state, catalog);
    if (project == nullptr || destination == nullptr || !projectUnlockedForDestination(*project, state.meta, *destination)) {
        return outcome;
    }
    if (!spendMaterials(state.meta.materials, project->materialCost)) {
        return outcome;
    }

    const int blueprintGain = researchBlueprintGain(state.meta, *project);
    const bool rewardUnlockAvailable = !project->rewardUnlockKey.empty() && !hasUnlock(state.meta, project->rewardUnlockKey);
    if (rewardUnlockAvailable) {
        state.meta.unlockKeys.push_back(project->rewardUnlockKey);
    }
    state.meta.blueprintProgress += blueprintGain;
    unlockFromBlueprints(state);
    if (hasTag(project->tags, "artifact")) {
        if (ArtifactRecord* artifact = firstUnidentifiedArtifact(state)) {
            artifact->identified = true;
            outcome.identifiedArtifact = true;
            outcome.artifactId = artifact->id;
        }
    }
    state.run.researchProjectIds[static_cast<std::size_t>(index)].clear();

    outcome.completed = true;
    outcome.projectId = project->id;
    outcome.blueprintGain = blueprintGain;
    outcome.materialCost = project->materialCost;
    outcome.rewardUnlockKey = project->rewardUnlockKey;
    outcome.unlockedReward = rewardUnlockAvailable;
    ensureDroneBayState(state, catalog);
    return outcome;
}

void startSurfaceExpedition(GameState& state, const ContentCatalog& catalog, Random* rng)
{
    const Destination* destination = currentResearchDestination(state, catalog);
    if (destination == nullptr || !destinationSupportsSurface(*destination)) {
        state.run.surfaceExpedition = {};
        return;
    }

    SurfaceExpeditionState expedition;
    expedition.active = true;
    expedition.destinationId = destination->id;
    expedition.siteProfile = generatedSurfaceSiteProfile(state, *destination, rng);
    const SurfaceSiteProfileEffects site = surfaceSiteProfileEffects(expedition.siteProfile);
    const SurfaceCrewEffects crew = surfaceCrewEffects(state);
    const double baseHazard = tuning::research::baseHazard + destination->tier * tuning::research::hazardPerTier;
    const double reconPenalty = landingReconHazardPenalty(state, catalog, *destination);
    expedition.supply = tuning::research::baseSupply + destination->tier * tuning::research::supplyPerTier + surfaceToolEffects(state.meta).supplyBonus + crew.supplyBonus + site.supplyBonus;
    expedition.hazard = std::max(baseHazard + reconPenalty, baseHazard + site.hazardDelta + reconPenalty - crew.hazardRelief);
    expedition.enemyEncountersEnabled = destinationAllowsEnemyEncounters(*destination);
    addDestinationHistoryValue(state.meta.destinationLandings, catalog, destination->id);
    state.run.arrivalOps = {};
    appendSurfaceLog(expedition, std::string(surfaceSiteProfileName(expedition.siteProfile)) + ": " + std::string(surfaceSiteProfileDetail(expedition.siteProfile)));
    state.run.surfaceExpedition = expedition;
}

void generateSurfaceUpgradeOffers(GameState& state, const ContentCatalog& catalog, Random& rng)
{
    SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    if (!expedition.active || expedition.surfaceUpgradeOfferAvailable || expedition.surfaceUpgradeOffersSeen > 0) {
        return;
    }

    std::vector<const SurfaceUpgrade*> available;
    for (const SurfaceUpgrade& upgrade : catalog.surfaceUpgrades) {
        if (std::find(state.run.surfaceUpgradeIds.begin(), state.run.surfaceUpgradeIds.end(), upgrade.id) == state.run.surfaceUpgradeIds.end()) {
            available.push_back(&upgrade);
        }
    }

    expedition.surfaceUpgradeOfferIds = {};
    for (std::size_t slot = 0; slot < expedition.surfaceUpgradeOfferIds.size() && !available.empty(); ++slot) {
        const int picked = rng.rangeInt(0, static_cast<int>(available.size()) - 1);
        expedition.surfaceUpgradeOfferIds[slot] = available[static_cast<std::size_t>(picked)]->id;
        available.erase(available.begin() + picked);
    }

    expedition.surfaceUpgradeOfferAvailable = std::any_of(
        expedition.surfaceUpgradeOfferIds.begin(),
        expedition.surfaceUpgradeOfferIds.end(),
        [](const std::string& id) {
            return !id.empty();
        });
    if (expedition.surfaceUpgradeOfferAvailable) {
        expedition.surfaceUpgradeOffersSeen += 1;
    }
}

bool rerollSurfaceUpgradeOffers(GameState& state, const ContentCatalog& catalog, Random& rng)
{
    SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    if (!expedition.active || !expedition.surfaceUpgradeOfferAvailable) {
        return false;
    }

    const double cost = offerRerollCost(state);
    if (state.run.credits < cost) {
        state.statusLine = "Not enough mission credits to reroll the field-upgrade board.";
        return false;
    }

    std::vector<const SurfaceUpgrade*> available;
    for (const SurfaceUpgrade& upgrade : catalog.surfaceUpgrades) {
        if (std::find(state.run.surfaceUpgradeIds.begin(), state.run.surfaceUpgradeIds.end(), upgrade.id) == state.run.surfaceUpgradeIds.end()) {
            available.push_back(&upgrade);
        }
    }
    if (available.empty()) {
        return false;
    }

    state.run.credits -= cost;
    state.run.offerRerollsThisExpedition += 1;

    expedition.surfaceUpgradeOfferIds = {};
    for (std::size_t slot = 0; slot < expedition.surfaceUpgradeOfferIds.size() && !available.empty(); ++slot) {
        const int picked = rng.rangeInt(0, static_cast<int>(available.size()) - 1);
        expedition.surfaceUpgradeOfferIds[slot] = available[static_cast<std::size_t>(picked)]->id;
        available.erase(available.begin() + picked);
    }

    expedition.surfaceUpgradeOfferAvailable = std::any_of(
        expedition.surfaceUpgradeOfferIds.begin(),
        expedition.surfaceUpgradeOfferIds.end(),
        [](const std::string& id) {
            return !id.empty();
        });

    state.statusLine = "Field upgrade board rerolled. Next reroll costs " + std::to_string(static_cast<int>(offerRerollCost(state))) + " credits.";
    return expedition.surfaceUpgradeOfferAvailable;
}

bool chooseSurfaceUpgrade(GameState& state, const ContentCatalog& catalog, int index)
{
    SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    if (!expedition.active || !expedition.surfaceUpgradeOfferAvailable || index < 0 || index >= static_cast<int>(expedition.surfaceUpgradeOfferIds.size())) {
        return false;
    }

    const std::string upgradeId = expedition.surfaceUpgradeOfferIds[static_cast<std::size_t>(index)];
    const SurfaceUpgrade* upgrade = catalog.findSurfaceUpgrade(upgradeId);
    if (upgrade == nullptr) {
        return false;
    }

    state.run.surfaceUpgradeIds.push_back(upgrade->id);
    expedition.surfaceUpgradeOfferIds = {};
    expedition.surfaceUpgradeOfferAvailable = false;
    appendSurfaceLog(expedition, "Field upgrade installed: " + upgrade->name + ".");
    state.statusLine = "Field upgrade installed: " + upgrade->name + ".";
    return true;
}

double surfaceExtractionRisk(const GameState& state)
{
    const SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    if (!expedition.active) {
        return 0.0;
    }

    const SurfaceToolEffects tools = surfaceToolEffects(state.meta);
    const SurfaceCrewEffects crew = surfaceCrewEffects(state);
    const SurfaceSiteProfileEffects site = surfaceSiteProfileEffects(expedition.siteProfile);
    const SurfaceUpgradeEffects upgrades = surfaceUpgradeEffects(state, createDefaultContent());
    const MiniDroneLoadoutEffects drones = miniDroneLoadoutEffects(state, createDefaultContent());
    double risk = tuning::research::extractionRiskBase
        + expedition.hazard * tuning::research::extractionRiskHazardScale
        + static_cast<double>(std::max(0, expedition.cargo)) * std::max(0.0, tuning::research::extractionRiskCargoScale - tools.cargoRiskRelief)
        - tools.extractionRiskRelief
        - crew.extractionRiskRelief
        - upgrades.extractionRiskRelief
        - drones.extractionRiskRelief
        + site.extractionRiskDelta;
    if (expedition.supply <= 0) {
        risk += tuning::research::extractionRiskLowSupplyPenalty;
    }
    return std::clamp(risk, 0.0, tuning::research::extractionRiskMaximum);
}

double surfaceEnemyEncounterChance(const GameState& state)
{
    const SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    if (!expedition.active || !expedition.enemyEncountersEnabled) {
        return 0.0;
    }

    const SurfaceToolEffects tools = surfaceToolEffects(state.meta);
    const MiniDroneLoadoutEffects drones = miniDroneLoadoutEffects(state, createDefaultContent());
    return std::clamp(
        tuning::research::surfaceEnemyChanceBase
            + expedition.hazard * tuning::research::surfaceEnemyChanceHazardScale
            - tools.enemyEncounterRelief
            - drones.enemyEncounterRelief,
        0.0,
        tuning::research::surfaceEnemyChanceMaximum);
}

SurfaceActionOutcome surveySurfaceSite(GameState& state, Random& rng)
{
    SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    const double extractionRiskBefore = surfaceExtractionRisk(state);
    SurfaceActionOutcome outcome = spendSupply(expedition, tuning::research::surveySupplyCost);
    if (!outcome.applied) {
        return outcome;
    }

    const SurfaceToolEffects tools = surfaceToolEffects(state.meta);
    const SurfaceCrewEffects crew = surfaceCrewEffects(state);
    const SurfaceSiteProfileEffects site = surfaceSiteProfileEffects(expedition.siteProfile);
    const SurfaceUpgradeEffects upgrades = surfaceUpgradeEffects(state, createDefaultContent());
    const MaterialInventory gain {.common = tuning::research::surveyCommonGain + tools.surveyCommonBonus + crew.surveyCommonBonus + site.surveyCommonBonus};
    addMaterials(expedition.temporaryMaterials, gain);
    expedition.cargo += materialCargo(gain);
    outcome.materialDelta = gain;
    outcome.cargoDelta = materialCargo(gain);
    applySurfaceHazard(
        expedition,
        outcome,
        rng,
        tuning::research::surveyHazardChanceScale,
        (tools.surveyCommonBonus > 0 ? tuning::research::probeHazardRelief : 0.0) + crew.hazardRelief + upgrades.hazardRelief,
        text::status::surfaceDustHazard,
        tuning::research::dustHazardSupplyLoss,
        0,
        tuning::research::dustHazardIncrease);
    outcome.message = std::string(text::status::surfaceSurveyed);
    finalizeSurfaceAction(state, outcome, rng, extractionRiskBefore);
    return outcome;
}

SurfaceActionOutcome mineSurfaceDeposit(GameState& state, Random& rng)
{
    SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    const double extractionRiskBefore = surfaceExtractionRisk(state);
    SurfaceActionOutcome outcome = spendSupply(expedition, tuning::research::mineSupplyCost);
    if (!outcome.applied) {
        return outcome;
    }

    const SurfaceToolEffects tools = surfaceToolEffects(state.meta);
    const SurfaceCrewEffects crew = surfaceCrewEffects(state);
    const SurfaceSiteProfileEffects site = surfaceSiteProfileEffects(expedition.siteProfile);
    const SurfaceUpgradeEffects upgrades = surfaceUpgradeEffects(state, createDefaultContent());
    MaterialInventory gain {.common = tuning::research::mineCommonGain + tools.mineCommonBonus + crew.mineCommonBonus + site.mineCommonBonus};
    if (expedition.depth >= tuning::research::mineRareDepthThreshold || rng.chance(std::min(1.0, expedition.hazard + tools.mineRareChanceBonus + crew.mineRareChanceBonus + site.mineRareChanceBonus + upgrades.oreYieldChance))) {
        gain.rare += 1;
    }

    addMaterials(expedition.temporaryMaterials, gain);
    expedition.cargo += materialCargo(gain);
    outcome.materialDelta = gain;
    outcome.cargoDelta = materialCargo(gain);
    applySurfaceHazard(
        expedition,
        outcome,
        rng,
        tuning::research::mineHazardChanceScale,
        (tools.mineCommonBonus > 0 ? tuning::research::drillHazardRelief : 0.0) + crew.hazardRelief + upgrades.hazardRelief,
        text::status::surfaceDrillHazard,
        0,
        tuning::research::drillHazardCargoLoss,
        tuning::research::drillHazardIncrease);
    outcome.message = std::string(text::status::surfaceMined);
    finalizeSurfaceAction(state, outcome, rng, extractionRiskBefore);
    return outcome;
}

SurfaceActionOutcome pushSurfaceDeeper(GameState& state, Random& rng)
{
    SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    const double extractionRiskBefore = surfaceExtractionRisk(state);
    SurfaceActionOutcome outcome = spendSupply(expedition, tuning::research::pushSupplyCost);
    if (!outcome.applied) {
        return outcome;
    }

    expedition.depth += 1;
    expedition.hazard += tuning::research::hazardPerDepth;
    const SurfaceCrewEffects crew = surfaceCrewEffects(state);
    const SurfaceSiteProfileEffects site = surfaceSiteProfileEffects(expedition.siteProfile);
    const SurfaceUpgradeEffects upgrades = surfaceUpgradeEffects(state, createDefaultContent());
    if (expedition.depth >= tuning::research::artifactDepthThreshold && expedition.temporaryArtifacts.empty() && rng.chance(std::min(1.0, tuning::research::artifactChanceBase + crew.artifactChanceBonus + site.artifactChanceBonus))) {
        expedition.temporaryArtifacts.push_back({artifactId(expedition), expedition.destinationId, false});
        outcome.artifactFound = true;
        outcome.cargoDelta += 3;
        expedition.cargo += 3;
    }
    applySurfaceHazard(
        expedition,
        outcome,
        rng,
        tuning::research::pushHazardChanceScale,
        (surfaceToolEffects(state.meta).extractionRiskRelief > 0.0 ? tuning::research::cargoRigHazardRelief : 0.0) + crew.hazardRelief + upgrades.hazardRelief,
        text::status::surfaceTerrainHazard,
        tuning::research::pushHazardSupplyLoss,
        0,
        tuning::research::unstableTerrainHazardIncrease);
    outcome.message = std::string(text::status::surfacePushed);
    finalizeSurfaceAction(state, outcome, rng, extractionRiskBefore);
    return outcome;
}

SurfaceActionOutcome extractSurfacePayload(GameState& state, Random& rng)
{
    SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    SurfaceActionOutcome outcome;
    if (!expedition.active) {
        return outcome;
    }

    outcome.applied = true;
    outcome.extractionRisk = surfaceExtractionRisk(state);
    outcome.cargoRecovered = !rng.chance(outcome.extractionRisk);
    if (outcome.cargoRecovered) {
        addMaterials(state.meta.materials, expedition.temporaryMaterials);
        state.meta.artifacts.insert(state.meta.artifacts.end(), expedition.temporaryArtifacts.begin(), expedition.temporaryArtifacts.end());
        outcome.materialDelta = expedition.temporaryMaterials;
        outcome.artifactFound = !expedition.temporaryArtifacts.empty();
        outcome.message = std::string(text::status::surfaceExtracted);
    } else {
        const MaterialInventory recovered = halvedMaterials(expedition.temporaryMaterials);
        addMaterials(state.meta.materials, recovered);
        outcome.materialDelta = recovered;
        outcome.materialLost = {
            std::max(0, expedition.temporaryMaterials.common - recovered.common),
            std::max(0, expedition.temporaryMaterials.rare - recovered.rare),
            std::max(0, expedition.temporaryMaterials.exotic - recovered.exotic)
        };
        outcome.artifactsLost = static_cast<int>(expedition.temporaryArtifacts.size());
        outcome.message = std::string(text::status::surfaceExtractionRough);
    }

    expedition = {};
    return outcome;
}

} // namespace rocket
