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

void applyRecoveredArtifactRewards(GameState& state, std::vector<ArtifactRecord>& artifacts)
{
    for (ArtifactRecord& artifact : artifacts) {
        if (artifact.rewardApplied) {
            continue;
        }
        const double condition = std::clamp(artifact.condition, 0.0, 1.0);
        if (artifact.kind == ArtifactKind::Story) {
            state.meta.ark.repairProgress += tuning::mining::artifactStoryArkRepair;
            if (state.meta.ark.condition == ArkCondition::DamagedStranded) {
                state.meta.ark.hullDamage = std::max(0, state.meta.ark.hullDamage - static_cast<int>(std::ceil(tuning::mining::artifactStoryHullRepair * condition)));
            }
            artifact.rewardApplied = true;
            continue;
        }

        switch (artifact.rewardType) {
        case ArtifactRewardType::Credits:
            state.run.credits += std::ceil(tuning::mining::artifactCreditReward * std::max(0.25, condition));
            artifact.rewardApplied = true;
            break;
        case ArtifactRewardType::ArkFuel:
            state.meta.ark.fuelReserve += std::max(1, static_cast<int>(std::ceil(static_cast<double>(tuning::mining::artifactFuelReward) * condition)));
            artifact.rewardApplied = true;
            break;
        case ArtifactRewardType::BlueprintInsight:
            state.meta.blueprintProgress += std::max(1, static_cast<int>(std::ceil(static_cast<double>(tuning::mining::artifactBlueprintReward) * condition)));
            artifact.rewardApplied = true;
            break;
        case ArtifactRewardType::None:
            artifact.rewardApplied = true;
            break;
        }
    }
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
    addDelta(parts, outcome.fuelDelta, text::labels::sharedFuel);
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

double orbitPlanetRadius(const Destination& destination)
{
    return tuning::orbit::planetBaseRadius
        + static_cast<double>(std::min(5, std::max(0, destination.tier))) * tuning::orbit::planetTierRadius;
}

double orbitTargetRadius(double planetRadius)
{
    return planetRadius * tuning::orbit::targetRadiusScale;
}

int orbitZoneAt(const OrbitRunState& orbit, double x, double y)
{
    const double distance = std::hypot(x, y);
    if (distance <= orbit.planetRadius + tuning::orbit::collisionPadding) {
        return 0;
    }
    const double radialError = std::abs(distance - orbit.targetRadius);
    if (radialError <= orbit.perfectBand) {
        return 2;
    }
    if (radialError <= orbit.goodBand) {
        return 1;
    }
    return 0;
}

double normalizedAngleDelta(double previous, double current)
{
    double delta = current - previous;
    while (delta > 3.14159265358979323846) {
        delta -= 2.0 * 3.14159265358979323846;
    }
    while (delta < -3.14159265358979323846) {
        delta += 2.0 * 3.14159265358979323846;
    }
    return delta;
}

double orbitBaseReward(const Destination& destination)
{
    return std::max(tuning::orbit::goodRewardFloor, destination.baseReward * tuning::orbit::goodRewardFactor);
}

void populateOrbitRewardPreview(OrbitRunState& orbit, const Destination* destination)
{
    orbit.rewardCredits = 0.0;
    orbit.blueprintGain = 0;
    if (orbit.result == OrbitGrade::Active || orbit.result == OrbitGrade::Miss || destination == nullptr) {
        return;
    }

    const double baseReward = orbitBaseReward(*destination);
    orbit.rewardCredits = orbit.result == OrbitGrade::Perfect
        ? baseReward * tuning::orbit::perfectRewardMultiplier
        : baseReward;
    orbit.blueprintGain = orbit.result == OrbitGrade::Perfect
        ? tuning::orbit::perfectBlueprintGain
        : tuning::orbit::goodBlueprintGain;
    if (destinationSupportsResearch(*destination)) {
        orbit.blueprintGain += 1;
    }
}

void pushFlybyTrailPoint(FlybyRunState& flyby, double x, double y)
{
    if (!flyby.trailPoints.empty()) {
        const FlybyTrailPoint& last = flyby.trailPoints.back();
        if (std::hypot(last.x - x, last.y - y) < 0.012) {
            return;
        }
    }
    flyby.trailPoints.push_back({x, y});
}

void pushOrbitTrailPoint(OrbitRunState& orbit, double x, double y)
{
    constexpr std::size_t maxTrailPoints = 144;
    if (!orbit.trailPoints.empty()) {
        const FlybyTrailPoint& last = orbit.trailPoints.back();
        if (std::hypot(last.x - x, last.y - y) < 0.010) {
            return;
        }
    }
    orbit.trailPoints.push_back({x, y});
    if (orbit.trailPoints.size() > maxTrailPoints) {
        orbit.trailPoints.erase(orbit.trailPoints.begin());
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

bool bankArrivalLandingFlightData(GameState& state, const ContentCatalog& catalog)
{
    if (!canAttemptArrivalLanding(state, catalog)) {
        return false;
    }
    return bankFrontierReadiness(state, catalog);
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
    state.run.orbit = {};
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

void startArrivalOrbitRun(GameState& state, const ContentCatalog& catalog)
{
    const Destination* destination = currentResearchDestination(state, catalog);
    if (destination == nullptr || !canEnterArrivalOrbit(state, catalog)) {
        return;
    }

    OrbitRunState orbit;
    orbit.active = true;
    orbit.destinationId = destination->id;
    orbit.durationSeconds = tuning::orbit::durationSeconds;
    orbit.planetRadius = orbitPlanetRadius(*destination);
    orbit.targetRadius = orbitTargetRadius(orbit.planetRadius);
    orbit.goodBand = orbit.planetRadius * tuning::orbit::goodBandScale;
    orbit.perfectBand = orbit.planetRadius * tuning::orbit::perfectBandScale;
    orbit.gravityStrength = orbit.targetRadius * orbit.targetRadius * tuning::orbit::gravityScale;

    const double angle = tuning::orbit::startAngleRadians;
    orbit.shipX = std::cos(angle) * orbit.targetRadius;
    orbit.shipY = std::sin(angle) * orbit.targetRadius;
    orbit.velocityX = -std::sin(angle) * tuning::orbit::startTangentialSpeed;
    orbit.velocityY = std::cos(angle) * tuning::orbit::startTangentialSpeed;
    orbit.angleRadians = std::atan2(orbit.shipY, orbit.shipX);
    orbit.currentZone = orbitZoneAt(orbit, orbit.shipX, orbit.shipY);
    orbit.worstZone = orbit.currentZone;
    pushOrbitTrailPoint(orbit, orbit.shipX, orbit.shipY);

    state.run.orbit = orbit;
    state.run.arrivalOps = {true, destination->id};
    state.screen = Screen::Orbit;
}

void setOrbitMove(GameState& state, double xAxis, double yAxis)
{
    if (!state.run.orbit.active || state.run.orbit.completed) {
        return;
    }
    state.run.orbit.inputX = std::clamp(xAxis, -1.0, 1.0);
    state.run.orbit.inputY = std::clamp(yAxis, -1.0, 1.0);
}

OrbitGrade orbitGrade(const OrbitRunState& orbit)
{
    if (!orbit.completed) {
        return OrbitGrade::Active;
    }
    if (orbit.orbitProgress < 1.0) {
        return OrbitGrade::Miss;
    }
    if (orbit.worstZone >= 2) {
        return OrbitGrade::Perfect;
    }
    if (orbit.worstZone >= 1) {
        return OrbitGrade::Good;
    }
    return OrbitGrade::Miss;
}

void updateOrbitRun(GameState& state, double deltaSeconds)
{
    OrbitRunState& orbit = state.run.orbit;
    if (!orbit.active || orbit.completed) {
        return;
    }

    const double dt = std::clamp(deltaSeconds, 0.0, tuning::launch::maxFrameStepSeconds);
    const double previousAngle = orbit.angleRadians;
    const double distance = std::max(0.001, std::hypot(orbit.shipX, orbit.shipY));
    const double radialX = orbit.shipX / distance;
    const double radialY = orbit.shipY / distance;
    const double tangentX = -radialY;
    const double tangentY = radialX;
    const double gravityAcceleration = orbit.gravityStrength / (distance * distance + tuning::orbit::gravitySoftening);
    orbit.velocityX += -radialX * gravityAcceleration * dt;
    orbit.velocityY += -radialY * gravityAcceleration * dt;

    const double radialInput = std::clamp(orbit.inputX, -1.0, 1.0);
    const double tangentialInput = std::clamp(orbit.inputY, -1.0, 1.0);
    orbit.velocityX += (radialX * radialInput + tangentX * tangentialInput) * tuning::orbit::thrustAcceleration * dt;
    orbit.velocityY += (radialY * radialInput + tangentY * tangentialInput) * tuning::orbit::thrustAcceleration * dt;

    const double drag = std::max(0.0, 1.0 - tuning::orbit::driftDrag * dt);
    orbit.velocityX *= drag;
    orbit.velocityY *= drag;

    const double speed = std::hypot(orbit.velocityX, orbit.velocityY);
    if (speed > tuning::orbit::maxSpeed) {
        const double scale = tuning::orbit::maxSpeed / speed;
        orbit.velocityX *= scale;
        orbit.velocityY *= scale;
    } else if (speed < tuning::orbit::minSpeed) {
        const double scale = tuning::orbit::minSpeed / std::max(0.001, speed);
        orbit.velocityX *= scale;
        orbit.velocityY *= scale;
    }

    orbit.shipX += orbit.velocityX * dt;
    orbit.shipY += orbit.velocityY * dt;
    orbit.angleRadians = std::atan2(orbit.shipY, orbit.shipX);
    orbit.orbitProgress += std::abs(normalizedAngleDelta(previousAngle, orbit.angleRadians)) / (2.0 * 3.14159265358979323846);
    pushOrbitTrailPoint(orbit, orbit.shipX, orbit.shipY);

    orbit.currentZone = orbitZoneAt(orbit, orbit.shipX, orbit.shipY);
    orbit.worstZone = std::min(orbit.worstZone, orbit.currentZone);
    if (orbit.currentZone >= 2) {
        orbit.perfectSeconds += dt;
    } else if (orbit.currentZone == 1) {
        orbit.goodSeconds += dt;
    } else {
        orbit.missSeconds += dt;
    }

    orbit.elapsedSeconds += dt;
    const double currentDistance = std::hypot(orbit.shipX, orbit.shipY);
    const double escapeRadius = orbit.targetRadius + orbit.goodBand * tuning::orbit::escapeRadiusScale;
    if (currentDistance <= orbit.planetRadius + tuning::orbit::collisionPadding || currentDistance >= escapeRadius) {
        orbit.completed = true;
        orbit.result = OrbitGrade::Miss;
    } else if (orbit.orbitProgress >= 1.0) {
        orbit.completed = true;
        orbit.result = orbitGrade(orbit);
    } else if (orbit.elapsedSeconds >= orbit.durationSeconds) {
        orbit.elapsedSeconds = orbit.durationSeconds;
        orbit.completed = true;
        orbit.result = OrbitGrade::Miss;
    }

    if (orbit.completed) {
        populateOrbitRewardPreview(orbit, nullptr);
        orbit.velocityX = 0.0;
        orbit.velocityY = 0.0;
        orbit.inputX = 0.0;
        orbit.inputY = 0.0;
    }
}

void applyOrbitReward(GameState& state, const ContentCatalog& catalog, OrbitGrade grade)
{
    if (grade == OrbitGrade::Active || grade == OrbitGrade::Miss) {
        return;
    }

    const Destination* destination = currentResearchDestination(state, catalog);
    if (destination == nullptr && !state.run.orbit.destinationId.empty()) {
        destination = catalog.findDestination(state.run.orbit.destinationId);
    }
    if (destination == nullptr) {
        return;
    }

    addDestinationHistoryValue(state.meta.destinationOrbits, catalog, destination->id);
    populateOrbitRewardPreview(state.run.orbit, destination);
    state.meta.blueprintProgress += state.run.orbit.blueprintGain;
    state.run.credits += state.run.orbit.rewardCredits;
    unlockFromBlueprints(state);
}

void completeOrbitRun(GameState& state, const ContentCatalog& catalog)
{
    if (!state.run.orbit.active || !state.run.orbit.completed) {
        return;
    }

    const OrbitRunState orbit = state.run.orbit;
    const OrbitGrade grade = orbit.result == OrbitGrade::Active ? orbitGrade(orbit) : orbit.result;
    applyOrbitReward(state, catalog, grade);
    const Destination* destination = catalog.findDestination(orbit.destinationId);
    state.run.arrivalOps = {true, destination == nullptr ? orbit.destinationId : destination->id};
    state.run.orbit = {};
    state.screen = Screen::ArrivalOps;
}

void abortOrbitRun(GameState& state)
{
    if (!state.run.orbit.active || state.run.orbit.completed) {
        return;
    }

    const std::string destinationId = state.run.orbit.destinationId;
    state.run.arrivalOps = {true, destinationId};
    state.run.orbit = {};
    state.screen = Screen::ArrivalOps;
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
        effects.sentryDamagePerSecond += drone->stats.sentryDamagePerSecond;
        effects.enemyDamageRelief += drone->stats.enemyDamageRelief;
        effects.areaControlDamagePerSecond += drone->stats.areaControlDamagePerSecond;
        effects.enemySlow += drone->stats.enemySlow;
        effects.reactiveArmorDamagePerSecond += drone->stats.reactiveArmorDamagePerSecond;
        effects.environmentalShieldRelief += drone->stats.environmentalShieldRelief;
        effects.names.push_back(drone->name);
    }

    effects.passiveMiningRate = std::clamp(effects.passiveMiningRate, 0.0, 0.40);
    effects.scannerRadius = std::clamp(effects.scannerRadius, 0.0, 5.0);
    effects.drillIntegrityRelief = std::clamp(effects.drillIntegrityRelief, 0.0, 0.35);
    effects.hardRockBounceRelief = std::clamp(effects.hardRockBounceRelief, 0.0, 0.55);
    effects.extractionRiskRelief = std::clamp(effects.extractionRiskRelief, 0.0, 0.08);
    effects.enemyEncounterRelief = std::clamp(effects.enemyEncounterRelief, 0.0, 0.18);
    effects.sentryDamagePerSecond = std::clamp(effects.sentryDamagePerSecond, 0.0, 8.0);
    effects.enemyDamageRelief = std::clamp(effects.enemyDamageRelief, 0.0, 0.55);
    effects.areaControlDamagePerSecond = std::clamp(effects.areaControlDamagePerSecond, 0.0, 3.0);
    effects.enemySlow = std::clamp(effects.enemySlow, 0.0, 0.45);
    effects.reactiveArmorDamagePerSecond = std::clamp(effects.reactiveArmorDamagePerSecond, 0.0, 4.0);
    effects.environmentalShieldRelief = std::clamp(effects.environmentalShieldRelief, 0.0, 0.35);
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
        return outcome.message.empty() ? std::string(text::status::surfaceSupplyBlocked) : outcome.message;
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
    expedition.sharedFuelCapacity = tuning::research::sharedFuelCapacity;
    expedition.sharedFuel = expedition.sharedFuelCapacity;
    if (arkDiscovered(state)) {
        expedition.sharedFuel = std::min(expedition.sharedFuelCapacity, state.meta.ark.fuelReserve);
        state.meta.ark.fuelReserve = std::max(0, state.meta.ark.fuelReserve - expedition.sharedFuel);
    }
    expedition.hazard = std::max(baseHazard + reconPenalty, baseHazard + site.hazardDelta + reconPenalty - crew.hazardRelief);
    expedition.enemyEncountersEnabled = destinationAllowsEnemyEncounters(*destination);
    addDestinationHistoryValue(state.meta.destinationLandings, catalog, destination->id);
    state.run.arrivalOps = {};
    appendSurfaceLog(expedition, std::string(surfaceSiteProfileName(expedition.siteProfile)) + ": " + std::string(surfaceSiteProfileDetail(expedition.siteProfile)));
    appendSurfaceLog(expedition, "Shared fuel loaded: " + std::to_string(expedition.sharedFuel) + "/" + std::to_string(expedition.sharedFuelCapacity) + ".");
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
    expedition.miningSitePrepared = true;
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
    SurfaceActionOutcome outcome;
    if (expedition.miningRunUsed) {
        outcome.message = "Mining run is complete. Extract before pushing deeper.";
        return outcome;
    }
    const double extractionRiskBefore = surfaceExtractionRisk(state);
    outcome = spendSupply(expedition, tuning::research::pushSupplyCost);
    if (!outcome.applied) {
        return outcome;
    }

    expedition.miningSitePrepared = true;
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

bool hasPendingSurfacePayload(const MaterialInventory& materials, const std::vector<ArtifactRecord>& artifacts, int cargo)
{
    return cargo > 0 || materials.common > 0 || materials.rare > 0 || materials.exotic > 0 || !artifacts.empty();
}

struct SurfaceScanSupport {
    double signalBonus = 0.0;
    double riskRelief = 0.0;
    double rareChanceBonus = 0.0;
    double exoticChanceBonus = 0.0;
    double artifactChanceBonus = 0.0;
    double hazardRelief = 0.0;
    int maxPulseBonus = 0;
};

SurfaceScanSupport surfaceScanSupport(const GameState& state)
{
    const ContentCatalog catalog = createDefaultContent();
    const SurfaceUpgradeEffects upgrades = surfaceUpgradeEffects(state, catalog);
    const MiniDroneLoadoutEffects drones = miniDroneLoadoutEffects(state, catalog);
    const double scannerReach = std::max(0.0, upgrades.scannerRadius + drones.scannerRadius);

    SurfaceScanSupport support;
    support.signalBonus = std::clamp(scannerReach * 0.025 + upgrades.oreYieldChance * 0.25, 0.0, 0.16);
    support.riskRelief = std::clamp(upgrades.hazardRelief + scannerReach * 0.015, 0.0, 0.18);
    support.rareChanceBonus = std::clamp(upgrades.oreYieldChance + scannerReach * 0.012, 0.0, 0.24);
    support.exoticChanceBonus = std::clamp(upgrades.oreYieldChance * 0.35 + scannerReach * 0.006, 0.0, 0.10);
    support.artifactChanceBonus = std::clamp(scannerReach * 0.010, 0.0, 0.09);
    support.hazardRelief = std::clamp(upgrades.hazardRelief, 0.0, 0.06);
    support.maxPulseBonus = std::clamp(static_cast<int>(std::floor(scannerReach / 2.5)), 0, 2);
    return support;
}

struct SurfacePushSupport {
    double pressureRelief = 0.0;
    double collapseRelief = 0.0;
    double richChanceBonus = 0.0;
    double artifactChanceBonus = 0.0;
    double hazardRelief = 0.0;
    int maxStepBonus = 0;
};

SurfacePushSupport surfacePushSupport(const GameState& state)
{
    const ContentCatalog catalog = createDefaultContent();
    const SurfaceUpgradeEffects upgrades = surfaceUpgradeEffects(state, catalog);
    const MiniDroneLoadoutEffects drones = miniDroneLoadoutEffects(state, catalog);
    const double structureSupport =
        upgrades.drillDurability * 0.025 +
        upgrades.drillCooling * 0.010 +
        upgrades.hardRockBounceRelief * 0.30 +
        upgrades.droneSpeed * 0.045 +
        drones.drillIntegrityRelief * 0.22 +
        drones.hardRockBounceRelief * 0.24;

    SurfacePushSupport support;
    support.pressureRelief = std::clamp(structureSupport + upgrades.hazardRelief * 0.50, 0.0, 0.20);
    support.collapseRelief = std::clamp(structureSupport + upgrades.hazardRelief, 0.0, 0.24);
    support.richChanceBonus = std::clamp(upgrades.oreYieldChance + upgrades.hardRockBounceRelief * 0.15 + drones.hardRockBounceRelief * 0.10, 0.0, 0.22);
    support.artifactChanceBonus = std::clamp(upgrades.hardRockBounceRelief * 0.10 + drones.drillIntegrityRelief * 0.12, 0.0, 0.08);
    support.hazardRelief = std::clamp(upgrades.hazardRelief + structureSupport * 0.20, 0.0, 0.08);
    support.maxStepBonus = std::clamp(static_cast<int>(std::floor((upgrades.drillDurability + upgrades.drillCooling + drones.hardRockBounceRelief * 5.0) / 4.0)), 0, 1);
    return support;
}

MiningCellMaterial rollSurfacePushRichMarker(
    const SurfaceExpeditionState& expedition,
    int step,
    const SurfacePushSupport& support,
    Random& rng)
{
    const ContentCatalog catalog = createDefaultContent();
    const Destination* destination = catalog.findDestination(expedition.destinationId);
    const double bodyRichness = destination == nullptr
        ? 0.0
        : std::clamp(static_cast<double>(destination->tier) * 0.045 + destination->hazard * 0.025, 0.0, 0.22);
    const double depthRichness = std::clamp(static_cast<double>(std::max(0, expedition.depth + step)) * 0.055, 0.0, 0.30);
    const double exoticChance = std::clamp(0.03 + bodyRichness * 0.34 + depthRichness * 0.22 + support.richChanceBonus * 0.20, 0.0, 0.26);

    if (step >= 3 && rng.chance(exoticChance)) {
        return MiningCellMaterial::ExoticVein;
    }
    return MiningCellMaterial::RareOre;
}

const SurfaceDepthProspect* findSurfaceDepthProspect(const SurfaceExpeditionState& expedition, int absoluteDepth)
{
    const auto found = std::find_if(expedition.depthProspects.begin(), expedition.depthProspects.end(), [&](const SurfaceDepthProspect& prospect) {
        return prospect.absoluteDepth == absoluteDepth;
    });
    return found == expedition.depthProspects.end() ? nullptr : &(*found);
}

MaterialInventory maxMaterials(const MaterialInventory& left, const MaterialInventory& right)
{
    return {
        std::max(left.common, right.common),
        std::max(left.rare, right.rare),
        std::max(left.exotic, right.exotic)
    };
}

MaterialInventory materialDeltaAbove(const MaterialInventory& next, const MaterialInventory& previous)
{
    return {
        std::max(0, next.common - previous.common),
        std::max(0, next.rare - previous.rare),
        std::max(0, next.exotic - previous.exotic)
    };
}

void mergeSurfaceDepthProspect(SurfaceExpeditionState& expedition, const SurfaceDepthProspect& prospect)
{
    auto found = std::find_if(expedition.depthProspects.begin(), expedition.depthProspects.end(), [&](const SurfaceDepthProspect& existing) {
        return existing.absoluteDepth == prospect.absoluteDepth;
    });
    if (found == expedition.depthProspects.end()) {
        expedition.depthProspects.push_back(prospect);
        return;
    }

    found->depthOffset = std::max(0, found->absoluteDepth - expedition.depth);
    found->possibleMaterials = maxMaterials(found->possibleMaterials, prospect.possibleMaterials);
    found->possibleArtifacts = std::max(found->possibleArtifacts, prospect.possibleArtifacts);
}

SurfaceDepthProspect rollSurfaceDepthProspect(
    const GameState& state,
    int depthOffset,
    double signal,
    const SurfaceScanSupport& support,
    Random& rng)
{
    const SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    const SurfaceCrewEffects crew = surfaceCrewEffects(state);
    const SurfaceSiteProfileEffects site = surfaceSiteProfileEffects(expedition.siteProfile);
    SurfaceDepthProspect prospect;
    prospect.depthOffset = std::max(0, depthOffset);
    prospect.absoluteDepth = std::max(0, expedition.depth + prospect.depthOffset);

    prospect.possibleMaterials.common = prospect.depthOffset == 0 || rng.chance(0.62 + signal * 0.18) ? 1 : 0;
    if (prospect.depthOffset > 0 || rng.chance(0.12 + signal * 0.30 + site.mineRareChanceBonus + support.rareChanceBonus)) {
        prospect.possibleMaterials.rare += 1;
    }
    if (prospect.depthOffset >= 2 && rng.chance(0.12 + signal * 0.16 + support.exoticChanceBonus + static_cast<double>(prospect.depthOffset) * 0.035)) {
        prospect.possibleMaterials.exotic += 1;
    }
    if (prospect.depthOffset >= 3 && rng.chance(0.08 + signal * 0.10 + support.exoticChanceBonus)) {
        prospect.possibleMaterials.exotic += 1;
    }
    if (prospect.depthOffset >= 2 && rng.chance(std::min(0.70, 0.05 + signal * 0.18 + crew.artifactChanceBonus + site.artifactChanceBonus + support.artifactChanceBonus))) {
        prospect.possibleArtifacts = 1;
    }
    if (materialCargo(prospect.possibleMaterials) == 0 && prospect.possibleArtifacts == 0) {
        prospect.possibleMaterials.common = 1;
    }
    return prospect;
}

MaterialInventory actualizePushMaterials(
    const SurfaceExpeditionState& expedition,
    int step,
    const SurfaceDepthProspect* forecast,
    const SurfacePushSupport& support,
    Random& rng)
{
    MaterialInventory gain;
    gain.common = step == 1 ? 1 : 0;
    gain.rare = 1;
    if (forecast != nullptr) {
        gain.common = std::max(gain.common, forecast->possibleMaterials.common > 0 && rng.chance(0.78) ? 1 : 0);
        gain.rare = std::max(gain.rare, forecast->possibleMaterials.rare);
        if (forecast->possibleMaterials.exotic > 0) {
            if (rng.chance(0.58 + support.richChanceBonus)) {
                gain.exotic += 1;
            } else {
                gain.rare += 1;
            }
        }
        return gain;
    }

    if (step >= 3 && rng.chance(0.35 + support.richChanceBonus)) {
        const MiningCellMaterial richMarker = rollSurfacePushRichMarker(expedition, step, support, rng);
        if (richMarker == MiningCellMaterial::ExoticVein) {
            gain.exotic += 1;
        } else {
            gain.rare += 1;
        }
    }
    return gain;
}

std::vector<MiningCellMaterial> depthProspectMarkers(const SurfaceDepthProspect& prospect)
{
    std::vector<MiningCellMaterial> markers;
    for (int i = 0; i < std::max(0, prospect.possibleMaterials.common); ++i) {
        markers.push_back(MiningCellMaterial::CommonOre);
    }
    for (int i = 0; i < std::max(0, prospect.possibleMaterials.rare); ++i) {
        markers.push_back(MiningCellMaterial::RareOre);
    }
    for (int i = 0; i < std::max(0, prospect.possibleMaterials.exotic); ++i) {
        markers.push_back(MiningCellMaterial::ExoticVein);
    }
    for (int i = 0; i < std::max(0, prospect.possibleArtifacts); ++i) {
        markers.push_back(MiningCellMaterial::ArtifactCache);
    }
    return markers;
}

void appendSurfacePushMarkers(SurfacePushRunState& push, const MaterialInventory& gain, bool artifactFound, int depthOffset)
{
    for (int i = 0; i < std::max(0, gain.common); ++i) {
        push.rewardMarkers.push_back(MiningCellMaterial::CommonOre);
        push.rewardMarkerDepthOffsets.push_back(depthOffset);
    }
    for (int i = 0; i < std::max(0, gain.rare); ++i) {
        push.rewardMarkers.push_back(MiningCellMaterial::RareOre);
        push.rewardMarkerDepthOffsets.push_back(depthOffset);
    }
    for (int i = 0; i < std::max(0, gain.exotic); ++i) {
        push.rewardMarkers.push_back(MiningCellMaterial::ExoticVein);
        push.rewardMarkerDepthOffsets.push_back(depthOffset);
    }
    if (artifactFound) {
        push.rewardMarkers.push_back(MiningCellMaterial::ArtifactCache);
        push.rewardMarkerDepthOffsets.push_back(depthOffset);
    }
}

void resetSurfaceScan(GameState& state)
{
    state.run.surfaceScan = {};
}

void resetSurfacePush(GameState& state)
{
    state.run.surfacePush = {};
}

SurfaceActionOutcome startSurfaceScanRun(GameState& state, Random&)
{
    SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    SurfaceActionOutcome outcome = spendSupply(expedition, tuning::research::surveySupplyCost);
    if (!outcome.applied) {
        outcome.message = "Need an action kit to run a surface scan.";
        return outcome;
    }

    SurfaceScanRunState scan;
    const SurfaceScanSupport support = surfaceScanSupport(state);
    scan.active = true;
    scan.destinationId = expedition.destinationId;
    scan.maxPulses = tuning::research::scanMaxPulses + support.maxPulseBonus;
    scan.signal = std::clamp(0.12 + support.signalBonus, 0.0, 0.42);
    scan.interference = std::clamp(expedition.hazard * 0.25 - support.riskRelief * 0.35, 0.0, 0.30);
    scan.bustRisk = std::clamp(
        tuning::research::scanBaseBustRisk + expedition.hazard * tuning::research::scanBustRiskHazardScale - support.riskRelief,
        0.02,
        0.38);
    scan.message = "Scanner lattice armed. Pulse 1 maps +0; later pulses preview deeper push layers.";
    state.run.surfaceScan = scan;
    state.screen = Screen::SurfaceScan;
    outcome.message = "Scanner lattice armed. Map layers before deciding whether to push deeper.";
    return outcome;
}

SurfaceActionOutcome pulseSurfaceScan(GameState& state, Random& rng)
{
    SurfaceScanRunState& scan = state.run.surfaceScan;
    SurfaceActionOutcome outcome;
    if (!scan.active || scan.completed) {
        outcome.message = "Surface scan is not active.";
        return outcome;
    }

    outcome.applied = true;
    if (rng.chance(scan.bustRisk)) {
        scan.completed = true;
        scan.busted = true;
        scan.active = false;
        state.run.surfaceExpedition.hazard += tuning::research::scanBustHazardIncrease;
        outcome.hazardTriggered = true;
        outcome.hazardMessage = "Interference burned the survey window.";
        outcome.hazardDelta = tuning::research::scanBustHazardIncrease;
        outcome.message = "Scan busted. The crew pulled the array before it cooked the rig.";
        scan.message = surfaceActionSummary(outcome);
        appendSurfaceLog(state.run.surfaceExpedition, scan.message);
        return outcome;
    }

    scan.pulses += 1;
    const SurfaceScanSupport support = surfaceScanSupport(state);
    scan.signal = std::clamp(scan.signal + tuning::research::scanSignalPerPulse + support.signalBonus * 0.35 + rng.range(0.02, 0.09), 0.0, 1.0);
    scan.interference = std::clamp(scan.interference + std::max(0.06, 0.12 - support.riskRelief * 0.30) + rng.range(0.00, 0.05), 0.0, 1.0);
    const double scanHazardDelta = std::max(0.0, tuning::research::scanHazardPerPulse - support.hazardRelief * 0.10);
    scan.hazardDelta += scanHazardDelta;
    scan.bustRisk = std::clamp(
        tuning::research::scanBaseBustRisk +
            state.run.surfaceExpedition.hazard * tuning::research::scanBustRiskHazardScale +
            scan.pulses * tuning::research::scanBustRiskPerPulse +
            scan.interference * 0.045 -
            support.riskRelief,
        0.02,
        0.72);

    const int depthOffset = scan.pulses - 1;
    const SurfaceDepthProspect prospect = rollSurfaceDepthProspect(state, depthOffset, scan.signal, support, rng);
    scan.depthProspects.push_back(prospect);
    if (prospect.possibleArtifacts > 0 && scan.temporaryArtifacts.empty()) {
        scan.temporaryArtifacts.push_back({artifactId(state.run.surfaceExpedition), state.run.surfaceExpedition.destinationId, false});
        outcome.artifactFound = true;
        outcome.cargoDelta += 3;
        scan.cargo += 3;
    }

    addMaterials(scan.temporaryMaterials, prospect.possibleMaterials);
    const int cargoGain = materialCargo(prospect.possibleMaterials);
    scan.cargo += cargoGain;
    outcome.materialDelta = prospect.possibleMaterials;
    outcome.cargoDelta += cargoGain;
    outcome.hazardDelta = scanHazardDelta;
    outcome.message = scan.pulses >= scan.maxPulses
        ? "Full spectrum scan complete. Bank the payload before the window collapses."
        : "Scan mapped layer +" + std::to_string(depthOffset) + ". Push deeper to test that forecast.";
    scan.message = surfaceActionSummary(outcome);
    if (scan.pulses >= scan.maxPulses) {
        scan.completed = true;
    }
    return outcome;
}

SurfaceActionOutcome bankSurfaceScan(GameState& state)
{
    SurfaceScanRunState& scan = state.run.surfaceScan;
    SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    SurfaceActionOutcome outcome;
    if (!scan.completed && !scan.active) {
        outcome.message = "No surface scan is ready to bank.";
        return outcome;
    }

    outcome.applied = true;
    if (scan.busted) {
        outcome.message = "Scan window closed. No payload banked.";
    } else {
        for (const SurfaceDepthProspect& prospect : scan.depthProspects) {
            const SurfaceDepthProspect* existing = findSurfaceDepthProspect(expedition, prospect.absoluteDepth);
            const MaterialInventory existingMaterials = existing == nullptr ? MaterialInventory{} : existing->possibleMaterials;
            const int existingArtifacts = existing == nullptr ? 0 : existing->possibleArtifacts;
            if (prospect.absoluteDepth == expedition.depth) {
                addMaterials(expedition.prospectMaterials, materialDeltaAbove(maxMaterials(existingMaterials, prospect.possibleMaterials), existingMaterials));
                expedition.prospectArtifacts += std::max(0, prospect.possibleArtifacts - existingArtifacts);
            }
            mergeSurfaceDepthProspect(expedition, prospect);
        }
        expedition.hazard += scan.hazardDelta;
        expedition.miningSitePrepared = true;
        outcome.hazardDelta = scan.hazardDelta;
        outcome.message = !scan.depthProspects.empty()
            ? "Scan banked. Layer forecasts now line up with Push Deeper."
            : "Scan banked. The crew found a clean mining line, but no strong payload.";
    }
    appendSurfaceLog(expedition, surfaceActionSummary(outcome));
    resetSurfaceScan(state);
    state.screen = Screen::SurfaceExpedition;
    return outcome;
}

SurfaceActionOutcome abortSurfaceScan(GameState& state)
{
    SurfaceActionOutcome outcome;
    if (!state.run.surfaceScan.active && !state.run.surfaceScan.completed) {
        outcome.message = "No surface scan is active.";
        return outcome;
    }
    outcome.applied = true;
    outcome.message = "Surface scan recalled. No scan payload banked.";
    appendSurfaceLog(state.run.surfaceExpedition, surfaceActionSummary(outcome));
    resetSurfaceScan(state);
    state.screen = Screen::SurfaceExpedition;
    return outcome;
}

SurfaceActionOutcome startSurfacePushRun(GameState& state, Random&)
{
    SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    SurfaceActionOutcome outcome;
    if (expedition.miningRunUsed) {
        outcome.message = "Mining run is complete. Extract before pushing deeper.";
        return outcome;
    }

    outcome = spendSupply(expedition, tuning::research::pushSupplyCost);
    if (!outcome.applied) {
        outcome.message = "Need two action kits to push deeper.";
        return outcome;
    }

    SurfacePushRunState push;
    const SurfacePushSupport support = surfacePushSupport(state);
    push.active = true;
    push.destinationId = expedition.destinationId;
    push.maxSteps = tuning::research::pushMaxSteps + support.maxStepBonus;
    push.pressure = std::clamp(expedition.hazard * 0.35 - support.pressureRelief, 0.0, 0.45);
    push.collapseRisk = std::clamp(
        tuning::research::pushBaseCollapseRisk + expedition.hazard * tuning::research::pushRiskHazardScale - support.collapseRelief,
        0.04,
        0.42);
    push.message = "Descent lane armed. Each step commits a deeper layer and reveals actual finds.";
    state.run.surfacePush = push;
    state.screen = Screen::SurfacePush;
    outcome.message = "Deep route armed. Push to turn layer forecasts into actual mining marks.";
    return outcome;
}

SurfaceActionOutcome pushSurfaceDepthStep(GameState& state, Random& rng)
{
    SurfacePushRunState& push = state.run.surfacePush;
    SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    SurfaceActionOutcome outcome;
    if (!push.active || push.completed) {
        outcome.message = "Deep push is not active.";
        return outcome;
    }

    outcome.applied = true;
    if (rng.chance(push.collapseRisk)) {
        push.completed = true;
        push.busted = true;
        push.active = false;
        expedition.hazard += tuning::research::pushCollapseHazardIncrease;
        outcome.hazardTriggered = true;
        outcome.hazardMessage = "A shelf collapsed across the descent lane.";
        outcome.hazardDelta = tuning::research::pushCollapseHazardIncrease;
        outcome.message = "Deep push busted. The route collapsed before the crew could bank it.";
        push.message = surfaceActionSummary(outcome);
        appendSurfaceLog(expedition, push.message);
        return outcome;
    }

    push.steps += 1;
    const SurfacePushSupport support = surfacePushSupport(state);
    push.depthGain = std::max(push.depthGain, push.steps);
    push.pressure = std::clamp(push.pressure + std::max(0.08, 0.16 - support.pressureRelief * 0.35) + rng.range(0.00, 0.06), 0.0, 1.0);
    const double pushHazardDelta = std::max(0.0, tuning::research::pushHazardPerStep - support.hazardRelief * 0.12);
    push.hazardDelta += pushHazardDelta;
    push.collapseRisk = std::clamp(
        tuning::research::pushBaseCollapseRisk +
            expedition.hazard * tuning::research::pushRiskHazardScale +
            push.steps * tuning::research::pushRiskPerStep +
            push.pressure * 0.045 -
            support.collapseRelief,
        0.04,
        0.84);

    const int targetDepth = expedition.depth + push.steps;
    const SurfaceDepthProspect* forecast = findSurfaceDepthProspect(expedition, targetDepth);
    MaterialInventory gain = actualizePushMaterials(expedition, push.steps, forecast, support, rng);
    const SurfaceCrewEffects crew = surfaceCrewEffects(state);
    const SurfaceSiteProfileEffects site = surfaceSiteProfileEffects(expedition.siteProfile);
    bool artifactFound = false;
    const double artifactChance = forecast != nullptr && forecast->possibleArtifacts > 0
        ? 0.55 + support.artifactChanceBonus + crew.artifactChanceBonus * 0.50
        : tuning::research::artifactChanceBase * 0.40 + push.steps * 0.10 + crew.artifactChanceBonus + site.artifactChanceBonus + support.artifactChanceBonus;
    if (push.steps >= 2 && push.temporaryArtifacts.empty() && rng.chance(std::min(0.82, artifactChance))) {
        push.temporaryArtifacts.push_back({artifactId(expedition), expedition.destinationId, false});
        outcome.artifactFound = true;
        artifactFound = true;
        outcome.cargoDelta += 3;
        push.cargo += 3;
    }

    addMaterials(push.temporaryMaterials, gain);
    const int cargoGain = materialCargo(gain);
    push.cargo += cargoGain;
    appendSurfacePushMarkers(push, gain, artifactFound, push.steps);
    outcome.materialDelta = gain;
    outcome.cargoDelta += cargoGain;
    outcome.hazardDelta = pushHazardDelta;
    outcome.message = push.steps >= push.maxSteps
        ? "Maximum safe depth reached. Bank the route before the floor gives out."
        : (forecast != nullptr
            ? "Layer +" + std::to_string(push.steps) + " confirmed. Actual finds are now marked for mining."
            : "Blind push found a deeper lane. Scan first next time for a better read.");
    push.message = surfaceActionSummary(outcome);
    if (push.steps >= push.maxSteps) {
        push.completed = true;
    }
    return outcome;
}

SurfaceActionOutcome bankSurfacePush(GameState& state)
{
    SurfacePushRunState& push = state.run.surfacePush;
    SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    SurfaceActionOutcome outcome;
    if (!push.completed && !push.active) {
        outcome.message = "No deep route is ready to bank.";
        return outcome;
    }

    outcome.applied = true;
    if (push.busted) {
        outcome.message = "Deep route lost. No new payload banked.";
    } else {
        expedition.prospectMaterials = {};
        expedition.prospectArtifacts = 0;
        addMaterials(expedition.prospectMaterials, push.temporaryMaterials);
        expedition.prospectArtifacts += static_cast<int>(push.temporaryArtifacts.size());
        expedition.depth += std::max(1, push.depthGain);
        expedition.hazard += push.hazardDelta;
        expedition.miningSitePrepared = true;
        outcome.hazardDelta = push.hazardDelta;
        outcome.message = hasPendingSurfacePayload(push.temporaryMaterials, push.temporaryArtifacts, push.cargo)
            ? "Deep route banked. Richer deposits are marked in the mining lane."
            : "Deep route banked. The next mining lane is open.";
    }
    appendSurfaceLog(expedition, surfaceActionSummary(outcome));
    resetSurfacePush(state);
    state.screen = Screen::SurfaceExpedition;
    return outcome;
}

SurfaceActionOutcome abortSurfacePush(GameState& state)
{
    SurfaceActionOutcome outcome;
    if (!state.run.surfacePush.active && !state.run.surfacePush.completed) {
        outcome.message = "No deep push is active.";
        return outcome;
    }
    outcome.applied = true;
    outcome.message = "Deep push recalled. No route payload banked.";
    appendSurfaceLog(state.run.surfaceExpedition, surfaceActionSummary(outcome));
    resetSurfacePush(state);
    state.screen = Screen::SurfaceExpedition;
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
        applyRecoveredArtifactRewards(state, expedition.temporaryArtifacts);
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
