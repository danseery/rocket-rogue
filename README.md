# OREBIT (Rocket Rogue project)

OREBIT is a C++20 rocket-launch roguelite with direct Vulkan 1.3 applications for Windows and Linux plus a fully supported WebGL2/Emscripten build. The repository and some internal identifiers retain the Rocket Rogue project name. The game borrows the tension of a hidden crash-point launch, then wraps it in permanent ship systems, light astronaut consequences, surface extraction, and persistent roguelite unlock variety.

## What is implemented

- Portable C++ core for content, deterministic RNG, launch resolution, run state, save serialization, and balance tests.
- Native SDL 3 application with direct Vulkan 1.3 rendering, a Vulkan RmlUi host, controller support, atomic per-user saves, high-DPI metrics, and resizable Windows/Linux windows.
- Emscripten app shell with WebGL2 rendering, browser localStorage persistence, RmlUi, and the existing DOM fallback.
- NASA-arcade presentation using procedural backdrops, telemetry lines, HTML mission-control controls, and swappable 90s-style sprite assets under `assets/art`.
- Saved campaign and activity onboarding: New Game opens with the proving-flight brief, and the first launch, approach, flyby, orbit, landing, mining run, mini-drone encounter, and Prospector completion explain their progression value once without repeating on later saves.
- Frontier ladder: prove Earth Orbit, then reach Moon, Mars, Jupiter, Saturn, Uranus, and Neptune in order before continuing to Khepri Prime and the Rift Belt. Each successful arrival unlocks exactly the next destination; flyby, orbit, landing, and mining remain optional.
- Arrival operations: flyby, orbit, and landing gates with saved first-use briefs. Perfect orbit rewards show both science and mission credits.
- Post-arrival research and surface expeditions starting at Mars, including materials, artifacts, field upgrades, and extraction risk.
- Mining mini-game with direct drone control, destructible chunked terrain, fog-of-war scanning, ore pockets, physical artifact tethering, oxygen, drill integrity, a keyboard Toggle/Hold drill preference, thermal cutoff at 100% heat, and return/abort decisions.
- Shared surface fuel: the shuttle and mining drone draw from the same reserve, so a mining run competes with the route home. The current baseline mining oxygen tank is 30 seconds; crew, drones, and field upgrades can extend it up to the 120-second cap.
- Deterministic mining progression: chapter and surface depth select an Act and level, teaching noncombat excavation in Act 1, passive-drone combat in Act 2, and counter-heavy mastery in Act 3 without scaling difficulty from the equipped loadout. Rich-material guarantees and caps share one reward ledger.
- Lock-and-key artifact sites: forecasted Hazard, Survey, Mining, Resource, Attack, and Defense capabilities can open deterministic gates, while most sites preserve systemic alternatives such as manual triangulation, route preparation, terrain cover, or encounter clearance.
- The first mining contract reserves 3 safely recovered Common Ore to fabricate the Prospector Mk I, permanently unlocking one Mining Drone and one Drone Bay slot. Later research expands the bay with resource, survey, and hazard support; combat drones remain gated behind post-solar hostile-system progression.
- Permanent refit tracks: useful Flight Data or a successful arrival grants one saved shipyard opportunity. The pre-Moon ladder offers the next unique Reach, Control, and Recovery system; later boards return to randomized, role-diverse offers. Installed systems survive replacement ships even when expedition damage takes one offline.
- Ark campaign spine: the Straylight is completely absent before Neptune, then a saved full-screen discovery beat identifies it as the expedition's first viable home. The first Ark jump reaches friendly Aaru Vale; the second causes Arkfall near hostile Khepri Prime, where Navigation, Ark fuel, and passive combat take over.
- Harsh legacy failure: ship losses, astronaut memorials, expedition system outages, blueprint progress, and unlock variety without deleting permanent system ownership.

## Quick prerequisites

The project is cross-platform by design. Portable game rules live in `rocket_core`; shared application, UI, and backend-neutral scene composition live in `rocket_app`; SDL/Vulkan and Emscripten/WebGL2 provide separate native and browser backends.

You need:

- CMake 3.22+
- Ninja
- A C++20 compiler for native builds and tests
- A 64-bit Windows or Linux system with a Vulkan 1.3-capable GPU and current vendor driver for the playable native application
- Emscripten SDK for the browser build
- Node.js 22-24 for repo scripts, platform-boundary tests, and the included static server (CI uses Node.js 24)

Native builds fetch pinned Vulkan-Headers, Volk, and Vulkan Memory Allocator sources through CMake, so a Vulkan SDK is not needed for an ordinary build. Shader authors additionally need `glslc` and `spirv-val` from a Vulkan SDK (or distro packages) to regenerate and validate the committed SPIR-V.

Steamworks is optional. A Steam packaging build can configure `ROCKET_STEAMWORKS_SDK_ROOT` to initialize Steam before Vulkan and use `ISteamUtils::IsSteamRunningOnSteamDeck()` for the Deck default frame limit. Ordinary desktop and CI builds compile a no-SDK service and gain no Steam runtime dependency.

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

Both installers keep project-local development state in ignored folders. SDL 3.4.10, FreeType 2.13.3, and the pinned RmlUi revision use a local source override when available or are fetched reproducibly by CMake. Vulkan-Headers, Volk, and Vulkan Memory Allocator are fetched at exact commits for native builds:

- `.deps/emsdk` for the Emscripten SDK
- `.deps/SDL`, `.deps/freetype`, and `.deps/RmlUi` as optional local source overrides
- `.venv` for Python tooling
- `node_modules` if Node dependencies are added later

The installers default to the same Emscripten 6.0.0 version used by deployment CI. Override it with `EMSDK_VERSION` or the script option when deliberately testing another SDK.
Node dependencies are managed through `package-lock.json`; the installers use `npm ci` when the lockfile exists. Node 22-24 is supported, with Node 24 recorded in `.node-version` as the repo and CI default.

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

The portable archive includes the generated Vulkan SPIR-V and licenses for Vulkan-Headers, Volk, and Vulkan Memory Allocator. It deliberately does not include a Vulkan loader; Windows GPU drivers and the Linux/Steam Runtime supply it.

### Regenerate Vulkan shaders

Vulkan GLSL 450 sources and their generated SPIR-V live together under `assets/shaders`. After changing a native shader, run:

```powershell
npm.cmd run shaders:vulkan
npm.cmd run check:shaders:vulkan
```

The check recompiles for Vulkan 1.3, validates each module with `spirv-val` when available, and byte-compares the result with the packaged files. Runtime shader compilation is not part of the native application.

The save-isolated native benchmark CLI, three-run matrix, JSON metrics, and acceptance thresholds are documented in [Native performance benchmarks](docs/PERFORMANCE_BENCHMARKS.md).

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
.\emsdk install 6.0.0
.\emsdk activate 6.0.0
.\emsdk_env.ps1
cd <repo-path>
cmake --preset web-release
cmake --build --preset web-release
node tools/serve.mjs build/web-release 8080
```

## WSL Ubuntu notes

WSL is a good development target for this project. For better build performance, keep the repo under the Linux filesystem, such as `~/src/RocketGame`, instead of building from `/mnt/c` or OneDrive.

Install the native and web prerequisites with `scripts/install-ubuntu.sh`. Its native package set includes the compiler, CMake/Ninja, Mesa Vulkan drivers, Vulkan validation and shader tools, and SDL's X11/Wayland development headers. The minimal core toolchain is:

```bash
sudo apt update
sudo apt install -y build-essential cmake ninja-build git python3 nodejs npm
```

Install and activate Emscripten:

```bash
git clone https://github.com/emscripten-core/emsdk.git ~/emsdk
~/emsdk/emsdk install 6.0.0
~/emsdk/emsdk activate 6.0.0
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
- The yellow marker is the mission brief. Returning safely from farther beyond it brings home richer findings and more mission funding.
- During flight, choose `Return home`, `Cut engines`, or `Eject`.
- `Cut engines` lowers heat and vibration, slows the burn, and increases navigation drift.
- `Relief valve` vents physical pressure at the cost of navigation drift, with a small failure/decompression risk.
- `Jettison cargo` stabilizes fuel mix, but worsens navigation, vibration, and return-home risk.
- Seeded telemetry incidents create temporary one-or-two-system spikes, so a bad PRESS, VIB, MIX, NAV, or ABORT read can become a short decision window rather than a guaranteed cascade.
- New frontiers carry high mission pressure; repeated attempts and successful profiles reduce it, while pressure-control modules dampen the `PRESS` telemetry channel.
- Useful Flight Data or a successful arrival earns one permanent refit opportunity; crashes, shallow returns, and already-capped data do not.
- Before the Moon, choose the next unique Reach, Control, or Recovery upgrade, or keep the credits. Later refit boards return to randomized, role-diverse offers without duplicates.
- In the hangar, repair damage, recruit crew, train/rest astronauts, then launch again.
- Use Push Deeper through the frontier ladder only after enough proving data is banked.
- Successful arrivals can open flyby/orbit/landing operations. Their first selections pause for saved, one-time briefs: flyby and orbit explain that blueprint progress unlocks permanent ship upgrades, while landing explains surface work. The first mining brief presents the 3-Common-Ore Prospector contract before deployment; safely extracted ore advances its saved objective, and completion receives a one-time `PROSPECTOR ONLINE` acknowledgment before the Mining Drone joins future digs.
- Surface Ops uses action kits for survey/push/extract decisions and shared fuel for mining. `Mine deposit` deploys the mining drone once per surface loop; after that, the drone is offline and deeper pushes are unavailable.
- Mining keyboard controls: WASD/arrows move and face the rig, Space or mouse hold drills, `E` pulses the scanner, `T` tethers or releases an artifact, `R` leaves and banks the run while inside the ship ring, and Esc aborts.
- Mining controller controls: left stick moves and faces the rig, RT drills, West scans, North tethers, South leaves and banks at the ship, LB/RB service the drill/rig, and holding East recalls. Combat drones remain passive and the drill remains forward-facing.

## Deploy to Azure Static Web Apps

This repo includes a GitHub Actions workflow for Azure Static Web Apps Free:

- Workflow: `.github/workflows/azure-static-web-app.yml`
- Static app config: `staticwebapp.config.json`
- Deploy package script: `npm.cmd run prepare:azure` on Windows, `npm run prepare:azure` elsewhere
- Full setup notes: `docs/AZURE_STATIC_WEB_APPS.md`

The workflow expects a GitHub Actions secret named `AZURE_STATIC_WEB_APPS_API_TOKEN`.

## Portability notes

Native and web builds use the same game state and backend-neutral scene composition through injected platform services. Emscripten/browser symbols are confined to `src/platform/web`; SDL owns native windowing, Vulkan surface creation, events, gamepads, haptics, fullscreen, and shutdown. Steam services can be added behind these boundaries without adding Steamworks or Steam Input to the current build.

Current boundaries:

- `src/core`: deterministic content, progression, save data, flight tuning, launch resolution, and tests.
- `src/game`: platform-neutral application orchestration, shared fixed-step runner, live launch/mining controls, RmlUi behavior, panel HTML generation, and render snapshots.
- `src/render`: `SceneComposer` and immutable `ScenePacket` data shared by the direct Vulkan 1.3 native backend and the WebGL2 browser backend, plus backend-specific RmlUi render hosts.
- `src/platform/AppServices.h`: injected save, preference, host, controller, texture, renderer, UI, and browser-mirror contracts.
- `src/platform/sdl`: native SDL window/input/host, PNG texture loading, and atomic filesystem storage.
- `src/platform/web`: Emscripten entry point, browser storage, DOM mirror, async browser textures, and web gamepads.
- `web`: HTML/CSS/JS shell that forwards UI actions into exported C++ functions.
