import { spawnSync } from "node:child_process";
import { join } from "node:path";

const commands = {
  configure: (preset) => ["cmake", ["--preset", preset]],
  build: (preset) => ["cmake", ["--build", "--preset", preset]],
  test: (preset) => ["ctest", ["--preset", preset]]
};

const [mode, preset] = process.argv.slice(2);

if (!commands[mode] || !preset) {
  console.error("Usage: node tools/run-cmake-preset.mjs <configure|build|test> <preset>");
  process.exit(2);
}

const [command, args] = commands[mode](preset);

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
