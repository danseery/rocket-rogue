import { spawnSync } from "node:child_process";
import { existsSync } from "node:fs";
import { join } from "node:path";

const commands = {
  configure: (preset) => ["cmake", ["--preset", preset]],
  build: (preset) => ["cmake", ["--build", "--preset", preset]],
  test: (preset) => ["ctest", ["--preset", preset]]
};

const [mode, preset, ...extraArgs] = process.argv.slice(2);

if (!commands[mode] || !preset) {
  console.error("Usage: node tools/run-cmake-preset.mjs <configure|build|test> <preset> [extra args]");
  process.exit(2);
}

const [command, args] = commands[mode](preset);
args.push(...extraArgs);

function visualStudioNinjaPath() {
  const programFilesX86 = process.env["ProgramFiles(x86)"] || "C:\\Program Files (x86)";
  const programFiles = process.env.ProgramFiles || "C:\\Program Files";
  const vswhere = join(
    programFilesX86,
    "Microsoft Visual Studio",
    "Installer",
    "vswhere.exe"
  );
  const installations = [];
  if (existsSync(vswhere)) {
    const found = spawnSync(vswhere, [
      "-latest",
      "-products",
      "*",
      "-requires",
      "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
      "-property",
      "installationPath"
    ], { encoding: "utf8", windowsHide: true });
    if (found.status === 0 && found.stdout?.trim()) {
      installations.push(found.stdout.trim());
    }
  }

  for (const root of [programFiles, programFilesX86]) {
    for (const edition of ["Community", "Professional", "Enterprise", "BuildTools"]) {
      installations.push(join(root, "Microsoft Visual Studio", "2022", edition));
    }
  }

  for (const installation of installations) {
    const candidate = join(
      installation,
      "Common7",
      "IDE",
      "CommonExtensions",
      "Microsoft",
      "CMake",
      "Ninja",
      "ninja.exe"
    );
    if (existsSync(candidate)) return candidate;
  }
  return undefined;
}

if (process.platform === "win32"
    && mode === "configure"
    && !extraArgs.some((arg) => arg.toUpperCase().startsWith("-DCMAKE_MAKE_PROGRAM="))) {
  const bundledNinja = visualStudioNinjaPath();
  if (bundledNinja) {
    args.push(`-DCMAKE_MAKE_PROGRAM=${bundledNinja.replaceAll("\\", "/")}`);
  }
}

function psQuote(value) {
  return `'${String(value).replaceAll("'", "''")}'`;
}

let result;
if (process.platform === "win32") {
  const envScript = join(process.cwd(), "scripts", "env-windows.ps1");
  const invocation = `. ${psQuote(envScript)}; & ${psQuote(command)} ${args.map(psQuote).join(" ")}`;
  result = spawnSync(
    "powershell.exe",
    ["-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", invocation],
    { cwd: process.cwd(), stdio: "inherit" }
  );
} else {
  result = spawnSync(command, args, { cwd: process.cwd(), stdio: "inherit" });
}

if (result.error) {
  console.error(result.error.message);
  process.exit(1);
}

process.exit(result.status ?? 1);
