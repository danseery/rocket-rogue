import { existsSync, rmSync } from "node:fs";
import { join } from "node:path";
import { spawnSync } from "node:child_process";

const preset = "web-release";
const testDir = join("build", preset, "tests");
const targets = [
  "rocket_core_tests",
  "rocket_mining_progression_tests",
  "rocket_mining_economy_tests",
  "rocket_controller_input_tests",
  "rocket_app_services_tests"
];

function artifacts(target) {
  return {
    js: join(testDir, `${target}.js`),
    wasm: join(testDir, `${target}.wasm`)
  };
}

function run(command, args) {
  const result = spawnSync(command, args, { cwd: process.cwd(), stdio: "inherit" });
  if (result.error) {
    console.error(result.error.message);
    process.exit(1);
  }
  if ((result.status ?? 1) !== 0) {
    process.exit(result.status ?? 1);
  }
}

function removeIfPresent(path, reason) {
  if (!existsSync(path)) {
    return;
  }
  try {
    rmSync(path, { force: true });
  } catch (error) {
    console.error(`Could not remove stale ${path}.`);
    console.error(reason);
    if (error && error.message) {
      console.error(error.message);
    }
    process.exit(1);
  }
}

function forceRelinkIfWasmMissing() {
  for (const target of targets) {
    const { js, wasm } = artifacts(target);
    if (existsSync(js) && !existsSync(wasm)) {
      removeIfPresent(
        js,
        "The Emscripten test launcher exists without its wasm sidecar, so Ninja would otherwise report no work to do."
      );
    }
  }
}

forceRelinkIfWasmMissing();
run("cmake", ["--build", "--preset", preset, "--target", ...targets]);
forceRelinkIfWasmMissing();

for (const target of targets) {
  const { js, wasm } = artifacts(target);
  if (!existsSync(js) || !existsSync(wasm)) {
    console.error(`Missing web test artifact: ${!existsSync(js) ? js : wasm}`);
    process.exit(1);
  }
}

run("ctest", ["--preset", preset]);
