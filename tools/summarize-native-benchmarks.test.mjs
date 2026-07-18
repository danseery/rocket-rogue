import assert from "node:assert/strict";
import {mkdtempSync, readFileSync, rmSync, writeFileSync} from "node:fs";
import {tmpdir} from "node:os";
import path from "node:path";

import {
  aggregateBenchmarkReports,
  benchmarkSummaryHelp,
  formatBenchmarkAggregate,
  loadBenchmarkReports,
  median,
  parseBenchmarkSummaryArguments,
  validateBenchmarkReport,
  writeAggregateJson,
} from "./summarize-native-benchmarks.mjs";

const metricNames = [
  "cpuMilliseconds",
  "gpuMilliseconds",
  "queuePresentReturnIntervalMilliseconds",
  "limiterIdleMilliseconds",
  "sceneDrawCalls",
  "uploadedBytes",
  "pipelineEvents",
];

function distribution(base, maximum = base + 3) {
  return {
    count: 3600,
    minimum: Math.max(0, base - 1),
    maximum,
    average: base + 0.5,
    p50: base,
    p95: base + 1,
    p99: base + 2,
  };
}

function makeReport(runIndex = 0) {
  const offset = runIndex * 2;
  return {
    schemaVersion: 2,
    run: {
      scenario: "mining",
      renderer: "vulkan",
      platform: "Windows",
      gpu: "Test GPU",
      seed: 781079,
      width: 1280,
      height: 800,
      simulationFramesPerSecond: 60,
      targetFramesPerSecond: 60,
      activeRefreshRateHz: 240,
      presentIntervalSource: "queue-present-return",
      warmupSeconds: 10,
      captureSeconds: 60,
      initialGameplayStateHash: "0x0123456789abcdef",
      finalGameplayStateHash: "0xfedcba9876543210",
    },
    samples: {accepted: 3600, rejected: 0},
    queuePresentDeadlineMisses: {count: 0, rate: 0},
    metrics: {
      cpuMilliseconds: distribution(2 + offset),
      gpuMilliseconds: distribution(1 + offset),
      queuePresentReturnIntervalMilliseconds: {
        count: 3600,
        minimum: 15.8,
        maximum: 16.66,
        average: 16.1,
        p50: 16,
        p95: 16.3,
        p99: 16.6,
      },
      limiterIdleMilliseconds: distribution(10 + offset),
      sceneDrawCalls: distribution(17 + offset),
      uploadedBytes: distribution(150000 + offset * 1000),
      pipelineEvents: {
        count: 3600,
        minimum: 0,
        maximum: 0,
        average: 0,
        p50: 0,
        p95: 0,
        p99: 0,
      },
      deviceMemoryBytes: distribution(100000000),
    },
  };
}

assert.equal(median([5, 1, 3]), 3);
assert.equal(median([4, 1, 3, 2]), 2.5);
assert.throws(() => median([]), /at least one finite number/);

assert.deepEqual(parseBenchmarkSummaryArguments(["a.json", "b.json", "c.json"]), {
  files: ["a.json", "b.json", "c.json"],
  outputPath: undefined,
  help: false,
});
assert.deepEqual(parseBenchmarkSummaryArguments([
  "--json-output=aggregate.json", "a.json", "b.json", "c.json",
]), {
  files: ["a.json", "b.json", "c.json"],
  outputPath: "aggregate.json",
  help: false,
});
assert.throws(
  () => parseBenchmarkSummaryArguments(["--json-output"]),
  /requires a path/,
);
assert.throws(
  () => parseBenchmarkSummaryArguments(["--unknown"]),
  /Unknown option/,
);
assert.match(benchmarkSummaryHelp("summarize"), /summarize.*run1\.json/);

const reports = [makeReport(0), makeReport(1), makeReport(2)];
const aggregate = aggregateBenchmarkReports(reports);
assert.equal(aggregate.runCount, 3);
assert.equal(aggregate.samples.accepted, 10800);
assert.equal(aggregate.runPercentileMedians.cpuMilliseconds.p50, 4);
assert.equal(aggregate.runPercentileMedians.cpuMilliseconds.p95, 5);
assert.equal(aggregate.runPercentileMedians.cpuMilliseconds.p99, 6);
assert.equal(aggregate.runPercentileMedians.uploadedBytes.p50, 152000);
assert.deepEqual(aggregate.warnings, []);
assert.match(formatBenchmarkAggregate(aggregate), /OREBIT benchmark aggregate \(3 runs\)/);
assert.match(formatBenchmarkAggregate(aggregate), /Acceptance warnings: none/);

const resolvedCadenceReports = [makeReport(), makeReport(), makeReport()];
for (const report of resolvedCadenceReports) report.run.targetFramesPerSecond = 59.951;
const resolvedCadenceAggregate = aggregateBenchmarkReports(resolvedCadenceReports);
assert.match(
  formatBenchmarkAggregate(resolvedCadenceAggregate),
  /59\.951 presented FPS @ 240 Hz \| 60 simulation Hz/,
);

const legacySchemaTwoReport = makeReport();
delete legacySchemaTwoReport.run.simulationFramesPerSecond;
validateBenchmarkReport(legacySchemaTwoReport, "legacy schema-2 report");
assert.equal(legacySchemaTwoReport.run.simulationFramesPerSecond, 60);

assert.throws(() => aggregateBenchmarkReports(reports.slice(0, 2)), /At least three/);
const wrongSchema = makeReport();
wrongSchema.schemaVersion = 1;
assert.throws(
  () => aggregateBenchmarkReports([makeReport(), makeReport(), wrongSchema]),
  /schemaVersion 2/,
);

for (const [field, replacement, expected] of [
  ["scenario", "orbit", /scenario does not match/],
  ["renderer", "opengl", /renderer does not match/],
  ["gpu", "Other GPU", /GPU does not match/],
  ["width", 1920, /width does not match/],
  ["height", 1080, /height does not match/],
  ["seed", 42, /seed does not match/],
  ["simulationFramesPerSecond", 30, /simulation frame rate does not match/],
  ["targetFramesPerSecond", 30, /target frame rate does not match/],
  ["activeRefreshRateHz", 60, /active refresh rate does not match/],
  ["initialGameplayStateHash", "0x1111111111111111", /initial gameplay-state hash does not match/],
  ["finalGameplayStateHash", "0x2222222222222222", /final gameplay-state hash does not match/],
]) {
  const mismatch = makeReport();
  mismatch.run[field] = replacement;
  assert.throws(
    () => aggregateBenchmarkReports([makeReport(), makeReport(), mismatch]),
    expected,
    `expected ${field} mismatch to be rejected`,
  );
}

const missingHash = makeReport();
delete missingHash.run.finalGameplayStateHash;
assert.throws(() => validateBenchmarkReport(missingHash), /64-bit hexadecimal string/);

const warnedReports = [makeReport(), makeReport(), makeReport()];
for (const report of warnedReports) {
  report.run.warmupSeconds = 1;
  report.run.captureSeconds = 3;
  report.samples.rejected = 2;
  report.queuePresentDeadlineMisses = {count: 72, rate: 0.02};
  report.metrics.pipelineEvents.maximum = 1;
  report.metrics.cpuMilliseconds.p99 = 20;
  report.metrics.cpuMilliseconds.maximum = 21;
  report.metrics.gpuMilliseconds.p99 = 19;
  report.metrics.gpuMilliseconds.maximum = 20;
  report.metrics.queuePresentReturnIntervalMilliseconds.p99 = 18;
  report.metrics.queuePresentReturnIntervalMilliseconds.maximum = 19;
}
const warned = aggregateBenchmarkReports(warnedReports);
assert(warned.warnings.some((warning) => warning.includes("10-second warm-up")));
assert(warned.warnings.some((warning) => warning.includes("60 seconds")));
assert(warned.warnings.some((warning) => warning.includes("invalid frame")));
assert(warned.warnings.some((warning) => warning.includes("1% ceiling")));
assert(warned.warnings.some((warning) => warning.includes("pipeline events")));
assert(warned.warnings.some((warning) => warning.includes("CPU p99")));
assert(warned.warnings.some((warning) => warning.includes("GPU p99")));
assert(warned.warnings.some((warning) => warning.includes("queue/present-return p99")));

const fixedFrameReports = [makeReport(), makeReport(), makeReport()];
for (const report of fixedFrameReports) report.run.captureSeconds = 0;
assert(!aggregateBenchmarkReports(fixedFrameReports).warnings.some(
  (warning) => warning.includes("60 seconds"),
));

const fixtureRoot = mkdtempSync(path.join(tmpdir(), "orebit-benchmark-summary-test-"));
try {
  const inputs = reports.map((report, index) => {
    const input = path.join(fixtureRoot, `run${index + 1}.json`);
    writeFileSync(input, JSON.stringify(report), "utf8");
    return input;
  });
  const loadedAggregate = aggregateBenchmarkReports(loadBenchmarkReports(inputs));
  assert.equal(loadedAggregate.runCount, 3);
  assert.deepEqual(loadedAggregate.sources, inputs.map((input) => path.resolve(input)));
  assert.throws(
    () => loadBenchmarkReports([inputs[0], inputs[1], inputs[0]]),
    /distinct benchmark file/,
  );

  const output = path.join(fixtureRoot, "nested", "aggregate.json");
  assert.equal(writeAggregateJson(output, aggregate), path.resolve(output));
  assert.deepEqual(JSON.parse(readFileSync(output, "utf8")), aggregate);
} finally {
  rmSync(fixtureRoot, {recursive: true, force: true});
}

assert.deepEqual(metricNames, Object.keys(aggregate.runPercentileMedians));
console.log("Native benchmark summary tool tests passed.");
