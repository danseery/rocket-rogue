import fs from "node:fs";
import path from "node:path";
import {fileURLToPath} from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const forbidden = [
  /\bcontent::scenario::/,
  /\bcontent::miningSite::/,
  /\bcontent::destination::(?:moon|mars|jupiter|saturn)\b/,
];
const forbiddenMiningLegacyNames = [
  /\bfixedStoryGate\b/,
  /\bMiningStorySiteProgress\b/,
  /\bCompoundStoryVault\b/,
  /\bstoryCritical\b/,
];

function read(relativePath) {
  return fs.readFileSync(path.join(root, relativePath), "utf8");
}

function functionBody(source, signature) {
  const start = source.indexOf(signature);
  if (start < 0) throw new Error(`Missing architecture boundary function: ${signature}`);
  const open = source.indexOf("{", start);
  if (open < 0) throw new Error(`Missing function body: ${signature}`);
  let depth = 0;
  for (let index = open; index < source.length; index += 1) {
    if (source[index] === "{") depth += 1;
    if (source[index] === "}") {
      depth -= 1;
      if (depth === 0) return source.slice(start, index + 1);
    }
  }
  throw new Error(`Unterminated function body: ${signature}`);
}

const violations = [];
function requireNoAuthoredIds(label, text) {
  for (const pattern of forbidden) {
    if (pattern.test(text)) violations.push(`${label} matches ${pattern}`);
  }
}

function requireNoLegacyMiningNames(label, text) {
  for (const pattern of forbiddenMiningLegacyNames) {
    if (pattern.test(text)) violations.push(`${label} retains obsolete mining name ${pattern}`);
  }
}

// Mining is a reusable activity. A site comes from a scenario reference, but
// its implementation can never branch on an authored story/destination ID.
for (const relativePath of [
  "src/core/MiningSystem.cpp",
  "src/core/MiningProgression.cpp",
  "src/core/MiniDroneCoordination.cpp",
  "src/core/ScenarioSystem.cpp",
]) {
  const source = read(relativePath);
  requireNoAuthoredIds(relativePath, source);
  if (relativePath !== "src/core/ScenarioSystem.cpp") {
    requireNoLegacyMiningNames(relativePath, source);
  }
}

// Route evaluation, generic Flyby execution, and generic extraction/reward
// delivery are narrower checks because their files also contain explicit
// legacy-save adapters. Those adapters are deliberately outside these runtime
// function boundaries and may retain old serialized identifiers.
const gameState = read("src/core/GameState.cpp");
for (const signature of [
  "FrontierGateStatus frontierGateStatusForDestination(",
  "FrontierGateStatus frontierGateStatus(",
  "bool canCommitToNextFrontier(",
]) {
  requireNoAuthoredIds(`src/core/GameState.cpp:${signature}`, functionBody(gameState, signature));
}

const research = read("src/core/ResearchSystem.cpp");
for (const signature of [
  "bool canStartScenarioFlyby(",
  "bool startScenarioFlybyRun(",
  "void completeFlybyRun(GameState& state, const ContentCatalog& catalog)",
  "void abortFlybyRun(GameState& state, const ContentCatalog& catalog)",
  "SurfaceActionOutcome extractSurfacePayload(GameState& state, const ContentCatalog& catalog)",
]) {
  requireNoAuthoredIds(`src/core/ResearchSystem.cpp:${signature}`, functionBody(research, signature));
}

if (violations.length) {
  console.error("Authored scenario or destination IDs escaped a reusable scenario boundary:\n" + violations.join("\n"));
  process.exit(1);
}

console.log("Scenario architecture boundary check passed.");
