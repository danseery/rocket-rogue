#include "core/GameState.h"
#include "core/ContentIds.h"
#include "core/GameText.h"
#include "core/ScenarioSystem.h"
#include "core/Tuning.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>

namespace rocket {

namespace {

enum class RefitOfferKind {
    ShipModule,
    CrewUpgrade
};

struct RefitCandidate {
    RefitOfferKind kind = RefitOfferKind::ShipModule;
    std::string id;
    int cost = 0;
    RefitTrack track = RefitTrack::None;
};

bool hasMaterialCost(const MaterialInventory& cost)
{
    return cost.common > 0 || cost.rare > 0 || cost.exotic > 0;
}

bool materialRefitsAvailable(const GameState& state)
{
    return state.meta.furthestTier >= tuning::research::firstResearchTier;
}

std::vector<std::string> starterInventory()
{
    return {
        content::module::sparrowEngine,
        content::module::stableTank,
        content::module::patchworkHull,
        content::module::radiatorVanes,
        content::module::analogTelemetry,
        content::module::springCapsule
    };
}

bool containsId(const std::vector<std::string>& ids, std::string_view id)
{
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

void addUniqueId(std::vector<std::string>& ids, const std::string& id)
{
    if (!id.empty() && !containsId(ids, id)) {
        ids.push_back(id);
    }
}

void pruneUnknownModules(std::vector<std::string>& ids, const ContentCatalog& catalog)
{
    ids.erase(
        std::remove_if(
            ids.begin(),
            ids.end(),
            [&](const std::string& id) {
                return catalog.findModule(id) == nullptr;
            }),
        ids.end());
}

void ensurePermanentModuleState(GameState& state, const ContentCatalog& catalog)
{
    for (const std::string& moduleId : starterInventory()) {
        addUniqueId(state.meta.ownedModuleIds, moduleId);
    }

    pruneUnknownModules(state.meta.ownedModuleIds, catalog);
    pruneUnknownModules(state.meta.defaultEquippedModuleIds, catalog);

    state.meta.defaultEquippedModuleIds.erase(
        std::remove_if(
            state.meta.defaultEquippedModuleIds.begin(),
            state.meta.defaultEquippedModuleIds.end(),
            [&](const std::string& id) {
                return !containsId(state.meta.ownedModuleIds, id);
            }),
        state.meta.defaultEquippedModuleIds.end());

    for (const std::string& moduleId : state.meta.ownedModuleIds) {
        addUniqueId(state.meta.defaultEquippedModuleIds, moduleId);
    }
}

void ensureDestinationHistory(GameState& state, const ContentCatalog& catalog)
{
    const std::size_t count = catalog.destinations.size();
    std::vector<std::string> catalogIds;
    catalogIds.reserve(count);
    for (const Destination& destination : catalog.destinations) {
        catalogIds.push_back(destination.id);
    }

    if (!state.meta.destinationHistoryIds.empty() && state.meta.destinationHistoryIds != catalogIds) {
        const auto remap = [&](const std::vector<int>& source) {
            std::vector<int> result(count, 0);
            for (std::size_t oldIndex = 0; oldIndex < state.meta.destinationHistoryIds.size() && oldIndex < source.size(); ++oldIndex) {
                const auto found = std::find(catalogIds.begin(), catalogIds.end(), state.meta.destinationHistoryIds[oldIndex]);
                if (found != catalogIds.end()) {
                    result[static_cast<std::size_t>(std::distance(catalogIds.begin(), found))] = source[oldIndex];
                }
            }
            return result;
        };
        state.meta.destinationAttempts = remap(state.meta.destinationAttempts);
        state.meta.destinationSuccesses = remap(state.meta.destinationSuccesses);
        state.meta.destinationFlybys = remap(state.meta.destinationFlybys);
        state.meta.destinationOrbits = remap(state.meta.destinationOrbits);
        state.meta.destinationLandings = remap(state.meta.destinationLandings);
    } else {
        state.meta.destinationAttempts.resize(count, 0);
        state.meta.destinationSuccesses.resize(count, 0);
        state.meta.destinationFlybys.resize(count, 0);
        state.meta.destinationOrbits.resize(count, 0);
        state.meta.destinationLandings.resize(count, 0);
    }
    state.meta.destinationHistoryIds = std::move(catalogIds);
}

int destinationIndexForId(const ContentCatalog& catalog, const std::string& destinationId)
{
    for (std::size_t i = 0; i < catalog.destinations.size(); ++i) {
        if (catalog.destinations[i].id == destinationId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool isShallowRecoveryOutcome(const Destination& destination, const LaunchOutcome& outcome)
{
    return outcome.failureCause != LaunchFailureCause::TrainingRescue &&
        outcome.recoveryMethod == RecoveryMethod::ReturnHome
        && outcome.ejectMultiplier < 1.0 + (destination.targetMultiplier - 1.0) * tuning::rewards::shallowRecoveryTargetShare;
}

bool isCleanShallowRecoveryOutcome(const Destination& destination, const LaunchOutcome& outcome)
{
    return isShallowRecoveryOutcome(destination, outcome)
        && outcome.peakWarning < tuning::rewards::cleanShallowRecoveryWarningThreshold;
}

int navigationFuelCost(const Destination& destination)
{
    // Khepri Prime and Rift Belt moved to campaign tiers 7 and 8, but their
    // established sortie economics remain the former tier-4/tier-5 values.
    if (destination.id == content::destination::nearbyStar) {
        return 6;
    }
    if (destination.id == content::destination::nearbyGalaxy) {
        return 7;
    }
    return 2 + destination.tier;
}

int& launchUpgradeRankRef(LaunchUpgradeRanks& ranks, LaunchUpgradeKind kind)
{
    switch (kind) {
    case LaunchUpgradeKind::FuelTanks: return ranks.fuelTanks;
    case LaunchUpgradeKind::FlightControls: return ranks.flightControls;
    case LaunchUpgradeKind::Cooling: return ranks.cooling;
    case LaunchUpgradeKind::Hull: return ranks.hull;
    case LaunchUpgradeKind::None: break;
    }
    static int unused = 0;
    unused = 0;
    return unused;
}

int& surfaceDepthUpgradeRankRef(
    SurfaceDepthUpgradeRanks& ranks,
    SurfaceDepthUpgradeKind kind)
{
    switch (kind) {
    case SurfaceDepthUpgradeKind::SurveyArray: return ranks.surveyArray;
    case SurfaceDepthUpgradeKind::BoreSystem: return ranks.boreSystem;
    case SurfaceDepthUpgradeKind::None: break;
    }
    static int unused = 0;
    unused = 0;
    return unused;
}

bool& surfaceDepthPurchasedThisRefitRef(
    SurfaceRefitPurchaseState& purchases,
    SurfaceDepthUpgradeKind kind)
{
    switch (kind) {
    case SurfaceDepthUpgradeKind::SurveyArray: return purchases.surveyArray;
    case SurfaceDepthUpgradeKind::BoreSystem: return purchases.boreSystem;
    case SurfaceDepthUpgradeKind::None: break;
    }
    static bool unused = false;
    unused = false;
    return unused;
}

bool surfaceDepthPurchasedThisRefit(
    const GameState& state,
    SurfaceDepthUpgradeKind kind)
{
    switch (kind) {
    case SurfaceDepthUpgradeKind::SurveyArray:
        return state.run.surfaceRefitPurchases.surveyArray;
    case SurfaceDepthUpgradeKind::BoreSystem:
        return state.run.surfaceRefitPurchases.boreSystem;
    case SurfaceDepthUpgradeKind::None:
        return false;
    }
    return false;
}

int launchTrainingStageOrdinal(LaunchTrainingStage stage)
{
    return static_cast<int>(stage);
}

bool launchTrainingAtLeast(const GameState& state, LaunchTrainingStage stage)
{
    return launchTrainingStageOrdinal(state.meta.launchLessons.stage) >= launchTrainingStageOrdinal(stage);
}

const std::array<const char*, 3>& launchUpgradeIds(LaunchUpgradeKind kind)
{
    static constexpr std::array<const char*, 3> fuel {
        content::module::fuelTanks1,
        content::module::fuelTanks2,
        content::module::fuelTanks3};
    static constexpr std::array<const char*, 3> controls {
        content::module::flightControls1,
        content::module::flightControls2,
        content::module::flightControls3};
    static constexpr std::array<const char*, 3> cooling {
        content::module::coolingSystem1,
        content::module::coolingSystem2,
        content::module::coolingSystem3};
    static constexpr std::array<const char*, 3> hull {
        content::module::hullPlating1,
        content::module::hullPlating2,
        content::module::hullPlating3};
    static constexpr std::array<const char*, 3> none {"", "", ""};
    switch (kind) {
    case LaunchUpgradeKind::FuelTanks: return fuel;
    case LaunchUpgradeKind::FlightControls: return controls;
    case LaunchUpgradeKind::Cooling: return cooling;
    case LaunchUpgradeKind::Hull: return hull;
    case LaunchUpgradeKind::None: return none;
    }
    return none;
}

const std::array<const char*, 3>& surfaceDepthUpgradeIds(
    SurfaceDepthUpgradeKind kind)
{
    static constexpr std::array<const char*, 3> survey {
        content::module::surveyArray1,
        content::module::surveyArray2,
        content::module::surveyArray3};
    static constexpr std::array<const char*, 3> bore {
        content::module::boreSystem1,
        content::module::boreSystem2,
        content::module::boreSystem3};
    static constexpr std::array<const char*, 3> none {"", "", ""};
    switch (kind) {
    case SurfaceDepthUpgradeKind::SurveyArray: return survey;
    case SurfaceDepthUpgradeKind::BoreSystem: return bore;
    case SurfaceDepthUpgradeKind::None: return none;
    }
    return none;
}

const std::array<const char*, 3>& rigFuelLoopUpgradeIds()
{
    static constexpr std::array<const char*, 3> ids {
        content::module::rigFuelLoop1,
        content::module::rigFuelLoop2,
        content::module::rigFuelLoop3};
    return ids;
}

void syncSurfaceDepthUpgradeProgress(GameState& state, const ContentCatalog& catalog)
{
    for (const SurfaceDepthUpgradeKind kind : {
             SurfaceDepthUpgradeKind::SurveyArray,
             SurfaceDepthUpgradeKind::BoreSystem}) {
        int& rank = surfaceDepthUpgradeRankRef(state.meta.surfaceDepthUpgrades, kind);
        rank = std::clamp(
            rank,
            0,
            tuning::surfaceDepthProgression::maximumUpgradeRank);
        const auto& ids = surfaceDepthUpgradeIds(kind);
        for (int index = 0;
             index < tuning::surfaceDepthProgression::maximumUpgradeRank;
             ++index) {
            const ShipModule* module = catalog.findModule(ids[static_cast<std::size_t>(index)]);
            if (module != nullptr && containsId(state.meta.ownedModuleIds, module->id)) {
                rank = std::max(rank, index + 1);
            }
        }
        for (int index = 0; index < rank; ++index) {
            const std::string id = ids[static_cast<std::size_t>(index)];
            if (catalog.findModule(id) != nullptr) {
                addUniqueId(state.meta.ownedModuleIds, id);
            }
        }
    }

    int& fuelLoopRank = state.meta.rigFuelLoop.rank;
    fuelLoopRank = std::clamp(
        fuelLoopRank,
        0,
        tuning::rigFuelLoopProgression::maximumUpgradeRank);
    const auto& fuelLoopIds = rigFuelLoopUpgradeIds();
    for (int index = 0;
         index < tuning::rigFuelLoopProgression::maximumUpgradeRank;
         ++index) {
        const ShipModule* module = catalog.findModule(
            fuelLoopIds[static_cast<std::size_t>(index)]);
        if (module != nullptr && containsId(state.meta.ownedModuleIds, module->id)) {
            fuelLoopRank = std::max(fuelLoopRank, index + 1);
        }
    }
    for (int index = 0; index < fuelLoopRank; ++index) {
        const std::string id = fuelLoopIds[static_cast<std::size_t>(index)];
        if (catalog.findModule(id) != nullptr) {
            addUniqueId(state.meta.ownedModuleIds, id);
        }
    }
}

const Destination* launchTrainingDestination(const GameState& state, const ContentCatalog& catalog)
{
    switch (state.meta.launchLessons.stage) {
    case LaunchTrainingStage::FuelCalibration:
    case LaunchTrainingStage::FlightControlsCalibration:
    case LaunchTrainingStage::MoonTransfer:
        return catalog.findDestination(content::destination::moon);
    case LaunchTrainingStage::ThermalManagement:
    case LaunchTrainingStage::MarsTransfer:
        return catalog.findDestination(content::destination::mars);
    case LaunchTrainingStage::HullIntegrity:
    case LaunchTrainingStage::JupiterTransfer:
        return catalog.findDestination(content::destination::jupiter);
    case LaunchTrainingStage::Complete:
        return nullptr;
    }
    return nullptr;
}

bool launchTrainingTransferStage(LaunchTrainingStage stage)
{
    return launchStageUsesArrival(stage);
}

bool launchCurriculumTransferStage(LaunchTrainingStage stage)
{
    return stage == LaunchTrainingStage::MoonTransfer ||
        stage == LaunchTrainingStage::MarsTransfer ||
        stage == LaunchTrainingStage::JupiterTransfer;
}

bool launchLessonMissionActive(
    LaunchTrainingStage trainingStage,
    LaunchMissionKind missionKind)
{
    return
        (trainingStage == LaunchTrainingStage::FuelCalibration &&
            missionKind == LaunchMissionKind::FuelCalibration) ||
        (trainingStage == LaunchTrainingStage::FlightControlsCalibration &&
            missionKind == LaunchMissionKind::FlightControlsCalibration) ||
        (trainingStage == LaunchTrainingStage::ThermalManagement &&
            missionKind == LaunchMissionKind::ThermalManagement) ||
        (trainingStage == LaunchTrainingStage::HullIntegrity &&
            missionKind == LaunchMissionKind::AsteroidBelt);
}

bool launchLessonReturnSucceeded(
    LaunchTrainingStage trainingStage,
    LaunchMissionKind missionKind,
    const LaunchOutcome& outcome,
    double targetMultiplier)
{
    return launchLessonMissionActive(trainingStage, missionKind) &&
        outcome.type != LaunchResultType::Destroyed &&
        outcome.failureCause == LaunchFailureCause::None &&
        outcome.recoveryMethod == RecoveryMethod::ReturnHome &&
        outcome.ejectMultiplier + 0.000001 >= targetMultiplier;
}

bool launchLessonArrivalSucceeded(
    LaunchTrainingStage trainingStage,
    LaunchMissionKind missionKind,
    const LaunchOutcome& outcome)
{
    const bool arrivalLesson =
        (trainingStage == LaunchTrainingStage::ThermalManagement &&
            missionKind == LaunchMissionKind::ThermalManagement) ||
        (trainingStage == LaunchTrainingStage::HullIntegrity &&
            missionKind == LaunchMissionKind::AsteroidBelt);
    return arrivalLesson &&
        outcome.type == LaunchResultType::MissionComplete &&
        outcome.failureCause == LaunchFailureCause::None &&
        outcome.frontierTransfer &&
        outcome.recoveryMethod == RecoveryMethod::TransferArrival;
}

void advanceLaunchLessonAfterReturn(GameState& state, LaunchMissionKind missionKind)
{
    switch (state.meta.launchLessons.stage) {
    case LaunchTrainingStage::FuelCalibration:
        if (missionKind == LaunchMissionKind::FuelCalibration) {
            state.meta.launchLessons.stage = LaunchTrainingStage::FlightControlsCalibration;
        }
        break;
    case LaunchTrainingStage::FlightControlsCalibration:
        if (missionKind == LaunchMissionKind::FlightControlsCalibration) {
            state.meta.launchLessons.stage = LaunchTrainingStage::MoonTransfer;
        }
        break;
    case LaunchTrainingStage::ThermalManagement:
        if (missionKind == LaunchMissionKind::ThermalManagement) {
            state.meta.launchLessons.stage = LaunchTrainingStage::MarsTransfer;
        }
        break;
    case LaunchTrainingStage::HullIntegrity:
        if (missionKind == LaunchMissionKind::AsteroidBelt) {
            state.meta.launchLessons.stage = LaunchTrainingStage::JupiterTransfer;
        }
        break;
    case LaunchTrainingStage::MoonTransfer:
    case LaunchTrainingStage::MarsTransfer:
    case LaunchTrainingStage::JupiterTransfer:
    case LaunchTrainingStage::Complete:
        break;
    }
}

double expeditionCreditFloor(const GameState& state)
{
    return launchTutorialComplete(state) ? tuning::hangar::minimumExpeditionCredits : 0.0;
}

} // namespace

int moduleOfferCost(Rarity rarity)
{
    return tuning::moduleOfferCost(rarity);
}

int moduleOfferCost(const ShipModule& module)
{
    return moduleOfferCost(module.rarity);
}

int crewUpgradeCost(const CrewUpgrade& upgrade)
{
    return moduleOfferCost(upgrade.rarity);
}

int launchUpgradeRank(const GameState& state, LaunchUpgradeKind kind)
{
    switch (kind) {
    case LaunchUpgradeKind::FuelTanks: return state.meta.launchUpgrades.fuelTanks;
    case LaunchUpgradeKind::FlightControls: return state.meta.launchUpgrades.flightControls;
    case LaunchUpgradeKind::Cooling: return state.meta.launchUpgrades.cooling;
    case LaunchUpgradeKind::Hull: return state.meta.launchUpgrades.hull;
    case LaunchUpgradeKind::None: return 0;
    }
    return 0;
}

int surfaceDepthUpgradeRank(
    const GameState& state,
    SurfaceDepthUpgradeKind kind)
{
    switch (kind) {
    case SurfaceDepthUpgradeKind::SurveyArray:
        return state.meta.surfaceDepthUpgrades.surveyArray;
    case SurfaceDepthUpgradeKind::BoreSystem:
        return state.meta.surfaceDepthUpgrades.boreSystem;
    case SurfaceDepthUpgradeKind::None:
        return 0;
    }
    return 0;
}

int surfaceDepthRating(const GameState& state, SurfaceDepthUpgradeKind kind)
{
    return tuning::surfaceDepthProgression::baseDepthRating +
        std::clamp(
            surfaceDepthUpgradeRank(state, kind),
            0,
            tuning::surfaceDepthProgression::maximumUpgradeRank);
}

int installedRigFuelLoopRank(const GameState& state)
{
    return std::clamp(
        state.meta.rigFuelLoop.rank,
        0,
        tuning::rigFuelLoopProgression::maximumUpgradeRank);
}

double launchFuelCapacity(const GameState& state)
{
    return tuning::launchProgression::baseFuelCapacity +
        static_cast<double>(launchUpgradeRank(state, LaunchUpgradeKind::FuelTanks)) *
            tuning::launchProgression::fuelPerTankRank;
}

double pendingLaunchFuelSavings(const GameState& state)
{
    return std::max(0.0, state.run.nextLaunchFuelBoost);
}

double pendingLaunchInstabilityPenalty(const GameState& state)
{
    return std::clamp(state.run.nextLaunchInstabilityPenalty, 0.0, 1.0);
}

const PendingTransferAssist* pendingTransferAssistForDestination(
    const GameState& state,
    std::string_view destinationId)
{
    const PendingTransferAssist& assist = state.run.pendingTransferAssist;
    return assist.active() && assist.targetDestinationId == destinationId ? &assist : nullptr;
}

double pendingLaunchFuelSavingsForDestination(
    const GameState& state,
    std::string_view destinationId)
{
    const PendingTransferAssist* assist = pendingTransferAssistForDestination(state, destinationId);
    return std::max(pendingLaunchFuelSavings(state), assist == nullptr ? 0.0 : assist->fuelSavings);
}

double pendingLaunchSpeedBoostForDestination(
    const GameState& state,
    std::string_view destinationId)
{
    const PendingTransferAssist* assist = pendingTransferAssistForDestination(state, destinationId);
    return std::max(
        std::max(0.0, state.run.nextLaunchSpeedBoost),
        assist == nullptr ? 0.0 : assist->speedBoost);
}

double pendingLaunchInstabilityPenaltyForDestination(
    const GameState& state,
    std::string_view destinationId)
{
    const PendingTransferAssist* assist = pendingTransferAssistForDestination(state, destinationId);
    return std::clamp(
        pendingLaunchInstabilityPenalty(state) +
            (assist == nullptr ? 0.0 : assist->instabilityPenalty),
        0.0,
        1.0);
}

const RouteLinkDefinition* routeLinkForTransit(
    const ContentCatalog& catalog,
    const RouteTransitState& transit)
{
    if (!transit.active()) {
        return nullptr;
    }
    const RouteLinkDefinition* link = catalog.findRouteLink(transit.routeLinkId);
    if (link == nullptr) {
        return nullptr;
    }
    const bool forward = link->sourceDestinationId == transit.originDestinationId &&
        link->targetDestinationId == transit.targetDestinationId;
    const bool recovery = transit.intent == RouteTransitIntent::Recovery &&
        link->recoveryAvailable &&
        link->targetDestinationId == transit.originDestinationId &&
        link->sourceDestinationId == transit.targetDestinationId;
    return forward || recovery ? link : nullptr;
}

RouteTransitState makeRouteTransit(
    const ContentCatalog& catalog,
    std::string_view sourceDestinationId,
    std::string_view targetDestinationId,
    RouteTransitIntent intent)
{
    RouteTransitState transit;
    if (intent == RouteTransitIntent::None || sourceDestinationId.empty() || targetDestinationId.empty()) {
        return transit;
    }
    const RouteLinkDefinition* link = catalog.findRouteLink(sourceDestinationId, targetDestinationId);
    if (link == nullptr && intent == RouteTransitIntent::Recovery) {
        link = catalog.findRouteLink(targetDestinationId, sourceDestinationId);
        if (link == nullptr || !link->recoveryAvailable) {
            return {};
        }
    }
    if (link == nullptr) {
        return transit;
    }
    transit.routeLinkId = link->id;
    transit.originDestinationId = std::string(sourceDestinationId);
    transit.targetDestinationId = std::string(targetDestinationId);
    transit.intent = intent;
    return transit;
}

bool routeTransitIsRecovery(const RouteTransitState& transit)
{
    return transit.active() && transit.intent == RouteTransitIntent::Recovery;
}

double calibratedTransferFuelMargin(
    const GameState& state,
    const Destination& destination)
{
    const double routeBurn = std::min(
        tuning::launch::routeFuelMaximum,
        tuning::launch::routeFuelBase +
            static_cast<double>(std::max(1, destination.tier)) *
                tuning::launch::routeFuelPerTier);
    return launchFuelCapacity(state) -
        std::max(0.0, routeBurn - pendingLaunchFuelSavingsForDestination(state, destination.id));
}

bool jupiterTransferMarginReady(const GameState& state)
{
    return launchUpgradeRank(state, LaunchUpgradeKind::FuelTanks) >= 3 ||
        (pendingTransferAssistForDestination(state, content::destination::jupiter) != nullptr &&
         pendingLaunchFuelSavingsForDestination(state, content::destination::jupiter) + 0.000001 >=
             tuning::flyby::jupiterSlingshotFuelSavings);
}

bool destinationTransferMarginReady(
    const GameState& state,
    const ContentCatalog&,
    const Destination& destination)
{
    return destination.calibratedTransferMarginRequired <= 0.0 ||
        calibratedTransferFuelMargin(state, destination) + 0.000001 >=
            destination.calibratedTransferMarginRequired;
}

const ShipModule* nextLaunchUpgrade(
    const GameState& state,
    const ContentCatalog& catalog,
    LaunchUpgradeKind kind)
{
    if (kind == LaunchUpgradeKind::None) {
        return nullptr;
    }
    const int nextRank = launchUpgradeRank(state, kind) + 1;
    if (nextRank > tuning::launchProgression::maximumUpgradeRank) {
        return nullptr;
    }
    const auto found = std::find_if(catalog.modules.begin(), catalog.modules.end(), [&](const ShipModule& module) {
        return module.launchUpgradeKind == kind && module.launchUpgradeRank == nextRank;
    });
    return found == catalog.modules.end() ? nullptr : &*found;
}

bool launchUpgradeUnlocked(const GameState& state, LaunchUpgradeKind kind, int rank)
{
    if (rank < 1 || rank > tuning::launchProgression::maximumUpgradeRank) {
        return false;
    }
    if (rank <= launchUpgradeRank(state, kind)) {
        return true;
    }
    if (rank != launchUpgradeRank(state, kind) + 1) {
        return false;
    }

    if (rank == 1) {
        switch (kind) {
        case LaunchUpgradeKind::FuelTanks:
            return launchTrainingAtLeast(state, LaunchTrainingStage::FlightControlsCalibration);
        case LaunchUpgradeKind::FlightControls:
            return launchTrainingAtLeast(state, LaunchTrainingStage::MoonTransfer);
        case LaunchUpgradeKind::Cooling:
            return launchTrainingAtLeast(state, LaunchTrainingStage::MarsTransfer);
        case LaunchUpgradeKind::Hull:
            return launchTrainingAtLeast(state, LaunchTrainingStage::JupiterTransfer);
        case LaunchUpgradeKind::None:
            return false;
        }
    }

    switch (kind) {
    case LaunchUpgradeKind::FuelTanks:
        if (rank == 2) {
            return hasUnlock(state.meta, content::unlock::routeMars);
        }
        return hasUnlock(state.meta, content::unlock::routeJupiter);
    case LaunchUpgradeKind::FlightControls:
        return state.meta.furthestTier >= rank - 1;
    case LaunchUpgradeKind::Cooling:
        return state.meta.furthestTier >= rank;
    case LaunchUpgradeKind::Hull:
        return state.meta.furthestTier >= rank + 1;
    case LaunchUpgradeKind::None:
        return false;
    }
    return false;
}

bool canInstallLaunchUpgrade(
    const GameState& state,
    const ContentCatalog& catalog,
    LaunchUpgradeKind kind)
{
    const ShipModule* module = nextLaunchUpgrade(state, catalog, kind);
    return module != nullptr &&
        launchUpgradeUnlocked(state, kind, module->launchUpgradeRank) &&
        state.run.credits >= static_cast<double>(moduleOfferCost(*module));
}

bool installLaunchUpgrade(GameState& state, const ContentCatalog& catalog, LaunchUpgradeKind kind)
{
    const ShipModule* module = nextLaunchUpgrade(state, catalog, kind);
    if (module == nullptr || !launchUpgradeUnlocked(state, kind, module->launchUpgradeRank)) {
        return false;
    }
    const int cost = moduleOfferCost(*module);
    if (state.run.credits < static_cast<double>(cost)) {
        state.statusLine = text::insufficientCreditsFor(module->name);
        return false;
    }

    state.run.credits -= static_cast<double>(cost);
    launchUpgradeRankRef(state.meta.launchUpgrades, kind) = module->launchUpgradeRank;
    addUniqueId(state.meta.ownedModuleIds, module->id);
    addUniqueId(state.meta.defaultEquippedModuleIds, module->id);
    addUniqueId(state.run.inventoryModuleIds, module->id);
    addUniqueId(state.run.equippedModuleIds, module->id);
    state.run.refitEntitled = false;
    state.run.offerModuleIds = {};
    state.run.offerCrewUpgradeIds = {};
    state.statusLine = text::refitInstalled(module->name);
    syncLaunchConfig(state, catalog);
    return true;
}

const ShipModule* nextSurfaceDepthUpgrade(
    const GameState& state,
    const ContentCatalog& catalog,
    SurfaceDepthUpgradeKind kind)
{
    if (kind == SurfaceDepthUpgradeKind::None) {
        return nullptr;
    }
    const int nextRank = surfaceDepthUpgradeRank(state, kind) + 1;
    if (nextRank > tuning::surfaceDepthProgression::maximumUpgradeRank) {
        return nullptr;
    }
    const auto found = std::find_if(
        catalog.modules.begin(),
        catalog.modules.end(),
        [&](const ShipModule& module) {
            return module.surfaceDepthUpgradeKind == kind &&
                module.surfaceDepthUpgradeRank == nextRank;
        });
    return found == catalog.modules.end() ? nullptr : &*found;
}

bool surfaceDepthUpgradeUnlocked(
    const GameState& state,
    SurfaceDepthUpgradeKind kind,
    int rank)
{
    if (kind == SurfaceDepthUpgradeKind::None || rank < 1 ||
        rank > tuning::surfaceDepthProgression::maximumUpgradeRank) {
        return false;
    }
    if (rank <= surfaceDepthUpgradeRank(state, kind)) {
        return true;
    }
    if (rank != surfaceDepthUpgradeRank(state, kind) + 1) {
        return false;
    }
    return hasUnlock(
        state.meta,
        kind == SurfaceDepthUpgradeKind::SurveyArray
            ? content::unlock::surfaceProbes
            : content::unlock::surfaceDrills);
}

bool canInstallSurfaceDepthUpgrade(
    const GameState& state,
    const ContentCatalog& catalog,
    SurfaceDepthUpgradeKind kind)
{
    const ShipModule* module = nextSurfaceDepthUpgrade(state, catalog, kind);
    return module != nullptr &&
        !surfaceDepthPurchasedThisRefit(state, kind) &&
        surfaceDepthUpgradeUnlocked(state, kind, module->surfaceDepthUpgradeRank) &&
        state.run.credits >= static_cast<double>(moduleOfferCost(*module));
}

bool installSurfaceDepthUpgrade(
    GameState& state,
    const ContentCatalog& catalog,
    SurfaceDepthUpgradeKind kind)
{
    const ShipModule* module = nextSurfaceDepthUpgrade(state, catalog, kind);
    if (module == nullptr ||
        surfaceDepthPurchasedThisRefit(state, kind) ||
        !surfaceDepthUpgradeUnlocked(state, kind, module->surfaceDepthUpgradeRank)) {
        return false;
    }
    const int cost = moduleOfferCost(*module);
    if (state.run.credits < static_cast<double>(cost)) {
        state.statusLine = text::insufficientCreditsFor(module->name);
        return false;
    }

    state.run.credits -= static_cast<double>(cost);
    surfaceDepthUpgradeRankRef(state.meta.surfaceDepthUpgrades, kind) =
        module->surfaceDepthUpgradeRank;
    surfaceDepthPurchasedThisRefitRef(
        state.run.surfaceRefitPurchases,
        kind) = true;
    addUniqueId(state.meta.ownedModuleIds, module->id);
    state.statusLine = text::refitInstalled(module->name);
    return true;
}

const ShipModule* nextRigFuelLoopUpgrade(
    const GameState& state,
    const ContentCatalog& catalog)
{
    const int nextRank = installedRigFuelLoopRank(state) + 1;
    if (nextRank > tuning::rigFuelLoopProgression::maximumUpgradeRank) {
        return nullptr;
    }
    const auto found = std::find_if(
        catalog.modules.begin(),
        catalog.modules.end(),
        [&](const ShipModule& module) {
            return module.rigFuelLoopRank == nextRank;
        });
    return found == catalog.modules.end() ? nullptr : &*found;
}

bool rigFuelLoopUpgradeUnlocked(const GameState& state, int rank)
{
    if (rank < 1 || rank > tuning::rigFuelLoopProgression::maximumUpgradeRank) {
        return false;
    }
    if (rank <= installedRigFuelLoopRank(state)) {
        return true;
    }
    return rank == installedRigFuelLoopRank(state) + 1 &&
        hasUnlock(state.meta, content::unlock::surfaceDrills);
}

bool canInstallRigFuelLoopUpgrade(
    const GameState& state,
    const ContentCatalog& catalog)
{
    const ShipModule* module = nextRigFuelLoopUpgrade(state, catalog);
    return module != nullptr &&
        !state.run.surfaceRefitPurchases.rigFuelLoop &&
        rigFuelLoopUpgradeUnlocked(state, module->rigFuelLoopRank) &&
        state.run.credits >= static_cast<double>(moduleOfferCost(*module));
}

bool installRigFuelLoopUpgrade(
    GameState& state,
    const ContentCatalog& catalog)
{
    const ShipModule* module = nextRigFuelLoopUpgrade(state, catalog);
    if (module == nullptr ||
        state.run.surfaceRefitPurchases.rigFuelLoop ||
        !rigFuelLoopUpgradeUnlocked(state, module->rigFuelLoopRank)) {
        return false;
    }
    const int cost = moduleOfferCost(*module);
    if (state.run.credits < static_cast<double>(cost)) {
        state.statusLine = text::insufficientCreditsFor(module->name);
        return false;
    }

    state.run.credits -= static_cast<double>(cost);
    state.meta.rigFuelLoop.rank = module->rigFuelLoopRank;
    state.run.surfaceRefitPurchases.rigFuelLoop = true;
    addUniqueId(state.meta.ownedModuleIds, module->id);
    state.statusLine = text::refitInstalled(module->name);
    return true;
}

bool launchTutorialComplete(const GameState& state)
{
    return state.meta.furthestTier >= 1 ||
        launchTrainingAtLeast(state, LaunchTrainingStage::ThermalManagement);
}

bool launchStageUsesArrival(LaunchTrainingStage stage)
{
    return stage != LaunchTrainingStage::FuelCalibration &&
        stage != LaunchTrainingStage::FlightControlsCalibration;
}

bool launchMissionReady(const GameState& state)
{
    switch (state.meta.launchLessons.stage) {
    case LaunchTrainingStage::FuelCalibration:
    case LaunchTrainingStage::Complete:
        return true;
    case LaunchTrainingStage::FlightControlsCalibration:
        return launchUpgradeRank(state, LaunchUpgradeKind::FuelTanks) >= 1;
    case LaunchTrainingStage::MoonTransfer:
        return true;
    case LaunchTrainingStage::ThermalManagement:
        return hasUnlock(state.meta, content::unlock::routeMars) &&
            launchUpgradeRank(state, LaunchUpgradeKind::FuelTanks) >= 2;
    case LaunchTrainingStage::MarsTransfer:
        return hasUnlock(state.meta, content::unlock::routeMars) &&
            launchUpgradeRank(state, LaunchUpgradeKind::FuelTanks) >= 2;
    case LaunchTrainingStage::HullIntegrity:
        return hasUnlock(state.meta, content::unlock::routeJupiter) &&
            jupiterTransferMarginReady(state);
    case LaunchTrainingStage::JupiterTransfer:
        return hasUnlock(state.meta, content::unlock::routeJupiter) &&
            jupiterTransferMarginReady(state);
    }
    return false;
}

bool launchMissionReady(const GameState& state, const ContentCatalog& catalog)
{
    if (!launchMissionReady(state)) {
        return false;
    }
    if (state.meta.launchLessons.stage == LaunchTrainingStage::ThermalManagement ||
        state.meta.launchLessons.stage == LaunchTrainingStage::HullIntegrity) {
        const Destination* trainingDestination = launchTrainingDestination(state, catalog);
        return trainingDestination != nullptr &&
            scenarioRouteRequirementStatus(state, catalog, *trainingDestination).satisfied;
    }
    if (!launchTrainingTransferStage(state.meta.launchLessons.stage)) {
        return true;
    }
    return canCommitToNextFrontier(state, catalog);
}

bool currentDestinationLaunchReady(
    const GameState& state,
    const ContentCatalog& catalog)
{
    // Once the player has reached a frontier, replaying that destination is a
    // valid recovery path and must not inherit the next route's curriculum
    // hardware gate. The hidden starter origin still uses lesson readiness.
    return !currentDestination(state, catalog).hiddenFromProgression ||
        launchMissionReady(state, catalog);
}

LaunchMissionKind currentLaunchMissionKind(const GameState& state, const ContentCatalog& catalog)
{
    static_cast<void>(catalog);
    switch (state.meta.launchLessons.stage) {
    case LaunchTrainingStage::FuelCalibration:
        return LaunchMissionKind::FuelCalibration;
    case LaunchTrainingStage::FlightControlsCalibration:
        return LaunchMissionKind::FlightControlsCalibration;
    case LaunchTrainingStage::ThermalManagement:
        return LaunchMissionKind::ThermalManagement;
    case LaunchTrainingStage::HullIntegrity:
        return LaunchMissionKind::AsteroidBelt;
    case LaunchTrainingStage::MoonTransfer:
    case LaunchTrainingStage::MarsTransfer:
    case LaunchTrainingStage::JupiterTransfer:
    case LaunchTrainingStage::Complete:
        return LaunchMissionKind::Standard;
    }
    return LaunchMissionKind::Standard;
}

void syncLaunchTrainingProgress(GameState& state, const ContentCatalog& catalog)
{
    for (const LaunchUpgradeKind kind : {
             LaunchUpgradeKind::FuelTanks,
             LaunchUpgradeKind::FlightControls,
             LaunchUpgradeKind::Cooling,
             LaunchUpgradeKind::Hull}) {
        int& rank = launchUpgradeRankRef(state.meta.launchUpgrades, kind);
        rank = std::clamp(rank, 0, tuning::launchProgression::maximumUpgradeRank);
        const auto& ids = launchUpgradeIds(kind);
        for (int index = 0; index < tuning::launchProgression::maximumUpgradeRank; ++index) {
            const ShipModule* module = catalog.findModule(ids[static_cast<std::size_t>(index)]);
            if (module != nullptr && containsId(state.meta.ownedModuleIds, module->id)) {
                rank = std::max(rank, index + 1);
            }
        }
    }

    if (state.meta.furthestTier >= 3) {
        state.meta.launchLessons.stage = LaunchTrainingStage::Complete;
        state.meta.launchUpgrades.fuelTanks = std::max(3, state.meta.launchUpgrades.fuelTanks);
        state.meta.launchUpgrades.flightControls = std::max(1, state.meta.launchUpgrades.flightControls);
    } else if (state.meta.furthestTier >= 2) {
        state.meta.launchUpgrades.fuelTanks = std::max(2, state.meta.launchUpgrades.fuelTanks);
        state.meta.launchUpgrades.flightControls = std::max(1, state.meta.launchUpgrades.flightControls);
        if (!launchTrainingAtLeast(state, LaunchTrainingStage::HullIntegrity)) {
            state.meta.launchLessons.stage = LaunchTrainingStage::HullIntegrity;
        }
    } else if (state.meta.furthestTier >= 1) {
        state.meta.launchUpgrades.fuelTanks = std::max(1, state.meta.launchUpgrades.fuelTanks);
        state.meta.launchUpgrades.flightControls = std::max(1, state.meta.launchUpgrades.flightControls);
        if (!launchTrainingAtLeast(state, LaunchTrainingStage::ThermalManagement)) {
            state.meta.launchLessons.stage = LaunchTrainingStage::ThermalManagement;
        }
    }

    for (const LaunchUpgradeKind kind : {
             LaunchUpgradeKind::FuelTanks,
             LaunchUpgradeKind::FlightControls,
             LaunchUpgradeKind::Cooling,
             LaunchUpgradeKind::Hull}) {
        const int rank = launchUpgradeRank(state, kind);
        const auto& ids = launchUpgradeIds(kind);
        for (int index = 0; index < rank; ++index) {
            const std::string id = ids[static_cast<std::size_t>(index)];
            if (catalog.findModule(id) == nullptr) {
                continue;
            }
            addUniqueId(state.meta.ownedModuleIds, id);
            addUniqueId(state.meta.defaultEquippedModuleIds, id);
            addUniqueId(state.run.inventoryModuleIds, id);
            addUniqueId(state.run.equippedModuleIds, id);
        }
    }

}

bool canAffordMaterials(const MaterialInventory& owned, const MaterialInventory& cost)
{
    return owned.common >= cost.common && owned.rare >= cost.rare && owned.exotic >= cost.exotic;
}

bool spendMaterials(MaterialInventory& owned, const MaterialInventory& cost)
{
    if (!canAffordMaterials(owned, cost)) {
        return false;
    }

    owned.common -= cost.common;
    owned.rare -= cost.rare;
    owned.exotic -= cost.exotic;
    return true;
}

bool canAffordModuleOffer(const GameState& state, const ShipModule& module)
{
    return state.run.credits >= static_cast<double>(moduleOfferCost(module)) &&
        canAffordMaterials(state.meta.materials, module.materialCost);
}

CrewUpgradeStats& operator+=(CrewUpgradeStats& lhs, const CrewUpgradeStats& rhs)
{
    lhs.traitModifier += rhs.traitModifier;
    return lhs;
}

double defaultProvingTarget(const Destination& destination)
{
    return std::clamp(
        1.0 + (destination.targetMultiplier - 1.0) * tuning::mission::defaultProvingTargetShare,
        tuning::mission::defaultProvingTargetMinimum,
        destination.targetMultiplier);
}

GameState createNewGame(const ContentCatalog& catalog, std::uint64_t seed)
{
    GameState state;
    state.seed = seed;
    state.meta.unlockKeys = {content::unlock::starter};
    // Fresh campaigns begin with the real lunar transfer. Fuel and steering are taught
    // contextually in that flight; the retired calibration sorties are not
    // part of a fresh campaign.
    state.meta.launchLessons.stage = LaunchTrainingStage::MoonTransfer;
    state.meta.ark.fuelReserve = tuning::ark::startingFuelReserve;
    state.run.credits = tuning::hangar::startingCredits;
    state.run.crew = catalog.astronauts;
    ensureDestinationHistory(state, catalog);
    ensureScenarioInstances(state, catalog);
    startNewExpedition(state, catalog);

    state.statusLine = std::string(text::status::programInitialized);
    return state;
}

void startNewExpedition(GameState& state, const ContentCatalog& catalog)
{
    ensureDestinationHistory(state, catalog);
    ensureScenarioInstances(state, catalog);
    ensurePermanentModuleState(state, catalog);
    syncLaunchTrainingProgress(state, catalog);
    state.screen = hostileSystemActive(state) ? Screen::Navigation : Screen::Hangar;
    state.run.active = true;
    if (hostileSystemActive(state) && !state.meta.navigation.selectedDestinationId.empty()) {
        const int selectedIndex = destinationIndexForId(catalog, state.meta.navigation.selectedDestinationId);
        state.run.destinationIndex = selectedIndex >= 0 ? selectedIndex : std::clamp(state.meta.furthestTier, 0, static_cast<int>(catalog.destinations.size()) - 1);
    } else {
        state.run.destinationIndex = std::clamp(state.meta.furthestTier, 0, static_cast<int>(catalog.destinations.size()) - 1);
    }
    state.run.frontierReadiness = std::clamp(state.run.frontierReadiness, 0, frontierReadinessCap(state, catalog));
    state.run.shipDamage = 0;
    state.run.frameId = catalog.frames.empty() ? "" : catalog.frames.front().id;
    state.run.inventoryModuleIds = state.meta.ownedModuleIds;
    state.run.equippedModuleIds = state.meta.defaultEquippedModuleIds;
    state.run.planetaryExpedition = {};
    state.run.surfaceScan = {};
    state.run.surfacePush = {};
    state.run.mining = {};
    state.run.offerModuleIds = {};
    state.run.offerCrewUpgradeIds = {};
    state.run.launchesThisExpedition = 0;
    state.run.offerRerollsThisExpedition = 0;
    state.run.repairOpsThisExpedition = 0;
    const double creditFloor = expeditionCreditFloor(state);
    if (state.run.credits < creditFloor) {
        state.run.credits = creditFloor;
    }

    if (state.run.crew.empty()) {
        state.run.crew = catalog.astronauts;
    }

    if (activeAstronaut(state) == nullptr && !catalog.astronauts.empty()) {
        Astronaut recruit = catalog.astronauts.front();
        recruit.id = text::replacementId(state.meta.astronautsLost + state.meta.shipsLost + 1);
        recruit.name = std::string(text::panel::messages::replacementCadet);
        recruit.background = std::string(text::panel::messages::emergencyRecruitBackground);
        recruit.trait = std::string(text::panel::messages::generatedRecruitTrait);
        recruit.status = CrewStatus::Active;
        state.run.crew.push_back(recruit);
    }

    for (auto& astronaut : state.run.crew) {
        if (astronaut.status == CrewStatus::Injured) {
            astronaut.status = CrewStatus::Active;
        }
    }

    syncLaunchConfig(state, catalog);
}

void syncLaunchConfig(GameState& state, const ContentCatalog& catalog)
{
    ensureDestinationHistory(state, catalog);
    syncLaunchTrainingProgress(state, catalog);
    syncSurfaceDepthUpgradeProgress(state, catalog);
    const Destination* destination = launchTrainingDestination(state, catalog);
    if (destination != nullptr) {
        state.launchConfig.destinationId = destination->id;
        state.launchConfig.frontierTransfer = launchTrainingTransferStage(state.meta.launchLessons.stage);
    } else {
        destination = catalog.findDestination(state.launchConfig.destinationId);
        if (destination == nullptr || !state.launchConfig.frontierTransfer) {
            destination = &currentDestination(state, catalog);
            state.launchConfig.destinationId = destination->id;
        }
    }
    state.launchConfig.missionKind = currentLaunchMissionKind(state, catalog);
    state.launchConfig.frameId = state.run.frameId;
    state.launchConfig.equippedModuleIds = state.run.equippedModuleIds;

    // A queued route always overrides the otherwise frontier-derived target.
    // Training missions intentionally do not carry a route transit.
    if (state.run.routeTransit.active() &&
        routeLinkForTransit(catalog, state.run.routeTransit) != nullptr) {
        state.launchConfig.routeTransit = state.run.routeTransit;
        state.launchConfig.destinationId = state.run.routeTransit.targetDestinationId;
        state.launchConfig.frontierTransfer = true;
        if (const Destination* routeTarget = catalog.findDestination(state.run.routeTransit.targetDestinationId)) {
            state.launchConfig.burnGoalMultiplier = routeTarget->targetMultiplier;
        }
    } else {
        state.launchConfig.routeTransit = {};
    }

    if (launchTrainingDestination(state, catalog) != nullptr) {
        state.launchConfig.burnGoalMultiplier = state.launchConfig.frontierTransfer
            ? destination->targetMultiplier
            : 1.0 + (destination->targetMultiplier - 1.0) * tuning::launchProgression::calibrationTargetShare;
    } else if (state.launchConfig.burnGoalMultiplier < tuning::mission::launchConfigMinimumMultiplier ||
        state.launchConfig.burnGoalMultiplier > destination->targetMultiplier + tuning::mission::launchConfigOverTargetAllowance) {
        state.launchConfig.burnGoalMultiplier = state.launchConfig.frontierTransfer ? destination->targetMultiplier : defaultProvingTarget(*destination);
    }

    if (state.launchConfig.astronautId.empty()) {
        if (const Astronaut* astronaut = activeAstronaut(state)) {
            state.launchConfig.astronautId = astronaut->id;
        }
    } else {
        const auto selected = std::find_if(state.run.crew.begin(), state.run.crew.end(), [&](const Astronaut& astronaut) {
            return astronaut.id == state.launchConfig.astronautId && astronaut.status != CrewStatus::Dead;
        });
        if (selected == state.run.crew.end()) {
            if (const Astronaut* astronaut = activeAstronaut(state)) {
                state.launchConfig.astronautId = astronaut->id;
            } else {
                state.launchConfig.astronautId.clear();
            }
        }
    }

    syncChapterProgress(state, catalog);
}

bool curatedProvingRefitsActive(const GameState& state)
{
    if (!state.run.refitEntitled) {
        return false;
    }
    const LaunchTrainingStage stage = state.meta.launchLessons.stage;
    if (stage == LaunchTrainingStage::FlightControlsCalibration) {
        return launchUpgradeRank(state, LaunchUpgradeKind::FuelTanks) < 1;
    }
    if (stage == LaunchTrainingStage::MoonTransfer) {
        return launchUpgradeRank(state, LaunchUpgradeKind::FlightControls) < 1;
    }
    return (stage == LaunchTrainingStage::ThermalManagement ||
               stage == LaunchTrainingStage::MarsTransfer) &&
        hasUnlock(state.meta, content::unlock::routeMars) &&
        launchUpgradeRank(state, LaunchUpgradeKind::FuelTanks) < 2;
}

void generateModuleOffers(GameState& state, const ContentCatalog& catalog, Random& rng)
{
    state.run.offerModuleIds = {};
    state.run.offerCrewUpgradeIds = {};

    const auto modulePool = unlockedModules(catalog, state.meta);
    const auto crewPool = unlockedCrewUpgrades(catalog, state.meta);
    if (modulePool.empty() && crewPool.empty()) {
        state.run.offerModuleIds = {};
        state.run.offerCrewUpgradeIds = {};
        return;
    }

    const auto ownsModule = [&](std::string_view id) {
        return id.empty() || containsId(state.meta.ownedModuleIds, id);
    };
    const auto ownsCrewUpgrade = [&](std::string_view id) {
        return id.empty() || containsId(state.run.crewUpgradeIds, id);
    };

    if (curatedProvingRefitsActive(state)) {
        LaunchUpgradeKind requiredKind = LaunchUpgradeKind::None;
        switch (state.meta.launchLessons.stage) {
        case LaunchTrainingStage::FlightControlsCalibration:
            requiredKind = LaunchUpgradeKind::FuelTanks;
            break;
        case LaunchTrainingStage::MoonTransfer:
            requiredKind = LaunchUpgradeKind::FlightControls;
            break;
        case LaunchTrainingStage::ThermalManagement:
        case LaunchTrainingStage::MarsTransfer:
            requiredKind = LaunchUpgradeKind::FuelTanks;
            break;
        case LaunchTrainingStage::FuelCalibration:
        case LaunchTrainingStage::HullIntegrity:
        case LaunchTrainingStage::JupiterTransfer:
        case LaunchTrainingStage::Complete:
            break;
        }
        const ShipModule* next = nextLaunchUpgrade(state, catalog, requiredKind);
        if (next != nullptr && launchUpgradeUnlocked(state, requiredKind, next->launchUpgradeRank)) {
            state.run.offerModuleIds[0] = next->id;
        }
        return;
    }

    std::vector<RefitCandidate> candidates;
    for (const ShipModule* module : modulePool) {
        if (module->compatibilityOnly && module->launchUpgradeKind == LaunchUpgradeKind::None) {
            continue;
        }
        if (ownsModule(module->id) || !ownsModule(module->prerequisiteId)) {
            continue;
        }
        if (module->launchUpgradeKind != LaunchUpgradeKind::None &&
            (module->launchUpgradeRank != launchUpgradeRank(state, module->launchUpgradeKind) + 1 ||
             !launchUpgradeUnlocked(state, module->launchUpgradeKind, module->launchUpgradeRank))) {
            continue;
        }
        if (module->surfaceDepthUpgradeKind != SurfaceDepthUpgradeKind::None &&
            (module->surfaceDepthUpgradeRank !=
                 surfaceDepthUpgradeRank(state, module->surfaceDepthUpgradeKind) + 1 ||
             surfaceDepthPurchasedThisRefit(
                 state,
                 module->surfaceDepthUpgradeKind) ||
             !surfaceDepthUpgradeUnlocked(
                 state,
                 module->surfaceDepthUpgradeKind,
                 module->surfaceDepthUpgradeRank))) {
            continue;
        }
        if (module->rigFuelLoopRank > 0 &&
            (module->rigFuelLoopRank != installedRigFuelLoopRank(state) + 1 ||
             state.run.surfaceRefitPurchases.rigFuelLoop ||
             !rigFuelLoopUpgradeUnlocked(state, module->rigFuelLoopRank))) {
            continue;
        }
        if (!materialRefitsAvailable(state) && hasMaterialCost(module->materialCost)) {
            continue;
        }
        candidates.push_back({RefitOfferKind::ShipModule, module->id, moduleOfferCost(*module), module->refitTrack});
    }

    for (const CrewUpgrade* upgrade : crewPool) {
        if (ownsCrewUpgrade(upgrade->id) || !ownsCrewUpgrade(upgrade->prerequisiteId)) {
            continue;
        }
        candidates.push_back({RefitOfferKind::CrewUpgrade, upgrade->id, crewUpgradeCost(*upgrade), upgrade->refitTrack});
    }

    if (candidates.empty()) {
        return;
    }

    const auto candidateAffordable = [&](const RefitCandidate& candidate) {
        if (candidate.kind == RefitOfferKind::ShipModule) {
            const ShipModule* module = catalog.findModule(candidate.id);
            return module != nullptr && canAffordModuleOffer(state, *module);
        }
        const CrewUpgrade* upgrade = catalog.findCrewUpgrade(candidate.id);
        return upgrade != nullptr && state.run.credits >= static_cast<double>(crewUpgradeCost(*upgrade));
    };

    std::vector<RefitCandidate> remaining = candidates;
    std::vector<RefitCandidate> pickedIds;
    pickedIds.reserve(state.run.offerModuleIds.size());
    for (const SurfaceDepthUpgradeKind kind : {
             SurfaceDepthUpgradeKind::SurveyArray,
             SurfaceDepthUpgradeKind::BoreSystem}) {
        if (pickedIds.size() >= state.run.offerModuleIds.size()) {
            break;
        }
        const ShipModule* next = nextSurfaceDepthUpgrade(state, catalog, kind);
        if (next == nullptr ||
            surfaceDepthPurchasedThisRefit(state, kind) ||
            !surfaceDepthUpgradeUnlocked(state, kind, next->surfaceDepthUpgradeRank)) {
            continue;
        }
        const auto pinned = std::find_if(
            remaining.begin(),
            remaining.end(),
            [&](const RefitCandidate& candidate) {
                return candidate.kind == RefitOfferKind::ShipModule &&
                    candidate.id == next->id;
            });
        if (pinned != remaining.end()) {
            pickedIds.push_back(*pinned);
            remaining.erase(pinned);
        }
    }
    if (pickedIds.size() < state.run.offerModuleIds.size()) {
        const ShipModule* next = nextRigFuelLoopUpgrade(state, catalog);
        if (next != nullptr &&
            !state.run.surfaceRefitPurchases.rigFuelLoop &&
            rigFuelLoopUpgradeUnlocked(state, next->rigFuelLoopRank)) {
            const auto pinned = std::find_if(
                remaining.begin(),
                remaining.end(),
                [&](const RefitCandidate& candidate) {
                    return candidate.kind == RefitOfferKind::ShipModule &&
                        candidate.id == next->id;
                });
            if (pinned != remaining.end()) {
                pickedIds.push_back(*pinned);
                remaining.erase(pinned);
            }
        }
    }
    const auto pinnedFuelTanksThree = std::find_if(
        remaining.begin(),
        remaining.end(),
        [](const RefitCandidate& candidate) {
            return candidate.kind == RefitOfferKind::ShipModule &&
                candidate.id == content::module::fuelTanks3;
        });
    if (pinnedFuelTanksThree != remaining.end() &&
        pickedIds.size() < state.run.offerModuleIds.size()) {
        pickedIds.push_back(*pinnedFuelTanksThree);
        remaining.erase(pinnedFuelTanksThree);
    }
    const auto pickMatchingTrack = [&](RefitTrack track) {
        std::vector<std::size_t> matches;
        for (std::size_t i = 0; i < remaining.size(); ++i) {
            if (remaining[i].track == track) {
                matches.push_back(i);
            }
        }
        if (matches.empty()) {
            return;
        }
        const std::size_t match = matches[static_cast<std::size_t>(rng.rangeInt(0, static_cast<int>(matches.size()) - 1))];
        pickedIds.push_back(remaining[match]);
        remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(match));
    };

    for (const RefitTrack track : {RefitTrack::Reach, RefitTrack::Control, RefitTrack::Recovery}) {
        if (pickedIds.size() >= state.run.offerModuleIds.size()) {
            break;
        }
        pickMatchingTrack(track);
    }
    while (pickedIds.size() < state.run.offerModuleIds.size() && !remaining.empty()) {
        const std::size_t index = static_cast<std::size_t>(rng.rangeInt(0, static_cast<int>(remaining.size()) - 1));
        pickedIds.push_back(remaining[index]);
        remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(index));
    }

    const bool hasAffordableOffer = std::any_of(pickedIds.begin(), pickedIds.end(), candidateAffordable);
    if (!hasAffordableOffer && state.run.credits > 0.0 && !pickedIds.empty()) {
        const auto cheapestAffordable = std::min_element(remaining.begin(), remaining.end(), [&](const RefitCandidate& lhs, const RefitCandidate& rhs) {
            const bool lhsAffordable = candidateAffordable(lhs);
            const bool rhsAffordable = candidateAffordable(rhs);
            if (lhsAffordable != rhsAffordable) {
                return lhsAffordable;
            }
            return lhs.cost < rhs.cost;
        });
        const ShipModule* lastModule = catalog.findModule(pickedIds.back().id);
        const bool lastOfferIsGuaranteed =
            pickedIds.back().id == content::module::fuelTanks3 ||
            (lastModule != nullptr &&
             (lastModule->surfaceDepthUpgradeKind != SurfaceDepthUpgradeKind::None ||
              lastModule->rigFuelLoopRank > 0));
        if (cheapestAffordable != remaining.end() && candidateAffordable(*cheapestAffordable) &&
            !lastOfferIsGuaranteed) {
            pickedIds.back() = *cheapestAffordable;
        }
    }

    for (std::size_t i = 0; i < pickedIds.size(); ++i) {
        if (pickedIds[i].kind == RefitOfferKind::ShipModule) {
            state.run.offerModuleIds[i] = pickedIds[i].id;
        } else {
            state.run.offerCrewUpgradeIds[i] = pickedIds[i].id;
        }
    }
}

void beginRefitVisit(GameState& state)
{
    state.run.surfaceRefitPurchases = {};
}

double offerRerollCost(const GameState& state)
{
    return tuning::offerRerollCost(state.run.offerRerollsThisExpedition);
}

bool rerollOffers(GameState& state, const ContentCatalog& catalog, Random& rng)
{
    if (curatedProvingRefitsActive(state)) {
        return false;
    }
    const double cost = offerRerollCost(state);
    if (state.run.credits < cost) {
        state.statusLine = std::string(text::status::refitRerollUnaffordable);
        return false;
    }

    state.run.credits -= cost;
    state.run.offerRerollsThisExpedition += 1;
    generateModuleOffers(state, catalog, rng);
    state.statusLine = text::refitRerolled(static_cast<int>(offerRerollCost(state)));
    return true;
}

bool buyOffer(GameState& state, const ContentCatalog& catalog, int index)
{
    if (index < 0 || index >= static_cast<int>(state.run.offerModuleIds.size())) {
        return false;
    }

    const auto offerIndex = static_cast<std::size_t>(index);
    const ShipModule* module = catalog.findModule(state.run.offerModuleIds[offerIndex]);
    const CrewUpgrade* crewUpgrade = catalog.findCrewUpgrade(state.run.offerCrewUpgradeIds[offerIndex]);
    if (module == nullptr && crewUpgrade == nullptr) {
        return false;
    }

    if (module != nullptr && module->launchUpgradeKind != LaunchUpgradeKind::None) {
        const ShipModule* next = nextLaunchUpgrade(state, catalog, module->launchUpgradeKind);
        if (next == nullptr || next->id != module->id) {
            return false;
        }
        return installLaunchUpgrade(state, catalog, module->launchUpgradeKind);
    }

    if (module != nullptr &&
        module->surfaceDepthUpgradeKind != SurfaceDepthUpgradeKind::None) {
        const ShipModule* next = nextSurfaceDepthUpgrade(
            state,
            catalog,
            module->surfaceDepthUpgradeKind);
        if (next == nullptr || next->id != module->id) {
            return false;
        }
        const bool installed = installSurfaceDepthUpgrade(
            state,
            catalog,
            module->surfaceDepthUpgradeKind);
        if (installed) {
            state.run.offerModuleIds[offerIndex].clear();
            state.run.offerCrewUpgradeIds[offerIndex].clear();
        }
        return installed;
    }

    if (module != nullptr && module->rigFuelLoopRank > 0) {
        const ShipModule* next = nextRigFuelLoopUpgrade(state, catalog);
        if (next == nullptr || next->id != module->id) {
            return false;
        }
        const bool installed = installRigFuelLoopUpgrade(state, catalog);
        if (installed) {
            state.run.offerModuleIds[offerIndex].clear();
            state.run.offerCrewUpgradeIds[offerIndex].clear();
        }
        return installed;
    }

    const int cost = module != nullptr ? moduleOfferCost(*module) : crewUpgradeCost(*crewUpgrade);
    if (state.run.credits < static_cast<double>(cost)) {
        state.statusLine = text::insufficientCreditsFor(module != nullptr ? module->name : crewUpgrade->name);
        return false;
    }
    if (module != nullptr && !canAffordMaterials(state.meta.materials, module->materialCost)) {
        state.statusLine = std::string(text::panel::needMaterials);
        return false;
    }

    state.run.credits -= static_cast<double>(cost);
    if (module != nullptr) {
        spendMaterials(state.meta.materials, module->materialCost);
        addUniqueId(state.meta.ownedModuleIds, module->id);
        addUniqueId(state.run.inventoryModuleIds, module->id);
        addUniqueId(state.run.equippedModuleIds, module->id);
        addUniqueId(state.meta.defaultEquippedModuleIds, module->id);
    } else {
        addUniqueId(state.run.crewUpgradeIds, crewUpgrade->id);
    }

    state.run.offerModuleIds = {};
    state.run.offerCrewUpgradeIds = {};
    state.run.refitEntitled = false;
    state.statusLine = text::refitInstalled(module != nullptr ? module->name : crewUpgrade->name);
    syncLaunchConfig(state, catalog);
    return true;
}

namespace {

double standardRepairShipCost(const GameState& state)
{
    const int repaired = repairShipAmount(state);
    if (repaired <= 0) {
        return 0.0;
    }

    const double baseCost = tuning::hangar::repairBaseCost + static_cast<double>(repaired) * tuning::hangar::repairCostPerDamage;
    return tuning::escalatedHangarOpCost(baseCost, state.run.repairOpsThisExpedition);
}

bool salvageRebuildAvailable(const GameState& state)
{
    return state.run.shipDamage >= tuning::damage::destroyedShipDamage && state.run.credits < standardRepairShipCost(state);
}

} // namespace

int repairShipAmount(const GameState& state)
{
    return std::min(tuning::hangar::repairAmountCap, state.run.shipDamage);
}

double repairShipCost(const GameState& state)
{
    const double standardCost = standardRepairShipCost(state);
    if (salvageRebuildAvailable(state)) {
        return std::max(0.0, state.run.credits);
    }
    return standardCost;
}

bool repairShip(GameState& state)
{
    if (state.run.shipDamage <= 0) {
        state.statusLine = std::string(text::status::shipAlreadyReady);
        return false;
    }

    const int repaired = repairShipAmount(state);
    const bool salvageRebuild = salvageRebuildAvailable(state);
    const double cost = repairShipCost(state);
    if (state.run.credits < cost) {
        state.statusLine = std::string(text::status::repairsUnaffordable);
        return false;
    }

    state.run.credits -= cost;
    state.run.shipDamage -= repaired;
    state.run.repairOpsThisExpedition += 1;
    state.statusLine = salvageRebuild ? text::salvagedHull(repaired) : text::repairedHull(repaired);
    return true;
}

double recruitCrewCost(const GameState& state)
{
    return activeAstronaut(state) == nullptr ? 0.0 : tuning::hangar::recruitCost;
}

HangarOperationPreview hangarOperationPreview(const GameState& state, const ContentCatalog& catalog)
{
    (void)catalog;
    HangarOperationPreview preview;
    const Astronaut* astronaut = activeAstronaut(state);

    preview.repairAmount = repairShipAmount(state);
    preview.repairCost = repairShipCost(state);
    preview.repairAvailable = preview.repairAmount > 0 && state.run.credits >= preview.repairCost;

    preview.emergencyRecruitment = astronaut == nullptr;
    preview.recruitCost = recruitCrewCost(state);
    preview.recruitAvailable = state.run.credits >= preview.recruitCost;

    return preview;
}

std::vector<const Astronaut*> recruitCandidateTemplates(const GameState& state, const ContentCatalog& catalog, int count)
{
    std::vector<const Astronaut*> candidates;
    if (catalog.astronauts.empty() || count <= 0) {
        return candidates;
    }

    for (const Astronaut& candidate : catalog.astronauts) {
        if (!state.meta.pendingReplacementArchetypeId.empty() &&
            candidate.archetypeId != state.meta.pendingReplacementArchetypeId) {
            continue;
        }
        candidates.push_back(&candidate);
        if (static_cast<int>(candidates.size()) >= count) {
            break;
        }
    }
    return candidates;
}

bool acceptCrewReplacement(GameState& state, const ContentCatalog& catalog)
{
    if (activeAstronaut(state) != nullptr ||
        (!state.meta.crewLossPending && state.meta.pendingReplacementArchetypeId.empty())) {
        state.statusLine = "No crew replacement is awaiting acceptance.";
        return false;
    }

    const std::vector<const Astronaut*> authored = recruitCandidateTemplates(state, catalog, 1);
    Astronaut replacement;
    if (!authored.empty()) {
        replacement = *authored.front();
    } else {
        replacement.id = "emergency_specialist";
        replacement.name = "Emergency Specialist";
        replacement.background = "A trained reserve answers the open rescue berth.";
        replacement.trait = std::string(tuning::traits::hardReboot);
        replacement.archetypeId = state.meta.pendingReplacementArchetypeId.empty()
            ? "beaver_engineer"
            : state.meta.pendingReplacementArchetypeId;
    }

    state.meta.replacementSequence += 1;
    replacement.id += "_replacement_" + std::to_string(state.meta.replacementSequence);
    replacement.name += " " + std::to_string(state.meta.replacementSequence + 1);
    replacement.status = CrewStatus::Active;
    state.run.crew.push_back(replacement);
    state.meta.crewLossPending = false;
    state.meta.pendingReplacementArchetypeId.clear();
    syncLaunchConfig(state, catalog);
    state.statusLine = replacement.name + " accepted the open expedition berth.";
    return true;
}

bool arkDiscovered(const GameState& state)
{
    return state.meta.ark.condition != ArkCondition::NotFound ||
        state.meta.campaignMilestone != CampaignMilestone::SolarTutorial;
}

bool hostileSystemActive(const GameState& state)
{
    return state.meta.campaignMilestone == CampaignMilestone::HostileSystemStranded ||
        state.meta.campaignMilestone == CampaignMilestone::ArkRepairing ||
        state.meta.campaignMilestone == CampaignMilestone::GravityWellDisaster ||
        state.meta.ark.gravityWellDisaster ||
        state.meta.ark.condition == ArkCondition::DamagedStranded ||
        state.meta.ark.condition == ArkCondition::Repairing;
}

bool navigationAvailable(const GameState& state)
{
    return hostileSystemActive(state);
}

GameChapter chapterForState(const GameState& state, const ContentCatalog& catalog)
{
    const auto destinationSuccesses = [&](const std::string& destinationId) {
        const int index = destinationIndexForId(catalog, destinationId);
        if (index < 0 || index >= static_cast<int>(state.meta.destinationSuccesses.size())) {
            return 0;
        }
        return state.meta.destinationSuccesses[static_cast<std::size_t>(index)];
    };

    if (state.meta.campaignMilestone == CampaignMilestone::ArkRepairing ||
        state.meta.ark.condition == ArkCondition::Repairing) {
        return GameChapter::Ouroboros;
    }

    if (destinationSuccesses(content::destination::nearbyGalaxy) > 0) {
        return GameChapter::VoidCompass;
    }

    if (destinationSuccesses(content::destination::nearbyStar) > 0) {
        return GameChapter::LastCampfire;
    }

    if (hostileSystemActive(state)) {
        return GameChapter::Arkfall;
    }

    if (state.meta.ark.firstJumpComplete ||
        state.meta.campaignMilestone == CampaignMilestone::FirstArkJumpComplete ||
        state.meta.navigation.currentSystemId == "relay_system") {
        return GameChapter::Straylight;
    }

    const int outerIndex = destinationIndexForId(catalog, content::destination::jupiter);
    if (arkDiscovered(state) ||
        (outerIndex >= 0 && state.run.destinationIndex >= outerIndex) ||
        (outerIndex >= 0 && state.meta.furthestTier >= catalog.destinations[static_cast<std::size_t>(outerIndex)].tier)) {
        return GameChapter::Breakthrough;
    }

    const int marsIndex = destinationIndexForId(catalog, content::destination::mars);
    if (marsIndex >= 0 &&
        (state.run.destinationIndex >= marsIndex ||
            state.meta.furthestTier >= catalog.destinations[static_cast<std::size_t>(marsIndex)].tier)) {
        return GameChapter::RedFrontier;
    }

    const int moonIndex = destinationIndexForId(catalog, content::destination::moon);
    if (moonIndex >= 0 &&
        (state.run.destinationIndex >= moonIndex ||
            state.meta.furthestTier >= catalog.destinations[static_cast<std::size_t>(moonIndex)].tier)) {
        return GameChapter::LunarProgram;
    }

    return GameChapter::ProvingGround;
}

void syncChapterProgress(GameState& state, const ContentCatalog& catalog)
{
    const GameChapter derived = chapterForState(state, catalog);
    if (chapterNumber(derived) > chapterNumber(state.meta.chapter)) {
        state.meta.chapter = derived;
    }
}

void scheduleStoryBriefing(GameState& state, StoryBriefingId briefing, Screen continuation)
{
    state.storyBriefing.pending = briefing;
    state.storyBriefing.continuation = continuation;
}

bool acknowledgeStoryBriefing(GameState& state, const ContentCatalog& catalog)
{
    const StoryBriefingId briefing = state.storyBriefing.pending;
    if (briefing == StoryBriefingId::None) {
        return false;
    }

    const Screen continuation = state.storyBriefing.continuation;
    if (briefing == StoryBriefingId::CampaignIntroduction) {
        state.meta.campaignIntroductionAcknowledged = true;
        state.statusLine = "Mission brief acknowledged. Build a flight profile and open the route to the Moon.";
    } else if (briefing == StoryBriefingId::StraylightDiscovery) {
        state.meta.straylightDiscoveryAcknowledged = true;
        state.storyBriefing.pending = StoryBriefingId::StraylightApproach;
        state.statusLine = "Unknown contact locked. Begin the approach from Neptune.";
        syncLaunchConfig(state, catalog);
        return true;
    } else if (briefing == StoryBriefingId::StraylightApproach) {
        // This acknowledgement is executed by RocketGameApp because it starts
        // a realtime, save-safe ceremonial transfer rather than changing only
        // campaign data.
        return false;
    } else if (briefing == StoryBriefingId::ActOneComplete) {
        discoverArk(state, catalog);
        state.statusLine = "ACT I COMPLETE — Straylight is now the expedition's home among the stars.";
    }
    state.storyBriefing = {};
    state.screen = continuation;
    syncLaunchConfig(state, catalog);
    return true;
}

std::vector<const Destination*> navigationDestinations(const GameState& state, const ContentCatalog& catalog)
{
    std::vector<const Destination*> destinations;
    if (!hostileSystemActive(state)) {
        return destinations;
    }

    for (const Destination& destination : catalog.destinations) {
        if (destination.requiresHostileSystem) {
            destinations.push_back(&destination);
        }
    }
    return destinations;
}

void discoverArk(GameState& state, const ContentCatalog& catalog)
{
    if (arkDiscovered(state)) {
        return;
    }

    state.meta.campaignMilestone = CampaignMilestone::ArkDiscovered;
    state.meta.ark.condition = ArkCondition::DerelictOperable;
    state.meta.ark.fuelReserve = std::max(state.meta.ark.fuelReserve, 1);
    state.meta.navigation.currentSystemId = "solar_system";
    state.meta.navigation.arkLocationId = content::destination::neptune;
    state.meta.navigation.discoveredDestinationIds = {content::destination::neptune};
    state.statusLine = "Ark discovered beyond Neptune: derelict, under-equipped, but operable.";
    syncLaunchConfig(state, catalog);
}

bool performArkJump(GameState& state, const ContentCatalog& catalog)
{
    if (!arkDiscovered(state)) {
        state.statusLine = "The Ark has not been found yet.";
        return false;
    }

    if (!state.meta.ark.firstJumpComplete) {
        state.meta.ark.firstJumpComplete = true;
        state.meta.ark.condition = ArkCondition::DerelictOperable;
        state.meta.campaignMilestone = CampaignMilestone::FirstArkJumpComplete;
        state.meta.navigation.currentSystemId = "relay_system";
        state.meta.navigation.arkLocationId = "relay_void";
        state.statusLine = "First Ark jump complete. The vessel can move, but every jump spends the future.";
        syncChapterProgress(state, catalog);
        return true;
    }

    if (!state.meta.ark.gravityWellDisaster) {
        state.meta.ark.gravityWellDisaster = true;
        state.meta.ark.condition = ArkCondition::DamagedStranded;
        state.meta.ark.hullDamage = std::max(state.meta.ark.hullDamage, 72);
        state.meta.campaignMilestone = CampaignMilestone::HostileSystemStranded;
        state.meta.navigation.currentSystemId = "hostile_system";
        state.meta.navigation.arkLocationId = "gravity_well";
        state.meta.navigation.discoveredDestinationIds = {content::destination::nearbyStar, content::destination::nearbyGalaxy};
        state.meta.navigation.selectedDestinationId = content::destination::nearbyStar;
        state.meta.ark.fuelReserve = std::max(state.meta.ark.fuelReserve, tuning::ark::hostileSystemFuelReserve);
        addUniqueId(state.meta.unlockKeys, content::unlock::deepSpace);
        addUniqueId(state.meta.unlockKeys, content::unlock::droneBay);
        addUniqueId(state.meta.unlockKeys, content::unlock::perimeterDrones);
        state.meta.droneBaySlots = std::max(state.meta.droneBaySlots, 3);
        const std::array<std::string_view, 2> arkfallCombatDrones {
            content::drone::attackDrone,
            content::drone::defenseDrone
        };
        for (const std::string_view droneId : arkfallCombatDrones) {
            addUniqueId(state.meta.ownedDroneIds, std::string(droneId));
        }
        const int hostileIndex = destinationIndexForId(catalog, content::destination::nearbyStar);
        if (hostileIndex >= 0) {
            state.run.destinationIndex = hostileIndex;
            state.launchConfig.destinationId = content::destination::nearbyStar;
            state.launchConfig.burnGoalMultiplier = catalog.destinations[static_cast<std::size_t>(hostileIndex)].targetMultiplier;
        }
        state.screen = Screen::Navigation;
        state.statusLine = "Gravity well impact. The Ark is stranded; Mk I Attack and Defense drones are online, with at least 3 Drone Bay slots. Choose shuttle sorties from the local system.";
        syncLaunchConfig(state, catalog);
        return true;
    }

    state.statusLine = "The Ark cannot jump until alien artifacts and fuel systems are recovered.";
    return false;
}

bool selectNavigationDestination(GameState& state, const ContentCatalog& catalog, int index)
{
    const std::vector<const Destination*> destinations = navigationDestinations(state, catalog);
    if (index < 0 || index >= static_cast<int>(destinations.size())) {
        state.statusLine = "No navigable destination selected.";
        return false;
    }

    const Destination& destination = *destinations[static_cast<std::size_t>(index)];
    const int fuelCost = navigationFuelCost(destination);
    if (state.meta.ark.fuelReserve < fuelCost) {
        state.statusLine = "Ark fuel reserve is short for " + destination.name + ". Recover fuel or choose a closer sortie.";
        return false;
    }
    const int destinationIndex = destinationIndexForId(catalog, destination.id);
    if (destinationIndex < 0) {
        state.statusLine = "Navigation target is not in the catalog.";
        return false;
    }

    state.meta.navigation.selectedDestinationId = destination.id;
    state.meta.ark.fuelReserve = std::max(0, state.meta.ark.fuelReserve - fuelCost);
    addUniqueId(state.meta.navigation.discoveredDestinationIds, destination.id);
    state.run.destinationIndex = destinationIndex;
    state.launchConfig.frontierTransfer = true;
    state.launchConfig.destinationId = destination.id;
    state.launchConfig.burnGoalMultiplier = destination.targetMultiplier;
    syncLaunchConfig(state, catalog);
    state.screen = Screen::Hangar;
    state.statusLine = "Course plotted from the Ark to " + destination.name + ". Ark fuel spent: -" + std::to_string(fuelCost) + ". Prep the shuttle, then launch.";
    return true;
}

int frontierReadinessRequired(const GameState& state, const ContentCatalog& catalog)
{
    const Destination& destination = currentDestination(state, catalog);
    const Destination* next = nextDestination(state, catalog);
    if (next == nullptr) {
        return 0;
    }
    // The starter transfer retains its lighter readiness target by tier, not
    // by a narrative destination ID. Authored route requirements take
    // precedence in frontierGateStatusForDestination below.
    if (next->tier <= 1) {
        return tuning::launchProgression::moonRequiredUpgradeCount;
    }
    return tuning::mission::readinessBaseRequired + destination.tier;
}

int frontierReadinessCap(const GameState& state, const ContentCatalog& catalog)
{
    const int required = frontierReadinessRequired(state, catalog);
    if (required <= 0) {
        return 0;
    }
    return required + tuning::mission::readinessOverCap;
}

const Destination* nextDestination(const GameState& state, const ContentCatalog& catalog)
{
    const int nextIndex = state.run.destinationIndex + 1;
    if (nextIndex < 0 || nextIndex >= static_cast<int>(catalog.destinations.size())) {
        return nullptr;
    }
    const Destination& next = catalog.destinations[static_cast<std::size_t>(nextIndex)];
    return next.requiresHostileSystem && !hostileSystemActive(state) ? nullptr : &next;
}

FrontierGateStatus frontierGateStatusForDestination(
    const GameState& state,
    const ContentCatalog& catalog,
    std::string_view destinationId)
{
    FrontierGateStatus status;
    status.destinationId = std::string(destinationId);
    const Destination* destination = catalog.findDestination(destinationId);
    if (destination == nullptr) {
        return status;
    }

    const Destination& origin = currentDestination(state, catalog);
    if (destination->tier == 1 && origin.hiddenFromProgression) {
        status.kind = FrontierGateKind::FlightData;
        status.current = state.meta.launchLessons.stage == LaunchTrainingStage::MoonTransfer ? 1 : 0;
        status.required = 1;
        status.satisfied = state.meta.launchLessons.stage == LaunchTrainingStage::MoonTransfer &&
            status.current >= status.required;
        status.blockerText = "Complete the opening briefing to begin the Moon transfer.";
        return status;
    }

    // Authored route requirements describe whether the destination exists as
    // a player choice at all. Resolve them before hardware and generic flight
    // gates so a story-locked route cannot leak a clickable ship-readiness
    // details path. Once the authored requirement is satisfied, the normal
    // hardware checks below still decide whether the route is ready to fly.
    const ScenarioRouteRequirementStatus scenarioRequirement = scenarioRouteRequirementStatus(
        state,
        catalog,
        *destination);
    if (!scenarioRequirement.satisfied) {
        status.kind = FrontierGateKind::ScenarioRequirement;
        status.scenarioId = scenarioRequirement.scenarioId;
        status.scenarioStepId = scenarioRequirement.stepId;
        status.current = scenarioRequirement.current;
        status.required = scenarioRequirement.required;
        status.satisfied = false;
        const ScenarioInstance* instance = findScenarioInstance(state.meta, status.scenarioId);
        const std::string_view definitionId = instance == nullptr || instance->definitionId.empty()
            ? std::string_view(status.scenarioId)
            : std::string_view(instance->definitionId);
        const ScenarioDefinition* definition = findScenarioDefinition(catalog, definitionId);
        const ScenarioDefinition resolved = definition != nullptr && instance != nullptr
            ? resolveScenarioDefinition(*definition, *instance)
            : ScenarioDefinition {};
        const ScenarioStepDefinition* step = definition == nullptr
            ? nullptr
            : (instance == nullptr
                   ? findScenarioStepDefinition(*definition, status.scenarioStepId)
                   : findScenarioStepDefinition(resolved, status.scenarioStepId));
        status.blockerText = step == nullptr
            ? "Complete the required route objective."
            : (step->actionLabel.empty() ? step->detail : step->actionLabel);
        return status;
    }

    if (destination->tier == 2 &&
        !launchTrainingAtLeast(state, LaunchTrainingStage::HullIntegrity)) {
        status.kind = FrontierGateKind::FlightData;
        status.current = launchUpgradeRank(state, LaunchUpgradeKind::FuelTanks) >= 2 ? 1 : 0;
        status.required = 1;
        status.satisfied =
            (state.meta.launchLessons.stage == LaunchTrainingStage::ThermalManagement ||
                state.meta.launchLessons.stage == LaunchTrainingStage::MarsTransfer) &&
            status.current >= status.required;
        status.blockerText = "Mars requires 20 transfer fuel; current capacity is " +
            std::to_string(static_cast<int>(launchFuelCapacity(state))) + ".";
        if (!status.satisfied) {
            return status;
        }
    }
    if (destination->tier == 3 &&
        !launchTrainingAtLeast(state, LaunchTrainingStage::Complete)) {
        status.kind = FrontierGateKind::FlightData;
        status.current = destinationTransferMarginReady(state, catalog, *destination) ? 1 : 0;
        status.required = 1;
        status.satisfied =
            (state.meta.launchLessons.stage == LaunchTrainingStage::HullIntegrity ||
                state.meta.launchLessons.stage == LaunchTrainingStage::JupiterTransfer) &&
            status.current >= status.required;
        status.blockerText = destination->transferMarginBlockerText.empty()
            ? "Create " + std::to_string(static_cast<int>(destination->calibratedTransferMarginRequired)) +
                " fuel of " + destination->name + " transfer margin with permanent tank capacity, a matching transfer assist, or both."
            : destination->transferMarginBlockerText;
        if (!status.satisfied) {
            return status;
        }
    }

    // A destination with authored route keys is governed entirely by those
    // keys. Once every key is present, do not silently add a second Flight
    // Data grind after the scenario reward has already opened the route.
    // Destinations without route keys continue to use the generic Flight Data
    // requirement below.
    if (!destination->routeRequirementKeys.empty()) {
        status.kind = FrontierGateKind::ScenarioRequirement;
        status.current = static_cast<int>(destination->routeRequirementKeys.size());
        status.required = status.current;
        status.satisfied = true;
        return status;
    }

    status.kind = FrontierGateKind::FlightData;
    status.current = std::max(0, state.run.frontierReadiness);
    status.required = frontierReadinessRequired(state, catalog);
    status.satisfied = status.required > 0 && status.current >= status.required;
    return status;
}

FrontierGateStatus frontierGateStatus(const GameState& state, const ContentCatalog& catalog)
{
    const Destination* next = nextDestination(state, catalog);
    if (next == nullptr) {
        return {};
    }
    return frontierGateStatusForDestination(state, catalog, next->id);
}

bool canCommitToNextFrontier(const GameState& state, const ContentCatalog& catalog)
{
    const FrontierGateStatus gate = frontierGateStatus(state, catalog);
    return !gate.destinationId.empty() && gate.satisfied;
}

bool bankFrontierReadiness(GameState& state, const ContentCatalog& catalog)
{
    if (frontierReadinessRequired(state, catalog) <= 0) {
        return false;
    }

    const int before = state.run.frontierReadiness;
    state.run.frontierReadiness = std::min(frontierReadinessCap(state, catalog), state.run.frontierReadiness + 1);
    return state.run.frontierReadiness > before;
}

double missionPressureModifier(const GameState& state, const ContentCatalog& catalog, const Destination& destination)
{
    const int index = destinationIndexForId(catalog, destination.id);
    if (index < 0) {
        return tuning::mission::unknownDestinationDifficulty;
    }

    const auto attemptsIndex = static_cast<std::size_t>(index);
    const int attempts = attemptsIndex < state.meta.destinationAttempts.size() ? state.meta.destinationAttempts[attemptsIndex] : 0;
    const int successes = attemptsIndex < state.meta.destinationSuccesses.size() ? state.meta.destinationSuccesses[attemptsIndex] : 0;

    if (attempts <= 0 || successes <= 0) {
        return tuning::mission::unattemptedDifficulty;
    }
    return std::max(tuning::mission::provenDifficultyFloor, tuning::mission::provenDifficultyBase / static_cast<double>(successes + 1));
}

bool commitToNextFrontier(GameState& state, const ContentCatalog& catalog)
{
    const Destination* next = nextDestination(state, catalog);
    if (next == nullptr) {
        state.statusLine = std::string(text::status::noFartherFrontier);
        return false;
    }

    const FrontierGateStatus gate = frontierGateStatusForDestination(state, catalog, next->id);
    if (!gate.satisfied) {
        if (gate.kind == FrontierGateKind::FlightData) {
            state.statusLine = text::moreFlightDataNeeded(next->name);
        } else if (!gate.blockerText.empty()) {
            state.statusLine = gate.blockerText;
        } else {
            state.statusLine = std::string(text::status::noFartherFrontier);
        }
        return false;
    }

    state.run.destinationIndex += 1;
    state.run.frontierReadiness = 0;
    state.meta.furthestTier = std::max(state.meta.furthestTier, next->tier);
    state.launchConfig.frontierTransfer = false;
    state.launchConfig.destinationId = next->id;
    state.launchConfig.burnGoalMultiplier = defaultProvingTarget(*next);
    syncLaunchConfig(state, catalog);
    state.statusLine = text::transferAchieved(next->name);
    return true;
}

void unlockFromBlueprints(GameState& state)
{
    for (const auto& threshold : tuning::unlocks::blueprintUnlocks) {
        if (state.meta.blueprintProgress >= threshold.threshold && !hasUnlock(state.meta, threshold.key)) {
            state.meta.unlockKeys.push_back(std::string(threshold.key));
            state.statusLine = std::string(threshold.message);
        }
    }
}

void updateLegacyRecords(MetaProgress& meta, const LaunchOutcome& outcome)
{
    const double creditDelta = outcome.payout - outcome.recoveryCost;
    meta.maxBurnDepth = std::max(meta.maxBurnDepth, outcome.ejectMultiplier);
    meta.maxPeakWarning = std::max(meta.maxPeakWarning, outcome.peakWarning);
    meta.maxPeakAbortRisk = std::max(meta.maxPeakAbortRisk, outcome.peakAbortRisk);
    meta.bestCreditDelta = std::max(meta.bestCreditDelta, creditDelta);
    meta.worstCreditDelta = std::min(meta.worstCreditDelta, creditDelta);

    const double survivalMargin = outcome.pilotedFlight
        ? outcome.minimumSafetyMargin
        : outcome.crashMultiplier - outcome.ejectMultiplier;
    if (outcome.type != LaunchResultType::Destroyed && survivalMargin > 0.0
        && (meta.closestSurvivalMargin <= 0.0 || survivalMargin < meta.closestSurvivalMargin)) {
        meta.closestSurvivalMargin = survivalMargin;
        meta.closestSurvivalBurn = outcome.ejectMultiplier;
        meta.closestSurvivalFailurePoint = outcome.pilotedFlight ? 1.0 : outcome.crashMultiplier;
    }
}

bool isSkinOfYourTeethOutcome(const LaunchOutcome& outcome)
{
    const double survivalMargin = outcome.pilotedFlight
        ? outcome.minimumSafetyMargin
        : outcome.crashMultiplier - outcome.ejectMultiplier;
    return outcome.type != LaunchResultType::Destroyed &&
        survivalMargin > 0.0 &&
        survivalMargin <= tuning::records::closeCallSurvivalMargin;
}

void applyLaunchOutcome(GameState& state, const ContentCatalog& catalog, const LaunchOutcome& rawOutcome)
{
    const int readinessBefore = state.run.frontierReadiness;
    const LaunchTrainingStage trainingStage = state.meta.launchLessons.stage;
    const LaunchMissionKind missionKind = state.launchConfig.missionKind;
    const bool ceremonialTransfer = missionKind == LaunchMissionKind::StraylightApproach;
    const double lessonTargetMultiplier = state.launchConfig.burnGoalMultiplier;
    LaunchOutcome outcome = rawOutcome;
    const bool lessonMissionActive = launchLessonMissionActive(trainingStage, missionKind);
    const bool lessonReturnSucceeded = launchLessonReturnSucceeded(
        trainingStage,
        missionKind,
        outcome,
        lessonTargetMultiplier);
    const bool lessonArrivalSucceeded = launchLessonArrivalSucceeded(
        trainingStage,
        missionKind,
        outcome);
    const bool curriculumTransferAttempt =
        launchCurriculumTransferStage(trainingStage) &&
        missionKind == LaunchMissionKind::Standard &&
        outcome.frontierTransfer;
    const bool curriculumTransferSucceeded = curriculumTransferAttempt &&
        outcome.type == LaunchResultType::MissionComplete &&
        outcome.failureCause == LaunchFailureCause::None &&
        outcome.recoveryMethod == RecoveryMethod::TransferArrival;
    const bool failedCurriculumTransfer =
        curriculumTransferAttempt && !curriculumTransferSucceeded;
    const bool recoveryTransit = routeTransitIsRecovery(outcome.routeTransit);
    if (failedCurriculumTransfer) {
        // These are explicit arrival missions, not the legacy proving loop.
        // Returning early is safe, but it must not manufacture progress or an
        // empty refit entitlement from partial route distance.
        outcome.blueprintGain = 0;
    }
    if (lessonReturnSucceeded || lessonArrivalSucceeded || curriculumTransferSucceeded) {
        const double fuelSurveyBonus =
            outcome.fuelSurveyReturnTiming == FuelSurveyReturnTiming::Timely
            ? tuning::launchProgression::fuelSurveySafetyBonus
            : 0.0;
        outcome.payout = outcome.recoveryCost +
            tuning::launchProgression::lessonReward + fuelSurveyBonus;
    } else if (!lessonMissionActive && isSkinOfYourTeethOutcome(outcome)) {
        outcome.payout *= 1.0 + tuning::records::skinOfYourTeethCreditBonus;
    }

    ensureDestinationHistory(state, catalog);
    state.lastOutcome = outcome;
    updateLegacyRecords(state.meta, outcome);
    state.launchConfig.frontierTransfer = false;
    state.run.launchesThisExpedition += 1;
    state.run.shipDamage = std::clamp(state.run.shipDamage + outcome.shipDamage, 0, tuning::damage::destroyedShipDamage);
    state.meta.blueprintProgress += outcome.blueprintGain;
    unlockFromBlueprints(state);
    const int outcomeDestinationIndex = destinationIndexForId(catalog, outcome.destinationId);
    const Destination* outcomeDestination = catalog.findDestination(outcome.destinationId);
    const bool solarFrontierAdvance = outcomeDestination != nullptr
        && outcomeDestination->tier == state.run.destinationIndex + 1;
    const bool selectedHostileSortie = hostileSystemActive(state)
        && outcomeDestinationIndex == state.run.destinationIndex;
    const bool solarFrontierRevisit = !hostileSystemActive(state)
        && outcomeDestinationIndex == state.run.destinationIndex;
    const bool validDestinationSuccess = !ceremonialTransfer && !recoveryTransit &&
        outcome.type == LaunchResultType::MissionComplete
        && outcomeDestination != nullptr
        && (!lessonMissionActive || lessonArrivalSucceeded)
        && (!outcome.frontierTransfer || solarFrontierAdvance || solarFrontierRevisit || selectedHostileSortie);
    if (outcomeDestinationIndex >= 0) {
        const auto index = static_cast<std::size_t>(outcomeDestinationIndex);
        state.meta.destinationAttempts[index] += 1;
        if (validDestinationSuccess) {
            state.meta.destinationSuccesses[index] += 1;
        }
    }
    const bool shallowRecovery = outcomeDestination != nullptr && isShallowRecoveryOutcome(*outcomeDestination, outcome);
    const bool cleanShallowRecovery = outcomeDestination != nullptr && isCleanShallowRecoveryOutcome(*outcomeDestination, outcome);
    const bool cleanShallowRecoveryDestroyed = outcome.type == LaunchResultType::Destroyed
        && cleanShallowRecovery
        && state.run.cleanShallowRecoveryStreak + 1 >= tuning::rewards::cleanShallowRecoveryDestructionStreak;

    if (outcome.type != LaunchResultType::Destroyed && shallowRecovery) {
        state.run.shallowRecoveryStreak += 1;
        state.run.cleanShallowRecoveryStreak = cleanShallowRecovery ? state.run.cleanShallowRecoveryStreak + 1 : 0;
    } else {
        state.run.shallowRecoveryStreak = 0;
        state.run.cleanShallowRecoveryStreak = 0;
    }

    Astronaut* astronaut = activeAstronaut(state);
    if (astronaut != nullptr) {
        if (outcome.crewKilled) {
            state.meta.pendingReplacementArchetypeId = astronaut->archetypeId;
            state.meta.crewLossPending = true;
            astronaut->status = CrewStatus::Dead;
            state.meta.astronautsLost += 1;
            state.meta.memorials.push_back(astronaut->name + " lost during " + outcome.destinationId);
        } else if (outcome.crewInjured) {
            astronaut->status = CrewStatus::Injured;
        }
    }

    if (!outcome.moduleDestroyedId.empty() && outcome.type != LaunchResultType::Destroyed) {
        state.run.equippedModuleIds.erase(std::remove(state.run.equippedModuleIds.begin(), state.run.equippedModuleIds.end(), outcome.moduleDestroyedId), state.run.equippedModuleIds.end());
    }

    if (outcome.type == LaunchResultType::Destroyed) {
        state.meta.shipsLost += 1;
        state.run.credits = std::max(expeditionCreditFloor(state), state.run.credits - tuning::mission::destroyedCreditPenalty);
        state.run.planetaryExpedition = {};
        state.run.surfaceScan = {};
        state.run.surfacePush = {};
        state.run.mining = {};
        state.run.active = false;
        if (cleanShallowRecoveryDestroyed) {
            state.statusLine = std::string(text::status::cleanShallowRecoveryDestroyed);
        } else if (outcome.recoveryMethod == RecoveryMethod::ReturnHome) {
            state.statusLine = std::string(text::status::returnVehicleLost);
        } else if (outcome.frontierTransfer) {
            state.statusLine = std::string(text::status::transferVehicleLost);
        } else {
            state.statusLine = std::string(text::status::vehicleLost);
        }
    } else {
        state.run.credits = std::max(0.0, state.run.credits + outcome.payout - outcome.recoveryCost);
        const Destination* destination = outcomeDestination;
        if (destination != nullptr && !lessonMissionActive) {
            if (!outcome.frontierTransfer || outcome.type == LaunchResultType::MissionComplete) {
                state.meta.furthestTier = std::max(state.meta.furthestTier, destination->tier);
            }
            std::ostringstream famous;
            famous << destination->name << " at x" << outcome.ejectMultiplier;
            state.meta.famousLaunches.push_back(famous.str());
        }

        if (recoveryTransit) {
            if (outcome.type == LaunchResultType::MissionComplete &&
                outcome.recoveryMethod == RecoveryMethod::TransferArrival) {
                state.run.routeTransit = makeRouteTransit(
                    catalog,
                    outcome.destinationId,
                    outcome.routeTransit.originDestinationId,
                    RouteTransitIntent::Reapproach);
                const Destination* staging = catalog.findDestination(outcome.destinationId);
                const Destination* retry = catalog.findDestination(outcome.routeTransit.originDestinationId);
                const std::string retryLabel = retry == nullptr
                    ? "a reapproach"
                    : "reapproach " + retry->name;
                state.statusLine = "Recovery complete: " +
                    std::string(staging == nullptr ? "staging" : staging->name) +
                    " is ready for " +
                    retryLabel + ".";
            } else {
                // Turning around remains a valid choice during a recovery
                // flight. The original recovery route stays queued at the
                // passed body so the player may retry without a hidden jump.
                state.run.routeTransit = outcome.routeTransit;
                state.statusLine = outcome.recoveryMethod == RecoveryMethod::ReturnHome
                    ? "Recovery turn-around complete. The ship is back at the passed destination."
                    : "Recovery flight incomplete. The passed destination remains the active staging point.";
            }
        } else if (lessonMissionActive) {
            if (lessonReturnSucceeded) {
                advanceLaunchLessonAfterReturn(state, missionKind);
                state.statusLine = "Calibration telemetry validated. A direct launch upgrade is ready.";
            } else if (lessonArrivalSucceeded) {
                if (destination != nullptr && solarFrontierAdvance) {
                    state.run.destinationIndex += 1;
                    state.run.frontierReadiness = 0;
                    state.meta.furthestTier = std::max(state.meta.furthestTier, destination->tier);
                    state.launchConfig.destinationId = destination->id;
                    state.statusLine = text::transferAchievedNewRoute(destination->name);
                }
                if (trainingStage == LaunchTrainingStage::ThermalManagement) {
                    state.meta.launchLessons.stage = LaunchTrainingStage::HullIntegrity;
                } else if (trainingStage == LaunchTrainingStage::HullIntegrity) {
                    state.meta.launchLessons.stage = LaunchTrainingStage::Complete;
                }
            } else {
                state.statusLine = launchStageUsesArrival(trainingStage)
                    ? "Arrival incomplete. Reach the destination with enough fuel to land."
                    : "Calibration incomplete. Return after reaching the marked data point.";
            }
        } else if (outcome.frontierTransfer) {
            if (outcome.type == LaunchResultType::MissionComplete) {
                if (destination != nullptr && solarFrontierAdvance) {
                    state.run.destinationIndex += 1;
                    state.run.frontierReadiness = 0;
                    state.meta.furthestTier = std::max(state.meta.furthestTier, destination->tier);
                    state.launchConfig.destinationId = destination->id;
                    state.launchConfig.burnGoalMultiplier = defaultProvingTarget(*destination);
                    state.statusLine = text::transferAchievedNewRoute(destination->name);
                } else if (destination != nullptr && selectedHostileSortie) {
                    state.statusLine = text::transferAchievedNewRoute(destination->name);
                } else if (destination != nullptr && solarFrontierRevisit) {
                    state.statusLine = "Arrived at " + destination->name + ". Surface operations are ready.";
                } else {
                    state.statusLine = std::string(text::status::transferLedgerRejected);
                }
            } else if (failedCurriculumTransfer) {
                state.statusLine = "Arrival incomplete. Reach the destination with enough fuel to land.";
            } else {
                if (destination != nullptr && outcome.ejectMultiplier >= destination->targetMultiplier * tuning::outcomes::transferUsefulDataTargetShare) {
                    state.run.frontierReadiness = std::min(frontierReadinessCap(state, catalog), state.run.frontierReadiness + 1);
                    state.statusLine = std::string(text::status::transferAbortedReturn);
                } else {
                    state.statusLine = std::string(text::status::transferReturnEarly);
                }
            }
        } else if (outcome.type == LaunchResultType::MissionComplete) {
            state.run.frontierReadiness = std::min(frontierReadinessCap(state, catalog), state.run.frontierReadiness + 1);
            if (canCommitToNextFrontier(state, catalog)) {
                const Destination* next = nextDestination(state, catalog);
                const int required = frontierReadinessRequired(state, catalog);
                state.statusLine = state.run.frontierReadiness > required
                    ? std::string(text::status::extraProvingData)
                    : text::fullProfileReturned(next == nullptr ? std::string_view("the next route") : std::string_view(next->name));
            } else {
                state.statusLine = std::string(text::status::missionDataBanked);
            }
        } else {
            const Destination& current = currentDestination(state, catalog);
            const double usefulDataThreshold =
                current.targetMultiplier * tuning::outcomes::returnUsefulDataTargetShare;
            if (!shallowRecovery
                && outcome.ejectMultiplier >= usefulDataThreshold
                && frontierReadinessRequired(state, catalog) > 0) {
                state.run.frontierReadiness = std::min(frontierReadinessCap(state, catalog), state.run.frontierReadiness + 1);
                state.statusLine = std::string(text::status::earlyReturnUseful);
            } else {
                state.statusLine = std::string(text::status::earlyReturnShallow);
            }
        }
    }

    if (!recoveryTransit && outcome.type == LaunchResultType::MissionComplete &&
        outcome.recoveryMethod == RecoveryMethod::TransferArrival && outcome.routeTransit.active()) {
        state.run.routeTransit = {};
    }

    state.run.refitEntitled = state.run.refitEntitled ||
        lessonReturnSucceeded ||
        lessonArrivalSucceeded ||
        (!failedCurriculumTransfer && state.run.frontierReadiness > readinessBefore) ||
        validDestinationSuccess;

    if (validDestinationSuccess && outcome.frontierTransfer && outcomeDestination != nullptr) {
        if (outcomeDestination->id == content::destination::moon &&
            state.meta.launchLessons.stage == LaunchTrainingStage::MoonTransfer) {
            state.meta.launchLessons.stage = LaunchTrainingStage::ThermalManagement;
        } else if (outcomeDestination->id == content::destination::mars &&
            state.meta.launchLessons.stage == LaunchTrainingStage::MarsTransfer) {
            state.meta.launchLessons.stage = LaunchTrainingStage::HullIntegrity;
        } else if (outcomeDestination->id == content::destination::jupiter &&
            state.meta.launchLessons.stage == LaunchTrainingStage::JupiterTransfer) {
            state.meta.launchLessons.stage = LaunchTrainingStage::Complete;
        }
    }
    if (state.run.frontierReadiness > readinessBefore) {
        const Destination& origin = currentDestination(state, catalog);
        const Destination* target = nextDestination(state, catalog);
        recordScenarioEvent(
            state,
            catalog,
            {ScenarioEventKind::FlightDataBanked, {}, {}, origin.id,
             target == nullptr ? std::string {} : target->id,
              state.run.frontierReadiness - readinessBefore, 0});
    }
    if (validDestinationSuccess && outcome.frontierTransfer && outcomeDestination != nullptr) {
        (void)recordScenarioEvent(
            state,
            catalog,
            {ScenarioEventKind::DestinationReached, {}, {}, {}, outcomeDestination->id, 1, 0});
    }
    syncLaunchTrainingProgress(state, catalog);

    syncLaunchConfig(state, catalog);
}

ModuleStats aggregateShipStats(const GameState& state, const ContentCatalog& catalog)
{
    ModuleStats stats;
    if (const ShipFrame* frame = catalog.findFrame(state.run.frameId)) {
        stats += frame->baseStats;
    }

    for (const std::string& moduleId : state.run.equippedModuleIds) {
        if (const ShipModule* module = catalog.findModule(moduleId)) {
            stats += module->stats;
        }
    }

    const double damagePenalty = static_cast<double>(state.run.shipDamage) / static_cast<double>(tuning::damage::destroyedShipDamage);
    stats.hull -= damagePenalty * tuning::damage::hullPenaltyPerDamage;
    stats.cooling -= damagePenalty * tuning::damage::coolingPenaltyPerDamage;
    stats.escape -= damagePenalty * tuning::damage::escapePenaltyPerDamage;

    return stats;
}

CrewUpgradeStats aggregateCrewUpgradeStats(const GameState& state, const ContentCatalog& catalog)
{
    CrewUpgradeStats stats;
    for (const std::string& upgradeId : state.run.crewUpgradeIds) {
        if (const CrewUpgrade* upgrade = catalog.findCrewUpgrade(upgradeId)) {
            stats += upgrade->stats;
        }
    }
    return stats;
}

Astronaut* activeAstronaut(GameState& state)
{
    auto found = std::find_if(state.run.crew.begin(), state.run.crew.end(), [](const Astronaut& astronaut) {
        return astronaut.status != CrewStatus::Dead;
    });
    return found == state.run.crew.end() ? nullptr : &*found;
}

const Astronaut* activeAstronaut(const GameState& state)
{
    auto found = std::find_if(state.run.crew.begin(), state.run.crew.end(), [](const Astronaut& astronaut) {
        return astronaut.status != CrewStatus::Dead;
    });
    return found == state.run.crew.end() ? nullptr : &*found;
}

const Destination& currentDestination(const GameState& state, const ContentCatalog& catalog)
{
    const int index = std::clamp(state.run.destinationIndex, 0, static_cast<int>(catalog.destinations.size()) - 1);
    return catalog.destinations[static_cast<std::size_t>(index)];
}

} // namespace rocket
