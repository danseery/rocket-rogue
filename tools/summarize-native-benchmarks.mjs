import {
  mkdirSync,
  readFileSync,
  renameSync,
  rmSync,
  writeFileSync,
} from "node:fs";
import path from "node:path";
import {fileURLToPath} from "node:url";

export const benchmarkMetricDefinitions = Object.freeze([
  Object.freeze({key: "cpuMilliseconds", label: "CPU", unit: "ms"}),
  Object.freeze({key: "gpuMilliseconds", label: "GPU", unit: "ms"}),
  Object.freeze({
    key: "queuePresentReturnIntervalMilliseconds",
    label: "Queue/present return",
    unit: "ms",
  }),
  Object.freeze({key: "limiterIdleMilliseconds", label: "Limiter idle", unit: "ms"}),
  Object.freeze({key: "sceneDrawCalls", label: "Scene draws", unit: "count"}),
  Object.freeze({key: "uploadedBytes", label: "Uploaded bytes", unit: "bytes"}),
  Object.freeze({key: "pipelineEvents", label: "Pipeline events", unit: "count"}),
]);

const requiredRunFields = Object.freeze([
  "scenario",
  "renderer",
  "platform",
  "gpu",
  "presentIntervalSource",
]);

const identityFields = Object.freeze([
  Object.freeze({key: "scenario", label: "scenario"}),
  Object.freeze({key: "renderer", label: "renderer"}),
  Object.freeze({key: "platform", label: "platform"}),
  Object.freeze({key: "gpu", label: "GPU"}),
  Object.freeze({key: "seed", label: "seed"}),
  Object.freeze({key: "width", label: "width"}),
  Object.freeze({key: "height", label: "height"}),
  Object.freeze({key: "simulationFramesPerSecond", label: "simulation frame rate"}),
  Object.freeze({key: "targetFramesPerSecond", label: "target frame rate"}),
  Object.freeze({key: "activeRefreshRateHz", label: "active refresh rate"}),
  Object.freeze({key: "presentIntervalSource", label: "present interval source"}),
  Object.freeze({key: "initialGameplayStateHash", label: "initial gameplay-state hash"}),
  Object.freeze({key: "finalGameplayStateHash", label: "final gameplay-state hash"}),
]);

function isObject(value) {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

function assertObject(value, label, source) {
  if (!isObject(value)) throw new Error(`${source}: '${label}' must be an object.`);
}

function assertFinite(value, label, source, {minimum = -Infinity, integer = false} = {}) {
  if (!Number.isFinite(value) || value < minimum || (integer && !Number.isInteger(value))) {
    const qualifier = integer ? "integer" : "number";
    throw new Error(`${source}: '${label}' must be a finite ${qualifier} >= ${minimum}.`);
  }
}

function normalizeHash(value, label, source) {
  if (typeof value !== "string" || !/^0x[0-9a-f]{16}$/i.test(value)) {
    throw new Error(`${source}: '${label}' must be a 64-bit hexadecimal string.`);
  }
  return value.toLowerCase();
}

function validateDistribution(distribution, metric, source, expectedCount) {
  const label = `metrics.${metric}`;
  assertObject(distribution, label, source);
  assertFinite(distribution.count, `${label}.count`, source, {minimum: 1, integer: true});
  if (distribution.count !== expectedCount) {
    throw new Error(`${source}: '${label}.count' must equal samples.accepted (${expectedCount}).`);
  }
  for (const summary of ["minimum", "maximum", "average", "p50", "p95", "p99"]) {
    assertFinite(distribution[summary], `${label}.${summary}`, source, {minimum: 0});
  }
  if (distribution.minimum > distribution.p50
      || distribution.p50 > distribution.p95
      || distribution.p95 > distribution.p99
      || distribution.p99 > distribution.maximum
      || distribution.average < distribution.minimum
      || distribution.average > distribution.maximum) {
    throw new Error(`${source}: '${label}' summaries must lie in nondecreasing minimum/p50/p95/p99/maximum order.`);
  }
}

export function validateBenchmarkReport(report, source = "benchmark report") {
  assertObject(report, "report", source);
  if (report.schemaVersion !== 2) {
    throw new Error(`${source}: expected OREBIT benchmark schemaVersion 2, got ${String(report.schemaVersion)}.`);
  }

  assertObject(report.run, "run", source);
  for (const field of requiredRunFields) {
    if (typeof report.run[field] !== "string" || report.run[field].trim() === "") {
      throw new Error(`${source}: 'run.${field}' must be a non-empty string.`);
    }
  }
  assertFinite(report.run.seed, "run.seed", source, {minimum: 0, integer: true});
  if (!Number.isSafeInteger(report.run.seed)) {
    throw new Error(`${source}: 'run.seed' exceeds JavaScript's exact integer range.`);
  }
  assertFinite(report.run.width, "run.width", source, {minimum: 1, integer: true});
  assertFinite(report.run.height, "run.height", source, {minimum: 1, integer: true});
  assertFinite(report.run.targetFramesPerSecond, "run.targetFramesPerSecond", source, {minimum: Number.EPSILON});
  // Early schema-2 OpenGL baselines predate the explicit separation between
  // deterministic simulation cadence and resolved presentation cadence. For
  // those reports the single target value represented both contracts.
  if (report.run.simulationFramesPerSecond === undefined) {
    report.run.simulationFramesPerSecond = report.run.targetFramesPerSecond;
  }
  assertFinite(
    report.run.simulationFramesPerSecond,
    "run.simulationFramesPerSecond",
    source,
    {minimum: Number.EPSILON},
  );
  assertFinite(report.run.activeRefreshRateHz, "run.activeRefreshRateHz", source, {minimum: Number.EPSILON});
  assertFinite(report.run.warmupSeconds, "run.warmupSeconds", source, {minimum: 0});
  // Fixed-presented-frame captures intentionally serialize zero here because
  // their stopping condition is a frame count rather than wall-clock time.
  assertFinite(report.run.captureSeconds, "run.captureSeconds", source, {minimum: 0});

  report.run.initialGameplayStateHash = normalizeHash(
    report.run.initialGameplayStateHash,
    "run.initialGameplayStateHash",
    source,
  );
  report.run.finalGameplayStateHash = normalizeHash(
    report.run.finalGameplayStateHash,
    "run.finalGameplayStateHash",
    source,
  );

  assertObject(report.samples, "samples", source);
  assertFinite(report.samples.accepted, "samples.accepted", source, {minimum: 1, integer: true});
  assertFinite(report.samples.rejected, "samples.rejected", source, {minimum: 0, integer: true});

  assertObject(report.queuePresentDeadlineMisses, "queuePresentDeadlineMisses", source);
  assertFinite(report.queuePresentDeadlineMisses.count, "queuePresentDeadlineMisses.count", source, {
    minimum: 0,
    integer: true,
  });
  assertFinite(report.queuePresentDeadlineMisses.rate, "queuePresentDeadlineMisses.rate", source, {minimum: 0});
  if (report.queuePresentDeadlineMisses.rate > 1) {
    throw new Error(`${source}: 'queuePresentDeadlineMisses.rate' must be between 0 and 1.`);
  }
  if (report.queuePresentDeadlineMisses.count > report.samples.accepted) {
    throw new Error(`${source}: queue/present deadline misses cannot exceed accepted samples.`);
  }
  const computedMissRate = report.queuePresentDeadlineMisses.count / report.samples.accepted;
  if (Math.abs(computedMissRate - report.queuePresentDeadlineMisses.rate) > 1e-9) {
    throw new Error(`${source}: queue/present deadline miss count and rate disagree.`);
  }

  assertObject(report.metrics, "metrics", source);
  for (const {key} of benchmarkMetricDefinitions) {
    validateDistribution(report.metrics[key], key, source, report.samples.accepted);
  }
  return report;
}

export function median(values) {
  if (!Array.isArray(values) || values.length === 0 || values.some((value) => !Number.isFinite(value))) {
    throw new Error("Median requires at least one finite number.");
  }
  const ordered = [...values].sort((left, right) => left - right);
  const midpoint = Math.floor(ordered.length / 2);
  return ordered.length % 2 === 1
    ? ordered[midpoint]
    : (ordered[midpoint - 1] + ordered[midpoint]) / 2;
}

function assertMatchingIdentity(reference, candidate, source) {
  for (const {key, label} of identityFields) {
    if (reference.run[key] !== candidate.run[key]) {
      throw new Error(
        `${source}: ${label} does not match the first run `
        + `(${JSON.stringify(candidate.run[key])} != ${JSON.stringify(reference.run[key])}).`,
      );
    }
  }
}

function acceptanceWarnings(reports, aggregateMetrics) {
  const warnings = [];
  const run = reports[0].run;
  const frameBudget = 1000 / run.targetFramesPerSecond;
  const totalAccepted = reports.reduce((sum, report) => sum + report.samples.accepted, 0);
  const totalRejected = reports.reduce((sum, report) => sum + report.samples.rejected, 0);
  const totalMisses = reports.reduce(
    (sum, report) => sum + report.queuePresentDeadlineMisses.count,
    0,
  );
  const worstMissRate = Math.max(...reports.map(
    (report) => report.queuePresentDeadlineMisses.rate,
  ));

  if (reports.some((report) => report.run.warmupSeconds < 10)) {
    warnings.push("At least one run used less than the required 10-second warm-up.");
  }
  const nominalCaptureSeconds = (report) => report.run.captureSeconds > 0
    ? report.run.captureSeconds
    : (report.samples.accepted + report.samples.rejected) / report.run.targetFramesPerSecond;
  if (reports.some((report) => nominalCaptureSeconds(report) < 60)) {
    warnings.push("At least one run captured less than the required 60 seconds.");
  }
  if (totalRejected > 0) {
    warnings.push(`${totalRejected} invalid frame sample(s) were rejected across the run set.`);
  }
  if ((totalAccepted > 0 && totalMisses / totalAccepted > 0.01) || worstMissRate > 0.01) {
    warnings.push("Queue/present deadline misses exceed the 1% ceiling in the aggregate or an individual run.");
  }
  if (reports.some((report) => report.metrics.pipelineEvents.maximum > 0)) {
    warnings.push("Timed pipeline events were recorded; completion acceptance requires zero.");
  }
  if (aggregateMetrics.cpuMilliseconds.p99 > frameBudget) {
    warnings.push(
      `Median run-level CPU p99 (${aggregateMetrics.cpuMilliseconds.p99.toFixed(3)} ms) `
      + `exceeds the ${frameBudget.toFixed(3)} ms frame budget.`,
    );
  }
  if (aggregateMetrics.gpuMilliseconds.p99 > frameBudget) {
    warnings.push(
      `Median run-level GPU p99 (${aggregateMetrics.gpuMilliseconds.p99.toFixed(3)} ms) `
      + `exceeds the ${frameBudget.toFixed(3)} ms frame budget.`,
    );
  }
  if (aggregateMetrics.queuePresentReturnIntervalMilliseconds.p99 > frameBudget) {
    warnings.push(
      `Median run-level queue/present-return p99 `
      + `(${aggregateMetrics.queuePresentReturnIntervalMilliseconds.p99.toFixed(3)} ms) `
      + `exceeds the nominal ${frameBudget.toFixed(3)} ms frame interval.`,
    );
  }
  if (run.targetFramesPerSecond > run.activeRefreshRateHz + 0.01) {
    warnings.push("The selected frame-rate target exceeds the active display refresh rate.");
  }
  return warnings;
}

export function aggregateBenchmarkReports(entries) {
  if (!Array.isArray(entries) || entries.length < 3) {
    throw new Error("At least three OREBIT benchmark reports are required.");
  }

  const reports = entries.map((entry, index) => {
    const source = entry?.source ?? `benchmark report ${index + 1}`;
    const report = entry?.report ?? entry;
    return validateBenchmarkReport(report, source);
  });
  for (let index = 1; index < reports.length; ++index) {
    assertMatchingIdentity(reports[0], reports[index], entries[index]?.source ?? `benchmark report ${index + 1}`);
  }

  const metrics = Object.fromEntries(benchmarkMetricDefinitions.map(({key}) => [key, {
    p50: median(reports.map((report) => report.metrics[key].p50)),
    p95: median(reports.map((report) => report.metrics[key].p95)),
    p99: median(reports.map((report) => report.metrics[key].p99)),
  }]));
  const accepted = reports.reduce((sum, report) => sum + report.samples.accepted, 0);
  const rejected = reports.reduce((sum, report) => sum + report.samples.rejected, 0);
  const deadlineMisses = reports.reduce(
    (sum, report) => sum + report.queuePresentDeadlineMisses.count,
    0,
  );

  return {
    aggregateSchemaVersion: 1,
    sourceSchemaVersion: 2,
    runCount: reports.length,
    identity: {
      scenario: reports[0].run.scenario,
      renderer: reports[0].run.renderer,
      platform: reports[0].run.platform,
      gpu: reports[0].run.gpu,
      seed: reports[0].run.seed,
      width: reports[0].run.width,
      height: reports[0].run.height,
      simulationFramesPerSecond: reports[0].run.simulationFramesPerSecond,
      targetFramesPerSecond: reports[0].run.targetFramesPerSecond,
      activeRefreshRateHz: reports[0].run.activeRefreshRateHz,
      presentIntervalSource: reports[0].run.presentIntervalSource,
      initialGameplayStateHash: reports[0].run.initialGameplayStateHash,
      finalGameplayStateHash: reports[0].run.finalGameplayStateHash,
    },
    samples: {accepted, rejected},
    queuePresentDeadlineMisses: {
      count: deadlineMisses,
      rate: accepted === 0 ? 0 : deadlineMisses / accepted,
    },
    runPercentileMedians: metrics,
    warnings: acceptanceWarnings(reports, metrics),
    notes: [
      "Each value is the median of the corresponding run-level percentile; source frames are not pooled.",
      "Queue/present-return timing is CPU-side pacing evidence, not proof of displayed scanout timing.",
      "Draw and upload reduction acceptance requires a separate frozen-baseline comparison.",
    ],
    sources: entries.map((entry, index) => entry?.source ?? `benchmark report ${index + 1}`),
  };
}

export function loadBenchmarkReports(files) {
  const seen = new Set();
  return files.map((file) => {
    const resolved = path.resolve(file);
    const identity = process.platform === "win32" ? resolved.toLowerCase() : resolved;
    if (seen.has(identity)) {
      throw new Error(`${resolved}: each aggregate input must be a distinct benchmark file.`);
    }
    seen.add(identity);
    let report;
    try {
      report = JSON.parse(readFileSync(resolved, "utf8"));
    } catch (error) {
      throw new Error(`${resolved}: could not read a benchmark JSON report: ${error.message}`);
    }
    return {source: resolved, report};
  });
}

function formatMetric(value, unit) {
  if (unit === "ms") return value.toFixed(3);
  if (unit === "bytes") return Math.round(value).toLocaleString("en-US");
  return Number.isInteger(value) ? String(value) : value.toFixed(2);
}

export function formatBenchmarkAggregate(aggregate) {
  const identity = aggregate.identity;
  const cadence = `${identity.targetFramesPerSecond} presented FPS @ ${identity.activeRefreshRateHz} Hz`;
  const simulationCadence = Math.abs(
    identity.simulationFramesPerSecond - identity.targetFramesPerSecond,
  ) > 0.0001
    ? ` | ${identity.simulationFramesPerSecond} simulation Hz`
    : "";
  const lines = [
    `OREBIT benchmark aggregate (${aggregate.runCount} runs)`,
    `${identity.scenario} | ${identity.renderer} | ${identity.platform} | ${identity.gpu}`,
    `${identity.width}x${identity.height} | seed ${identity.seed} | `
      + cadence + simulationCadence,
    `${identity.initialGameplayStateHash} -> ${identity.finalGameplayStateHash}`,
    "",
    `${"Metric".padEnd(24)} ${"p50".padStart(12)} ${"p95".padStart(12)} ${"p99".padStart(12)}`,
  ];
  for (const {key, label, unit} of benchmarkMetricDefinitions) {
    const summary = aggregate.runPercentileMedians[key];
    lines.push(
      `${`${label} (${unit})`.padEnd(24)} `
      + `${formatMetric(summary.p50, unit).padStart(12)} `
      + `${formatMetric(summary.p95, unit).padStart(12)} `
      + `${formatMetric(summary.p99, unit).padStart(12)}`,
    );
  }
  lines.push(
    "",
    `Samples: ${aggregate.samples.accepted.toLocaleString("en-US")} accepted, `
      + `${aggregate.samples.rejected.toLocaleString("en-US")} rejected; `
      + `${aggregate.queuePresentDeadlineMisses.count.toLocaleString("en-US")} queue/present deadline misses `
      + `(${(aggregate.queuePresentDeadlineMisses.rate * 100).toFixed(3)}%).`,
  );
  if (aggregate.warnings.length === 0) {
    lines.push("Acceptance warnings: none.");
  } else {
    lines.push("Acceptance warnings:");
    for (const warning of aggregate.warnings) lines.push(`- ${warning}`);
  }
  return `${lines.join("\n")}\n`;
}

export function writeAggregateJson(outputPath, aggregate) {
  const resolved = path.resolve(outputPath);
  mkdirSync(path.dirname(resolved), {recursive: true});
  const temporary = path.join(
    path.dirname(resolved),
    `.${path.basename(resolved)}.tmp-${process.pid}`,
  );
  rmSync(temporary, {force: true});
  writeFileSync(temporary, `${JSON.stringify(aggregate, null, 2)}\n`, "utf8");
  try {
    renameSync(temporary, resolved);
  } finally {
    rmSync(temporary, {force: true});
  }
  return resolved;
}

export function parseBenchmarkSummaryArguments(arguments_) {
  const files = [];
  let outputPath;
  let help = false;
  for (let index = 0; index < arguments_.length; ++index) {
    const argument = arguments_[index];
    if (argument === "--help" || argument === "-h") {
      help = true;
    } else if (argument === "--json-output") {
      if (outputPath !== undefined) throw new Error("Option '--json-output' may only be specified once.");
      outputPath = arguments_[++index];
      if (!outputPath) throw new Error("Option '--json-output' requires a path.");
    } else if (argument.startsWith("--json-output=")) {
      if (outputPath !== undefined) throw new Error("Option '--json-output' may only be specified once.");
      outputPath = argument.slice("--json-output=".length);
      if (!outputPath) throw new Error("Option '--json-output' requires a path.");
    } else if (argument.startsWith("-")) {
      throw new Error(`Unknown option '${argument}'.`);
    } else {
      files.push(argument);
    }
  }
  return {files, outputPath, help};
}

export function benchmarkSummaryHelp(executable = "node tools/summarize-native-benchmarks.mjs") {
  return `Usage: ${executable} [--json-output aggregate.json] run1.json run2.json run3.json [runN.json ...]\n\n`
    + "Validates matching OREBIT schema-v2 runs, then reports medians of their run-level percentiles.\n";
}

function main() {
  const options = parseBenchmarkSummaryArguments(process.argv.slice(2));
  if (options.help) {
    process.stdout.write(benchmarkSummaryHelp());
    return;
  }
  if (options.files.length < 3) throw new Error("At least three benchmark JSON files are required.");
  if (options.outputPath) {
    const resolvedOutput = path.resolve(options.outputPath);
    if (options.files.some((file) => path.resolve(file) === resolvedOutput)) {
      throw new Error("The aggregate output path must not overwrite an input report.");
    }
  }
  const aggregate = aggregateBenchmarkReports(loadBenchmarkReports(options.files));
  process.stdout.write(formatBenchmarkAggregate(aggregate));
  if (options.outputPath) {
    const written = writeAggregateJson(options.outputPath, aggregate);
    process.stdout.write(`Aggregate JSON: ${written}\n`);
  }
}

const invokedPath = process.argv[1] ? path.resolve(process.argv[1]) : "";
if (invokedPath === fileURLToPath(import.meta.url)) {
  try {
    main();
  } catch (error) {
    process.stderr.write(`Benchmark summary failed: ${error.message}\n`);
    process.exitCode = 1;
  }
}
