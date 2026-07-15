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

std::string metric(std::string_view label, std::string value, std::string_view cssClass = {})
{
    std::string classAttr = label == text::labels::chapter ? " metric-chapter" : "";
    if (!cssClass.empty()) {
        classAttr += " ";
        classAttr += cssClass;
    }
    return "<div class=\"metric" + classAttr + "\"><strong>" + htmlEscape(value) + "</strong><span>" + htmlEscape(label) + "</span></div>";
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
        return "Science";
    case Screen::SurfaceExpedition:
        return "Surface Ops";
    case Screen::SurfaceUpgrade:
        return "Field Upgrade";
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

std::string button(std::string_view label, std::string_view action, std::string cssClass = "")
{
    const std::string classAttr = cssClass.empty() ? "" : " class=\"" + cssClass + "\"";
    return "<button" + classAttr + " data-rr-action=\"" + htmlEscape(action) + "\">" + htmlEscape(label) + "</button>";
}

std::string missionStamp(
    std::string_view kicker,
    std::string_view title,
    std::string_view detail,
    std::string_view tagOne,
    std::string_view tagTwo,
    std::string_view tagThree,
    std::string_view continueAction)
{
    std::ostringstream out;
    out << "<section class=\"arrival-fanfare-panel\">"
        << "<span class=\"arrival-stamp-kicker\">" << htmlEscape(kicker) << "</span>"
        << "<h2 class=\"arrival-stamp-title\">" << htmlEscape(title) << "</h2>"
        << "<strong class=\"arrival-stamp-destination\">" << htmlEscape(detail) << "</strong>"
        << "<div class=\"arrival-stamp-tags\"><span>" << htmlEscape(tagOne)
        << "</span><span>" << htmlEscape(tagTwo) << "</span>";
    if (!tagThree.empty()) {
        out << "<span class=\"gold\">" << htmlEscape(tagThree) << "</span>";
    }
    out << "</div>"
        << button("Click or press Space to continue", continueAction, "arrival-stamp-continue")
        << "</section>";
    return out.str();
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

std::string autoModalTemplate(std::string_view modalId, std::string_view title, std::string body, bool dismissible = true)
{
    return "<template data-modal=\"" + htmlEscape(modalId) + "\" data-auto-modal=\"1\" data-modal-dismissible=\"" +
        (dismissible ? "1" : "0") + "\" data-title=\"" + htmlEscape(title) + "\">" + body + "</template>";
}

std::string disabledButton(std::string_view label)
{
    return "<button class=\"disabled\" disabled>" + htmlEscape(label) + "</button>";
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

std::string statChip(const RefitStatChip& chip)
{
    const bool wideChip = chip.label.size() > 8 || chip.label.find(' ') != std::string::npos;
    return "<span class=\"stat-chip " + std::string(chip.positive ? "up" : "down") +
        std::string(wideChip ? " wide" : "") + "\">" +
        htmlEscape(chip.label) + " " + htmlEscape(chip.value) + "</span>";
}

std::string resourceChip(const PanelMetricPresentation& chip)
{
    const bool positive = chip.value.empty() || chip.value.front() != '-';
    const bool wideChip = chip.label.size() > 8 || chip.label.find(' ') != std::string::npos;
    return "<span class=\"stat-chip " + std::string(positive ? "up" : "down") +
        std::string(wideChip ? " wide" : "") + "\">" +
        htmlEscape(chip.label) + " " + htmlEscape(chip.value) + "</span>";
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
    if (label == text::labels::sharedFuel || label == text::labels::arkFuel) {
        return "Fuel";
    }
    return std::string(label);
}

std::string surfaceActionChip(const PanelMetricPresentation& chip)
{
    const std::string label = surfaceActionChipLabel(chip.label);
    const std::string text = label + " " + chip.value;
    const bool positive = chip.value.empty() || chip.value.front() != '-';
    const bool wideChip = text.size() > 11 || label.find(' ') != std::string::npos;
    return "<span class=\"stat-chip " + std::string(positive ? "up" : "down") +
        std::string(wideChip ? " wide" : "") + "\">" +
        htmlEscape(text) + "</span>";
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
        std::string(wideChip ? " wide" : "") + "\">" +
        htmlEscape(text) + "</span>";
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

std::string surfaceActionChipGrid(const std::vector<PanelMetricPresentation>& chips)
{
    std::string tags;
    for (const PanelMetricPresentation& chip : chips) {
        tags += surfaceActionChip(chip);
    }
    return tags;
}

bool isSurfaceMiningAction(const SurfaceActionPreviewPresentation& action)
{
    return action.action.actionId == ui::actions::mineSurface || action.title == text::buttons::mineDeposit;
}

std::string surfaceActionDetail(const SurfaceActionPreviewPresentation& action)
{
    return isSurfaceMiningAction(action) ? "Spend shared fuel for one mining run this loop." : action.detail;
}

PanelButtonPresentation surfaceActionFooterButton(const SurfaceActionPreviewPresentation& action)
{
    PanelButtonPresentation buttonPresentation = action.action;
    if (isSurfaceMiningAction(action) && buttonPresentation.enabled) {
        buttonPresentation.cssClass = buttonPresentation.cssClass.empty()
            ? "rare"
            : buttonPresentation.cssClass + " rare";
    }
    if (!buttonPresentation.enabled && (buttonPresentation.label.rfind("Need ", 0) == 0 || buttonPresentation.label.size() > 14)) {
        buttonPresentation.label = std::string(text::buttons::unavailable);
    }
    return buttonPresentation;
}

std::string operationCard(std::string title, std::string detail, std::string cost, std::string_view action, bool available, std::string cssClass = "")
{
    std::ostringstream out;
    out << "<article class=\"ops-card " << htmlEscape(cssClass) << "\">";
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
    out << "<article class=\"ops-card " << htmlEscape(card.cssClass) << "\">";
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
    out << "<div class=\"pilot-stat-grid metric-strip\">";
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
        return "Full loop completed inside the perfect research band. Bonus science and funding secured.";
    case OrbitGrade::Good:
        return "Full loop completed inside the orbital research band. Science and funding secured.";
    case OrbitGrade::Miss:
        return "The orbital track fell outside the research band before a loop was completed. No orbit data banked.";
    case OrbitGrade::Active:
    default:
        return "Complete one full loop before the insertion timer expires.";
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

std::string refitOfferCard(const RefitOfferPresentation& offer)
{
    const RefitPresentation& presentation = offer.card;
    std::ostringstream out;
    out << "<article class=\"pilot-card upgrade-draft-card slot-" << htmlEscape(presentation.slotClass) << " "
        << rarityCardClass(presentation.rarity) << "\">";
    out << "<div class=\"pilot-card-top\"><span>" << htmlEscape(presentation.category) << "</span><strong>"
        << htmlEscape(presentation.rarity) << " refit</strong></div>";
    out << "<div class=\"pilot-portrait-placeholder draft-art\"><span>" << htmlEscape(presentation.glyph) << "</span></div>";
    out << "<h3 class=\"card-title\">" << htmlEscape(presentation.title) << "</h3>";
    out << "<p class=\"card-copy\">" << htmlEscape(presentation.detail) << "</p>";
    out << "<strong class=\"module-impact\">" << htmlEscape(presentation.primaryImpact) << "</strong>";
    out << "<div class=\"stat-grid chip-strip\">" << statChipGrid(presentation.statChips) << "</div>";
    out << "<div class=\"draft-card-footer action-row\"><span>" << htmlEscape(offer.footerCostSummary) << "</span>"
        << panelButton(offer.action) << "</div></article>";
    return out.str();
}

std::string researchProjectCard(const ResearchProjectCardPresentation& project)
{
    std::ostringstream out;
    out << "<article class=\"ops-card\">";
    out << "<div class=\"card-topline\"><span>" << htmlEscape(project.rarity) << "</span><span>"
        << htmlEscape(project.blueprintGain) << "</span></div>";
    out << "<h3 class=\"card-title\">" << htmlEscape(project.title) << "</h3>";
    out << "<p class=\"card-copy\">" << htmlEscape(project.detail) << "</p>";
    if (!project.reward.empty()) {
        out << "<strong class=\"module-impact\">" << htmlEscape(project.reward) << "</strong>";
    }
    out << "<div class=\"stat-grid chip-strip\">" << resourceChipGrid(project.resourceChips) << "</div>";
    out << "<div class=\"card-footer action-row\"><span>" << htmlEscape(project.materialCost) << "</span>"
        << panelButton(project.action) << "</div></article>";
    return out.str();
}

std::string surfaceActionCard(const SurfaceActionPreviewPresentation& action)
{
    std::ostringstream out;
    const bool isMining = isSurfaceMiningAction(action);
    const std::string detail = surfaceActionDetail(action);
    out << "<article class=\"ops-card surface-action-card" << (isMining ? " featured-action" : "") << "\">";
    out << "<div class=\"card-topline\"><span>" << htmlEscape(action.cost) << "</span><span>"
        << htmlEscape(action.risk) << " " << htmlEscape(surfaceActionChipLabel(action.riskLabel)) << "</span></div>";
    out << "<h3 class=\"card-title\">" << htmlEscape(action.title) << "</h3>";
    out << "<p class=\"card-copy ops-detail\">" << htmlEscape(detail) << "</p>";
    out << "<div class=\"stat-grid chip-strip\">" << surfaceActionChipGrid(action.payoffChips) << "</div>";
    out << "<div class=\"card-footer action-row\"><span class=\"surface-action-status\">" << htmlEscape(action.availability)
        << "</span>" << panelButton(surfaceActionFooterButton(action)) << "</div></article>";
    return out.str();
}

std::string phaseCardSlot(std::string cardHtml, std::string_view cssClass, bool isLast)
{
    return "<div class=\"phase-card-slot " + htmlEscape(cssClass) + (isLast ? " is-last" : "") + "\">" + std::move(cardHtml) + "</div>";
}

std::string surfaceUpgradeCard(const SurfaceUpgradeCardPresentation& upgrade)
{
    std::ostringstream out;
    out << "<article class=\"pilot-card upgrade-draft-card surface-upgrade-card " << rarityCardClass(upgrade.rarity) << "\">";
    out << "<div class=\"pilot-card-top\"><span>" << htmlEscape(upgrade.category) << "</span><strong>"
        << htmlEscape(upgrade.rarity) << " field mod</strong></div>";
    out << "<div class=\"pilot-portrait-placeholder draft-art\"><span>" << htmlEscape(draftInitial(upgrade.category, "F")) << "</span></div>";
    out << "<h3 class=\"card-title\">" << htmlEscape(upgrade.title) << "</h3>";
    out << "<p class=\"card-copy\">" << htmlEscape(upgrade.detail) << "</p>";
    out << "<div class=\"stat-grid chip-strip\">" << resourceChipGrid(upgrade.effectChips) << "</div>";
    out << "<div class=\"draft-card-footer action-row\"><span>" << htmlEscape("Until drone loss") << "</span>"
        << panelButton(upgrade.action) << "</div></article>";
    return out.str();
}

std::string miniDroneControlCard(const MiniDroneCardPresentation& drone)
{
    std::vector<PanelMetricPresentation> chips;
    chips.reserve(std::min<std::size_t>(drone.effectChips.size(), 4));
    for (std::size_t i = 0; i < drone.effectChips.size() && i < 4; ++i) {
        chips.push_back(drone.effectChips[i]);
    }

    std::ostringstream out;
    out << "<article class=\"drone-control-card " << rarityCardClass(drone.rarity) << "\">";
    out << "<div class=\"drone-card-head\"><span class=\"drone-role-mark\">"
        << htmlEscape(drone.role.empty() ? "D" : std::string(1, drone.role.front())) << "</span>"
        << "<div class=\"drone-card-id\"><div class=\"card-topline\"><span>" << htmlEscape(drone.role)
        << "</span><span>" << htmlEscape(drone.rarity) << "</span></div>"
        << "<h3 class=\"card-title\">" << htmlEscape(drone.title) << "</h3></div></div>";
    out << "<p class=\"card-copy drone-control-status\">" << htmlEscape(drone.status) << "</p>";
    out << "<div class=\"stat-grid chip-strip\">" << resourceChipGrid(chips) << "</div>";
    out << "<div class=\"card-footer action-row\">" << panelButton(drone.action)
        << panelButton(drone.upgradeAction) << "</div></article>";
    return out.str();
}

std::string droneLoadoutSlotCard(const DroneLoadoutSlotPresentation& slot)
{
    std::ostringstream out;
    out << "<article class=\"drone-loadout-slot " << htmlEscape(slot.cssClass) << "\">";
    out << "<div class=\"slot-card-head\"><span class=\"slot-number\">" << htmlEscape(std::to_string(slot.slot))
        << "</span>";
    if (!slot.action.label.empty()) {
        out << panelButton(slot.action);
    } else {
        out << "<strong class=\"slot-state\">" << htmlEscape(slot.status) << "</strong>";
    }
    out << "</div>";
    out << "<div class=\"slot-card-body\"><h3 class=\"card-title\">" << htmlEscape(slot.title) << "</h3>";
    out << "<p class=\"card-copy slot-role\">" << htmlEscape(slot.role) << "</p></div>";
    out << "<div class=\"stat-grid chip-strip\">" << resourceChipGrid(slot.chips) << "</div>";
    out << "</article>";
    return out.str();
}

std::string primarySurfaceActionCard(const SurfaceActionPreviewPresentation& action)
{
    std::ostringstream out;
    out << "<article class=\"surface-primary-action\">";
    out << "<div class=\"surface-primary-copy\"><div class=\"surface-action-topline\"><span>" << htmlEscape(action.cost)
        << "</span><span>" << htmlEscape(action.risk) << " " << htmlEscape(surfaceActionChipLabel(action.riskLabel)) << "</span></div>";
    out << "<h3 class=\"card-title\">" << htmlEscape(action.title) << "</h3>";
    out << "<p class=\"card-copy ops-detail\">" << htmlEscape(surfaceActionDetail(action)) << "</p>";
    out << "<div class=\"stat-grid chip-strip\">" << surfaceActionChipGrid(action.payoffChips) << "</div></div>";
    out << "<div class=\"surface-primary-control action-row\"><span>" << htmlEscape(action.availability) << "</span>";
    out << panelButton(action.action);
    out << "</div>";
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
    const PanelButtonPresentation& action)
{
    std::ostringstream out;
    out << "<article class=\"ops-card arrival-card\">";
    out << "<div class=\"card-topline\"><span>" << htmlEscape(risk) << "</span><span>" << htmlEscape(reward) << "</span></div>";
    out << "<h3 class=\"card-title\">" << htmlEscape(title) << "</h3>";
    out << "<p class=\"card-copy\">" << htmlEscape(detail) << "</p>";
    out << "<div class=\"card-footer action-row\"><span>" << htmlEscape(action.enabled ? std::string(text::panel::ready) : std::string(text::buttons::unavailable))
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
    std::string body = "<div class=\"detail-stack modal-body\">";
    for (const DetailPresentationRow& row : rows) {
        body += row.heading ? detailHeader(row.label) : detailRow(row.label, row.value);
    }
    body += "</div>";
    return body;
}

std::string missionLog(const std::vector<std::string>& entries)
{
    std::string body = "<div class=\"detail-stack modal-body\">";
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
    (void)status;
    std::string out = "<section class=\"phase-board " + htmlEscape(cssClass) + "\"";
    if (fullPanel) {
        out += " data-panel-mode=\"phase-board\"";
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

std::string surfacePosture(const SurfaceExpeditionPresentation& surface)
{
    return "<article class=\"phase-advisory surface-posture " + htmlEscape(surface.postureClass) + "\"><strong>" +
        htmlEscape(surface.postureTitle) + "</strong><span>" + htmlEscape(surface.postureDetail) + "</span></article>";
}

std::string surfaceCommandSummary(const SurfaceExpeditionPresentation& surface)
{
    std::ostringstream out;
    out << "<section class=\"surface-command\">";
    out << "<article class=\"surface-site-card\"><span>" << htmlEscape(text::labels::site) << "</span><strong>" << htmlEscape(surface.metrics[0].value)
        << "</strong><p>" << htmlEscape(surface.siteDetail) << "</p></article>";
    out << surfacePosture(surface);
    out << "</section>";
    return out.str();
}

std::string surfaceQuickbar(const SurfaceExpeditionPresentation& surface, const SurfaceExpeditionState& expedition, double extractionRisk)
{
    std::ostringstream out;
    out << "<section class=\"surface-quickbar phase-lane phase-row\">";
    out << surfaceQuickMetric(text::labels::site, surface.metrics.empty() ? "" : surface.metrics.front().value, "surface-quick-site");
    out << surfaceQuickMetric("Next", surface.postureTitle, "surface-quick-next");
    out << surfaceQuickMetric(text::labels::supply, std::to_string(expedition.supply));
    out << surfaceQuickMetric(text::labels::sharedFuel, std::to_string(expedition.sharedFuel) + "/" + std::to_string(std::max(1, expedition.sharedFuelCapacity)));
    out << surfaceQuickMetric(text::labels::cargo, std::to_string(expedition.cargo));
    out << surfaceQuickMetric(text::labels::extractionRisk, display::percent(extractionRisk), "", true);
    out << "</section>";
    return out.str();
}

std::string surfaceKpiGrid(const SurfaceExpeditionState& expedition, double extractionRisk)
{
    std::ostringstream out;
    out << "<div class=\"surface-kpi-grid\">";
    out << compactMetric(text::labels::supply, std::to_string(expedition.supply));
    out << compactMetric(text::labels::sharedFuel, std::to_string(expedition.sharedFuel) + "/" + std::to_string(std::max(1, expedition.sharedFuelCapacity)));
    out << compactMetric(text::labels::cargo, std::to_string(expedition.cargo));
    out << compactMetric(text::labels::hazard, display::percent(expedition.hazard));
    out << compactMetric(text::labels::extractionRisk, display::percent(extractionRisk));
    out << compactMetric(text::labels::commonMaterials, std::to_string(expedition.temporaryMaterials.common));
    out << compactMetric(text::labels::rareMaterials, std::to_string(expedition.temporaryMaterials.rare));
    out << compactMetric(text::labels::artifacts, std::to_string(expedition.temporaryArtifacts.size()));
    out << compactMetric("Prospects",
        std::to_string(
            std::max(0, expedition.prospectMaterials.common) +
            std::max(0, expedition.prospectMaterials.rare) +
            std::max(0, expedition.prospectMaterials.exotic) +
            std::max(0, expedition.prospectArtifacts)));
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
    std::string out = "<article class=\"result-group" + classAttr + "\"><h3 class=\"card-title\">" + htmlEscape(group.title) + "</h3>";
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
    const bool jupiterExplored = reached(3, 3);
    const bool saturnExplored = reached(4, 3);
    const bool uranusExplored = reached(5, 3);
    const bool neptuneExplored = reached(6, 3) || arkDiscovered(state);
    const bool straylightFound = arkDiscovered(state);
    const int exploredWorlds = 1
        + (moonExplored ? 1 : 0)
        + (marsExplored ? 1 : 0)
        + (jupiterExplored ? 1 : 0)
        + (saturnExplored ? 1 : 0)
        + (uranusExplored ? 1 : 0)
        + (neptuneExplored ? 1 : 0);

    std::ostringstream out;
    out << "<div class=\"solar-map\">"
        << "<div class=\"solar-map-summary\">"
        << "<div><span>Current frontier</span><strong>" << htmlEscape(currentDestination(state, catalog).name) << "</strong></div>"
        << "<div><span>Explored worlds</span><strong>" << exploredWorlds << " / 7</strong></div>"
        << "<div><span>System</span><strong>Sol</strong></div></div>"
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
        << solarMapNode("Europa", "Eu", "map-moon", jupiterExplored ? MapKnowledge::Explored : MapKnowledge::Charted)
        << solarMapNode("Titan", "T", "map-moon", saturnExplored ? MapKnowledge::Explored : MapKnowledge::Charted)
        << solarMapNode("Triton", "Tr", "map-moon", neptuneExplored ? MapKnowledge::Explored : MapKnowledge::Charted)
        << "</div></div>"
        << "<div class=\"solar-map-lower\">"
        << "<div class=\"solar-map-section solar-map-lower-section\"><div class=\"solar-map-section-head\"><h3>Vessels</h3><span>Tracked contacts</span></div><div class=\"solar-map-row\">"
        << solarMapNode("Pathfinder", "PF", "map-vessel", MapKnowledge::Explored)
        << solarMapNode(straylightFound ? "Straylight" : "Unknown vessel", "ARK", "map-vessel", straylightFound ? MapKnowledge::Explored : MapKnowledge::Unknown)
        << "</div></div>"
        << "<div class=\"solar-map-section solar-map-lower-section\"><div class=\"solar-map-section-head\"><h3>Anomalies</h3><span>Sensor returns</span></div><div class=\"solar-map-row\">"
        << solarMapNode(marsExplored ? "Mars Echo" : "Unresolved signal", "ME", "map-anomaly", marsExplored ? MapKnowledge::Explored : MapKnowledge::Unknown)
        << solarMapNode(straylightFound ? "Neptune Signal" : "Deep-space signal", "NS", "map-anomaly", straylightFound ? MapKnowledge::Explored : MapKnowledge::Unknown)
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
    out << "<div class=\"panel-head\"><div class=\"panel-title\"><span class=\"game-mark\">" << htmlEscape(text::panel::title)
        << "</span><h1>" << htmlEscape(phaseTitle(state.screen)) << "</h1></div>"
        << "<div class=\"panel-head-actions\">"
        << modalButton("Map", ui::modals::map, "ghost")
        << modalButton("Inventory", ui::modals::inventory, "ghost");
    if (state.screen == Screen::Hangar) {
        out << modalButton(text::buttons::legacy, ui::modals::legacy, "ghost");
    }
    out << modalButton(text::buttons::settings, ui::modals::settings, "ghost") << "</div></div>";
    out << "<p class=\"status\">" << htmlEscape(state.statusLine) << "</p>";
    out << solarMapTemplate(context);

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
        << "<button class=\"settings-toggle\" data-help-toggle=\"1\">"
        << htmlEscape("Hide mission help") << "</button></section>";
    settingsBody << "<section class=\"settings-control\" data-camera-shake-settings>"
        << "<div><h3>" << htmlEscape("Camera shake") << "</h3>"
        << "<p>" << htmlEscape("Keep impact and drilling screen shake enabled, or disable it for comfort.") << "</p></div>"
        << "<button class=\"settings-toggle\" data-camera-shake-toggle=\"1\">"
        << htmlEscape("Disable camera shake") << "</button></section>";
    settingsBody << "<section class=\"settings-control\" data-debug-tools-settings>"
        << "<div><h3>" << htmlEscape("Debug screens") << "</h3>"
        << "<p>" << htmlEscape("Show sandbox screen tools for board, mining, flyby, and orbit checks. These do not write save data.") << "</p></div>"
        << "<button class=\"settings-toggle\" data-debug-tools-toggle=\"1\">"
        << htmlEscape("Show debug tools") << "</button></section>";
    settingsBody << "<div class=\"modal-actions action-row\">";
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
        out << "<div class=\"phase-titlebar phase-lane\"><div><h2>" << htmlEscape("Solar System Navigation")
            << "</h2><p>" << htmlEscape(hostileSystemActive(state)
                ? "The Ark is stranded. Pick the next shuttle sortie, then prep the crew and vehicle."
                : "Plot the next route through known space.") << "</p></div></div>";
        out << "<section class=\"resource-bank ark-status phase-lane\"><div><h2>" << htmlEscape("Ark status")
            << "</h2><p>" << htmlEscape(std::string(toString(state.meta.ark.condition))) << "</p></div>"
            << "<div class=\"stat-grid chip-strip\">"
            << resourceChipGrid({
                panelMetric("Campaign", std::string(toString(state.meta.campaignMilestone))),
                panelMetric("System", state.meta.navigation.currentSystemId.empty() ? "Solar system" : state.meta.navigation.currentSystemId),
                panelMetric("Ark hull", display::percent(static_cast<double>(state.meta.ark.hullDamage) / 100.0)),
                panelMetric(text::labels::arkFuel, std::to_string(state.meta.ark.fuelReserve))
            })
            << "</div></section>";

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
                out << "<article class=\"ops-card nav-card " << (selected ? "selected" : "") << "\">";
                out << "<div class=\"card-kicker\"><span>" << htmlEscape("Fuel " + std::to_string(fuelCost) + " / reserve " + std::to_string(state.meta.ark.fuelReserve))
                    << "</span><span>" << htmlEscape("Danger " + display::percent(static_cast<double>(danger) / 100.0)) << "</span></div>";
                out << "<h3 class=\"card-title\">" << htmlEscape(destination.name) << "</h3>";
                out << "<p class=\"card-copy\">" << htmlEscape(destination.tier >= 4
                    ? "Hostile-system sortie. Rich resources, artifact leads, and enemy pressure."
                    : "Known solar-system route.") << "</p>";
                out << "<div class=\"stat-grid chip-strip\">";
                out << resourceChip(panelMetric("Value", std::to_string(value)));
                out << resourceChip(panelMetric("Terrain", display::percent(static_cast<double>(durability) / 100.0)));
                out << resourceChip(panelMetric("Artifacts", destination.tier >= 4 ? "Possible" : "None"));
                out << resourceChip(panelMetric("Enemies", destination.tier >= 4 ? "Detected" : "None"));
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
            "Flight data secured",
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
        const double activeSeconds = std::max(0.01, flyby.missSeconds + flyby.goodSeconds + flyby.perfectSeconds);
        const double goodShare = (flyby.goodSeconds + flyby.perfectSeconds) / activeSeconds;
        const double perfectShare = flyby.perfectSeconds / activeSeconds;
        const FlybyGrade grade = flyby.completed ? flyby.result : FlybyGrade::Active;

        if (flyby.completed) {
            const std::string resultTitle = flyby.collidedWithBody ? "Impact recorded" : flybyGradeLabel(grade);
            const double flybySpeedScale = flyby.slingshotAwarded ? flyby.slingshotSpeedScale : flybySlingshotScale(flyby);
            const double flybyFuelBoost = flyby.slingshotAwarded ? flyby.slingshotFuelBoost : tuning::flyby::slingshotFuelBoost * flybySpeedScale;
            const double flybySpeedBoost = flyby.slingshotAwarded ? flyby.slingshotSpeedBoost : tuning::flyby::slingshotSpeedBoost * flybySpeedScale;
            const std::string resultBody = flyby.collidedWithBody
                ? "The ship clipped the destination body. Hull damage added and no flyby data was recovered."
                : (grade == FlybyGrade::Perfect
                    ? "Perfect corridor exit. Fast completion amplified the payout, and exit speed amplified the next-launch slingshot."
                    : flybyResultBody(grade));
            const std::string tagOne = flyby.collidedWithBody
                ? "Hull +" + std::to_string(flyby.impactHullDamage) + "%"
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
            out << "<div data-panel-mode=\"mission-stamp\" data-flyby-run=\"1\" data-flyby-completed=\"1\" hidden></div>";
            out << missionStamp("Flyby stamp", resultTitle, resultBody, tagOne, tagTwo, tagThree, ui::actions::flybyContinue);
            out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
            out << inventoryTemplate(state, catalog);
            return out.str();
        }

        out << "<div data-flyby-run=\"1\" data-flyby-completed=\"0\" hidden></div>";
        out << "<h2>" << htmlEscape("Manual Flyby") << "</h2>";
        out << "<p class=\"phase-copy\">" << htmlEscape("Steer through " + destinationName + "'s approach corridor. Perfect timing creates a slingshot window for the next launch.") << "</p>";
        const std::string zoneValue = flyby.collidedWithBody
            ? "Impact"
            : flybyZoneLabel(flyby.currentZone);

        out << "<div class=\"metric-grid flight-readout\">"
            << metric("Timer", std::to_string(static_cast<int>(std::ceil(remaining))) + "s")
            << metric("Speed", flybySpeedLabel(flyby))
            << metric("Zone", zoneValue)
            << metric("Good hold", display::percent(goodShare))
            << metric("Perfect hold", display::percent(perfectShare))
            << metric("Reward", "x" + display::fixed(flyby.rewardBonusScale, 1))
            << metric("Slingshot", "x" + display::fixed(flybySlingshotScale(flyby), 1))
            << "</div>";

        out << "<section class=\"cockpit-hud flight-hud\"><div class=\"cockpit-label\"><span>"
            << htmlEscape("Flyby controls") << "</span><strong>"
            << htmlEscape("W/S speed, A/D rotate") << "</strong></div>";
        out << "<p class=\"cockpit-hold-copy\">" << htmlEscape("W/Up faster, S/Down slower. A/Left rotates counter-clockwise; D/Right rotates clockwise. Escape aborts as a Miss.") << "</p>";
        out << "<div class=\"actions action-row primary-actions\">"
            << panelButton(panelActionButton("Abort flyby", ui::actions::flybyAbort, "danger"))
            << "</div>";
        out << "</section>";
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (layoutMode == PanelLayoutMode::ControlPanel && state.screen == Screen::Orbit) {
        const OrbitRunState& orbit = state.run.orbit;
        const Destination* orbitDestination = catalog.findDestination(orbit.destinationId);
        const std::string destinationName = orbitDestination == nullptr ? currentFrontier.name : orbitDestination->name;
        const double remaining = std::max(0.0, orbit.durationSeconds - orbit.elapsedSeconds);
        const double activeSeconds = std::max(0.01, orbit.missSeconds + orbit.goodSeconds + orbit.perfectSeconds);
        const double goodShare = (orbit.goodSeconds + orbit.perfectSeconds) / activeSeconds;
        const double perfectShare = orbit.perfectSeconds / activeSeconds;
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
            const std::string resultTitle = orbitGradeLabel(grade);
            const std::string resultBody = orbitResultBody(grade);
            const std::string tagOne = grade == OrbitGrade::Miss ? "No orbit data" : "Orbit data secured";
            const std::string tagTwo = grade == OrbitGrade::Miss ? "No science bonus" : "+" + std::to_string(blueprintGain) + " science";
            const std::string tagThree = grade == OrbitGrade::Miss ? "Fuel and time spent" : "+" + display::money(rewardCredits) + " credits";
            out << "<div data-panel-mode=\"mission-stamp\" data-orbit-run=\"1\" data-orbit-completed=\"1\" hidden></div>";
            out << missionStamp("Orbit stamp", resultTitle, resultBody, tagOne, tagTwo, tagThree, ui::actions::orbitContinue);
            out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
            out << inventoryTemplate(state, catalog);
            return out.str();
        }

        out << "<div data-orbit-run=\"1\" data-orbit-completed=\"0\" hidden></div>";
        out << "<h2>" << htmlEscape("Orbital Research") << "</h2>";
        out << "<p class=\"phase-copy\">" << htmlEscape("Nudge the ship around " + destinationName + ". Complete one full loop inside the orbital research band before the timer closes.") << "</p>";
        const std::string zoneValue = orbitZoneLabel(orbit.currentZone);

        out << "<div class=\"metric-grid flight-readout\">"
            << metric("Timer", std::to_string(static_cast<int>(std::ceil(remaining))) + "s")
            << metric("Zone", zoneValue)
            << metric("Loop", display::percent(progress))
            << metric("Good hold", display::percent(goodShare))
            << metric("Perfect hold", display::percent(perfectShare))
            << metric("Reward", grade == OrbitGrade::Active ? "Pending" : display::money(rewardCredits))
            << "</div>";

        out << "<section class=\"cockpit-hud flight-hud\"><div class=\"cockpit-label\"><span>"
            << htmlEscape("Orbit controls") << "</span><strong>"
            << htmlEscape("Small WASD corrections") << "</strong></div>";
        const auto orbitControlCard = [&](std::string_view className, std::string_view key, std::string_view title, std::string_view detail) {
            out << "<div class=\"orbit-control-card " << className << "\">"
                << "<div class=\"orbit-keycap\">" << htmlEscape(std::string(key)) << "</div>"
                << "<div><strong>" << htmlEscape(std::string(title)) << "</strong>"
                << "<span>" << htmlEscape(std::string(detail)) << "</span></div>"
                << "</div>";
        };
        out << "<div class=\"orbit-control-panel\">"
            << "<div class=\"orbit-trim-scope\" aria-hidden=\"true\">"
            << "<div class=\"orbit-trim-axis\"></div>"
            << "<div class=\"orbit-trim-ship\"></div>"
            << "<div class=\"orbit-trim-label\">" << htmlEscape("Trim scope") << "</div>"
            << "</div>"
            << "<div class=\"orbit-key-grid\">";
        orbitControlCard("prograde", "W", "Prograde", "Up arrow");
        orbitControlCard("retrograde", "S", "Retrograde", "Down arrow");
        orbitControlCard("widen", "D", "Widen", "Right arrow");
        orbitControlCard("tighten", "A", "Tighten", "Left arrow");
        out << "</div></div>";
        out << "<p class=\"cockpit-hold-copy orbit-control-note\">" << htmlEscape("Complete one full loop before the insertion timer closes. Escape aborts as a Miss.") << "</p>";
        out << "<div class=\"actions action-row primary-actions\">"
            << panelButton(panelActionButton("Abort orbit", ui::actions::orbitAbort, "danger"))
            << "</div>";
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
            out << "<div data-preflight-launch=\"1\" data-preflight-ready=\""
                << (context.preflightReady ? "1" : "0") << "\" hidden></div>";
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
        out << "<div class=\"utility-row utility-actions\">" << modalButton(text::panel::modals::telemetryDetails, ui::modals::telemetry, "ghost") << "</div>";
        out << "<p class=\"status telemetry-status\">" << htmlEscape(launchPanel.telemetryMessage) << "</p>";
        const bool hasAdvancedFlightControls = !launchPanel.systemActions.empty();
        const bool arkKnown = arkDiscovered(state);
        out << tutorialCard(
            "launch-controls",
            arkKnown ? "Press your luck, then return to Ark" : "Press your luck, then return to Earth",
            hasAdvancedFlightControls
                ? (arkKnown
                    ? "Return to Ark banks data but still has return risk. Eject is the expensive emergency out. If gauges climb, cut engines, open the relief valve, or jettison cargo to buy time with tradeoffs."
                    : "Return to Earth banks data but still has return risk. Eject is the expensive emergency out. If gauges climb, cut engines, open the relief valve, or jettison cargo to buy time with tradeoffs.")
                : (arkKnown
                    ? "Return to Ark banks data but still has return risk. Eject is the expensive emergency out. Keep the shuttle useful while the Ark stays operational."
                    : "Return to Earth banks data but still has return risk. Eject is the expensive emergency out. Learn the burn, bank flight data, and unlock the Moon route."));

        out << "<section class=\"cockpit-hud flight-hud\"><div class=\"cockpit-label\"><span>"
            << htmlEscape(text::panel::sections::flightControls) << "</span><strong>"
            << htmlEscape(context.flightArmed
                ? "Choose the next move"
                : (!context.droneTransferEnabled ? "Launch corridor clear" : (context.preflightReady ? "Launch corridor clear" : "Securing mining drone"))) << "</strong></div>";
        if (!context.flightArmed) {
            const std::string_view preflightCopy = !context.droneTransferEnabled
                ? "Bay sealed. Use the cockpit launch control beside the vehicle."
                : (context.preflightReady
                    ? "Drone secured and bay sealed. Use the cockpit launch control beside the vehicle."
                    : "Drone transfer in progress. Launch control unlocks after the bay doors seal.");
            out << "<p class=\"cockpit-hold-copy\">" << htmlEscape(preflightCopy) << "</p>";
        } else {
            out << "<div class=\"actions action-row primary-actions\">";
            for (const FlightActionButtonPresentation& action : launchPanel.primaryActions) {
                out << panelButton(action);
            }
            out << "</div>";

            if (hasAdvancedFlightControls) {
                out << "<div class=\"actions action-row system-actions\">";
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
        out << phaseBoardOpen("phase-board-results", "");
        out << "<section class=\"debrief-hero\"><span>" << htmlEscape("Mission debrief") << "</span><h2>"
            << htmlEscape(presentation.label) << "</h2><p>"
            << htmlEscape("Review the recovery record, banked gains, and vehicle consequences before choosing the next refit window.")
            << "</p></section>";
        if (opensPostArrival) {
            const PhaseBriefingPresentation arrivalBriefing = postArrivalPhaseBriefing(Screen::Results);
            out << "<section class=\"debrief-handoff\"><div class=\"debrief-handoff-copy\"><span>"
                << htmlEscape("Next windows") << "</span><h3>" << htmlEscape("Arrival handoff")
                << "</h3><p>" << htmlEscape("Arrival is open now. Research, surface work, and refit queue up next.")
                << "</p></div><div class=\"debrief-handoff-plan\">" << debriefPhaseTrack(postArrivalPhaseSteps(Screen::Results))
                << "</div><div class=\"debrief-handoff-actions utility-actions\">"
                << modalButton(text::buttons::briefing, ui::modals::phaseBriefing, "ghost") << "</div></section>";
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
        out << "<div class=\"actions action-row\">";
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
        out << "<div class=\"metric-grid approach-metrics\">";
        out << compactMetric(text::labels::currentFrontier, destinationName);
        out << compactMetric(text::panel::details::flyby, std::to_string(destinationHistoryValue(state.meta.destinationFlybys, catalog, state.run.arrivalOps.destinationId)));
        out << compactMetric(text::panel::details::orbit, std::to_string(destinationHistoryValue(state.meta.destinationOrbits, catalog, state.run.arrivalOps.destinationId)));
        out << compactMetric(text::panel::details::landing, std::to_string(destinationHistoryValue(state.meta.destinationLandings, catalog, state.run.arrivalOps.destinationId)));
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
            << "<div class=\"utility-row compact-tools utility-actions\">" << modalButton(text::buttons::details, ui::modals::research, "ghost")
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
        out << "<div class=\"actions action-row\">";
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
        const MiningRunState& mining = state.run.mining;
        const MiningDrillStats miningStats = miningDrillStats(state, catalog);
        const MiningLoadStats loadStats = miningLoadStats(state, catalog);
        const bool arkKnown = arkDiscovered(state);
        const bool debugArena = !mining.progressionCreditEligible;
        const std::string miningRunKicker = debugArena
            ? "Mining Arena Lab"
            : std::string(text::panel::sections::miningRun);
        const std::string miningRunTitle = debugArena
            ? std::string(miningActName(mining.arenaMetadata.act)) + " • Level " + std::to_string(mining.arenaMetadata.difficulty)
            : std::string("Rig health ") + miningPanel.rigHealth;
        const std::string miningGateDetail = mining.gate.active
            ? " | Gate " + std::string(miningGateName(mining.gate.type))
                + " | " + std::string(miningGateStateName(mining.gate.state))
            : std::string();
        const std::string miningRunDetail = debugArena
            ? "Seed " + std::to_string(mining.arenaMetadata.seed)
                + " • Ruleset v" + std::to_string(mining.arenaMetadata.rulesVersion)
                + " • Rig health " + miningPanel.rigHealth
            : state.statusLine;
        const std::string miningRunDetailWithGate = miningRunDetail + miningGateDetail;
        const int rigHealthWidth = static_cast<int>(std::round(std::clamp(miningPanel.rigHealthRatio, 0.0, 1.0) * 100.0));
        const int rigHealthBucket = std::clamp(((rigHealthWidth + 5) / 10) * 10, 0, 100);
        const double oxygenPressure = miningStats.oxygenSeconds > 0.0
            ? std::clamp(1.0 - mining.oxygenSeconds / miningStats.oxygenSeconds, 0.0, 1.0)
            : 1.0;
        const double drillPressure = std::clamp(1.0 - mining.drillIntegrity, 0.0, 1.0);
        const double fuelPressure = state.run.surfaceExpedition.sharedFuelCapacity > 0
            ? std::clamp(
                  1.0 - static_cast<double>(state.run.surfaceExpedition.sharedFuel) /
                      static_cast<double>(state.run.surfaceExpedition.sharedFuelCapacity),
                  0.0,
                  1.0)
            : 1.0;
        const double speedLoadPressure = std::clamp(
            (1.0 - loadStats.speedMultiplier) / (1.0 - tuning::mining::minLoadedSpeedMultiplier),
            0.0,
            1.0);
        const double fuelLoadPressure = std::clamp(
            (loadStats.fuelConsumptionMultiplier - 1.0) / (tuning::mining::maxLoadedFuelMultiplier - 1.0),
            0.0,
            1.0);
        const double loadPressure = std::max(speedLoadPressure, fuelLoadPressure);
        auto metricFor = [&miningPanel](std::string_view label) -> const PanelMetricPresentation* {
            const auto it = std::find_if(miningPanel.metrics.begin(), miningPanel.metrics.end(), [label](const PanelMetricPresentation& item) {
                return item.label == label;
            });
            return it == miningPanel.metrics.end() ? nullptr : &*it;
        };
        auto payloadMetricFor = [&miningPanel](std::string_view label) -> const PanelMetricPresentation* {
            const auto it = std::find_if(miningPanel.payloadMetrics.begin(), miningPanel.payloadMetrics.end(), [label](const PanelMetricPresentation& item) {
                return item.label == label;
            });
            return it == miningPanel.payloadMetrics.end() ? nullptr : &*it;
        };
        auto emitMetricIfPresent = [&out, &metricFor](std::string_view label) {
            if (const PanelMetricPresentation* item = metricFor(label)) {
                out << metric(item->label, item->value);
            }
        };
        auto emitVitalIfPresent = [&out, &metricFor, &mining](
                                      std::string_view label,
                                      std::string_view vitalClass,
                                      double pressure,
                                      bool outlinedNominal = false,
                                      bool broken = false) {
            if (const PanelMetricPresentation* item = metricFor(label)) {
                std::string cssClass = miningVitalAlertClass(vitalClass, pressure, mining.elapsedSeconds, outlinedNominal);
                if (broken) {
                    cssClass += " mining-vital-broken";
                }
                out << metric(
                    item->label,
                    item->value,
                    cssClass);
            }
        };
        auto emitPayloadIfPresent = [&out, &payloadMetricFor](std::string_view label) {
            if (const PanelMetricPresentation* item = payloadMetricFor(label)) {
                out << resourceChip(*item);
            }
        };

        out << "<section class=\"mining-fullscreen\" data-panel-mode=\"mining-fullscreen\">";
        out << "<div class=\"mining-top-rail\"><div class=\"mining-run-title" << (debugArena ? " debug-arena" : "") << "\"><span>"
            << htmlEscape(miningRunKicker) << "</span><strong>" << htmlEscape(miningRunTitle)
            << "</strong>";
        if (debugArena) {
            out << "<strong class=\"mining-arena-metadata\">" << htmlEscape(miningRunDetailWithGate)
                << "</strong>";
        } else {
            out << "<small>" << htmlEscape(miningRunDetailWithGate) << "</small>";
        }
        out << "<section class=\"mining-health-strip\"><div class=\"mining-health-bar\"><i class=\"health-fill-" << rigHealthBucket << "\"></i></div></section></div>";
        out << "<div class=\"metric-grid mining-vitals\">";
        emitVitalIfPresent(text::labels::oxygen, "mining-vital-oxygen", oxygenPressure);
        emitVitalIfPresent(text::fuel::reserveLabel(arkKnown), "mining-vital-fuel", fuelPressure);
        emitVitalIfPresent("Next fuel", "mining-vital-fuel-cadence", mining.fuelCycleProgress, true);
        emitVitalIfPresent(text::labels::drillBit, "mining-vital-drill", drillPressure, false, mining.drillIntegrity <= 0.0);
        if (const PanelMetricPresentation* item = metricFor(text::labels::drillHeat)) {
            out << metric(
                item->label,
                item->value,
                miningDrillHeatAlertClass(mining.drillHeat, mining.elapsedSeconds));
        }
        emitVitalIfPresent(text::labels::load, "mining-vital-load", loadPressure);
        out << "</div><div class=\"mining-utility-cluster\"><span>" << htmlEscape("Systems")
            << "</span>" << modalButton(text::buttons::details, ui::modals::surface, "ghost")
            << modalButton("Inventory", ui::modals::inventory, "ghost")
            << modalButton(text::buttons::settings, ui::modals::settings, "ghost") << "</div></div>";
        if (miningPanel.failurePending) {
            out << "<div class=\"mining-failure-banner\"><strong>" << htmlEscape(miningPanel.failureTitle)
                << "</strong><span>" << htmlEscape(miningPanel.failureBody) << "</span></div>";
        }
        out << "<div class=\"mining-playfield-space\"></div>";
        out << "<div class=\"mining-bottom-rail\"><section class=\"mining-payload-strip\"><span>"
            << htmlEscape("Payload") << "</span><div class=\"stat-grid chip-strip\">";
        emitPayloadIfPresent("Carried cargo");
        emitPayloadIfPresent("Banked cargo");
        emitPayloadIfPresent("Carried mats");
        emitPayloadIfPresent("Banked mats");
        out << "</div>";
        if (metricFor("Artifact") != nullptr) {
            out << "<div class=\"mining-artifact-strip\"><div class=\"stat-grid chip-strip\">";
            emitMetricIfPresent("Artifact");
            emitMetricIfPresent("Tether");
            emitMetricIfPresent("Artifact integrity");
            out << "</div></div>";
        }
        const bool miningAtShip = miningAtReturnZone(mining);
        const PanelButtonPresentation* miningPrimaryAction = nullptr;
        const PanelButtonPresentation* drillRepairAction = nullptr;
        const PanelButtonPresentation* droneRepairAction = nullptr;
        std::vector<const PanelButtonPresentation*> miningDockActions;
        for (const PanelButtonPresentation& action : miningPanel.actions) {
            if (action.actionId == ui::actions::miningStow || action.actionId == ui::actions::miningAbort) {
                miningPrimaryAction = &action;
                continue;
            }
            if (miningAtShip && drillRepairAction == nullptr) {
                drillRepairAction = &action;
                continue;
            }
            if (miningAtShip && droneRepairAction == nullptr) {
                droneRepairAction = &action;
                continue;
            }
            miningDockActions.push_back(&action);
        }
        out << "</section>";
        if (!miningDockActions.empty()) {
            out << "<section class=\"mining-command-dock\"><span>" << htmlEscape(miningPanel.commandTitle)
                << "</span><strong>" << htmlEscape(miningPanel.commandDetail) << "</strong><div class=\"actions action-row system-actions\">";
            for (const PanelButtonPresentation* action : miningDockActions) {
                out << panelButton(*action);
            }
            out << "</div></section>";
        }
        if (miningAtShip && drillRepairAction != nullptr && droneRepairAction != nullptr) {
            const bool drillVisible = miningDrillRepairCost(mining) > 0;
            const bool droneVisible = miningDroneRepairCost(mining) > 0;
            out << "<div class=\"mining-ship-service-marker\" data-mining-ship-service=\"1\""
                << " data-mining-width=\"" << mining.terrain.width << "\""
                << " data-mining-height=\"" << mining.terrain.height << "\""
                << " data-mining-return-x=\"" << mining.returnZoneX << "\""
                << " data-mining-return-y=\"" << mining.returnZoneY << "\""
                << " data-drill-visible=\"" << (drillVisible ? 1 : 0) << "\""
                << " data-drill-enabled=\"" << (drillRepairAction->enabled ? 1 : 0) << "\""
                << " data-drill-label=\"" << htmlEscape(drillRepairAction->label) << "\""
                << " data-drone-visible=\"" << (droneVisible ? 1 : 0) << "\""
                << " data-drone-enabled=\"" << (droneRepairAction->enabled ? 1 : 0) << "\""
                << " data-drone-label=\"" << htmlEscape(droneRepairAction->label) << "\"></div>";
        }
        if (miningPrimaryAction != nullptr) {
            PanelButtonPresentation dockAction = *miningPrimaryAction;
            dockAction.cssClass += dockAction.cssClass.empty() ? "mining-primary-command" : " mining-primary-command";
            out << "<section class=\"mining-recall-dock\"><div class=\"actions action-row system-actions\">" << panelButton(dockAction) << "</div></section>";
        }
        out << "</div></section>";
        if (miningPanel.failurePending) {
            std::ostringstream failureBody;
            failureBody << "<div class=\"phase-advisory danger mining-failure-callout\"><strong>" << htmlEscape(miningPanel.failureTitle)
                << "</strong><span>" << htmlEscape(miningPanel.failureBody) << "</span></div>";
            failureBody << "<div class=\"modal-actions actions action-row\">"
                << panelButton(panelActionButton("Return to Surface Ops", ui::actions::miningFailureAck, "danger"))
                << "</div>";
            out << autoModalTemplate(ui::modals::miningFailure, miningPanel.failureTitle, failureBody.str(), false);
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
            << "<div class=\"utility-row compact-tools utility-actions\">" << modalButton(text::buttons::details, ui::modals::surface, "ghost")
            << panelButton(dronePanel.backAction) << "</div></div>";
        const std::vector<PanelMetricPresentation> droneBayChips {
            dronePanel.metrics.size() > 0 ? dronePanel.metrics[0] : panelMetric("Slots", "0/0"),
            panelMetric("Types", dronePanel.metrics.size() > 1 ? dronePanel.metrics[1].value : "0"),
            panelMetric("Next slot", dronePanel.nextSlotCost)
        };
        const std::vector<PanelMetricPresentation> droneMaterialChips {
            panelMetric("Common", dronePanel.metrics.size() > 2 ? dronePanel.metrics[2].value : "0"),
            panelMetric("Rare", dronePanel.metrics.size() > 3 ? dronePanel.metrics[3].value : "0"),
            panelMetric("Exotic", dronePanel.metrics.size() > 4 ? dronePanel.metrics[4].value : "0")
        };
        out << "<div class=\"drone-top-row\">";
        out << "<section class=\"resource-bank drone-bay-strip\"><div class=\"drone-bay-copy\"><h2>" << htmlEscape("Drone Bay")
            << "</h2><p>" << htmlEscape("Slots persist. Equipped drones support the next mining run.") << "</p></div>"
            << "<div class=\"stat-grid chip-strip drone-bay-stats\">" << resourceChipGrid(droneBayChips) << "</div>"
            << "<div class=\"stat-grid chip-strip drone-bay-materials\">" << resourceChipGrid(droneMaterialChips) << "</div>"
            << panelButton(dronePanel.upgradeSlotAction) << "</section>";
        out << "</div>";
        out << "<section class=\"board-primary drone-roster\"><div class=\"section-heading\"><h2>" << htmlEscape("Drone controls")
            << "</h2><p>" << htmlEscape("Owned drone types: add copies and upgrade tuning.") << "</p></div><div class=\"drone-control-grid\">";
        for (const MiniDroneCardPresentation& drone : dronePanel.drones) {
            out << miniDroneControlCard(drone);
        }
        out << "</div></section>";
        out << "<section class=\"board-primary drone-loadout-bench\"><div class=\"section-heading\"><h2>" << htmlEscape("Drone Loadout")
            << "</h2><p>" << htmlEscape("Active bay slots that launch with the rig. Unequip one copy from here.") << "</p></div><div class=\"drone-loadout-grid\">";
        for (const DroneLoadoutSlotPresentation& slot : dronePanel.loadoutSlots) {
            out << droneLoadoutSlotCard(slot);
        }
        out << "</div></section>";
        std::vector<PanelMetricPresentation> compactForecastChips;
        for (const PanelMetricPresentation& chip : dronePanel.forecastChips) {
            if (chip.label == "Volley" || chip.label == "Cadence" || chip.label == "Crit chance" || chip.label == "Sentry output") {
                compactForecastChips.push_back(chip);
            }
        }
        out << "<section class=\"resource-bank drone-combat-forecast\"><div class=\"drone-forecast-copy\"><h2>" << htmlEscape("Combat forecast")
            << "</h2><p>" << htmlEscape(dronePanel.arenaTitle.empty() ? "Passive swarm cover during hostile mining." : dronePanel.arenaTitle + " — " + dronePanel.arenaDetail) << "</p></div>"
            << "<div class=\"stat-grid chip-strip drone-forecast-stats\">" << resourceChipGrid(compactForecastChips) << "</div></section>";
        out << phaseBoardClose();
        out << modalTemplate(ui::modals::surface, "Drone Ops Details", detailStack(dronePanel.details));
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (state.screen == Screen::SurfaceScan) {
        const SurfaceExpeditionPresentation surfacePanel = surfaceExpeditionPresentation(state, catalog);
        const SurfaceScanRunState& scan = state.run.surfaceScan;
        out << phaseBoardOpen("phase-board-surface phase-board-surface-minigame phase-board-scan", state.statusLine);
        const std::vector<PanelMetricPresentation> scanMetrics {
            panelMetric("Pulses", std::to_string(scan.pulses) + "/" + std::to_string(std::max(1, scan.maxPulses))),
            panelMetric("Next layer", "+" + std::to_string(scan.pulses)),
            panelMetric("Mapped", std::to_string(scan.depthProspects.size())),
            panelMetric("Signal", display::percent(scan.signal)),
            panelMetric("Interference", display::percent(scan.interference)),
            panelMetric("Bust risk", display::percent(scan.bustRisk))
        };
        std::vector<PanelButtonPresentation> actions;
        if (scan.busted) {
            actions.push_back(panelActionButton("Return to Surface Ops", ui::actions::surfaceScanBank, "danger"));
        } else {
            actions.push_back(scan.completed
                ? disabledPanelButton("Pulse complete")
                : panelActionButton("Pulse scanner", ui::actions::surfaceScanPulse, "warn"));
            actions.push_back(panelActionButton(scan.pulses > 0 ? "Bank scan" : "Return to Surface Ops", ui::actions::surfaceScanBank, "ok"));
            actions.push_back(panelActionButton("Abort scan", ui::actions::surfaceScanAbort, "danger"));
        }
        out << surfaceMiniGamePanel(
            "scan-minigame",
            "Planet Scan",
            "Pulse 1 maps the current layer. Later pulses preview layers available through Push Deeper.",
            scanMetrics,
            materialRewardChips(scan.temporaryMaterials, static_cast<int>(scan.temporaryArtifacts.size()), scan.cargo),
            scan.busted ? "Signal Burnout" : (scan.completed ? "Jackpot Window" : "Scanner Window"),
            scan.message.empty() ? "Pulse to forecast a layer, then bank the scan before interference burns the array." : scan.message,
            actions);
        out << phaseBoardClose();
        out << modalTemplate(ui::modals::surface, text::panel::modals::surfaceDetails, detailStack(surfacePanel.details));
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (state.screen == Screen::SurfacePush) {
        const SurfaceExpeditionPresentation surfacePanel = surfaceExpeditionPresentation(state, catalog);
        const SurfacePushRunState& push = state.run.surfacePush;
        out << phaseBoardOpen("phase-board-surface phase-board-surface-minigame phase-board-push", state.statusLine);
        const int nextDepthOffset = push.steps + 1;
        const bool nextLayerScanned = std::any_of(
            state.run.surfaceExpedition.depthProspects.begin(),
            state.run.surfaceExpedition.depthProspects.end(),
            [&](const SurfaceDepthProspect& prospect) {
                return prospect.absoluteDepth == state.run.surfaceExpedition.depth + nextDepthOffset;
            });
        const std::vector<PanelMetricPresentation> pushMetrics {
            panelMetric("Steps", std::to_string(push.steps) + "/" + std::to_string(std::max(1, push.maxSteps))),
            panelMetric("Depth gain", "+" + std::to_string(push.depthGain)),
            panelMetric("Next layer", "+" + std::to_string(nextDepthOffset)),
            panelMetric("Survey read", nextLayerScanned ? "Known" : "Unknown"),
            panelMetric("Pressure", display::percent(push.pressure)),
            panelMetric("Collapse risk", display::percent(push.collapseRisk))
        };
        std::vector<PanelButtonPresentation> actions;
        if (push.busted) {
            actions.push_back(panelActionButton("Return to Surface Ops", ui::actions::surfacePushBank, "danger"));
        } else {
            actions.push_back(push.completed
                ? disabledPanelButton("Route limit reached")
                : panelActionButton(text::buttons::pushDeeper, ui::actions::surfacePushStep, "danger"));
            actions.push_back(panelActionButton(push.steps > 0 ? "Bank route" : "Return to Surface Ops", ui::actions::surfacePushBank, "ok"));
            actions.push_back(panelActionButton("Abort descent", ui::actions::surfacePushAbort, "danger"));
        }
        out << surfaceMiniGamePanel(
            "push-minigame",
            text::buttons::pushDeeper,
            "Push Deeper turns layer forecasts into marked finds for mining.",
            pushMetrics,
            materialRewardChips(push.temporaryMaterials, static_cast<int>(push.temporaryArtifacts.size()), push.cargo),
            push.busted ? "Route Collapse" : (push.completed ? "Deep Route Locked" : "Descent Window"),
            push.message.empty() ? "Push Deeper for the next layer, then bank before the terrain turns." : push.message,
            actions);
        out << phaseBoardClose();
        out << modalTemplate(ui::modals::surface, text::panel::modals::surfaceDetails, detailStack(surfacePanel.details));
        out << modalTemplate(ui::modals::settings, text::panel::modals::settings, settingsBody.str());
        out << inventoryTemplate(state, catalog);
        return out.str();
    }

    if (state.screen == Screen::SurfaceExpedition) {
        const SurfaceExpeditionPresentation surfacePanel = surfaceExpeditionPresentation(state, catalog);
        const SurfaceExpeditionState& expedition = state.run.surfaceExpedition;
        const double extractionRisk = surfaceExtractionRisk(state);
        out << phaseBoardOpen("phase-board-surface surface-ops-screen", state.statusLine);
        out << "<div class=\"phase-titlebar phase-title-row\"><div><h2>" << htmlEscape(text::panel::sections::surfaceExpedition)
            << "</h2></div>";
        out << "<div class=\"utility-row compact-tools utility-actions\">" << modalButton(text::buttons::briefing, ui::modals::phaseBriefing, "ghost")
            << modalButton(text::buttons::details, ui::modals::surface, "ghost");
        if (!surfacePanel.logEntries.empty()) {
            out << modalButton(text::panel::sections::missionLog, ui::modals::missionLog, "ghost");
        }
        out << "</div></div>";
        out << surfaceQuickbar(surfacePanel, expedition, extractionRisk);
        if (surfacePanel.droneOpsAction.enabled) {
            out << "<section class=\"resource-bank drone-ops-callout phase-lane phase-row\"><div><h2>" << htmlEscape("Drone Ops")
                << "</h2><p>" << htmlEscape("Equip persistent helper drones before you launch the mining run.") << "</p></div>"
                << panelButton(surfacePanel.droneOpsAction) << "</section>";
        }
        out << "<section class=\"resource-bank surface-arena-forecast phase-lane phase-row\"><div><h2>" << htmlEscape(surfacePanel.arenaTitle)
            << "</h2><p>" << htmlEscape(surfacePanel.arenaDetail) << "</p></div></section>";
        const auto mineAction = std::find_if(surfacePanel.actions.begin(), surfacePanel.actions.end(), [](const SurfaceActionPreviewPresentation& action) {
            return isSurfaceMiningAction(action);
        });
        std::vector<const SurfaceActionPreviewPresentation*> orderedActions;
        orderedActions.reserve(surfacePanel.actions.size());
        if (mineAction != surfacePanel.actions.end()) {
            orderedActions.push_back(&*mineAction);
        }
        for (const SurfaceActionPreviewPresentation& action : surfacePanel.actions) {
            if (isSurfaceMiningAction(action)) {
                continue;
            }
            orderedActions.push_back(&action);
        }
        out << "<section class=\"board-primary surface-actions phase-lane\">";
        out << "<div class=\"ops-grid phase-action-grid\">";
        for (std::size_t i = 0; i < orderedActions.size(); ++i) {
            out << phaseCardSlot(surfaceActionCard(*orderedActions[i]), "surface-action-slot", i + 1 == orderedActions.size());
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
        out << "<section class=\"draft-hero\"><div><span>" << htmlEscape("Drone Upgrade")
            << "</span><h2>" << htmlEscape("Pick your Field Research") << "</h2><p>"
            << htmlEscape("Choose one drone upgrade. It stays active until your shuttle or mining drone is destroyed.") << "</p></div>";
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
        out << "<div class=\"stat-grid chip-strip draft-context\">" << fieldContextChipGrid(fieldContext) << "</div></section>";
        out << "<section class=\"draft-board\"><div class=\"phase-titlebar\"><div><h2>"
            << htmlEscape("Choose one drone upgrade") << "</h2><p>"
            << htmlEscape("Scanner, drill, and drone tech improve future digs until the rig is lost. Take one, reroll the field research, or walk away.") << "</p></div></div>";
        out << "<div class=\"pilot-card-grid draft-card-grid\">";
        for (const SurfaceUpgradeCardPresentation& upgrade : surfacePanel.upgradeOffers) {
            out << surfaceUpgradeCard(upgrade);
        }
        out << "</div><div class=\"actions action-row draft-actions\">";
        const double rerollCost = offerRerollCost(state);
        out << panelButton(state.run.credits >= rerollCost
            ? panelActionButton(std::string("Reroll draft (") + display::money(rerollCost) + ")", ui::actions::rerollOffers, "warn")
            : disabledPanelButton(display::needCredits(rerollCost)));
        out << panelButton(panelActionButton("Skip field research", ui::actions::next));
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
            << "</span><h2>" << htmlEscape("Pick your next build card") << "</h2><p>"
            << htmlEscape("The bay crew can only sleeve one permanent refit before launch. Choose the card that defines this vehicle's next role.") << "</p></div>";
        out << "<div class=\"stat-grid chip-strip draft-context\">" << resourceChipGrid(refitWindow.resourceChips) << "</div></section>";
        if (!refitWindow.recoveryDetail.empty()) {
            out << "<p class=\"draft-recovery-note\">" << htmlEscape(refitWindow.recoveryDetail) << "</p>";
        }
        out << "<section class=\"draft-board\"><div class=\"phase-titlebar\"><div><h2>"
            << htmlEscape("Choose one permanent refit") << "</h2><p>"
            << htmlEscape("Card effects carry forward. Install one, reroll the board, or bank the credits for the next hangar window.") << "</p></div></div><div class=\"pilot-card-grid draft-card-grid\">";
        for (const RefitOfferPresentation& offer : refitWindow.offers) {
            out << refitOfferCard(offer);
        }
        out << "</div><div class=\"actions action-row draft-actions\">";
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

    out << "<div class=\"actions action-row hangar-actions\">";
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
