import { existsSync, rmSync } from "node:fs";
import { join } from "node:path";
import { spawnSync } from "node:child_process";

const preset = "web-release";
const target = "rocket_core_tests";
const testDir = join("build", preset, "tests");
const testJs = join(testDir, `${target}.js`);
const testWasm = join(testDir, `${target}.wasm`);

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
  if (existsSync(testJs) && !existsSync(testWasm)) {
    removeIfPresent(
      testJs,
      "The Emscripten test launcher exists without its wasm sidecar, so Ninja would otherwise report no work to do."
    );
  }
}

forceRelinkIfWasmMissing();
run("cmake", ["--build", "--preset", preset, "--target", target]);
forceRelinkIfWasmMissing();

if (!existsSync(testJs) || !existsSync(testWasm)) {
  console.error(`Missing web test artifact: ${!existsSync(testJs) ? testJs : testWasm}`);
  process.exit(1);
}

run("ctest", ["--preset", preset]);
