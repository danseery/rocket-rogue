#include "game/GamePanel.h"
#include "core/CrewPresentation.h"
#include "core/FlightInstrumentPresentation.h"
#include "core/GameFormat.h"
#include "core/GameText.h"
#include "core/HangarPresentation.h"
#include "core/InventoryPresentation.h"
#include "core/LaunchPresentation.h"
#include "core/LaunchReadinessPresentation.h"
#include "core/MiningPresentation.h"
#include "core/OutcomePresentation.h"
#include "core/PanelChromePresentation.h"
#include "core/ProgramPresentation.h"
#include "core/RefitPresentation.h"
#include "core/ResearchPresentation.h"
#include "core/ResearchSystem.h"
#include "core/ScenarioSystem.h"
#include "core/ShipPresentation.h"
#include "core/SurfaceScanPresentation.h"
#include "core/Tuning.h"
#include "core/GameUi.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <sstream>
#include <utility>
#include <vector>

namespace rocket {

namespace {

FlightInstrumentPresentation flightInstrumentsForContext(const PanelRenderContext& context)
{
    if (context.state.screen == Screen::Launch && context.flightArmed && context.launchFlight != nullptr) {
        return launchFlightInstruments(context.flightModel, *context.launchFlight);
    }
    if (context.state.screen == Screen::Flyby) {
        return flybyFlightInstruments(context.state.run.flyby);
    }
    if (context.state.screen == Screen::Orbit) {
        return orbitFlightInstruments(context.state.run.orbit);
    }
    return {};
}

std::string htmlEscape(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

std::string flightStatusRow(std::string_view id, std::string_view label, std::string value)
{
    return "<div class=\"flight-status-row\"><span>" + htmlEscape(label) +
        "</span><strong id=\"" + std::string(id) + "\">" + htmlEscape(value) +
        "</strong></div>";
}

std::string metricClass(std::string_view label, std::string_view cssClass = {})
{
    std::string result = "metric";
    if (label == text::labels::chapter) {
        result += " metric-chapter";
    }
    if (!cssClass.empty()) {
        result += " ";
        result += cssClass;
    }
    return result;
}

std::string launchMetricSeverity(const PanelRenderContext& context, std::string_view label)
{
    if (context.launchFlight == nullptr) return {};
    const LaunchFlightState& flight = *context.launchFlight;
    double caution = tuning::launch::pilotingWarningThreshold;
    double critical = tuning::launch::pilotingCriticalThreshold;
    double value = 0.0;
    if (label == "Fuel") {
        if (flight.fuelFailureSeconds > 0.0) {
            return "critical";
        }
        const CalibrationFuelWarning calibrationWarning =
            calibrationFuelWarning(context.flightModel, flight);
        if (calibrationWarning != CalibrationFuelWarning::None) {
            const bool late = calibrationWarning == CalibrationFuelWarning::Critical;
            const int pulse = static_cast<int>(std::floor(
                flight.elapsedSeconds * (late ? 8.0 : 4.0))) % 4;
            if (late) {
                return "critical fuel-survey-alert fuel-survey-late fuel-survey-pulse-" +
                    std::to_string(pulse);
            }
            if (calibrationWarning == CalibrationFuelWarning::TurnAround) {
                return "caution fuel-survey-alert fuel-survey-action fuel-survey-pulse-" +
                    std::to_string(pulse);
            }
            if (calibrationWarning == CalibrationFuelWarning::Approaching) {
                return "caution fuel-survey-alert fuel-survey-prepare fuel-survey-pulse-" +
                    std::to_string(pulse);
            }
        }
        value = 1.0 - flight.fuelRemaining / std::max(0.01, flight.fuelCapacity);
        caution = 0.75;
        critical = 0.90;
    } else if (label == "Course") {
        value = std::abs(flight.courseOffset);
        caution = tuning::launch::pilotingCourseSafe;
        critical = tuning::launch::pilotingCourseCaution;
    } else if (label == "Temperature") {
        value = flight.heat;
    } else if (label == "Hull") {
        value = 1.0 - flight.hullRemaining / std::max(1.0, flight.hullMaximum);
        caution = 0.50;
        critical = 0.75;
    } else {
        return {};
    }
    return value >= critical ? "critical" : (value >= caution ? "caution" : std::string {});
}

std::string launchStatusSeverity(const PanelRenderContext& context)
{
    if (context.launchFlight == nullptr) {
        return "status telemetry-status";
    }
    const LaunchFlightState& flight = *context.launchFlight;
    const CalibrationFuelWarning calibrationWarning =
        calibrationFuelWarning(context.flightModel, flight);
    if (calibrationWarning == CalibrationFuelWarning::None) {
        return "status telemetry-status";
    }
    const bool late = calibrationWarning == CalibrationFuelWarning::Critical;
    const int pulse = static_cast<int>(std::floor(
        flight.elapsedSeconds * (late ? 8.0 : 4.0))) % 4;
    return std::string("status telemetry-status fuel-survey-status ") +
        (late ? "critical fuel-survey-late " : "caution ") +
        "fuel-survey-pulse-" + std::to_string(pulse);
}

void appendHudText(RealtimeHudState& state, std::string_view id, std::string value)
{
    state.patches.push_back({std::string(id), std::move(value), {}, true, false});
}

void appendHudClass(RealtimeHudState& state, std::string_view id, std::string value)
{
    state.patches.push_back({std::string(id), {}, std::move(value), false, true});
}

std::string metric(std::string_view label, std::string value, std::string_view cssClass = {})
{
    return "<div class=\"" + metricClass(label, cssClass) + "\"><strong>" + htmlEscape(value) +
        "</strong><span>" + htmlEscape(label) + "</span></div>";
}

std::string realtimeMetric(
    std::string_view id,
    std::string_view label,
    std::string value,
    std::string_view cssClass = {})
{
    return "<div id=\"" + std::string(id) + "\" class=\"" + metricClass(label, cssClass) +
        "\"><strong id=\"" + std::string(id) + "-value\">" + htmlEscape(value) +
        "</strong><span>" + htmlEscape(label) + "</span></div>";
}

std::string expeditionControlsMarkup()
{
    return R"(<section class="opening-controls" aria-label="Expedition controls">
<span class="opening-controls-kicker">CONTROLS // FIELD REFERENCE</span>
<div class="opening-control-cards">
<article class="opening-control-card opening-keyboard-controls">
<span class="opening-control-title">Keyboard + mouse</span>
<div class="opening-control-row"><strong>Menus</strong><p>Mouse or WASD / Arrows navigate. Enter / Space selects. Esc goes back or pauses.</p></div>
<div class="opening-control-row"><strong>Launch</strong><p>Space launches. WASD / Arrows steer and change throttle. R turns around. C turns engines off or on.</p></div>
<div class="opening-control-row"><strong>Flight</strong><p>WASD / Arrows steer approaches. Esc aborts. During Scan or Push, Space acts and B or Esc safely banks progress.</p></div>
<div class="opening-control-row"><strong>Mining rig</strong><p>WASD / Arrows move. Space or left click drills. E scans. T tethers. F exits the rig. R banks payload at the ship.</p></div>
<div class="opening-control-row"><strong>Jetpack EVA</strong><p>WASD / Arrows thrust. Mouse aims. Left click fires. Right click drills. E scans. T tethers. F enters the rig.</p></div>
</article>
<article class="opening-control-card opening-controller-controls">
<span class="opening-control-title">Controller</span>
<div class="opening-control-row"><strong>Menus</strong><p>L-stick / D-pad navigate. <strong data-controller-south>{{controller_south}}</strong> selects. <strong data-controller-east>{{controller_east}}</strong> goes back. R-stick scrolls.</p></div>
<div class="opening-control-row"><strong>Shortcuts</strong><p><strong data-controller-menu>{{controller_menu}}</strong> pauses. <strong data-controller-view>{{controller_view}}</strong> opens Map. <strong data-controller-north>{{controller_north}}</strong> opens Inventory.</p></div>
<div class="opening-control-row"><strong>Launch</strong><p>L-stick steers and changes throttle. <strong data-controller-south>{{controller_south}}</strong> launches or turns around. <strong data-controller-west>{{controller_west}}</strong> turns engines off or on.</p></div>
<div class="opening-control-row"><strong>Flight / Mining rig</strong><p>L-stick steers or moves. Hold <strong data-controller-east>{{controller_east}}</strong> to abort. <strong data-controller-rt>{{controller_rt}}</strong> drills. <strong data-controller-west>{{controller_west}}</strong> scans. <strong data-controller-north>{{controller_north}}</strong> tethers. Tap <strong data-controller-south>{{controller_south}}</strong> to stow cargo or leave; hold it to exit the rig.</p></div>
<div class="opening-control-row"><strong>Jetpack EVA</strong><p>L-stick thrusts. R-stick aims. <strong data-controller-rt>{{controller_rt}}</strong> fires. <strong data-controller-lt>{{controller_lt}}</strong> drills. <strong data-controller-west>{{controller_west}}</strong> scans. <strong data-controller-north>{{controller_north}}</strong> tethers. Hold <strong data-controller-south>{{controller_south}}</strong> to enter the rig.</p></div>
</article>
</div>
</section>)";
}

std::string compactMetric(std::string_view label, std::string value)
{
    return "<div class=\"surface-kpi\"><span>" + htmlEscape(label) + "</span><strong>" + htmlEscape(value) + "</strong></div>";
}

std::string surfaceQuickMetric(std::string_view label, std::string value, std::string_view cssClass = "", bool isLast = false)
{
    const std::string classAttr = cssClass.empty() ? "" : " " + htmlEscape(cssClass);
    return "<div class=\"surface-kpi surface-quick-item" + classAttr + (isLast ? " is-last" : "") + "\"><span>" + htmlEscape(label) +
        "</span><strong>" + htmlEscape(value) + "</strong></div>";
}

std::string phaseTitle(Screen screen)
{
    switch (screen) {
    case Screen::Launch:
        return "Flight";
    case Screen::Results:
        return "Debrief";
    case Screen::ArrivalFanfare:
        return "Arrival";
    case Screen::ArrivalOps:
        return "Approach";
    case Screen::Flyby:
        return "Flyby";
    case Screen::Orbit:
        return "Orbit";
    case Screen::Research:
        return "Research (Debug)";
    case Screen::SurfaceExpedition:
        return "Surface Ops";
    case Screen::SurfaceUpgrade:
        return "Level Up";
    case Screen::SurfaceScan:
        return "Planet Scan";
    case Screen::SurfacePush:
        return std::string(text::buttons::pushDeeper);
    case Screen::Mining:
        return "Mining";
    case Screen::DroneOps:
        return "Drone Ops";
    case Screen::Navigation:
        return "Navigation";
    case Screen::Upgrade:
        return "Refit";
    case Screen::Hangar:
    default:
        return "Hangar";
    }
}

std::string button(std::string_view label, std::string_view action, std::string cssClass = "", bool defaultFocus = false)
{
    const std::string classAttr = " class=\"" + std::string(cssClass) + (cssClass.empty() ? "" : " ") + "rr-text-button\"";
    const std::string defaultAttr = defaultFocus ? " data-ui-default-focus=\"1\"" : "";
    return "<button" + classAttr + " data-rr-action=\"" + htmlEscape(action) + "\" data-ui-focus-id=\"action:" +
        htmlEscape(action) + "\"" + defaultAttr + "><span class=\"rr-button-label\">" + htmlEscape(label) + "</span></button>";
}

std::string scenarioActionButton(
    const ScenarioObjectivePresentation& objective,
    std::string_view cssClass = "",
    bool defaultFocus = false)
{
    if (!objective.available || objective.action == ScenarioActionKind::None || objective.actionLabel.empty()) {
        return {};
    }

    const std::string action = ui::actions::scenarioAction(
        objective.scenarioId,
        objective.stepId,
        static_cast<int>(objective.action));
    const std::string classAttr = " class=\"" + std::string(cssClass) + (cssClass.empty() ? "" : " ") + "rr-text-button\"";
    const std::string defaultAttr = defaultFocus ? " data-ui-default-focus=\"1\"" : "";
    return "<button" + classAttr + " data-rr-action=\"" + htmlEscape(action) +
        "\" data-scenario-id=\"" + htmlEscape(objective.scenarioId) +
        "\" data-scenario-step-id=\"" + htmlEscape(objective.stepId) +
        "\" data-scenario-action=\"" + std::to_string(static_cast<int>(objective.action)) +
        "\" data-ui-focus-id=\"scenario:" + htmlEscape(objective.scenarioId) + ":" +
        htmlEscape(objective.stepId) + ":" + std::to_string(static_cast<int>(objective.action)) +
        "\"" + defaultAttr + "><span class=\"rr-button-label\">" + htmlEscape(objective.actionLabel) + "</span></button>";
}

std::string missionStamp(
    std::string_view kicker,
    std::string_view title,
    std::string_view detail,
    std::string_view tagOne,
    std::string_view tagTwo,
    std::string_view tagThree,
    std::string_view continueAction,
    std::string_view continueLabel = "Continue",
    const ScenarioObjectivePresentation* scenarioObjective = nullptr)
{
    std::ostringstream out;
    out << "<section class=\"arrival-fanfare-panel\">"
        << "<div class=\"arrival-stamp-content\">"
        << "<span class=\"arrival-stamp-kicker\">" << htmlEscape(kicker) << "</span>"
        << "<h2 class=\"arrival-stamp-title\">" << htmlEscape(title) << "</h2>"
        << "<strong class=\"arrival-stamp-destination\">" << htmlEscape(detail) << "</strong>"
        << "<div class=\"arrival-stamp-tags\"><span>" << htmlEscape(tagOne)
        << "</span><span>" << htmlEscape(tagTwo) << "</span>";
    if (!tagThree.empty()) {
        out << "<span class=\"gold\">" << htmlEscape(tagThree) << "</span>";
    }
    out << "</div></div>";
    if (scenarioObjective != nullptr) {
        ScenarioObjectivePresentation actionObjective = *scenarioObjective;
        actionObjective.actionLabel = std::string(continueLabel);
        out << scenarioActionButton(actionObjective, "arrival-stamp-continue", true);
    } else {
        out << button(continueLabel, continueAction, "arrival-stamp-continue", true);
    }
    out << "</section>";
    return out.str();
}

bool scenarioClaimQueuesRoute(
    const GameState& state,
    const ContentCatalog& catalog,
    const ScenarioObjectivePresentation& objective)
{
    if (!objective.available || objective.action != ScenarioActionKind::ClaimReward) {
        return false;
    }
    const ScenarioDefinition* definition = scenarioDefinitionForRuntimeId(
        state,
        catalog,
        objective.scenarioId);
    const ScenarioInstance* instance = findScenarioInstance(state.meta, objective.scenarioId);
    if (definition == nullptr || instance == nullptr) {
        return false;
    }
    const ScenarioDefinition resolved = resolveScenarioDefinition(*definition, *instance);
    const ScenarioStepDefinition* step = findScenarioStepDefinition(resolved, objective.stepId);
    if (step == nullptr || scenarioRouteRewardDestination(catalog, *step) == nullptr) {
        return false;
    }
    return step->completionEvent == ScenarioEventKind::FlybyFinished ||
        std::any_of(
            step->rewards.begin(),
            step->rewards.end(),
            [](const ScenarioReward& reward) {
                return reward.kind == ScenarioRewardKind::RouteAccess;
            });
}

std::string modalButton(
    std::string_view label,
    std::string_view modalId,
    std::string cssClass = "",
    bool defaultFocus = false)
{
    const std::string classAttr = " class=\"" + std::string(cssClass) + (cssClass.empty() ? "" : " ") + "rr-text-button\"";
    const std::string defaultAttr = defaultFocus ? " data-ui-default-focus=\"1\"" : "";
    return "<button type=\"button\"" + classAttr + " data-ui-modal=\"" + htmlEscape(modalId) +
        "\" data-ui-focus-id=\"modal:" + htmlEscape(modalId) + "\"" + defaultAttr + "><span class=\"rr-button-label\">" + htmlEscape(label) + "</span></button>";
}

std::string droneDetailsModalId(int index)
{
    return "drone_details_" + std::to_string(index);
}

thread_local std::vector<ModalPresentation>* activeModalCollector = nullptr;

class ScopedModalCollector {
public:
    explicit ScopedModalCollector(std::vector<ModalPresentation>& collector)
        : previous_(std::exchange(activeModalCollector, &collector))
    {
    }

    ~ScopedModalCollector()
    {
        activeModalCollector = previous_;
    }

    ScopedModalCollector(const ScopedModalCollector&) = delete;
    ScopedModalCollector& operator=(const ScopedModalCollector&) = delete;

private:
    std::vector<ModalPresentation>* previous_ = nullptr;
};

void collectModal(ModalPresentation modal)
{
    if (activeModalCollector != nullptr) {
        activeModalCollector->push_back(std::move(modal));
    }
}

void collectSharedUtilityModals();

std::string modalTemplate(std::string_view modalId, std::string_view title, std::string body)
{
    collectModal({
        std::string(modalId),
        std::string(title),
        std::move(body),
    });
    if (modalId == ui::modals::settings) {
        collectSharedUtilityModals();
    }
    return {};
}

std::string autoModalTemplate(
    std::string_view modalId,
    std::string_view title,
    std::string body,
    bool dismissible = true,
    std::string_view closeAction = {},
    ModalTone tone = ModalTone::Neutral)
{
    collectModal({
        std::string(modalId),
        std::string(title),
        std::move(body),
        std::string(closeAction),
        true,
        dismissible,
        true,
        tone,
    });
    return {};
}

void collectSharedUtilityModals()
{
    const std::string controlsBody =
        "<div class=\"detail-stack rr-detail-stack modal-body controller-controls\">"
        "<div><strong>Menus</strong><span>Left stick or D-pad navigates. South selects. East goes back. Right stick scrolls.</span></div>"
        "<div><strong>Shortcuts</strong><span>Menu opens this pause menu. View opens Map. North opens Inventory outside real-time play.</span></div>"
        "<div><strong>Launch</strong><span>Left stick steers and changes throttle. South turns around. West turns engines off or on.</span></div>"
        "<div><strong>Flight</strong><span>Left stick steers. Hold East to abort Flyby or Orbit. During Scan or Push, tap East to log or bank; hold East to abort.</span></div>"
        "<div><strong>Mining rig</strong><span>Left stick moves. Right trigger drills. West scans. North tethers. Tap South to stow cargo or leave; hold South for 0.6 seconds to exit.</span></div>"
        "<div><strong>Jetpack EVA</strong><span>Left stick thrusts. Right stick aims. Right trigger fires. Left trigger drills. West scans. North tethers. Hold South for 0.6 seconds to enter.</span></div>"
        "</div>";
    const std::string systemMenuBody =
        "<div class=\"modal-actions action-row system-menu-actions\">"
        "<button type=\"button\" class=\"ok rr-text-button\" data-ui-close-modal=\"1\" data-controller-resume=\"1\" "
        "data-ui-focus-id=\"system:resume\" data-ui-default-focus=\"1\"><span class=\"rr-button-label\">Resume</span></button>" +
        modalButton("Controls", "controls", "ghost") +
        modalButton("Settings", ui::modals::settings, "ghost") +
        modalButton("Map", ui::modals::map, "ghost") +
        modalButton("Inventory", ui::modals::inventory, "ghost") +
        "</div>";
    const std::string resetBody =
        "<p class=\"modal-intro\">This permanently clears campaign progress and starts a new save.</p>"
        "<div class=\"modal-actions action-row\">"
        "<button type=\"button\" class=\"ok rr-text-button\" data-ui-close-modal=\"1\" data-ui-focus-id=\"reset:cancel\" data-ui-default-focus=\"1\"><span class=\"rr-button-label\">Cancel</span></button>"
        "<button type=\"button\" class=\"danger rr-text-button\" data-rr-action=\"reset_save\" data-ui-focus-id=\"action:reset_save\" "
        "data-controller-hold-seconds=\"0.75\"><span class=\"rr-button-label\">Hold to reset save</span></button></div>";

    collectModal({"system_menu", "Paused", systemMenuBody, {}, false, false, false});
    collectModal({"controls", "Controller controls", controlsBody});
    collectModal({"reset_save_confirm", "Reset save?", resetBody, {}, false, true, false});
}

std::string disabledButton(std::string_view label)
{
    return "<button class=\"disabled rr-text-button\" disabled><span class=\"rr-button-label\">" + htmlEscape(label) + "</span></button>";
}

std::string warningClass(double value)
{
    if (value >= tuning::launch::warningCriticalThreshold) {
        return "critical";
    }
    if (value >= tuning::launch::warningCautionThreshold) {
        return "caution";
    }
    return "nominal";
}

std::string warningButton(std::string_view label, double value)
{
    return "<button type=\"button\" class=\"warning-button " + warningClass(value) + "\"><strong>" +
        htmlEscape(label) + "</strong><span>" + htmlEscape(display::percent(value)) + "</span></button>";
}

std::string realtimeWarningButton(std::string_view id, std::string_view label, double value)
{
    return "<button id=\"" + std::string(id) + "\" type=\"button\" class=\"warning-button " +
        warningClass(value) + "\"><strong>" + htmlEscape(label) + "</strong><span id=\"" +
        std::string(id) + "-value\">" + htmlEscape(display::percent(value)) + "</span></button>";
}

int miningAlertPulseBucket(double elapsedSeconds, double pulsesPerSecond)
{
    constexpr double twoPi = 6.28318530717958647692;
    const double wave = 0.5 + 0.5 * std::sin(elapsedSeconds * pulsesPerSecond * twoPi - 1.57079632679489661923);
    return std::clamp(static_cast<int>(std::round(wave * 3.0)), 0, 3);
}

std::string miningVitalAlertClass(
    std::string_view vitalClass,
    double pressure,
    double elapsedSeconds,
    bool outlinedNominal = false)
{
    const std::string severity = warningClass(pressure);
    if (severity == "nominal") {
        return std::string(vitalClass) + (outlinedNominal ? " mining-alert-neutral" : " mining-alert-nominal");
    }

    const double pulseRate = severity == "critical" ? 1.55 : 1.15;
    return std::string(vitalClass) + " mining-alert-" + severity + " mining-alert-pulse-" +
        std::to_string(miningAlertPulseBucket(elapsedSeconds, pulseRate));
}

std::string miningDrillHeatAlertClass(double heat, double elapsedSeconds)
{
    if (heat >= tuning::mining::drillHeatFlashThreshold) {
        return "mining-vital-heat mining-alert-critical mining-alert-pulse-" +
            std::to_string(miningAlertPulseBucket(elapsedSeconds, 1.55));
    }
    if (heat >= tuning::mining::drillHeatCriticalThreshold) {
        return "mining-vital-heat mining-alert-critical";
    }
    if (heat >= tuning::mining::drillHeatCautionThreshold) {
        return "mining-vital-heat mining-alert-caution";
    }
    return "mining-vital-heat mining-alert-nominal";
}

bool miningOperatorIsEva(const MiningRunState& mining)
{
    return mining.operatorMode == MiningOperatorMode::Jetpack && mining.operatorPresent;
}

std::string miningOperatorModeLabel(const MiningRunState& mining)
{
    if (miningOperatorIsEva(mining)) {
        return "JETPACK EVA";
    }
    return mining.rigDisabled ? "RIG DISABLED" : "MINING RIG";
}

std::string_view miningActorIntegrityLabel(const MiningRunState& mining)
{
    return miningOperatorIsEva(mining) ? "SUIT INTEGRITY" : "RIG INTEGRITY";
}

double miningActorIntegrity(const MiningRunState& mining)
{
    return miningOperatorIsEva(mining) ? mining.operatorIntegrity : mining.droneHealth;
}

std::string miningGravityLabel(const MiningRunState& mining)
{
    std::string direction = "DOWN";
    if (std::abs(mining.gravityDirectionX) > std::abs(mining.gravityDirectionY)) {
        direction = mining.gravityDirectionX >= 0.0 ? "RIGHT" : "LEFT";
    } else if (mining.gravityDirectionY < 0.0) {
        direction = "UP";
    }
    const double destinationScale = tuning::mining::baseGravityCellsPerSecondSquared > 0.0
        ? mining.gravityStrength / tuning::mining::baseGravityCellsPerSecondSquared
        : 0.0;
    return direction + " / " + display::fixed(destinationScale, 2) + "g";
}

std::string miningTetherBurdenLabel(const MiningRunState& mining, const MiningLoadStats& load)
{
    const bool artifactTethered = mining.artifact.present &&
        mining.artifact.tethered &&
        mining.artifact.state != MiningArtifactState::Delivered &&
        mining.artifact.state != MiningArtifactState::Destroyed;
    const bool operatorRigTethered = mining.operatorRigTethered;
    if (!artifactTethered && !operatorRigTethered) {
        return "CLEAR";
    }
    if (operatorRigTethered && !artifactTethered) {
        return "EVA TO MINING RIG / TOW TO SHIP";
    }
    if (operatorRigTethered && artifactTethered) {
        return "EVA + ARTIFACT / " + display::fixed(load.burden, 1) + " LOAD";
    }
    return display::fixed(load.burden, 1) + " / " + display::percent(load.speedMultiplier) + " SPD";
}

int activeMiningLooseChunkCount(const MiningRunState& mining)
{
    return static_cast<int>(std::count_if(
        mining.looseChunks.begin(),
        mining.looseChunks.end(),
        [](const MiningLooseChunk& chunk) { return chunk.active; }));
}

std::string miningDroneParentLabel(const MiningRunState& mining)
{
    if (mining.miniDrones.empty()) {
        return "NO SWARM";
    }
    const MiningAnchorTarget commonTarget = mining.miniDrones.front().anchorTarget;
    const bool mixedTargets = std::any_of(
        mining.miniDrones.begin() + 1,
        mining.miniDrones.end(),
        [commonTarget](const MiningMiniDroneAgent& drone) { return drone.anchorTarget != commonTarget; });
    if (mixedTargets) {
        return "SWARM -> MIXED";
    }
    switch (commonTarget) {
    case MiningAnchorTarget::Rig:
        return "SWARM -> RIG";
    case MiningAnchorTarget::Operator:
        return "SWARM -> OPERATOR";
    case MiningAnchorTarget::ControlledActor:
    default:
        return miningOperatorIsEva(mining) ? "SWARM -> OPERATOR" : "SWARM -> RIG";
    }
}

std::string miningSupportTileLabel(const MiningRunState& mining)
{
    const bool hasMiningDrone = std::any_of(
        mining.miniDrones.begin(), mining.miniDrones.end(), [](const MiningMiniDroneAgent& drone) {
            return drone.role == MiniDroneRole::Mining;
        });
    if (hasMiningDrone) {
        return "SUPPORT DRONES";
    }
    const bool hasHazardDrone = std::any_of(
        mining.miniDrones.begin(),
        mining.miniDrones.end(),
        [](const MiningMiniDroneAgent& drone) {
            return drone.role == MiniDroneRole::Hazard;
        });
    return hasHazardDrone ? "HAZARD SQUAD" : "SUPPORT ANCHOR";
}

std::string miningSupportTileValue(const MiningRunState& mining)
{
    for (const MiningMiniDroneAgent& drone : mining.miniDrones) {
        if (drone.role != MiniDroneRole::Mining) {
            continue;
        }
        const int capacity = tuning::mining::miningDroneCapacityChunks(drone.upgradeLevel);
        const int ore = std::max(0, drone.haulMaterials.common) + std::max(0, drone.haulMaterials.rare) + std::max(0, drone.haulMaterials.exotic);
        if (drone.behavior == MiningMiniDroneBehavior::Working) {
            const MiningCell* cell = miningCellAt(mining.terrain, drone.targetCellX, drone.targetCellY);
            const MiningCellMaterial material = cell == nullptr ? MiningCellMaterial::CommonOre : cell->material;
            const double duration = tuning::mining::miningDroneWorkSeconds(drone.upgradeLevel, material);
            return std::string(drone.finishTargetBeforeReturn ? "FINISHING ORE • RETURN PENDING " : "MINING ") +
                display::percent(std::clamp(drone.taskProgressSeconds / std::max(0.01, duration), 0.0, 1.0)) +
                " â¢ ORE " + std::to_string(ore) + "/" + std::to_string(capacity);
        }
        if (drone.behavior == MiningMiniDroneBehavior::DeliveringToShip) {
            return "RETURNING TO SHIP â¢ ORE " + std::to_string(ore) + "/" + std::to_string(capacity);
        }
        if (drone.behavior == MiningMiniDroneBehavior::ReturningFromShip) {
            return "RETURNING TO RIG â¢ ORE 0/" + std::to_string(capacity);
        }
        if (drone.behavior == MiningMiniDroneBehavior::RecoveringToRig) {
            return "SAFE RECALLING TO RIG â¢ ORE " + std::to_string(ore) + "/" + std::to_string(capacity);
        }
        if (drone.behavior == MiningMiniDroneBehavior::Returning) {
            return "RETURNING TO RIG â¢ ORE " + std::to_string(ore) + "/" + std::to_string(capacity);
        }
        if (ore > 0 && drone.targetCellX < 0) {
            return "NO REACHABLE ORE â¢ ORE " + std::to_string(ore) + "/" + std::to_string(capacity);
        }
    }
    std::vector<std::pair<int, int>> activeTargets;
    int total = 0;
    int working = 0;
    int traveling = 0;
    int assisting = 0;
    for (const MiningMiniDroneAgent& drone : mining.miniDrones) {
        if (drone.role != MiniDroneRole::Hazard) {
            continue;
        }
        ++total;
        if (drone.behavior == MiningMiniDroneBehavior::Working) {
            ++working;
            const std::pair<int, int> target {
                drone.targetCellX,
                drone.targetCellY
            };
            if (std::find(activeTargets.begin(), activeTargets.end(), target) !=
                activeTargets.end()) {
                ++assisting;
            } else {
                activeTargets.push_back(target);
            }
        } else if (
            drone.behavior == MiningMiniDroneBehavior::Traveling &&
            drone.targetCellX >= 0 &&
            drone.targetCellY >= 0) {
            ++traveling;
        }
    }
    if (total <= 0) {
        return miningDroneParentLabel(mining);
    }
    if (working > 0 && assisting > 0) {
        return std::to_string(working - assisting) + " TREATING \xE2\x80\xA2 " +
            std::to_string(assisting) + " ASSISTING";
    }
    if (working > 0 && traveling > 0) {
        return std::to_string(working) + " WORK \xE2\x80\xA2 " +
            std::to_string(traveling) + " EN ROUTE";
    }
    if (working > 0) {
        return std::to_string(working) + "/" + std::to_string(total) +
            " TREATING";
    }
    if (traveling > 0) {
        return std::to_string(traveling) + "/" + std::to_string(total) +
            " EN ROUTE";
    }
    return "NO REVEALED HAZARD IN RANGE";
}

std::string statChip(const RefitStatChip& chip)
{
    const bool wideChip = chip.label.size() > 8 || chip.label.find(' ') != std::string::npos;
    return "<span class=\"stat-chip " + std::string(chip.positive ? "up" : "down") +
        std::string(wideChip ? " wide" : "") + "\"><span class=\"rr-chip-label\">" +
        htmlEscape(chip.label) + " " + htmlEscape(chip.value) + "</span></span>";
}

std::string resourceChip(const PanelMetricPresentation& chip)
{
    const bool positive = chip.value.empty() || chip.value.front() != '-';
    const bool wideChip = chip.label.empty() || chip.label.size() > 8 || chip.label.find(' ') != std::string::npos;
    const std::string text = chip.label.empty()
        ? chip.value
        : chip.label + " " + chip.value;
    return "<span class=\"stat-chip " + std::string(positive ? "up" : "down") +
        std::string(wideChip ? " wide" : "") + "\"><span class=\"rr-chip-label\">" +
        htmlEscape(text) + "</span></span>";
}

std::string hangarFuelChip(const PanelMetricPresentation& chip)
{
    return "<span class=\"stat-chip up wide hangar-fuel-chip\"><span class=\"rr-chip-label\"><span class=\"hangar-fuel-label\">" +
        htmlEscape(chip.label) + "</span><strong>" + htmlEscape(chip.value) + "</strong></span></span>";
}

std::string realtimeResourceChip(std::string_view id, const PanelMetricPresentation& chip)
{
    const bool positive = chip.value.empty() || chip.value.front() != '-';
    const bool wideChip = chip.label.size() > 8 || chip.label.find(' ') != std::string::npos;
    return "<span id=\"" + std::string(id) + "\" class=\"stat-chip " +
        std::string(positive ? "up" : "down") + std::string(wideChip ? " wide" : "") + "\"><span class=\"rr-chip-label\">" +
        htmlEscape(chip.label) + " " + htmlEscape(chip.value) + "</span></span>";
}

std::string surfaceActionChipLabel(std::string_view label)
{
    if (label == text::labels::commonMaterials) {
        return "Common";
    }
    if (label == text::labels::rareMaterials) {
        return "Rare";
    }
    if (label == text::labels::exoticMaterials) {
        return "Exotic";
    }
    if (label == text::labels::artifacts) {
        return "Art";
    }
    if (label == text::labels::extractionRisk) {
        return "Risk";
    }
    if (label == text::labels::hazard) {
        return "Haz";
    }
    if (label == text::labels::oxygen) {
        return "O2";
    }
    if (label == text::labels::rigFuel || label == text::labels::arkFuel) {
        return "Fuel";
    }
    return std::string(label);
}

std::string fieldContextValue(const PanelMetricPresentation& chip)
{
    if (chip.label != text::labels::site) {
        return chip.value;
    }
    if (chip.value == text::panel::surfaceSites::surveyBasin) {
        return "Basin";
    }
    if (chip.value == text::panel::surfaceSites::oreShelf) {
        return "Ore";
    }
    if (chip.value == text::panel::surfaceSites::fractureField) {
        return "Fracture";
    }
    return chip.value;
}

std::string fieldContextChip(const PanelMetricPresentation& chip)
{
    const std::string label = surfaceActionChipLabel(chip.label);
    const std::string text = label + " " + fieldContextValue(chip);
    const bool positive = chip.value.empty() || chip.value.front() != '-';
    const bool wideChip = text.size() > 11 || label.find(' ') != std::string::npos;
    return "<span class=\"stat-chip " + std::string(positive ? "up" : "down") +
        std::string(wideChip ? " wide" : "") + "\"><span class=\"rr-chip-label\">" +
        htmlEscape(text) + "</span></span>";
}

std::string statChipGrid(const std::vector<RefitStatChip>& chips)
{
    std::string tags;
    for (const RefitStatChip& chip : chips) {
        tags += statChip(chip);
    }
    return tags;
}

std::string resourceChipGrid(const std::vector<PanelMetricPresentation>& chips)
{
    std::string tags;
    for (const PanelMetricPresentation& chip : chips) {
        tags += resourceChip(chip);
    }
    return tags;
}

std::string fieldContextChipGrid(const std::vector<PanelMetricPresentation>& chips)
{
    std::string tags;
    for (const PanelMetricPresentation& chip : chips) {
        tags += fieldContextChip(chip);
    }
    return tags;
}

bool isSurfaceMiningAction(const SurfaceActionPreviewPresentation& action)
{
    return action.action.actionId == ui::actions::mineSurface || action.title == text::buttons::mineDeposit;
}

bool isSurfaceExtractionAction(const SurfaceActionPreviewPresentation& action)
{
    return action.action.actionId == ui::actions::extractSurface;
}

std::string surfaceActionRiskRewardCue(const SurfaceActionPreviewPresentation& action)
{
    if (!action.summary.empty()) {
        return action.summary;
    }
    std::string cue = action.risk + " " + surfaceActionChipLabel(action.riskLabel);
    if (!action.payoffChips.empty()) {
        const PanelMetricPresentation& payoff = action.payoffChips.front();
        cue += " / " + surfaceActionChipLabel(payoff.label) + " " + payoff.value;
    }
    return cue;
}

PanelButtonPresentation surfaceActionFooterButton(const SurfaceActionPreviewPresentation& action)
{
    PanelButtonPresentation buttonPresentation = action.action;
    if (isSurfaceMiningAction(action) && buttonPresentation.enabled) {
        if (buttonPresentation.cssClass.empty()) {
            buttonPresentation.cssClass = "risk";
        } else if (buttonPresentation.cssClass.find("risk") == std::string::npos) {
            buttonPresentation.cssClass += " risk";
        }
    }
    if (isSurfaceExtractionAction(action) && buttonPresentation.enabled) {
        buttonPresentation.cssClass = "surface-return-action";
    }
    if (!buttonPresentation.enabled && (buttonPresentation.label.rfind("Need ", 0) == 0 || buttonPresentation.label.size() > 14)) {
        buttonPresentation.label = std::string(text::buttons::unavailable);
    }
    return buttonPresentation;
}

std::string operationCard(std::string title, std::string detail, std::string cost, std::string_view action, bool available, std::string cssClass = "")
{
    std::ostringstream out;
    out << "<article class=\"ops-card rr-fixed-lane-card ui-choice-row management-choice-row " << htmlEscape(cssClass) << "\">";
    out << "<h3 class=\"card-title\">" << htmlEscape(title) << "</h3>";
    out << "<p class=\"card-copy ops-detail\">" << htmlEscape(detail) << "</p>";
    out << "<div class=\"card-footer action-row\"><span class=\"ops-cost\">" << htmlEscape(cost) << "</span>";
    out << (available ? button(text::buttons::assign, action) : disabledButton(text::buttons::unavailable));
    out << "</div></article>";
    return out.str();
}

std::string operationCard(const HangarOperationCardPresentation& card)
{
    return operationCard(card.title, card.detail, card.cost, card.actionId, card.available, card.cssClass);
}

std::string operationModalCard(const HangarOperationCardPresentation& card, std::string_view buttonLabel, std::string_view modalId)
{
    std::ostringstream out;
    out << "<article class=\"ops-card rr-fixed-lane-card ui-choice-row management-choice-row " << htmlEscape(card.cssClass) << "\">";
    out << "<h3 class=\"card-title\">" << htmlEscape(card.title) << "</h3>";
    out << "<p class=\"card-copy ops-detail\">" << htmlEscape(card.detail) << "</p>";
    out << "<div class=\"card-footer action-row\"><span class=\"ops-cost\">" << htmlEscape(card.cost) << "</span>";
    out << (card.available ? modalButton(buttonLabel, modalId) : disabledButton(text::buttons::unavailable));
    out << "</div></article>";
    return out.str();
}

std::pair<std::string, std::string> crewClassAndFocus(const Astronaut& astronaut)
{
    const std::string marker = " - ";
    const std::size_t split = astronaut.background.find(marker);
    if (split == std::string::npos) {
        return {astronaut.background, "Field specialist"};
    }
    return {
        astronaut.background.substr(0, split),
        astronaut.background.substr(split + marker.size())
    };
}

std::string pilotCandidateCard(const Astronaut& candidate, int index, bool available)
{
    const auto [crewClass, focus] = crewClassAndFocus(candidate);
    std::ostringstream out;
    out << "<article class=\"pilot-card\">";
    out << "<div class=\"pilot-card-top\"><span>" << htmlEscape(focus) << "</span><strong>" << htmlEscape(crewClass) << "</strong></div>";
    out << "<div class=\"pilot-portrait-placeholder\"><span>" << htmlEscape(candidate.name.substr(0, 1)) << "</span></div>";
    out << "<h3 class=\"card-title\">" << htmlEscape(candidate.name) << "</h3>";
    out << "<p class=\"card-copy\">" << htmlEscape(candidate.trait) << "</p>";
    out << "<div class=\"pilot-stat-grid metric-strip rr-metric-strip\">";
    out << "<span>Training <strong>" << htmlEscape(std::to_string(candidate.training)) << "</strong></span>";
    out << "<span>Stress <strong>" << htmlEscape(display::percent(candidate.stress)) << "</strong></span>";
    out << "</div>";
    out << (available ? button("Select pilot", ui::actions::recruitCandidate(index), "ok") : disabledButton(text::buttons::unavailable));
    out << "</article>";
    return out.str();
}

std::string panelButton(const PanelButtonPresentation& action, bool defaultFocus = false)
{
    if (!action.enabled) {
        if (!action.actionId.empty()) {
            return "<button type=\"button\" class=\"disabled rr-text-button\" data-rr-action=\"" + htmlEscape(action.actionId) + "\" disabled><span class=\"rr-button-label\">" +
                htmlEscape(action.label) + "</span></button>";
        }
        return disabledButton(action.label);
    }
    return button(action.label, action.actionId, action.cssClass, defaultFocus);
}

// The live mining dock uses a fixed-height, single-line control tier. Keep its
// labels as direct text so RmlUi can center them with the button's line-height
// without the zero-width flex-child layout seen in the responsive dock.
std::string miningPanelButton(const PanelButtonPresentation& action, bool defaultFocus = false)
{
    const std::string cssClass = action.enabled ? action.cssClass : "disabled";
    const std::string classAttr = " class=\"" + htmlEscape(cssClass) + " rr-mining-text-button\"";
    const std::string defaultAttr = defaultFocus ? " data-ui-default-focus=\"1\"" : "";
    std::ostringstream out;
    out << "<button type=\"button\"" << classAttr;
    if (!action.actionId.empty()) {
        out << " data-rr-action=\"" << htmlEscape(action.actionId) << "\" data-ui-focus-id=\"action:"
            << htmlEscape(action.actionId) << "\"";
    }
    out << defaultAttr;
    if (!action.enabled) {
        out << " disabled";
    }
    out << ">" << htmlEscape(action.label) << "</button>";
    return out.str();
}

std::string miningModalButton(
    std::string_view label,
    std::string_view modalId,
    std::string_view cssClass = "ghost")
{
    const std::string classAttr = " class=\"" + std::string(cssClass) +
        (cssClass.empty() ? "" : " ") + "rr-mining-text-button mining-utility-button\"";
    return "<button type=\"button\"" + classAttr + " data-ui-modal=\"" + htmlEscape(modalId) +
        "\" data-ui-focus-id=\"modal:" + htmlEscape(modalId) + "\">" + htmlEscape(label) + "</button>";
}

std::string introductoryPanelButton(
    const PanelButtonPresentation& action,
    std::string_view modalId,
    bool defaultFocus = false)
{
    if (!action.enabled || modalId.empty()) {
        return panelButton(action, defaultFocus);
    }
    return modalButton(action.label, modalId, action.cssClass, defaultFocus);
}

std::string activityIntroductionModal(
    std::string_view modalId,
    std::string_view title,
    std::string_view setup,
    std::string_view payoff,
    std::string_view actionLabel,
    std::string_view actionId,
    std::string_view actionClass,
    bool autoOpen = false)
{
    std::ostringstream body;
    body << "<section class=\"activity-introduction modal-body\">"
        << "<span class=\"activity-introduction-kicker\">First operations brief</span>"
        << "<p class=\"activity-introduction-setup\">" << htmlEscape(setup) << "</p>"
        << "<div class=\"activity-introduction-payoff\"><span>Why it matters</span><strong>"
        << htmlEscape(payoff) << "</strong></div>"
        << "<div class=\"modal-actions action-row rr-action-footer activity-introduction-actions\">"
        << button(actionLabel, actionId, std::string(actionClass), true) << "</div></section>";
    return autoOpen
        ? autoModalTemplate(modalId, title, body.str())
        : modalTemplate(modalId, title, body.str());
}

// Superseded campaign-only presentation. ScenarioObjectivePresentation below
// is the sole live presentation path; retain this source block temporarily so
// old save migration diagnostics can be compared while v9 lands.
#if 0
bool prospectorCompletionPending(const GameState& state)
{
    return canClaimLunarProspector(state);
}

std::string prospectorCompletionModal(const GameState& state)
{
    if (!prospectorCompletionPending(state)) {
        return {};
    }

    std::ostringstream body;
    body << "<section class=\"activity-introduction modal-body\">"
        << "<span class=\"activity-introduction-kicker\">READY TO CLAIM // MOON</span>"
        << "<p class=\"activity-introduction-setup\">"
        << tuning::research::prospectorCommonOreGoal
        << " gray-seamed Common Ore samples are delivered and reserved for fabrication.</p>"
        << "<div class=\"activity-introduction-payoff\"><span>Prospector Mk I</span><strong>"
        << "Install your first Support Drone and bring Support Drone Slot 1 online."
        << "</strong></div><div class=\"modal-actions action-row rr-action-footer activity-introduction-actions\">"
        << button("Install Prospector Mk I", ui::actions::claimLunarProspector, "ok", true)
        << "</div></section>";
    return autoModalTemplate(
        ui::modals::prospectorCompletion,
        "LUNAR CONTRACT COMPLETE",
        body.str(),
        false);
}

std::string marsBayCompletionModal(const GameState& state)
{
    if (!canClaimMarsBayExpansion(state)) {
        return {};
    }

    std::ostringstream body;
    body << "<section class=\"activity-introduction modal-body\">"
        << "<span class=\"activity-introduction-kicker\">READY TO CLAIM // MARS</span>"
        << "<p class=\"activity-introduction-setup\">"
        << tuning::research::marsBayCommonOreGoal
        << " local Common Ore samples are delivered and reserved for the bay frame.</p>"
        << "<div class=\"activity-introduction-payoff\"><span>Support Drone Slot 2</span><strong>"
        << "Fabricate one empty specialist slot. No duplicate drone will be assigned."
        << "</strong></div><div class=\"modal-actions action-row rr-action-footer activity-introduction-actions\">"
        << button("Fabricate Drone Bay Slot 2", ui::actions::claimMarsBayExpansion, "ok", true)
        << "</div></section>";
    return autoModalTemplate(
        ui::modals::marsBayCompletion,
        "MARS EXPANSION READY",
        body.str(),
        false);
}

std::string lunarMiningBriefingModal(const GameState& state)
{
    const CampaignObjectiveStatus status = campaignObjectiveStatus(state, CampaignObjectiveId::LunarProspector);
    if (status.state == CampaignObjectiveState::Complete || status.briefingAcknowledged) {
        return {};
    }

    std::ostringstream body;
    body << "<section class=\"activity-introduction modal-body campaign-briefing\">"
        << "<span class=\"activity-introduction-kicker\">MANDATORY DIRECTIVE // MOON</span>"
        << "<p class=\"activity-introduction-setup\">Most lunar regolith is inert. Recover "
        << tuning::research::prospectorCommonOreGoal
        << " gray-seamed Common Ore deposits and return them safely.</p>"
        << "<div class=\"activity-introduction-payoff\"><span>Visual identification</span><strong>"
        << "Plain dirt yields nothing. Common Ore uses a silver seam, gray shimmer, and hex marker."
        << "</strong></div><div class=\"modal-actions action-row rr-action-footer activity-introduction-actions\">"
        << button("Accept Contract", ui::actions::acknowledgeLunarMiningBriefing, "ok", true)
        << "</div></section>";
    return autoModalTemplate(
        ui::modals::lunarMiningBriefing,
        "LUNAR PROSPECTOR CONTRACT",
        body.str(),
        false);
}

std::string marsMiningBriefingModal(const GameState& state)
{
    const CampaignObjectiveStatus status = campaignObjectiveStatus(state, CampaignObjectiveId::MarsBayExpansion);
    if (status.state == CampaignObjectiveState::Complete || status.briefingAcknowledged) {
        return {};
    }

    std::ostringstream body;
    body << "<section class=\"activity-introduction modal-body campaign-briefing\">"
        << "<span class=\"activity-introduction-kicker\">MANDATORY DIRECTIVE // MARS</span>"
        << "<p class=\"activity-introduction-setup\">Local material can support a second specialist bay. Recover "
        << tuning::research::marsBayCommonOreGoal
        << " Martian Common Ore and extract it safely.</p>"
        << "<div class=\"activity-introduction-payoff\"><span>Support Drone Slot 2</span><strong>"
        << "Oxygen, drill heat, integrity, repairs, and the return decision are now live. The completed bay starts empty so you can choose the next specialist."
        << "</strong></div><div class=\"modal-actions action-row rr-action-footer activity-introduction-actions\">"
        << button("Accept Contract", ui::actions::acknowledgeMarsMiningBriefing, "ok", true)
        << "</div></section>";
    return autoModalTemplate(
        ui::modals::marsMiningBriefing,
        "MARS BAY EXPANSION",
        body.str(),
        false);
}

std::string ioVolcanicBriefingModal(const GameState& state)
{
    const CampaignObjectiveStatus status = campaignObjectiveStatus(state, CampaignObjectiveId::IoVolcanicDescent);
    if (status.state == CampaignObjectiveState::Complete || state.meta.ioHazardDroneCommissioned) {
        return {};
    }

    std::ostringstream body;
    body << "<section class=\"activity-introduction modal-body campaign-briefing\">"
        << "<span class=\"activity-introduction-kicker\">MANDATORY DIRECTIVE // IO, JUPITER SYSTEM</span>"
        << "<p class=\"activity-introduction-setup\">Io's regolith is barren. Ore survives only inside lava seams, and untreated lava cannot be drilled safely.</p>"
        << "<div class=\"activity-introduction-payoff\"><span>Hazard Drone Mk I</span><strong>"
        << "Commission this permanent Support Drone to cool each lava seal into gray Common Ore."
        << "</strong></div><div class=\"modal-actions action-row rr-action-footer activity-introduction-actions\">"
        << button("Commission Hazard Drone", ui::actions::commissionIoHazardDrone, "ok", true)
        << "</div></section>";
    return autoModalTemplate(
        ui::modals::ioVolcanicBriefing,
        "IO VOLCANIC DESCENT",
        body.str(),
        false);
}

std::string saturnSlingshotBriefingModal(const GameState& state)
{
    const CampaignObjectiveStatus status = campaignObjectiveStatus(state, CampaignObjectiveId::SaturnSlingshot);
    if (status.state == CampaignObjectiveState::Complete
        || status.briefingAcknowledged
        || !state.meta.ioArtifactRecovered) {
        return {};
    }

    std::ostringstream body;
    body << "<section class=\"activity-introduction modal-body campaign-briefing\">"
        << "<span class=\"activity-introduction-kicker\">MANDATORY DIRECTIVE // JUPITER DEPARTURE</span>"
        << "<p class=\"activity-introduction-setup\">Saturn is beyond normal transfer range. Only a Perfect pass through the gold corridor opens the route.</p>"
        << "<div class=\"activity-introduction-payoff\"><span>Point of no return</span><strong>"
        << "Locking Saturn commits the expedition outward. The inner system will no longer be reachable."
        << "</strong></div><div class=\"modal-actions action-row rr-action-footer activity-introduction-actions\">"
        << button("Begin Slingshot Run", ui::actions::beginSaturnSlingshot, "warn", true)
        << "</div></section>";
    return autoModalTemplate(
        ui::modals::saturnSlingshotBriefing,
        "SATURN SLINGSHOT REQUIRED",
        body.str(),
        false);
}

std::string saturnSlingshotFailureModal(const GameState& state)
{
    const bool completedFailure = state.screen == Screen::Flyby
        && state.run.flyby.purpose == FlybyPurpose::SaturnSlingshot
        && state.run.flyby.completed
        && state.run.flyby.result != FlybyGrade::Perfect;
    const bool savedFailure = state.screen == Screen::Hangar
        && state.meta.saturnSlingshotFailed
        && !state.meta.saturnSlingshotPerfect;
    if ((!completedFailure && !savedFailure)
        || state.meta.saturnSlingshotFailureAcknowledged) {
        return {};
    }

    std::ostringstream body;
    body << "<section class=\"activity-introduction modal-body campaign-briefing\">"
        << "<span class=\"activity-introduction-kicker\">SATURN ROUTE // LOCKED</span>"
        << "<p class=\"activity-introduction-setup\">INSUFFICIENT SLINGSHOT — Saturn remains locked. Hold the gold corridor for a Perfect pass.</p>"
        << "<div class=\"activity-introduction-payoff\"><span>Why the transfer failed</span><strong>"
        << "Only a Perfect pass supplies enough exit energy. Hold the gold corridor from entry through the finish gate."
        << "</strong></div><div class=\"modal-actions action-row rr-action-footer activity-introduction-actions\">"
        << button("Retry Perfect Corridor", ui::actions::retrySaturnSlingshot, "warn", true)
        << button("Return to Hangar", ui::actions::acknowledgeSaturnSlingshotFailure, "ghost")
        << "</div></section>";
    return autoModalTemplate(
        ui::modals::saturnSlingshotFailure,
        "INSUFFICIENT SLINGSHOT",
        body.str(),
        false);
}

#endif

bool jupiterWindowAvailable(const GameState& state, const ContentCatalog& catalog)
{
    const TransferAssistDefinition* definition = catalog.findTransferAssist(content::transferAssist::marsJupiter);
    const Destination* next = nextDestination(state, catalog);
    return definition != nullptr && next != nullptr &&
        currentDestination(state, catalog).id == definition->sourceDestinationId &&
        next->id == definition->targetDestinationId &&
        scenarioRouteRequirementStatus(state, catalog, *next).satisfied &&
        (definition->allowedLaunchStages.empty() ||
         std::find(
             definition->allowedLaunchStages.begin(),
             definition->allowedLaunchStages.end(),
             state.meta.launchLessons.stage) != definition->allowedLaunchStages.end());
}

std::string jupiterWindowModal(const GameState& state, const ContentCatalog& catalog)
{
    if (!jupiterWindowAvailable(state, catalog)) {
        return {};
    }

    const bool reviewed = jupiterWindowReviewed(state, catalog);
    const double tank = launchFuelCapacity(state);
    const double routeBurn = launchCruiseFuelCostForTier(3);
    const double tankMargin = tank - routeBurn;
    const TransferAssistDefinition* definition = catalog.findTransferAssist(content::transferAssist::marsJupiter);
    const double assistSavings = definition == nullptr ? tuning::flyby::jupiterSlingshotFuelSavings : definition->fuelSavings;
    const double slingshotBurn = std::max(0.0, routeBurn - assistSavings);
    const double combinedMargin = tank - slingshotBurn;
    const PendingTransferAssist* activeAssist = pendingTransferAssistForDestination(state, content::destination::jupiter);
    const double instabilityPenalty = activeAssist == nullptr ? 0.0 : activeAssist->instabilityPenalty;
    const bool tanksThreeInstalled =
        launchUpgradeRank(state, LaunchUpgradeKind::FuelTanks) >= 3;
    const ShipModule* fuelTanksThree = catalog.findModule(content::module::fuelTanks3);
    const int tankCost = fuelTanksThree == nullptr ? 92 : moduleOfferCost(*fuelTanksThree);

    std::ostringstream body;
    body << "<section class=\"jupiter-window modal-body campaign-briefing\">"
        << "<span class=\"activity-introduction-kicker\">JUPITER TRANSFER // OPEN OPTIONS</span>"
        << "<p class=\"activity-introduction-setup\">Create five fuel of transfer margin. Build it into the ship, take it from Mars's gravity, or stack both.</p>"
        << "<div class=\"jupiter-option-grid controller-choice-row\">"
        << "<article class=\"jupiter-option-card\"><span>PERMANENT ENGINEERING</span><h3>FUEL TANKS III</h3>"
        << "<p>" << (tanksThreeInstalled
            ? "Transfer Tank capacity is 25. The permanent engineering margin is installed; the Jupiter burn remains "
            : "Increase the Transfer Tank from 20 to 25. The Jupiter burn remains ")
        << display::fixed(routeBurn, 0) << ".</p>"
        << "<strong>" << (tanksThreeInstalled
            ? "INSTALLED // +5 permanent capacity // No flight risk"
            : std::to_string(tankCost) + " credits // +5 permanent capacity // No flight risk")
        << "</strong></article>"
        << "<article class=\"jupiter-option-card\"><span>PRESS YOUR LUCK</span><h3>MARS SLINGSHOT</h3>"
        << "<p>A Good pass supplies enough momentum. Perfect keeps the Jupiter transfer stable; Good adds +35% flight instability for that attempt.</p>"
        << "<strong>" << (activeAssist != nullptr
            ? "ACTIVE // " + display::fixed(slingshotBurn, 0) + " powered burn // +" +
                display::percent(activeAssist->speedBoost) + " velocity from finish // " +
                (instabilityPenalty > 0.0
                    ? "+" + display::percent(instabilityPenalty) + " instability"
                    : "Perfect: stable")
            : "Good required // " + display::fixed(slingshotBurn, 0) +
                " powered burn // +0–40% velocity from finish speed // Perfect: stable")
        << "</strong></article>"
        << "</div><div class=\"jupiter-combined-preview\"><span>STACK BOTH</span><strong>25 tank // 15 burn // 10 margin // +slingshot velocity</strong>"
        << "<p>Good is enough to depart but adds +35% flight instability. Perfect preserves the same transfer benefit without the penalty.</p>"
        << "<p>Neither option closes the other. Current hardware margin: "
        << display::signedFixed(tankMargin, 0) << ". Current combined margin: "
        << display::signedFixed(combinedMargin, 0) << ".</p></div>"
        << "<div class=\"modal-actions action-row rr-action-footer jupiter-window-actions\">"
        << button("Open Refit", ui::actions::openJupiterRefit, "ok", true)
        << (activeAssist != nullptr
            ? button("Continue to Jupiter", ui::actions::continueTransferAssist, "warn")
            : (!reviewed
                  ? button("Review Options", ui::actions::acknowledgeJupiterWindow, "warn")
                  : (canStartJupiterSlingshot(state, catalog)
                        ? button("Begin Mars Slingshot", ui::actions::beginTransferAssist(content::transferAssist::marsJupiter), "warn")
                        : disabledButton("Mars Slingshot Unavailable"))))
        << button("Return to Hangar", ui::actions::acknowledgeJupiterWindow, "ghost")
        << "</div></section>";
    return reviewed
        ? modalTemplate(ui::modals::jupiterWindow, "THE JUPITER WINDOW", body.str())
        : autoModalTemplate(ui::modals::jupiterWindow, "THE JUPITER WINDOW", body.str(), false);
}

std::string jupiterSlingshotActiveModal(const GameState& state, const ContentCatalog& catalog)
{
    const PendingTransferAssist& pending = state.run.pendingTransferAssist;
    const TransferAssistDefinition* definition = catalog.findTransferAssist(pending.definitionId);
    const Destination* source = definition == nullptr ? nullptr : catalog.findDestination(definition->sourceDestinationId);
    const Destination* target = definition == nullptr ? nullptr : catalog.findDestination(definition->targetDestinationId);
    if (state.screen != Screen::Hangar || definition == nullptr || !pending.active() || source == nullptr ||
        target == nullptr || currentDestination(state, catalog).id != definition->sourceDestinationId) {
        return {};
    }
    const PendingTransferAssist* activeAssist = &pending;
    const double tank = launchFuelCapacity(state);
    const double routeBurn = launchCruiseFuelCostForTier(target->tier);
    const double poweredBurn = std::max(0.0, routeBurn - activeAssist->fuelSavings);
    const double margin = tank - poweredBurn;
    const double instabilityPenalty = activeAssist->instabilityPenalty;
    std::ostringstream body;
    body << "<section class=\"activity-introduction modal-body campaign-briefing jupiter-slingshot-active\">"
        << "<span class=\"activity-introduction-kicker\">SLINGSHOT ACTIVE"
        << (instabilityPenalty > 0.0 ? " // WILD RIDE" : " // STABLE") << "</span>"
        << "<p class=\"activity-introduction-setup\">"
        << (instabilityPenalty > 0.0
            ? "The Good pass supplied enough " + source->name + " momentum. Its recorded finish lane and outward drift carry into the " + target->name + " flight, and Good adds extra control instability."
            : "The Perfect pass carries its recorded finish lane and actual " + source->name + " exit velocity into launch without adding grade instability.")
        << "</p>"
        << "<div class=\"activity-introduction-payoff\"><span>Transfer underway</span><strong>"
        << display::fixed(tank, 0) << " tank // " << display::fixed(poweredBurn, 0)
        << " powered burn // " << display::fixed(margin, 0) << " margin // +"
        << display::percent(activeAssist->speedBoost) << " velocity from finish // "
        << (instabilityPenalty > 0.0
            ? "+" + display::percent(instabilityPenalty) + " flight instability"
            : "stable flight")
        << "</strong></div>"
        << "<div class=\"modal-actions action-row rr-action-footer activity-introduction-actions\">"
        << button("Continue to " + target->name, ui::actions::continueTransferAssist, "ok", true)
        << "</div></section>";
    return autoModalTemplate(
        ui::modals::jupiterSlingshotActive,
        "SLINGSHOT ACTIVE",
        body.str(),
        false);
}

std::string scenarioObjectiveStateLabel(ScenarioStepState state)
{
    switch (state) {
    case ScenarioStepState::Active:
        return "ACTIVE";
    case ScenarioStepState::ReadyToClaim:
        return "READY TO CLAIM";
    case ScenarioStepState::Complete:
        return "COMPLETE";
    case ScenarioStepState::Locked:
    default:
        return "LOCKED";
    }
}

std::string scenarioObjectiveStateClass(ScenarioStepState state)
{
    switch (state) {
    case ScenarioStepState::Active:
        return "state-active active";
    case ScenarioStepState::ReadyToClaim:
        return "state-ready ready-to-claim";
    case ScenarioStepState::Complete:
        return "state-complete complete";
    case ScenarioStepState::Locked:
    default:
        return "state-locked locked";
    }
}

std::string scenarioObjectiveDisplayStateLabel(
    const ScenarioObjectivePresentation& objective)
{
    return objective.returnPending
        ? "PENDING RETURN"
        : scenarioObjectiveStateLabel(objective.state);
}

int scenarioObjectiveDisplayCurrent(const ScenarioObjectivePresentation& objective)
{
    return objective.returnPending
        ? objective.required
        : objective.current;
}

std::string scenarioObjectiveMarkup(
    const ScenarioObjectivePresentation& objective,
    bool showAction = true,
    bool usePhaseLane = true)
{
    if (!objective.available) {
        return {};
    }

    std::ostringstream out;
    const std::string displayState = scenarioObjectiveDisplayStateLabel(objective);
    const int displayCurrent = scenarioObjectiveDisplayCurrent(objective);
    out << "<section class=\"objective-strip rr-objective-strip scenario-objective "
        << (usePhaseLane ? "phase-lane " : "")
        << scenarioObjectiveStateClass(objective.state)
        << "\" data-scenario-id=\"" << htmlEscape(objective.scenarioId)
        << "\" data-scenario-step-id=\"" << htmlEscape(objective.stepId)
        << "\" data-objective-state=\"" << htmlEscape(displayState) << "\">"
        << "<div class=\"campaign-objective-head\"><span>" << htmlEscape(objective.location)
        << "</span><em>" << htmlEscape(displayState) << "</em></div>"
        << "<strong>" << htmlEscape(objective.title) << "</strong>";
    if (objective.required > 0) {
        // A numeric readout remains authoritative for large contracts; a
        // compact 8-cell strip keeps all shared layout lanes stable.
        constexpr int visualSegments = 8;
        const int filled = std::clamp(
            static_cast<int>(std::ceil(
                static_cast<double>(std::clamp(displayCurrent, 0, objective.required)) /
                static_cast<double>(objective.required) * visualSegments)),
            0,
            visualSegments);
        out << "<div class=\"campaign-progress\" aria-label=\""
            << htmlEscape(std::to_string(displayCurrent) + " of " + std::to_string(objective.required) +
                (objective.returnPending ? ", pending return" : ""))
            << "\">";
        for (int index = 0; index < visualSegments; ++index) {
            out << "<i class=\"" << (index < filled ? "is-filled" : "") << "\"></i>";
        }
        out << "<b>" << std::clamp(displayCurrent, 0, objective.required) << "/"
            << objective.required
            << (objective.returnPending ? " // PENDING RETURN" : "")
            << "</b></div>";
    }
    out << "<p>" << htmlEscape(objective.detail) << "</p>"
        << "<div class=\"campaign-objective-foot\"><small>" << htmlEscape(objective.rewardPreview) << "</small>";
    if (showAction) {
        out << scenarioActionButton(
            objective,
            objective.state == ScenarioStepState::ReadyToClaim ? "ok" : "warn");
    }
    out << "</div></section>";
    return out.str();
}

std::string droneMissionStripMarkup(
    const ScenarioObjectivePresentation& objective,
    std::string_view instruction)
{
    if (!objective.available) {
        return {};
    }

    std::ostringstream out;
    const std::string displayState = scenarioObjectiveDisplayStateLabel(objective);
    const int displayCurrent = scenarioObjectiveDisplayCurrent(objective);
    out << "<section class=\"drone-mission-strip scenario-objective "
        << scenarioObjectiveStateClass(objective.state)
        << "\" data-scenario-id=\"" << htmlEscape(objective.scenarioId)
        << "\" data-scenario-step-id=\"" << htmlEscape(objective.stepId)
        << "\" data-objective-state=\"" << htmlEscape(displayState) << "\">"
        << "<div class=\"drone-mission-identity\"><span>" << htmlEscape(objective.location)
        << "</span><em>" << htmlEscape(displayState) << "</em></div>"
        << "<strong class=\"drone-mission-title\">" << htmlEscape(objective.title) << "</strong>";
    if (objective.required > 0) {
        out << "<div class=\"drone-mission-progress\" aria-label=\""
            << htmlEscape(std::to_string(displayCurrent) + " of " + std::to_string(objective.required) +
                (objective.returnPending ? ", pending return" : ""))
            << "\"><b>" << std::clamp(displayCurrent, 0, objective.required) << "/"
            << objective.required
            << (objective.returnPending ? " // PENDING RETURN" : "")
            << "</b></div>";
    }
    out << "<p class=\"drone-mission-instruction\">" << htmlEscape(instruction)
        << "</p><small class=\"drone-mission-reward\">" << htmlEscape(objective.rewardPreview)
        << "</small></section>";
    return out.str();
}

std::string scenarioObjectiveModal(const ScenarioObjectivePresentation& objective)
{
    if (!objective.available || objective.state == ScenarioStepState::Complete) {
        return {};
    }

    const bool mandatoryBriefing = objective.mandatoryBriefing && !objective.briefingAcknowledged;
    const bool readyToClaim = objective.state == ScenarioStepState::ReadyToClaim;
    const bool firstFailure = objective.firstFailurePending;
    if (!mandatoryBriefing && !readyToClaim && !firstFailure) {
        return {};
    }

    const std::string modalId = "scenario_" + objective.scenarioId + "_" + objective.stepId;
    const std::string title = firstFailure
        ? "OBJECTIVE RETRY REQUIRED"
        : (readyToClaim ? objective.title + " READY" : objective.title);
    const std::string setup = firstFailure
        ? objective.failureExplanation
        : objective.detail;
    const std::string kicker = firstFailure
        ? "OBJECTIVE // LOCKED"
        : (readyToClaim ? "READY TO CLAIM" : "MANDATORY DIRECTIVE");
    const std::string actionClass = readyToClaim ? "ok" : (firstFailure ? "warn" : "ok");
    ScenarioObjectivePresentation actionObjective = objective;
    if (firstFailure) {
        // The action is AcknowledgeFailure, not a navigation action. Keep the
        // label honest so semantic buttons never imply a different outcome.
        actionObjective.actionLabel = "Acknowledge";
    }
    std::ostringstream body;
    body << "<section class=\"activity-introduction modal-body campaign-briefing\" data-scenario-id=\""
        << htmlEscape(objective.scenarioId) << "\" data-scenario-step-id=\""
        << htmlEscape(objective.stepId) << "\">"
        << "<span class=\"activity-introduction-kicker\">" << htmlEscape(kicker) << "</span>"
        << "<p class=\"activity-introduction-setup\">" << htmlEscape(setup) << "</p>"
        << "<div class=\"activity-introduction-payoff\"><span>Reward preview</span><strong>"
        << htmlEscape(objective.rewardPreview) << "</strong></div>"
        << "<div class=\"modal-actions action-row rr-action-footer activity-introduction-actions\">"
        << scenarioActionButton(actionObjective, actionClass, true) << "</div></section>";
    // Campaign gates always require an explicit semantic action. In
    // particular, Escape/Back must not skip a mandatory briefing or claim.
    return autoModalTemplate(modalId, title, body.str(), false);
}

ScenarioObjectivePresentation scenarioObjectiveForSurface(
    const GameState& state,
    const ContentCatalog& catalog)
{
    const SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    return expedition.active
        ? scenarioObjectiveForDestination(state, catalog, expedition.destinationId)
        : scenarioObjectiveForDestination(state, catalog, currentDestination(state, catalog).id);
}

std::string scenarioObjectiveModalForDestination(
    const GameState& state,
    const ContentCatalog& catalog,
    std::string_view destinationId)
{
    const ScenarioObjectivePresentation objective =
        scenarioObjectiveForDestination(state, catalog, destinationId);
    if (objective.scenarioId == content::scenario::marsBayExpansion &&
        objective.stepId == "funding") {
        // The Jupiter Window owns a three-action, non-exclusive briefing.
        // The generic one-action scenario modal would falsely imply a single
        // prescribed path through Refit.
        return {};
    }
    return scenarioObjectiveModal(objective);
}

int scenarioCommonAboard(const GameState& state, std::string_view destinationId)
{
    const SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    if (!expedition.active
        || expedition.destinationId != destinationId
        || !expedition.bankedMiningArenaValid
        || !expedition.bankedMiningProgressionEligible) {
        return 0;
    }
    return std::max(
        0,
        std::min(
            expedition.bankedMiningMaterials.common,
            expedition.temporaryMaterials.common));
}

// The live objective renderer is content-driven. Keep the former
// CampaignObjectiveId implementation out of the build while migration tests
// are being converted; it is not an alternate runtime path.
#if 0
struct CampaignObjectivePresentation {
    CampaignObjectiveId id = CampaignObjectiveId::LunarProspector;
    CampaignObjectiveState state = CampaignObjectiveState::Locked;
    std::string location;
    std::string title;
    std::string detail;
    std::string reward;
    int current = 0;
    int required = 0;
    PanelButtonPresentation action;
};

int campaignCommonAboard(const GameState& state, std::string_view destinationId)
{
    const SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
    if (!expedition.active
        || expedition.destinationId != destinationId
        || !expedition.bankedMiningArenaValid
        || !expedition.bankedMiningProgressionEligible) {
        return 0;
    }
    return std::max(
        0,
        std::min(
            expedition.bankedMiningMaterials.common,
            expedition.temporaryMaterials.common));
}

int moonCampaignCommonAboard(const GameState& state)
{
    return state.meta.lunarProspectorClaimed
        ? 0
        : campaignCommonAboard(state, content::destination::moon);
}

int marsCampaignCommonAboard(const GameState& state)
{
    return state.meta.marsBayExpansionClaimed
        ? 0
        : campaignCommonAboard(state, content::destination::mars);
}

std::string campaignObjectiveStateLabel(CampaignObjectiveState state)
{
    switch (state) {
    case CampaignObjectiveState::Active:
        return "ACTIVE";
    case CampaignObjectiveState::ReadyToClaim:
        return "READY TO CLAIM";
    case CampaignObjectiveState::Complete:
        return "COMPLETE";
    case CampaignObjectiveState::Locked:
    default:
        return "LOCKED";
    }
}

std::string campaignObjectiveStateClass(CampaignObjectiveState state)
{
    switch (state) {
    case CampaignObjectiveState::Active:
        return "state-active active";
    case CampaignObjectiveState::ReadyToClaim:
        return "state-ready ready-to-claim";
    case CampaignObjectiveState::Complete:
        return "state-complete complete";
    case CampaignObjectiveState::Locked:
    default:
        return "state-locked locked";
    }
}

CampaignObjectivePresentation campaignObjectivePresentation(
    const GameState& state,
    CampaignObjectiveId objective)
{
    const CampaignObjectiveStatus status = campaignObjectiveStatus(state, objective);
    CampaignObjectivePresentation presentation;
    presentation.id = objective;
    presentation.state = status.state;
    presentation.current = status.current;
    presentation.required = status.required;

    switch (objective) {
    case CampaignObjectiveId::LunarProspector:
        presentation.location = "MOON";
        presentation.title = "Lunar Prospector Contract";
        if (status.state == CampaignObjectiveState::Complete) {
            presentation.detail = "PROSPECTOR MK I INSTALLED // SLOT 1 ONLINE.";
        } else if (status.state == CampaignObjectiveState::ReadyToClaim) {
            presentation.detail = std::to_string(status.required) + "/"
                + std::to_string(status.required)
                + " DELIVERED // INSTALL PROSPECTOR MK I.";
        } else {
            const int commonAboard = moonCampaignCommonAboard(state);
            presentation.detail = commonAboard > 0
                ? std::to_string(commonAboard)
                    + " COMMON ABOARD // RETURN TO EARTH, THEN EXTRACT SAFELY."
                : "MINE " + std::to_string(status.required)
                    + " GRAY COMMON, RETURN TO THE SHUTTLE, THEN EXTRACT SAFELY. "
                      "PLAIN REGOLITH YIELDS NOTHING.";
        }
        presentation.reward = "REWARD // PROSPECTOR MK I + SLOT 1";
        if (status.state == CampaignObjectiveState::ReadyToClaim) {
            presentation.action = panelActionButton(
                "Install Prospector Mk I",
                ui::actions::claimLunarProspector,
                "ok");
        }
        break;
    case CampaignObjectiveId::MarsBayExpansion:
        presentation.location = "MARS";
        presentation.title = "Bay Expansion";
        if (status.state == CampaignObjectiveState::Complete) {
            presentation.detail =
                "SLOT 2 OPEN // NO SECOND SUPPORT DRONE REQUIRED. THE IO HAZARD DRONE CAN USE IT LATER.";
        } else if (status.state == CampaignObjectiveState::ReadyToClaim) {
            presentation.detail = std::to_string(status.required) + "/"
                + std::to_string(status.required)
                + " DELIVERED // FABRICATE THE EMPTY SLOT. NO SECOND SUPPORT DRONE IS REQUIRED.";
        } else {
            const int commonAboard = marsCampaignCommonAboard(state);
            presentation.detail = commonAboard > 0
                ? std::to_string(commonAboard)
                    + " COMMON ABOARD // RETURN TO SURFACE OPS, THEN EXTRACT SAFELY. "
                      "NO SECOND SUPPORT DRONE REQUIRED."
                : "MINE " + std::to_string(status.required)
                    + " GRAY COMMON, RETURN TO THE SHUTTLE, THEN EXTRACT SAFELY. "
                      "NO SECOND SUPPORT DRONE REQUIRED.";
        }
        presentation.reward = "REWARD // EMPTY SUPPORT DRONE SLOT 2";
        if (status.state == CampaignObjectiveState::ReadyToClaim) {
            presentation.action = panelActionButton(
                "Fabricate Slot 2",
                ui::actions::claimMarsBayExpansion,
                "ok");
        }
        break;
    case CampaignObjectiveId::IoVolcanicDescent: {
        const bool hazardEquipped = std::find(
            state.meta.equippedDroneIds.begin(),
            state.meta.equippedDroneIds.end(),
            content::drone::hazardDrone) != state.meta.equippedDroneIds.end();
        presentation.location = "IO // JUPITER SYSTEM";
        presentation.title = "Volcanic Artifact Recovery";
        presentation.detail = !state.meta.ioHazardDroneCommissioned
            ? "Commission the Hazard Drone. Io ore exists only inside lava seams."
            : (!hazardEquipped
                  ? "ASSIGN HAZARD DRONE: free one slot, then equip it for the Io descent."
                  : "Cool and drill both lava seals, tow the artifact, then extract safely.");
        presentation.reward = "REWARD // 75 EXPEDITION XP + OUTER TRANSFER DATA";
        if (!state.meta.ioHazardDroneCommissioned) {
            presentation.action = panelActionButton(
                "Commission Hazard Drone",
                ui::actions::commissionIoHazardDrone,
                "warn");
        }
        break;
    }
    case CampaignObjectiveId::SaturnSlingshot:
        presentation.location = "JUPITER DEPARTURE";
        presentation.title = "Perfect Slingshot to Saturn";
        presentation.detail = state.meta.saturnSlingshotPerfect
            ? "Perfect corridor recorded. Lock the one-way outer course."
            : "Hold the gold corridor through the finish. Good is not enough.";
        presentation.reward = "REWARD // SATURN ROUTE";
        if (status.state == CampaignObjectiveState::Active && state.meta.ioArtifactRecovered) {
            presentation.action = panelActionButton(
                "Begin Slingshot Run",
                ui::actions::beginSaturnSlingshot,
                "warn");
        } else if (status.state == CampaignObjectiveState::ReadyToClaim) {
            presentation.action = panelActionButton(
                "Lock Saturn Course",
                ui::actions::claimSaturnCourse,
                "ok");
        }
        break;
    }
    return presentation;
}

std::string campaignObjectiveMarkup(
    const CampaignObjectivePresentation& objective,
    bool showAction = true,
    bool usePhaseLane = true)
{
    std::ostringstream out;
    out << "<section class=\"objective-strip rr-objective-strip campaign-objective "
        << (usePhaseLane ? "phase-lane " : "")
        << campaignObjectiveStateClass(objective.state)
        << "\" data-campaign-objective=\"" << static_cast<int>(objective.id)
        << "\" data-objective-state=\"" << campaignObjectiveStateLabel(objective.state) << "\">"
        << "<div class=\"campaign-objective-head\"><span>" << htmlEscape(objective.location)
        << "</span><em>" << htmlEscape(campaignObjectiveStateLabel(objective.state)) << "</em></div>"
        << "<strong>" << htmlEscape(objective.title) << "</strong>";
    if (objective.required > 0) {
        out << "<div class=\"campaign-progress\" aria-label=\""
            << htmlEscape(std::to_string(objective.current) + " of " + std::to_string(objective.required))
            << "\">";
        for (int index = 0; index < objective.required; ++index) {
            out << "<i class=\"" << (index < objective.current ? "is-filled" : "") << "\"></i>";
        }
        out << "<b>" << std::clamp(objective.current, 0, objective.required) << "/"
            << objective.required << "</b></div>";
    }
    out << "<p>" << htmlEscape(objective.detail) << "</p>"
        << "<div class=\"campaign-objective-foot\"><small>" << htmlEscape(objective.reward) << "</small>";
    if (showAction && !objective.action.actionId.empty()) {
        out << panelButton(objective.action);
    }
    out << "</div></section>";
    return out.str();
}

CampaignObjectiveId objectiveForFrontier(const GameState& state, const ContentCatalog& catalog)
{
    const std::string& destinationId = currentDestination(state, catalog).id;
    if (destinationId == content::destination::mars) {
        return CampaignObjectiveId::MarsBayExpansion;
    }
    if (destinationId == content::destination::jupiter) {
        return state.meta.ioArtifactRecovered
            ? CampaignObjectiveId::SaturnSlingshot
            : CampaignObjectiveId::IoVolcanicDescent;
    }
    return CampaignObjectiveId::LunarProspector;
}
#endif

std::string scenarioProgressTargetLabel(const ScenarioObjectivePresentation& presentation)
{
    if (presentation.completionEvent != ScenarioEventKind::SafeMaterialDelivered ||
        presentation.eventTargetId.empty()) {
        return {};
    }
    std::string label;
    label.reserve(presentation.eventTargetId.size());
    bool previousWasSpace = false;
    for (const unsigned char character : presentation.eventTargetId) {
        if (character == '_' || character == '-') {
            if (!label.empty() && !previousWasSpace) {
                label.push_back(' ');
                previousWasSpace = true;
            }
            continue;
        }
        label.push_back(static_cast<char>(std::toupper(character)));
        previousWasSpace = false;
    }
    return label;
}

std::string miningCocoonLayerLabel(const MiningGateRuntime& gate, std::size_t layerIndex)
{
    if (layerIndex >= gate.cocoonLayers.size()) {
        return {};
    }
    const MiningCocoonLayerProgress& layer = gate.cocoonLayers[layerIndex];
    if (layer.revealed) {
        return layer.label;
    }
    if (layerIndex == 0U) {
        return layer.label + " — FIND";
    }
    return layer.label + " — LOCKED UNTIL " +
        gate.cocoonLayers[layerIndex - 1U].label + " CLEAR";
}

std::string miningCocoonLayerValue(const MiningGateRuntime& gate, std::size_t layerIndex)
{
    if (layerIndex >= gate.cocoonLayers.size()) {
        return {};
    }
    const MiningCocoonLayerProgress& layer = gate.cocoonLayers[layerIndex];
    if (layer.revealed) {
        const int cleared = std::clamp(layer.total - layer.remaining, 0, std::max(0, layer.total));
        return std::to_string(cleared) + "/" + std::to_string(std::max(0, layer.total));
    }
    if (layerIndex == 0U) {
        return "FIND";
    }
    return gate.cocoonLayers[layerIndex - 1U].label + " FIRST";
}

std::string compactMiningScenarioObjective(const GameState& state, const ContentCatalog& catalog)
{
    const ScenarioObjectivePresentation presentation = scenarioObjectiveForMining(state, catalog);
    const MiningGateRuntime& gate = state.run.mining.gate;
    if (!gate.cocoonLayers.empty()) {
        std::ostringstream out;
        out << (presentation.location.empty() ? "RECOVERY SITE" : presentation.location);
        for (std::size_t layerIndex = 0; layerIndex < gate.cocoonLayers.size(); ++layerIndex) {
            const MiningCocoonLayerProgress& layer = gate.cocoonLayers[layerIndex];
            if (!layer.revealed) {
                out << " // " << miningCocoonLayerLabel(gate, layerIndex);
                break;
            }
            const int cleared = std::max(0, layer.total - layer.remaining);
            out << " // " << layer.label << " " << cleared << "/" << std::max(0, layer.total);
            if (!layer.completed) {
                break;
            }
        }
        const MiningArtifactObject& artifact = state.run.mining.artifact;
        const bool rigTethered = state.run.mining.operatorRigTethered;
        const std::string objectiveState = artifact.revealed
            ? (artifact.state == MiningArtifactState::Delivered
                   ? "EXTRACT SAFELY"
                   : (artifact.tethered ? "TOW TO SHUTTLE" : "OBJECTIVE EXPOSED"))
            : (rigTethered ? "TOW MINING RIG TO SHUTTLE" : "CLEAR ACTIVE LAYER");
        out << " // " << objectiveState;
        return out.str();
    }
    if (!presentation.available) {
        return "MINING";
    }
    const std::string targetLabel = scenarioProgressTargetLabel(presentation);
    const std::string locationAndTarget = presentation.location +
        (targetLabel.empty() ? std::string {} : " " + targetLabel);
    return locationAndTarget + " // DELIVERED " +
        std::to_string(presentation.current) + "/" + std::to_string(presentation.required);
}

std::string miningCocoonObjectiveState(const MiningRunState& mining)
{
    const MiningArtifactObject& artifact = mining.artifact;
    const bool rigTethered = mining.operatorRigTethered;
    if (artifact.revealed) {
        if (artifact.state == MiningArtifactState::Delivered) {
            return "EXTRACT SAFELY";
        }
        return artifact.tethered ? "TOW TO SHUTTLE" : "OBJECTIVE EXPOSED";
    }
    if (rigTethered) {
        return "TOW MINING RIG TO SHUTTLE";
    }
    for (const MiningCocoonLayerProgress& layer : mining.gate.cocoonLayers) {
        if (!layer.revealed) {
            return "FIND " + layer.label;
        }
        if (!layer.completed) {
            return "CLEAR " + layer.label;
        }
    }
    return "LOCATE PROTECTED OBJECTIVE";
}

struct MiningCocoonHudLayout {
    int routeTop = 60;
};

// The cocoon renderer accepts an authored list of layers, not a fixed
// outer/inner pair. Keep the route marker below however many rows the compact
// layer display needs so additional scenario layers never overlap it.
MiningCocoonHudLayout miningCocoonHudLayout(const MiningGateRuntime& gate)
{
    constexpr int layerFootprint = 112;
    constexpr int objectiveFootprint = 132;
    constexpr int availableWidth = 364;
    constexpr int rowHeight = 34;
    constexpr int verticalPadding = 12;

    const int contentWidth = static_cast<int>(gate.cocoonLayers.size()) * layerFootprint
        + objectiveFootprint;
    const int rows = std::max(1, (contentWidth + availableWidth - 1) / availableWidth);
    const int displayHeight = verticalPadding + rows * rowHeight;
    return {.routeTop = std::max(60, displayHeight + 12)};
}

std::string flybyZoneLabel(int zone)
{
    if (zone >= 2) {
        return "PERFECT";
    }
    if (zone == 1) {
        return "GOOD";
    }
    return "MISS";
}

std::string flybyGradeLabel(FlybyGrade grade)
{
    switch (grade) {
    case FlybyGrade::Perfect:
        return "PERFECT SLINGSHOT";
    case FlybyGrade::Good:
        return "CLEAN FLYBY";
    case FlybyGrade::Miss:
        return "MISSED WINDOW";
    case FlybyGrade::Active:
    default:
        return "FLYBY ACTIVE";
    }
}

std::string orbitZoneLabel(int zone)
{
    if (zone >= 2) {
        return "PERFECT";
    }
    if (zone == 1) {
        return "GOOD";
    }
    return "MISS";
}

std::string orbitGradeLabel(OrbitGrade grade)
{
    switch (grade) {
    case OrbitGrade::Perfect:
        return "PERFECT ORBIT";
    case OrbitGrade::Good:
        return "STABLE ORBIT";
    case OrbitGrade::Miss:
        return "MISSED ORBIT";
    case OrbitGrade::Active:
    default:
        return "ORBIT ACTIVE";
    }
}

std::string orbitResultBody(OrbitGrade grade)
{
    switch (grade) {
    case OrbitGrade::Perfect:
        return "Deliberate trim held the Perfect band. Orbit is captured; Pass Through is closed.";
    case OrbitGrade::Good:
        return "The stable capture solution held. Orbit is captured; Pass Through is closed.";
    case OrbitGrade::Miss:
        return "The capture did not complete. No Research Data was validated and the approach remains uncommitted.";
    case OrbitGrade::Active:
    default:
        return "Complete one full loop before the insertion timer expires.";
    }
}

std::string flybyResultBody(FlybyGrade grade)
{
    switch (grade) {
    case FlybyGrade::Perfect:
        return "Planet skipped. Research Data and a next-launch fuel and speed solution were secured.";
    case FlybyGrade::Good:
        return "Planet skipped. Research Data was secured and this visit is over.";
    case FlybyGrade::Miss:
        return "No Research Data was validated. The approach remains uncommitted.";
    case FlybyGrade::Active:
    default:
        return "Hold the corridor until the timer expires.";
    }
}

std::string researchDataMilestoneLabel(int progress)
{
    for (const tuning::unlocks::BlueprintUnlock& milestone : tuning::unlocks::blueprintUnlocks) {
        if (progress < milestone.threshold) {
            std::string family = unlockDisplayName(milestone.key);
            if (family == "Thermal systems") family = "THERMAL";
            else if (family == "Recovery hardware") family = "RECOVERY";
            else if (family == "Deep-space modules") family = "DEEP-SPACE";
            else if (family == "Predictive guidance") family = "PREDICTIVE";
            else if (family == "Exotic prototypes") family = "EXOTIC";
            return std::to_string(std::max(0, progress)) + "/" + std::to_string(milestone.threshold)
                + " • " + family;
        }
    }
    const auto& finalMilestone = tuning::unlocks::blueprintUnlocks[std::size(tuning::unlocks::blueprintUnlocks) - 1];
    return std::to_string(std::max(0, progress)) + " / " + std::to_string(finalMilestone.threshold)
        + " — ALL RESEARCH FAMILIES AVAILABLE";
}

struct ArrivalResearchUnlockPresentation {
    std::string label;
    std::string value;
};

ArrivalResearchUnlockPresentation arrivalResearchUnlockPresentation(int progress)
{
    for (const tuning::unlocks::BlueprintUnlock& milestone : tuning::unlocks::blueprintUnlocks) {
        if (progress >= milestone.threshold) {
            continue;
        }
        std::string family = unlockDisplayName(milestone.key);
        if (family == "Thermal systems") family = "THERMAL";
        else if (family == "Recovery hardware") family = "RECOVERY";
        else if (family == "Deep-space modules") family = "DEEP SPACE";
        else if (family == "Predictive guidance") family = "GUIDANCE";
        else if (family == "Exotic prototypes") family = "EXOTIC";
        const int remaining = std::max(0, milestone.threshold - progress);
        return {family, "IN " + std::to_string(remaining) + " RD"};
    }
    return {"Research", "ALL OPEN"};
}

const tuning::unlocks::BlueprintUnlock* pendingResearchBreakthrough(const GameState& state)
{
    for (const tuning::unlocks::BlueprintUnlock& milestone : tuning::unlocks::blueprintUnlocks) {
        const std::string acknowledgment = std::string(ui::actions::acknowledgeResearchBreakthroughPrefix) + std::string(milestone.key);
        if (state.meta.blueprintProgress >= milestone.threshold
            && hasUnlock(state.meta, milestone.key)
            && !ui::briefings::acknowledged(state.meta.acknowledgedActivityBriefingIds, acknowledgment)) {
            return &milestone;
        }
    }
    return nullptr;
}

double flybySlingshotScale(const FlybyRunState& flyby)
{
    const double speed = std::hypot(flyby.velocityX, flyby.velocityY);
    const double baselineSpeed = std::hypot(tuning::flyby::startVelocityX, tuning::flyby::startVelocityY);
    const double range = std::max(0.001, tuning::flyby::maxSpeed - baselineSpeed);
    const double fastShare = std::clamp((speed - baselineSpeed) / range, 0.0, 1.0);
    return 1.0 + fastShare * (tuning::flyby::slingshotMaxSpeedScale - 1.0);
}

double flybySlingshotSpeedBoost(
    const FlybyRunState& flyby,
    double maximumBaseBoost)
{
    const double speed = std::hypot(flyby.velocityX, flyby.velocityY);
    const double range = std::max(
        0.001,
        tuning::flyby::maxSpeed - tuning::flyby::minSpeed);
    const double speedShare = std::clamp(
        (speed - tuning::flyby::minSpeed) / range,
        0.0,
        1.0);
    return std::max(0.0, maximumBaseBoost) *
        tuning::flyby::slingshotMaxSpeedScale * speedShare;
}

std::string flybySpeedLabel(const FlybyRunState& flyby)
{
    const double speed = std::hypot(flyby.velocityX, flyby.velocityY);
    return display::fixed(speed * 100.0, 0) + " m/s";
}

std::string rarityCardClass(std::string_view rarity)
{
    if (rarity == text::enums::rarity::common) {
        return "rarity-common";
    }
    if (rarity == text::enums::rarity::uncommon) {
        return "rarity-uncommon";
    }
    if (rarity == text::enums::rarity::rare) {
        return "rarity-rare";
    }
    if (rarity == text::enums::rarity::prototype) {
        return "rarity-prototype";
    }
    return "rarity-common";
}

std::string refitOfferCard(const RefitOfferPresentation& offer, bool defaultFocus)
{
    const RefitPresentation& presentation = offer.card;
    std::ostringstream out;
    if (offer.kind == RefitOfferPresentationKind::LaunchUpgrade) {
        out << "<article class=\"pilot-card upgrade-draft-card compact-draft-selector refit-offer-card launch-upgrade-row slot-"
            << htmlEscape(presentation.slotClass) << "\">";
        out << "<div class=\"pilot-card-top\"><span>LAUNCH UPGRADE</span><strong>"
            << htmlEscape(offer.footerCostSummary) << "</strong></div>";
        out << "<h3 class=\"card-title\">" << htmlEscape(presentation.title) << "</h3>";
        out << "<p class=\"card-copy refit-offer-detail\">" << htmlEscape(presentation.detail) << "</p>";
        out << "<div class=\"draft-card-footer action-row\"><strong class=\"module-impact\">"
            << htmlEscape(presentation.primaryImpact) << "</strong>"
            << panelButton(offer.action, defaultFocus) << "</div></article>";
        return out.str();
    }
    out << "<article class=\"pilot-card upgrade-draft-card compact-draft-selector refit-offer-card refit-choice-card slot-"
        << htmlEscape(presentation.slotClass) << " " << rarityCardClass(presentation.rarity) << "\">";
    out << "<div class=\"pilot-card-top\"><span>" << htmlEscape(presentation.category) << "</span><strong>"
        << htmlEscape(presentation.rarity) << " permanent</strong></div>";
    out << "<h3 class=\"card-title\">" << htmlEscape(presentation.title) << "</h3>";
    out << "<p class=\"card-copy refit-offer-detail\">" << htmlEscape(presentation.detail) << "</p>";
    out << "<strong class=\"module-impact\">" << htmlEscape(presentation.primaryImpact) << "</strong>";
    out << "<div class=\"stat-grid chip-strip\">";
    for (const RefitStatChip& chip : presentation.statChips) {
        out << statChip(chip);
    }
    out << "</div>";
    out << "<div class=\"draft-card-footer action-row\"><span class=\"refit-offer-cost"
        << (offer.affordable ? "" : " unaffordable") << "\">"
        << htmlEscape(offer.footerCostSummary) << "</span>"
        << panelButton(offer.action, defaultFocus) << "</div></article>";
    return out.str();
}

std::string researchProjectCard(const ResearchProjectCardPresentation& project)
{
    std::ostringstream out;
    out << "<article class=\"ops-card rr-fixed-lane-card ui-choice-row management-choice-row\">";
    out << "<div class=\"card-topline\"><span>" << htmlEscape(project.rarity) << "</span><span>"
        << htmlEscape(project.blueprintGain) << "</span></div>";
    out << "<h3 class=\"card-title\">" << htmlEscape(project.title) << "</h3>";
    out << "<p class=\"card-copy\">" << htmlEscape(project.detail) << "</p>";
    if (!project.reward.empty()) {
        out << "<strong class=\"module-impact\">" << htmlEscape(project.reward) << "</strong>";
    }
    out << "<div class=\"card-footer action-row\"><span>" << htmlEscape(project.materialCost) << "</span>"
        << panelButton(project.action) << "</div></article>";
    return out.str();
}

std::string surfaceActionCard(
    const SurfaceActionPreviewPresentation& action,
    std::string_view introductionModal = {})
{
    std::ostringstream out;
    const bool isMining = isSurfaceMiningAction(action);
    const bool featured = isSurfaceExtractionAction(action)
            && (action.title == "Extract Mars Payload" || action.title.rfind("Deliver ", 0) == 0);
    out << "<article class=\"resource-bank rr-fixed-lane-card surface-choice-row"
        << (featured ? " featured-action" : "")
        << (isMining && action.action.enabled ? " risk-action" : "") << "\">";
    out << "<div class=\"surface-choice-summary\"><h3 class=\"card-title\">" << htmlEscape(action.title) << "</h3>";
    out << "<div class=\"card-topline surface-choice-cues\"><span class=\"surface-choice-cost\">" << htmlEscape(action.cost)
        << "</span><span class=\"surface-choice-outcome\">" << htmlEscape(surfaceActionRiskRewardCue(action)) << "</span></div></div>";
    const PanelButtonPresentation footerButton = surfaceActionFooterButton(action);
    out << introductoryPanelButton(footerButton, introductionModal) << "</article>";
    return out.str();
}

std::string surfaceUpgradeCard(const SurfaceUpgradeCardPresentation& upgrade, bool defaultFocus)
{
    std::ostringstream out;
    out << "<article id=\"rr-run-upgrade-card-" << upgrade.index
        << "\" class=\"pilot-card upgrade-draft-card compact-draft-selector surface-upgrade-card "
        << rarityCardClass(upgrade.rarity) << " offer-index-" << upgrade.index << "\">";
    out << "<i id=\"rr-run-upgrade-resolve-" << upgrade.index
        << "\" class=\"run-upgrade-resolve-flash\"></i>";
    out << "<div class=\"pilot-card-top\"><span>" << htmlEscape(upgrade.category) << "</span><strong>"
        << htmlEscape(upgrade.rarity) << "</strong></div>";
    out << "<h3 class=\"card-title\">" << htmlEscape(upgrade.title) << "</h3>";
    out << "<p class=\"card-copy surface-upgrade-detail\">" << htmlEscape(upgrade.detail) << "</p>";
    out << "<div class=\"stat-grid chip-strip\">";
    for (std::size_t index = 0; index < upgrade.effectChips.size() && index < 2; ++index) {
        out << resourceChip(upgrade.effectChips[index]);
    }
    out << "</div>";
    out << "<div class=\"draft-card-footer action-row\"><span>TRANSPORT EXPEDITION</span>"
        << panelButton(upgrade.action, defaultFocus) << "</div></article>";
    return out.str();
}

std::string miniDroneControlCard(const MiniDroneCardPresentation& drone)
{
    std::ostringstream out;
    out << "<article class=\"drone-control-card " << rarityCardClass(drone.rarity) << "\">";
    out << "<div class=\"drone-card-head\"><span class=\"drone-role-mark\">"
        << htmlEscape(drone.role.empty() ? "D" : std::string(1, drone.role.front())) << "</span>"
        << "<div class=\"drone-card-id\"><div class=\"card-topline\"><span>" << htmlEscape(drone.role)
        << "</span><span>" << htmlEscape(drone.rarity) << "</span></div>"
        << "<h3 class=\"card-title\">" << htmlEscape(drone.title) << "</h3></div></div>";
    out << "<p class=\"card-copy drone-control-status\">" << htmlEscape(drone.status) << "</p>";
    out << "<p class=\"card-copy drone-card-summary\">" << htmlEscape(drone.detail) << "</p>";
    out << "<div class=\"card-footer action-row\">"
        << modalButton("Details", droneDetailsModalId(drone.index), "ghost")
        << panelButton(drone.action) << "</div></article>";
    return out.str();
}

std::string droneDetailsModalBody(const MiniDroneCardPresentation& drone)
{
    std::ostringstream out;
    out << "<section class=\"drone-details-modal modal-body\">"
        << "<header class=\"drone-details-summary\"><span class=\"ui-kicker\">"
        << htmlEscape(drone.role) << " // " << htmlEscape(drone.rarity) << " FRAME</span>"
        << "<h3>" << htmlEscape(drone.title) << "</h3><p class=\"drone-details-status\">"
        << htmlEscape(drone.status) << "</p></header>"
        << "<section class=\"drone-detail-section\"><h3>Operational profile</h3><p>"
        << htmlEscape(drone.detail) << "</p></section>"
        << "<section class=\"drone-detail-section\"><h3>Capabilities</h3>"
        << "<div class=\"stat-grid chip-strip\">" << resourceChipGrid(drone.effectChips) << "</div></section>"
        << "<section class=\"drone-detail-section\"><h3>Expedition progression</h3><p class=\"drone-details-upgrade\">"
        << htmlEscape(drone.upgradeSummary) << "</p></section>"
        << "<section class=\"drone-detail-section\"><h3>Build contribution</h3><p>"
        << htmlEscape(drone.buildHook) << "</p></section>"
        << "<div class=\"modal-actions action-row drone-details-actions\">"
        << panelButton(drone.action);
    out << "</div></section>";
    return out.str();
}

std::string droneSynergyModalBody(const DroneOpsPresentation& presentation)
{
    std::ostringstream out;
    out << "<section class=\"drone-synergy-modal modal-body\">"
        << "<header class=\"drone-synergy-summary\"><span class=\"ui-kicker\">CURRENT BUILD</span>"
        << "<h3>" << htmlEscape(presentation.buildTitle) << "</h3>"
        << "<p>" << htmlEscape(presentation.buildDetail) << "</p>"
        << "<div class=\"stat-grid chip-strip\">" << resourceChipGrid(presentation.buildChips) << "</div></header>"
        << "<div class=\"drone-synergy-list\">";
    for (const DroneBuildRecipePresentation& recipe : presentation.buildRecipes) {
        out << "<article class=\"drone-synergy-row" << (recipe.active ? " active" : "")
            << (recipe.signature ? " signature" : "") << "\"><div class=\"recipe-topline\"><strong>"
            << htmlEscape(recipe.title) << "</strong><span>" << htmlEscape(recipe.active ? "ACTIVE" : recipe.status)
            << "</span></div><p class=\"drone-synergy-requirements\">"
            << htmlEscape(recipe.requirements) << "</p><p>" << htmlEscape(recipe.detail) << "</p></article>";
    }
    out << "</div></section>";
    return out.str();
}

std::string droneLoadoutSlotCard(const DroneLoadoutSlotPresentation& slot)
{
    std::ostringstream out;
    out << "<article class=\"drone-loadout-slot " << htmlEscape(slot.cssClass)
        << "\" data-drone-slot-index=\"" << std::max(0, slot.slot - 1) << "\">";
    out << "<div class=\"slot-card-head\"><span class=\"slot-number\">" << htmlEscape(std::to_string(slot.slot))
        << "</span>";
    if (!slot.action.label.empty()) {
        out << panelButton(slot.action);
    } else {
        out << "<strong class=\"slot-state\">" << htmlEscape(slot.status) << "</strong>";
    }
    out << "</div>";
    out << "<div class=\"slot-card-content\"><div class=\"slot-card-body\"><h3 class=\"card-title\">"
        << htmlEscape(slot.title) << "</h3>";
    out << "<p class=\"card-copy slot-role\">" << htmlEscape(slot.role) << "</p></div>";
    out << "<div class=\"stat-grid chip-strip\">" << resourceChipGrid(slot.chips) << "</div></div>";
    out << "</article>";
    return out.str();
}

std::vector<PanelMetricPresentation> materialRewardChips(const MaterialInventory& materials, int artifacts, int cargo)
{
    std::vector<PanelMetricPresentation> chips;
    if (materials.common > 0) {
        chips.push_back(panelMetric("Common", "+" + std::to_string(materials.common)));
    }
    if (materials.rare > 0) {
        chips.push_back(panelMetric("Rare", "+" + std::to_string(materials.rare)));
    }
    if (materials.exotic > 0) {
        chips.push_back(panelMetric("Exotic", "+" + std::to_string(materials.exotic)));
    }
    if (artifacts > 0) {
        chips.push_back(panelMetric("Artifact", "+" + std::to_string(artifacts)));
    }
    if (cargo > 0) {
        chips.push_back(panelMetric("Yield est", "+" + std::to_string(cargo)));
    }
    if (chips.empty()) {
        chips.push_back(panelMetric("Prospects", "None"));
    }
    return chips;
}

std::string surfaceMiniGamePanel(
    std::string_view cssClass,
    std::string_view title,
    std::string_view subtitle,
    const std::vector<PanelMetricPresentation>& metrics,
    const std::vector<PanelMetricPresentation>& rewards,
    std::string_view statusTitle,
    std::string_view statusDetail,
    const std::vector<PanelButtonPresentation>& actions)
{
    std::ostringstream out;
    out << "<section class=\"surface-minigame " << htmlEscape(cssClass) << "\">";
    out << "<div class=\"phase-titlebar\"><div><h2>" << htmlEscape(title)
        << "</h2><p>" << htmlEscape(subtitle) << "</p></div>"
        << "<div class=\"utility-row compact-tools utility-actions\">" << modalButton(text::buttons::details, ui::modals::surface, "ghost")
        << "</div></div>";
    out << "<div class=\"minigame-readout\"><div class=\"minigame-metrics\">";
    for (std::size_t index = 0; index < metrics.size(); index += 2) {
        out << "<div class=\"minigame-metric-row\">";
        out << metric(metrics[index].label, metrics[index].value);
        if (index + 1 < metrics.size()) {
            out << metric(metrics[index + 1].label, metrics[index + 1].value);
        }
        out << "</div>";
    }
    out << "</div><div class=\"stat-grid minigame-rewards\">" << resourceChipGrid(rewards) << "</div></div>";
    out << "<article class=\"resource-bank minigame-callout\"><div><h2>" << htmlEscape(statusTitle)
        << "</h2><p>" << htmlEscape(statusDetail) << "</p></div></article>";
    out << "<div class=\"actions action-row minigame-actions\">";
    for (const PanelButtonPresentation& action : actions) {
        out << panelButton(action);
    }
    out << "</div></section>";
    return out.str();
}

std::string arrivalOperationCard(
    std::string_view title,
    std::string_view detail,
    std::string_view risk,
    std::string_view reward,
    const PanelButtonPresentation& action,
    std::string_view introductionModal = {},
    bool defaultFocus = false)
{
    std::ostringstream out;
    out << "<article class=\"ops-card arrival-card rr-fixed-lane-card ui-choice-row decision-choice-row\">";
    out << "<div class=\"card-topline\"><span>" << htmlEscape(risk) << "</span><span>" << htmlEscape(reward) << "</span></div>";
    out << "<h3 class=\"card-title\">" << htmlEscape(title) << "</h3>";
    if (!detail.empty()) {
        out << "<p class=\"card-copy arrival-operation-detail"
            << (action.enabled ? "" : " compact-block-reason") << "\">" << htmlEscape(detail) << "</p>";
    }
    out << "<div class=\"card-footer action-row\"><span class=\"arrival-card-status\">" << htmlEscape(action.enabled ? std::string(text::panel::ready) : std::string(text::buttons::unavailable))
        << "</span>" << introductoryPanelButton(action, introductionModal, defaultFocus) << "</div></article>";
    return out.str();
}

std::string detailRow(std::string_view label, std::string_view value)
{
    return "<div class=\"detail-row\"><span>" + htmlEscape(label) + "</span><strong>" + htmlEscape(value) + "</strong></div>";
}

std::string detailHeader(std::string_view label)
{
    return "<div class=\"detail-section\">" + htmlEscape(label) + "</div>";
}

std::string detailStack(const std::vector<DetailPresentationRow>& rows)
{
    std::string body = "<div class=\"detail-stack rr-detail-stack modal-body\">";
    for (const DetailPresentationRow& row : rows) {
        body += row.heading ? detailHeader(row.label) : detailRow(row.label, row.value);
    }
    body += "</div>";
    return body;
}

std::string missionLog(const std::vector<std::string>& entries)
{
    std::string body = "<div class=\"detail-stack rr-detail-stack modal-body\">";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        body += detailRow(std::to_string(i + 1), entries[i]);
    }
    body += "</div>";
    return body;
}

std::string inventoryItemCard(const InventoryItemPresentation& item)
{
    std::ostringstream out;
    out << "<article class=\"inventory-item " << htmlEscape(item.cssClass) << "\">";
    out << "<div class=\"inventory-art\"><span>" << htmlEscape(item.glyph) << "</span></div>";
    out << "<div class=\"inventory-copy card-copy\"><h3 class=\"card-title\">" << htmlEscape(item.title) << "</h3><p>" << htmlEscape(item.detail) << "</p></div>";
    out << "<strong class=\"inventory-count\">" << htmlEscape(item.count) << "</strong>";
    out << "</article>";
    return out.str();
}

std::string inventorySectionClass(std::string_view title)
{
    if (title == "Current payload") {
        return "payload";
    }
    if (title == "Recovered resources") {
        return "resources";
    }
    if (title == "Artifacts") {
        return "artifacts";
    }
    if (title == "Ship tech") {
        return "modules";
    }
    if (title == "Drone bay") {
        return "drones";
    }
    return "misc";
}

std::string inventorySection(const InventorySectionPresentation& section)
{
    std::ostringstream out;
    out << "<section class=\"inventory-section inventory-section-" << inventorySectionClass(section.title) << "\"><div class=\"inventory-section-head\"><h3>"
        << htmlEscape(section.title) << "</h3><p>" << htmlEscape(section.detail) << "</p></div>";
    out << "<div class=\"inventory-grid\">";
    for (const InventoryItemPresentation& item : section.items) {
        out << inventoryItemCard(item);
    }
    out << "</div></section>";
    return out.str();
}

bool shouldShowInventorySection(const InventorySectionPresentation& section)
{
    if (section.title == "Ship tech") {
        return false;
    }
    if (section.title != "Artifacts") {
        return true;
    }
    return std::any_of(section.items.begin(), section.items.end(), [](const InventoryItemPresentation& item) {
        return item.count != "0" && !item.count.empty();
    });
}

std::string inventoryBody(const InventoryPresentation& inventory)
{
    std::ostringstream out;
    const bool hasSideColumn = !inventory.sideSections.empty();
    out << "<div class=\"inventory-modal " << (hasSideColumn ? "inventory-with-side" : "inventory-main-only") << "\">";
    out << "<div class=\"inventory-layout " << (hasSideColumn ? "inventory-layout-with-side" : "inventory-layout-main-only") << "\">";
    if (hasSideColumn) {
        out << "<aside class=\"inventory-side-column\">";
        for (const InventorySectionPresentation& section : inventory.sideSections) {
            out << inventorySection(section);
        }
        out << "</aside>";
    }
    out << "<div class=\"inventory-main-column\">";
    out << "<div class=\"metric-grid inventory-summary\">";
    for (const PanelMetricPresentation& metricItem : inventory.summary) {
        out << metric(metricItem.label, metricItem.value);
    }
    out << "</div>";
    for (const InventorySectionPresentation& section : inventory.sections) {
        if (shouldShowInventorySection(section)) {
            out << inventorySection(section);
        }
    }
    out << "</div></div></div>";
    return out.str();
}

std::string inventoryTemplate(const GameState& state, const ContentCatalog& catalog)
{
    return modalTemplate(ui::modals::inventory, "Inventory", inventoryBody(inventoryPresentation(state, catalog)));
}

std::string phaseBoardOpen(
    std::string_view cssClass,
    std::string_view status,
    bool fullPanel = true,
    std::string_view id = {})
{
    (void)status;
    std::string out = "<section";
    if (!id.empty()) {
        out += " id=\"" + htmlEscape(id) + "\"";
    }
    out += " class=\"phase-board " + htmlEscape(cssClass) + "\"";
    if (fullPanel) {
        const bool activeSurfaceMinigame = cssClass.find("phase-board-scan") != std::string_view::npos
            || cssClass.find("phase-board-push") != std::string_view::npos;
        out += activeSurfaceMinigame
            ? " data-panel-mode=\"phase-board\""
            : " data-panel-mode=\"workspace\"";
    }
    out += ">";
    return out;
}

std::string phaseBoardClose()
{
    return "</section>";
}

std::string boardNote(std::string_view note)
{
    return "<p class=\"board-note\">" + htmlEscape(note) + "</p>";
}

std::string debriefPhaseTrack(const std::vector<PhaseStepPresentation>& steps)
{
    std::string out = "<div class=\"debrief-phase-track\">";
    for (const PhaseStepPresentation& step : steps) {
        out += "<div class=\"phase-step-card " + htmlEscape(step.stateClass) + "\"><span>" +
            htmlEscape(step.label) + "</span><strong>" + htmlEscape(step.stateLabel) + "</strong></div>";
    }
    out += "</div>";
    return out;
}

constexpr int kExpeditionXpSegments = 12;
constexpr double kLevelUpDraftFanfareSeconds = 0.70;

int expeditionXpFilledSegments(const SurfaceExpeditionState& expedition)
{
    const double required = std::max(1.0, expeditionExperienceThreshold(expedition.expeditionLevel));
    const double progress = std::clamp(expedition.expeditionExperience / required, 0.0, 1.0);
    return std::clamp(
        static_cast<int>(std::floor(progress * static_cast<double>(kExpeditionXpSegments) + 0.0001)),
        0,
        kExpeditionXpSegments);
}

std::string expeditionXpClass(bool pulse, bool hero = false)
{
    std::string result = "expedition-xp";
    if (pulse) result += " is-pulsing";
    if (hero) result += " is-hero";
    return result;
}

std::string expeditionXpMarkup(
    const SurfaceExpeditionState& expedition,
    std::string_view id,
    bool pulse,
    bool hero = false)
{
    const int required = static_cast<int>(std::ceil(std::max(
        1.0,
        expeditionExperienceThreshold(expedition.expeditionLevel))));
    const int current = std::clamp(
        static_cast<int>(std::floor(expedition.expeditionExperience + 0.0001)),
        0,
        required);
    const int filled = expeditionXpFilledSegments(expedition);
    std::ostringstream out;
    out << "<section id=\"" << id << "\" class=\"" << expeditionXpClass(pulse, hero)
        << "\" aria-label=\"Expedition experience\"><header><strong id=\"" << id
        << "-level\">LV " << std::max(1, expedition.expeditionLevel) << "</strong><span id=\""
        << id << "-value\">" << current << " / " << required << " XP</span><b id=\""
        << id << "-pending\">" << std::max(0, expedition.pendingRunUpgradeChoices)
        << " PICKS</b></header><div class=\"expedition-xp-track\">";
    for (int segment = 0; segment < kExpeditionXpSegments; ++segment) {
        out << "<i id=\"" << id << "-segment-" << segment << "\" class=\"xp-segment"
            << (segment < filled ? " is-filled" : "") << "\"></i>";
    }
    out << "</div></section>";
    return out.str();
}

std::string levelUpDraftClass(const PanelRenderContext& context)
{
    const bool celebrating = context.levelUpBatchChoices > 0
        && context.levelUpFanfareElapsed < kLevelUpDraftFanfareSeconds;
    std::string result = "phase-board phase-board-surface-upgrade phase-board-draft-room level-up-draft";
    if (celebrating) result += " is-celebrating";
    if (context.levelUpActivationLocked) result += " is-activation-locked";
    if (context.levelUpActivationLocked && !celebrating) result += " is-refreshing";
    return result;
}

std::string surfaceQuickbar(const SurfaceExpeditionState& expedition, bool xpPulse)
{
    std::ostringstream out;
    out << "<section class=\"surface-quickbar phase-lane phase-row\">";
    out << surfaceQuickMetric(text::labels::supply, std::to_string(expedition.supply));
    out << surfaceQuickMetric(text::labels::rigFuel, display::fixed(expedition.rigFuel, 1) + "/" + display::fixed(std::max(0.0, expedition.rigFuelCapacity), 1));
    out << surfaceQuickMetric(text::labels::cargo, std::to_string(expedition.cargo));
    out << surfaceQuickMetric("On Ship", std::to_string(expedition.temporaryMaterials.common) + " CM", "", true);
    out << expeditionXpMarkup(expedition, "rr-hud-surface-xp", xpPulse);
    out << "</section>";
    return out.str();
}

std::vector<PanelMetricPresentation> compactHeaderMetrics(
    const GameState& state,
    const ContentCatalog& catalog,
    const PreparedLaunch& activeLaunch)
{
    const Destination& displayDestination = panelDisplayDestination(state, catalog, activeLaunch);
    switch (state.screen) {
    case Screen::Hangar: {
        return {
            panelMetric("Credits", display::money(state.run.credits)),
            panelMetric("Hull", std::string(hullDamageLevel(state.run.shipDamage))),
            panelMetric(
                "Crew",
                activeAstronaut(state) == nullptr
                    ? "NEED PILOT"
                    : std::string(crewStressLevel(activeAstronaut(state)->stress)))
        };
    }
    case Screen::Navigation:
        return {
            panelMetric("Ark", std::string(toString(state.meta.ark.condition))),
            panelMetric(text::labels::arkFuel, std::to_string(state.meta.ark.fuelReserve)),
            panelMetric(text::labels::currentFrontier, displayDestination.name),
            panelMetric(text::labels::hullDamage, display::wholePercent(state.run.shipDamage))
        };
    case Screen::ArrivalOps:
    {
        const ScenarioObjectivePresentation objective =
            scenarioObjectiveForDestination(state, catalog, displayDestination.id);
        const std::string location = objective.available && !objective.location.empty()
            ? objective.location
            : displayDestination.name;
        return {
            panelMetric(
                text::labels::currentFrontier,
                location),
            panelMetric(text::labels::hullDamage, display::wholePercent(state.run.shipDamage)),
            panelMetric(text::labels::crewStress, crewStressSummary(activeAstronaut(state))),
            panelMetric(text::labels::missionCredits, display::money(state.run.credits))
        };
    }
    default:
        // Realtime, minigame, draft, and specialist screens own their context
        // locally. A second global strip only repeats values and consumes the
        // protected scene lane.
        return {};
    }
}

const Destination& hangarLaunchTargetDestination(
    const GameState& state,
    const ContentCatalog& catalog)
{
    if (state.run.routeTransit.active()) {
        if (const Destination* target = catalog.findDestination(state.run.routeTransit.targetDestinationId)) {
            return *target;
        }
    }
    const Destination& current = currentDestination(state, catalog);
    if (current.hiddenFromProgression) {
        if (const Destination* target = catalog.findDestination(state.launchConfig.destinationId)) {
            if (!target->hiddenFromProgression) {
                return *target;
            }
        }
        if (const Destination* next = nextDestination(state, catalog)) {
            return *next;
        }
    }
    return current;
}

std::pair<std::string, std::string> launchLessonHangarObjective(
    const GameState& state,
    const ContentCatalog& catalog)
{
    const Destination& target = hangarLaunchTargetDestination(state, catalog);
    switch (state.meta.launchLessons.stage) {
    case LaunchTrainingStage::FuelCalibration:
        return {"Map the Moon route", "Fly to the low-fuel warning. Turn Around any time before FUEL reaches 0."};
    case LaunchTrainingStage::FlightControlsCalibration:
        return launchUpgradeRank(state, LaunchUpgradeKind::FuelTanks) < 1
            ? std::pair<std::string, std::string>{"Install Fuel Tanks I", "Install the taught fuel upgrade in Refit."}
            : std::pair<std::string, std::string>{"Calibrate Flight Controls", "Lunar landing guidance is uncalibrated. Reach the yellow test line, then return before the Moon."};
    case LaunchTrainingStage::MoonTransfer:
        return launchMissionReady(state)
            ? std::pair<std::string, std::string>{"Reach the Moon", "Fuel and controls are ready for the lunar transfer."}
            : std::pair<std::string, std::string>{"Install Flight Controls I", "Install the taught control upgrade in Refit."};
    case LaunchTrainingStage::ThermalManagement:
        if (!hasUnlock(state.meta, content::unlock::routeMars)) {
            return {"Complete Lunar Prospector", "Finish the Moon contract to reveal the Mars route."};
        }
        if (launchUpgradeRank(state, LaunchUpgradeKind::FuelTanks) < 2) {
            return {"Mars transfer requires 20 transfer fuel", "Current capacity is 15. Mission credits fund permanent Refit upgrades."};
        }
        return {"Reach Mars", "Use Engines Off to manage heat. Reaching Mars completes the flight."};
    case LaunchTrainingStage::MarsTransfer:
        return launchMissionReady(state)
            ? std::pair<std::string, std::string>{"Reach Mars", "Fuel Tanks II is ready. Engine Cooling remains optional."}
            : std::pair<std::string, std::string>{"Mars transfer requires 20 transfer fuel", "Current capacity is 15. Mission credits fund permanent Refit upgrades."};
    case LaunchTrainingStage::HullIntegrity:
        if (!hasUnlock(state.meta, content::unlock::routeJupiter)) {
            return {"Complete Mars Bay Expansion", "Finish the Mars contract to reveal the Jupiter route."};
        }
        if (!jupiterTransferMarginReady(state)) {
            return {
                "Create 5 fuel of Jupiter transfer margin",
                "Install Fuel Tanks III, fly a Good-or-better Mars slingshot, or stack both. Perfect avoids the slingshot's instability penalty."};
        }
        return {
            "Reach Jupiter",
            pendingTransferAssistForDestination(state, content::destination::jupiter) != nullptr
                ? "Mars gravity is carrying the ship outward. Continue through the asteroid gaps."
                : "Fuel Tanks III supplies permanent margin. The optional Mars slingshot still stacks."};
    case LaunchTrainingStage::JupiterTransfer:
        return launchMissionReady(state)
            ? std::pair<std::string, std::string>{
                  "Reach Jupiter",
                  pendingTransferAssistForDestination(state, content::destination::jupiter) != nullptr
                      ? (pendingLaunchInstabilityPenalty(state) > 0.0
                            ? "Good Mars slingshot active: +35% flight instability. Fuel Tanks III remains optional and stacks."
                            : "Perfect Mars slingshot active: stable flight. Fuel Tanks III remains optional and stacks.")
                      : "Fuel Tanks III is ready. A Good-or-better Mars slingshot remains optional and stacks."}
            : std::pair<std::string, std::string>{
                  "Create 5 fuel of Jupiter transfer margin",
                  "Install Fuel Tanks III, fly a Good-or-better Mars slingshot, or stack both."};
    case LaunchTrainingStage::Complete:
        return {"Prepare the next flight", "Current destination: " + target.name};
    }
    return {"Prepare the next flight", target.name};
}

std::string compactHeaderObjective(
    const GameState& state,
    const ContentCatalog& catalog)
{
    switch (state.screen) {
    case Screen::Hangar: {
        if (state.meta.launchLessons.stage != LaunchTrainingStage::Complete) {
            const auto [title, detail] = launchLessonHangarObjective(state, catalog);
            return title + " // " + detail;
        }
        const Destination& current = hangarLaunchTargetDestination(state, catalog);
        const ScenarioObjectivePresentation objective = scenarioObjectiveForDestination(state, catalog, current.id);
        if (objective.available && objective.state != ScenarioStepState::Complete) {
            return scenarioObjectiveStateLabel(objective.state) + " // " + objective.title;
        }
        const FrontierGateStatus gate = frontierGateStatus(state, catalog);
        if (!gate.satisfied && gate.kind == FrontierGateKind::ScenarioRequirement) {
            const ScenarioObjectivePresentation routeObjective = scenarioObjectivePresentation(
                state,
                catalog,
                gate.scenarioId,
                gate.scenarioStepId);
            if (routeObjective.available) {
                return scenarioObjectiveStateLabel(routeObjective.state) + " // " + routeObjective.title;
            }
        }
        return "Prepare the next flight";
    }
    case Screen::Navigation:
        return "Choose the next destination";
    case Screen::ArrivalOps:
        return state.run.arrivalOps.commitment == ApproachCommitment::OrbitCaptured
            ? "ORBIT CAPTURED // LAND OR DEPART"
            : "APPROACH UNCOMMITTED // CHOOSE ONE PATH";
    default:
        return state.statusLine;
    }
}

std::string phaseAdvisory(const PhaseAdvisoryPresentation& advisory)
{
    return "<article class=\"phase-advisory " + htmlEscape(advisory.cssClass) + "\"><strong>" +
        htmlEscape(advisory.title) + "</strong><span>" + htmlEscape(advisory.detail) + "</span></article>";
}

std::string resultMetricGroup(const LaunchOutcomeMetricGroupPresentation& group)
{
    const std::string classAttr = group.cssClass.empty() ? "" : " " + htmlEscape(group.cssClass);
    std::string out = "<article class=\"result-group rr-fixed-lane-card" + classAttr + "\"><h3 class=\"card-title\">" + htmlEscape(group.title) + "</h3>";
    for (const LaunchOutcomeMetricPresentation& metricItem : group.metrics) {
        out += "<div class=\"result-row\"><span>" + htmlEscape(metricItem.label) + "</span><strong>" +
            htmlEscape(metricItem.value) + "</strong></div>";
    }
    out += "</article>";
    return out;
}

std::string achievementCard(const AchievementPresentation& achievement)
{
    return "<article class=\"achievement-card\" data-achievement-id=\"" + htmlEscape(achievement.id) + "\"><span>" +
        htmlEscape(text::panel::sections::achievements) + "</span><strong>" + htmlEscape(achievement.title) +
        "</strong><p>" + htmlEscape(achievement.detail) + "</p></article>";
}

std::string crewFateCard(const CrewFatePresentation& fate)
{
    if (!fate.active) {
        return "";
    }
    return "<article class=\"crew-fate-card " + htmlEscape(fate.cssClass) + "\" data-crew-fate=\"" +
        htmlEscape(fate.cssClass) + "\"><div><span>" + htmlEscape(fate.label) +
        "</span><strong>" + htmlEscape(fate.title) + "</strong><p>" + htmlEscape(fate.detail) +
        "</p></div><div class=\"crew-fate-signal\" aria-hidden=\"true\"><i></i><i></i><i></i></div></article>";
}

enum class MapKnowledge {
    Explored,
    Charted,
    Unknown
};

std::string_view mapKnowledgeClass(MapKnowledge knowledge)
{
    switch (knowledge) {
    case MapKnowledge::Explored:
        return "is-explored";
    case MapKnowledge::Charted:
        return "is-charted";
    case MapKnowledge::Unknown:
    default:
        return "is-unknown";
    }
}

std::string_view mapKnowledgeLabel(MapKnowledge knowledge)
{
    switch (knowledge) {
    case MapKnowledge::Explored:
        return "Explored";
    case MapKnowledge::Charted:
        return "Charted";
    case MapKnowledge::Unknown:
    default:
        return "Unresolved";
    }
}

std::string solarMapNode(
    std::string_view name,
    std::string_view glyph,
    std::string_view objectClass,
    MapKnowledge knowledge)
{
    const bool unknown = knowledge == MapKnowledge::Unknown;
    std::ostringstream out;
    out << "<div class=\"solar-map-node " << htmlEscape(objectClass) << " " << mapKnowledgeClass(knowledge) << "\">"
        << "<div class=\"solar-map-glyph\"><span>" << htmlEscape(unknown ? "?" : glyph) << "</span></div>"
        << "<strong>" << htmlEscape(name) << "</strong>"
        << "<span class=\"solar-map-state\">" << mapKnowledgeLabel(knowledge) << "</span></div>";
    return out.str();
}

ScenarioObjectivePresentation scenarioObjectiveForMap(
    const GameState& state,
    const ContentCatalog& catalog,
    const ScenarioInstance& instance)
{
    const ScenarioDefinition* authored = scenarioDefinitionForRuntimeId(
        state,
        catalog,
        instance.id);
    if (authored == nullptr) {
        return {};
    }
    const ScenarioDefinition definition = resolveScenarioDefinition(*authored, instance);

    ScenarioObjectivePresentation best;
    int bestRank = 100;
    for (const ScenarioStepDefinition& step : definition.steps) {
        ScenarioObjectivePresentation candidate =
            scenarioObjectivePresentation(state, catalog, instance.id, step.id);
        if (!candidate.available) {
            continue;
        }
        const int rank = candidate.state == ScenarioStepState::ReadyToClaim ? 0
            : candidate.state == ScenarioStepState::Active ? 1
            : candidate.state == ScenarioStepState::Locked ? 2
            : 3;
        if (rank < bestRank) {
            best = std::move(candidate);
            bestRank = rank;
        }
    }
    return best;
}

std::string scenarioMapCocoonDetail(
    const GameState& state,
    const ScenarioObjectivePresentation& objective)
{
    const MiningRunState& mining = state.run.mining;
    if (!objective.miningSiteDefinitionId.empty() &&
        mining.active && mining.miningSiteDefinitionId == objective.miningSiteDefinitionId) {
        std::ostringstream out;
        for (const MiningCocoonLayerProgress& layer : mining.gate.cocoonLayers) {
            if (!out.str().empty()) {
                out << " / ";
            }
            if (!layer.revealed) {
                out << "[LOCKED] " << layer.label;
                break;
            }
            out << layer.label << " "
                << std::max(0, layer.total - layer.remaining) << "/" << std::max(0, layer.total);
            if (!layer.completed) {
                break;
            }
        }
        if (mining.operatorRigTethered) {
            out << " / TOW MINING RIG TO SHUTTLE";
        }
        if (mining.artifact.revealed) {
            out << (mining.artifact.tethered ? " / TOW TO SHUTTLE" : " / OBJECTIVE EXPOSED");
        }
        return out.str();
    }
    return {};
}

std::string scenarioCampaignTrack(const GameState& state, const ContentCatalog& catalog)
{
    std::ostringstream out;
    for (const ScenarioInstance& instance : state.meta.scenarios) {
        const ScenarioDefinition* authored = scenarioDefinitionForRuntimeId(
            state,
            catalog,
            instance.id);
        if (authored == nullptr) {
            continue;
        }
        const ScenarioDefinition definition = resolveScenarioDefinition(*authored, instance);
        if (definition.steps.empty()) {
            continue;
        }
        ScenarioObjectivePresentation objective = scenarioObjectiveForMap(state, catalog, instance);
        const ScenarioStepDefinition& firstStep = definition.steps.front();
        const std::string stepId = objective.available && !objective.stepId.empty()
            ? objective.stepId
            : firstStep.id;
        const std::string location = objective.available && !objective.location.empty()
            ? objective.location
            : firstStep.location;
        const std::string title = objective.available && !objective.title.empty()
            ? objective.title
            : firstStep.title;
        const ScenarioStepState stepState = objective.available
            ? objective.state
            : ScenarioStepState::Locked;
        std::string detail = scenarioMapCocoonDetail(state, objective);
        if (detail.empty()) {
            if (objective.available && objective.required > 1) {
                detail = std::to_string(objective.current) + "/" +
                    std::to_string(objective.required) + " " + objective.title;
            } else if (objective.available && stepState == ScenarioStepState::Complete) {
                detail = "[DONE] " + title;
            } else if (objective.available) {
                detail = objective.detail;
            } else {
                detail = "[LOCKED] " + title;
            }
        }
        out << "<div class=\"" << scenarioObjectiveStateClass(stepState)
            << "\" data-scenario-id=\"" << htmlEscape(instance.id)
            << "\" data-scenario-step-id=\"" << htmlEscape(stepId)
            << "\" data-objective-state=\"" << scenarioObjectiveStateLabel(stepState)
            << "\" aria-label=\"" << htmlEscape(
                location + " " + scenarioObjectiveStateLabel(stepState) + " " + title)
            << "\">"
            << "<span>" << htmlEscape(location) << "</span><strong>"
            << htmlEscape(detail) << "</strong></div>";
    }
    return out.str();
}

struct MapRouteSummary {
    std::string label = "Route requirements";
    std::string detail = "ROUTES OPEN";
    std::string scenarioId;
    std::string stepId;
};

MapRouteSummary mapRouteSummary(const GameState& state, const ContentCatalog& catalog)
{
    for (const Destination& destination : catalog.destinations) {
        if (destination.routeRequirementKeys.empty()) {
            continue;
        }
        const ScenarioRouteRequirementStatus requirement =
            scenarioRouteRequirementStatus(state, catalog, destination);
        if (requirement.satisfied) {
            continue;
        }
        MapRouteSummary summary;
        summary.label = destination.name + " connector";
        summary.scenarioId = requirement.scenarioId;
        summary.stepId = requirement.stepId;
        const ScenarioObjectivePresentation objective =
            scenarioObjectivePresentation(state, catalog, requirement.scenarioId, requirement.stepId);
        summary.detail = objective.available && !objective.title.empty()
            ? "LOCKED — " + objective.title
            : "LOCKED — COMPLETE REQUIREMENT";
        return summary;
    }
    return {};
}

std::string solarMapBody(const PanelRenderContext& context)
{
    const GameState& state = context.state;
    const ContentCatalog& catalog = context.catalog;
    const auto reached = [&](int checkpoint, int tier) {
        return context.debugActOneCheckpoint >= 0
            ? context.debugActOneCheckpoint >= checkpoint
            : state.meta.furthestTier >= tier;
    };
    const auto destinationVisited = [&](std::string_view destinationId) {
        return destinationHistoryValue(state.meta.destinationSuccesses, catalog, destinationId) > 0
            || destinationHistoryValue(state.meta.destinationFlybys, catalog, destinationId) > 0
            || destinationHistoryValue(state.meta.destinationOrbits, catalog, destinationId) > 0;
    };

    const bool moonExplored = reached(1, 1) || destinationVisited(content::destination::moon);
    const bool marsExplored = reached(2, 2) || destinationVisited(content::destination::mars);
    const bool jupiterExplored = reached(3, 3) || destinationVisited(content::destination::jupiter);
    const bool saturnExplored = reached(4, 4) || destinationVisited(content::destination::saturn);
    const bool uranusExplored = reached(5, 5) || destinationVisited(content::destination::uranus);
    const bool neptuneExplored = reached(6, 6) || destinationVisited(content::destination::neptune) || arkDiscovered(state);
    const bool straylightFound = arkDiscovered(state);
    const MapRouteSummary nextRoute = mapRouteSummary(state, catalog);
    const std::string routeScenarioAttributes = nextRoute.scenarioId.empty()
        ? std::string {}
        : " data-scenario-id=\"" + htmlEscape(nextRoute.scenarioId)
            + "\" data-scenario-step-id=\"" + htmlEscape(nextRoute.stepId)
            + "\" data-objective-state=\"LOCKED\"";
#if 0 // Superseded fixed campaign checklist; scenarioCampaignTrack is content-driven.
    const std::string saturnConnector = state.meta.saturnRouteUnlocked
        ? "ROUTE OPEN"
        : (state.meta.saturnSlingshotPerfect
              ? "SLINGSHOT READY"
              : "LOCKED — PERFECT IO FLYBY");
    std::string ioChecklist;
    if (state.meta.ioArtifactRecovered) {
        ioChecklist = "[DONE] HAZARD / 4+4 SEALS / SAFE EXTRACTION";
    } else if (!state.meta.ioHazardDroneCommissioned) {
        ioChecklist = "[ ] COMMISSION HAZARD DRONE";
    } else if (state.screen == Screen::Mining
        && state.run.mining.destinationId == content::destination::jupiter) {
        const MiningGateRuntime& gate = state.run.mining.gate;
        const int outerCleared = std::clamp(
            gate.outerShellTilesTotal - gate.outerShellTilesRemaining,
            0,
            4);
        const int innerCleared = std::clamp(
            gate.innerShellTilesTotal - gate.innerShellTilesRemaining,
            0,
            4);
        ioChecklist = "[DONE] HAZARD / OUTER " + std::to_string(outerCleared) +
            "/4 / INNER " + std::to_string(innerCleared) + "/4 / [ ] EXTRACT";
    } else {
        ioChecklist = "[DONE] HAZARD / [ ] OUTER 0/4 / [LOCKED] INNER / [ ] EXTRACT";
    }
#endif
    const int exploredWorlds = 1
        + (moonExplored ? 1 : 0)
        + (marsExplored ? 1 : 0)
        + (jupiterExplored ? 1 : 0)
        + (saturnExplored ? 1 : 0)
        + (uranusExplored ? 1 : 0)
        + (neptuneExplored ? 1 : 0);
    const Destination& savedMapFrontier = currentDestination(state, catalog);
    const Destination* visibleMapFrontier = &savedMapFrontier;
    if (savedMapFrontier.hiddenFromProgression) {
        if (const Destination* next = nextDestination(state, catalog)) {
            visibleMapFrontier = next;
        }
    }

    std::ostringstream out;
    out << "<div class=\"solar-map\">"
        << "<div class=\"solar-map-summary\">"
        << "<div><span>Current frontier</span><strong>" << htmlEscape(visibleMapFrontier->name) << "</strong></div>"
        << "<div><span>Explored worlds</span><strong>" << exploredWorlds << " / 7</strong></div>"
        << "<div" << routeScenarioAttributes << "><span>" << htmlEscape(nextRoute.label) << "</span><strong>"
        << htmlEscape(nextRoute.detail) << "</strong></div></div>"
        << "<div class=\"solar-map-campaign-track\">"
        << scenarioCampaignTrack(state, catalog)
#if 0 // Superseded fixed campaign checklist; scenarioCampaignTrack is content-driven.
        << "<div class=\"" << (state.meta.lunarProspectorClaimed ? "is-complete" : "is-active")
        << "\"><span>MOON</span><strong>" << (state.meta.lunarProspectorClaimed ? "✓ SLOT 1" : "○ ORE → SLOT 1") << "</strong></div>"
        << "<div class=\"" << (state.meta.marsBayExpansionClaimed ? "is-complete" : (state.meta.lunarProspectorClaimed ? "is-active" : "is-locked"))
        << "\"><span>MARS</span><strong>" << (state.meta.marsBayExpansionClaimed ? "✓ SLOT 2" : "○ ORE → SLOT 2") << "</strong></div>"
        << "<div class=\"" << (state.meta.ioArtifactRecovered ? "is-complete" : (state.meta.marsBayExpansionClaimed ? "is-active" : "is-locked"))
        << "\"><span>IO // JUPITER SYSTEM</span><strong>" << htmlEscape(ioChecklist) << "</strong></div>"
        << "<div class=\"" << (state.meta.saturnRouteUnlocked ? "is-complete" : (state.meta.ioArtifactRecovered ? "is-active" : "is-locked"))
        << "\"><span>SATURN</span><strong>" << (state.meta.saturnRouteUnlocked ? "✓ ROUTE OPEN" : "○ PERFECT FLYBY") << "</strong></div>"
#endif
        << "</div>"
        << "<div class=\"solar-map-section solar-map-system\"><div class=\"solar-map-section-head\"><h3>System bodies</h3><span>Inner system to heliopause</span></div>"
        << "<div class=\"solar-system-track\">"
        << solarMapNode("Sun", "S", "map-sun", MapKnowledge::Explored)
        << solarMapNode("Mercury", "Me", "map-mercury", MapKnowledge::Charted)
        << solarMapNode("Venus", "V", "map-venus", MapKnowledge::Charted)
        << solarMapNode("Earth", "E", "map-earth", MapKnowledge::Explored)
        << solarMapNode("Mars", "Ma", "map-mars", marsExplored ? MapKnowledge::Explored : MapKnowledge::Charted)
        << solarMapNode("Jupiter", "J", "map-jupiter", jupiterExplored ? MapKnowledge::Explored : MapKnowledge::Charted)
        << solarMapNode("Saturn", "Sa", "map-saturn", saturnExplored ? MapKnowledge::Explored : MapKnowledge::Charted)
        << solarMapNode("Uranus", "U", "map-uranus", uranusExplored ? MapKnowledge::Explored : MapKnowledge::Charted)
        << solarMapNode("Neptune", "N", "map-neptune", neptuneExplored ? MapKnowledge::Explored : MapKnowledge::Charted)
        << "</div></div>"
        << "<div class=\"solar-map-section\"><div class=\"solar-map-section-head\"><h3>Moons</h3><span>Primary survey targets</span></div><div class=\"solar-map-row\">"
        << solarMapNode("Luna", "L", "map-moon", moonExplored ? MapKnowledge::Explored : MapKnowledge::Charted)
        << solarMapNode("Phobos", "Ph", "map-moon", marsExplored ? MapKnowledge::Explored : MapKnowledge::Charted)
        << solarMapNode("Deimos", "De", "map-moon", marsExplored ? MapKnowledge::Explored : MapKnowledge::Charted)
        << solarMapNode("Io", "Io", "map-moon", jupiterExplored ? MapKnowledge::Explored : MapKnowledge::Charted)
        << solarMapNode("Titan", "T", "map-moon", saturnExplored ? MapKnowledge::Explored : MapKnowledge::Charted)
        << solarMapNode("Triton", "Tr", "map-moon", neptuneExplored ? MapKnowledge::Explored : MapKnowledge::Charted)
        << "</div></div>"
        << "<div class=\"solar-map-lower\">"
        << "<div class=\"solar-map-section solar-map-lower-section\"><div class=\"solar-map-section-head\"><h3>Vessels</h3><span>Tracked contacts</span></div><div class=\"solar-map-row\">"
        << solarMapNode("Pathfinder", "PF", "map-vessel", MapKnowledge::Explored)
        << (straylightFound ? solarMapNode("Straylight", "ARK", "map-vessel", MapKnowledge::Explored) : std::string {})
        << "</div></div>"
        << "<div class=\"solar-map-section solar-map-lower-section\"><div class=\"solar-map-section-head\"><h3>Anomalies</h3><span>Sensor returns</span></div><div class=\"solar-map-row\">"
        << solarMapNode(marsExplored ? "Mars Echo" : "Unresolved signal", "ME", "map-anomaly", marsExplored ? MapKnowledge::Explored : MapKnowledge::Unknown)
        << (straylightFound ? solarMapNode("Neptune Signal", "NS", "map-anomaly", MapKnowledge::Explored) : std::string {})
        << "</div></div>"
        << "<div class=\"solar-map-section solar-map-lower-section\"><div class=\"solar-map-section-head\"><h3>Asteroid fields</h3><span>Navigation hazards</span></div><div class=\"solar-map-row\">"
        << solarMapNode("Main Belt", "* *", "map-field", marsExplored ? MapKnowledge::Explored : MapKnowledge::Charted)
        << solarMapNode("Jovian Trojans", "* *", "map-field", jupiterExplored ? MapKnowledge::Explored : MapKnowledge::Charted)
        << solarMapNode("Kuiper Belt", "* *", "map-field", uranusExplored ? MapKnowledge::Explored : MapKnowledge::Charted)
        << "</div></div></div>"
        << "<div class=\"solar-map-legend\"><span class=\"legend-explored\">Explored</span><span class=\"legend-charted\">Charted</span><span class=\"legend-unknown\">? Unresolved</span></div>"
        << "</div>";
    return out.str();
}

std::string solarMapTemplate(const PanelRenderContext& context)
{
    return modalTemplate(ui::modals::map, "Solar System Map", solarMapBody(context));
}

} // namespace

std::string buildGamePanelMarkup(
    const PanelRenderContext& context,
    std::vector<ModalPresentation>& modals)
{
    ScopedModalCollector modalCollector(modals);
    const GameState& state = context.state;
    const ContentCatalog& catalog = context.catalog;
    const Destination& currentFrontier = currentDestination(state, catalog);
    const Destination& launchTarget = hangarLaunchTargetDestination(state, catalog);

    const Astronaut* astronaut = activeAstronaut(state);
    const Destination* next = nextDestination(state, catalog);
    const LaunchReadinessPresentation launchReadiness = launchReadinessPresentation(state, catalog);
    const std::vector<PanelMetricPresentation> headerMetrics = compactHeaderMetrics(state, catalog, context.activeLaunch);
    const PanelLayoutMode layoutMode = panelLayoutMode(state.screen);

    std::ostringstream out;
    std::ostringstream settingsBody;
    std::vector<DetailPresentationRow> settingsDetails {
        detailPresentationRow(text::panel::details::keyboard, text::panel::details::keyboardValue),
        detailPresentationRow(text::panel::details::save, context.saveDescription),
        detailPresentationRow(text::panel::details::build, context.renderDescription),
    };
    settingsDetails.push_back(detailPresentationRow(
        "Controller",
        std::string_view("Left stick or D-pad navigates; South selects; East goes back; Menu pauses. Context prompts show flight and mining controls.")));
    settingsBody << detailStack(settingsDetails);
    settingsBody << "<section class=\"settings-control\" data-resolution-settings>"
        << "<div><h3>" << htmlEscape("Display resolution") << "</h3>"
        << "<p>" << htmlEscape("Choose the render target. Auto follows the current display and pixel density.") << "</p></div>"
        << "<label><span>" << htmlEscape("Resolution") << "</span>"
        << "<select data-resolution-select aria-label=\"Display resolution\">"
        << "<option value=\"auto\">Auto (display)</option>"
        << "<option value=\"1280x800\">1280 x 800 (Steam Deck)</option>"
        << "<option value=\"1920x1080\">1920 x 1080</option>"
        << "<option value=\"2560x1440\">2560 x 1440</option>"
        << "<option value=\"3840x2160\">3840 x 2160</option>"
        << "</select></label></section>";
    settingsBody << "<section class=\"settings-control\" data-desktop-fullscreen-settings>"
        << "<div><h3>" << htmlEscape("Fullscreen") << "</h3>"
        << "<p>" << htmlEscape("Use the entire display. Native builds also support F11 and Alt+Enter.") << "</p></div>"
        << "<button class=\"settings-toggle rr-text-button\" data-desktop-fullscreen-toggle=\"1\" data-ui-focus-id=\"setting:fullscreen\">"
        << "<span class=\"rr-button-label\">" << htmlEscape("Enter fullscreen") << "</span></button></section>";
    settingsBody << "<section class=\"settings-control\" data-frame-limit-settings>"
        << "<div><h3>" << htmlEscape("Frame rate") << "</h3>"
        << "<p>" << htmlEscape("Choose stable, refresh-compatible pacing. Auto Power uses full refresh on external power and half refresh on battery.") << "</p>"
        << "<p id=\"frame-limit-status\" data-frame-limit-status>" << htmlEscape("Auto Power status is loading.") << "</p></div>"
        << "<label><span>" << htmlEscape("Frame limit") << "</span>"
        << "<select data-frame-limit-select data-ui-focus-id=\"setting:frame_limit\" aria-label=\"Frame rate limit\">"
        << "<option value=\"platform_default\">Platform default</option>"
        << "<option value=\"smooth60\">Smooth (60 FPS)</option>"
        << "<option value=\"balanced\">Balanced (40 / 45 FPS)</option>"
        << "<option value=\"battery30\">Battery (30 FPS)</option>"
        << "<option value=\"display\">Display refresh</option>"
        << "<option value=\"auto_power\">Auto Power</option>"
        << "</select></label></section>";
    settingsBody << "<section class=\"settings-control\" data-game-speed-settings>"
        << "<div><h3>" << htmlEscape("Game speed") << "</h3>"
        << "<p>" << htmlEscape("Local testing multiplier. Shared builds start at 1x.") << "</p></div>"
        << "<label><span>" << htmlEscape("Multiplier") << "</span>"
        << "<select data-game-speed-select data-ui-focus-id=\"setting:game_speed\" aria-label=\"Game speed multiplier\">"
        << "<option value=\"0.5\">0.5x</option>"
        << "<option value=\"1\">1x</option>"
        << "<option value=\"1.5\">1.5x</option>"
        << "<option value=\"2\">2x</option>"
        << "<option value=\"3\">3x</option>"
        << "<option value=\"5\">5x</option>"
        << "<option value=\"8\">8x</option>"
        << "</select></label></section>";
    settingsBody << "<section class=\"settings-control\" data-help-settings>"
        << "<div><h3>" << htmlEscape("First-time introductions") << "</h3>"
        << "<p>" << htmlEscape("Show a short briefing the first time a new mission activity becomes available.") << "</p></div>"
        << "<button class=\"settings-toggle rr-text-button\" data-help-toggle=\"1\" data-ui-focus-id=\"setting:mission_help\">"
        << "<span class=\"rr-button-label\">" << htmlEscape(context.firstTimeIntroductionsEnabled ? "Hide introductions" : "Show introductions") << "</span></button></section>";
    settingsBody << "<section class=\"settings-control\" data-camera-shake-settings>"
        << "<div><h3>" << htmlEscape("Camera shake") << "</h3>"
        << "<p>" << htmlEscape("Keep impact and drilling screen shake enabled, or disable it for comfort.") << "</p></div>"
        << "<button class=\"settings-toggle rr-text-button\" data-camera-shake-toggle=\"1\" data-ui-focus-id=\"setting:camera_shake\">"
        << "<span class=\"rr-button-label\">" << htmlEscape("Disable camera shake") << "</span></button></section>";
    settingsBody << "<section class=\"settings-control\" data-keyboard-drill-mode-settings>"
        << "<div><h3>" << htmlEscape("Keyboard drill mode") << "</h3>"
        << "<p>" << htmlEscape("Toggle avoids holding Space. Hold drills only while Space remains pressed. Mouse and controller always use hold.") << "</p></div>"
        << "<label><span>" << htmlEscape("Space key") << "</span>"
        << "<select data-keyboard-drill-mode-select data-ui-focus-id=\"setting:keyboard_drill_mode\" aria-label=\"Keyboard drill mode\">"
        << "<option value=\"toggle\">Toggle (default)</option><option value=\"hold\">Hold</option>"
        << "</select></label></section>";
    settingsBody << "<section class=\"settings-control\" data-controller-prompt-settings>"
        << "<div><h3>" << htmlEscape("Controller prompts") << "</h3>"
        << "<p>" << htmlEscape("Auto follows the active controller. Override labels if the detected family is wrong.") << "</p></div>"
        << "<label><span>" << htmlEscape("Button labels") << "</span>"
        << "<select data-controller-prompt-select data-ui-focus-id=\"setting:controller_prompt\" aria-label=\"Controller prompt family\">"
        << "<option value=\"auto\">Auto detect</option><option value=\"xbox\">Xbox</option>"
        << "<option value=\"playstation\">PlayStation</option><option value=\"steamdeck\">Steam Deck</option>"
        << "<option value=\"generic\">Generic</option></select></label></section>";
    settingsBody << "<section class=\"settings-control\" data-controller-deadzone-settings>"
        << "<div><h3>" << htmlEscape("Stick deadzone") << "</h3>"
        << "<p>" << htmlEscape("Raise this if a resting stick drifts. Lower it for faster response.") << "</p></div>"
        << "<label><span>" << htmlEscape("Deadzone") << "</span>"
        << "<select data-controller-deadzone-select data-ui-focus-id=\"setting:controller_deadzone\" aria-label=\"Controller stick deadzone\">"
        << "<option value=\"0.10\">10%</option><option value=\"0.15\">15%</option><option value=\"0.20\">20% (default)</option>"
        << "<option value=\"0.25\">25%</option><option value=\"0.30\">30%</option><option value=\"0.35\">35%</option>"
        << "</select></label></section>";
    settingsBody << "<section class=\"settings-control\"><div><h3>" << htmlEscape("Invert flight Y") << "</h3>"
        << "<p>" << htmlEscape("Reverse vertical stick input during flyby and orbit flight.") << "</p></div>"
        << "<button class=\"settings-toggle rr-text-button\" data-controller-invert-toggle=\"1\" data-ui-focus-id=\"setting:controller_invert\"><span class=\"rr-button-label\">Enable inverted Y</span></button></section>";
    settingsBody << "<section class=\"settings-control\"><div><h3>" << htmlEscape("Confirm / cancel") << "</h3>"
        << "<p>" << htmlEscape("Swap the positional South and East buttons for menu confirm and cancel.") << "</p></div>"
        << "<button class=\"settings-toggle rr-text-button\" data-controller-swap-toggle=\"1\" data-ui-focus-id=\"setting:controller_swap\"><span class=\"rr-button-label\">Swap confirm and cancel</span></button></section>";
    settingsBody << "<section class=\"settings-control\"><div><h3>" << htmlEscape("Controller vibration") << "</h3>"
        << "<p>" << htmlEscape("Use supported controller haptics for impacts, drilling contact, and alerts.") << "</p></div>"
        << "<button class=\"settings-toggle rr-text-button\" data-controller-vibration-toggle=\"1\" data-ui-focus-id=\"setting:controller_vibration\"><span class=\"rr-button-label\">Disable vibration</span></button></section>";
    settingsBody << "<section class=\"settings-control\" data-debug-tools-settings>"
        << "<div><h3>" << htmlEscape("Debug screens") << "</h3>"
        << "<p>" << htmlEscape("Show sandbox screen tools for board, mining, flyby, and orbit checks. These do not write save data.") << "</p></div>"
        << "<button class=\"settings-toggle rr-text-button\" data-debug-tools-toggle=\"1\" data-ui-focus-id=\"setting:debug_tools\">"
        << "<span class=\"rr-button-label\">" << htmlEscape("Show debug tools") << "</span></button></section>";
    settingsBody << "<section class=\"settings-control\" data-performance-stats-settings>"
        << "<div><h3>" << htmlEscape("Performance diagnostics") << "</h3>"
        << "<p>" << htmlEscape("Show FPS, frame pacing, CPU stage timings, drawable size, and scene rendering counters.") << "</p></div>"
        << "<button class=\"settings-toggle rr-text-button\" data-performance-stats-toggle=\"1\" data-ui-focus-id=\"setting:performance_stats\">"
        << "<span class=\"rr-button-label\">" << htmlEscape("Show performance stats") << "</span></button></section>";
    settingsBody << "<div class=\"modal-actions action-row\">";
    for (const PanelButtonPresentation& action : settingsActionPresentation()) {
        if (action.actionId == ui::actions::resetSave) {
            if (context.titleScreenActive && !context.hasSavedGame) {
                continue;
            }
            settingsBody << modalButton(action.label, "reset_save_confirm", action.cssClass);
        } else {
            settingsBody << panelButton(action);
        }
    }
    settingsBody << "</div>";

    if (context.titleScreenActive) {
        out << "<section class=\"title-screen"
            << (context.titleLaunchActive ? " is-launching" : "")
            << "\" data-panel-mode=\"title\">"
            << "<div class=\"title-scanline title-scanline-a\"></div>"
            << "<div class=\"title-scanline title-scanline-b\"></div>"
            << "<div class=\"title-content\">"
            << "<span class=\"title-kicker\">DEEP SPACE RECOVERY PROGRAM // SIGNAL ONLINE</span>"
            << "<h1 class=\"orebit-lockup\" aria-label=\"OREBIT\">"
            << "<span class=\"orebit-letter orebit-ore orebit-letter-o\">O</span>"
            << "<span class=\"orebit-letter orebit-ore orebit-letter-r\">R</span>"
            << "<span class=\"orebit-letter orebit-ore orebit-letter-e\">E</span>"
            << "<span class=\"orebit-letter orebit-bit orebit-letter-b\">B</span>"
            << "<span class=\"orebit-letter orebit-bit orebit-letter-i\">I</span>"
            << "<span class=\"orebit-letter orebit-bit orebit-letter-t\">T</span>"
            << "</h1>"
            << "<p class=\"title-tagline\">DIG DEEP. FLY FAR. BRING THEM HOME.</p>"
            << "<div class=\"title-divider\"><span></span><strong>ORE // ORBIT // RETURN</strong><span></span></div>"
            << "<div class=\"title-menu\">";
        if (context.hasSavedGame) {
            out << "<div class=\"title-menu-primary\">"
                << button("Continue", ui::actions::continueGame, "title-action title-continue", true)
                << "</div>"
                << "<div class=\"title-menu-separator\" aria-hidden=\"true\"></div>"
                << "<div class=\"title-menu-secondary\">"
                << modalButton("New Game", "new_game_confirm", "title-action title-new-game")
                << modalButton("Settings", ui::modals::settings, "title-action title-settings")
                << "</div>";
        } else {
            out << button("New Game", ui::actions::newGame, "title-action title-new-game", true);
            out << modalButton("Settings", ui::modals::settings, "title-action title-settings");
        }
        out << "</div>"
            << "<span class=\"title-save-state ";
        out << (context.hasSavedGame ? "save-found\">SAVE SIGNAL ACQUIRED" : "save-empty\">NO LOCAL SAVE DETECTED");
        out << "</span>";
        if (!context.titleNotice.empty()) {
            out << "<p class=\"title-notice\">" << htmlEscape(context.titleNotice) << "</p>";
        }
        out << "<p class=\"title-footer\">A FRONTIER EXTRACTION ROGUELITE // BUILD 0.1</p>"
            << "</div></section>"
            << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        if (context.hasSavedGame) {
            const std::string newGameBody =
                "<p class=\"modal-intro\">Starting a new expedition replaces the current campaign save. Settings are preserved.</p>"
                "<div class=\"modal-actions action-row\">"
                "<button type=\"button\" class=\"ok rr-text-button\" data-ui-close-modal=\"1\" data-ui-focus-id=\"new-game:cancel\" data-ui-default-focus=\"1\"><span class=\"rr-button-label\">Cancel</span></button>" +
                button("Start New Game", ui::actions::newGame, "danger") +
                "</div>";
            out << modalTemplate("new_game_confirm", "Begin a new expedition?", newGameBody);
        }
        return out.str();
    }

    if (state.screen == Screen::StoryBriefing) {
        const bool straylight = state.storyBriefing.pending == StoryBriefingId::StraylightDiscovery;
        out << "<section class=\"story-briefing " << (straylight ? "story-straylight" : "story-introduction")
            << "\" data-panel-mode=\"story-briefing\" data-story-briefing-id=\""
            << (straylight ? "straylight-discovery" : "campaign-introduction") << "\">"
            << "<div class=\"story-vignette\"></div><div class=\"story-content\">";
        if (straylight) {
            out << "<span class=\"story-kicker\">NEPTUNE // DEEP-SPACE CONTACT</span>"
                << "<h1>STRAYLIGHT</h1>"
                << "<p class=\"story-lead\">Beyond Neptune, an impossible contact resolves against the dark.</p>"
                << "<div class=\"story-beats\">"
                << "<article><span>01</span><h2>Contact</h2><p>A hull answers where your charts insist there should be nothing.</p></article>"
                << "<article><span>02</span><h2>Identification</h2><p>The registry wakes one name: Straylight. Derelict. Under-equipped. Operable.</p></article>"
                << "<article><span>03</span><h2>Home</h2><p>For the first time, this expedition has more than a launch site. It has somewhere to return to.</p></article>"
                << "</div>"
                << button("Approach the Straylight", ui::actions::acknowledgeStoryBriefing, "story-action ok", true);
        } else {
            out << "<span class=\"story-kicker\">ARCHIVE PLAYBACK // EARTH, 20X6</span>"
                << "<h1>THE YEAR IS 20X6</h1>"
                << "<div class=\"story-exposition\">"
                << "<p>Humans have thoroughly f@#$ed the planet.</p>"
                << "<p>After getting their hands on a bootleg copy of KSP2, a small band of adorable varmints have decided to take to the stars where they can dig and tunnel and forge their way through new, pure, un-human-tainted soils.</p>"
                << "<p class=\"story-directive\">Help them. Help them trek out across the stars to build a brand new space empire.</p>"
                << "</div>"
                << button("Help them", ui::actions::acknowledgeStoryBriefing, "story-action ok", true)
                << "</div>"
                << expeditionControlsMarkup();
        }
        if (straylight) {
            out << "</div>";
        }
        out << "</section>";
        return out.str();
    }

    const std::string_view visualFamily = panelVisualFamilyName(panelVisualFamily(state.screen));
    out << "<div class=\"panel-head rr-screen-header ui-family-" << visualFamily << "\" data-ui-family=\"" << visualFamily
        << "\"><div class=\"panel-title\"><span class=\"game-mark\">" << htmlEscape(text::panel::title)
        << "</span><h1>" << htmlEscape(phaseTitle(state.screen)) << "</h1></div>"
        << "<div class=\"panel-head-actions\">"
        << modalButton("Map", ui::modals::map, "ghost")
        << modalButton("Inventory", ui::modals::inventory, "ghost");
    if (state.screen == Screen::Hangar) {
        out << modalButton("Details", ui::modals::hangarDetails, "ghost hangar-details-button");
    }
    out << modalButton("Menu", "system_menu", "ghost") << "</div></div>";
    out << solarMapTemplate(context);

    if (!headerMetrics.empty()) {
        if (state.screen != Screen::ArrivalOps) {
            out << "<p class=\"status panel-objective\">" << htmlEscape(compactHeaderObjective(state, catalog)) << "</p>";
        }
        out << "<div class=\"metric-grid rr-metric-strip panel-kpis\">";
        for (const PanelMetricPresentation& metricItem : headerMetrics) {
            out << metric(metricItem.label, metricItem.value);
        }
        out << "</div>";
    }

    if (state.screen == Screen::Navigation) {
        const std::vector<const Destination*> destinations = navigationDestinations(state, catalog);
        out << phaseBoardOpen("phase-board-navigation", state.statusLine);
        out << "<div class=\"phase-titlebar phase-lane\"><div><h2>" << htmlEscape("Solar System Navigation")
            << "</h2><p>" << htmlEscape(hostileSystemActive(state)
                ? "The Ark is stranded. Pick the next shuttle sortie, then prep the crew and vehicle."
                : "Plot the next route through known space.") << "</p></div></div>";
        if (destinations.empty()) {
            out << boardNote("No mapped destinations yet. Continue the frontier ladder to discover the Ark.");
        } else {
            out << "<section class=\"board-primary navigation-map phase-lane\"><h2>" << htmlEscape("Choose sortie") << "</h2><div class=\"ops-grid nav-grid\">";
            for (int index = 0; index < static_cast<int>(destinations.size()); ++index) {
                const Destination& destination = *destinations[static_cast<std::size_t>(index)];
                const bool selected = state.meta.navigation.selectedDestinationId == destination.id;
                const int fuelCost = 2 + destination.tier;
                const bool fuelAvailable = state.meta.ark.fuelReserve >= fuelCost;
                const int danger = static_cast<int>(std::round(destination.hazard * 24.0));
                const int value = static_cast<int>(std::round(destination.baseReward));
                const int durability = 35 + destination.tier * 12;
                out << "<article class=\"ops-card nav-card rr-fixed-lane-card ui-choice-row management-choice-row " << (selected ? "selected" : "") << "\">";
                out << "<div class=\"card-kicker\"><span>" << htmlEscape("Fuel " + std::to_string(fuelCost))
                    << "</span><span>" << htmlEscape("Danger " + display::percent(static_cast<double>(danger) / 100.0)) << "</span></div>";
                out << "<h3 class=\"card-title\">" << htmlEscape(destination.name) << "</h3>";
                out << "<p class=\"card-copy\">" << htmlEscape(destination.tier >= 4
                    ? "Hostile-system sortie. Rich resources, artifact leads, and enemy pressure."
                    : "Known solar-system route.") << "</p>";
                out << "<div class=\"stat-grid chip-strip\">";
                out << resourceChip(panelMetric("Value", std::to_string(value)));
                out << resourceChip(panelMetric("Terrain", display::percent(static_cast<double>(durability) / 100.0)));
                out << "</div>";
                out << "<div class=\"card-footer action-row\"><span>" << htmlEscape(selected ? "Selected" : (fuelAvailable ? "Mapped" : "Need fuel"))
                    << "</span>" << (fuelAvailable
                        ? button("Plot course", ui::actions::selectNavigationDestination(index), selected ? "ok" : "warn")
                        : disabledButton("Need fuel"))
                    << "</div></article>";
            }
            out << "</div></section>";
        }
        out << phaseBoardClose();
        out << scenarioObjectiveModalForDestination(state, catalog, currentDestination(state, catalog).id);
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (state.screen == Screen::ArrivalFanfare) {
        const Destination* arrivalDestination = catalog.findDestination(state.lastOutcome.destinationId);
        const std::string destinationName = arrivalDestination == nullptr ? currentFrontier.name : arrivalDestination->name;
        const bool closeCall = !launchOutcomeAchievements(state.lastOutcome).empty();
        out << "<div data-panel-mode=\"arrival-fanfare\" data-arrival-fanfare=\"1\" data-arrival-destination=\"" << htmlEscape(destinationName)
            << "\" data-arrival-close-call=\"" << (closeCall ? "1" : "0") << "\" hidden></div>";
        out << missionStamp(
            "Mission stamp",
            "Arrival confirmed",
            destinationName + " approach window open",
            "Approach data secured",
            "Approach window open",
            closeCall ? "Close call bonus" : "",
            ui::actions::skipArrivalFanfare);
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (layoutMode == PanelLayoutMode::ControlPanel && state.screen == Screen::Flyby) {
        const FlybyRunState& flyby = state.run.flyby;
        const Destination* flybyDestination = catalog.findDestination(flyby.destinationId);
        const std::string destinationName = flybyDestination == nullptr ? currentFrontier.name : flybyDestination->name;
        const double remaining = std::max(0.0, flyby.durationSeconds - flyby.elapsedSeconds);
        const FlybyGrade grade = flyby.completed ? flyby.result : FlybyGrade::Active;
        const TransferAssistDefinition* transferAssist = catalog.findTransferAssist(flyby.transferAssistId);
        const bool transferAssistRun = transferAssist != nullptr;
        const bool scenarioChallenge = flyby.purpose == FlybyPurpose::ScenarioChallenge &&
            !flyby.scenarioId.empty() && !flyby.scenarioStepId.empty();
        const ScenarioObjectivePresentation challengeObjective = scenarioChallenge
            ? scenarioObjectivePresentation(state, catalog, flyby.scenarioId, flyby.scenarioStepId)
            : ScenarioObjectivePresentation {};
        const ScenarioObjectivePresentation skippedObjective = !scenarioChallenge && flybyDestination != nullptr
            ? scenarioObjectiveForDestination(state, catalog, flybyDestination->id)
            : ScenarioObjectivePresentation {};
        const bool clearsGenericRoute = !scenarioChallenge && flybyClearsGenericNextRoute(state, catalog);

        if (flyby.completed && transferAssistRun) {
            const double speedBoost = flyby.slingshotAwarded
                ? flyby.slingshotSpeedBoost
                : flybySlingshotSpeedBoost(flyby, transferAssist->speedBoostBase);
            const double tank = launchFuelCapacity(state);
            const Destination* target = catalog.findDestination(transferAssist->targetDestinationId);
            const Destination* source = catalog.findDestination(transferAssist->sourceDestinationId);
            const std::string sourceName = source == nullptr ? destinationName : source->name;
            const std::string targetName = target == nullptr ? "the target" : target->name;
            const double poweredBurn = launchCruiseFuelCostForTier(target == nullptr ? 3 : target->tier) -
                transferAssist->fuelSavings;
            const double margin = tank - poweredBurn;
            const bool perfect = grade == FlybyGrade::Perfect;
            const bool good = grade == FlybyGrade::Good;
            const bool departing = good || perfect;
            const double instabilityPenalty = good
                ? transferAssist->goodInstabilityPenalty
                : 0.0;
            const std::string title = perfect
                ? "SLINGSHOT ACTIVE — STABLE"
                : (good
                      ? "SLINGSHOT ACTIVE — WILD RIDE"
                      : (flyby.collidedWithBody ? sourceName + " IMPACT" : "SLINGSHOT LOST"));
            const std::string body = perfect
                ? sourceName + "'s gravity has already sent the ship toward " + targetName + ". The Perfect pass supplies propellant-free velocity without changing normal flight stability. Its finish lane and outward drift carry into launch."
                : (good
                      ? sourceName + "'s gravity supplies the same " + display::fixed(transferAssist->fuelSavings, 0) + "-fuel saving and achieved velocity. The finish lane carries into launch, and the Good exit adds " + display::signedPercent(transferAssist->goodInstabilityPenalty) + " flight instability: more drift, oversteer, and throttle kick."
                      : (flyby.collidedWithBody
                            ? "The ship clipped " + sourceName + ". Hull damage applies, " + targetName + " departure did not occur, and the assist remains retryable."
                            : "The pass missed the departure corridor. Retry the Flyby or build more permanent margin."));
            out << "<div data-panel-mode=\"mission-stamp\" data-flyby-run=\"1\" data-flyby-completed=\"1\" data-flyby-purpose=\"transfer-assist\" hidden></div>";
            out << missionStamp(
                sourceName + " departure",
                title,
                body,
                departing ? display::fixed(tank, 0) + " tank" : "NO DEPARTURE",
                departing ? display::fixed(poweredBurn, 0) + " powered burn" : "GOOD REQUIRED",
                departing
                    ? flybySpeedLabel(flyby) + " finish // +" +
                        display::percent(speedBoost) + " launch velocity"
                    : (flyby.collidedWithBody
                          ? "Hull +" + std::to_string(flyby.impactHullDamage) + "%"
                          : "RETRY OR REFIT"),
                departing ? ui::actions::continueTransferAssist : ui::actions::flybyContinue,
                departing ? std::string_view("Continue to " + targetName) : std::string_view("Return to Hangar"));
            out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
            out << inventoryTemplate(state, catalog);
            return out.str();
        }

        if (flyby.completed) {
            const bool successfulRecon = !scenarioChallenge && !flyby.collidedWithBody &&
                (grade == FlybyGrade::Good || grade == FlybyGrade::Perfect);
            const RouteTransitState& incomingRoute = state.run.arrivalOps.incomingRoute;
            const Destination* recoveryDestination = incomingRoute.active()
                ? catalog.findDestination(incomingRoute.originDestinationId)
                : nullptr;
            const RouteLinkDefinition* recoveryLink = recoveryDestination == nullptr
                ? nullptr
                : catalog.findRouteLink(recoveryDestination->id, flyby.destinationId);
            const bool recoveryRequired = successfulRecon && !clearsGenericRoute &&
                recoveryDestination != nullptr && recoveryLink != nullptr && recoveryLink->recoveryAvailable;
            const std::string recoveryRouteLabel = recoveryRequired
                ? destinationName + " \xE2\x86\x92 " + recoveryDestination->name
                : std::string();
            const std::string resultTitle = scenarioChallenge
                ? (grade == FlybyGrade::Perfect
                      ? challengeObjective.title + " READY"
                      : (grade == FlybyGrade::Good
                            ? "CLEAN FLYBY — INSUFFICIENT"
                            : "CHALLENGE INCOMPLETE"))
                : (flyby.collidedWithBody
                      ? "IMPACT RECORDED"
                      : (grade == FlybyGrade::Good || grade == FlybyGrade::Perfect
                            ? (recoveryRequired ? "RECOVERY ROUTE REQUIRED" : "PLANET SKIPPED")
                            : flybyGradeLabel(grade)));
            const double flybySpeedScale = flyby.slingshotAwarded ? flyby.slingshotSpeedScale : flybySlingshotScale(flyby);
            const double flybyFuelSavings = flyby.slingshotAwarded ? flyby.slingshotFuelSavings : tuning::flyby::slingshotFuelBoost * flybySpeedScale;
            const double flybySpeedBoost = flyby.slingshotAwarded
                ? flyby.slingshotSpeedBoost
                : flybySlingshotSpeedBoost(flyby, tuning::flyby::slingshotSpeedBoost);
            const std::string resultBody = scenarioChallenge
                ? (grade == FlybyGrade::Perfect
                      ? "Required flight grade reached. Claim the configured reward explicitly."
                      : (!challengeObjective.failureExplanation.empty()
                            ? challengeObjective.failureExplanation
                            : challengeObjective.detail))
                : (flyby.collidedWithBody
                ? "The ship clipped the destination body. Hull damage added and no Research Data was recovered."
                : ((recoveryRequired
                    ? "Planet skipped. " +
                        (skippedObjective.available
                            ? "“" + skippedObjective.title + "” remains active; "
                            : std::string()) +
                        "the onward route stays locked. Fly " + recoveryRouteLabel +
                        " to recover, then reapproach."
                    : flybyResultBody(grade))
                    + ((grade == FlybyGrade::Good || grade == FlybyGrade::Perfect) && skippedObjective.available
                          ? (recoveryRequired ? "" : " “" + skippedObjective.title + "” remains active; the next story route is still blocked.")
                          : ((grade == FlybyGrade::Good || grade == FlybyGrade::Perfect) && clearsGenericRoute
                                ? " The generic onward route is cleared by sacrificing this world's surface resources for speed."
                                : ""))));
            const std::string tagOne = scenarioChallenge
                ? (grade == FlybyGrade::Perfect ? "READY TO CLAIM" : "ROUTE REMAINS LOCKED")
                : (flyby.collidedWithBody
                ? "Hull +" + std::to_string(flyby.impactHullDamage) + "%"
                : (grade == FlybyGrade::Miss ? "No Research Data" : "+" + std::to_string(flyby.blueprintGain) + " Research Data"));
            const std::string tagTwo = scenarioChallenge
                ? "PERFECT REQUIRED"
                : (flyby.collidedWithBody
                ? "No recon recovered"
                : (grade == FlybyGrade::Perfect
                    ? "Reward x" + display::fixed(flyby.rewardBonusScale, 1)
                    : (grade == FlybyGrade::Good ? "+" + display::money(flyby.rewardCredits) + " credits" : "NO COMMITMENT")));
            const std::string tagThree = scenarioChallenge
                ? (grade == FlybyGrade::Perfect ? challengeObjective.rewardPreview : "RETRY AVAILABLE")
                : (flyby.collidedWithBody
                ? "Hull damage logged"
                : (grade == FlybyGrade::Perfect
                    ? display::fixed(flybyFuelSavings, 1) + " fuel saved, +" + display::percent(flybySpeedBoost) + " velocity"
                    : (grade == FlybyGrade::Good
                          ? (recoveryRequired
                              ? recoveryRouteLabel + " RECOVERY"
                              : (skippedObjective.available ? "STORY ROUTE BLOCKED" : (clearsGenericRoute ? "GENERIC ROUTE CLEARED" : "NEXT LAUNCH: NO BOOST")))
                          : "APPROACH UNCOMMITTED")));
            const bool perfectChallengeReadyToClaim = scenarioChallenge &&
                grade == FlybyGrade::Perfect;
            const std::string perfectChallengeClaimLabel =
                challengeObjective.action == ScenarioActionKind::ClaimReward
                    ? challengeObjective.actionLabel
                    : "Lock Saturn Course";
            out << "<div data-panel-mode=\"mission-stamp\" data-flyby-run=\"1\" data-flyby-completed=\"1\" data-flyby-purpose=\""
                << (scenarioChallenge ? "scenario-challenge" : "recon") << "\" hidden></div>";
            out << missionStamp(
                scenarioChallenge ? "Scenario flyby" : "Flyby stamp",
                resultTitle,
                resultBody,
                tagOne,
                tagTwo,
                tagThree,
                perfectChallengeReadyToClaim
                    ? ui::actions::claimSaturnCourse
                    : ui::actions::flybyContinue,
                perfectChallengeReadyToClaim
                    ? std::string_view(perfectChallengeClaimLabel)
                    : (scenarioChallenge
                          ? std::string_view("Return to Hangar")
                          : (recoveryRequired
                                ? std::string_view("Begin Recovery: " + recoveryRouteLabel)
                                : std::string_view("Continue"))),
                nullptr);
            out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
            out << inventoryTemplate(state, catalog);
            return out.str();
        }

        out << "<div data-flyby-run=\"1\" data-flyby-completed=\"0\" data-flyby-purpose=\""
            << (transferAssistRun ? "transfer-assist" : (scenarioChallenge ? "scenario-challenge" : "recon"))
            << "\" hidden></div>";
        out << "<section class=\"live-hud-header\"><div><h2>"
            << htmlEscape(transferAssistRun
                ? transferAssist->displayName
                : (scenarioChallenge ? challengeObjective.title : "Manual Flyby")) << "</h2>"
            << "<p class=\"phase-copy\">" << htmlEscape(transferAssistRun
                ? "GOOD DEPARTS — Hold the gold corridor for Perfect stability. A Good pass reaches " +
                    (catalog.findDestination(transferAssist->targetDestinationId) == nullptr
                        ? std::string("the target")
                        : catalog.findDestination(transferAssist->targetDestinationId)->name) + " with " +
                    display::signedPercent(transferAssist->goodInstabilityPenalty) + " flight instability."
                : (scenarioChallenge
                      ? "PERFECT REQUIRED — " + challengeObjective.detail
                      : "Hold the approach corridor until the timer closes."))
            << "</p></div>" << modalButton("DETAILS", "flight_details", "ghost") << "</section>";
        if (scenarioChallenge) {
            out << scenarioObjectiveMarkup(challengeObjective, false, false);
        }
        const std::string zoneValue = flyby.collidedWithBody
            ? "Impact"
            : flybyZoneLabel(flyby.worstZone);

        out << "<div class=\"flight-status-list\">"
            << flightStatusRow("rr-hud-flyby-timer", "Timer", std::to_string(static_cast<int>(std::ceil(remaining))) + "s")
            << flightStatusRow("rr-hud-flyby-grade", "Grade forecast", zoneValue)
            << "</div>";

        out << "<div class=\"actions action-row rr-action-footer live-hud-actions\">"
            << panelButton(panelActionButton("ABORT FLYBY", ui::actions::flybyAbort, "danger")) << "</div>";

        const std::vector<DetailPresentationRow> flybyDetails {
            detailPresentationRow("Destination", scenarioChallenge ? challengeObjective.location : destinationName),
            detailPresentationRow("Reward multiplier", "x" + display::fixed(flyby.rewardBonusScale, 1)),
            detailPresentationRow(
                "Perfect window",
                scenarioChallenge
                    ? std::string_view(challengeObjective.rewardPreview)
                    : std::string_view("Creates next-launch fuel and speed margin")),
            detailPresentationRow("Controls", std::string_view("Turn and adjust speed; Abort records a Miss"))
        };
        out << modalTemplate("flight_details", "Flyby Details", detailStack(flybyDetails));
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (layoutMode == PanelLayoutMode::ControlPanel && state.screen == Screen::Orbit) {
        const OrbitRunState& orbit = state.run.orbit;
        const Destination* orbitDestination = catalog.findDestination(orbit.destinationId);
        const std::string destinationName = orbitDestination == nullptr ? currentFrontier.name : orbitDestination->name;
        const double remaining = std::max(0.0, orbit.durationSeconds - orbit.elapsedSeconds);
        const OrbitGrade grade = orbit.completed ? orbit.result : OrbitGrade::Active;
        const double progress = std::clamp(orbit.orbitProgress, 0.0, 1.0);
        const double baseOrbitReward = orbitDestination == nullptr
            ? tuning::orbit::goodRewardFloor
            : std::max(tuning::orbit::goodRewardFloor, orbitDestination->baseReward * tuning::orbit::goodRewardFactor);
        const double rewardCredits = grade == OrbitGrade::Perfect
            ? baseOrbitReward * tuning::orbit::perfectRewardMultiplier
            : (grade == OrbitGrade::Good ? baseOrbitReward : 0.0);
        const int blueprintGain = grade == OrbitGrade::Perfect
            ? tuning::orbit::perfectBlueprintGain + (orbitDestination != nullptr && destinationSupportsResearch(*orbitDestination) ? 1 : 0)
            : (grade == OrbitGrade::Good ? tuning::orbit::goodBlueprintGain + (orbitDestination != nullptr && destinationSupportsResearch(*orbitDestination) ? 1 : 0) : 0);

        if (orbit.completed) {
            const std::string resultTitle = grade == OrbitGrade::Good || grade == OrbitGrade::Perfect
                ? "ORBIT CAPTURED"
                : orbitGradeLabel(grade);
            const std::string resultBody = orbitResultBody(grade);
            const std::string tagOne = grade == OrbitGrade::Miss ? "APPROACH UNCOMMITTED" : "+" + std::to_string(blueprintGain) + " Research Data";
            const std::string tagTwo = grade == OrbitGrade::Miss ? "No Research Data" : "+" + display::money(rewardCredits) + " credits";
            const std::string tagThree = grade == OrbitGrade::Miss ? "RETRY OR CHOOSE ANOTHER PATH" : "LAND OR DEPART";
            out << "<div data-panel-mode=\"mission-stamp\" data-orbit-run=\"1\" data-orbit-completed=\"1\" hidden></div>";
            out << missionStamp("Orbit stamp", resultTitle, resultBody, tagOne, tagTwo, tagThree, ui::actions::orbitContinue);
            out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
            out << inventoryTemplate(state, catalog);
            return out.str();
        }

        out << "<div data-orbit-run=\"1\" data-orbit-completed=\"0\" hidden></div>";
        out << "<section class=\"live-hud-header\"><div><h2>" << htmlEscape("Orbit Capture") << "</h2>"
            << "<p class=\"phase-copy\">" << htmlEscape("The insertion holds Good without input. Finish in Perfect without entering red for the bonus.")
            << "</p></div>" << modalButton("DETAILS", "flight_details", "ghost") << "</section>";
        const std::string zoneValue = orbitZoneLabel(orbit.currentZone);

        out << "<div class=\"flight-status-list\">"
            << flightStatusRow("rr-hud-orbit-timer", "Timer", std::to_string(static_cast<int>(std::ceil(remaining))) + "s")
            << flightStatusRow("rr-hud-orbit-zone", "Zone", zoneValue)
            << flightStatusRow("rr-hud-orbit-loop", "Loop", display::percent(progress))
            << "</div>";

        out << "<div class=\"actions action-row rr-action-footer live-hud-actions\">"
            << panelButton(panelActionButton("ABORT ORBIT", ui::actions::orbitAbort, "danger")) << "</div>";

        const std::vector<DetailPresentationRow> orbitDetails {
            detailPresentationRow("Destination", destinationName),
            detailPresentationRow("Projected reward", grade == OrbitGrade::Active ? "Pending" : display::money(rewardCredits)),
            detailPresentationRow("Research Data", grade == OrbitGrade::Active ? "Pending" : "+" + std::to_string(blueprintGain)),
            detailPresentationRow("Controls", std::string_view("Up/Down adjust orbital speed; Left/Right adjust altitude")),
            detailPresentationRow(
                "Orbit assists",
                "Fuel +" + display::fixed(
                    static_cast<double>(launchUpgradeRank(state, LaunchUpgradeKind::FuelTanks)) *
                        tuning::orbit::fuelDurationAssistPerRank,
                    1) +
                    "s | Controls +" +
                    display::fixed(
                        static_cast<double>(launchUpgradeRank(state, LaunchUpgradeKind::FlightControls)) *
                            tuning::orbit::flightControlsThrustAssistPerRank * 100.0 +
                            static_cast<double>(launchUpgradeRank(state, LaunchUpgradeKind::Cooling)) *
                                tuning::orbit::coolingThrustAssistPerRank * 100.0,
                        0) +
                    "% trim | Hull reduces low-orbit collision risk " +
                    display::percent(
                        std::clamp(
                            1.0 - orbit.collisionPadding / tuning::orbit::collisionPadding,
                            0.0,
                            1.0)))
        };
        out << modalTemplate("flight_details", "Orbit Details", detailStack(orbitDetails));
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (layoutMode == PanelLayoutMode::ControlPanel && state.screen == Screen::Launch) {
        const LaunchPanelPresentation launchPanel = launchPanelPresentation(
            state,
            catalog,
            context.flightModel,
            context.currentMultiplier,
            context.returnBurnMultiplier,
            context.returnElapsed,
            context.returnDuration,
            context.flightActions,
            context.launchFlight);
        if (!context.flightArmed) {
            out << "<div data-preflight-launch=\"1\" data-preflight-ready=\""
                << (context.preflightReady ? "1" : "0") << "\" data-preflight-queued=\""
                << (context.launchQueued ? "1" : "0") << "\" hidden></div>";
        }
        out << "<div data-launch-manual-controls=\""
            << (context.flightModel.manualControlsEnabled ? "1" : "0")
            << "\" hidden></div>";

        out << "<section class=\"live-hud-header\"><div><h2>" << htmlEscape(launchPanel.sectionTitle)
            << "</h2></div></section>";
        out << "<section class=\"objective-strip rr-objective-strip\"><span>Lesson</span><strong>"
            << htmlEscape(launchPanel.objectiveTitle) << "</strong><p>"
            << htmlEscape(launchPanel.objectiveCopy) << "</p></section>";
        if (context.launchFlight != nullptr && context.flightModel.asteroidsEnabled) {
            out << "<div class=\"flight-status-list\">"
                << flightStatusRow(
                    "rr-hud-launch-hull",
                    "Hull",
                    display::fixed(std::max(0.0, context.launchFlight->hullRemaining), 0) + " / " +
                        display::fixed(context.launchFlight->hullMaximum, 0) + " HP")
                << "</div>";
        }

        out << "<p id=\"rr-hud-launch-status\" class=\"" << launchStatusSeverity(context) << "\">"
            << htmlEscape(launchPanel.telemetryMessage) << "</p>";
        const bool hasAdvancedFlightControls = !launchPanel.systemActions.empty();

        out << "<section class=\"cockpit-hud flight-hud\"><div class=\"cockpit-label\"><span>"
            << htmlEscape(text::panel::sections::flightControls) << "</span><strong>"
            << htmlEscape(context.flightArmed
                ? "Choose the next move"
                : (!context.droneTransferEnabled ? "Launch corridor clear" : (context.preflightReady ? "Launch corridor clear" : "Securing Mining Rig"))) << "</strong></div>";
        if (!context.flightArmed) {
            const std::string_view preflightCopy = context.launchQueued
                ? "Launch queued. The burn will begin automatically when the bay seals."
                : (!context.droneTransferEnabled
                    ? "Bay sealed. Use the cockpit launch control beside the vehicle."
                    : (context.preflightReady
                        ? "Mining Rig secured and bay sealed. Use the cockpit launch control beside the vehicle."
                        : "Mining Rig transfer in progress. Press Cross or A now to queue launch for bay seal."));
            out << "<p class=\"cockpit-hold-copy\">" << htmlEscape(preflightCopy) << "</p>";
        } else {
            out << "<div class=\"actions action-row primary-actions\">";
            for (std::size_t index = 0; index < launchPanel.primaryActions.size() && index < 2; ++index) {
                out << panelButton(launchPanel.primaryActions[index]);
            }
            out << "</div>";

            if (hasAdvancedFlightControls) {
                out << "<div class=\"actions action-row system-actions\">";
                for (std::size_t index = 0; index < launchPanel.systemActions.size() && index < 2; ++index) {
                    out << panelButton(launchPanel.systemActions[index]);
                }
                out << "</div>";
            }
        }
        out << "</section>";
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (state.screen == Screen::Results) {
        const bool opensPostArrival = shouldOpenPostArrivalPhases(state.lastOutcome, catalog);
        const LaunchOutcomePresentation presentation = launchOutcomePresentation(
            state,
            catalog,
            opensPostArrival);
        const LaunchOutcomeSummaryPresentation summary = launchOutcomeSummaryPresentation(state, catalog);
        const bool successfulReturn = state.lastOutcome.type != LaunchResultType::Destroyed
            && state.lastOutcome.failureCause == LaunchFailureCause::None
            && (state.lastOutcome.type == LaunchResultType::MissionComplete
                || rewardedLaunchLessonReturn(state.lastOutcome)
                || state.lastOutcome.recoveryMethod == RecoveryMethod::TransferArrival);
        const ModalTone outcomeTone = state.lastOutcome.fuelSurveyReturnTiming ==
                FuelSurveyReturnTiming::Late
            ? ModalTone::Warning
            : (successfulReturn
            ? ModalTone::Positive
            : (state.lastOutcome.type == LaunchResultType::None
                ? ModalTone::Neutral
                : ModalTone::Negative));
        std::ostringstream report;
        report << crewFateCard(presentation.crewFate) << "<div class=\"result-grid rr-card-grid\">";
        for (const LaunchOutcomeMetricGroupPresentation& group : presentation.metricGroups) {
            report << resultMetricGroup(group);
        }
        report << "</div>";
        for (const std::string& note : presentation.notes) {
            report << boardNote(note);
        }
        if (!presentation.achievements.empty()) {
            report << "<h2>" << htmlEscape(text::panel::sections::achievements) << "</h2><div class=\"achievement-grid\">";
            for (const AchievementPresentation& achievement : presentation.achievements) {
                report << achievementCard(achievement);
            }
            report << "</div>";
        }

        std::ostringstream summaryBody;
        summaryBody << "<section class=\"launch-outcome-summary\"><p class=\"launch-outcome-consequence\">"
            << htmlEscape(summary.consequence) << "</p>";
        if (successfulReturn &&
            state.lastOutcome.destinationId == content::destination::jupiter &&
            state.lastOutcome.recoveryMethod == RecoveryMethod::TransferArrival) {
            const bool usedSlingshot = state.lastOutcome.slingshotFuelSavings + 0.000001 >=
                tuning::flyby::jupiterSlingshotFuelSavings;
            const bool usedTanks = state.lastOutcome.transferFuelCapacity + 0.000001 >= 25.0;
            const bool wildRide = state.lastOutcome.slingshotInstabilityPenalty > 0.0;
            const std::string arrivalMethod = usedSlingshot && usedTanks
                ? (wildRide ? "MAXIMUM PREPARATION — WILD RIDE" : "MAXIMUM PREPARATION")
                : (usedSlingshot
                      ? (wildRide ? "BORROWED MOMENTUM — WILD RIDE" : "BORROWED MOMENTUM")
                      : "PERMANENT ENGINEERING MARGIN");
            const std::string arrivalCopy = usedSlingshot && usedTanks
                ? (wildRide
                      ? "Fuel Tanks III supplied permanent reserve while a Good Mars pass cut the burn and made flight control wilder. Hardware and risk stacked."
                      : "Fuel Tanks III supplied permanent reserve while a Perfect Mars pass cut the powered burn. Preparation and execution stacked.")
                : (usedSlingshot
                    ? (wildRide
                          ? "Mars supplied the missing movement. The Good exit reached Jupiter after a visibly less stable flight."
                          : "Mars supplied the missing movement. The Perfect exit preserved normal flight stability.")
                    : "Fuel Tanks III carried five permanent fuel beyond the calibrated burn. No flyby risk was required.");
            summaryBody << "<div class=\"jupiter-arrival-method\"><span>JUPITER ARRIVAL</span><strong>"
                << htmlEscape(arrivalMethod) << "</strong><p>" << htmlEscape(arrivalCopy) << "</p></div>";
        }
        summaryBody << "<div class=\"ui-outcome-rows\">"
            << "<div><span>OUTCOME</span><strong>" << htmlEscape(presentation.label) << "</strong></div>"
            << "<div><span>CREW</span><strong>" << htmlEscape(
                presentation.crewFate.active ? presentation.crewFate.title : std::string_view("Recovered")) << "</strong></div>"
            << "<div><span>NEXT</span><strong>" << htmlEscape(summary.progression) << "</strong></div>"
            << "</div><div class=\"modal-actions action-row rr-action-footer launch-outcome-actions\">"
            << modalButton("Flight Report", ui::modals::flightReport, "ghost")
            << button("Continue", ui::actions::next, "ok", true) << "</div></section>";

        out << "<section class=\"results-panel phase-board-results\" data-panel-mode=\"results\">"
            << "<section class=\"debrief-hero compact-result-backdrop\"><span>Mission resolved</span><h2>"
            << htmlEscape(summary.title) << "</h2><p>Select Continue to move into the next operation.</p></section>"
            << "</section>";
        // A launch outcome is an acknowledgement checkpoint. It must not
        // inherit the generic Close/Escape behaviour: the player needs to
        // deliberately choose Continue (or inspect the Flight Report) after
        // the flight has resolved.
        out << autoModalTemplate(
            ui::modals::launchOutcome,
            summary.title,
            summaryBody.str(),
            false,
            {},
            outcomeTone);
        out << modalTemplate(ui::modals::flightReport, "Flight Report", report.str());
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (state.screen == Screen::ArrivalOps) {
        const Destination* arrivalDestination = catalog.findDestination(state.run.arrivalOps.destinationId);
        const bool orbitCaptured = state.run.arrivalOps.commitment == ApproachCommitment::OrbitCaptured;
        const bool flybyAvailable = canRunArrivalFlyby(state, catalog);
        const bool orbitAvailable = canEnterArrivalOrbit(state, catalog);
        const bool firstLandingSequence = requiresArrivalOrbitBeforeLanding(state, catalog);
        const bool landingAvailable = canAttemptArrivalLanding(state, catalog);
        const bool canDepartOrbit = canDepartCapturedArrivalOrbit(state, catalog);
        const std::string_view flybyIntroduction = !context.firstTimeIntroductionsEnabled
                || !flybyAvailable
                || ui::briefings::acknowledged(state.meta.acknowledgedActivityBriefingIds, ui::briefings::flyby)
            ? std::string_view {}
            : ui::modals::flybyIntroduction;
        const std::string_view orbitIntroduction = !context.firstTimeIntroductionsEnabled
                || ui::briefings::acknowledged(state.meta.acknowledgedActivityBriefingIds, ui::briefings::orbit)
            ? std::string_view {}
            : ui::modals::orbitIntroduction;
        const std::string_view landingIntroduction = !context.firstTimeIntroductionsEnabled
                || !landingAvailable
                || ui::briefings::acknowledged(state.meta.acknowledgedActivityBriefingIds, ui::briefings::landing)
            ? std::string_view {}
            : ui::modals::landingIntroduction;
        const bool showApproachIntroduction = arrivalDestination != nullptr
            && arrivalDestination->requiresArrivalSurveySequence
            && context.firstTimeIntroductionsEnabled
            && !ui::briefings::acknowledged(state.meta.acknowledgedActivityBriefingIds, ui::briefings::approach);
        const ScenarioObjectivePresentation arrivalScenario = arrivalDestination == nullptr
            ? ScenarioObjectivePresentation {}
            : scenarioObjectiveForDestination(state, catalog, arrivalDestination->id);
        const ScenarioObjectivePresentation departureScenario = arrivalDestination == nullptr
            ? ScenarioObjectivePresentation {}
            : scenarioDepartureChallengeForDestination(state, catalog, arrivalDestination->id);
        const bool arrivalDepartureChallenge = departureScenario.available;
        const Destination* routeDestination = nextDestination(state, catalog);
        const bool authoredRoute = routeDestination != nullptr && !routeDestination->routeRequirementKeys.empty();
        const std::string routeStatus = routeDestination == nullptr
            ? "No onward route"
            : (authoredRoute
                  ? "Objective remains active; " + routeDestination->name + " route stays locked"
                  : "Clears all Flight Data required for " + routeDestination->name);
        const std::string commitmentLabel = orbitCaptured ? "ORBIT CAPTURED" : "APPROACH UNCOMMITTED";
        const std::string landingTarget = arrivalScenario.available && !arrivalScenario.location.empty()
            ? arrivalScenario.location
            : (arrivalDestination == nullptr ? "surface" : arrivalDestination->name);

        std::string flybyRewardDetail = "Research Data +1. ";
        std::string orbitRewardDetail;
        if (arrivalDestination != nullptr) {
            flybyRewardDetail += "Good "
                + display::money(flybyCreditRewardMinimum(*arrivalDestination, FlybyGrade::Good)) + "–"
                + display::money(flybyCreditRewardMaximum(*arrivalDestination, FlybyGrade::Good))
                + "; Perfect "
                + display::money(flybyCreditRewardMinimum(*arrivalDestination, FlybyGrade::Perfect)) + "–"
                + display::money(flybyCreditRewardMaximum(*arrivalDestination, FlybyGrade::Perfect))
                + ". " + routeStatus
                + ". Perfect also stores +1.5–3.0 fuel and +0.00–0.40 speed from the actual finish velocity for the next launch. Closes Orbit and Landing.";
            orbitRewardDetail = "Good +" + std::to_string(orbitResearchDataReward(*arrivalDestination, OrbitGrade::Good))
                + " Research Data and " + display::money(orbitCreditReward(*arrivalDestination, OrbitGrade::Good))
                + " credits; Perfect +" + std::to_string(orbitResearchDataReward(*arrivalDestination, OrbitGrade::Perfect))
                + " and " + display::money(orbitCreditReward(*arrivalDestination, OrbitGrade::Perfect))
                + ". Removes +20 descent hazard for this visit. Closes Pass Through; then Land or Depart.";
            if (firstLandingSequence) {
                orbitRewardDetail += " The first landing must use this orbital map.";
            }
        }

        out << phaseBoardOpen("phase-board-arrival", "");
        out << "<p class=\"status panel-objective arrival-objective\">"
            << htmlEscape(compactHeaderObjective(state, catalog)) << "</p>";
        out << "<div class=\"phase-titlebar\"><div><h2>" << htmlEscape(text::panel::sections::arrivalOps)
            << "</h2><p>" << htmlEscape(commitmentLabel + " — choose one arrival path for this visit.") << "</p></div></div>";
        const double landingPackFuel = arkDiscovered(state)
            ? std::min(
                  tuning::research::expeditionRigPackFuel,
                  static_cast<double>(std::max(0, state.meta.ark.fuelReserve)))
            : tuning::research::expeditionRigPackFuel;
        const ArrivalResearchUnlockPresentation researchUnlock =
            arrivalResearchUnlockPresentation(state.meta.blueprintProgress);
        const std::vector<PanelMetricPresentation> arrivalFuelMetrics {
            panelMetric("Transfer", display::fixed(state.run.arrivalOps.transferFuelRemaining, 1)),
            panelMetric("Rig fuel", display::fixed(landingPackFuel + state.run.arrivalOps.transferFuelRemaining, 1)),
            panelMetric(researchUnlock.label, researchUnlock.value),
            panelMetric("Descent", orbitCaptured ? "MAPPED +0" : "UNMAPPED +20")
        };
        out << "<div class=\"stat-grid chip-strip phase-lane\">"
            << resourceChipGrid(arrivalFuelMetrics) << "</div>";
        if (arrivalDestination != nullptr) {
            out << scenarioObjectiveMarkup(
                scenarioObjectiveForDestination(state, catalog, arrivalDestination->id),
                false);
            if (!arrivalDestination->approachBriefTitle.empty()) {
                out << phaseAdvisory({
                    arrivalDestination->approachBriefTitle,
                    arrivalDestination->approachBriefDetail,
                    arrivalDestination->requiresArrivalSurveySequence ? "info" : "warning"});
            }
        }
        out << "<p class=\"phase-copy\">Research families are added to future Refit offers when milestones are reached; they are not immediately owned.</p>";
        out << "<h2>" << htmlEscape(
            orbitCaptured ? "Resolve captured orbit" : (firstLandingSequence ? "Map first landing" : "Commit approach")) << "</h2>";
        out << "<div class=\"ops-grid\">";
        if (arrivalDepartureChallenge) {
            out << arrivalOperationCard(
                "JUPITER DEPARTURE — PERFECT SLINGSHOT",
                "The Saturn route is locked behind this departure pass. Hold the gold corridor through the finish, then lock the Saturn course.",
                "Required route challenge",
                "Perfect unlocks Saturn",
                panelActionButton(
                    "Launch",
                    ui::actions::beginSaturnSlingshot,
                    "ok"),
                {},
                true);
        } else if (orbitCaptured) {
            out << arrivalOperationCard(
                "LAND",
                "Descend to " + landingTarget + ". Surface hazard +0 from approach mapping. Earns the normal one-step Flight Data contribution. Orbit remains this visit's committed path.",
                "Mapped descent",
                "",
                landingAvailable
                    ? panelActionButton("LAND", ui::actions::arrivalLanding, "ok")
                    : disabledPanelButton(text::buttons::unavailable),
                landingIntroduction,
                true);
            if (canDepartOrbit) {
                out << arrivalOperationCard(
                    "DEPART WITH SCIENCE",
                    "Keep the Orbit credits and Research Data, end the visit, and skip surface resources. Grants no route clearance and no Landing Flight Data.",
                    "End visit",
                    "Research Data banked",
                    panelActionButton("DEPART", ui::actions::arrivalOrbitDepart, "warn"));
            }
        } else {
            if (flybyAvailable) {
                out << arrivalOperationCard(
                    "FLYBY — PASS THROUGH",
                    flybyRewardDetail,
                    "Terminal path",
                    "Research Data + credits",
                    panelActionButton("PASS THROUGH", ui::actions::arrivalFlyby, "ok"),
                    flybyIntroduction);
            }
            out << arrivalOperationCard(
                "ORBIT — CAPTURE",
                orbitRewardDetail,
                "Branching path",
                "Map descent or depart",
                orbitAvailable
                    ? panelActionButton("ORBIT", ui::actions::arrivalOrbit, "warn")
                    : disabledPanelButton(text::buttons::unavailable),
                orbitIntroduction);
            if (!firstLandingSequence) {
                out << arrivalOperationCard(
                    "DIRECT DESCENT",
                    "Immediately descend to " + landingTarget + ". SURFACE HAZARD +20. Closes Pass Through and Orbit. Earns the normal one-step Flight Data contribution after landing.",
                    "Terminal approach",
                    "",
                    landingAvailable
                        ? panelActionButton("LAND", ui::actions::arrivalLanding, "danger")
                        : disabledPanelButton(text::buttons::unavailable),
                    landingIntroduction);
            }
        }
        out << "</div>";
        out << phaseBoardClose();
        if (showApproachIntroduction) {
            const std::string approachLocation = arrivalScenario.available && !arrivalScenario.location.empty()
                ? arrivalScenario.location
                : arrivalDestination->name;
            out << activityIntroductionModal(
                ui::modals::approachIntroduction,
                approachLocation + " APPROACH",
                firstLandingSequence
                    ? "First landing protocol requires Capture Orbit, then Land with the orbital map."
                    : "Choose one committed path: Pass Through ends the visit, Capture Orbit opens mapped landing or science departure, and Direct Descent accepts +20 surface hazard.",
                firstLandingSequence
                    ? "Flyby is introduced later at the Jupiter transfer window. The Lunar contract and Mars route still require surface recovery."
                    : "At the Moon, skipping the planet does not complete the active capture objective or open the Mars route.",
                "Review",
                ui::actions::acknowledgeApproachIntroduction,
                "ok",
                true);
        }
        if (!flybyIntroduction.empty()) {
            out << activityIntroductionModal(
                ui::modals::flybyIntroduction,
                "FLYBY — PASS THROUGH",
                "A Good or Perfect pass banks Research Data and credits, closes Orbit and Landing, and ends this visit.",
                "At authored objectives the route stays blocked. Later generic routes can be cleared quickly by sacrificing surface resources. Perfect also stores powered-fuel savings and extra velocity for the next launch.",
                "Begin flyby",
                ui::actions::arrivalFlyby,
                "ok");
        }
        if (!orbitIntroduction.empty()) {
            out << activityIntroductionModal(
                ui::modals::orbitIntroduction,
                "ORBIT — CAPTURE",
                "The insertion begins in a stable Good orbit. Trim deliberately into Perfect for the larger Research Data and credit award.",
                "Capture closes Pass Through and removes the +20 descent hazard for this visit. Then choose mapped landing or depart with science.",
                "Enter orbit",
                ui::actions::arrivalOrbit,
                "warn");
        }
        if (!landingIntroduction.empty()) {
            out << activityIntroductionModal(
                ui::modals::landingIntroduction,
                orbitCaptured ? "MAPPED DESCENT" : "DIRECT DESCENT",
                orbitCaptured
                    ? "The orbital map removes the +20 approach hazard for this visit."
                    : "Direct Descent intentionally accepts +20 surface hazard and closes both flight paths.",
                "Surface Ops remains the path to materials, artifacts, campaign objectives, and one step of Flight Data.",
                "Begin landing",
                ui::actions::arrivalLanding,
                "danger");
        }
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (state.screen == Screen::Research) {
        const ResearchPhasePresentation researchPanel = researchPhasePresentation(state, catalog);
        out << phaseBoardOpen("phase-board-research", state.statusLine);
        out << "<div class=\"phase-titlebar\"><div><h2>" << htmlEscape("Research Board — Debug Only")
            << "</h2><p>" << htmlEscape("This deferred material-project prototype is not entered or populated by the campaign. Orbit does not open it.") << "</p></div>"
            << "<div class=\"utility-row compact-tools utility-actions\">" << modalButton(text::buttons::details, ui::modals::research, "ghost")
            << "</div></div>";
        out << phaseAdvisory(researchPanel.advisory);
        out << "<div class=\"metric-grid focus-metrics\">";
        for (const PanelMetricPresentation& metricItem : researchPanel.metrics) {
            if (metricItem.label == text::labels::blueprints
                || metricItem.label == text::labels::commonMaterials
                || metricItem.label == text::labels::rareMaterials
                || metricItem.label == text::labels::exoticMaterials) {
                out << metric(metricItem.label, metricItem.value);
            }
        }
        out << "</div>";
        out << "<section class=\"board-primary\"><h2>" << htmlEscape("Research options") << "</h2><div class=\"ops-grid\">";
        for (const ResearchProjectCardPresentation& project : researchPanel.projects) {
            out << researchProjectCard(project);
        }
        out << "</div></section>";
        out << "<div class=\"actions action-row\">";
        out << panelButton(researchPanel.skipAction);
        out << "</div>";
        out << phaseBoardClose();
        out << modalTemplate(ui::modals::research, text::panel::modals::researchDetails, detailStack(researchPanel.details));
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (state.screen == Screen::Mining) {
        const MiningHudPresentation miningHud = miningHudPresentation(state, catalog);
        const MiningRunPresentation miningRun = miningRunPresentation(state, catalog);
        const MiningRunState& mining = state.run.mining;
        const MiningLoadStats miningLoad = miningLoadStats(state, catalog);
        const bool evaActive = miningOperatorIsEva(mining);
        const ScenarioObjectivePresentation miningScenario = scenarioObjectiveForMining(state, catalog);
        const bool scenarioMining = miningScenario.available;
        const bool cocoonMining = !mining.gate.cocoonLayers.empty();

        out << "<section class=\"mining-fullscreen\" data-panel-mode=\"mining-fullscreen\" data-mining-drones=\""
            << (!mining.miniDrones.empty() ? 1 : 0) << "\" data-mining-operator-mode=\""
            << (evaActive ? "eva" : "rig") << "\">";
        const std::array<std::string_view, 4> miningVitalIds {
            "rr-hud-mining-oxygen",
            "rr-hud-mining-fuel",
            "rr-hud-mining-drill-bit",
            "rr-hud-mining-load"
        };
        const double activeOxygenCapacity = miningActiveOxygenCapacity(state, catalog);
        const double oxygenPressure = activeOxygenCapacity > 0.0
            ? std::clamp(1.0 - miningActiveOxygenSeconds(mining) / activeOxygenCapacity, 0.0, 1.0)
            : 1.0;
        const double fuelPressure = state.run.surfaceExpedition.rigFuelCapacity > 0.0
            ? std::clamp(
                  1.0 - state.run.surfaceExpedition.rigFuel /
                      state.run.surfaceExpedition.rigFuelCapacity,
                  0.0,
                  1.0)
            : 1.0;
        out << "<header class=\"mining-top-rail ui-screen-header rr-screen-header\"><div class=\"mining-run-title\"><strong id=\"rr-hud-mining-title\">"
            << htmlEscape(miningHud.runLabel) << "</strong><span class=\"mining-run-objective\" id=\"rr-hud-mining-objective-title\">"
            << htmlEscape(scenarioMining ? compactMiningScenarioObjective(state, catalog) : miningHud.objective)
            << "</span></div><section class=\"mining-vitals ui-kpi-strip rr-metric-strip\">";
        for (std::size_t index = 0; index < miningHud.vitals.size(); ++index) {
            const MiningHudTilePresentation& tile = miningHud.vitals[index];
            const std::string_view id = miningVitalIds[index];
            std::string vitalClass;
            if (index == 0) {
                vitalClass = miningVitalAlertClass("mining-vital-oxygen", oxygenPressure, mining.elapsedSeconds);
            } else if (index == 1) {
                vitalClass = miningVitalAlertClass(
                    "mining-vital-fuel",
                    std::max(fuelPressure, mining.fuelCycleProgress),
                    mining.elapsedSeconds,
                    true);
            } else if (index == 2) {
                vitalClass = miningVitalAlertClass(
                    "mining-vital-drill",
                    std::clamp(1.0 - mining.drillIntegrity, 0.0, 1.0),
                    mining.elapsedSeconds);
                vitalClass += " " + miningDrillHeatAlertClass(mining.drillHeat, mining.elapsedSeconds);
                if (mining.drillIntegrity <= 0.0) {
                    vitalClass += " mining-vital-broken";
                }
            } else {
                vitalClass = "mining-vital-load mining-alert-nominal";
            }
            if (!tile.cssClass.empty()) {
                vitalClass += " " + tile.cssClass;
            }
            out << "<article id=\"" << id << "\" class=\"mining-vital-tile " << htmlEscape(vitalClass) << "\"><span"
                << (index == 0 ? " id=\"rr-hud-mining-oxygen-label\"" : "") << ">"
                << htmlEscape(tile.label) << "</span><strong id=\"" << id << "-value\">" << htmlEscape(tile.value) << "</strong>";
            if (!tile.microLabel.empty()) {
                out << "<small><b>" << htmlEscape(tile.microLabel) << "</b> <i id=\"" << id << "-micro\">"
                    << htmlEscape(tile.microValue) << "</i></small>";
            }
            out << "</article>";
        }
        out << "</section><nav class=\"mining-utility-cluster\">"
            << miningModalButton("DETAILS", ui::modals::surface)
            << miningModalButton("INV", ui::modals::inventory)
            << miningModalButton("MENU", ui::modals::settings) << "</nav></header>";
        const MiningCocoonHudLayout cocoonHudLayout = miningCocoonHudLayout(mining.gate);
        if (cocoonMining) {
            out << "<section class=\"mining-cocoon-progress\" aria-label=\"Protected objective layer progress\""
                << " data-cocoon-layer-count=\"" << mining.gate.cocoonLayers.size() << "\""
                << " data-protected-objective-state=\""
                << htmlEscape(miningCocoonObjectiveState(mining)) << "\">";
            for (std::size_t layerIndex = 0; layerIndex < mining.gate.cocoonLayers.size(); ++layerIndex) {
                const MiningCocoonLayerProgress& layer = mining.gate.cocoonLayers[layerIndex];
                const int cleared = std::clamp(layer.total - layer.remaining, 0, std::max(0, layer.total));
                out << "<div id=\"rr-hud-cocoon-layer-" << layerIndex << "\" class=\""
                    << (!layer.revealed ? "is-locked" : (layer.completed ? "is-complete" : "")) << "\">"
                    << "<span id=\"rr-hud-cocoon-label-" << layerIndex << "\">"
                    << htmlEscape(miningCocoonLayerLabel(mining.gate, layerIndex)) << "</span><section>";
                for (int cellIndex = 0; cellIndex < std::max(0, layer.total); ++cellIndex) {
                    out << "<i id=\"rr-hud-cocoon-" << layerIndex << "-" << cellIndex << "\" class=\""
                        << (layer.revealed && cellIndex < cleared ? "is-cleared" : "") << "\"></i>";
                }
                out << "</section><b id=\"rr-hud-cocoon-value-" << layerIndex << "\">"
                    << htmlEscape(miningCocoonLayerValue(mining.gate, layerIndex))
                    << "</b></div>";
            }
            out << "<strong id=\"rr-hud-cocoon-objective-state\">"
                << htmlEscape(miningCocoonObjectiveState(mining)) << "</strong></section>";
        }
        out << "<div class=\"mining-playfield-space\"><aside class=\"mining-eva-readout "
            << (evaActive ? "is-eva" : "is-rig") << "\" aria-label=\"Active mining actor status\">"
            << "<header><span>ACTIVE ACTOR</span><strong id=\"rr-hud-mining-operator-mode\">"
            << htmlEscape(miningOperatorModeLabel(mining)) << "</strong></header>"
            << "<div class=\"mining-eva-status-grid\">"
            << "<span><i>GRAVITY</i><b id=\"rr-hud-mining-gravity\">"
            << htmlEscape(miningGravityLabel(mining)) << "</b></span>"
            << "<span><i id=\"rr-hud-mining-actor-integrity-label\">"
            << htmlEscape(miningActorIntegrityLabel(mining))
            << "</i><b id=\"rr-hud-mining-actor-integrity\">"
            << htmlEscape(display::percent(miningActorIntegrity(mining))) << "</b></span>"
            << "<span><i>DRILL HEAT</i><b id=\"rr-hud-mining-drill-heat\">"
            << htmlEscape(display::percent(mining.drillHeat)) << "</b></span>"
            << "<span><i>TETHER BURDEN</i><b id=\"rr-hud-mining-tether-burden\">"
            << htmlEscape(miningTetherBurdenLabel(mining, miningLoad)) << "</b></span>"
            << "<span><i>LOOSE CHUNKS</i><b id=\"rr-hud-mining-loose-chunks\">"
            << activeMiningLooseChunkCount(mining) << "</b></span>"
            << "<span><i id=\"rr-hud-mining-support-label\">"
            << htmlEscape(miningSupportTileLabel(mining))
            << "</i><b id=\"rr-hud-mining-drone-parent\">"
            << htmlEscape(miningSupportTileValue(mining)) << "</b></span>"
            << "</div><footer><span id=\"rr-hud-mining-suit-carry\">Suit carry: 0</span></footer>"
            << "</aside></div>";
        const int currentDepth = std::max(0, mining.depthZone);
        out << "<div class=\"mining-depth-route-overlay" << (cocoonMining ? " is-cocoon" : "")
            << "\"";
        if (cocoonMining) {
            out << " style=\"--rr-cocoon-route-top: " << cocoonHudLayout.routeTop << "px\"";
        }
        out << ">"
            << "<span id=\"rr-hud-mining-route-up\" class=\"mining-route-up\">"
            << htmlEscape(currentDepth == 0
                    ? std::string("SURFACE \xE2\x80\xA2 SHIP HERE")
                    : std::string("ASCEND \xE2\x80\xA2 SHIP \xE2\x86\x91 ") + std::to_string(currentDepth))
            << "</span><span id=\"rr-hud-mining-route-down\" class=\"mining-route-down\">"
            << htmlEscape(std::string("DESCEND \xE2\x80\xA2 DEPTH +") + std::to_string(currentDepth + 1))
            << "</span></div>";
        const std::array<std::string_view, 2> miningPayloadIds {
            "rr-hud-mining-payload-banked",
            "rr-hud-mining-payload-artifact"
        };
        const std::array<std::string_view, 3> miningOreIds {
            "rr-hud-mining-ore-common",
            "rr-hud-mining-ore-rare",
            "rr-hud-mining-ore-exotic"
        };
        out << "<footer class=\"mining-bottom-rail\"><section class=\"mining-payload-strip ui-kpi-strip rr-metric-strip\">"
            << "<article class=\"mining-ore-manifest\"><header><span>ORE MANIFEST</span><small>"
            << htmlEscape(miningHud.oreManifest.legend) << "</small></header><div class=\"mining-ore-manifest-grid\">";
        for (std::size_t index = 0; index < miningHud.oreManifest.ores.size(); ++index) {
            const MiningHudTilePresentation& ore = miningHud.oreManifest.ores[index];
            out << "<div class=\"mining-ore-entry " << htmlEscape(ore.cssClass) << "\"><span>"
                << htmlEscape(ore.label) << "</span><strong id=\"" << miningOreIds[index] << "\">"
                << htmlEscape(ore.value) << "</strong></div>";
        }
        out << "</div></article>";
        for (std::size_t index = 0; index < miningHud.payload.size(); ++index) {
            const MiningHudTilePresentation& tile = miningHud.payload[index];
            out << "<article class=\"mining-payload-tile " << htmlEscape(tile.cssClass) << "\"><span>"
                << htmlEscape(tile.label) << "</span><strong id=\"" << miningPayloadIds[index] << "\">"
                << htmlEscape(tile.value) << "</strong></article>";
        }
        out << "</section><section class=\"mining-command-dock" << (miningHud.atShip ? " at-ship" : " away")
            << "\"><div class=\"actions action-row system-actions\">";
        for (const PanelButtonPresentation& action : miningHud.actions) {
            out << miningPanelButton(action);
        }
        out << "</div></section></footer>";
        const auto drillRepair = std::find_if(miningRun.actions.begin(), miningRun.actions.end(), [](const PanelButtonPresentation& action) {
            return action.actionId == ui::actions::miningRepairDrill;
        });
        const auto droneRepair = std::find_if(miningRun.actions.begin(), miningRun.actions.end(), [](const PanelButtonPresentation& action) {
            return action.actionId == ui::actions::miningRepairDrone;
        });
        if (miningHud.atShip && drillRepair != miningRun.actions.end() && droneRepair != miningRun.actions.end()) {
            const bool drillVisible = miningDrillRepairCost(mining) > 0;
            const bool disabledRigAtShip =
                mining.rigDisabled && miningRigAtReturnZone(mining);
            const bool droneVisible = disabledRigAtShip
                ? miningDroneRepairCost(mining) > 0
                : (evaActive
                        ? mining.operatorIntegrity < 1.0
                        : miningDroneRepairCost(mining) > 0);
            out << "<div class=\"mining-ship-service-marker\" data-mining-ship-service=\"1\""
                << " data-mining-width=\"" << mining.terrain.width << "\""
                << " data-mining-height=\"" << mining.terrain.height << "\""
                << " data-mining-return-x=\"" << mining.returnZoneX << "\""
                << " data-mining-return-y=\"" << mining.returnZoneY << "\""
                << " data-drill-visible=\"" << (drillVisible ? 1 : 0) << "\""
                << " data-drill-enabled=\"" << (drillRepair->enabled ? 1 : 0) << "\""
                << " data-drill-label=\"" << htmlEscape(drillRepair->label) << "\""
                << " data-drone-visible=\"" << (droneVisible ? 1 : 0) << "\""
                << " data-drone-enabled=\"" << (droneRepair->enabled ? 1 : 0) << "\""
                << " data-drone-label=\"" << htmlEscape(droneRepair->label) << "\"></div>";
        }
        out << "</section>";
        if (miningHud.failurePending && context.miningFailureModalReady) {
            std::ostringstream failureBody;
            failureBody << "<div class=\"phase-advisory danger mining-failure-callout\"><strong>" << htmlEscape(miningHud.failureTitle)
                << "</strong><span>" << htmlEscape(miningHud.failureBody) << "</span></div>";
            failureBody << "<div class=\"modal-actions actions action-row\">"
                << panelButton(panelActionButton("Return to Surface Ops", ui::actions::miningFailureAck, "danger"), true)
                << "</div>";
            out << autoModalTemplate(ui::modals::miningFailure, miningHud.failureTitle, failureBody.str(), false);
        }
        out << modalTemplate(ui::modals::surface, text::panel::modals::surfaceDetails, detailStack(miningHud.details));
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (state.screen == Screen::DroneOps) {
        const DroneOpsPresentation dronePanel = droneOpsPresentation(state, catalog);
        const Destination& droneDestination = currentDestination(state, catalog);
        ScenarioObjectivePresentation droneScenario = scenarioObjectiveForDestination(
            state,
            catalog,
            droneDestination.id);
        if (droneDestination.id == content::destination::mars) {
            const ScenarioObjectivePresentation bayExpansion = scenarioObjectivePresentation(
                state,
                catalog,
                content::scenario::marsBayExpansion,
                "delivery");
            if (bayExpansion.available) {
                droneScenario = bayExpansion;
            }
        }
        const ScenarioDefinition* droneScenarioDefinition = droneScenario.available
            ? findScenarioDefinition(catalog, droneScenario.scenarioId)
            : nullptr;
        const ScenarioStepDefinition* droneScenarioStep = droneScenarioDefinition == nullptr
            ? nullptr
            : findScenarioStepDefinition(*droneScenarioDefinition, droneScenario.stepId);
        const MiningSiteDefinition* requiredSite = droneScenarioStep == nullptr ||
                droneScenarioStep->miningSiteDefinitionId.empty()
            ? nullptr
            : findMiningSiteDefinition(catalog, droneScenarioStep->miningSiteDefinitionId);
        const bool recoveryNeedsHazard = requiredSite != nullptr && std::any_of(
            requiredSite->cocoon.layers.begin(),
            requiredSite->cocoon.layers.end(),
            [](const MiningCocoonLayerDefinition& layer) { return layer.requiredHazardMark > 0; });
        const bool hazardEquipped = std::any_of(
            state.meta.equippedDroneIds.begin(),
            state.meta.equippedDroneIds.end(),
            [&](const std::string& equippedId) {
                const auto found = std::find_if(
                    catalog.miniDrones.begin(),
                    catalog.miniDrones.end(),
                    [&](const MiniDrone& drone) { return drone.id == equippedId; });
                return found != catalog.miniDrones.end() && found->role == MiniDroneRole::Hazard;
            });
        const bool hazardSwapRequired = recoveryNeedsHazard && !hazardEquipped;
        out << "<section class=\"phase-board phase-board-drone-ops drone-workspace\" data-panel-mode=\"drone-workspace\">";
        out << "<div class=\"drone-workspace-toolbar\"><div class=\"drone-workspace-heading\">"
            << "<span class=\"ui-kicker\">" << htmlEscape("MINING SUPPORT WORKSPACE") << "</span>"
            << "<h2>" << htmlEscape("Configure the next loadout") << "</h2>"
            << "<p>" << htmlEscape("Assign owned Support Drone frames or build paid copies into open slots. Every change saves immediately.") << "</p></div>"
            << "<div class=\"utility-row utility-actions drone-workspace-actions\">" << modalButton(text::buttons::details, ui::modals::surface, "ghost")
            << modalButton("Synergies", ui::modals::droneSynergies, "ghost")
            << panelButton(dronePanel.backAction) << "</div></div>";
        std::string droneMissionInstruction = droneScenario.detail;
        if (droneScenarioStep != nullptr &&
            droneScenarioStep->completionEvent == ScenarioEventKind::SafeMaterialDelivered) {
            constexpr std::string_view noSecondDroneRequired = "No second Support Drone is required.";
            const std::string missionQualifier =
                droneScenario.detail.find(noSecondDroneRequired) != std::string::npos
                ? "No second Support Drone is required. // "
                : "";
            const int commonAboard = scenarioCommonAboard(state, droneDestination.id);
            if (droneScenario.state == ScenarioStepState::Complete) {
                droneMissionInstruction = missionQualifier
                    + "OBJECTIVE COMPLETE // The configured reward is claimed. Open slots remain your choice.";
            } else if (commonAboard > 0) {
                droneMissionInstruction = missionQualifier + std::to_string(commonAboard)
                    + " COMMON ABOARD // RETURN TO SURFACE OPS, THEN EXTRACT SAFELY.";
            } else {
                droneMissionInstruction = missionQualifier + "SAFE DELIVERY REQUIRED // Mine "
                    + std::to_string(droneScenario.required)
                    + " Common Ore, then extract it safely.";
            }
        }
        if (droneScenario.available) {
            out << droneMissionStripMarkup(droneScenario, droneMissionInstruction);
        }
        if (hazardSwapRequired) {
            out << "<section class=\"phase-advisory warn scenario-hazard-swap-objective\">"
                << "<strong>RECOVERY LOADOUT // EQUIP HAZARD SUPPORT</strong>"
                << "<span>The active site needs a Hazard Drone. Free a slot, then assign a qualified frame before returning.</span>"
                << "</section>";
        }
        const std::vector<PanelMetricPresentation> droneBayChips {
            dronePanel.metrics.size() > 0 ? dronePanel.metrics[0] : panelMetric("Slots", "0/0"),
            panelMetric("Owned types", dronePanel.metrics.size() > 1 ? dronePanel.metrics[1].value : "0"),
            panelMetric("Common", dronePanel.metrics.size() > 2 ? dronePanel.metrics[2].value : "0"),
            panelMetric("Rare", dronePanel.metrics.size() > 3 ? dronePanel.metrics[3].value : "0"),
            panelMetric("Exotic", dronePanel.metrics.size() > 4 ? dronePanel.metrics[4].value : "0"),
            panelMetric("Next slot", dronePanel.nextSlotCost)
        };
        out << "<div class=\"drone-top-row\">";
        out << "<section class=\"resource-bank drone-bay-strip\"><div class=\"drone-bay-copy\"><span class=\"ui-kicker\">"
            << htmlEscape("BAY STATUS") << "</span><h2>" << htmlEscape("Drone Bay")
            << "</h2><p>" << htmlEscape("Capacity, owned frames, and material reserves.") << "</p></div>"
            << "<div class=\"stat-grid chip-strip drone-bay-stats\">" << resourceChipGrid(droneBayChips) << "</div>"
            << panelButton(dronePanel.upgradeSlotAction) << "</section>";
        out << "</div>";
        out << "<div class=\"drone-workspace-main\">";
        out << "<section class=\"board-primary drone-roster\"><div class=\"section-heading\"><div><span class=\"ui-kicker\">"
            << htmlEscape("AVAILABLE FRAMES") << "</span><h2>" << htmlEscape("Drone controls")
            << "</h2></div><p>" << htmlEscape("Assign owned frames and inspect expedition grafts or synergies.") << "</p></div><div class=\"drone-control-grid drone-controller-choice-row\">";
        for (const MiniDroneCardPresentation& drone : dronePanel.drones) {
            out << miniDroneControlCard(drone);
        }
        out << "</div></section>";
        out << "<section class=\"board-primary drone-loadout-bench\"><div class=\"section-heading\"><div><span class=\"ui-kicker\">"
            << htmlEscape("NEXT DEPLOYMENT") << "</span><h2>" << htmlEscape("Active loadout")
            << "</h2></div><p>" << htmlEscape("These Support Drones deploy with the Mining Rig.") << "</p></div><div class=\"drone-loadout-grid drone-controller-loadout-row\">";
        for (std::size_t slotIndex = 0; slotIndex < dronePanel.loadoutSlots.size(); ++slotIndex) {
            if (slotIndex % 2 == 0) {
                out << "<div class=\"drone-loadout-row\">";
            }
            out << droneLoadoutSlotCard(dronePanel.loadoutSlots[slotIndex]);
            if (slotIndex % 2 == 1 || slotIndex + 1 == dronePanel.loadoutSlots.size()) {
                out << "</div>";
            }
        }
        out << "</div></section></div>";
        out << phaseBoardClose();
        out << modalTemplate(ui::modals::surface, "Drone Ops Details", detailStack(dronePanel.details));
        out << modalTemplate(ui::modals::droneSynergies, "Drone Synergies", droneSynergyModalBody(dronePanel));
        for (const MiniDroneCardPresentation& drone : dronePanel.drones) {
            out << modalTemplate(
                droneDetailsModalId(drone.index),
                drone.title + " Details",
                droneDetailsModalBody(drone));
        }
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (state.screen == Screen::SurfaceScan) {
        const SurfaceExpeditionPresentation surfacePanel = surfaceExpeditionPresentation(state, catalog);
        const SurfaceScanRailPresentation scanPanel = surfaceScanRailPresentation(state);
        const MiningSwarmPreview swarmPreview = miningSwarmPreview(
            state,
            catalog,
            upcomingMiningArenaRules(state, catalog),
            0,
            !state.run.surfaceExpedition.pendingMiningSiteDefinitionId.empty());
        out << phaseBoardOpen("phase-board-surface phase-board-surface-minigame phase-board-scan", state.statusLine);
        out << "<section class=\"surface-scan-rail scan-minigame\">";
        out << "<header class=\"ui-screen-header rr-screen-header scan-header\"><div class=\"scan-heading\"><span class=\"ui-kicker\">"
            << htmlEscape(scanPanel.kicker) << "</span><h2>" << htmlEscape(scanPanel.title) << "</h2></div>"
            << "<div class=\"utility-row scan-utility-actions\">"
            << modalButton("INV", ui::modals::inventory, "ghost")
            << modalButton("MENU", ui::modals::settings, "ghost") << "</div></header>";
        out << "<p class=\"scan-objective\">" << htmlEscape(scanPanel.objective) << "</p>";
        out << "<section class=\"ui-kpi-strip rr-metric-strip scan-kpis\">";
        for (const PanelMetricPresentation& item : scanPanel.metrics) {
            out << "<article class=\"ui-kpi\"><span>" << htmlEscape(item.label) << "</span><strong>"
                << htmlEscape(item.value) << "</strong></article>";
        }
        out << "</section>";
        out << "<section class=\"scan-signal-card\"><div class=\"scan-signal-copy\"><span>SIGNAL</span><strong>"
            << htmlEscape(scanPanel.signal) << "</strong></div><div class=\"scan-signal-track\"><i class=\"scan-signal-fill scan-signal-"
            << std::clamp(((scanPanel.signalPercent + 5) / 10) * 10, 0, 100)
            << "\"></i><b class=\"scan-signal-risk-marker\"></b></div></section>";
        out << "<article class=\"scan-layer-readout " << htmlEscape(scanPanel.layerCssClass) << "\"><strong>"
            << htmlEscape(scanPanel.layerReadout) << "</strong></article>";
        if (swarmPreview.available) {
            out << phaseAdvisory({
                "DANGER: SWARM NEST",
                "Melee, ranged, and armored contacts detected at Depth +" +
                    std::to_string(swarmPreview.depthZone) + ".\n\nSwarm cache \xE2\x80\xA2 Bonus artifact chance: " +
                    display::percent(swarmPreview.artifactChance),
                "danger"
            });
        }
        out << "<div class=\"scan-actions ui-action-bar rr-action-footer\">";
        for (const PanelButtonPresentation& action : scanPanel.actions) {
            out << panelButton(action);
        }
        out << "</div>";
        out << "<div class=\"surface-scan-scene-marker\" data-scan-signal=\"" << htmlEscape(scanPanel.signal)
            << "\"><strong>" << htmlEscape(scanPanel.signal) << "</strong></div>";
        out << "</section>";
        out << phaseBoardClose();
        out << modalTemplate(ui::modals::surface, text::panel::modals::surfaceDetails, detailStack(surfacePanel.details));
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (state.screen == Screen::SurfacePush) {
        const SurfaceExpeditionPresentation surfacePanel = surfaceExpeditionPresentation(state, catalog);
        const SurfacePushRunState& push = state.run.surfacePush;
        const MiningSwarmPreview swarmPreview = miningSwarmPreview(
            state,
            catalog,
            upcomingMiningArenaRules(state, catalog),
            0,
            !state.run.surfaceExpedition.pendingMiningSiteDefinitionId.empty());
        out << phaseBoardOpen("phase-board-surface phase-board-surface-minigame phase-board-push", state.statusLine);
        const int nextDepthOffset = push.steps + 1;
        const SurfaceDepthCapability nextDepthCapability = surfaceDepthCapability(
            state,
            catalog,
            state.run.surfaceExpedition.depth + nextDepthOffset);
        const std::vector<PanelMetricPresentation> pushMetrics {
            panelMetric("Steps", std::to_string(push.steps) + "/" + std::to_string(std::max(1, push.maxSteps))),
            panelMetric("Start depth", "+" + std::to_string(state.run.surfaceExpedition.depth + push.depthGain)),
            panelMetric(
                "Next push risk",
                push.busted
                    ? "ROUTE COLLAPSED"
                    : push.completed
                    ? surfaceDepthBlockerLabel(nextDepthCapability)
                    : push.steps == 0
                    ? "SAFE FIRST LAYER"
                    : display::percent(push.collapseRisk) + " / surveyed")
        };
        const int selectedDepth =
            state.run.surfaceExpedition.depth + push.depthGain;
        const int possibleNextDepth =
            state.run.surfaceExpedition.depth + std::max(push.depthGain, push.steps + 1);
        SurfaceReturnSafetyPresentation returnSafety =
            surfaceReturnSafetyPresentation(state, catalog, selectedDepth);
        if (returnSafety.severity == SurfaceReturnSafetySeverity::Safe &&
            !push.completed && !push.busted) {
            SurfaceReturnSafetyPresentation nextSafety =
                surfaceReturnSafetyPresentation(state, catalog, possibleNextDepth);
            if (nextSafety.severity != SurfaceReturnSafetySeverity::Safe) {
                const SurfaceReturnSafetySeverity nextSeverity = nextSafety.severity;
                returnSafety = std::move(nextSafety);
                returnSafety.title = nextSeverity == SurfaceReturnSafetySeverity::Critical
                    ? "NEXT DIG: RETURN RANGE CRITICAL"
                    : "NEXT DIG: RETURN MARGIN LOW";
                returnSafety.detail +=
                    "\n\nRETURN NOW to set the shallower start depth.";
            }
        }
        std::vector<PanelButtonPresentation> actions;
        if (push.busted) {
            actions.push_back(panelActionButton("Return", ui::actions::surfacePushBank, "ok"));
        } else {
            actions.push_back(push.completed
                ? disabledPanelButton("Route limit reached")
                : panelActionButton(text::buttons::pushDeeper, ui::actions::surfacePushStep, "warn"));
            actions.push_back(push.depthGain > 0
                ? panelActionButton("Set Start Depth", ui::actions::surfacePushBank, "ok")
                : disabledPanelButton("Dig one layer first"));
        }
        out << surfaceMiniGamePanel(
            "push-minigame",
            text::buttons::pushDeeper,
            "Tunnel through surveyed levels within Bore rating and safe return range. The first step is stable; later steps risk collapse.",
            pushMetrics,
            materialRewardChips(push.temporaryMaterials, static_cast<int>(push.temporaryArtifacts.size()), push.cargo),
            push.busted ? "Route Collapse" : (push.completed ? "Deep Route Locked" : "Descent Window"),
            push.message.empty() ? "Dig the first surveyed level, then set that start depth or risk a deeper surveyed tunnel." : push.message,
            actions);
        if (returnSafety.severity != SurfaceReturnSafetySeverity::Safe) {
            out << phaseAdvisory({
                returnSafety.title,
                returnSafety.detail,
                returnSafety.cssClass
            });
        }
        if (swarmPreview.available) {
            out << phaseAdvisory({
                "DANGER BELOW: SWARM NEST",
                "This tunnel opens beside a hostile nest at Depth +" +
                    std::to_string(swarmPreview.depthZone) + ". Mining there is optional; ascend to disengage.\n\nBonus artifact chance: " +
                    display::percent(swarmPreview.artifactChance),
                "danger"
            });
        }
        out << phaseBoardClose();
        out << modalTemplate(ui::modals::surface, text::panel::modals::surfaceDetails, detailStack(surfacePanel.details));
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (state.screen == Screen::SurfaceExpedition) {
        const SurfaceExpeditionPresentation surfacePanel = surfaceExpeditionPresentation(state, catalog);
        const SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
        const ScenarioObjectivePresentation surfaceScenario = scenarioObjectiveForSurface(state, catalog);
        const ScenarioDeliveryPresentation surfaceDelivery =
            scenarioSafeDeliveryPresentation(state, catalog, expedition);
        const bool scenarioSurface = surfaceScenario.available;
        // A fixed scenario mining site still uses Surface Ops' single Mining
        // Rig deployment. Mine remains visible so the core activity is never
        // hidden behind a campaign-specific label; its action routes into the
        // active recovery site.
        const bool scenarioMiningSiteActive = scenarioSurface &&
            surfaceScenario.state == ScenarioStepState::Active &&
            !surfaceScenario.miningSiteDefinitionId.empty();
        const bool scenarioMiningDeploymentSpent = scenarioMiningSiteActive &&
            expedition.miningRunUsed;
        ScenarioObjectivePresentation displayedSurfaceScenario = surfaceScenario;
        if (scenarioMiningDeploymentSpent) {
            displayedSurfaceScenario.detail =
                "This surface loop's Mining Rig deployment is spent. Return to Earth, then land again to retry this recovery.";
            displayedSurfaceScenario.action = ScenarioActionKind::None;
        }
        const bool showMiniDroneIntroduction = context.firstTimeIntroductionsEnabled
            && surfacePanel.droneOpsAction.enabled
            && !ui::briefings::acknowledged(state.meta.acknowledgedActivityBriefingIds, ui::briefings::miniDrones);
        const bool showSurveyIntroduction = expedition.active
            && !surfaceOpsTutorialSurveyComplete(state)
            && !ui::briefings::acknowledged(
                state.meta.acknowledgedActivityBriefingIds,
                ui::briefings::surfaceSurveyIntroduction);
        const bool showDigIntroduction = expedition.active
            && surfaceOpsTutorialDigUnlocked(state)
            && !surfaceOpsTutorialDigComplete(state)
            && !ui::briefings::acknowledged(
                state.meta.acknowledgedActivityBriefingIds,
                ui::briefings::surfaceDigIntroduction);
        const bool showMiningIntroduction = expedition.active
            && surfaceOpsTutorialMiningUnlocked(state)
            && !expedition.miningRunUsed
            && expedition.rigFuel >= 1.0
            && !ui::briefings::acknowledged(state.meta.acknowledgedActivityBriefingIds, ui::briefings::mining);
        out << phaseBoardOpen(
            std::string("phase-board-surface surface-ops-screen rr-fixed-action-stack") +
                (scenarioSurface ? " scenario-surface" : ""),
            state.statusLine);
        out << "<div class=\"rr-fixed-action-context\">";
        out << "<div class=\"phase-titlebar phase-title-row\"><div><h2>" << htmlEscape(text::panel::sections::surfaceExpedition)
            << "</h2><p>" << htmlEscape(scenarioSurface
                ? surfaceScenario.location + " / " + surfacePanel.postureTitle
                : (surfacePanel.metrics.empty() ? std::string("Active site") : surfacePanel.metrics.front().value)
                    + " / " + surfacePanel.postureTitle)
            << "</p></div>";
        out << "<div class=\"utility-row compact-tools utility-actions\">" << modalButton(text::buttons::details, ui::modals::surface, "ghost");
        if (!surfacePanel.logEntries.empty()) {
            out << modalButton(text::panel::sections::missionLog, ui::modals::missionLog, "ghost");
        }
        out << "</div></div>";
        out << surfaceQuickbar(expedition, context.expeditionXpPulse);
        if (scenarioSurface) {
            out << scenarioObjectiveMarkup(displayedSurfaceScenario);
            if (surfaceDelivery.objective.available &&
                surfaceDelivery.objective.state == ScenarioStepState::Active &&
                surfaceDelivery.safelyAboard > 0) {
                const std::string material =
                    scenarioTargetMaterialLabel(surfaceDelivery.objective.eventTargetId);
                const int remaining = std::max(
                    0,
                    surfaceDelivery.objective.required - surfaceDelivery.objective.current);
                out << "<section class=\"phase-advisory caution scenario-extraction-objective\">"
                    << "<strong>ON SHIP // RETURN GUARANTEED</strong><span>"
                    << htmlEscape(
                           std::to_string(surfaceDelivery.safelyAboard) + " " + material +
                           " on Ship. Return will deliver +" +
                           std::to_string(std::min(remaining, surfaceDelivery.safelyAboard)) + ".")
                    << "</span></section>";
            }
        }
        if (surfacePanel.droneOpsAction.enabled) {
            out << "<section class=\"resource-bank drone-ops-callout surface-controller-callout phase-lane phase-row\"><div><h2>" << htmlEscape("Drone Ops")
                << "</h2></div>"
                << introductoryPanelButton(
                    surfacePanel.droneOpsAction,
                    showMiniDroneIntroduction ? ui::modals::miniDroneIntroduction : std::string_view {})
                << "</section>";
        }
        out << "</div>";
        out << "<section class=\"board-primary surface-actions phase-lane primary-actions rr-fixed-action-lane rr-action-footer\">";
        out << "<div class=\"surface-choice-list controller-action-row surface-controller-action-row rr-card-grid\">";
        for (const SurfaceActionPreviewPresentation& action : surfacePanel.actions) {
            const std::string_view introductionModal =
                showSurveyIntroduction && action.action.actionId == ui::actions::surveySurface
                    ? ui::modals::surfaceSurveyIntroduction
                    : (showDigIntroduction && action.action.actionId == ui::actions::pushSurface
                           ? ui::modals::surfaceDigIntroduction
                           : (showMiningIntroduction && isSurfaceMiningAction(action)
                                  ? ui::modals::miningIntroduction
                                  : std::string_view {}));
            out << surfaceActionCard(action, introductionModal);
        }
        out << "</div></section>";
        out << phaseBoardClose();
        out << scenarioObjectiveModal(surfaceScenario);
        if (showMiniDroneIntroduction) {
            out << activityIntroductionModal(
                ui::modals::miniDroneIntroduction,
                "DRONE BAY ONLINE",
                "Support Drones are persistent craft you can equip before a mining run.",
                "Assign owned frames, or build paid copies into open slots, to carry, survey, mine, and protect the expedition.",
                "Open Drone Bay",
                ui::actions::droneOps,
                "warn");
        }
        if (showSurveyIntroduction) {
            out << activityIntroductionModal(
                ui::modals::surfaceSurveyIntroduction,
                "SURVEY THE SITE",
                "Survey scans each reachable level for resources and artifacts before you commit to digging.",
                "Map the current level and level +1, then log the survey. Dig unlocks only after the first deeper level is successfully recorded.",
                "Begin Survey",
                ui::actions::surveySurface,
                "ok");
        }
        if (showDigIntroduction) {
            out << activityIntroductionModal(
                ui::modals::surfaceDigIntroduction,
                "DIG THE TUNNEL",
                "Dig opens a tunnel to the depth you choose. Your Mining Rig begins at the deepest start depth you set.",
                "Every step must be surveyed, within the permanent Bore System rating, and inside a non-critical return range. The first step is stable; later steps can collapse.",
                "Begin Dig",
                ui::actions::pushSurface,
                "warn");
        }
        if (showMiningIntroduction) {
            if (!hasUnlock(state.meta, content::unlock::droneBay)) {
                out << activityIntroductionModal(
                    ui::modals::miningIntroduction,
                    "MINE THE DEPOSIT",
                    "Mine is where all the action is. Take direct control of the Mining Rig to drill ore and recover artifacts from the tunnel you prepared. Bring back " +
                        std::to_string(tuning::research::prospectorCommonOreGoal) +
                        " Common Ore to build Prospector Mk I.",
                    "Mining starts at your selected start depth. Watch oxygen and drill heat, stow cargo at the ship, and leave before the rig can no longer make it home.",
                    "Deploy Mining Rig",
                    ui::actions::mineSurface,
                    "rare");
            } else {
                out << activityIntroductionModal(
                    ui::modals::miningIntroduction,
                    "MINE THE DEPOSIT",
                    "Mine is where all the action is. Take direct control of the Mining Rig to drill ore and recover artifacts from the tunnel you prepared.",
                    "Mining starts at your selected start depth. Watch oxygen and drill heat, stow cargo at the ship, and leave before the rig can no longer make it home.",
                    "Deploy Mining Rig",
                    ui::actions::mineSurface,
                    "rare");
            }
        }
        out << modalTemplate(ui::modals::surface, text::panel::modals::surfaceDetails, detailStack(surfacePanel.details));
        out << modalTemplate(ui::modals::missionLog, text::panel::sections::missionLog, missionLog(surfacePanel.logEntries));
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (state.screen == Screen::SurfaceUpgrade) {
        const SurfaceExpeditionPresentation surfacePanel = surfaceExpeditionPresentation(state, catalog);
        const int pendingPicks = std::max(0, state.run.surfaceExpedition.pendingRunUpgradeChoices);
        const int fanfarePicks = std::max(pendingPicks, context.levelUpBatchChoices);
        std::string levelUpClass = levelUpDraftClass(context);
        levelUpClass.erase(0, std::string("phase-board ").size());
        out << phaseBoardOpen(levelUpClass, state.statusLine, true, "rr-level-up-draft");
        out << "<section class=\"draft-hero level-up-stamp\"><div><span>"
            << htmlEscape(fanfarePicks > 1 ? "LEVEL UP \xC3\x97" + std::to_string(fanfarePicks) : "LEVEL UP")
            << "</span><h2>EXPEDITION LEVEL " << std::max(1, state.run.surfaceExpedition.expeditionLevel)
            << "</h2><p>Choose one upgrade</p><strong>" << pendingPicks << " PICKS REMAIN</strong></div>"
            << expeditionXpMarkup(
                state.run.surfaceExpedition,
                "rr-hud-level-up-xp",
                context.expeditionXpPulse,
                true)
            << "</section>";
        out << "<section class=\"draft-board\"><div class=\"phase-titlebar\"><div><h2>"
            << htmlEscape(surfacePanel.upgradeOffers.empty() ? "ALL ELIGIBLE UPGRADES INSTALLED" : "CHOOSE ONE UPGRADE")
            << "</h2></div>";
        if (!surfacePanel.upgradeOffers.empty()) {
            out << "<div class=\"utility-row compact-tools utility-actions\">"
                << modalButton("Compare", "surface_upgrade_compare", "ghost") << "</div>";
        }
        out << "</div>";
        out << "<div class=\"pilot-card-grid draft-card-grid controller-choice-row\">";
        for (std::size_t index = 0; index < surfacePanel.upgradeOffers.size(); ++index) {
            out << surfaceUpgradeCard(surfacePanel.upgradeOffers[index], index == 0);
        }
        out << "</div>";
        out << "</section>";
        out << phaseBoardClose();
        std::ostringstream surfaceUpgradeComparison;
        surfaceUpgradeComparison << "<div class=\"comparison-list\">";
        for (const SurfaceUpgradeCardPresentation& upgrade : surfacePanel.upgradeOffers) {
            surfaceUpgradeComparison << "<article class=\"comparison-card\"><div class=\"card-topline\"><span>"
                << htmlEscape(upgrade.category) << "</span><span>" << htmlEscape(upgrade.rarity)
                << "</span></div><h3>" << htmlEscape(upgrade.title) << "</h3><p>"
                << htmlEscape(upgrade.detail) << "</p><div class=\"stat-grid chip-strip\">"
                << resourceChipGrid(upgrade.effectChips) << "</div></article>";
        }
        surfaceUpgradeComparison << "</div>";
        out << modalTemplate("surface_upgrade_compare", "Compare Expedition Upgrades", surfaceUpgradeComparison.str());
        out << scenarioObjectiveModalForDestination(state, catalog, currentDestination(state, catalog).id);
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (state.screen == Screen::Upgrade) {
        const RefitWindowPresentation refitWindow = refitWindowPresentation(state, catalog);
        const bool singleLaunchLessonOffer = refitWindow.offers.size() == 1 &&
            refitWindow.offers.front().kind == RefitOfferPresentationKind::LaunchUpgrade;
        const bool marsTransferFuelLesson = singleLaunchLessonOffer &&
            (state.meta.launchLessons.stage == LaunchTrainingStage::ThermalManagement ||
             state.meta.launchLessons.stage == LaunchTrainingStage::MarsTransfer) &&
            launchUpgradeRank(state, LaunchUpgradeKind::FuelTanks) < 2;
        out << phaseBoardOpen("phase-board-refit phase-board-draft-room", state.statusLine);
        out << "<section class=\"draft-hero\"><div><span>" << htmlEscape(marsTransferFuelLesson ? "Mars transfer refit" : "Shipyard refit")
            << "</span><h2>" << htmlEscape(marsTransferFuelLesson ? "Expand fuel capacity" : "Install one upgrade") << "</h2><p>"
            << htmlEscape(marsTransferFuelLesson
                    ? "Mars requires 20 transfer fuel. Current capacity is 15. Spend 22 mission credits on Fuel Tanks II."
                    : singleLaunchLessonOffer
                        ? "Install this required launch upgrade to continue."
                    : "Choose an unlocked system or keep your credits.") << "</p></div>";
        out << "<div class=\"stat-grid chip-strip draft-context\">" << resourceChipGrid(refitWindow.resourceChips) << "</div></section>";
        if (!refitWindow.recoveryDetail.empty()) {
            out << "<p class=\"draft-recovery-note\">" << htmlEscape(refitWindow.recoveryDetail) << "</p>";
        }
        out << "<section class=\"draft-board\"><div class=\"phase-titlebar\"><div><h2>"
            << htmlEscape("AVAILABLE UPGRADES") << "</h2><p>"
            << htmlEscape(singleLaunchLessonOffer
                    ? "Install the required system to continue."
                    : "Install one system or keep the credits.") << "</p></div>";
        if (!singleLaunchLessonOffer) {
            out << "<div class=\"utility-row compact-tools utility-actions\">"
                << modalButton("Compare", "refit_compare", "ghost") << "</div>";
        }
        out << "</div><div class=\"pilot-card-grid draft-card-grid controller-choice-row "
            << (singleLaunchLessonOffer ? "single-refit-offer" : "multi-refit-offers")
            << "\">";
        // Ordinary Refit purchases spend credits and need deliberate focus.
        // A single curated tutorial offer is the sole safe default.
        const auto defaultRefit = singleLaunchLessonOffer
            ? std::find_if(
                  refitWindow.offers.begin(),
                  refitWindow.offers.end(),
                  [](const RefitOfferPresentation& offer) { return offer.action.enabled; })
            : refitWindow.offers.end();
        const std::size_t defaultRefitIndex = defaultRefit == refitWindow.offers.end()
            ? refitWindow.offers.size()
            : static_cast<std::size_t>(std::distance(refitWindow.offers.begin(), defaultRefit));
        for (std::size_t index = 0; index < refitWindow.offers.size(); ++index) {
            out << refitOfferCard(refitWindow.offers[index], index == defaultRefitIndex);
        }
        out << "</div>";
        out << "<div class=\"actions action-row draft-actions controller-action-row\">";
        if (refitWindow.showReroll) {
            out << panelButton(refitWindow.rerollAction);
        }
        if (refitWindow.showSkip) {
            out << panelButton(refitWindow.skipAction);
        }
        out << "</div></section>";
        out << phaseBoardClose();
        if (!singleLaunchLessonOffer) {
            std::ostringstream refitComparison;
            refitComparison << "<div class=\"comparison-list\">";
            for (std::size_t index = 0; index < refitWindow.offers.size(); ++index) {
                if (index > 0) {
                    refitComparison << "<div class=\"refit-comparison-divider\" aria-hidden=\"true\"></div>";
                }
                const RefitOfferPresentation& offer = refitWindow.offers[index];
                const RefitPresentation& refit = offer.card;
                refitComparison << "<article class=\"comparison-card refit-comparison-card\"><div class=\"card-topline\"><span>"
                    << htmlEscape(refit.category) << "</span><span>" << htmlEscape(refit.rarity)
                    << "</span></div><h3>" << htmlEscape(refit.title) << "</h3><p>"
                    << htmlEscape(refit.detail) << "</p><strong class=\"module-impact\">"
                    << htmlEscape(refit.primaryImpact) << "</strong><div class=\"stat-grid chip-strip\">"
                    << statChipGrid(refit.statChips) << "</div><span class=\"comparison-cost\">"
                    << htmlEscape(offer.footerCostSummary) << "</span></article>";
            }
            refitComparison << "</div>";
            out << modalTemplate("refit_compare", "Compare Permanent Refits", refitComparison.str());
        }
        out << scenarioObjectiveModalForDestination(state, catalog, currentDestination(state, catalog).id);
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    const std::string shipBody = detailStack(shipDetailsPresentation(state, catalog));
    const std::string crewBody = detailStack(crewDetailsPresentation(state, catalog));
    const std::string frontierBody = detailStack(frontierDetailsPresentation(state, catalog));

    std::ostringstream launchBlockedBody;
    for (const std::string& message : launchReadiness.messages) {
        launchBlockedBody << "<p class=\"status\">" << htmlEscape(message) << "</p>";
    }
    const bool launchHardwareBlocked =
        state.meta.launchLessons.stage != LaunchTrainingStage::Complete &&
        !launchMissionReady(state, catalog);
    if (launchHardwareBlocked) {
        const auto [title, detail] = launchLessonHangarObjective(state, catalog);
        launchBlockedBody << "<p class=\"status\"><strong>" << htmlEscape(title)
            << "</strong><br>" << htmlEscape(detail) << "</p>";
        launchBlockedBody << "<div class=\"modal-actions actions action-row\">"
            << modalButton("Open details", ui::modals::hangarDetails, "ok")
            << "</div>";
    }
    std::vector<DetailPresentationRow> launchBlockedDetails = launchReadiness.details;
    if (launchHardwareBlocked && !launchReadiness.blocked && !launchBlockedDetails.empty()) {
        const auto [title, detail] = launchLessonHangarObjective(state, catalog);
        static_cast<void>(detail);
        launchBlockedDetails.back() = detailPresentationRow(
            text::panel::details::requiredAction,
            title);
    }
    launchBlockedBody << detailStack(launchBlockedDetails);
    launchBlockedBody << "<div class=\"modal-actions actions action-row\">";
    for (const PanelButtonPresentation& action : launchReadiness.actions) {
        if (astronaut == nullptr && action.actionId == ui::actions::recruitCrew) {
            launchBlockedBody << modalButton("Choose pilot", ui::modals::pilotIntake, action.cssClass);
        } else {
            launchBlockedBody << panelButton(action);
        }
    }
    launchBlockedBody << "</div>";

    std::ostringstream pilotIntakeBody;
    pilotIntakeBody << "<p class=\"modal-intro\">Choose one specialist for the next launch window.</p>";
    pilotIntakeBody << "<div class=\"pilot-card-grid\">";
    const std::vector<const Astronaut*> pilotCandidates = recruitCandidateTemplates(state, catalog);
    const HangarOperationPreview hangarPreview = hangarOperationPreview(state, catalog);
    for (int index = 0; index < static_cast<int>(pilotCandidates.size()); ++index) {
        pilotIntakeBody << pilotCandidateCard(*pilotCandidates[static_cast<std::size_t>(index)], index, hangarPreview.recruitAvailable);
    }
    pilotIntakeBody << "</div>";

    out << phaseBoardOpen("phase-board-hangar", state.statusLine);
    out << "<h2>" << htmlEscape(text::panel::sections::hangarBay) << "</h2>";
    const std::vector<PanelMetricPresentation> hangarFuelMetrics {
        panelMetric("Transfer Tank", display::fixed(launchFuelCapacity(state), 0) + " fuel"),
        panelMetric("Rig fuel", display::fixed(tuning::research::expeditionRigPackFuel, 0) + " fuel"),
        panelMetric("Return fuel", "Full")
    };
    out << "<div class=\"stat-grid chip-strip phase-lane hangar-fuel-strip\">";
    for (const PanelMetricPresentation& metric : hangarFuelMetrics) {
        out << hangarFuelChip(metric);
    }
    out << "</div>";
    const FrontierGateStatus nextFrontierGate = next == nullptr
        ? FrontierGateStatus {}
        : frontierGateStatus(state, catalog);
    const bool showJupiterOptions = jupiterWindowAvailable(state, catalog);
    if (showJupiterOptions) {
        const double tank = launchFuelCapacity(state);
        const double routeBurn = launchCruiseFuelCostForTier(3);
        const PendingTransferAssist* activeAssist = pendingTransferAssistForDestination(state, content::destination::jupiter);
        const double savings = activeAssist != nullptr
            ? activeAssist->fuelSavings
            : 0.0;
        const double instabilityPenalty = activeAssist != nullptr
            ? activeAssist->instabilityPenalty
            : 0.0;
        const double poweredBurn = std::max(0.0, routeBurn - savings);
        const double margin = tank - poweredBurn;
        const bool tanksThree = launchUpgradeRank(state, LaunchUpgradeKind::FuelTanks) >= 3;
        const int filledSegments = std::clamp(
            static_cast<int>(std::floor(std::max(0.0, margin) + 0.000001)),
            0,
            10);
        out << "<section class=\"hangar-frontier-readiness phase-lane\" data-jupiter-options=\"1\" aria-label=\"Jupiter transfer margin "
            << htmlEscape(display::fixed(margin, 0) + " fuel; 5 required") << "\">"
            << "<div class=\"hangar-frontier-head\"><div><span>NEXT FRONTIER</span><strong>JUPITER OPTIONS</strong></div>"
            << "<div><b>" << htmlEscape(display::signedFixed(margin, 0))
            << " MARGIN</b><small>5 REQUIRED</small></div></div>"
            << "<div class=\"hangar-frontier-contributors\">"
            << "<div><span>FUEL TANKS III</span><strong>" << display::fixed(tank, 0)
            << " tank</strong><small>" << (tanksThree ? "+5 permanent capacity // INSTALLED" : "+5 permanent capacity // 92 credits")
            << "</small></div>"
            << "<div><span>MARS SLINGSHOT</span><strong>";
        if (activeAssist != nullptr) {
            out << display::fixed(poweredBurn, 0) << " powered burn</strong><small>ACTIVE // +"
                << display::percent(activeAssist->speedBoost) << " velocity from finish // "
                << (instabilityPenalty > 0.0
                    ? "+" + display::percent(instabilityPenalty) + " instability"
                    : "Perfect: stable");
        } else {
            out << "Good-or-better Flyby</strong><small>-5 powered fuel // +0–40% from finish speed // Good: +35% instability";
        }
        out << "</small></div></div><div class=\"hangar-frontier-meter\" aria-hidden=\"true\">";
        for (int segment = 0; segment < 10; ++segment) {
            out << "<i class=\"" << (segment < filledSegments ? "is-filled " : "")
                << (segment == 4 ? "is-required-edge" : "") << "\"></i>";
        }
        out << "</div><div class=\"hangar-frontier-foot\"><p>Either option creates the required margin. Both stack to 10 margin and slingshot velocity.</p>"
            << modalButton("Review options", ui::modals::jupiterWindow, "ghost")
            << "</div></section>";
    } else if (state.run.routeTransit.active()) {
        const Destination* routeOrigin = catalog.findDestination(state.run.routeTransit.originDestinationId);
        const Destination* routeTarget = catalog.findDestination(state.run.routeTransit.targetDestinationId);
        const std::string routeLabel = std::string(routeOrigin == nullptr ? "Staging" : routeOrigin->name) +
            " \xE2\x86\x92 " + (routeTarget == nullptr ? "destination" : routeTarget->name);
        if (state.run.routeTransit.intent == RouteTransitIntent::Recovery) {
            out << phaseAdvisory({"RECOVERY ROUTE", routeLabel + ". Complete the return flight to regain a safe staging point.", "warning"});
        } else if (state.run.routeTransit.intent == RouteTransitIntent::Reapproach) {
            out << phaseAdvisory({"REAPPROACH", routeLabel + ". The prior objective remains active.", "info"});
        } else {
            out << phaseAdvisory({"TRANSFER ROUTE", routeLabel + ".", "info"});
        }
    } else if (state.meta.launchLessons.stage != LaunchTrainingStage::Complete) {
        const auto [title, detail] = launchLessonHangarObjective(state, catalog);
        out << "<section class=\"objective-strip rr-objective-strip phase-lane\"><span>Objective</span><strong>"
            << htmlEscape(title) << "</strong><p>" << htmlEscape(detail) << "</p></section>";
    } else {
        out << scenarioObjectiveMarkup(
            scenarioObjectiveForDestination(state, catalog, currentFrontier.id));
    }
    out << "<h2>" << htmlEscape(text::panel::sections::hangarOps) << "</h2>";
    out << "<div class=\"ops-grid rr-card-grid controller-choice-row hangar-controller-choice-row\">";
    for (const HangarOperationCardPresentation& card : hangarOperationCards(state, catalog)) {
        if (astronaut == nullptr && card.actionId == ui::actions::recruitCrew) {
            out << operationModalCard(card, "Choose pilot", ui::modals::pilotIntake);
        } else {
            out << operationCard(card);
        }
    }
    out << "</div>";

    const bool showLaunchIntroduction = context.firstTimeIntroductionsEnabled
        && launchTarget.id == content::destination::moon
        && state.meta.launchLessons.stage == LaunchTrainingStage::FuelCalibration
        && !ui::briefings::acknowledged(state.meta.acknowledgedActivityBriefingIds, ui::briefings::launch);
    const bool showFlightControlsIntroduction = context.firstTimeIntroductionsEnabled
        && state.meta.launchLessons.stage == LaunchTrainingStage::FlightControlsCalibration
        && launchUpgradeRank(state, LaunchUpgradeKind::FuelTanks) >= 1
        && !ui::briefings::acknowledged(
            state.meta.acknowledgedActivityBriefingIds,
            ui::briefings::flightControlsCalibration);
    const ScenarioObjectivePresentation departureChallenge =
        scenarioDepartureChallengeForDestination(state, catalog, launchTarget.id);
    const ScenarioObjectivePresentation launchScenario =
        scenarioObjectiveForDestination(state, catalog, launchTarget.id);
    const bool saturnCourseReady =
        canClaimSaturnCourse(state);
    const bool scenarioRouteClaimReady =
        scenarioClaimQueuesRoute(state, catalog, launchScenario);

    std::string prepareLaunchLabel = "Launch: " + launchTarget.name;
    if (state.run.routeTransit.active()) {
        const Destination* origin = catalog.findDestination(state.run.routeTransit.originDestinationId);
        const std::string route = (origin == nullptr ? std::string("Staging") : origin->name) +
            " \xE2\x86\x92 " + launchTarget.name;
        if (state.run.routeTransit.intent == RouteTransitIntent::Recovery) {
            prepareLaunchLabel = "Begin Recovery: " + route;
        } else if (state.run.routeTransit.intent == RouteTransitIntent::Reapproach) {
            prepareLaunchLabel = "Reapproach: " + launchTarget.name;
        } else {
            prepareLaunchLabel = "Transfer: " + route;
        }
    } else if (departureChallenge.available) {
        prepareLaunchLabel =
            (departureChallenge.action == ScenarioActionKind::RetryActivity ||
             departureChallenge.activityStarted)
            ? "Retry"
            : "Launch";
    }
    const bool prepareLaunchBlocked =
        launchReadiness.blocked || !currentDestinationLaunchReady(state, catalog);
    out << "<div class=\"actions action-row rr-action-footer hangar-actions controller-action-row hangar-controller-action-row primary-actions\">";
    if (navigationAvailable(state)) {
        out << button("Open Navigation", ui::actions::openNavigation, "warn");
    }
    if (arkDiscovered(state) && !hostileSystemActive(state)) {
        out << button(state.meta.ark.firstJumpComplete ? "Attempt next Ark jump" : "Make first Ark jump", ui::actions::arkJump, "warn");
    }
    if (saturnCourseReady) {
        out << button(
            "Lock Saturn Course",
            ui::actions::claimSaturnCourse,
            "ok hangar-launch-prep",
            true);
    } else if (scenarioRouteClaimReady) {
        out << scenarioActionButton(
            launchScenario,
            "ok hangar-launch-prep",
            true);
    } else if (pendingTransferAssistForDestination(state, launchTarget.id) != nullptr) {
        out << button("Continue to Jupiter", ui::actions::continueTransferAssist, "ok", true);
    } else {
        out << (prepareLaunchBlocked
            ? modalButton(prepareLaunchLabel, ui::modals::launchBlocked, "ok hangar-launch-prep", true)
            : (showLaunchIntroduction
                ? modalButton(prepareLaunchLabel, ui::modals::launchIntroduction, "ok hangar-launch-prep", true)
                : (showFlightControlsIntroduction
                    ? modalButton(prepareLaunchLabel, ui::modals::flightControlsIntroduction, "ok hangar-launch-prep", true)
                    : button(prepareLaunchLabel, ui::actions::prepareLaunch, "ok hangar-launch-prep", true))));
    }
    if (showJupiterOptions && pendingTransferAssistForDestination(state, content::destination::jupiter) == nullptr) {
        out << (canStartJupiterSlingshot(state, catalog)
            ? button("Begin Mars Slingshot", ui::actions::beginTransferAssist(content::transferAssist::marsJupiter), "warn")
            : disabledButton("Mars Slingshot Unavailable"));
        if (jupiterTransferMarginReady(state)) {
            out << button("Transfer: Jupiter", ui::actions::attemptFrontier, "danger");
        }
    } else if (next != nullptr && !navigationAvailable(state) && !currentFrontier.hiddenFromProgression &&
               pendingTransferAssistForDestination(state, next == nullptr ? std::string_view{} : next->id) == nullptr) {
        if (!nextFrontierGate.satisfied) {
            out << disabledButton(next->name + ": " + std::string(text::buttons::unavailable));
        } else if (state.meta.launchLessons.stage != LaunchTrainingStage::Complete) {
            out << (launchHardwareBlocked
                ? modalButton(text::panel::attemptFrontier(next->name), ui::modals::launchBlocked, "danger")
                : button(text::panel::attemptFrontier(next->name), ui::actions::attemptFrontier, "danger"));
        } else if (canCommitToNextFrontier(state, catalog)) {
            const bool oneWayCommit = next->oneWayExpedition;
            out << (launchReadiness.blocked
                ? modalButton(text::panel::attemptFrontier(next->name), ui::modals::launchBlocked, "danger")
                : (oneWayCommit
                      ? modalButton("Commit to " + next->name, "one_way_launch_confirm", "danger")
                      : button(text::panel::attemptFrontier(next->name), ui::actions::attemptFrontier, "danger")));
        }
    }
    out << "</div>";

    const std::string legacyBody = detailStack(legacyDetailsPresentation(state, catalog));
    const std::string hangarDetailsBody =
        "<div class=\"modal-actions action-row hangar-details-menu\">" +
        modalButton("Ship details", ui::modals::ship, "ghost") +
        modalButton("Crew details", ui::modals::crew, "ghost") +
        modalButton("Frontier details", ui::modals::frontier, "ghost") +
        modalButton(text::buttons::legacy, ui::modals::legacy, "ghost") +
        "</div>";

    out << phaseBoardClose();
    out << modalTemplate(ui::modals::hangarDetails, "Details", hangarDetailsBody);
    out << modalTemplate(ui::modals::ship, text::panel::modals::shipDetails, shipBody);
    out << modalTemplate(ui::modals::crew, text::panel::modals::crewDetails, crewBody);
    out << modalTemplate(ui::modals::frontier, text::panel::modals::frontierDetails, frontierBody);
    out << modalTemplate(ui::modals::launchBlocked, text::panel::modals::launchHold, launchBlockedBody.str());
    out << modalTemplate(ui::modals::pilotIntake, text::panel::modals::pilotIntake, pilotIntakeBody.str());
    if (next != nullptr && next->oneWayExpedition && canCommitToNextFrontier(state, catalog)) {
        const std::string oneWayLaunchBody =
            "<section class=\"activity-introduction modal-body campaign-briefing\">"
            "<span class=\"activity-introduction-kicker\">ONE-WAY OUTER EXPEDITION</span>"
            "<p class=\"activity-introduction-setup\">The required transfer solution is complete. Crossing this window commits the expedition outward.</p>"
            "<div class=\"activity-introduction-payoff\"><span>Point of no return</span><strong>The inner planets will no longer be reachable after departure.</strong></div>"
            "<div class=\"modal-actions action-row rr-action-footer activity-introduction-actions\">" +
            button("Lock course for " + next->name, ui::actions::attemptFrontier, "danger", true) +
            "</div></section>";
        out << modalTemplate("one_way_launch_confirm", "CONFIRM OUTER COURSE", oneWayLaunchBody);
    }
    if (showLaunchIntroduction && !launchReadiness.blocked) {
        out << activityIntroductionModal(
            ui::modals::launchIntroduction,
            "FIRST FLIGHT BRIEF",
            "The Moon is out of range. This ship carries 10 fuel; the lunar route needs 15.",
            "Fly until the FUEL warning appears. Turn Around any time before the tank reaches 0; the return burn is protected. Fuel Tanks and Flight Controls are the two upgrades needed before the Moon transfer.",
            "Begin fuel test",
            ui::actions::prepareLaunch,
            "ok");
    }
    if (showFlightControlsIntroduction && !launchReadiness.blocked) {
        out << activityIntroductionModal(
            ui::modals::flightControlsIntroduction,
            "FLIGHT CONTROLS TEST",
            "The Moon is reachable, but the landing navigation system has not been calibrated.",
            "Fly to the yellow test line, correct the unstable steering, then Turn Around. Do not continue to the Moon: without Flight Controls I, the ship will impact the surface.",
            "Begin test flight",
            ui::actions::prepareLaunch,
            "ok");
    }
    if (!currentFrontier.hiddenFromProgression) {
        out << scenarioObjectiveModalForDestination(state, catalog, currentFrontier.id);
    }
    out << jupiterWindowModal(state, catalog);
    out << jupiterSlingshotActiveModal(state, catalog);
    out << modalTemplate(ui::modals::legacy, text::panel::modals::legacy, legacyBody);
    out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
    out << inventoryTemplate(state, catalog);

    return out.str();
}

namespace {

PanelTemplateKind templateKindForContext(const PanelRenderContext& context)
{
    if (context.titleScreenActive) {
        return PanelTemplateKind::LegacyRaw;
    }

    switch (context.state.screen) {
    case Screen::Hangar:
        return PanelTemplateKind::Workspace;
    case Screen::Flyby:
        return context.state.run.flyby.completed
            ? PanelTemplateKind::LegacyRaw
            : PanelTemplateKind::ControlPanel;
    case Screen::SurfaceScan:
    case Screen::SurfacePush:
        return PanelTemplateKind::SurfaceMinigame;
    case Screen::Mining:
        return PanelTemplateKind::Mining;
    case Screen::StoryBriefing:
        return PanelTemplateKind::Takeover;
    case Screen::Results:
        return PanelTemplateKind::Results;
    default:
        return PanelTemplateKind::LegacyRaw;
    }
}

bool legacyContentOwnsLaneGeometry(const PanelRenderContext& context)
{
    switch (context.state.screen) {
    case Screen::Hangar:
    case Screen::SurfaceScan:
    case Screen::SurfacePush:
    case Screen::Mining:
    case Screen::StoryBriefing:
    case Screen::Results:
        return true;
    case Screen::Flyby:
        return !context.state.run.flyby.completed;
    default:
        return false;
    }
}

PanelSurfaceKind surfaceKindForScreen(Screen screen)
{
    switch (screen) {
    case Screen::SurfaceExpedition:
        return PanelSurfaceKind::SurfaceOps;
    case Screen::SurfaceUpgrade:
        return PanelSurfaceKind::SurfaceUpgrade;
    case Screen::SurfaceScan:
        return PanelSurfaceKind::SurfaceScan;
    case Screen::SurfacePush:
        return PanelSurfaceKind::SurfacePush;
    case Screen::Mining:
        return PanelSurfaceKind::Mining;
    case Screen::DroneOps:
        return PanelSurfaceKind::DroneOps;
    default:
        return PanelSurfaceKind::None;
    }
}

PanelInteractionMode interactionModeForContext(const PanelRenderContext& context)
{
    const GameState& state = context.state;
    switch (state.screen) {
    case Screen::Launch:
    case Screen::Mining:
        return PanelInteractionMode::Realtime;
    case Screen::SurfaceScan:
        return state.run.surfaceScan.active
                && !state.run.surfaceScan.completed
                && !state.run.surfaceScan.busted
            ? PanelInteractionMode::Realtime
            : PanelInteractionMode::Standard;
    case Screen::SurfacePush:
        return state.run.surfacePush.active
                && !state.run.surfacePush.completed
                && !state.run.surfacePush.busted
            ? PanelInteractionMode::Realtime
            : PanelInteractionMode::Standard;
    case Screen::Flyby:
        return state.run.flyby.completed ? PanelInteractionMode::Takeover : PanelInteractionMode::Realtime;
    case Screen::Orbit:
        return state.run.orbit.completed ? PanelInteractionMode::Takeover : PanelInteractionMode::Realtime;
    case Screen::StoryBriefing:
    case Screen::Results:
    case Screen::ArrivalFanfare:
        return PanelInteractionMode::Takeover;
    default:
        return PanelInteractionMode::Standard;
    }
}

PanelOverlayKind overlayKindForContext(const PanelRenderContext& context)
{
    if (context.titleScreenActive) {
        return PanelOverlayKind::None;
    }

    if (context.state.screen == Screen::Launch) {
        return context.flightArmed
            ? PanelOverlayKind::FlightInstruments
            : PanelOverlayKind::PreflightLaunch;
    }
    if ((context.state.screen == Screen::Flyby || context.state.screen == Screen::Orbit) &&
        flightInstrumentsForContext(context).visible) {
        return PanelOverlayKind::FlightInstruments;
    }
    if (context.state.screen == Screen::Results) {
        return PanelOverlayKind::TelemetryLegend;
    }
    if (context.state.screen == Screen::SurfaceScan) {
        return PanelOverlayKind::SurfaceScanReadout;
    }
    if (context.state.screen == Screen::Mining) {
        return PanelOverlayKind::MiningExperience;
    }
    return PanelOverlayKind::None;
}

std::string variantForContext(const PanelRenderContext& context)
{
    if (context.titleScreenActive) {
        return "title";
    }

    const GameState& state = context.state;
    switch (state.screen) {
    case Screen::Hangar:
        return "hangar";
    case Screen::Launch:
        return context.flightArmed ? "flight" : "preflight";
    case Screen::Results:
        return "launch-results";
    case Screen::ArrivalFanfare:
        return "arrival-fanfare";
    case Screen::ArrivalOps:
        return "arrival-ops";
    case Screen::Flyby:
        return state.run.flyby.completed ? "flyby-result" : "flyby-active";
    case Screen::Orbit:
        return state.run.orbit.completed ? "orbit-result" : "orbit-active";
    case Screen::Research:
        return "research";
    case Screen::SurfaceExpedition:
        return "surface-ops";
    case Screen::SurfaceUpgrade:
        return "surface-upgrade";
    case Screen::SurfaceScan:
        return "surface-scan";
    case Screen::SurfacePush:
        return "surface-push";
    case Screen::Mining:
        return miningOperatorIsEva(state.run.mining) ? "mining-eva" : "mining-rig";
    case Screen::Upgrade:
        return "refit";
    case Screen::Legacy:
        return "legacy";
    case Screen::DroneOps:
        return "drone-ops";
    case Screen::Navigation:
        return "navigation";
    case Screen::StoryBriefing:
        return state.storyBriefing.pending == StoryBriefingId::StraylightDiscovery
            ? "straylight-discovery"
            : "campaign-introduction";
    }
    return "unknown";
}

bool usesResponsiveViewport(const PanelRenderContext& context)
{
    switch (context.state.screen) {
    case Screen::Launch:
    case Screen::Flyby:
    case Screen::Orbit:
    case Screen::SurfaceScan:
    case Screen::SurfacePush:
        return true;
    default:
        return false;
    }
}

bool usesGameplayInputHelper(const PanelRenderContext& context)
{
    if (context.state.screen == Screen::Mining) {
        return true;
    }
    if (context.state.screen == Screen::Flyby) {
        return !context.state.run.flyby.completed;
    }
    if (context.state.screen == Screen::Orbit) {
        return !context.state.run.orbit.completed;
    }
    return false;
}

} // namespace

PanelDocumentPresentation buildGamePanelPresentation(const PanelRenderContext& context)
{
    PanelDocumentPresentation result;
    result.templateKind = templateKindForContext(context);
    result.metadata.screen = context.state.screen;
    result.metadata.visualFamily = panelVisualFamily(context.state.screen);
    result.metadata.layoutMode = panelLayoutMode(context.state.screen);
    result.metadata.surface = surfaceKindForScreen(context.state.screen);
    result.metadata.interaction = interactionModeForContext(context);
    result.metadata.overlay = overlayKindForContext(context);
    result.metadata.legacyContentOwnsLaneGeometry =
        result.templateKind != PanelTemplateKind::LegacyRaw
        && legacyContentOwnsLaneGeometry(context);
    result.metadata.variant = variantForContext(context);

    result.runtime.titleScreen = context.titleScreenActive;
    result.runtime.responsiveViewport = usesResponsiveViewport(context);
    result.runtime.gameplayInputHelper = usesGameplayInputHelper(context);
    result.runtime.preflightReady = context.preflightReady;
    result.runtime.launchQueued = context.launchQueued;
    result.runtime.miningEvaActive =
        context.state.screen == Screen::Mining && miningOperatorIsEva(context.state.run.mining);
    if (context.state.screen == Screen::Mining) {
        const SurfaceExpeditionState& expedition = context.state.run.surfaceExpedition;
        const int required = static_cast<int>(std::ceil(std::max(
            1.0,
            expeditionExperienceThreshold(expedition.expeditionLevel))));
        result.runtime.expeditionLevel = std::max(1, expedition.expeditionLevel);
        result.runtime.expeditionExperienceRequired = required;
        result.runtime.expeditionExperienceCurrent = std::clamp(
            static_cast<int>(std::floor(expedition.expeditionExperience + 0.0001)),
            0,
            required);
        result.runtime.expeditionExperienceFilledSegments = expeditionXpFilledSegments(expedition);
        result.runtime.expeditionPendingPicks = std::max(0, expedition.pendingRunUpgradeChoices);
        result.runtime.expeditionXpPulse = context.expeditionXpPulse;
    }
    if (context.state.screen == Screen::Mining) {
        const MiningHudPresentation miningHud = miningHudPresentation(context.state, context.catalog);
        const auto hasAction = [&miningHud](std::string_view actionId) {
            return std::any_of(
                miningHud.actions.begin(),
                miningHud.actions.end(),
                [actionId](const PanelButtonPresentation& action) {
                    return action.actionId == actionId;
                });
        };
        result.runtime.miningTetherAvailable = hasAction(ui::actions::miningTether);
        result.runtime.miningStowAvailable = hasAction(ui::actions::miningStow);
        result.runtime.miningAbortAvailable = hasAction(ui::actions::miningAbort);
    }
    if (result.metadata.overlay == PanelOverlayKind::SurfaceScanReadout) {
        result.runtime.overlayValue = display::percent(context.state.run.surfaceScan.signal);
    }
    if (result.metadata.overlay == PanelOverlayKind::FlightInstruments) {
        const FlightInstrumentPresentation instruments = flightInstrumentsForContext(context);
        result.runtime.instrumentSpeedValue = instruments.speedValue;
        result.runtime.instrumentTemperatureValue = instruments.temperatureValue;
        result.runtime.instrumentFuelValue = instruments.fuelValue;
        result.runtime.instrumentThrottleValue = instruments.throttleValue;
        result.runtime.instrumentTemperatureCritical = instruments.temperatureCritical;
        result.runtime.instrumentOffCourse = instruments.offCourse;
        result.runtime.instrumentCourseCritical = instruments.courseCritical;
    }

    result.contentMarkup = buildGamePanelMarkup(context, result.modals);
    result.runtime.sceneTransitionActive = context.sceneFadeToBlack > 0.0;
    if (context.sceneFadeToBlack > 0.0) {
        // The renderer owns a full-frame clip-space blackout pass. Do not
        // imitate it with an RmlUi rectangle: its document viewport can be
        // smaller than the actual native/Deck framebuffer after a resize.
        // Removing panel content during the handoff leaves the camera fade as
        // the sole transition surface, above no stale HUD or control rail.
        result.contentMarkup.clear();
        result.modals.clear();
    }
    const bool canReviewResearchBreakthrough = !context.titleScreenActive
        && (context.state.screen == Screen::ArrivalOps
            || context.state.screen == Screen::Hangar
            || context.state.screen == Screen::Navigation
            || context.state.screen == Screen::Upgrade
            || context.state.screen == Screen::Research
            || context.state.screen == Screen::SurfaceExpedition);
    const bool hasBlockingAutoModal = std::any_of(result.modals.begin(), result.modals.end(), [](const ModalPresentation& modal) {
        return modal.autoOpen;
    });
    if (canReviewResearchBreakthrough && !hasBlockingAutoModal) {
        if (const tuning::unlocks::BlueprintUnlock* milestone = pendingResearchBreakthrough(context.state)) {
            const std::string action = std::string(ui::actions::acknowledgeResearchBreakthroughPrefix)
                + std::string(milestone->key);
            std::ostringstream body;
            body << "<section class=\"activity-introduction modal-body research-breakthrough\">"
                << "<span class=\"activity-introduction-kicker\">RESEARCH DATA " << milestone->threshold << " REACHED</span>"
                << "<p class=\"activity-introduction-setup\">" << htmlEscape(unlockDisplayName(milestone->key))
                << " is now eligible to appear in future Refit offers.</p>"
                << "<div class=\"activity-introduction-payoff\"><span>What changed</span><strong>"
                << htmlEscape("This unlock expands the offer pool; no module was granted or installed automatically. "
                    + researchDataMilestoneLabel(context.state.meta.blueprintProgress))
                << "</strong></div><div class=\"modal-actions action-row rr-action-footer\">"
                << button("REVIEWED", action, "ok", true) << "</div></section>";
            result.modals.push_back(ModalPresentation {
                "research_breakthrough_" + std::string(milestone->key),
                "RESEARCH BREAKTHROUGH",
                body.str(),
                action,
                true,
                false,
                false,
                ModalTone::Positive
            });
        }
    }
    return result;
}

void buildRealtimeHudState(const PanelRenderContext& context, RealtimeHudState& result)
{
    const GameState& state = context.state;
    const ContentCatalog& catalog = context.catalog;
    result.patches.clear();
    result.patches.reserve(80);

    const FlightInstrumentPresentation instruments = flightInstrumentsForContext(context);
    if (instruments.visible) {
        appendHudText(result, "rr-flight-speed-value", instruments.speedValue);
        appendHudText(result, "rr-flight-temperature-value", instruments.temperatureValue);
        appendHudText(result, "rr-flight-fuel-value", instruments.fuelValue);
        appendHudText(result, "rr-flight-throttle-accessibility", instruments.throttleValue);
        double warningTime = 0.0;
        if (state.screen == Screen::Launch && context.launchFlight != nullptr) {
            warningTime = context.launchFlight->elapsedSeconds;
        } else if (state.screen == Screen::Flyby) {
            warningTime = state.run.flyby.elapsedSeconds;
        } else if (state.screen == Screen::Orbit) {
            warningTime = state.run.orbit.elapsedSeconds;
        }
        std::string warningClass = "flight-nav-indicator";
        if (instruments.offCourse) {
            warningClass += " is-active";
            if (instruments.courseCritical) {
                warningClass += " is-critical";
            }
            if (static_cast<int>(std::floor(warningTime * 3.0)) % 2 == 0) {
                warningClass += " blink-on";
            }
        }
        appendHudClass(result, "rr-flight-nav-indicator", std::move(warningClass));
        std::string temperatureLabelClass = "flight-dial-label temperature";
        std::string temperatureReadoutClass = "flight-gauge-readout temperature";
        if (instruments.temperatureCritical) {
            temperatureLabelClass += " is-critical";
            temperatureReadoutClass += " is-critical";
            if (static_cast<int>(std::floor(warningTime * 3.0)) % 2 == 0) {
                temperatureLabelClass += " blink-on";
                temperatureReadoutClass += " blink-on";
            }
        }
        appendHudClass(result, "rr-flight-temperature-label", std::move(temperatureLabelClass));
        appendHudClass(result, "rr-flight-temperature-readout", std::move(temperatureReadoutClass));
    }

    const auto appendExpeditionXp = [&](std::string_view id, bool hero = false) {
        const SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
        const int required = static_cast<int>(std::ceil(std::max(
            1.0,
            expeditionExperienceThreshold(expedition.expeditionLevel))));
        const int current = std::clamp(
            static_cast<int>(std::floor(expedition.expeditionExperience + 0.0001)),
            0,
            required);
        const int filled = expeditionXpFilledSegments(expedition);
        const std::string xpClass = std::string(id) == "rr-hud-mining-xp"
            ? "mining-scene-xp " + expeditionXpClass(context.expeditionXpPulse, hero)
            : expeditionXpClass(context.expeditionXpPulse, hero);
        appendHudClass(result, id, xpClass);
        appendHudText(result, std::string(id) + "-level", "LV " + std::to_string(std::max(1, expedition.expeditionLevel)));
        appendHudText(result, std::string(id) + "-value", std::to_string(current) + " / " + std::to_string(required) + " XP");
        appendHudText(result, std::string(id) + "-pending", std::to_string(std::max(0, expedition.pendingRunUpgradeChoices)) + " PICKS");
        for (int segment = 0; segment < kExpeditionXpSegments; ++segment) {
            appendHudClass(
                result,
                std::string(id) + "-segment-" + std::to_string(segment),
                segment < filled ? "xp-segment is-filled" : "xp-segment");
        }
    };

    if (state.screen == Screen::Mining) {
        appendExpeditionXp("rr-hud-mining-xp");
    } else if (state.screen == Screen::SurfaceExpedition) {
        appendExpeditionXp("rr-hud-surface-xp");
    } else if (state.screen == Screen::SurfaceUpgrade) {
        appendHudClass(result, "rr-level-up-draft", levelUpDraftClass(context));
        appendExpeditionXp("rr-hud-level-up-xp", true);
        for (int index = 0; index < 3; ++index) {
            appendHudClass(
                result,
                "rr-run-upgrade-resolve-" + std::to_string(index),
                index == context.levelUpResolvingOfferIndex
                    ? "run-upgrade-resolve-flash is-active"
                    : "run-upgrade-resolve-flash");
        }
    }

    if (state.screen == Screen::Flyby && !state.run.flyby.completed) {
        const FlybyRunState& flyby = state.run.flyby;
        const double remaining = std::max(0.0, flyby.durationSeconds - flyby.elapsedSeconds);
        appendHudText(result, "rr-hud-flyby-timer", std::to_string(static_cast<int>(std::ceil(remaining))) + "s");
        appendHudText(
            result,
            "rr-hud-flyby-grade",
            flyby.collidedWithBody ? "Impact" : flybyZoneLabel(flyby.worstZone));
        return;
    }

    if (state.screen == Screen::Orbit && !state.run.orbit.completed) {
        const OrbitRunState& orbit = state.run.orbit;
        const double remaining = std::max(0.0, orbit.durationSeconds - orbit.elapsedSeconds);
        appendHudText(result, "rr-hud-orbit-timer", std::to_string(static_cast<int>(std::ceil(remaining))) + "s");
        appendHudText(result, "rr-hud-orbit-zone", orbitZoneLabel(orbit.currentZone));
        appendHudText(result, "rr-hud-orbit-loop", display::percent(std::clamp(orbit.orbitProgress, 0.0, 1.0)));
        return;
    }

    if (state.screen == Screen::Launch) {
        const LaunchPanelPresentation launchPanel = launchPanelPresentation(
            state,
            catalog,
            context.flightModel,
            context.currentMultiplier,
            context.returnBurnMultiplier,
            context.returnElapsed,
            context.returnDuration,
            context.flightActions,
            context.launchFlight);
        if (context.launchFlight != nullptr && context.flightModel.asteroidsEnabled) {
            appendHudText(
                result,
                "rr-hud-launch-hull",
                display::fixed(std::max(0.0, context.launchFlight->hullRemaining), 0) + " / " +
                    display::fixed(context.launchFlight->hullMaximum, 0) + " HP");
        }
        appendHudText(result, "rr-hud-launch-status", launchPanel.telemetryMessage);
        appendHudClass(result, "rr-hud-launch-status", launchStatusSeverity(context));
        return;
    }

    if (state.screen == Screen::SurfaceScan) {
        const SurfaceScanRailPresentation scan = surfaceScanRailPresentation(state);
        appendHudText(result, "rr-scan-scene-readout", scan.signal);
        return;
    }

    if (state.screen != Screen::Mining) {
        return;
    }

    const MiningRunState& mining = state.run.mining;
    const MiningLoadStats miningLoad = miningLoadStats(state, catalog);
    const MiningHudPresentation miningHud = miningHudPresentation(state, catalog);
    const int currentDepth = std::max(0, mining.depthZone);
    const std::string depthRoute = currentDepth == 0
        ? std::string("SURFACE \xE2\x80\xA2 SHIP HERE")
        : "DEPTH +" + std::to_string(currentDepth) + " \xE2\x80\xA2 SHIP \xE2\x86\x91 " + std::to_string(currentDepth);
    const ScenarioObjectivePresentation miningScenario = scenarioObjectiveForMining(state, catalog);
    const bool scenarioMining = miningScenario.available;
    appendHudText(
        result,
        "rr-hud-mining-route-up",
        currentDepth == 0
            ? std::string("SURFACE \xE2\x80\xA2 SHIP HERE")
            : std::string("ASCEND \xE2\x80\xA2 SHIP \xE2\x86\x91 ") + std::to_string(currentDepth));
    appendHudText(
        result,
        "rr-hud-mining-route-down",
        std::string("DESCEND \xE2\x80\xA2 DEPTH +") + std::to_string(currentDepth + 1));

    const double activeOxygenCapacity = miningActiveOxygenCapacity(state, catalog);
    const double oxygenPressure = activeOxygenCapacity > 0.0
        ? std::clamp(1.0 - miningActiveOxygenSeconds(mining) / activeOxygenCapacity, 0.0, 1.0)
        : 1.0;
    const double fuelPressure = state.run.surfaceExpedition.rigFuelCapacity > 0.0
        ? std::clamp(
              1.0 - state.run.surfaceExpedition.rigFuel /
                  state.run.surfaceExpedition.rigFuelCapacity,
              0.0,
              1.0)
        : 1.0;
    const auto vitalClass = [&](std::size_t index, std::string className) {
        if (!miningHud.vitals[index].cssClass.empty()) {
            className += " " + miningHud.vitals[index].cssClass;
        }
        return className;
    };
    appendHudClass(
        result,
        "rr-hud-mining-oxygen",
        vitalClass(
            0,
            "mining-vital-tile "
                + miningVitalAlertClass(
                    "mining-vital-oxygen",
                    oxygenPressure,
                    mining.elapsedSeconds)
                + " oxygen"));
    appendHudClass(
        result,
        "rr-hud-mining-fuel",
        vitalClass(
            1,
            "mining-vital-tile "
                + miningVitalAlertClass(
                    "mining-vital-fuel",
                    std::max(fuelPressure, mining.fuelCycleProgress),
                    mining.elapsedSeconds,
                    true)
                + " fuel"));
    std::string drillClass = "mining-vital-tile "
        + miningVitalAlertClass(
            "mining-vital-drill",
            std::clamp(1.0 - mining.drillIntegrity, 0.0, 1.0),
            mining.elapsedSeconds)
        + " " + miningDrillHeatAlertClass(mining.drillHeat, mining.elapsedSeconds)
        + " drill";
    if (mining.drillIntegrity <= 0.0) {
        drillClass += " mining-vital-broken";
    }
    appendHudClass(
        result,
        "rr-hud-mining-drill-bit",
        vitalClass(2, std::move(drillClass)));

    // The compact HUD uses stable tile wrappers and patches only their values,
    // so caution styling can evolve independently without collapsing the
    // mockup-faithful grid on every realtime update.
    appendHudText(result, "rr-hud-mining-title", miningHud.runLabel);
    appendHudText(
        result,
        "rr-hud-mining-objective-title",
        scenarioMining
            ? compactMiningScenarioObjective(state, catalog) + " \xE2\x80\xA2 " + depthRoute
            : miningHud.objective);
    const std::array<std::string_view, 4> vitalValueIds {
        "rr-hud-mining-oxygen-value",
        "rr-hud-mining-fuel-value",
        "rr-hud-mining-drill-bit-value",
        "rr-hud-mining-load-value"
    };
    for (std::size_t index = 0; index < miningHud.vitals.size(); ++index) {
        appendHudText(result, vitalValueIds[index], miningHud.vitals[index].value);
    }
    appendHudText(result, "rr-hud-mining-oxygen-label", miningHud.vitals[0].label);
    appendHudText(result, "rr-hud-mining-fuel-micro", miningHud.vitals[1].microValue);
    appendHudText(result, "rr-hud-mining-drill-bit-micro", miningHud.vitals[2].microValue);
    appendHudText(result, "rr-hud-mining-ore-common", miningHud.oreManifest.ores[0].value);
    appendHudText(result, "rr-hud-mining-ore-rare", miningHud.oreManifest.ores[1].value);
    appendHudText(result, "rr-hud-mining-ore-exotic", miningHud.oreManifest.ores[2].value);
    appendHudText(result, "rr-hud-mining-payload-banked", miningHud.payload[0].value);
    appendHudText(result, "rr-hud-mining-payload-artifact", miningHud.payload[1].value);
    appendHudText(result, "rr-hud-mining-operator-mode", miningOperatorModeLabel(mining));
    appendHudText(result, "rr-hud-mining-gravity", miningGravityLabel(mining));
    appendHudText(result, "rr-hud-mining-actor-integrity-label", std::string(miningActorIntegrityLabel(mining)));
    appendHudText(result, "rr-hud-mining-actor-integrity", display::percent(miningActorIntegrity(mining)));
    appendHudText(result, "rr-hud-mining-drill-heat", display::percent(mining.drillHeat));
    appendHudText(result, "rr-hud-mining-tether-burden", miningTetherBurdenLabel(mining, miningLoad));
    appendHudText(result, "rr-hud-mining-loose-chunks", std::to_string(activeMiningLooseChunkCount(mining)));
    appendHudText(result, "rr-hud-mining-support-label", miningSupportTileLabel(mining));
    appendHudText(result, "rr-hud-mining-drone-parent", miningSupportTileValue(mining));
    appendHudText(result, "rr-hud-mining-suit-carry", "Suit carry: 0");
    if (!mining.gate.cocoonLayers.empty()) {
        for (std::size_t layerIndex = 0; layerIndex < mining.gate.cocoonLayers.size(); ++layerIndex) {
            const MiningCocoonLayerProgress& layer = mining.gate.cocoonLayers[layerIndex];
            const int cleared = std::clamp(layer.total - layer.remaining, 0, std::max(0, layer.total));
            const std::string prefix = "rr-hud-cocoon-" + std::to_string(layerIndex);
            appendHudClass(
                result,
                "rr-hud-cocoon-layer-" + std::to_string(layerIndex),
                !layer.revealed ? "is-locked" : (layer.completed ? "is-complete" : ""));
            appendHudText(
                result,
                prefix + "-label",
                miningCocoonLayerLabel(mining.gate, layerIndex));
            appendHudText(
                result,
                prefix + "-value",
                miningCocoonLayerValue(mining.gate, layerIndex));
            for (int cellIndex = 0; cellIndex < std::max(0, layer.total); ++cellIndex) {
                appendHudClass(
                    result,
                    prefix + "-" + std::to_string(cellIndex),
                    layer.revealed && cellIndex < cleared ? "is-cleared" : "");
            }
        }
        appendHudText(result, "rr-hud-cocoon-objective-state", miningCocoonObjectiveState(mining));
    }
}

std::uint64_t realtimePanelStructureKey(const PanelRenderContext& context)
{
    const GameState& state = context.state;
    std::ostringstream key;
    key << static_cast<int>(state.screen) << '|';
    if (state.screen == Screen::Launch) {
        const LaunchPanelPresentation panel = launchPanelPresentation(
            state,
            context.catalog,
            context.flightModel,
            context.currentMultiplier,
            context.returnBurnMultiplier,
            context.returnElapsed,
            context.returnDuration,
            context.flightActions,
            context.launchFlight);
        key << context.flightArmed << '|' << context.launchQueued << '|' << context.preflightReady << '|';
        for (const FlightActionButtonPresentation& action : panel.primaryActions) {
            key << action.actionId << ':' << action.label << ':' << action.enabled << ':' << action.cssClass << ';';
        }
        for (const FlightActionButtonPresentation& action : panel.systemActions) {
            key << action.actionId << ':' << action.label << ':' << action.enabled << ':' << action.cssClass << ';';
        }
    } else if (state.screen == Screen::Flyby) {
        key << state.run.flyby.completed << '|' << state.run.flyby.collidedWithBody;
    } else if (state.screen == Screen::Orbit) {
        key << state.run.orbit.completed;
    } else if (state.screen == Screen::Mining) {
        const MiningRunPresentation panel = miningRunPresentation(state, context.catalog);
        key << panel.failurePending << '|' << context.miningFailureModalReady << '|'
            << miningAtReturnZone(state.run.mining) << '|'
            << state.run.mining.progressionCreditEligible << '|' << state.run.mining.artifact.present << '|'
            << static_cast<int>(state.run.mining.artifact.state) << '|' << state.run.mining.miniDrones.size() << '|'
            << static_cast<int>(state.run.mining.operatorMode) << '|' << state.run.mining.operatorPresent << '|'
            << state.run.mining.rigDisabled << '|';
        for (const MiningMiniDroneAgent& drone : state.run.mining.miniDrones) {
            key << static_cast<int>(drone.anchorTarget) << ',';
        }
        key << '|';
        for (const std::string& hint : panel.commandHints) {
            key << hint << ';';
        }
        for (const PanelButtonPresentation& action : panel.actions) {
            key << action.actionId << ':' << action.label << ':' << action.enabled << ':' << action.cssClass << ';';
        }
        for (const MiningCocoonLayerProgress& layer : state.run.mining.gate.cocoonLayers) {
            key << layer.id << ':' << layer.total << ':' << layer.revealed << ':' << layer.completed << ';';
        }
    }
    return static_cast<std::uint64_t>(std::hash<std::string>{}(key.str()));
}

} // namespace rocket
