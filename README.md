# Rocket Rogue POC

Rocket Rogue is a C++20/WebGL2 proof of concept for a fictional-stakes rocket launch roguelite. It borrows the tension of a hidden crash-point launch game, then wraps it in ship-first module management, light astronaut consequences, and persistent roguelite unlock variety.

## What is implemented

- Portable C++ core for content, deterministic RNG, launch resolution, run state, save serialization, and balance tests.
- Web-first Emscripten app shell with WebGL2 rendering and browser localStorage persistence.
- NASA-arcade presentation using procedural backdrops, telemetry lines, HTML mission-control controls, and swappable 90s-style sprite assets under `assets/art`.
- Frontier ladder: prove Earth Orbit repeatedly, commit to Moon, then continue outward through Mars, Outer Planets, Nearby Star, and Nearby Galaxy.
- Harsh legacy failure: ship losses, astronaut memorials, module destruction, blueprint progress, and unlock variety.

## Quick prerequisites

The project is cross-platform by design. The portable game rules live in `rocket_core`, while the browser target uses Emscripten and WebGL2.

You need:

- CMake 3.22+
- Ninja
- A C++20 compiler for native tests
- Emscripten SDK for the browser build
- Node.js 24 for repo scripts and the included static server

## Install dependencies

Windows PowerShell:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\scripts\install-windows.ps1
. .\scripts\env-windows.ps1
```

Ubuntu or WSL Ubuntu:

```bash
chmod +x scripts/install-ubuntu.sh scripts/env-ubuntu.sh
./scripts/install-ubuntu.sh
source scripts/env-ubuntu.sh
```

Both installers keep project-local development state in ignored folders:

- `.deps/emsdk` for the Emscripten SDK
- `.venv` for Python tooling
- `node_modules` if Node dependencies are added later

Use `EMSDK_VERSION` or the script option to install a specific SDK version instead of `latest`.
Node dependencies are managed through `package-lock.json`; the installers use `npm ci` when the lockfile exists. The repo standard is Node 24, recorded in `.node-version` and `package.json`.

## Build for the browser with presets

Install the Emscripten SDK, then open a terminal with the Emscripten environment activated:

```powershell
cmake --preset web-release
cmake --build --preset web-release
```

### Codex desktop sandbox note

In the Codex desktop sandbox on Windows, `cmake --build --preset web-release` may fail with `operation not permitted` when CMake launches Ninja. When a Codex agent needs to run this build, request escalated execution for the build command up front instead of trying the sandboxed command first. The working command is still:

```powershell
cmake --build --preset web-release
```

Codex agents must also use the known-good Windows command forms below instead of first trying commands that are known to fail in this workspace:

```powershell
git -c safe.directory=C:/Users/danie/OneDrive/Documents/RocketGame status --short --branch
git -c safe.directory=C:/Users/danie/OneDrive/Documents/RocketGame add --all
git -c safe.directory=C:/Users/danie/OneDrive/Documents/RocketGame commit -m "Commit message"
git -c safe.directory=C:/Users/danie/OneDrive/Documents/RocketGame push origin main
npm.cmd run sanity
npm.cmd run prepare:azure
```

The `safe.directory` override avoids Git's dubious-ownership error in the Codex sandbox without changing global Git config. `npm.cmd` avoids PowerShell's blocked `npm.ps1` shim.

Serve the generated build directory:

```powershell
node tools/serve.mjs build/web-release 8080
```

Prepare the clean static package used by Azure Static Web Apps:

```powershell
npm.cmd run prepare:azure
```

This writes only deployable game files to `dist/azure-static-web-app`.

Then open `http://localhost:8080/rocket_rogue.html`.

The equivalent manual command is:

```powershell
cmake -S . -B build/web-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$env:EMSDK/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"
cmake --build build/web-release
node tools/serve.mjs build/web-release 8080
```

## Run core tests

With a native C++20 compiler and CMake installed:

```powershell
cmake --preset native-debug
cmake --build --preset native-debug
ctest --preset native-debug
```

This workspace currently does not include a compiler or Emscripten on PATH, so the repository also includes a Node sanity check:

```powershell
npm.cmd run sanity
```

## Windows notes

For Windows native development, install Visual Studio Build Tools with the C++ workload, plus CMake and Ninja. For browser development, install and activate Emscripten before configuring the `web-release` preset:

```powershell
git clone https://github.com/emscripten-core/emsdk.git C:\dev\emsdk
cd C:\dev\emsdk
.\emsdk install latest
.\emsdk activate latest
.\emsdk_env.ps1
cd C:\Users\danie\OneDrive\Documents\RocketGame
cmake --preset web-release
cmake --build --preset web-release
node tools/serve.mjs build/web-release 8080
```

## WSL Ubuntu notes

WSL is a good development target for this project. For better build performance, keep the repo under the Linux filesystem, such as `~/src/RocketGame`, instead of building from `/mnt/c` or OneDrive.

Install native build tools:

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build git python3 nodejs npm
```

Install and activate Emscripten:

```bash
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
~/emsdk/emsdk install latest
~/emsdk/emsdk activate latest
source ~/emsdk/emsdk_env.sh
```

Build and serve:

```bash
cmake --preset web-release
cmake --build --preset web-release
node tools/serve.mjs build/web-release 8080
```

## Controls

Use the on-screen mission-control buttons:

- Launch proving flights from the current frontier to bank flight data.
- During flight, choose `Return home`, `Cut engines`, or `Eject`.
- `Cut engines` lowers heat and vibration, slows the burn, and increases navigation drift.
- `Relief valve` vents physical pressure at the cost of navigation drift, with a small failure/decompression risk.
- `Jettison cargo` stabilizes fuel mix, but worsens navigation, vibration, and return-home risk.
- Seeded telemetry incidents create temporary one-or-two-system spikes, so a bad PRESS, VIB, MIX, NAV, or ABORT read can become a short decision window rather than a guaranteed cascade.
- New frontiers carry high mission pressure; repeated attempts and successful profiles reduce it, while pressure-control modules dampen the `PRESS` telemetry channel.
- After each mission summary, choose one of three refit cards or skip the refit window.
- In the hangar, repair damage, recruit crew, train/rest astronauts, then launch again.
- Push deeper through the frontier ladder only after enough proving data is banked.

## Deploy to Azure Static Web Apps

This repo includes a GitHub Actions workflow for Azure Static Web Apps Free:

- Workflow: `.github/workflows/azure-static-web-app.yml`
- Static app config: `staticwebapp.config.json`
- Deploy package script: `npm.cmd run prepare:azure` on Windows, `npm run prepare:azure` elsewhere
- Full setup notes: `docs/AZURE_STATIC_WEB_APPS.md`

The workflow expects a GitHub Actions secret named `AZURE_STATIC_WEB_APPS_API_TOKEN`.

## Portability notes

The web app is Emscripten-first, but game logic is isolated in `rocket_core`. A future desktop or Steam build should add a second platform layer for windowing, save storage, input, audio, achievements, and cloud save without rewriting launch rules or progression.

Current boundaries:

- `src/core`: deterministic content, progression, save data, flight tuning, launch resolution, and tests.
- `src/game`: browser game-state orchestration, live launch controls, panel HTML generation, and render snapshots. `RocketGameApp` owns session flow; `GamePanel` owns mission-control HTML.
- `src/render`: WebGL2 renderer and texture upload for procedural shapes plus sprite assets.
- `src/platform`: tiny browser bridge for localStorage and panel updates.
- `web`: HTML/CSS/JS shell that forwards UI actions into exported C++ functions.
