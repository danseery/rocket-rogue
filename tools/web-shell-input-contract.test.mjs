import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { dirname, resolve } from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

const repositoryRoot = resolve(dirname(fileURLToPath(import.meta.url)), "..");
const shell = readFileSync(resolve(repositoryRoot, "web", "shell.html"), "utf8");
const webPlatform = readFileSync(
  resolve(repositoryRoot, "src", "platform", "web", "WebPlatform.cpp"),
  "utf8",
);
const webMain = readFileSync(resolve(repositoryRoot, "src/platform/web/WebMain.cpp"), "utf8");

test("controller lab honors query-only debug sessions without persisted settings", () => {
  const body = webMain.match(/EM_JS\(int, rr_controller_debug_tools_enabled, \(\), \{([\s\S]*?)\n\}\);/);
  assert.ok(body);
  const enabled = new Function("globalThis", "URLSearchParams", body[1]);
  const check = (search, setting) => enabled({
    location: { search },
    localStorage: { getItem: () => setting },
  }, URLSearchParams);
  assert.equal(check("?debug_tools=1", null), 1);
  assert.equal(check("?debug_minigames=1", null), 1);
  assert.equal(check("", "1"), 1);
  assert.equal(check("", null), 0);
  assert.equal(check("?ordinary_game=1", "0"), 0);
});

function functionBody(name) {
  const match = shell.match(new RegExp(
    `function ${name}\\([^)]*\\) \\{([\\s\\S]*?)\\n    \\}`,
  ));
  assert.ok(match, `web shell should define ${name}()`);
  return match[1];
}

test("completed realtime screens return input ownership to RmlUi", () => {
  assert.match(
    functionBody("setUiHostContext"),
    /realtimeActivityActive:\s*Boolean\(context\?\.realtimeActivityActive\)/,
  );
  assert.match(
    functionBody("setUiHostContext"),
    /previousRealtimeActivity\s*!==\s*currentUiHostContext\.realtimeActivityActive[\s\S]*releaseRealtimeInputs\(\)/,
    "active-to-results transitions on the same Screen enum must release held realtime input",
  );

  for (const helper of ["isLaunchActive", "isMiningActive"]) {
    assert.match(
      functionBody(helper),
      /rmlUiAvailable[\s\S]*currentUiHostContext\.realtimeActivityActive/,
      `${helper} must reject completed/takeover presentations`,
    );
  }

  assert.match(
    webPlatform,
    /rr_web_set_ui_host_context[\s\S]*context\.realtimeActivityActive\s*\?\s*1\s*:\s*0/,
    "the C++ web bridge must publish authoritative realtime activity state",
  );
});

test("realtime input cannot bypass explicit RmlUi actions", () => {
  const pointerDown = shell.match(
    /canvas\.addEventListener\("pointerdown",[\s\S]*?\n    \}\);/,
  );
  assert.ok(pointerDown, "web shell should define canvas pointer-down routing");
  assert.doesNotMatch(pointerDown[0], /flybyContinue|orbitContinue/);
  assert.match(pointerDown[0], /if \(!isMiningActive\(\)\) return/);

  const keyDown = functionBody("handleRealtimeKeyDown");
  const launchMove = functionBody("updateLaunchMove");
  assert.match(keyDown, /if \(isLaunchActive\(\)\)/);
  assert.match(keyDown, /launchKeys\.add\(key\)[\s\S]*updateLaunchMove\(\)/);
  assert.match(
    launchMove,
    /rr\.launchMove\(\(right \? 1 : 0\) - \(left \? 1 : 0\)/,
    "launch left/right keys must preserve the screen-space steering sign",
  );
  assert.match(keyDown, /key === "c"[\s\S]*rr_toggle_cruise/);
  assert.match(keyDown, /key === "m"[\s\S]*rr_open_navigation/);
  assert.doesNotMatch(keyDown, /rr_pressure_relief|rr_jettison|rr_eject/,
    "launch keyboard routing must not retain pressure, jettison, or manual-eject shortcuts");
  assert.match(keyDown, /if \(!isMiningActive\(\)\) return false/);
  assert.doesNotMatch(keyDown, /flybyContinue|orbitContinue/);
  assert.match(functionBody("releaseRealtimeInputs"), /launchKeys\.clear\(\)[\s\S]*updateLaunchMove\(\)/);
});

test("retired activity shortcuts stay out of the web input surface", () => {
  for (const retiredIdentifier of [
    "isFlybyActive",
    "isOrbitActive",
    "surfaceScanPulse",
    "surfacePushStep",
    "rr_debug_flyby",
    "rr_debug_orbit",
    "rr_debug_surface_scan",
    "rr_debug_surface_push",
  ]) {
    assert.doesNotMatch(shell, new RegExp(retiredIdentifier));
  }

  const availability = functionBody("setRmlUiEnabled");
  assert.match(availability, /realtimeActivityActive:\s*false/);
  assert.match(availability, /releaseRealtimeInputs\(\)/);
  assert.match(
    webPlatform,
    /RocketBridge\.setRmlUiEnabled\(rmlEnabled\)/,
    "the native web bridge must publish RmlUi availability changes",
  );
});

test("web shell leaves semantic scenario controls inside the shared RmlUi document", () => {
  assert.doesNotMatch(
    shell,
    /RocketBridge\.setPanel|setPanelHtml|data-rr-action/,
    "the web shell must not recreate a DOM panel or intercept semantic RmlUi actions",
  );
  assert.match(
    shell,
    /rr\.rmlMouseDown\(event\.clientX, event\.clientY, event\.button \|\| 0\)/,
    "web pointer input must reach the same RmlUi semantic-element dispatcher as native",
  );
  assert.match(
    shell,
    /rr\.uiActivateFocused\(\)/,
    "web keyboard and controller confirmation must activate the focused RmlUi element",
  );
});

test("web console exposes launch-lesson visual verification hooks", () => {
  assert.match(
    shell,
    /debugLaunchLesson:\s*\(lessonIndex\)\s*=>\s*rrCall\(\s*"rr_debug_launch_lesson"/,
    "debugLaunchLesson must call the exported launch-lesson scene hook",
  );
  assert.match(
    shell,
    /debugExit:\s*\(\)\s*=>\s*rrCall\("rr_debug_exit"\)/,
    "debugExit must leave the forced verification scene through its exported hook",
  );
  assert.match(
    shell,
    /get\("debug_launch_lesson"\)[\s\S]*?\^\[0-5\]\$[\s\S]*?window\.rr\.debugLaunchLesson\(lessonIndex\)/,
    "debug_launch_lesson must safely select the lessons or the main-belt crossing/collision sandboxes",
  );
});
