import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import path from "node:path";

const EXPECTED_PLAYTEST_WAVES = [50, 250, 1000];
const READY_BUILD_STATUS = "ready";
const ACCEPTED_DECK_REVIEWS = new Set(["completed", "issues_corrected_and_resubmitted"]);

function isFiniteNumber(value) {
  return typeof value === "number" && Number.isFinite(value);
}

function isNonEmptyString(value) {
  return typeof value === "string" && value.trim().length > 0;
}

function isIsoDate(value) {
  return typeof value === "string" && /^\d{4}-\d{2}-\d{2}$/.test(value) && !Number.isNaN(Date.parse(`${value}T00:00:00Z`));
}

function equalNumberArrays(actual, expected) {
  return Array.isArray(actual)
    && actual.length === expected.length
    && actual.every((value, index) => value === expected[index]);
}

function requireValue(errors, condition, message) {
  if (!condition) {
    errors.push(message);
  }
}

export function validateReleasePolicy(policy) {
  const errors = [];

  requireValue(errors, policy?.schemaVersion === 1, "policy.schemaVersion must be 1");
  requireValue(errors, policy?.product?.name === "OREBIT", "policy.product.name must be OREBIT");
  requireValue(
    errors,
    isNonEmptyString(policy?.product?.corePromise)
      && /mining|mine/i.test(policy.product.corePromise)
      && /escape|home/i.test(policy.product.corePromise),
    "policy.product.corePromise must preserve the mine-and-escape promise"
  );
  requireValue(
    errors,
    policy?.product?.publicReleaseDate === null || isIsoDate(policy?.product?.publicReleaseDate),
    "policy.product.publicReleaseDate must be null or an ISO YYYY-MM-DD date"
  );

  requireValue(errors, policy?.store?.primary === "Steam", "policy.store.primary must be Steam");
  requireValue(errors, policy?.store?.steamNativeCampaign === true, "the campaign must remain Steam-native");
  requireValue(errors, policy?.store?.publicWebDemo === false, "a competing public web demo is not permitted");
  requireValue(errors, policy?.store?.priceUsd === 14.99, "the planned base price must remain $14.99");
  requireValue(errors, policy?.store?.launchDiscountPercent === 10, "the planned launch discount must remain 10%");

  requireValue(errors, policy?.platforms?.windowsX64 === "first_class", "Windows x64 must be first-class");
  requireValue(errors, policy?.platforms?.linuxX64 === "first_class", "Linux x64 must be first-class");
  requireValue(errors, policy?.platforms?.nativeLinux === true, "the Linux build must be native");
  requireValue(errors, policy?.platforms?.steamDeckTarget === "verified", "Steam Deck must target Verified");

  requireValue(
    errors,
    equalNumberArrays(policy?.audience?.playtestWaves, EXPECTED_PLAYTEST_WAVES),
    "Steam Playtest waves must be 50, 250, and 1000 players"
  );
  requireValue(errors, policy?.audience?.minimumWishlists === 10000, "the launch gate must require 10,000 wishlists");
  requireValue(errors, policy?.audience?.minimumDemoPlayers === 3000, "the launch gate must require 3,000 demo players");
  requireValue(errors, policy?.audience?.minimumRepeatIntentPercent === 70, "repeat-play intent must be at least 70%");
  requireValue(errors, policy?.audience?.minimumTradeoffUnderstandingPercent === 60, "ore/fuel/escape understanding must be at least 60%");
  requireValue(errors, policy?.audience?.minimumMedianSessionMinutes === 30, "median session length must be at least 30 minutes");

  requireValue(errors, policy?.notifications?.minimumCooldownDays >= 14, "Steam notification triggers must be separated by at least 14 days");
  requireValue(errors, policy?.notifications?.maximumNextFestAppearances === 1, "OREBIT has at most one Steam Next Fest appearance");
  requireValue(
    errors,
    policy?.notifications?.meaningfulUpdateCadenceWeeks?.minimum === 6
      && policy?.notifications?.meaningfulUpdateCadenceWeeks?.maximum === 8,
    "meaningful Steam updates must use the planned six-to-eight-week cadence"
  );

  for (const domain of ["art", "music", "story"]) {
    requireValue(errors, policy?.creative?.[domain] === "human_created", `${domain} must be human-created`);
  }
  for (const forbidden of ["art", "music", "story", "dialogue", "lore", "characters", "collaborator_style_imitation"]) {
    requireValue(
      errors,
      Array.isArray(policy?.creative?.aiForbidden) && policy.creative.aiForbidden.includes(forbidden),
      `AI boundary must explicitly forbid ${forbidden}`
    );
  }

  requireValue(errors, policy?.scope?.virtualReality === false, "OREBIT must not have VR scope");
  requireValue(errors, policy?.scope?.liveService === false, "OREBIT must not be turned into a live service");

  return errors;
}

export function validateReleaseCandidate(policy, readiness) {
  const errors = validateReleasePolicy(policy);
  const audience = policy?.audience ?? {};

  requireValue(errors, readiness?.schemaVersion === 1, "readiness.schemaVersion must be 1");
  requireValue(errors, readiness?.product === "OREBIT", "readiness.product must be OREBIT");
  requireValue(errors, isIsoDate(readiness?.releaseDate), "a release candidate requires an ISO YYYY-MM-DD releaseDate");
  requireValue(
    errors,
    policy?.product?.publicReleaseDate === readiness?.releaseDate,
    "policy and readiness release dates must match after the readiness gates pass"
  );

  for (const [field, minimum] of [
    ["wishlists", audience.minimumWishlists],
    ["demoPlayers", audience.minimumDemoPlayers],
    ["repeatIntentPercent", audience.minimumRepeatIntentPercent],
    ["tradeoffUnderstandingPercent", audience.minimumTradeoffUnderstandingPercent],
    ["medianSessionMinutes", audience.minimumMedianSessionMinutes]
  ]) {
    const actual = readiness?.metrics?.[field];
    requireValue(
      errors,
      isFiniteNumber(actual) && actual >= minimum,
      `readiness.metrics.${field} must be at least ${minimum}`
    );
  }

  const playtestWaves = readiness?.playtest?.waves;
  requireValue(
    errors,
    Array.isArray(playtestWaves)
      && equalNumberArrays(playtestWaves.map((wave) => wave?.targetPlayers), EXPECTED_PLAYTEST_WAVES),
    "Steam Playtest evidence must contain the 50, 250, and 1000-player waves in order"
  );
  if (Array.isArray(playtestWaves)) {
    for (const wave of playtestWaves) {
      const label = `${wave?.targetPlayers ?? "unknown"}-player Playtest wave`;
      requireValue(errors, wave?.status === "complete", `${label} must be complete`);
      requireValue(errors, wave?.repeatIntentPercent >= audience.minimumRepeatIntentPercent, `${label} repeat-play intent is below threshold`);
      requireValue(errors, wave?.tradeoffUnderstandingPercent >= audience.minimumTradeoffUnderstandingPercent, `${label} tradeoff understanding is below threshold`);
      requireValue(errors, wave?.medianSessionMinutes >= audience.minimumMedianSessionMinutes, `${label} median session is below threshold`);
      requireValue(errors, wave?.windowsLinuxProgressionParity === true, `${label} must have clean Windows/Linux progression parity`);
      requireValue(errors, wave?.deckBlockers === 0, `${label} must have zero Steam Deck blockers`);
      requireValue(errors, isNonEmptyString(wave?.evidence), `${label} requires a retained evidence path or URL`);
    }
  }

  for (const build of ["windowsX64", "linuxX64", "steamDeck"]) {
    requireValue(errors, readiness?.builds?.[build]?.status === READY_BUILD_STATUS, `${build} build status must be ready`);
    requireValue(errors, isNonEmptyString(readiness?.builds?.[build]?.evidence), `${build} requires a verification evidence path or URL`);
  }
  requireValue(
    errors,
    ACCEPTED_DECK_REVIEWS.has(readiness?.builds?.steamDeck?.valveReview),
    "Steam Deck review must be completed or corrected and resubmitted"
  );
  for (const check of [
    "controllerNavigationAndGlyphs",
    "noLauncherOrMouseOnlyStep",
    "readableAt1280x800",
    "suspendResume",
    "offlineFirstLaunch",
    "crossPlatformSteamCloud",
    "stableFramePacing",
    "onScreenKeyboardForTextEntry"
  ]) {
    requireValue(errors, readiness?.builds?.steamDeck?.checks?.[check] === true, `Steam Deck check ${check} must pass`);
  }

  for (const field of [
    "scopeLocked",
    "featureComplete",
    "capsuleArtHumanApproved",
    "musicHumanApprovedOrNotInScope",
    "storyHumanApprovedOrNotInScope"
  ]) {
    requireValue(errors, readiness?.content?.[field] === true, `readiness.content.${field} must be true`);
  }

  for (const field of [
    "comingSoonPageReady",
    "publicDemoReleased",
    "curatorConnectPrepared",
    "launchAnnouncementPrepared",
    "broadcastPrepared"
  ]) {
    requireValue(errors, readiness?.steam?.[field] === true, `readiness.steam.${field} must be true`);
  }
  requireValue(errors, readiness?.steam?.nextFestAppearances === 1, "the final polished demo must complete OREBIT's one Next Fest appearance");

  return errors;
}

function readJson(filePath) {
  return JSON.parse(readFileSync(filePath, "utf8"));
}

function parseArguments(argv) {
  const options = {
    mode: "policy",
    policyPath: "config/steam-release-policy.json",
    readinessPath: "config/steam-readiness.json"
  };

  for (let index = 0; index < argv.length; index += 1) {
    const argument = argv[index];
    if (argument === "--policy") {
      options.mode = "policy";
    } else if (argument === "--release-candidate") {
      options.mode = "release-candidate";
    } else if (argument === "--policy-path") {
      options.policyPath = argv[index + 1];
      index += 1;
    } else if (argument === "--readiness-path") {
      options.readinessPath = argv[index + 1];
      index += 1;
    } else {
      throw new Error(`Unknown argument: ${argument}`);
    }
  }

  return options;
}

function runCli() {
  const options = parseArguments(process.argv.slice(2));
  const policy = readJson(options.policyPath);
  const errors = options.mode === "release-candidate"
    ? validateReleaseCandidate(policy, readJson(options.readinessPath))
    : validateReleasePolicy(policy);

  if (errors.length > 0) {
    console.error(`OREBIT Steam ${options.mode} verification failed:`);
    for (const error of errors) {
      console.error(`- ${error}`);
    }
    process.exitCode = 1;
    return;
  }

  console.log(
    options.mode === "release-candidate"
      ? "OREBIT Steam release-candidate gates passed."
      : "OREBIT Steam release policy passed."
  );
}

const invokedPath = process.argv[1] ? path.resolve(process.argv[1]) : "";
if (invokedPath === fileURLToPath(import.meta.url)) {
  runCli();
}
