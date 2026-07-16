import { existsSync, mkdtempSync, rmSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { delimiter, join } from "node:path";
import { spawnSync } from "node:child_process";

const isWindows = process.platform === "win32";

function commandExists(command, env = process.env) {
  const probe = isWindows ? "powershell" : "command";
  const args = isWindows
    ? ["-NoProfile", "-Command", `if (Get-Command '${command}' -ErrorAction SilentlyContinue) { exit 0 } else { exit 1 }`]
    : ["-v", command];
  const result = spawnSync(probe, args, { stdio: "ignore", shell: !isWindows, env });
  return result.status === 0;
}

function pathWithLocalEmsdk() {
  const repoEmsdk = join(process.cwd(), ".deps", "emsdk");
  const entries = [
    repoEmsdk,
    join(repoEmsdk, "upstream", "emscripten"),
    join(repoEmsdk, "upstream", "bin"),
    process.env.PATH ?? ""
  ];
  return entries.filter(Boolean).join(delimiter);
}

function commandExistsWithLocalEmsdk(command) {
  const env = { ...process.env, PATH: pathWithLocalEmsdk(), EMSDK: join(process.cwd(), ".deps", "emsdk") };
  return commandExists(command, env);
}

function parseSetOutput(text) {
  const env = { ...process.env };
  for (const line of text.split(/\r?\n/)) {
    const equals = line.indexOf("=");
    if (equals <= 0) {
      continue;
    }
    env[line.slice(0, equals)] = line.slice(equals + 1);
  }
  return env;
}

function findVisualStudioDevCmd() {
  if (!isWindows) {
    return undefined;
  }

  const candidates = [];
  if (process.env["ProgramFiles(x86)"]) {
    candidates.push(join(process.env["ProgramFiles(x86)"], "Microsoft Visual Studio", "Installer", "vswhere.exe"));
  }
  candidates.push("C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe");

  for (const vswhere of candidates) {
    if (!existsSync(vswhere)) {
      continue;
    }

    const result = spawnSync(vswhere, [
      "-latest",
      "-products",
      "*",
      "-requires",
      "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
      "-property",
      "installationPath"
    ], { encoding: "utf8" });
    const installPath = result.status === 0 ? result.stdout.trim() : "";
    if (installPath) {
      const devCmd = join(installPath, "Common7", "Tools", "VsDevCmd.bat");
      if (existsSync(devCmd)) {
        return devCmd;
      }
    }
  }

  for (const installPath of [
    "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community",
    "C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional",
    "C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise",
    "C:\\Program Files\\Microsoft Visual Studio\\2022\\BuildTools"
  ]) {
    const devCmd = join(installPath, "Common7", "Tools", "VsDevCmd.bat");
    if (existsSync(devCmd)) {
      return devCmd;
    }
  }

  return undefined;
}

function visualStudioDevEnv() {
  const devCmd = findVisualStudioDevCmd();
  if (!devCmd) {
    return undefined;
  }

  const tempDir = mkdtempSync(join(tmpdir(), "rocket-rogue-vs-"));
  const scriptPath = join(tempDir, "vs-env.cmd");
  try {
    writeFileSync(scriptPath, `@echo off\r\ncall "${devCmd}" -no_logo -arch=x64 -host_arch=x64 >nul\r\nset\r\n`);
    const result = spawnSync("cmd.exe", ["/d", "/c", scriptPath], { encoding: "utf8" });
    if (result.status !== 0) {
      return undefined;
    }
    return { devCmd, env: parseSetOutput(result.stdout) };
  } finally {
    rmSync(tempDir, { force: true, recursive: true });
  }
}

function statusLine(ok, label, detail) {
  const marker = ok ? "ok" : "missing";
  console.log(`${marker.padEnd(7)} ${label}${detail ? ` - ${detail}` : ""}`);
}

const repoEmsdk = join(process.cwd(), ".deps", "emsdk");
const emsdkEnv = isWindows ? join(repoEmsdk, "emsdk_env.ps1") : join(repoEmsdk, "emsdk_env.sh");
const emscriptenToolchain = join(repoEmsdk, "upstream", "emscripten", "cmake", "Modules", "Platform", "Emscripten.cmake");

const cmake = commandExists("cmake");
const ninja = commandExists("ninja");
const node = commandExists("node");
const npm = isWindows ? commandExists("npm.cmd") : commandExists("npm");
const vsEnv = visualStudioDevEnv();
const nativeCompilerOnPath = ["cl", "clang++", "g++"].some(commandExists);
const nativeCompilerViaVs = vsEnv ? commandExists("cl", vsEnv.env) : false;
const nativeCompiler = nativeCompilerOnPath || nativeCompilerViaVs;
const emsdkCheckout = existsSync(emsdkEnv);
const emscriptenConfigured = existsSync(emscriptenToolchain);
const emcc = commandExists("emcc") || commandExistsWithLocalEmsdk("emcc");

console.log("Rocket Rogue toolchain check");
statusLine(cmake, "CMake");
statusLine(ninja, "Ninja");
statusLine(node, "Node.js");
statusLine(npm, isWindows ? "npm.cmd" : "npm", isWindows ? "use npm.cmd in PowerShell to avoid the npm.ps1 execution-policy shim" : "");
statusLine(
  nativeCompiler,
  "Native C++20 compiler",
  nativeCompilerOnPath
    ? "available on PATH"
    : nativeCompilerViaVs
      ? `available through ${vsEnv.devCmd}`
      : "needed for native-debug and native-release"
);
statusLine(emsdkCheckout, "Emscripten SDK checkout", emsdkEnv);
statusLine(emscriptenConfigured, "Emscripten CMake toolchain", emscriptenToolchain);
statusLine(emcc, "emcc", "needed for web-release");

const nativeReady = cmake && ninja && nativeCompiler;
const webReady = cmake && ninja && emsdkCheckout && emscriptenConfigured && emcc;

console.log("");
statusLine(nativeReady, "native-debug/native-release presets");
statusLine(webReady, "web-release preset");

if (!nativeReady || !webReady) {
  console.log("");
  console.log("Workarounds:");
  if (!nativeReady) {
    console.log("- Native build: install Visual Studio Build Tools with the C++ workload, dot-source scripts\\env-windows.ps1, or use npm.cmd run configure:native:release.");
  }
  if (!webReady) {
    console.log("- Web build: run scripts\\install-windows.ps1, then dot-source scripts\\env-windows.ps1 before configuring web-release.");
    console.log("- If .deps\\emsdk exists but emcc is missing, rerun scripts\\install-windows.ps1 so emsdk install/activate completes.");
  }
  console.log("- Sanity-only fallback: node tools\\sanity-check.mjs");
  process.exit(1);
}

console.log("");
console.log("The native and web CMake presets have the required local toolchain pieces.");
