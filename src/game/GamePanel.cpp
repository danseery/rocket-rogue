#include "game/GamePanel.h"
#include "core/CrewPresentation.h"
#include "core/GameFormat.h"
#include "core/GameText.h"
#include "core/HangarPresentation.h"
#include "core/LaunchPresentation.h"
#include "core/LaunchReadinessPresentation.h"
#include "core/OutcomePresentation.h"
#include "core/PanelChromePresentation.h"
#include "core/ProgramPresentation.h"
#include "core/RefitPresentation.h"
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

std::string button(std::string_view label, std::string_view action, std::string cssClass = "")
{
    const std::string classAttr = cssClass.empty() ? "" : " class=\"" + cssClass + "\"";
    return "<button" + classAttr + " data-rr-action=\"" + htmlEscape(action) + "\">" + htmlEscape(label) + "</button>";
}

std::string modalButton(std::string_view label, std::string_view modalId, std::string cssClass = "")
{
    const std::string classAttr = cssClass.empty() ? "" : " class=\"" + cssClass + "\"";
    return "<button" + classAttr + " data-ui-modal=\"" + htmlEscape(modalId) + "\">" + htmlEscape(label) + "</button>";
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

std::string statChipGrid(const std::vector<RefitStatChip>& chips)
{
    std::string tags;
    for (const RefitStatChip& chip : chips) {
        tags += statChip(chip);
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
    out << "<div class=\"card-footer\"><span>" << htmlEscape(display::credits(offer.cost)) << "</span>"
        << panelButton(offer.action) << "</div></article>";
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

    std::ostringstream out;
    out << "<div class=\"panel-head\"><div><h1>" << htmlEscape(text::panel::title) << "</h1></div>"
        << modalButton(text::buttons::settings, ui::modals::settings, "ghost") << "</div>";
    out << "<p class=\"status\">" << htmlEscape(state.statusLine) << "</p>";

    std::ostringstream settingsBody;
    settingsBody << detailStack(settingsDetailsPresentation());
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
        out << "<p class=\"status\">" << htmlEscape(launchPanel.telemetryMessage) << "</p>";

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
        const LaunchOutcomePresentation presentation = launchOutcomePresentation(state.lastOutcome);
        out << "<h2>" << htmlEscape(text::panel::sections::result) << "</h2>";
        out << "<div class=\"metric-grid\">";
        out << metric(text::labels::outcome, std::string(presentation.label));
        out << metric(text::labels::recovery, std::string(toString(state.lastOutcome.recoveryMethod)));
        out << metric(text::labels::burnDepth, display::multiplier(state.lastOutcome.ejectMultiplier));
        out << metric(text::labels::failurePoint, display::multiplier(state.lastOutcome.crashMultiplier));
        out << metric(text::labels::peakWarning, display::percent(state.lastOutcome.peakWarning));
        out << metric(text::labels::peakAbort, display::percent(state.lastOutcome.peakAbortRisk));
        out << metric(text::labels::creditDelta, display::signedMoney(state.lastOutcome.payout - state.lastOutcome.recoveryCost));
        out << "</div>";
        for (const std::string& note : presentation.notes) {
            out << "<p class=\"status\">" << htmlEscape(note) << "</p>";
        }
        out << "<div class=\"actions\">";
        out << button(presentation.nextActionLabel, ui::actions::next, "ok");
        out << "</div>";
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        return out.str();
    }

    if (state.screen == Screen::Upgrade) {
        const RefitWindowPresentation refitWindow = refitWindowPresentation(state, catalog);
        out << "<h2>" << htmlEscape(text::panel::sections::refitWindow) << "</h2>";
        out << "<p>" << htmlEscape(text::panel::messages::chooseOneRefit) << "</p>";
        out << "<div class=\"upgrade-grid\">";
        for (const RefitOfferPresentation& offer : refitWindow.offers) {
            out << refitOfferCard(offer);
        }
        out << "</div><div class=\"actions\">";
        out << panelButton(refitWindow.rerollAction);
        out << panelButton(refitWindow.skipAction);
        out << "</div>";
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

    const std::string legacyBody = detailStack(legacyDetailsPresentation(state));

    out << "<div class=\"utility-row bottom-tools\">";
    out << modalButton(text::buttons::legacy, ui::modals::legacy, "ghost");
    out << "</div>";
    out << modalTemplate(ui::modals::ship, text::panel::modals::shipDetails, shipBody);
    out << modalTemplate(ui::modals::crew, text::panel::modals::crewDetails, crewBody);
    out << modalTemplate(ui::modals::frontier, text::panel::modals::frontierDetails, frontierBody);
    out << modalTemplate(ui::modals::launchBlocked, text::panel::modals::launchHold, launchBlockedBody.str());
    out << modalTemplate(ui::modals::legacy, text::panel::modals::legacy, legacyBody);
    out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());

    return out.str();
}

} // namespace rocket
