import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { validateReleaseCandidate, validateReleasePolicy } from "./verify-steam-readiness.mjs";

function loadJson(filePath) {
  return JSON.parse(readFileSync(filePath, "utf8"));
}

function clone(value) {
  return structuredClone(value);
}

const policy = loadJson("config/steam-release-policy.json");
const readiness = loadJson("config/steam-readiness.json");

assert.deepEqual(validateReleasePolicy(policy), []);

{
  const invalid = clone(policy);
  invalid.scope.virtualReality = true;
  assert(validateReleasePolicy(invalid).some((error) => error.includes("VR scope")));
}

{
  const invalid = clone(policy);
  invalid.store.publicWebDemo = true;
  assert(validateReleasePolicy(invalid).some((error) => error.includes("public web demo")));
}

{
  const invalid = clone(policy);
  invalid.creative.music = "ai_generated";
  assert(validateReleasePolicy(invalid).some((error) => error.includes("music must be human-created")));
}

assert(validateReleaseCandidate(policy, readiness).length > 0);

{
  const readyPolicy = clone(policy);
  const ready = clone(readiness);
  readyPolicy.product.publicReleaseDate = "2028-04-12";
  ready.releaseDate = "2028-04-12";
  ready.metrics = {
    wishlists: 10000,
    demoPlayers: 3000,
    repeatIntentPercent: 70,
    tradeoffUnderstandingPercent: 60,
    medianSessionMinutes: 30
  };
  ready.playtest.waves = [50, 250, 1000].map((targetPlayers) => ({
    targetPlayers,
    status: "complete",
    repeatIntentPercent: 70,
    tradeoffUnderstandingPercent: 60,
    medianSessionMinutes: 30,
    windowsLinuxProgressionParity: true,
    deckBlockers: 0,
    evidence: `verification/playtest-${targetPlayers}.json`
  }));
  for (const build of ["windowsX64", "linuxX64", "steamDeck"]) {
    ready.builds[build].status = "ready";
    ready.builds[build].evidence = `verification/${build}.json`;
  }
  ready.builds.steamDeck.valveReview = "completed";
  for (const check of Object.keys(ready.builds.steamDeck.checks)) {
    ready.builds.steamDeck.checks[check] = true;
  }
  for (const field of Object.keys(ready.content)) {
    ready.content[field] = true;
  }
  ready.steam = {
    comingSoonPageReady: true,
    publicDemoReleased: true,
    curatorConnectPrepared: true,
    nextFestAppearances: 1,
    launchAnnouncementPrepared: true,
    broadcastPrepared: true
  };

  assert.deepEqual(validateReleaseCandidate(readyPolicy, ready), []);
}

console.log("OREBIT Steam readiness tests passed.");
