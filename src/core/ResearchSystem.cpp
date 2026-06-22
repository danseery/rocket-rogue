#include "core/ResearchSystem.h"
#include "core/ContentIds.h"
#include "core/GameFormat.h"
#include "core/GameText.h"
#include "core/Tuning.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
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
    state.run.surfaceExpedition = {};
}

void startArrivalOps(GameState& state, const LaunchOutcome& outcome)
{
    state.run.arrivalOps = {true, outcome.destinationId};
}

void completeArrivalFlyby(GameState& state, const ContentCatalog& catalog)
{
    const Destination* destination = currentResearchDestination(state, catalog);
    if (destination == nullptr) {
        return;
    }
    addDestinationHistoryValue(state.meta.destinationFlybys, catalog, destination->id);
    state.meta.blueprintProgress += 1;
    state.run.credits += std::max(12.0, destination->baseReward * 0.35);
    unlockFromBlueprints(state);
    state.run.arrivalOps = {};
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
    expedition.supply = tuning::research::baseSupply + destination->tier * tuning::research::supplyPerTier + surfaceToolEffects(state.meta).supplyBonus + site.supplyBonus;
    expedition.hazard = std::max(0.0, tuning::research::baseHazard + destination->tier * tuning::research::hazardPerTier + site.hazardDelta + landingReconHazardPenalty(state, catalog, *destination));
    expedition.enemyEncountersEnabled = destinationAllowsEnemyEncounters(*destination);
    addDestinationHistoryValue(state.meta.destinationLandings, catalog, destination->id);
    state.run.arrivalOps = {};
    appendSurfaceLog(expedition, std::string(surfaceSiteProfileName(expedition.siteProfile)) + ": " + std::string(surfaceSiteProfileDetail(expedition.siteProfile)));
    state.run.surfaceExpedition = expedition;
}

double surfaceExtractionRisk(const GameState& state)
{
    const SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    if (!expedition.active) {
        return 0.0;
    }

    const SurfaceToolEffects tools = surfaceToolEffects(state.meta);
    const SurfaceSiteProfileEffects site = surfaceSiteProfileEffects(expedition.siteProfile);
    double risk = tuning::research::extractionRiskBase
        + expedition.hazard * tuning::research::extractionRiskHazardScale
        + static_cast<double>(std::max(0, expedition.cargo)) * std::max(0.0, tuning::research::extractionRiskCargoScale - tools.cargoRiskRelief)
        - tools.extractionRiskRelief
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
    return std::clamp(
        tuning::research::surfaceEnemyChanceBase
            + expedition.hazard * tuning::research::surfaceEnemyChanceHazardScale
            - tools.enemyEncounterRelief,
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
    const SurfaceSiteProfileEffects site = surfaceSiteProfileEffects(expedition.siteProfile);
    const MaterialInventory gain {.common = tuning::research::surveyCommonGain + tools.surveyCommonBonus + site.surveyCommonBonus};
    addMaterials(expedition.temporaryMaterials, gain);
    expedition.cargo += materialCargo(gain);
    outcome.materialDelta = gain;
    outcome.cargoDelta = materialCargo(gain);
    applySurfaceHazard(
        expedition,
        outcome,
        rng,
        tuning::research::surveyHazardChanceScale,
        tools.surveyCommonBonus > 0 ? tuning::research::probeHazardRelief : 0.0,
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
    const SurfaceSiteProfileEffects site = surfaceSiteProfileEffects(expedition.siteProfile);
    MaterialInventory gain {.common = tuning::research::mineCommonGain + tools.mineCommonBonus + site.mineCommonBonus};
    if (expedition.depth >= tuning::research::mineRareDepthThreshold || rng.chance(std::min(1.0, expedition.hazard + tools.mineRareChanceBonus + site.mineRareChanceBonus))) {
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
        tools.mineCommonBonus > 0 ? tuning::research::drillHazardRelief : 0.0,
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
    const SurfaceSiteProfileEffects site = surfaceSiteProfileEffects(expedition.siteProfile);
    if (expedition.depth >= tuning::research::artifactDepthThreshold && expedition.temporaryArtifacts.empty() && rng.chance(std::min(1.0, tuning::research::artifactChanceBase + site.artifactChanceBonus))) {
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
        surfaceToolEffects(state.meta).extractionRiskRelief > 0.0 ? tuning::research::cargoRigHazardRelief : 0.0,
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
