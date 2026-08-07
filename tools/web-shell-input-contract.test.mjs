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

  for (const helper of ["isLaunchActive", "isFlybyActive", "isOrbitActive", "isMiningActive"]) {
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
  assert.match(keyDown, /key === "c"[\s\S]*rr_cut_engines/);
  assert.match(keyDown, /key === "v"[\s\S]*rr_pressure_relief/);
  assert.match(keyDown, /if \(isFlybyActive\(\)\)/);
  assert.match(keyDown, /if \(isOrbitActive\(\)\)/);
  assert.match(keyDown, /if \(!isMiningActive\(\)\) return false/);
  assert.doesNotMatch(keyDown, /flybyContinue|orbitContinue/);
  assert.match(functionBody("releaseRealtimeInputs"), /launchKeys\.clear\(\)[\s\S]*updateLaunchMove\(\)/);
});

test("surface shortcuts yield to result actions and shutdown clears input ownership", () => {
  const globalKeyDown = shell.match(
    /window\.addEventListener\("keydown",[\s\S]*?\n    \}\);/,
  );
  assert.ok(globalKeyDown, "web shell should define global keyboard routing");
  for (const screen of ["surfaceScan", "surfacePush"]) {
    assert.match(
      globalKeyDown[0],
      new RegExp(
        `currentUiHostContext\\.realtimeActivityActive[\\s\\S]*screen === rrScreen\\.${screen}`,
      ),
      `${screen} Space shortcut must only own input during active play`,
    );
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
