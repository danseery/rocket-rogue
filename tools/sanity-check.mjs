import { existsSync, readFileSync } from "node:fs";

const required = [
  "CMakeLists.txt",
  "CMakePresets.json",
  "cmake/PackageNative.cmake",
  "package.json",
  "package-lock.json",
  ".node-version",
  "requirements-dev.txt",
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
  "tools/import-chroma-sprite.py",
  "staticwebapp.config.json",
  ".github/workflows/azure-static-web-app.yml",
  ".github/workflows/native-release.yml",
  "docs/AZURE_STATIC_WEB_APPS.md",
  "docs/DESKTOP_BUILDS.md",
  "src/core/GameTypes.h",
  "src/core/GameText.h",
  "src/core/Telemetry.h",
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
for (const token of ["requestFullscreen", "exitFullscreen", "fullscreenchange"]) {
  if (!webShell.includes(token)) {
    console.error(`web/shell.html missing browser fullscreen support: ${token}`);
    failed = true;
  }
}
for (const token of [
  "rocket_rogue_frame_limit_mode",
  "data-frame-limit-select",
  "setFrameLimitMode",
  "syncFrameLimitControls"
]) {
  if (!webShell.includes(token)) {
    console.error(`web frame-limit preference missing token: ${token}`);
    failed = true;
  }
}

const gameRmlUi = existsSync("src/game/GameRmlUi.cpp") ? readFileSync("src/game/GameRmlUi.cpp", "utf8") : "";
for (const token of [
  "data-frame-limit-select",
  "rr_rml_set_frame_limit_preference",
  "selectCurrentFrameLimit"
]) {
  if (!gameRmlUi.includes(token)) {
    console.error(`native RmlUi frame-limit preference missing token: ${token}`);
    failed = true;
  }
}
for (const token of [
  "RmlPanelMode::Title",
  ".title-screen",
  ".orebit-letter",
  "@keyframes orebit-letter-float",
  "infinite alternate orebit-letter-float"
]) {
  if (!gameRmlUi.includes(token)) {
    console.error(`native RmlUi title presentation missing token: ${token}`);
    failed = true;
  }
}

for (const token of [
  ".title-screen-panel",
  ".title-screen-panel-mode",
  ".orebit-letter",
  "@keyframes orebit-letter-float",
  "animation: orebit-letter-float",
  "rr_new_game",
  "rr_continue_game",
  "new_game",
  "continue_game",
  "function isTitleScreenActive()",
  "if (isTitleScreenActive())"
]) {
  if (!webShell.includes(token)) {
    console.error(`web title presentation or action bridge missing token: ${token}`);
    failed = true;
  }
}

const webMain = existsSync("src/platform/web/WebMain.cpp") ? readFileSync("src/platform/web/WebMain.cpp", "utf8") : "";
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
