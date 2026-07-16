# Rocket Rogue POC

Rocket Rogue is a C++20/OpenGL rocket-launch roguelite with genuine native Windows and Linux applications plus a fully supported WebGL2/Emscripten build. It borrows the tension of a hidden crash-point launch game, then wraps it in ship-first module management, light astronaut consequences, and persistent roguelite unlock variety.

## What is implemented

- Portable C++ core for content, deterministic RNG, launch resolution, run state, save serialization, and balance tests.
- Native SDL 3 application with OpenGL 3.3 Core rendering, RmlUi, controller support, atomic per-user saves, high-DPI metrics, and resizable Windows/Linux windows.
- Emscripten app shell with WebGL2 rendering, browser localStorage persistence, RmlUi, and the existing DOM fallback.
- NASA-arcade presentation using procedural backdrops, telemetry lines, HTML mission-control controls, and swappable 90s-style sprite assets under `assets/art`.
- Frontier ladder: prove Earth Orbit repeatedly, commit to Moon, then continue outward through Mars, Outer Planets, Nearby Star, and Nearby Galaxy.
- Arrival operations: flyby, orbit, and landing gates. Perfect orbit rewards show both science and mission credits.
- Post-arrival research and surface expeditions starting at Mars, including materials, artifacts, field upgrades, and extraction risk.
- Mining mini-game with direct drone control, destructible chunked terrain, fog-of-war scanning, ore pockets, artifacts, oxygen, drill integrity, and stow/abort decisions.
- Shared surface fuel: the shuttle and mining drone draw from the same reserve, so a mining run competes with the route home. The current baseline mining oxygen tank is 30 seconds; crew, drones, and field upgrades can extend it up to the 120-second cap.
- Drone Bay progression with mining, resource, survey, stabilizer, attack, and defense drones. Combat-facing drones stay gated behind post-solar hostile-system progression.
- Ark campaign spine: Outer Planets discovery, scripted Ark jump/disaster beats, Navigation after the hostile-system stranding, and Ark fuel framing for later sorties.
- Harsh legacy failure: ship losses, astronaut memorials, module destruction, blueprint progress, and unlock variety.

## Quick prerequisites

The project is cross-platform by design. Portable game rules live in `rocket_core`; shared application, UI, and OpenGL behavior live in `rocket_app`; SDL and Emscripten provide separate platform adapters.

You need:

- CMake 3.22+
- Ninja
- A C++20 compiler for native builds and tests
- An OpenGL 3.3-capable Windows or Linux system for the playable native application
- Emscripten SDK for the browser build
- Node.js 24 for repo scripts, platform-boundary tests, and the included static server

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

Both installers keep project-local development state in ignored folders. SDL 3.4.10, FreeType 2.13.3, and the pinned RmlUi revision are built from a local source override when present or fetched reproducibly by CMake:

- `.deps/emsdk` for the Emscripten SDK
- `.deps/SDL`, `.deps/freetype`, and `.deps/RmlUi` as optional local source overrides
- `.venv` for Python tooling
- `node_modules` if Node dependencies are added later

Use `EMSDK_VERSION` or the script option to install a specific SDK version instead of `latest`.
Node dependencies are managed through `package-lock.json`; the installers use `npm ci` when the lockfile exists. The repo standard is Node 24, recorded in `.node-version` and `package.json`.

## Verify local toolchains

Before running CMake builds in a fresh terminal or Codex desktop session, run the toolchain doctor:

```powershell
node tools\verify-toolchain.mjs
```

It checks CMake, Ninja, Node, npm, a native C++ compiler, the project-local Emscripten checkout, the Emscripten CMake toolchain file, and `emcc`. If it reports missing native support, install Visual Studio Build Tools with the C++ workload or build from WSL/Ubuntu with `build-essential`. If it reports missing web support while `.deps\emsdk` exists, rerun:

```powershell
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
.\scripts\install-windows.ps1
. .\scripts\env-windows.ps1
```

PowerShell may block the `npm.ps1` shim on locked-down machines. Use `npm.cmd run ...` from PowerShell, or call Node scripts directly, for example `node tools\sanity-check.mjs`.

## Build for the browser with presets

Install the Emscripten SDK, then open a terminal with the Emscripten environment activated:

```powershell
. .\scripts\env-windows.ps1
node tools\verify-toolchain.mjs
cmake --preset web-release
cmake --build --preset web-release
```

## Build the native application

The native application does not contain a browser, Node, Electron, JavaScript, or WASM. On Windows PowerShell, configure, build, test, and package the x86-64 Release build with:

```powershell
npm.cmd run configure:native:release
npm.cmd run build:native:release
npm.cmd run test:native:release
npm.cmd run package:native
```

The executable is written to `build/native-release/bin/RocketRogue.exe`; the portable archive is `build/packages/RocketRogue-windows-x64.zip`. The same commands without the `.cmd` suffix produce `build/native-release/bin/RocketRogue` and `build/packages/RocketRogue-linux-x64.tar.gz` on Linux. Each archive contains the executable, `assets`, and `licenses` as ordinary files. See [Desktop Builds](docs/DESKTOP_BUILDS.md) for runtime behavior, save paths, package layout, and CI validation.

### Codex desktop sandbox note

In the Codex desktop sandbox on Windows, `cmake --build --preset web-release` may fail with `operation not permitted` when CMake launches Ninja. When a Codex agent needs to run this build, request escalated execution for the build command up front instead of trying the sandboxed command first. The working command is still:

```powershell
cmake --build --preset web-release
```

Codex agents must also use the known-good Windows command forms below instead of first trying commands that are known to fail in this workspace:

```powershell
$repo = (Get-Location).Path
git -c safe.directory="$repo" status --short --branch
git -c safe.directory="$repo" add --all
git -c safe.directory="$repo" commit -m "Commit message"
git -c safe.directory="$repo" push origin main
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

## Run native tests

With a native C++20 compiler and CMake installed:

```powershell
cmake --preset native-debug
cmake --build --preset native-debug
ctest --preset native-debug
```

This suite covers the portable core, mining rules, controller routing, browser-free application services, platform-boundary enforcement, atomic native storage, preference round trips, and missing/corrupt native assets. The Windows npm scripts import the Visual Studio developer environment automatically before running the native preset. If you call CMake directly in a fresh PowerShell session, dot-source the repo environment first:

```powershell
. .\scripts\env-windows.ps1
```

The repository also includes a lightweight Node sanity check:

```powershell
npm.cmd run sanity
```

If CMake reports `No CMAKE_CXX_COMPILER could be found`, the native compiler is missing from the active shell environment, not necessarily from the machine. Run `npm.cmd run doctor` first; on Windows it checks Visual Studio Build Tools through `VsDevCmd.bat`. If the web preset reports that Emscripten paths or `emcc` are missing, rerun `scripts\install-windows.ps1`; a checked-out `.deps\emsdk` folder by itself is not enough until `emsdk install` and `emsdk activate` have completed.

## Windows notes

For Windows native development, install Visual Studio Build Tools with the C++ workload, plus CMake and Ninja. For browser development, install and activate Emscripten before configuring the `web-release` preset:

```powershell
git clone https://github.com/emscripten-core/emsdk.git C:\dev\emsdk
cd C:\dev\emsdk
.\emsdk install latest
.\emsdk activate latest
.\emsdk_env.ps1
cd <repo-path>
cmake --preset web-release
cmake --build --preset web-release
node tools/serve.mjs build/web-release 8080
```

## WSL Ubuntu notes

WSL is a good development target for this project. For better build performance, keep the repo under the Linux filesystem, such as `~/src/RocketGame`, instead of building from `/mnt/c` or OneDrive.

Install the native and web prerequisites with `scripts/install-ubuntu.sh`. Its native package set includes the compiler, CMake/Ninja, Mesa OpenGL headers, and SDL's X11/Wayland development headers. The minimal core toolchain is:

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

Build and test the native application:

```bash
cmake --preset native-debug
cmake --build --preset native-debug
ctest --preset native-debug
cmake --preset native-release
cmake --build --preset native-release
cmake --build --preset package-native
```

Build and serve the web application:

```bash
cmake --preset web-release
cmake --build --preset web-release
node tools/serve.mjs build/web-release 8080
```

## Controls

All player-facing screens in the native and web builds support standard-mapped Xbox, PlayStation, and Steam Deck controls. Use the left stick or D-pad for spatial menu focus, South (A/Cross) to confirm, East (B/Circle) to go back, and Menu to pause. Input-aware prompt bars show the active device family. The full contextual layout, preference schema, and release matrix live in [`docs/CONTROLLER_SUPPORT.md`](docs/CONTROLLER_SUPPORT.md).

Use the on-screen mission-control buttons or their controller prompts:

- Launch proving flights from the current frontier to bank flight data.
- During flight, choose `Return home`, `Cut engines`, or `Eject`.
- `Cut engines` lowers heat and vibration, slows the burn, and increases navigation drift.
- `Relief valve` vents physical pressure at the cost of navigation drift, with a small failure/decompression risk.
- `Jettison cargo` stabilizes fuel mix, but worsens navigation, vibration, and return-home risk.
- Seeded telemetry incidents create temporary one-or-two-system spikes, so a bad PRESS, VIB, MIX, NAV, or ABORT read can become a short decision window rather than a guaranteed cascade.
- New frontiers carry high mission pressure; repeated attempts and successful profiles reduce it, while pressure-control modules dampen the `PRESS` telemetry channel.
- After each mission summary, choose one of three refit cards or skip the refit window.
- In the hangar, repair damage, recruit crew, train/rest astronauts, then launch again.
- Use Push Deeper through the frontier ladder only after enough proving data is banked.
- Successful arrivals can open flyby/orbit/landing operations. Flyby can grant next-launch slingshot boosts; orbit grants science and credit rewards; landing opens post-arrival research and Surface Ops.
- Surface Ops uses action kits for survey/push/extract decisions and shared fuel for mining. `Mine deposit` deploys the mining drone once per surface loop; after that, the drone is offline and deeper pushes are unavailable.
- Mining keyboard controls: WASD/arrows move and face the rig, Space or mouse hold drills, `E` pulses the scanner, `R` stows payload, and Esc aborts.
- Mining controller controls: left stick moves and faces the rig, RT drills, West scans, North tethers, South stows at the ship, LB/RB service the drill/rig, and holding East recalls. Combat drones remain passive and the drill remains forward-facing.

## Deploy to Azure Static Web Apps

This repo includes a GitHub Actions workflow for Azure Static Web Apps Free:

- Workflow: `.github/workflows/azure-static-web-app.yml`
- Static app config: `staticwebapp.config.json`
- Deploy package script: `npm.cmd run prepare:azure` on Windows, `npm run prepare:azure` elsewhere
- Full setup notes: `docs/AZURE_STATIC_WEB_APPS.md`

The workflow expects a GitHub Actions secret named `AZURE_STATIC_WEB_APPS_API_TOKEN`.

## Portability notes

Native and web builds use the same game and presentation code through injected platform services. Emscripten/browser symbols are confined to `src/platform/web`; SDL owns native windowing, OpenGL context creation, events, gamepads, haptics, fullscreen, and shutdown. Steam services can be added behind these boundaries later without adding Steamworks or Steam Input to the current build, and a future Vulkan renderer can replace the OpenGL backend without changing gameplay.

Current boundaries:

- `src/core`: deterministic content, progression, save data, flight tuning, launch resolution, and tests.
- `src/game`: platform-neutral application orchestration, shared fixed-step runner, live launch/mining controls, RmlUi behavior, panel HTML generation, and render snapshots.
- `src/render`: shared OpenGL draw code with desktop GLSL 330 Core and WebGL2 GLSL ES 300 dialects.
- `src/platform/AppServices.h`: injected save, preference, host, controller, texture, renderer, UI, and browser-mirror contracts.
- `src/platform/sdl`: native SDL window/input/host, PNG texture loading, and atomic filesystem storage.
- `src/platform/web`: Emscripten entry point, browser storage, DOM mirror, async browser textures, and web gamepads.
- `web`: HTML/CSS/JS shell that forwards UI actions into exported C++ functions.
