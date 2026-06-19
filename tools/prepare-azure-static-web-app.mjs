import { existsSync, mkdirSync, readFileSync, readdirSync, rmSync, statSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const buildDir = "build/web-release";
const outputDir = "dist/azure-static-web-app";

const requiredFiles = [
  "rocket_rogue.html",
  "rocket_rogue.js",
  "rocket_rogue.wasm"
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

rmSync(outputDir, { recursive: true, force: true });
mkdirSync(outputDir, { recursive: true });

for (const file of requiredFiles) {
  copyFileBytes(join(buildDir, file), join(outputDir, file));
}

copyDirectory(join(buildDir, "assets"), join(outputDir, "assets"));
copyFileBytes("staticwebapp.config.json", join(outputDir, "staticwebapp.config.json"));

console.log(`Prepared Azure Static Web Apps package in ${outputDir}`);
