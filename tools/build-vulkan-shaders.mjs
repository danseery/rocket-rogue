import {
  accessSync,
  copyFileSync,
  existsSync,
  mkdtempSync,
  readFileSync,
  renameSync,
  rmSync,
  statSync,
} from "node:fs";
import {constants as fsConstants} from "node:fs";
import {tmpdir} from "node:os";
import path from "node:path";
import {spawnSync} from "node:child_process";
import {fileURLToPath, pathToFileURL} from "node:url";

const toolDirectory = path.dirname(fileURLToPath(import.meta.url));
const defaultRoot = path.resolve(toolDirectory, "..");

export const shaderJobs = Object.freeze([
  Object.freeze({source: "scene.vert", stage: "vert", output: "scene.vert.spv"}),
  Object.freeze({source: "scene.frag", stage: "frag", output: "scene.frag.spv"}),
  Object.freeze({source: "scene_instance.vert", stage: "vert", output: "scene_instance.vert.spv"}),
  Object.freeze({source: "scene_instance.frag", stage: "frag", output: "scene_instance.frag.spv"}),
  Object.freeze({source: "rml_ui.vert.glsl", stage: "vert", output: "rml_ui.vert.spv"}),
  Object.freeze({source: "rml_ui.frag.glsl", stage: "frag", output: "rml_ui.frag.spv"}),
]);

function executableExtensions(platform) {
  return platform === "win32" ? [".exe", ".cmd", ".bat", ""] : [""];
}

function isExecutable(file, platform) {
  try {
    if (!statSync(file).isFile()) return false;
    if (platform !== "win32") accessSync(file, fsConstants.X_OK);
    return true;
  } catch {
    return false;
  }
}

export function findExecutable(
  name,
  {env = process.env, platform = process.platform} = {},
) {
  const candidates = [];
  const extensions = executableExtensions(platform);
  const sdk = String(env.VULKAN_SDK ?? "").trim();
  if (sdk) {
    for (const directory of ["Bin", "bin", "MacOS", ""]) {
      for (const extension of extensions) {
        candidates.push(path.join(sdk, directory, `${name}${extension}`));
      }
    }
  }

  for (const directory of String(env.PATH ?? "").split(path.delimiter)) {
    if (!directory) continue;
    for (const extension of extensions) {
      candidates.push(path.join(directory, `${name}${extension}`));
    }
  }

  const seen = new Set();
  for (const candidate of candidates) {
    const normalized = path.resolve(candidate);
    const key = platform === "win32" ? normalized.toLowerCase() : normalized;
    if (seen.has(key)) continue;
    seen.add(key);
    if (isExecutable(normalized, platform)) return normalized;
  }
  return undefined;
}

function runProcess(executable, args, options) {
  const extension = path.extname(executable).toLowerCase();
  const shell = process.platform === "win32"
    && (extension === ".cmd" || extension === ".bat");
  return spawnSync(executable, args, {
    ...options,
    encoding: "utf8",
    shell,
    windowsHide: true,
  });
}

function commandFailure(executable, args, result) {
  const details = [result.stderr, result.stdout]
    .map((value) => String(value ?? "").trim())
    .filter(Boolean)
    .join("\n");
  const command = [executable, ...args].map((value) => JSON.stringify(value)).join(" ");
  if (result.error) {
    return `${command}\n${result.error.message}`;
  }
  return `${command}\nExited with code ${result.status ?? "unknown"}${details ? `:\n${details}` : "."}`;
}

function atomicCopy(source, destination) {
  const temporary = path.join(
    path.dirname(destination),
    `.${path.basename(destination)}.tmp-${process.pid}`,
  );
  rmSync(temporary, {force: true});
  copyFileSync(source, temporary);
  try {
    renameSync(temporary, destination);
  } finally {
    rmSync(temporary, {force: true});
  }
}

export function parseShaderBuildArguments(arguments_) {
  let check = false;
  let help = false;
  for (const argument of arguments_) {
    if (argument === "--check") {
      if (check) throw new Error("Option '--check' may only be specified once.");
      check = true;
    } else if (argument === "--help" || argument === "-h") {
      help = true;
    } else {
      throw new Error(`Unknown option '${argument}'. Run --help for usage.`);
    }
  }
  return {check, help};
}

export function shaderBuildHelp(executable = "node tools/build-vulkan-shaders.mjs") {
  return [
    "Usage:",
    `  ${executable}`,
    `  ${executable} --check`,
    "",
    "Compiles optimized Vulkan 1.3 SPIR-V for the scene and RmlUi shaders.",
    "The default mode atomically updates assets/shaders/*.spv.",
    "--check compiles into a temporary directory and byte-compares every",
    "committed output without modifying the workspace.",
  ].join("\n");
}

export function buildVulkanShaders({
  root = defaultRoot,
  check = false,
  env = process.env,
  platform = process.platform,
  glslcPath,
  spirvValPath,
  run = runProcess,
  log = console.log,
} = {}) {
  const shaderDirectory = path.join(root, "assets", "shaders");
  if (!existsSync(shaderDirectory)) {
    throw new Error(`Shader directory does not exist: ${shaderDirectory}`);
  }

  const compiler = glslcPath ?? findExecutable("glslc", {env, platform});
  if (!compiler) {
    throw new Error(
      "Unable to locate glslc. Install the Vulkan SDK and set VULKAN_SDK, "
      + "or add glslc to PATH.",
    );
  }
  const validator = spirvValPath === undefined
    ? findExecutable("spirv-val", {env, platform})
    : spirvValPath;

  for (const job of shaderJobs) {
    const source = path.join(shaderDirectory, job.source);
    if (!existsSync(source)) {
      throw new Error(`Required shader source is missing: ${source}`);
    }
  }

  const temporaryDirectory = mkdtempSync(
    path.join(tmpdir(), "orebit-vulkan-shaders-"),
  );
  try {
    for (const job of shaderJobs) {
      const output = path.join(temporaryDirectory, job.output);
      const args = [
        "--target-env=vulkan1.3",
        "-O",
        "-Werror",
        `-fshader-stage=${job.stage}`,
        job.source,
        "-o",
        output,
      ];
      const result = run(compiler, args, {cwd: shaderDirectory, env});
      if (result.error || result.status !== 0) {
        throw new Error(
          `glslc failed while compiling ${job.source}:\n`
          + commandFailure(compiler, args, result),
        );
      }
      if (!existsSync(output) || statSync(output).size === 0) {
        throw new Error(`glslc produced no SPIR-V for ${job.source}.`);
      }

      if (validator) {
        const validationArgs = ["--target-env", "vulkan1.3", output];
        const validation = run(validator, validationArgs, {cwd: shaderDirectory, env});
        if (validation.error || validation.status !== 0) {
          throw new Error(
            `spirv-val rejected ${job.output}:\n`
            + commandFailure(validator, validationArgs, validation),
          );
        }
      }
    }

    if (!validator) {
      log("spirv-val was not found; compiled output validation was skipped.");
    }

    if (check) {
      const mismatches = [];
      for (const job of shaderJobs) {
        const generated = path.join(temporaryDirectory, job.output);
        const committed = path.join(shaderDirectory, job.output);
        if (!existsSync(committed)) {
          mismatches.push(`${job.output} is missing`);
        } else if (!readFileSync(generated).equals(readFileSync(committed))) {
          mismatches.push(`${job.output} differs from ${job.source}`);
        }
      }
      if (mismatches.length) {
        throw new Error(
          "Committed Vulkan shaders are stale:\n"
          + mismatches.map((value) => `  - ${value}`).join("\n")
          + "\nRun 'node tools/build-vulkan-shaders.mjs' to regenerate them.",
        );
      }
      log(`Vulkan shader check passed (${shaderJobs.length} files, Vulkan 1.3, optimized).`);
      return;
    }

    // Compile and validate the complete set before replacing any committed
    // output, so a shader error cannot leave a half-regenerated asset set.
    for (const job of shaderJobs) {
      atomicCopy(
        path.join(temporaryDirectory, job.output),
        path.join(shaderDirectory, job.output),
      );
    }
    log(`Updated ${shaderJobs.length} optimized Vulkan 1.3 shader binaries.`);
  } finally {
    rmSync(temporaryDirectory, {recursive: true, force: true});
  }
}

function isMainModule() {
  if (!process.argv[1]) return false;
  return pathToFileURL(path.resolve(process.argv[1])).href === import.meta.url;
}

if (isMainModule()) {
  try {
    const options = parseShaderBuildArguments(process.argv.slice(2));
    if (options.help) {
      console.log(shaderBuildHelp());
    } else {
      buildVulkanShaders({check: options.check});
    }
  } catch (error) {
    console.error(`Vulkan shader build failed: ${error.message}`);
    process.exitCode = 1;
  }
}
