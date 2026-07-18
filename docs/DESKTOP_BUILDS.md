# Native Desktop Builds

Rocket Rogue ships genuine x86-64 Windows and Linux applications. The native executable runs the shared C++ application directly through SDL 3 and Vulkan 1.3; it does not embed Chromium, Electron, Node, JavaScript, WASM, a browser shell, or an OpenGL fallback. The Emscripten/WebGL2 build remains a separate, supported target.

## Runtime architecture

- `rocket_core` contains portable game rules, progression, saves, and controller routing.
- `rocket_app` contains `RocketGameApp`, `GameRunner`, `GamePanel`, RmlUi behavior, render snapshots, backend-neutral `SceneComposer`/`ScenePacket` generation, and the native Vulkan backend.
- `src/platform/sdl` owns the native window, Vulkan surface handoff, event loop, high-DPI metrics, keyboard/mouse input, gamepad lifecycle, rumble, fullscreen, native files, and PNG decoding.
- `src/platform/web` owns Emscripten, browser storage, DOM/localStorage integration, web image loading, and the browser entry point.
- `AppServices` injects save, preference, host, controller, texture, renderer, UI, and optional DOM-mirror services into the shared application.

`SceneComposer` converts each synchronous render snapshot into ordered, triangle-only scene commands without deciding gameplay outcomes. Native rendering consumes those commands through a direct Vulkan 1.3 backend and a custom Vulkan RmlUi host that share one device, swapchain image, command buffer, and synchronization. The web backend consumes the same scene data through WebGL2 and keeps GLSL ES shaders. Native GLSL 450 is compiled offline and only generated SPIR-V is loaded at runtime.

Native startup requires Vulkan 1.3 plus dynamic rendering, Synchronization2, timeline semaphores, graphics/present support, and a supported UNORM swapchain format. Unsupported hardware receives a startup diagnostic; there is no OpenGL fallback. Vulkan-Headers, Volk, and Vulkan Memory Allocator are pinned source dependencies, while the Vulkan loader is supplied by the GPU driver or Steam Runtime.

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

Release builds use the static MSVC runtime. The archive contains `RocketRogue.exe`, `assets` (including generated SPIR-V), and dependency `licenses`; it does not contain Electron, Chromium, Node, WASM, the browser shell, an OpenGL fallback, or a Vulkan loader DLL.

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

The native release workflow builds Linux in a pinned Steam Runtime 4 SDK container, checks `ldd`, verifies committed SPIR-V, performs an unrelated-working-directory Vulkan startup smoke under Xvfb/lavapipe with validation and synchronization validation, splits debug symbols, and uploads the archive and symbols separately. Lavapipe is correctness-only and never supplies performance numbers. This provides a Valve-compatible Linux baseline without linking Steamworks.

## Window, input, and saves

The initial window is resizable, high-DPI aware, windowed, and 1280x800. Fullscreen is controlled by the in-game setting, F11, or Alt+Enter; all three update and persist the same preference. SDL supplies logical and pixel dimensions so RmlUi and the Vulkan swapchain respond correctly to resize and DPI changes.

SDL gamepads are converted to the shared `RawControllerSnapshot`, then routed through `ControllerTracker`, `InputSourceArbiter`, and `GameInputRouter`. This preserves holds, active-device prompt families, disconnect pausing, haptic cues, and input reset during disconnect/shutdown. No Steam Input API is present.

Native saves start fresh and do not import browser or legacy desktop-wrapper storage. SDL chooses the per-user preference directory. It contains:

- `save_v1.txt`
- `preferences_v1.txt`

Writes use a temporary file followed by replacement. A failed save keeps the prior file, logs the detailed error, and shows a concise in-game failure message.

Assets are resolved relative to the executable rather than the process working directory. Missing or corrupt required PNG files produce a startup error that identifies the asset.

## Shader workflow and package contents

Install a Vulkan SDK, or distro packages providing `glslc` and `spirv-val`, only when editing shaders. Regenerate and verify the native shader modules with:

```powershell
npm.cmd run shaders:vulkan
npm.cmd run check:shaders:vulkan
```

The package must contain `scene.vert.spv`, `scene.frag.spv`, `scene_instance.vert.spv`, `scene_instance.frag.spv`, `rml_ui.vert.spv`, and `rml_ui.frag.spv` under `assets/shaders`, plus separate notices for Vulkan-Headers, Volk, and Vulkan Memory Allocator. Do not package `vulkan-1.dll`, `libvulkan.so`, a runtime GLSL compiler, or a second RmlUi renderer/device.

## Verification

Local and CI verification uses:

```powershell
npm.cmd run test:native
npm.cmd run test:native:release
npm.cmd run build:web
npm.cmd run test:web
```

The native suites cover portable gameplay, controller routing, browser-free fake services, scene packet ordering and batching, Vulkan device/present policies, frame pacing, benchmark isolation, atomic saves, preference round trips, fullscreen and high-DPI metrics, haptics, shutdown, missing/corrupt assets, and a source-boundary check that confines browser/Emscripten symbols to the web adapter.

The legacy Electron wrapper was retired after native Windows and Linux parity passed. Desktop releases now contain only the native executable, assets, and licenses; the independent Emscripten web build remains fully supported.
