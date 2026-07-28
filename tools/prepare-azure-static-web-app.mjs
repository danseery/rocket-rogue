import { existsSync, mkdirSync, readFileSync, readdirSync, rmSync, statSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { validateResourceGraph } from "./validate-rmlui-resources.mjs";

const buildDir = "build/web-release";
const outputDir = "dist/azure-static-web-app";

const requiredFiles = [
  "rocket_rogue.html",
  "rocket_rogue.js",
  "rocket_rogue.data",
  "rocket_rogue.wasm"
];

const requiredSceneAtlasFiles = [
  "scene-atlas-0.png",
  "scene-atlas-1.png",
  "scene-atlas.json"
];

const requiredFontFiles = [
  "SourceCodePro-Regular.ttf",
  "SourceCodePro-Semibold.ttf",
  "SourceCodePro-It.ttf",
  "LICENSE.md"
];

const requiredUiFiles = [
  "panel.rml",
  "styles/all.rcss",
  "styles/tokens.rcss",
  "styles/primitives.rcss",
  "styles/shells.rcss",
  "styles/families.rcss",
  "styles/screen-exceptions.rcss",
  "styles/legacy.rcss",
  "templates/rr-document-shell.rml",
  "templates/rr-workspace-shell.rml",
  "templates/rr-control-shell.rml",
  "templates/rr-surface-minigame-shell.rml",
  "templates/rr-mining-shell.rml",
  "templates/rr-takeover-shell.rml",
  "templates/rr-results-shell.rml",
  "templates/rr-modal-shell.rml"
];

function copyDirectory(source, target) {
  mkdirSync(target, { recursive: true });
  for (const entry of readdirSync(source)) {
    const sourcePath = join(source, entry);
    const targetPath = join(target, entry);
    if (statSync(sourcePath).isDirectory()) {
      copyDirectory(sourcePath, targetPath);
    } else {
      copyFileBytes(sourcePath, targetPath);
    }
  }
}

function copyFileBytes(source, target) {
  try {
    writeFileSync(target, readFileSync(source));
  } catch (error) {
    if (error && error.code === "EPERM") {
      console.error(`Could not read or write ${source}.`);
      console.error("On Windows OneDrive workspaces, generated build files can become cloud reparse points.");
      console.error("Make the build folder available offline, or rebuild/package from WSL/Linux or GitHub Actions.");
      process.exit(1);
    }
    throw error;
  }
}

function requireValidUiGraph(root, label) {
  const errors = validateResourceGraph(root);
  if (errors.length === 0) {
    return;
  }
  console.error(`${label} contains an invalid RmlUi resource graph:`);
  for (const error of errors) {
    console.error(`- ${error}`);
  }
  process.exit(1);
}

for (const file of requiredFiles) {
  const path = join(buildDir, file);
  if (!existsSync(path)) {
    console.error(`Missing web build artifact: ${path}`);
    console.error("Run cmake --preset web-release and cmake --build --preset web-release first.");
    process.exit(1);
  }
}

if (!existsSync(join(buildDir, "assets"))) {
  console.error(`Missing copied assets directory: ${join(buildDir, "assets")}`);
  process.exit(1);
}
for (const file of requiredSceneAtlasFiles) {
  const path = join(buildDir, "assets", "scene-atlas", file);
  if (!existsSync(path)) {
    console.error(`Missing generated scene atlas asset: ${path}`);
    process.exit(1);
  }
}
for (const file of requiredFontFiles) {
  const path = join(buildDir, "assets", "fonts", file);
  if (!existsSync(path)) {
    console.error(`Missing Source Code Pro runtime asset: ${path}`);
    process.exit(1);
  }
}
for (const file of requiredUiFiles) {
  const path = join(buildDir, "assets", "ui", ...file.split("/"));
  if (!existsSync(path)) {
    console.error(`Missing packaged RmlUi resource: ${path}`);
    process.exit(1);
  }
}
requireValidUiGraph(join(buildDir, "assets", "ui"), "Web build");
if (existsSync(join(buildDir, "assets", "art"))) {
  console.error("Web build still contains source art in addition to the generated scene atlas.");
  console.error("Rebuild the web target so only runtime atlas assets are deployed.");
  process.exit(1);
}

rmSync(outputDir, { recursive: true, force: true });
mkdirSync(outputDir, { recursive: true });

for (const file of requiredFiles) {
  copyFileBytes(join(buildDir, file), join(outputDir, file));
}
copyFileBytes(join(buildDir, "rocket_rogue.html"), join(outputDir, "index.html"));

copyDirectory(join(buildDir, "assets"), join(outputDir, "assets"));
copyFileBytes("staticwebapp.config.json", join(outputDir, "staticwebapp.config.json"));
requireValidUiGraph(join(outputDir, "assets", "ui"), "Prepared Azure package");

console.log(`Prepared Azure Static Web Apps package in ${outputDir}`);
