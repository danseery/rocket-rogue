#include "game/GamePanel.h"
#include "core/CrewPresentation.h"
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
#include "core/ShipPresentation.h"
#include "core/Tuning.h"
#include "core/GameUi.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>
#include <utility>
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
    case Screen::Research:
        return "Science";
    case Screen::SurfaceExpedition:
        return "Surface Ops";
    case Screen::SurfaceUpgrade:
        return "Field Upgrade";
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

std::string autoModalTemplate(std::string_view modalId, std::string_view title, std::string body)
{
    return "<template data-modal=\"" + htmlEscape(modalId) + "\" data-auto-modal=\"1\" data-title=\"" + htmlEscape(title) + "\">" + body + "</template>";
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

std::string operationModalCard(const HangarOperationCardPresentation& card, std::string_view buttonLabel, std::string_view modalId)
{
    std::ostringstream out;
    out << "<article class=\"ops-card " << htmlEscape(card.cssClass) << "\">";
    out << "<h3>" << htmlEscape(card.title) << "</h3>";
    out << "<p>" << htmlEscape(card.detail) << "</p>";
    out << "<div class=\"card-footer\"><span>" << htmlEscape(card.cost) << "</span>";
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
    out << "<h3>" << htmlEscape(candidate.name) << "</h3>";
    out << "<p>" << htmlEscape(candidate.trait) << "</p>";
    out << "<div class=\"pilot-stat-grid\">";
    out << "<span>Training <strong>" << htmlEscape(std::to_string(candidate.training)) << "</strong></span>";
    out << "<span>Stress <strong>" << htmlEscape(display::percent(candidate.stress)) << "</strong></span>";
    out << "</div>";
    out << (available ? button("Select pilot", ui::actions::recruitCandidate(index), "ok") : disabledButton(text::buttons::unavailable));
    out << "</article>";
    return out.str();
}

std::string draftInitial(std::string_view text, std::string_view fallback)
{
    return text.empty() ? std::string(fallback) : std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(text.front()))));
}

std::string panelButton(const PanelButtonPresentation& action)
{
    if (!action.enabled) {
        return disabledButton(action.label);
    }
    return button(action.label, action.actionId, action.cssClass);
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

std::string flybyResultBody(FlybyGrade grade)
{
    switch (grade) {
    case FlybyGrade::Perfect:
        return "Bonus recon secured. Gate speed multiplies the next launch's fuel margin and travel speed.";
    case FlybyGrade::Good:
        return "Recon data secured. Approach options update now.";
    case FlybyGrade::Miss:
        return "No flyby data banked. Try again or choose another unlocked approach.";
    case FlybyGrade::Active:
    default:
        return "Hold the corridor until the timer expires.";
    }
}

double flybySlingshotScale(const FlybyRunState& flyby)
{
    const double speed = std::hypot(flyby.velocityX, flyby.velocityY);
    const double baselineSpeed = std::hypot(tuning::flyby::startVelocityX, tuning::flyby::startVelocityY);
    const double range = std::max(0.001, tuning::flyby::maxSpeed - baselineSpeed);
    const double fastShare = std::clamp((speed - baselineSpeed) / range, 0.0, 1.0);
    return 1.0 + fastShare * (tuning::flyby::slingshotMaxSpeedScale - 1.0);
}

std::string refitOfferCard(const RefitOfferPresentation& offer)
{
    const RefitPresentation& presentation = offer.card;
    std::ostringstream out;
    out << "<article class=\"pilot-card upgrade-draft-card slot-" << htmlEscape(presentation.slotClass) << "\">";
    out << "<div class=\"pilot-card-top\"><span>" << htmlEscape(presentation.category) << "</span><strong>"
        << htmlEscape(presentation.rarity) << " refit</strong></div>";
    out << "<div class=\"pilot-portrait-placeholder draft-art\"><span>" << htmlEscape(presentation.glyph) << "</span></div>";
    out << "<h3>" << htmlEscape(presentation.title) << "</h3>";
    out << "<p>" << htmlEscape(presentation.detail) << "</p>";
    out << "<strong class=\"module-impact\">" << htmlEscape(presentation.primaryImpact) << "</strong>";
    out << "<div class=\"stat-grid\">" << statChipGrid(presentation.statChips) << "</div>";
    out << "<div class=\"draft-card-footer\"><span>" << htmlEscape(offer.costSummary) << "</span>"
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
    const bool isMining = action.action.actionId == ui::actions::mineSurface;
    out << "<article class=\"ops-card surface-action-card" << (isMining ? " featured-action" : "") << "\">";
    out << "<div class=\"card-topline\"><span>" << htmlEscape(action.cost) << "</span><span>"
        << htmlEscape(action.risk) << " " << htmlEscape(action.riskLabel) << "</span></div>";
    out << "<h3>" << htmlEscape(action.title) << "</h3>";
    out << "<p>" << htmlEscape(action.detail) << "</p>";
    out << "<div class=\"stat-grid\">" << resourceChipGrid(action.payoffChips) << "</div>";
    out << "<div class=\"card-footer\"><span>" << htmlEscape(action.availability)
        << "</span>" << panelButton(action.action) << "</div></article>";
    return out.str();
}

std::string surfaceUpgradeCard(const SurfaceUpgradeCardPresentation& upgrade)
{
    std::ostringstream out;
    out << "<article class=\"pilot-card upgrade-draft-card surface-upgrade-card\">";
    out << "<div class=\"pilot-card-top\"><span>" << htmlEscape(upgrade.category) << "</span><strong>"
        << htmlEscape(upgrade.rarity) << " field mod</strong></div>";
    out << "<div class=\"pilot-portrait-placeholder draft-art\"><span>" << htmlEscape(draftInitial(upgrade.category, "F")) << "</span></div>";
    out << "<h3>" << htmlEscape(upgrade.title) << "</h3>";
    out << "<p>" << htmlEscape(upgrade.detail) << "</p>";
    out << "<div class=\"stat-grid\">" << resourceChipGrid(upgrade.effectChips) << "</div>";
    out << "<div class=\"draft-card-footer\"><span>" << htmlEscape("This landing only") << "</span>"
        << panelButton(upgrade.action) << "</div></article>";
    return out.str();
}

std::string miniDroneCard(const MiniDroneCardPresentation& drone)
{
    std::ostringstream out;
    out << "<article class=\"ops-card drone-card\">";
    out << "<div class=\"card-topline\"><span>" << htmlEscape(drone.role) << "</span><span>"
        << htmlEscape(drone.rarity) << "</span></div>";
    out << "<div class=\"module-art\"><span>" << htmlEscape(drone.title.empty() ? "D" : std::string(1, drone.title.front())) << "</span></div>";
    out << "<h3>" << htmlEscape(drone.title) << "</h3>";
    out << "<p>" << htmlEscape(drone.detail) << "</p>";
    out << "<div class=\"stat-grid\">" << resourceChipGrid(drone.effectChips) << "</div>";
    out << "<div class=\"card-footer\"><span>" << htmlEscape(drone.status)
        << "</span>" << panelButton(drone.action) << "</div></article>";
    return out.str();
}

std::string primarySurfaceActionCard(const SurfaceActionPreviewPresentation& action)
{
    std::ostringstream out;
    out << "<article class=\"surface-primary-action\">";
    out << "<div><span>" << htmlEscape(action.cost) << " / " << htmlEscape(action.risk) << " "
        << htmlEscape(action.riskLabel) << "</span>";
    out << "<h3>" << htmlEscape(action.title) << "</h3>";
    out << "<p>" << htmlEscape(action.detail) << "</p>";
    out << "<div class=\"stat-grid\">" << resourceChipGrid(action.payoffChips) << "</div></div>";
    out << panelButton(action.action);
    out << "</article>";
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

std::string inventoryItemCard(const InventoryItemPresentation& item)
{
    std::ostringstream out;
    out << "<article class=\"inventory-item " << htmlEscape(item.cssClass) << "\">";
    out << "<div class=\"inventory-art\"><span>" << htmlEscape(item.glyph) << "</span></div>";
    out << "<div><h3>" << htmlEscape(item.title) << "</h3><p>" << htmlEscape(item.detail) << "</p></div>";
    out << "<strong>" << htmlEscape(item.count) << "</strong>";
    out << "</article>";
    return out.str();
}

std::string inventorySection(const InventorySectionPresentation& section)
{
    std::ostringstream out;
    out << "<section class=\"inventory-section\"><div class=\"inventory-section-head\"><h3>"
        << htmlEscape(section.title) << "</h3><p>" << htmlEscape(section.detail) << "</p></div>";
    out << "<div class=\"inventory-grid\">";
    for (const InventoryItemPresentation& item : section.items) {
        out << inventoryItemCard(item);
    }
    out << "</div></section>";
    return out.str();
}

std::string inventoryBody(const InventoryPresentation& inventory)
{
    std::ostringstream out;
    out << "<div class=\"inventory-modal\">";
    out << "<div class=\"metric-grid inventory-summary\">";
    for (const PanelMetricPresentation& metricItem : inventory.summary) {
        out << metric(metricItem.label, metricItem.value);
    }
    out << "</div>";
    for (const InventorySectionPresentation& section : inventory.sections) {
        out << inventorySection(section);
    }
    out << "</div>";
    return out.str();
}

std::string inventoryTemplate(const GameState& state, const ContentCatalog& catalog)
{
    return modalTemplate(ui::modals::inventory, "Inventory", inventoryBody(inventoryPresentation(state, catalog)));
}

std::string inventoryBank(const GameState& state, const ContentCatalog& catalog)
{
    const InventoryPresentation inventory = inventoryPresentation(state, catalog);
    std::ostringstream out;
    out << "<section class=\"resource-bank inventory-bank\"><div><h2>" << htmlEscape("Inventory")
        << "</h2><p>" << htmlEscape("Recovered stock available for upgrades, research, and field work.") << "</p></div>";
    out << "<div class=\"stat-grid\">";
    for (const PanelMetricPresentation& metricItem : inventory.summary) {
        out << resourceChip(metricItem);
    }
    out << "</div>" << modalButton("Open inventory", ui::modals::inventory, "ghost") << "</section>";
    return out.str();
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
    out << "<div class=\"panel-head\"><div><span class=\"game-mark\">" << htmlEscape(text::panel::title)
        << "</span><h1>" << htmlEscape(phaseTitle(state.screen)) << "</h1></div>"
        << "<div class=\"panel-head-actions\">"
        << modalButton("Inventory", ui::modals::inventory, "ghost")
        << modalButton(text::buttons::settings, ui::modals::settings, "ghost")
        << "</div></div>";
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

    out << "<div class=\"metric-grid panel-kpis\">";
    for (const PanelMetricPresentation& metricItem : headerMetrics) {
        out << metric(metricItem.label, metricItem.value);
    }
    out << "</div>";

    if (state.screen == Screen::Navigation) {
        const std::vector<const Destination*> destinations = navigationDestinations(state, catalog);
        out << phaseBoardOpen("phase-board-navigation", state.statusLine);
        out << "<div class=\"phase-titlebar\"><div><h2>" << htmlEscape("Solar System Navigation")
            << "</h2><p>" << htmlEscape(hostileSystemActive(state)
                ? "The Ark is stranded. Pick the next shuttle sortie, then prep the crew and vehicle."
                : "Plot the next route through known space.") << "</p></div></div>";
        out << "<section class=\"resource-bank ark-status\"><div><h2>" << htmlEscape("Ark status")
            << "</h2><p>" << htmlEscape(std::string(toString(state.meta.ark.condition))) << "</p></div>"
            << "<div class=\"stat-grid\">"
            << resourceChipGrid({
                panelMetric("Campaign", std::string(toString(state.meta.campaignMilestone))),
                panelMetric("System", state.meta.navigation.currentSystemId.empty() ? "Solar system" : state.meta.navigation.currentSystemId),
                panelMetric("Ark hull", display::percent(static_cast<double>(state.meta.ark.hullDamage) / 100.0)),
                panelMetric("Ark fuel", std::to_string(state.meta.ark.fuelReserve))
            })
            << "</div></section>";

        if (destinations.empty()) {
            out << boardNote("No mapped destinations yet. Continue the frontier ladder to discover the Ark.");
        } else {
            out << "<section class=\"board-primary navigation-map\"><h2>" << htmlEscape("Choose sortie") << "</h2><div class=\"ops-grid nav-grid\">";
            for (int index = 0; index < static_cast<int>(destinations.size()); ++index) {
                const Destination& destination = *destinations[static_cast<std::size_t>(index)];
                const bool selected = state.meta.navigation.selectedDestinationId == destination.id;
                const int fuelCost = 2 + destination.tier;
                const int danger = static_cast<int>(std::round(destination.hazard * 24.0));
                const int value = static_cast<int>(std::round(destination.baseReward));
                const int durability = 35 + destination.tier * 12;
                out << "<article class=\"ops-card nav-card " << (selected ? "selected" : "") << "\">";
                out << "<div class=\"card-kicker\"><span>" << htmlEscape("Fuel " + std::to_string(fuelCost))
                    << "</span><span>" << htmlEscape("Danger " + display::percent(static_cast<double>(danger) / 100.0)) << "</span></div>";
                out << "<h3>" << htmlEscape(destination.name) << "</h3>";
                out << "<p>" << htmlEscape(destination.tier >= 4
                    ? "Hostile-system sortie. Rich resources, artifact leads, and enemy pressure."
                    : "Known solar-system route.") << "</p>";
                out << "<div class=\"stat-grid\">";
                out << resourceChip(panelMetric("Value", std::to_string(value)));
                out << resourceChip(panelMetric("Terrain", display::percent(static_cast<double>(durability) / 100.0)));
                out << resourceChip(panelMetric("Artifacts", destination.tier >= 4 ? "Possible" : "None"));
                out << resourceChip(panelMetric("Enemies", destination.tier >= 4 ? "Detected" : "None"));
                out << "</div>";
                out << "<div class=\"card-footer\"><span>" << htmlEscape(selected ? "Selected" : "Mapped")
                    << "</span>" << button("Plot course", ui::actions::selectNavigationDestination(index), selected ? "ok" : "warn")
                    << "</div></article>";
            }
            out << "</div></section>";
        }
        out << phaseBoardClose();
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (state.screen == Screen::ArrivalFanfare) {
        const Destination* arrivalDestination = catalog.findDestination(state.lastOutcome.destinationId);
        const std::string destinationName = arrivalDestination == nullptr ? currentFrontier.name : arrivalDestination->name;
        const bool closeCall = !launchOutcomeAchievements(state.lastOutcome).empty();
        out << "<div data-arrival-fanfare=\"1\" data-arrival-destination=\"" << htmlEscape(destinationName)
            << "\" data-arrival-close-call=\"" << (closeCall ? "1" : "0") << "\" hidden></div>";
        out << "<section class=\"arrival-fanfare-panel\">";
        out << "<h2>" << htmlEscape("Arrival lock") << "</h2>";
        out << "<p>" << htmlEscape("Mission control has the vehicle on target. Approach choices are coming online.") << "</p>";
        out << "<div class=\"metric-grid flight-readout\">";
        out << metric(text::labels::burnDepth, display::multiplier(state.lastOutcome.ejectMultiplier));
        out << metric(text::labels::failurePoint, display::multiplier(state.lastOutcome.crashMultiplier));
        out << metric(text::labels::peakWarning, display::percent(state.lastOutcome.peakWarning));
        out << "</div>";
        out << "</section>";
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (layoutMode == PanelLayoutMode::ControlPanel && state.screen == Screen::Flyby) {
        const FlybyRunState& flyby = state.run.flyby;
        const Destination* flybyDestination = catalog.findDestination(flyby.destinationId);
        const std::string destinationName = flybyDestination == nullptr ? currentFrontier.name : flybyDestination->name;
        const double remaining = std::max(0.0, flyby.durationSeconds - flyby.elapsedSeconds);
        const double activeSeconds = std::max(0.01, flyby.missSeconds + flyby.goodSeconds + flyby.perfectSeconds);
        const double goodShare = (flyby.goodSeconds + flyby.perfectSeconds) / activeSeconds;
        const double perfectShare = flyby.perfectSeconds / activeSeconds;
        const FlybyGrade grade = flyby.completed ? flyby.result : FlybyGrade::Active;

        out << "<div data-flyby-run=\"1\" data-flyby-completed=\"" << (flyby.completed ? "1" : "0") << "\" hidden></div>";
        out << "<h2>" << htmlEscape("Manual Flyby") << "</h2>";
        out << "<p class=\"phase-copy\">" << htmlEscape("Steer through " + destinationName + "'s approach corridor. Perfect timing creates a slingshot window for the next launch.") << "</p>";
        const std::string zoneValue = flyby.collidedWithBody
            ? "Impact"
            : (flyby.completed ? flybyGradeLabel(grade) : flybyZoneLabel(flyby.currentZone));

        out << "<div class=\"metric-grid flight-readout\">"
            << metric("Timer", std::to_string(static_cast<int>(std::ceil(remaining))) + "s")
            << metric("Zone", zoneValue)
            << metric("Good hold", display::percent(goodShare))
            << metric("Perfect hold", display::percent(perfectShare))
            << metric("Reward", "x" + display::fixed(flyby.rewardBonusScale, 1))
            << metric("Slingshot", "x" + display::fixed(flybySlingshotScale(flyby), 1))
            << "</div>";

        if (flyby.completed) {
            const std::string resultTitle = flyby.collidedWithBody ? "IMPACT RECORDED" : flybyGradeLabel(grade);
            const double flybySpeedScale = flyby.slingshotAwarded ? flyby.slingshotSpeedScale : flybySlingshotScale(flyby);
            const double flybyFuelBoost = flyby.slingshotAwarded ? flyby.slingshotFuelBoost : tuning::flyby::slingshotFuelBoost * flybySpeedScale;
            const double flybySpeedBoost = flyby.slingshotAwarded ? flyby.slingshotSpeedBoost : tuning::flyby::slingshotSpeedBoost * flybySpeedScale;
            const std::string resultBody = flyby.collidedWithBody
                ? "The ship clipped the destination body. Hull damage added and no flyby data was recovered."
                : (grade == FlybyGrade::Perfect
                    ? "Perfect corridor exit. Fast completion amplified the payout, and exit speed amplified the next-launch slingshot."
                    : flybyResultBody(grade));
            const std::string tagOne = flyby.collidedWithBody
                ? "Hull +" + std::to_string(tuning::flyby::impactHullDamage) + "%"
                : (grade == FlybyGrade::Miss ? "No flyby data" : "Flyby data secured");
            const std::string tagTwo = flyby.collidedWithBody
                ? "No recon recovered"
                : (grade == FlybyGrade::Perfect
                    ? "Reward x" + display::fixed(flyby.rewardBonusScale, 1)
                    : (grade == FlybyGrade::Good ? "Reward x" + display::fixed(flyby.rewardBonusScale, 1) : "No slingshot"));
            const std::string tagThree = flyby.collidedWithBody
                ? "Hull damage logged"
                : (grade == FlybyGrade::Perfect
                    ? "+" + display::fixed(flybyFuelBoost, 1) + " fuel, +" + display::fixed(flybySpeedBoost, 2) + " speed"
                    : (grade == FlybyGrade::Good ? "Clean exit gate" : "Retry from approach"));
            out << "<div data-flyby-stamp=\"1\" data-flyby-title=\"" << htmlEscape(resultTitle)
                << "\" data-flyby-body=\"" << htmlEscape(resultBody)
                << "\" data-flyby-tag-one=\"" << htmlEscape(tagOne)
                << "\" data-flyby-tag-two=\"" << htmlEscape(tagTwo)
                << "\" data-flyby-tag-three=\"" << htmlEscape(tagThree)
                << "\" hidden></div>";
        }

        out << "<section class=\"cockpit-hud flight-hud\"><div class=\"cockpit-label\"><span>"
            << htmlEscape("Flyby controls") << "</span><strong>"
            << htmlEscape(flyby.completed ? "Confirm the result" : "W/S speed, A/D rotate") << "</strong></div>";
        if (flyby.completed) {
            out << "<p class=\"cockpit-hold-copy\">" << htmlEscape("Result locked. Click the mission stamp or press Space to continue.") << "</p>";
        } else {
            out << "<p class=\"cockpit-hold-copy\">" << htmlEscape("W/Up faster, S/Down slower. A/Left rotates counter-clockwise; D/Right rotates clockwise. Escape aborts as a Miss.") << "</p>";
            out << "<div class=\"actions primary-actions\">"
                << panelButton(panelActionButton("Abort flyby", ui::actions::flybyAbort, "danger"))
                << "</div>";
        }
        out << "</section>";
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
            context.pressureReliefUsed);
        if (!context.flightArmed) {
            out << "<div data-preflight-launch=\"1\" hidden></div>";
        }

        out << "<h2>" << htmlEscape(launchPanel.sectionTitle) << "</h2>";
        out << "<div class=\"metric-grid flight-readout\">";
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
        const bool hasAdvancedFlightControls = !launchPanel.systemActions.empty();
        out << tutorialCard(
            "launch-controls",
            "Press your luck, then return to Earth",
            hasAdvancedFlightControls
                ? "Return to Earth banks data but still has return risk. Eject is the expensive emergency out. If gauges climb, cut engines, open the relief valve, or jettison cargo to buy time with tradeoffs."
                : "Return to Earth banks data but still has return risk. Eject is the expensive emergency out. Learn the burn, bank flight data, and unlock the Moon route.");

        out << "<section class=\"cockpit-hud flight-hud\"><div class=\"cockpit-label\"><span>"
            << htmlEscape(text::panel::sections::flightControls) << "</span><strong>"
            << htmlEscape(context.flightArmed ? "Choose the next move" : "Start the burn when ready") << "</strong></div>";
        if (!context.flightArmed) {
            out << "<p class=\"cockpit-hold-copy\">" << htmlEscape("Review the burn profile, then use the cockpit launch control beside the vehicle.") << "</p>";
        } else {
            out << "<div class=\"actions primary-actions\">";
            for (const FlightActionButtonPresentation& action : launchPanel.primaryActions) {
                out << panelButton(action);
            }
            out << "</div>";

            if (hasAdvancedFlightControls) {
                out << "<div class=\"actions system-actions\">";
                for (const FlightActionButtonPresentation& action : launchPanel.systemActions) {
                    out << panelButton(action);
                }
                out << "</div>";
            }
        }
        out << "</section>";
        out << modalTemplate(ui::modals::telemetry, text::panel::modals::telemetryDetails, detailStack(launchPanel.telemetryDetails));
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
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
        out << crewFateCard(presentation.crewFate);
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
        out << inventoryTemplate(state, catalog);
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
        out << "<div class=\"phase-titlebar\"><div><h2>" << htmlEscape(text::panel::sections::arrivalOps)
            << "</h2><p>" << htmlEscape("You made it. Choose how boldly to engage the destination.") << "</p></div></div>";
        const LaunchOutcomePresentation arrivalResult = launchOutcomePresentation(state.lastOutcome);
        out << "<h2>" << htmlEscape("Arrival summary") << "</h2>";
        out << crewFateCard(arrivalResult.crewFate);
        out << "<div class=\"result-grid\">";
        for (const LaunchOutcomeMetricGroupPresentation& group : arrivalResult.metricGroups) {
            out << resultMetricGroup(group);
        }
        out << "</div>";
        for (const std::string& note : arrivalResult.notes) {
            out << boardNote(note);
        }
        if (!arrivalResult.achievements.empty()) {
            out << "<h2>" << htmlEscape(text::panel::sections::achievements) << "</h2>";
            out << "<div class=\"achievement-grid\">";
            for (const AchievementPresentation& achievement : arrivalResult.achievements) {
                out << achievementCard(achievement);
            }
            out << "</div>";
        }
        out << tutorialCard(
            "arrival-ops",
            "Flyby, orbit, then land",
            "Flyby is the safest scan. Orbit opens stronger science. Landing is the risky payoff for surface work and mining. The Moon requires flyby and orbit first; later worlds let you gamble.");
        out << "<h2>" << htmlEscape("Choose approach") << "</h2>";
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
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (state.screen == Screen::Research) {
        const ResearchPhasePresentation researchPanel = researchPhasePresentation(state, catalog);
        out << phaseBoardOpen("phase-board-research", state.statusLine);
        out << "<div class=\"phase-titlebar\"><div><h2>" << htmlEscape(text::panel::sections::research)
            << "</h2><p>" << htmlEscape("Turn arrival data into better tools, or continue down to the surface.") << "</p></div>"
            << "<div class=\"utility-row compact-tools\">" << modalButton(text::buttons::details, ui::modals::research, "ghost")
            << "</div></div>";
        out << phaseAdvisory(researchPanel.advisory);
        out << "<div class=\"metric-grid focus-metrics\">";
        for (const PanelMetricPresentation& metricItem : researchPanel.metrics) {
            out << metric(metricItem.label, metricItem.value);
        }
        out << "</div>";
        out << "<section class=\"board-primary\"><h2>" << htmlEscape("Research options") << "</h2><div class=\"ops-grid\">";
        for (const ResearchProjectCardPresentation& project : researchPanel.projects) {
            out << researchProjectCard(project);
        }
        out << "</div></section>";
        out << "<div class=\"actions\">";
        out << panelButton(researchPanel.skipAction);
        out << "</div>";
        out << phaseBoardClose();
        out << modalTemplate(ui::modals::phaseBriefing, researchPanel.briefing.title, detailStack(researchPanel.briefing.rows));
        out << modalTemplate(ui::modals::research, text::panel::modals::researchDetails, detailStack(researchPanel.details));
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (state.screen == Screen::Mining) {
        const MiningRunPresentation miningPanel = miningRunPresentation(state, catalog);
        out << phaseBoardOpen("phase-board-mining", state.statusLine, false);
        out << "<div class=\"phase-titlebar\"><div><h2>" << htmlEscape(text::panel::sections::miningRun)
            << "</h2><p>" << htmlEscape("Drill tunnels, scan shadows, and stow payload before oxygen or extraction risk turns ugly.") << "</p></div>"
            << "<div class=\"utility-row compact-tools\">" << modalButton(text::buttons::details, ui::modals::surface, "ghost")
            << modalButton("Inventory", ui::modals::inventory, "ghost")
            << "</div></div>";
        out << tutorialCard(
            "mining-basics",
            "Dig, scan, stow",
            "Move with WASD or arrows, aim with the mouse, hold Space or click to drill, E pulses the scanner, and R stows payload. Bring back materials and artifacts before oxygen or extraction risk gets ugly.");
        if (miningPanel.failurePending) {
            out << "<div class=\"phase-advisory danger mining-failure-callout\"><strong>" << htmlEscape(miningPanel.failureTitle)
                << "</strong><span>" << htmlEscape(miningPanel.failureBody) << "</span></div>";
        }
        out << "<div class=\"metric-grid mining-metrics\">";
        for (const PanelMetricPresentation& metricItem : miningPanel.metrics) {
            out << metric(metricItem.label, metricItem.value);
        }
        out << "</div>";
        out << "<section class=\"cockpit-hud mining-hud\"><div class=\"cockpit-label\"><span>"
            << htmlEscape("Mining controls") << "</span><strong>" << htmlEscape("Scan, stow, or abort") << "</strong></div>";
        out << "<div class=\"actions system-actions\">";
        for (const PanelButtonPresentation& action : miningPanel.actions) {
            out << panelButton(action);
        }
        out << "</div></section>";
        out << phaseBoardClose();
        if (miningPanel.failurePending) {
            std::ostringstream failureBody;
            failureBody << "<div class=\"phase-advisory danger mining-failure-callout\"><strong>" << htmlEscape(miningPanel.failureTitle)
                << "</strong><span>" << htmlEscape(miningPanel.failureBody) << "</span></div>";
            failureBody << "<div class=\"modal-actions actions\">"
                << panelButton(panelActionButton("Return to Surface Ops", ui::actions::miningFailureAck, "danger"))
                << "</div>";
            out << autoModalTemplate(ui::modals::miningFailure, miningPanel.failureTitle, failureBody.str());
        }
        out << modalTemplate(ui::modals::surface, text::panel::modals::surfaceDetails, detailStack(miningPanel.details));
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (state.screen == Screen::DroneOps) {
        const DroneOpsPresentation dronePanel = droneOpsPresentation(state, catalog);
        out << phaseBoardOpen("phase-board-drone-ops", state.statusLine);
        out << "<div class=\"phase-titlebar\"><div><h2>" << htmlEscape("Drone Ops")
            << "</h2><p>" << htmlEscape("Assign persistent helper drones before committing the mining rig.") << "</p></div>"
            << "<div class=\"utility-row compact-tools\">" << modalButton(text::buttons::details, ui::modals::surface, "ghost")
            << "</div></div>";
        out << "<div class=\"metric-grid focus-metrics\">";
        for (const PanelMetricPresentation& metricItem : dronePanel.metrics) {
            out << metric(metricItem.label, metricItem.value);
        }
        out << "</div>";
        const std::vector<PanelMetricPresentation> droneSlotChips {panelMetric("Next slot", dronePanel.nextSlotCost)};
        out << "<section class=\"resource-bank\"><div><h2>" << htmlEscape("Drone Bay")
            << "</h2><p>" << htmlEscape("Slots persist across expeditions. Equipped drones support the next mining run.") << "</p></div>"
            << "<div class=\"stat-grid\">" << resourceChipGrid(droneSlotChips) << "</div>"
            << panelButton(dronePanel.upgradeSlotAction) << "</section>";
        out << "<section class=\"board-primary drone-roster\"><h2>" << htmlEscape("Drone roster") << "</h2><div class=\"ops-grid\">";
        for (const MiniDroneCardPresentation& drone : dronePanel.drones) {
            out << miniDroneCard(drone);
        }
        out << "</div></section>";
        out << "<div class=\"actions\">" << panelButton(dronePanel.backAction) << "</div>";
        out << phaseBoardClose();
        out << modalTemplate(ui::modals::surface, "Drone Ops Details", detailStack(dronePanel.details));
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (state.screen == Screen::SurfaceExpedition) {
        const SurfaceExpeditionPresentation surfacePanel = surfaceExpeditionPresentation(state, catalog);
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
        out << surfaceCommandSummary(surfacePanel);
        out << surfaceKpiGrid(expedition, extractionRisk);
        if (surfacePanel.droneOpsAction.enabled) {
            out << "<section class=\"resource-bank drone-ops-callout\"><div><h2>" << htmlEscape("Drone Ops")
                << "</h2><p>" << htmlEscape("Equip persistent helper drones before you launch the mining run.") << "</p></div>"
                << panelButton(surfacePanel.droneOpsAction) << "</section>";
        }
        const auto mineAction = std::find_if(surfacePanel.actions.begin(), surfacePanel.actions.end(), [](const SurfaceActionPreviewPresentation& action) {
            return action.action.actionId == ui::actions::mineSurface;
        });
        if (mineAction != surfacePanel.actions.end()) {
            out << primarySurfaceActionCard(*mineAction);
        }
        out << "<section class=\"board-primary surface-actions\">";
        out << "<h2>" << htmlEscape("Field actions") << "</h2>";
        out << "<div class=\"ops-grid\">";
        for (const SurfaceActionPreviewPresentation& action : surfacePanel.actions) {
            if (action.action.actionId == ui::actions::mineSurface) {
                continue;
            }
            out << surfaceActionCard(action);
        }
        out << "</div></section>";
        out << phaseBoardClose();
        out << modalTemplate(ui::modals::phaseBriefing, surfacePanel.briefing.title, detailStack(surfacePanel.briefing.rows));
        out << modalTemplate(ui::modals::surface, text::panel::modals::surfaceDetails, detailStack(surfacePanel.details));
        out << modalTemplate(ui::modals::missionLog, text::panel::sections::missionLog, missionLog(surfacePanel.logEntries));
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (state.screen == Screen::SurfaceUpgrade) {
        const SurfaceExpeditionPresentation surfacePanel = surfaceExpeditionPresentation(state, catalog);
        out << phaseBoardOpen("phase-board-surface-upgrade phase-board-draft-room", state.statusLine);
        out << "<section class=\"draft-hero\"><div><span>" << htmlEscape("Surface draft")
            << "</span><h2>" << htmlEscape("Pick your field edge") << "</h2><p>"
            << htmlEscape("Choose one temporary upgrade for this landing. The rest leave with the cargo shuttle.") << "</p></div>";
        std::vector<PanelMetricPresentation> fieldContext;
        if (!surfacePanel.metrics.empty()) {
            fieldContext.push_back(surfacePanel.metrics[0]);
        }
        for (const PanelMetricPresentation& metricItem : surfacePanel.metrics) {
            if (metricItem.label == text::labels::supply || metricItem.label == text::labels::cargo ||
                metricItem.label == text::labels::extractionRisk || metricItem.label == text::labels::rareMaterials) {
                fieldContext.push_back(metricItem);
            }
        }
        out << "<div class=\"stat-grid draft-context\">" << resourceChipGrid(fieldContext) << "</div></section>";
        out << "<section class=\"draft-board\"><div class=\"phase-titlebar\"><div><h2>"
            << htmlEscape("Choose one field mod") << "</h2><p>"
            << htmlEscape("Scanner, drill, and drone tech change how the next dig feels. Take one, reroll the draft, or walk away.") << "</p></div></div>";
        out << "<div class=\"pilot-card-grid draft-card-grid\">";
        for (const SurfaceUpgradeCardPresentation& upgrade : surfacePanel.upgradeOffers) {
            out << surfaceUpgradeCard(upgrade);
        }
        out << "</div><div class=\"actions draft-actions\">";
        const double rerollCost = offerRerollCost(state);
        out << panelButton(state.run.credits >= rerollCost
            ? panelActionButton(std::string("Reroll draft (") + display::money(rerollCost) + ")", ui::actions::rerollOffers, "warn")
            : disabledPanelButton(display::needCredits(rerollCost)));
        out << panelButton(panelActionButton("Skip field pick", ui::actions::next));
        out << "</div></section>";
        out << phaseBoardClose();
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (state.screen == Screen::Upgrade) {
        const RefitWindowPresentation refitWindow = refitWindowPresentation(state, catalog);
        out << phaseBoardOpen("phase-board-refit phase-board-draft-room", state.statusLine);
        out << "<section class=\"draft-hero\"><div><span>" << htmlEscape("Hangar draft")
            << "</span><h2>" << htmlEscape("Choose the next refit") << "</h2><p>"
            << htmlEscape("Install one ship or crew upgrade before the next launch window closes.") << "</p></div>";
        out << "<div class=\"stat-grid draft-context\">" << resourceChipGrid(refitWindow.resourceChips) << "</div></section>";
        if (!refitWindow.recoveryDetail.empty()) {
            out << "<p class=\"draft-recovery-note\">" << htmlEscape(refitWindow.recoveryDetail) << "</p>";
        }
        out << "<section class=\"draft-board\"><div class=\"phase-titlebar\"><div><h2>"
            << htmlEscape("Choose one permanent upgrade") << "</h2><p>"
            << htmlEscape("These improvements carry forward into future vehicles. Pick the build direction, reroll, or save resources.") << "</p></div></div><div class=\"pilot-card-grid draft-card-grid\">";
        for (const RefitOfferPresentation& offer : refitWindow.offers) {
            out << refitOfferCard(offer);
        }
        out << "</div><div class=\"actions draft-actions\">";
        out << panelButton(refitWindow.rerollAction);
        out << panelButton(refitWindow.skipAction);
        out << "</div></section>";
        out << phaseBoardClose();
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
    launchBlockedBody << detailStack(launchReadiness.details);
    launchBlockedBody << "<div class=\"modal-actions actions\">";
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
        if (astronaut == nullptr && card.actionId == ui::actions::recruitCrew) {
            out << operationModalCard(card, "Choose pilot", ui::modals::pilotIntake);
        } else {
            out << operationCard(card);
        }
    }
    out << "</div>";

    out << "<div class=\"actions\">";
    if (navigationAvailable(state)) {
        out << button("Open Navigation", ui::actions::openNavigation, "warn");
    }
    if (arkDiscovered(state) && !hostileSystemActive(state)) {
        out << button(state.meta.ark.firstJumpComplete ? "Attempt next Ark jump" : "Make first Ark jump", ui::actions::arkJump, "warn");
    }
    out << (launchReadiness.blocked
        ? modalButton(text::buttons::launchProvingFlight, ui::modals::launchBlocked, "ok")
        : button(text::buttons::launchProvingFlight, ui::actions::prepareLaunch, "ok"));
    if (next != nullptr && !navigationAvailable(state)) {
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
    out << modalTemplate(ui::modals::pilotIntake, text::panel::modals::pilotIntake, pilotIntakeBody.str());
    out << modalTemplate(ui::modals::legacy, text::panel::modals::legacy, legacyBody);
    out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
    out << inventoryTemplate(state, catalog);

    return out.str();
}

} // namespace rocket
