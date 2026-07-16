# Native Desktop Builds

Rocket Rogue ships genuine x86-64 Windows and Linux applications. The native executable runs the shared C++ application directly through SDL 3 and OpenGL 3.3 Core; it does not embed Chromium, Electron, Node, JavaScript, HTML, or WASM. The Emscripten/WebGL2 build remains a separate, supported target.

## Runtime architecture

- `rocket_core` contains portable game rules, progression, saves, and controller routing.
- `rocket_app` contains `RocketGameApp`, `GameRunner`, `GamePanel`, RmlUi behavior, render snapshots, and shared OpenGL drawing.
- `src/platform/sdl` owns the native window, OpenGL context, event loop, high-DPI metrics, keyboard/mouse input, gamepad lifecycle, rumble, fullscreen, native files, and PNG decoding.
- `src/platform/web` owns Emscripten, browser storage, DOM/localStorage integration, web image loading, and the browser entry point.
- `AppServices` injects save, preference, host, controller, texture, renderer, UI, and optional DOM-mirror services into the shared application.

OpenGL context creation is outside `OpenGlRenderer`. Native shaders use GLSL 330 Core; web shaders use GLSL ES 300. This keeps the platform and renderer seams available for later Steam and Vulkan work without adding either API now.

## Windows x86-64

From PowerShell:

```powershell
npm.cmd run configure:native:release
npm.cmd run build:native:release
npm.cmd run test:native:release
npm.cmd run package:native
```

Outputs:

- `build/native-release/bin/RocketRogue.exe` - unpacked Release executable.
- `build/native-release/bin/assets` - unpacked runtime assets.
- `build/native-release/bin/RocketRogue.pdb` - separate debug symbols.
- `build/packages/RocketRogue-windows-x64.zip` - portable release archive.

Release builds use the static MSVC runtime. The archive contains `RocketRogue.exe`, `assets`, and `licenses`; it does not contain Electron, Chromium, Node, WASM, or the browser shell.

## Linux x86-64

On Ubuntu or WSL Ubuntu:

```bash
./scripts/install-ubuntu.sh
source scripts/env-ubuntu.sh
npm run configure:native:release
npm run build:native:release
npm run test:native:release
npm run package:native
```

Outputs:

- `build/native-release/bin/RocketRogue` - unpacked Release executable.
- `build/native-release/bin/assets` - unpacked runtime assets.
- `build/packages/RocketRogue-linux-x64.tar.gz` - portable release archive.

The native release workflow builds Linux in a pinned Steam Runtime 4 SDK container, checks `ldd`, performs an unrelated-working-directory startup smoke under Xvfb/Mesa, splits debug symbols, and uploads the archive and symbols separately. This provides a Valve-compatible Linux baseline without linking Steamworks.

## Window, input, and saves

The initial window is resizable, high-DPI aware, windowed, and 1280x800. Fullscreen is controlled by the in-game setting, F11, or Alt+Enter; all three update and persist the same preference. SDL supplies logical and drawable dimensions so RmlUi and OpenGL respond correctly to resize and DPI changes.

SDL gamepads are converted to the shared `RawControllerSnapshot`, then routed through `ControllerTracker`, `InputSourceArbiter`, and `GameInputRouter`. This preserves holds, active-device prompt families, disconnect pausing, haptic cues, and input reset during disconnect/shutdown. No Steam Input API is present.

Native saves start fresh and do not import browser or legacy desktop-wrapper storage. SDL chooses the per-user preference directory. It contains:

- `save_v1.txt`
- `preferences_v1.txt`

Writes use a temporary file followed by replacement. A failed save keeps the prior file, logs the detailed error, and shows a concise in-game failure message.

Assets are resolved relative to the executable rather than the process working directory. Missing or corrupt required PNG files produce a startup error that identifies the asset.

## Verification

Local and CI verification uses:

```powershell
npm.cmd run test:native
npm.cmd run test:native:release
npm.cmd run build:web
npm.cmd run test:web
```

The native suites cover portable gameplay, controller routing, browser-free fake services, atomic saves, preference round trips, fullscreen and high-DPI metrics, haptics, shutdown, missing/corrupt assets, and a source-boundary check that confines browser/Emscripten symbols to the web adapter.

The legacy Electron wrapper was retired after native Windows and Linux parity passed. Desktop releases now contain only the native executable, assets, and licenses; the independent Emscripten web build remains fully supported.
