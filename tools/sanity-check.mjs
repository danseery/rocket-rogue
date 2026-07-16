import { existsSync, readFileSync } from "node:fs";

const required = [
  "CMakeLists.txt",
  "CMakePresets.json",
  "package.json",
  "package-lock.json",
  "requirements-dev.txt",
  "README.md",
  "docs/DESIGN.md",
  "web/shell.html",
  "scripts/install-windows.ps1",
  "scripts/install-ubuntu.sh",
  "scripts/env-windows.ps1",
  "scripts/env-ubuntu.sh",
  "tools/serve.mjs",
  "tools/prepare-azure-static-web-app.mjs",
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
  "src/game/RocketGameApp.cpp",
  "src/render/OpenGlRenderer.cpp",
  "src/platform/AppServices.h",
  "src/platform/web/WebSaveStore.cpp",
  "src/platform/sdl/SdlPlatform.cpp",
  "tests/core_tests.cpp",
  "tests/app_services_tests.cpp",
  "tests/native_platform_tests.cpp"
];

let failed = false;

for (const file of required) {
  if (!existsSync(file)) {
    console.error(`missing: ${file}`);
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
