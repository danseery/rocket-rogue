#include "game/GamePanel.h"
#include "core/CrewPresentation.h"
#include "core/GameFormat.h"
#include "core/GameText.h"
#include "core/HangarPresentation.h"
#include "core/LaunchPresentation.h"
#include "core/LaunchReadinessPresentation.h"
#include "core/MiningPresentation.h"
#include "core/OutcomePresentation.h"
#include "core/PanelChromePresentation.h"
#include "core/ProgramPresentation.h"
#include "core/RefitPresentation.h"
#include "core/ResearchPresentation.h"
#include "core/ShipPresentation.h"
#include "core/Tuning.h"
#include "core/GameUi.h"

#include <sstream>
#include <vector>

namespace rocket {

namespace {

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

std::string metric(std::string_view label, std::string value)
{
    return "<div class=\"metric\"><strong>" + htmlEscape(value) + "</strong><span>" + htmlEscape(label) + "</span></div>";
}

std::string compactMetric(std::string_view label, std::string value)
{
    return "<div class=\"surface-kpi\"><span>" + htmlEscape(label) + "</span><strong>" + htmlEscape(value) + "</strong></div>";
}

std::string button(std::string_view label, std::string_view action, std::string cssClass = "")
{
    const std::string classAttr = cssClass.empty() ? "" : " class=\"" + cssClass + "\"";
    return "<button" + classAttr + " data-rr-action=\"" + htmlEscape(action) + "\">" + htmlEscape(label) + "</button>";
}

std::string modalButton(std::string_view label, std::string_view modalId, std::string cssClass = "")
{
    const std::string classAttr = cssClass.empty() ? "" : " class=\"" + cssClass + "\"";
    return "<button type=\"button\"" + classAttr + " data-ui-modal=\"" + htmlEscape(modalId) + "\">" + htmlEscape(label) + "</button>";
}

std::string modalTemplate(std::string_view modalId, std::string_view title, std::string body)
{
    return "<template data-modal=\"" + htmlEscape(modalId) + "\" data-title=\"" + htmlEscape(title) + "\">" + body + "</template>";
}

std::string disabledButton(std::string_view label)
{
    return "<button disabled>" + htmlEscape(label) + "</button>";
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

std::string statChip(const RefitStatChip& chip)
{
    return "<span class=\"stat-chip " + std::string(chip.positive ? "up" : "down") + "\">" +
        htmlEscape(chip.label) + " " + htmlEscape(chip.value) + "</span>";
}

std::string resourceChip(const PanelMetricPresentation& chip)
{
    const bool positive = chip.value.empty() || chip.value.front() != '-';
    return "<span class=\"stat-chip " + std::string(positive ? "up" : "down") + "\">" +
        htmlEscape(chip.label) + " " + htmlEscape(chip.value) + "</span>";
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

std::string operationCard(std::string title, std::string detail, std::string cost, std::string_view action, bool available, std::string cssClass = "")
{
    std::ostringstream out;
    out << "<article class=\"ops-card " << htmlEscape(cssClass) << "\">";
    out << "<h3>" << htmlEscape(title) << "</h3>";
    out << "<p>" << htmlEscape(detail) << "</p>";
    out << "<div class=\"card-footer\"><span>" << htmlEscape(cost) << "</span>";
    out << (available ? button(text::buttons::assign, action) : disabledButton(text::buttons::unavailable));
    out << "</div></article>";
    return out.str();
}

std::string operationCard(const HangarOperationCardPresentation& card)
{
    return operationCard(card.title, card.detail, card.cost, card.actionId, card.available, card.cssClass);
}

std::string panelButton(const PanelButtonPresentation& action)
{
    if (!action.enabled) {
        return disabledButton(action.label);
    }
    return button(action.label, action.actionId, action.cssClass);
}

std::string refitOfferCard(const RefitOfferPresentation& offer)
{
    const RefitPresentation& presentation = offer.card;
    std::ostringstream out;
    out << "<article class=\"upgrade-card slot-" << htmlEscape(presentation.slotClass) << "\">";
    out << "<div class=\"card-topline\"><span>" << htmlEscape(presentation.category) << "</span><span>"
        << htmlEscape(presentation.rarity) << "</span></div>";
    out << "<div class=\"module-art\"><span>" << htmlEscape(presentation.glyph) << "</span></div>";
    out << "<h3>" << htmlEscape(presentation.title) << "</h3>";
    out << "<p class=\"module-threat\">" << htmlEscape(presentation.detail) << "</p>";
    out << "<strong class=\"module-impact\">" << htmlEscape(presentation.primaryImpact) << "</strong>";
    out << "<div class=\"stat-grid\">" << statChipGrid(presentation.statChips) << "</div>";
    out << "<div class=\"card-footer\"><span>" << htmlEscape(offer.costSummary) << "</span>"
        << panelButton(offer.action) << "</div></article>";
    return out.str();
}

std::string researchProjectCard(const ResearchProjectCardPresentation& project)
{
    std::ostringstream out;
    out << "<article class=\"ops-card\">";
    out << "<div class=\"card-topline\"><span>" << htmlEscape(project.rarity) << "</span><span>"
        << htmlEscape(project.blueprintGain) << "</span></div>";
    out << "<h3>" << htmlEscape(project.title) << "</h3>";
    out << "<p>" << htmlEscape(project.detail) << "</p>";
    if (!project.reward.empty()) {
        out << "<strong class=\"module-impact\">" << htmlEscape(project.reward) << "</strong>";
    }
    out << "<div class=\"stat-grid\">" << resourceChipGrid(project.resourceChips) << "</div>";
    out << "<div class=\"card-footer\"><span>" << htmlEscape(project.materialCost) << "</span>"
        << panelButton(project.action) << "</div></article>";
    return out.str();
}

std::string surfaceActionCard(const SurfaceActionPreviewPresentation& action)
{
    std::ostringstream out;
    out << "<article class=\"ops-card surface-action-card\">";
    out << "<div class=\"card-topline\"><span>" << htmlEscape(action.cost) << "</span><span>"
        << htmlEscape(action.risk) << " " << htmlEscape(action.riskLabel) << "</span></div>";
    out << "<h3>" << htmlEscape(action.title) << "</h3>";
    out << "<p>" << htmlEscape(action.detail) << "</p>";
    out << "<div class=\"stat-grid\">" << resourceChipGrid(action.payoffChips) << "</div>";
    out << "<div class=\"card-footer\"><span>" << htmlEscape(action.availability)
        << "</span>" << panelButton(action.action) << "</div></article>";
    return out.str();
}

std::string arrivalOperationCard(
    std::string_view title,
    std::string_view detail,
    std::string_view risk,
    std::string_view reward,
    const PanelButtonPresentation& action)
{
    std::ostringstream out;
    out << "<article class=\"ops-card arrival-card\">";
    out << "<div class=\"card-topline\"><span>" << htmlEscape(risk) << "</span><span>" << htmlEscape(reward) << "</span></div>";
    out << "<h3>" << htmlEscape(title) << "</h3>";
    out << "<p>" << htmlEscape(detail) << "</p>";
    out << "<div class=\"card-footer\"><span>" << htmlEscape(action.enabled ? std::string(text::panel::ready) : std::string(text::buttons::unavailable))
        << "</span>" << panelButton(action) << "</div></article>";
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
    std::string body = "<div class=\"detail-stack\">";
    for (const DetailPresentationRow& row : rows) {
        body += row.heading ? detailHeader(row.label) : detailRow(row.label, row.value);
    }
    body += "</div>";
    return body;
}

std::string missionLog(const std::vector<std::string>& entries)
{
    std::string body = "<div class=\"detail-stack\">";
    for (std::size_t i = 0; i < entries.size(); ++i) {
        body += detailRow(std::to_string(i + 1), entries[i]);
    }
    body += "</div>";
    return body;
}

std::string phaseBoardOpen(std::string_view cssClass, std::string_view status, bool fullPanel = true)
{
    std::string out = "<section class=\"phase-board " + htmlEscape(cssClass) + "\"";
    if (fullPanel) {
        out += " data-panel-mode=\"phase-board\"";
    }
    out += ">";
    if (!status.empty()) {
        out += "<p class=\"phase-status\">" + htmlEscape(status) + "</p>";
    }
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

std::string phaseTrack(const std::vector<PhaseStepPresentation>& steps)
{
    std::string out = "<ol class=\"phase-track\">";
    for (const PhaseStepPresentation& step : steps) {
        out += "<li class=\"" + htmlEscape(step.stateClass) + "\"><span>" + htmlEscape(step.label) +
            "</span><strong>" + htmlEscape(step.stateLabel) + "</strong></li>";
    }
    out += "</ol>";
    return out;
}

std::string surfacePosture(const SurfaceExpeditionPresentation& surface)
{
    return "<article class=\"phase-advisory " + htmlEscape(surface.postureClass) + "\"><strong>" +
        htmlEscape(surface.postureTitle) + "</strong><span>" + htmlEscape(surface.postureDetail) + "</span></article>";
}

std::string surfaceCommandSummary(const SurfaceExpeditionPresentation& surface)
{
    std::ostringstream out;
    out << "<section class=\"surface-command\">";
    out << "<div><span>" << htmlEscape(text::labels::site) << "</span><strong>" << htmlEscape(surface.metrics[0].value)
        << "</strong><p>" << htmlEscape(surface.siteDetail) << "</p></div>";
    out << surfacePosture(surface);
    out << "</section>";
    return out.str();
}

std::string surfaceKpiGrid(const SurfaceExpeditionState& expedition, double extractionRisk)
{
    std::ostringstream out;
    out << "<div class=\"surface-kpi-grid\">";
    out << compactMetric(text::labels::supply, std::to_string(expedition.supply));
    out << compactMetric(text::labels::cargo, std::to_string(expedition.cargo));
    out << compactMetric(text::labels::hazard, display::percent(expedition.hazard));
    out << compactMetric(text::labels::extractionRisk, display::percent(extractionRisk));
    out << compactMetric(text::labels::commonMaterials, std::to_string(expedition.temporaryMaterials.common));
    out << compactMetric(text::labels::rareMaterials, std::to_string(expedition.temporaryMaterials.rare));
    out << compactMetric(text::labels::artifacts, std::to_string(expedition.temporaryArtifacts.size()));
    out << "</div>";
    return out.str();
}

std::string phaseAdvisory(const PhaseAdvisoryPresentation& advisory)
{
    return "<article class=\"phase-advisory " + htmlEscape(advisory.cssClass) + "\"><strong>" +
        htmlEscape(advisory.title) + "</strong><span>" + htmlEscape(advisory.detail) + "</span></article>";
}

std::string resultMetricGroup(const LaunchOutcomeMetricGroupPresentation& group)
{
    const std::string classAttr = group.cssClass.empty() ? "" : " " + htmlEscape(group.cssClass);
    std::string out = "<article class=\"result-group" + classAttr + "\"><h3>" + htmlEscape(group.title) + "</h3>";
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

std::string tutorialCard(std::string_view topic, std::string_view title, std::string_view detail)
{
    return "<aside class=\"tutorial-card\" data-help-topic=\"" + htmlEscape(topic) + "\"><div><span>Mission help</span><strong>" +
        htmlEscape(title) + "</strong><p>" + htmlEscape(detail) +
        "</p></div><button type=\"button\" class=\"ghost\" data-help-dismiss=\"" + htmlEscape(topic) + "\">Got it</button></aside>";
}

} // namespace

std::string buildGamePanelHtml(const PanelRenderContext& context)
{
    const GameState& state = context.state;
    const ContentCatalog& catalog = context.catalog;
    const Destination& currentFrontier = currentDestination(state, catalog);

    const Astronaut* astronaut = activeAstronaut(state);
    const Destination* next = nextDestination(state, catalog);
    const LaunchReadinessPresentation launchReadiness = launchReadinessPresentation(state, catalog);
    const std::vector<PanelMetricPresentation> headerMetrics = panelHeaderMetrics(state, catalog, context.activeLaunch, context.flightModel);
    const PanelLayoutMode layoutMode = panelLayoutMode(state.screen);

    std::ostringstream out;
    out << "<div class=\"panel-head\"><div><h1>" << htmlEscape(text::panel::title) << "</h1></div>"
        << modalButton(text::buttons::settings, ui::modals::settings, "ghost") << "</div>";
    out << "<p class=\"status\">" << htmlEscape(state.statusLine) << "</p>";

    std::ostringstream settingsBody;
    settingsBody << detailStack(settingsDetailsPresentation());
    settingsBody << "<section class=\"settings-control\" data-game-speed-settings>"
        << "<div><h3>" << htmlEscape("Game speed") << "</h3>"
        << "<p>" << htmlEscape("Local testing multiplier. Shared builds start at 1x.") << "</p></div>"
        << "<label><span>" << htmlEscape("Multiplier") << "</span>"
        << "<select data-game-speed-select aria-label=\"Game speed multiplier\">"
        << "<option value=\"0.5\">0.5x</option>"
        << "<option value=\"1\">1x</option>"
        << "<option value=\"1.5\">1.5x</option>"
        << "<option value=\"2\">2x</option>"
        << "<option value=\"3\">3x</option>"
        << "<option value=\"5\">5x</option>"
        << "<option value=\"8\">8x</option>"
        << "</select></label></section>";
    settingsBody << "<section class=\"settings-control\" data-help-settings>"
        << "<div><h3>" << htmlEscape("Mission help") << "</h3>"
        << "<p>" << htmlEscape("Show light tips when new systems appear.") << "</p></div>"
        << "<label class=\"settings-checkbox\"><input type=\"checkbox\" data-help-toggle checked><span>"
        << htmlEscape("Show help") << "</span></label></section>";
    settingsBody << "<div class=\"modal-actions\">";
    for (const PanelButtonPresentation& action : settingsActionPresentation()) {
        settingsBody << panelButton(action);
    }
    settingsBody << "</div>";

    out << "<div class=\"metric-grid\">";
    for (const PanelMetricPresentation& metricItem : headerMetrics) {
        out << metric(metricItem.label, metricItem.value);
    }
    out << "</div>";

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
            context.pressureReliefUsed);

        out << "<h2>" << htmlEscape(launchPanel.sectionTitle) << "</h2>";
        out << "<div class=\"metric-grid\">";
        for (const PanelMetricPresentation& metricItem : launchPanel.metrics) {
            out << metric(metricItem.label, metricItem.value);
        }
        out << "</div>";

        out << "<h2>" << htmlEscape(text::panel::sections::telemetry) << "</h2>";
        out << "<div class=\"warning-grid\">";
        for (const TelemetryChannelSample& sample : launchPanel.telemetry) {
            out << warningButton(sample.label, sample.value);
        }
        out << "</div>";
        out << "<div class=\"utility-row\">" << modalButton(text::panel::modals::telemetryDetails, ui::modals::telemetry, "ghost") << "</div>";
        out << "<p class=\"status telemetry-status\">" << htmlEscape(launchPanel.telemetryMessage) << "</p>";
        out << tutorialCard(
            "launch-controls",
            "Press your luck, then get home",
            "Return home banks data but still has return risk. Eject is the expensive emergency out. If gauges climb, cut engines, open the relief valve, or jettison cargo to buy time with tradeoffs.");

        out << "<h2>" << htmlEscape(text::panel::sections::flightControls) << "</h2>";
        out << "<div class=\"actions primary-actions\">";
        for (const FlightActionButtonPresentation& action : launchPanel.primaryActions) {
            out << panelButton(action);
        }
        out << "</div>";

        out << "<div class=\"actions system-actions\">";
        for (const FlightActionButtonPresentation& action : launchPanel.systemActions) {
            out << panelButton(action);
        }
        out << "</div>";
        out << modalTemplate(ui::modals::telemetry, text::panel::modals::telemetryDetails, detailStack(launchPanel.telemetryDetails));
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        return out.str();
    }

    if (state.screen == Screen::Results) {
        const bool opensPostArrival = shouldOpenPostArrivalPhases(state.lastOutcome, catalog);
        const LaunchOutcomePresentation presentation = launchOutcomePresentation(
            state.lastOutcome,
            opensPostArrival);
        out << phaseBoardOpen("phase-board-results", state.statusLine);
        out << "<h2>" << htmlEscape(text::panel::sections::result) << "</h2>";
        if (opensPostArrival) {
            const PhaseBriefingPresentation arrivalBriefing = postArrivalPhaseBriefing(Screen::Results);
            out << phaseTrack(postArrivalPhaseSteps(Screen::Results));
            out << "<div class=\"utility-row\">" << modalButton(text::buttons::briefing, ui::modals::phaseBriefing, "ghost") << "</div>";
            out << modalTemplate(ui::modals::phaseBriefing, arrivalBriefing.title, detailStack(arrivalBriefing.rows));
        }
        out << "<div class=\"result-grid\">";
        for (const LaunchOutcomeMetricGroupPresentation& group : presentation.metricGroups) {
            out << resultMetricGroup(group);
        }
        out << "</div>";
        for (const std::string& note : presentation.notes) {
            out << boardNote(note);
        }
        if (!presentation.achievements.empty()) {
            out << "<h2>" << htmlEscape(text::panel::sections::achievements) << "</h2>";
            out << "<div class=\"achievement-grid\">";
            for (const AchievementPresentation& achievement : presentation.achievements) {
                out << achievementCard(achievement);
            }
            out << "</div>";
        }
        out << "<div class=\"actions\">";
        out << button(presentation.nextActionLabel, ui::actions::next, "ok");
        out << "</div>";
        out << phaseBoardClose();
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        return out.str();
    }

    if (state.screen == Screen::ArrivalOps) {
        const Destination* arrivalDestination = catalog.findDestination(state.run.arrivalOps.destinationId);
        const std::string destinationName = arrivalDestination == nullptr ? currentFrontier.name : arrivalDestination->name;
        const bool flybyAvailable = canRunArrivalFlyby(state, catalog);
        const bool orbitAvailable = canEnterArrivalOrbit(state, catalog);
        const bool landingAvailable = canAttemptArrivalLanding(state, catalog);
        const std::string orbitReason = arrivalOperationBlockReason(state, catalog, "orbit");
        const std::string landingReason = arrivalOperationBlockReason(state, catalog, "landing");
        const std::string orbitDetail = orbitReason.empty() ? std::string(text::panel::messages::orbitDetail) : orbitReason;
        const std::string landingDetail = landingReason.empty() ? std::string(text::panel::messages::landingDetail) : landingReason;

        out << phaseBoardOpen("phase-board-arrival", state.statusLine);
        out << "<h2>" << htmlEscape(text::panel::sections::arrivalOps) << "</h2>";
        out << "<p>" << htmlEscape(text::panel::messages::chooseArrivalOperation) << "</p>";
        out << tutorialCard(
            "arrival-ops",
            "Flyby, orbit, then land",
            "Flyby is the safest scan. Orbit opens stronger science. Landing is the risky payoff for surface work and mining. The Moon requires flyby and orbit first; later worlds let you gamble.");
        out << "<div class=\"metric-grid\">";
        out << metric(text::labels::currentFrontier, destinationName);
        out << metric(text::panel::details::flyby, std::to_string(destinationHistoryValue(state.meta.destinationFlybys, catalog, state.run.arrivalOps.destinationId)));
        out << metric(text::panel::details::orbit, std::to_string(destinationHistoryValue(state.meta.destinationOrbits, catalog, state.run.arrivalOps.destinationId)));
        out << metric(text::panel::details::landing, std::to_string(destinationHistoryValue(state.meta.destinationLandings, catalog, state.run.arrivalOps.destinationId)));
        out << "</div>";
        out << "<div class=\"ops-grid\">";
        out << arrivalOperationCard(
            text::panel::details::flyby,
            text::panel::messages::flybyDetail,
            "Low risk",
            "Credits + BP",
            flybyAvailable
                ? panelActionButton(text::buttons::runFlyby, ui::actions::arrivalFlyby, "ok")
                : disabledPanelButton(text::buttons::unavailable));
        out << arrivalOperationCard(
            text::panel::details::orbit,
            orbitDetail,
            "Medium risk",
            arrivalDestination != nullptr && destinationSupportsResearch(*arrivalDestination) ? "Research" : "Science + BP",
            orbitAvailable
                ? panelActionButton(text::buttons::enterOrbit, ui::actions::arrivalOrbit, "warn")
                : disabledPanelButton(text::buttons::unavailable));
        out << arrivalOperationCard(
            text::panel::details::landing,
            landingDetail,
            "High risk",
            "Materials + artifacts",
            landingAvailable
                ? panelActionButton(text::buttons::attemptLanding, ui::actions::arrivalLanding, "danger")
                : disabledPanelButton(text::buttons::unavailable));
        out << "</div>";
        out << phaseBoardClose();
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        return out.str();
    }

    if (state.screen == Screen::Research) {
        const ResearchPhasePresentation researchPanel = researchPhasePresentation(state, catalog);
        out << phaseBoardOpen("phase-board-research", state.statusLine);
        out << "<h2>" << htmlEscape(text::panel::sections::research) << "</h2>";
        out << "<p>" << htmlEscape(text::panel::messages::chooseOneResearch) << "</p>";
        out << phaseTrack(researchPanel.phaseSteps);
        out << phaseAdvisory(researchPanel.advisory);
        out << "<div class=\"utility-row\">" << modalButton(text::buttons::briefing, ui::modals::phaseBriefing, "ghost")
            << modalButton(text::buttons::details, ui::modals::research, "ghost") << "</div>";
        out << "<div class=\"metric-grid\">";
        for (const PanelMetricPresentation& metricItem : researchPanel.metrics) {
            out << metric(metricItem.label, metricItem.value);
        }
        out << "</div>";
        out << "<div class=\"ops-grid\">";
        for (const ResearchProjectCardPresentation& project : researchPanel.projects) {
            out << researchProjectCard(project);
        }
        out << "</div>";
        out << "<div class=\"actions\">";
        out << panelButton(researchPanel.skipAction);
        out << "</div>";
        out << phaseBoardClose();
        out << modalTemplate(ui::modals::phaseBriefing, researchPanel.briefing.title, detailStack(researchPanel.briefing.rows));
        out << modalTemplate(ui::modals::research, text::panel::modals::researchDetails, detailStack(researchPanel.details));
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        return out.str();
    }

    if (state.screen == Screen::Mining) {
        const MiningRunPresentation miningPanel = miningRunPresentation(state, catalog);
        out << phaseBoardOpen("phase-board-mining", state.statusLine, false);
        out << "<h2>" << htmlEscape(text::panel::sections::miningRun) << "</h2>";
        out << "<p>" << htmlEscape("Pilot the mining drone through destructible terrain. Stow early with partial cargo or keep drilling for deeper pockets.") << "</p>";
        out << tutorialCard(
            "mining-basics",
            "Dig, scan, stow",
            "Move with WASD or arrows, aim with the mouse, hold Space or click to drill, E pulses the scanner, and R stows payload. Bring back materials and artifacts before oxygen or extraction risk gets ugly.");
        out << "<div class=\"utility-row\">" << modalButton(text::buttons::details, ui::modals::surface, "ghost") << "</div>";
        out << "<div class=\"metric-grid mining-metrics\">";
        for (const PanelMetricPresentation& metricItem : miningPanel.metrics) {
            out << metric(metricItem.label, metricItem.value);
        }
        out << "</div>";
        out << "<h2>" << htmlEscape(text::panel::sections::flightControls) << "</h2>";
        out << "<div class=\"actions system-actions\">";
        for (const PanelButtonPresentation& action : miningPanel.actions) {
            out << panelButton(action);
        }
        out << "</div>";
        out << phaseBoardClose();
        out << modalTemplate(ui::modals::surface, text::panel::modals::surfaceDetails, detailStack(miningPanel.details));
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        return out.str();
    }

    if (state.screen == Screen::SurfaceExpedition) {
        const SurfaceExpeditionPresentation surfacePanel = surfaceExpeditionPresentation(state);
        const SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
        const double extractionRisk = surfaceExtractionRisk(state);
        out << phaseBoardOpen("phase-board-surface", state.statusLine);
        out << "<div class=\"phase-titlebar\"><div><h2>" << htmlEscape(text::panel::sections::surfaceExpedition)
            << "</h2><p>" << htmlEscape(text::panel::messages::surfaceExpeditionBrief) << "</p></div>";
        out << "<div class=\"utility-row compact-tools\">" << modalButton(text::buttons::briefing, ui::modals::phaseBriefing, "ghost")
            << modalButton(text::buttons::details, ui::modals::surface, "ghost");
        if (!surfacePanel.logEntries.empty()) {
            out << modalButton(text::panel::sections::missionLog, ui::modals::missionLog, "ghost");
        }
        out << "</div></div>";
        out << phaseTrack(surfacePanel.phaseSteps);
        out << surfaceCommandSummary(surfacePanel);
        out << surfaceKpiGrid(expedition, extractionRisk);
        out << "<section class=\"board-primary surface-actions\">";
        out << "<h2>" << htmlEscape("Field Actions") << "</h2>";
        out << "<div class=\"ops-grid\">";
        for (const SurfaceActionPreviewPresentation& action : surfacePanel.actions) {
            out << surfaceActionCard(action);
        }
        out << "</div></section>";
        out << phaseBoardClose();
        out << modalTemplate(ui::modals::phaseBriefing, surfacePanel.briefing.title, detailStack(surfacePanel.briefing.rows));
        out << modalTemplate(ui::modals::surface, text::panel::modals::surfaceDetails, detailStack(surfacePanel.details));
        out << modalTemplate(ui::modals::missionLog, text::panel::sections::missionLog, missionLog(surfacePanel.logEntries));
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        return out.str();
    }

    if (state.screen == Screen::Upgrade) {
        const RefitWindowPresentation refitWindow = refitWindowPresentation(state, catalog);
        out << phaseBoardOpen("phase-board-refit", state.statusLine);
        out << "<h2>" << htmlEscape(text::panel::sections::refitWindow) << "</h2>";
        out << "<p>" << htmlEscape(text::panel::messages::chooseOneRefit) << "</p>";
        if (!refitWindow.resourceChips.empty() || !refitWindow.recoveryDetail.empty()) {
            out << "<section class=\"resource-bank\"><div><h2>" << htmlEscape(text::panel::sections::recoveredResources)
                << "</h2><p>" << htmlEscape(refitWindow.recoveryDetail) << "</p></div>";
            out << "<div class=\"stat-grid\">" << resourceChipGrid(refitWindow.resourceChips) << "</div></section>";
        }
        out << "<div class=\"upgrade-grid\">";
        for (const RefitOfferPresentation& offer : refitWindow.offers) {
            out << refitOfferCard(offer);
        }
        out << "</div><div class=\"actions\">";
        out << panelButton(refitWindow.rerollAction);
        out << panelButton(refitWindow.skipAction);
        out << "</div>";
        out << phaseBoardClose();
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        return out.str();
    }

    const std::string shipBody = detailStack(shipDetailsPresentation(state, catalog));
    const std::string crewBody = detailStack(crewDetailsPresentation(state, catalog));
    const std::string frontierBody = detailStack(frontierDetailsPresentation(state, catalog));

    std::ostringstream launchBlockedBody;
    for (const std::string& message : launchReadiness.messages) {
        launchBlockedBody << "<p class=\"status\">" << htmlEscape(message) << "</p>";
    }
    launchBlockedBody << detailStack(launchReadiness.details);
    launchBlockedBody << "<div class=\"modal-actions actions\">";
    for (const PanelButtonPresentation& action : launchReadiness.actions) {
        launchBlockedBody << panelButton(action);
    }
    launchBlockedBody << "</div>";

    out << phaseBoardOpen("phase-board-hangar", state.statusLine);
    out << "<h2>" << htmlEscape(text::panel::sections::hangarBay) << "</h2>";
    out << "<div class=\"summary-grid\">";
    out << "<article class=\"summary-card\"><span>" << htmlEscape(text::panel::details::ship) << "</span><strong>" << htmlEscape(display::damage(state.run.shipDamage)) << "</strong>"
        << modalButton(text::buttons::details, ui::modals::ship, "ghost") << "</article>";
    out << "<article class=\"summary-card\"><span>" << htmlEscape(text::panel::details::crew) << "</span><strong>"
        << htmlEscape(astronaut == nullptr ? std::string(text::panel::noPilot) : astronaut->name) << "</strong>"
        << modalButton(text::buttons::details, ui::modals::crew, "ghost") << "</article>";
    out << "<article class=\"summary-card\"><span>" << htmlEscape(text::panel::details::frontier) << "</span><strong>" << htmlEscape(currentFrontier.name) << "</strong>"
        << modalButton(text::buttons::details, ui::modals::frontier, "ghost") << "</article>";
    out << "</div>";

    out << "<h2>" << htmlEscape(text::panel::sections::hangarOps) << "</h2>";
    out << "<div class=\"ops-grid\">";
    for (const HangarOperationCardPresentation& card : hangarOperationCards(state, catalog)) {
        out << operationCard(card);
    }
    out << "</div>";

    out << "<div class=\"actions\">";
    out << (launchReadiness.blocked
        ? modalButton(text::buttons::launchProvingFlight, ui::modals::launchBlocked, "ok")
        : button(text::buttons::launchProvingFlight, ui::actions::startLaunch, "ok"));
    if (next != nullptr) {
        if (canCommitToNextFrontier(state, catalog)) {
            out << (launchReadiness.blocked
                ? modalButton(text::panel::attemptFrontier(next->name), ui::modals::launchBlocked, "danger")
                : button(text::panel::attemptFrontier(next->name), ui::actions::attemptFrontier, "danger"));
        } else {
            out << disabledButton(text::buttons::needFlightData);
        }
    }
    out << "</div>";

    const std::string legacyBody = detailStack(legacyDetailsPresentation(state, catalog));

    out << "<div class=\"utility-row bottom-tools\">";
    out << modalButton(text::buttons::legacy, ui::modals::legacy, "ghost");
    out << "</div>";
    out << phaseBoardClose();
    out << modalTemplate(ui::modals::ship, text::panel::modals::shipDetails, shipBody);
    out << modalTemplate(ui::modals::crew, text::panel::modals::crewDetails, crewBody);
    out << modalTemplate(ui::modals::frontier, text::panel::modals::frontierDetails, frontierBody);
    out << modalTemplate(ui::modals::launchBlocked, text::panel::modals::launchHold, launchBlockedBody.str());
    out << modalTemplate(ui::modals::legacy, text::panel::modals::legacy, legacyBody);
    out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());

    return out.str();
}

} // namespace rocket
