import assert from "node:assert/strict";
import {
  mkdirSync,
  mkdtempSync,
  readFileSync,
  rmSync,
  statSync,
  writeFileSync,
} from "node:fs";
import {tmpdir} from "node:os";
import path from "node:path";

import {
  buildVulkanShaders,
  parseShaderBuildArguments,
  shaderBuildHelp,
  shaderJobs,
} from "./build-vulkan-shaders.mjs";

assert.deepEqual(parseShaderBuildArguments([]), {check: false, help: false});
assert.deepEqual(parseShaderBuildArguments(["--check"]), {check: true, help: false});
assert.deepEqual(parseShaderBuildArguments(["-h"]), {check: false, help: true});
assert.throws(() => parseShaderBuildArguments(["--check", "--check"]), /only be specified once/);
assert.throws(() => parseShaderBuildArguments(["--unknown"]), /Unknown option/);
assert.match(shaderBuildHelp("shader-tool"), /shader-tool --check/);

const fixtureRoot = mkdtempSync(path.join(tmpdir(), "orebit-shader-tool-test-"));
try {
  const shaderDirectory = path.join(fixtureRoot, "assets", "shaders");
  mkdirSync(shaderDirectory, {recursive: true});
  for (const job of shaderJobs) {
    writeFileSync(
      path.join(shaderDirectory, job.source),
      `#version 450\n// ${job.source}\n`,
    );
  }

  let validationCount = 0;
  const fakeRun = (executable, args, options) => {
    if (executable === "fake-spirv-val") {
      validationCount += 1;
      return {status: 0, stdout: "", stderr: ""};
    }
    assert.equal(executable, "fake-glslc");
    assert(args.includes("--target-env=vulkan1.3"));
    assert(args.includes("-O"));
    assert(args.includes("-Werror"));
    const outputIndex = args.indexOf("-o");
    assert(outputIndex > 0);
    const source = path.join(options.cwd, args[outputIndex - 1]);
    const output = args[outputIndex + 1];
    writeFileSync(
      output,
      Buffer.concat([
        Buffer.from("deterministic-spv\0"),
        readFileSync(source),
      ]),
    );
    return {status: 0, stdout: "", stderr: ""};
  };

  buildVulkanShaders({
    root: fixtureRoot,
    glslcPath: "fake-glslc",
    spirvValPath: "fake-spirv-val",
    run: fakeRun,
    log: () => {},
  });
  assert.equal(validationCount, shaderJobs.length);

  // Regeneration must atomically replace an already-committed set on Windows
  // as well as POSIX platforms.
  buildVulkanShaders({
    root: fixtureRoot,
    glslcPath: "fake-glslc",
    spirvValPath: "fake-spirv-val",
    run: fakeRun,
    log: () => {},
  });
  assert.equal(validationCount, shaderJobs.length * 2);

  const committed = new Map(shaderJobs.map((job) => {
    const output = path.join(shaderDirectory, job.output);
    return [job.output, {
      bytes: readFileSync(output),
      modified: statSync(output).mtimeMs,
    }];
  }));

  validationCount = 0;
  buildVulkanShaders({
    root: fixtureRoot,
    check: true,
    glslcPath: "fake-glslc",
    spirvValPath: "fake-spirv-val",
    run: fakeRun,
    log: () => {},
  });
  assert.equal(validationCount, shaderJobs.length);
  for (const job of shaderJobs) {
    const output = path.join(shaderDirectory, job.output);
    assert(readFileSync(output).equals(committed.get(job.output).bytes));
    assert.equal(statSync(output).mtimeMs, committed.get(job.output).modified);
  }

  writeFileSync(
    path.join(shaderDirectory, "scene.vert"),
    "#version 450\n// changed\n",
  );
  assert.throws(() => buildVulkanShaders({
    root: fixtureRoot,
    check: true,
    glslcPath: "fake-glslc",
    spirvValPath: "fake-spirv-val",
    run: fakeRun,
    log: () => {},
  }), /scene\.vert\.spv differs from scene\.vert/);
  assert(readFileSync(path.join(shaderDirectory, "scene.vert.spv"))
    .equals(committed.get("scene.vert.spv").bytes));
} finally {
  rmSync(fixtureRoot, {recursive: true, force: true});
}

console.log("Vulkan shader build tool tests passed.");
