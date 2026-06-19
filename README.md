# Rocket Rogue POC

Rocket Rogue is a C++20/WebGL2 proof of concept for a fictional-stakes rocket launch roguelite. It borrows the tension of a hidden crash-point launch game, then wraps it in ship-first module management, light astronaut consequences, and persistent roguelite unlock variety.

## What is implemented

- Portable C++ core for content, deterministic RNG, launch resolution, run state, save serialization, and balance tests.
- Web-first Emscripten app shell with WebGL2 procedural rendering and browser localStorage persistence.
- Asset-free NASA-arcade presentation: primitive rocket silhouette, launch plume, telemetry line, destination backdrop, and HTML mission-control controls.
- Frontier ladder: prove Earth Orbit repeatedly, commit to Moon, then continue outward through Mars, Outer Planets, Nearby Star, and Nearby Galaxy.
- Harsh legacy failure: ship losses, astronaut memorials, module destruction, blueprint progress, and unlock variety.

## Quick prerequisites

The project is cross-platform by design. The portable game rules live in `rocket_core`, while the browser target uses Emscripten and WebGL2.

You need:

- CMake 3.22+
- Ninja
- A C++20 compiler for native tests
- Emscripten SDK for the browser build
- Node.js for the included static server

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
Node dependencies are managed through `package-lock.json`; the installers use `npm ci` when the lockfile exists.

## Build for the browser with presets

Install the Emscripten SDK, then open a terminal with the Emscripten environment activated:

```powershell
cmake --preset web-release
cmake --build --preset web-release
```

Serve the generated build directory:

```powershell
node tools/serve.mjs build/web-release 8080
```

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
node tools/sanity-check.mjs
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

- Adjust destination and eject target in the hangar.
- Launch, then eject manually before the hidden failure point.
- Spend fictional mission credits on repair, training, rest, or module offers.
- Push deeper through the destination ladder, or accept that going too far can end the expedition brutally.

## Portability notes

The web app is Emscripten-first, but game logic is isolated in `rocket_core`. A future desktop or Steam build should add a second platform layer for windowing, save storage, input, audio, achievements, and cloud save without rewriting launch rules or progression.
