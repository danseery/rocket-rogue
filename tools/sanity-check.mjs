import { existsSync, readFileSync } from "node:fs";

const required = [
  "CMakeLists.txt",
  "CMakePresets.json",
  "cmake/PackageNative.cmake",
  "package.json",
  "package-lock.json",
  ".node-version",
  "requirements-dev.txt",
  "config/steam-release-policy.json",
  "config/steam-readiness.json",
  "README.md",
  "docs/DESIGN.md",
  "web/shell.html",
  "scripts/install-windows.ps1",
  "scripts/install-ubuntu.sh",
  "scripts/env-windows.ps1",
  "scripts/env-ubuntu.sh",
  "tools/serve.mjs",
  "tools/verify-toolchain.mjs",
  "tools/run-cmake-preset.mjs",
  "tools/run-web-tests.mjs",
  "tools/prepare-azure-static-web-app.mjs",
  "tools/check-platform-boundaries.mjs",
  "tools/verify-steam-readiness.mjs",
  "tools/verify-steam-readiness.test.mjs",
  "tools/import-chroma-sprite.py",
  "staticwebapp.config.json",
  ".github/workflows/azure-static-web-app.yml",
  ".github/workflows/native-release.yml",
  "docs/AZURE_STATIC_WEB_APPS.md",
  "docs/DESKTOP_BUILDS.md",
  "docs/STEAM_RELEASE_READINESS.md",
  "src/core/GameTypes.h",
  "src/core/GameText.h",
  "src/core/Tuning.h",
  "src/core/Content.cpp",
  "src/core/GameState.cpp",
  "src/core/LaunchSimulation.cpp",
  "src/core/SaveData.cpp",
  "src/game/GameRunner.cpp",
  "src/game/GamePanel.cpp",
  "src/game/GameRmlUi.cpp",
  "src/game/RocketGameApp.cpp",
  "src/render/OpenGlRenderer.cpp",
  "src/platform/AppServices.h",
  "src/platform/web/WebSaveStore.cpp",
  "src/platform/web/WebMain.cpp",
  "src/platform/sdl/SdlPlatform.cpp",
  "tests/core_tests.cpp",
  "tests/app_services_tests.cpp",
  "tests/native_platform_tests.cpp"
];

const retired = ["desktop"];

let failed = false;

for (const file of required) {
  if (!existsSync(file)) {
    console.error(`missing: ${file}`);
    failed = true;
  }
}

for (const path of retired) {
  if (existsSync(path)) {
    console.error(`retired path returned: ${path}`);
    failed = true;
  }
}

const cmake = existsSync("CMakeLists.txt") ? readFileSync("CMakeLists.txt", "utf8") : "";
for (const token of ["rocket_core", "rocket_app", "rocket_core_tests", "rocket_rogue", "RocketRogue", "SDL3", "EMSCRIPTEN"]) {
  if (!cmake.includes(token)) {
    console.error(`CMakeLists.txt missing token: ${token}`);
    failed = true;
  }
}

const packageJson = existsSync("package.json") ? readFileSync("package.json", "utf8") : "";
for (const token of ['"electron"', "electron-builder", "package:desktop", "build:desktop", "test:desktop"]) {
  if (packageJson.includes(token)) {
    console.error(`package.json still references retired desktop tooling: ${token}`);
    failed = true;
  }
}

const webShell = existsSync("web/shell.html") ? readFileSync("web/shell.html", "utf8") : "";
if (webShell.includes("RocketDesktop")) {
  console.error("web/shell.html still references the retired Electron fullscreen bridge");
  failed = true;
}
for (const token of [
  '<div id="ui-startup-status" role="status">Starting RmlUi...</div>',
  'body[data-ui-renderer="rmlui"] #ui-startup-status'
]) {
  if (!webShell.includes(token)) {
    console.error(`web/shell.html missing fail-loud RmlUi startup status: ${token}`);
    failed = true;
  }
}
for (const token of [
  'id="panel"',
  'id="modal-root"',
  'id="controller-prompt-bar"',
  'id="toast-root"',
  "RocketBridge.setPanel",
  "setRealtimeHudState",
  "template[data-modal]"
]) {
  if (webShell.includes(token)) {
    console.error(`web shell still contains an obsolete UI renderer token: ${token}`);
    failed = true;
  }
}
for (const token of ["function setUiHostContext(context)", "setUiHostContext,"]) {
  if (!webShell.includes(token)) {
    console.error(`web shell missing typed UI host context token: ${token}`);
    failed = true;
  }
}

const gameRmlUi = existsSync("src/game/GameRmlUi.cpp") ? readFileSync("src/game/GameRmlUi.cpp", "utf8") : "";
const nativeRcss = existsSync("assets/ui/styles/all.rcss")
  ? readFileSync("assets/ui/styles/all.rcss", "utf8")
  : "";
const nativeCssRuleBody = (selector) => {
  const selectorStart = nativeRcss.indexOf(selector);
  if (selectorStart < 0) return "";
  const bodyStart = nativeRcss.indexOf("{", selectorStart);
  const bodyEnd = bodyStart < 0 ? -1 : nativeRcss.indexOf("}", bodyStart);
  return bodyStart < 0 || bodyEnd < 0 ? "" : nativeRcss.slice(bodyStart + 1, bodyEnd);
};
const requireNativeCssRule = (selector, tokens, contract) => {
  const body = nativeCssRuleBody(selector);
  for (const token of tokens) {
    if (!body.includes(token)) {
      console.error(`native RmlUi ${contract} missing ${selector} token: ${token}`);
      failed = true;
    }
  }
};

for (const [pattern, contract] of [
  [/const int modalActivityHeight\s*=\s*std::max\(1,\s*std::min\(360,\s*viewportHeight - modalGutter \* 2\)\);/, "viewport-clamped activity briefing height"],
  [/const int modalOutcomeHeight\s*=\s*std::max\(1,\s*std::min\(\d+,\s*viewportHeight - modalGutter \* 2\)\);/, "viewport-clamped launch outcome height"],
  [/const int modalOutcomeTop\s*=\s*std::max\(modalGutter,\s*\(viewportHeight - modalOutcomeHeight\) \/ 2\);/, "centered launch outcome position"],
  [/const bool modalOutcomeNeedsScroll\s*=\s*modalOutcomeHeight < \d+;/, "short-height launch outcome scrolling"],
  [/SetProperty\("--rr-legacy-layout-115",\s*std::to_string\(modalOutcomeTop\) \+ "px"\)/, "launch outcome top custom property"],
  [/SetProperty\("--rr-legacy-layout-116",\s*std::to_string\(modalOutcomeHeight\) \+ "px"\)/, "launch outcome height custom property"],
  [/SetProperty\("--rr-legacy-layout-117",\s*std::string\(modalOutcomeNeedsScroll \? "auto" : "hidden"\)\)/, "launch outcome scroll custom property"]
]) {
  if (!pattern.test(gameRmlUi)) {
    console.error(`native RmlUi modal layout contract missing: ${contract}`);
    failed = true;
  }
}
requireNativeCssRule("#rr-modal-scrim", ["z-index: 100;"], "modal stacking");
requireNativeCssRule(
  "#rr-scene-launch-control,",
  ["pointer-events: auto;"],
  "actionable scene launch overlay");
requireNativeCssRule("#rr-modal {", ["box-sizing: border-box;", "z-index: 101;", "display: flex;", "overflow: hidden;"], "modal containment");
requireNativeCssRule(
  "#rr-modal.modal-launch_outcome {",
  ["top: var(--rr-legacy-layout-115, 0px);", "height: var(--rr-legacy-layout-116, 0px);"],
  "launch outcome geometry");
requireNativeCssRule(
  "#rr-modal.modal-launch_outcome.modal-tone-positive {",
  ["background-color: #071b15;", "border-width: 2px;", "border-color: #46c587;"],
  "positive launch outcome hue and outline");
requireNativeCssRule(
  "#rr-modal.modal-launch_outcome.modal-tone-negative {",
  ["background-color: #200b0e;", "border-width: 2px;", "border-color: #e36868;"],
  "negative launch outcome hue and outline");
requireNativeCssRule(
  "#rr-modal.modal-launch_outcome .modal-scroll-body {",
  ["display: flex;", "flex-direction: column;", "overflow-y: var(--rr-legacy-layout-117, hidden);"],
  "launch outcome short-height body");
requireNativeCssRule(
  "#rr-modal.modal-launch_introduction .modal-scroll-body,",
  ["overflow-y: auto;", "padding-right: 8px;"],
  "activity briefing short-height fallback");
requireNativeCssRule(
  ".launch-outcome-summary {",
  ["flex: 1 1 auto;", "min-height: 0px;", "height: 100%;"],
  "launch outcome shrinkable summary");
requireNativeCssRule(
  ".launch-outcome-actions {",
  ["flex: 0 0 40px;", "height: 40px;", "margin-top: auto;", "flex-wrap: nowrap;"],
  "launch outcome persistent action lane");
requireNativeCssRule(
  ".launch-outcome-actions button {",
  ["height: 40px;", "min-height: 0px;"],
  "launch outcome action target");
requireNativeCssRule(
  "#rr-modal.modal-launch_outcome .ui-outcome-rows > div {",
  ["min-height: 40px;", "height: 40px;"],
  "launch outcome compact consequence rows");
for (const token of [
  "#rr-panel.workspace-panel > .rr-shell .rr-shell-content > .panel-head",
  "#rr-document.rr-side-rail #rr-panel.control-panel > .rr-shell .rr-shell-content > .panel-head",
  "#rr-panel.results-panel-mode > .rr-shell .rr-shell-content > .panel-head"
]) {
  if (!nativeRcss.includes(token)) {
    console.error(`native RmlUi template compatibility selector missing: ${token}`);
    failed = true;
  }
}

for (const token of [
  "data-frame-limit-select",
  "rr_rml_set_frame_limit_preference",
  "selectCurrentFrameLimit",
  "const ModalPresentation* activeModal",
  "overlayHost->SetInnerRML(activeModal ? std::string {} : nativeSceneOverlayMarkup(presentation_));"
]) {
  if (!gameRmlUi.includes(token)) {
    console.error(`native RmlUi frame-limit preference missing token: ${token}`);
    failed = true;
  }
}
const nativePresentationSource = `${gameRmlUi}\n${nativeRcss}`;
for (const token of [
  "RmlPanelMode::Title",
  ".title-screen",
  ".orebit-letter",
  "@keyframes orebit-letter-float",
  "infinite alternate orebit-letter-float"
]) {
  if (!nativePresentationSource.includes(token)) {
    console.error(`native RmlUi title presentation missing token: ${token}`);
    failed = true;
  }
}

const webMain = existsSync("src/platform/web/WebMain.cpp") ? readFileSync("src/platform/web/WebMain.cpp", "utf8") : "";
const webPlatform = existsSync("src/platform/web/WebPlatform.cpp") ? readFileSync("src/platform/web/WebPlatform.cpp", "utf8") : "";
for (const token of ["requestFullscreen", "exitFullscreen", "fullscreenchange", "data-frame-limit-select"]) {
  if (!webPlatform.includes(token)) {
    console.error(`WebPlatform missing browser display preference support: ${token}`);
    failed = true;
  }
}
const singleRendererSources = `${webMain}\n${webPlatform}\n${gameRmlUi}\n${webShell}`;
for (const token of [
  "WebDomFallbackUi",
  "rr_force_dom_fallback_requested",
  "force_dom_fallback",
  "ui_renderer",
  "forceDomFallback",
  "rr_rml_dom_",
  "rr_web_ui_action",
  "rr_web_focused_id",
  "rr_web_request_focus"
]) {
  if (singleRendererSources.includes(token)) {
    console.error(`single-renderer web contract still exposes fallback token: ${token}`);
    failed = true;
  }
}
for (const token of [
  "std::make_unique<rocket::GameRmlUi>",
  "std::make_unique<rocket::WebGlRmlRenderHost>"
]) {
  if (!webMain.includes(token)) {
    console.error(`WebMain missing required RmlUi renderer construction: ${token}`);
    failed = true;
  }
}
for (const token of ["void rr_new_game", "g_app->newGame()", "void rr_continue_game", "g_app->continueGame()"]) {
  if (!webMain.includes(token)) {
    console.error(`WebMain title action export missing token: ${token}`);
    failed = true;
  }
}
for (const token of ["'_rr_new_game'", "'_rr_continue_game'"]) {
  if (!cmake.includes(token)) {
    console.error(`CMake web exports missing title action: ${token}`);
    failed = true;
  }
}

const content = existsSync("src/core/Content.cpp") ? readFileSync("src/core/Content.cpp", "utf8") : "";
for (const token of ["Earth Orbit", "Rift Belt", "Abort Tower", "Predictive Guidance"]) {
  if (!content.includes(token)) {
    console.error(`content missing token: ${token}`);
    failed = true;
  }
}

if (failed) {
  process.exit(1);
}

console.log("Rocket Rogue scaffold sanity check passed.");
