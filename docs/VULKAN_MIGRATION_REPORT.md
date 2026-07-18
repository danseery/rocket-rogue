# Direct Vulkan 1.3 migration report

## Outcome

OREBIT's Windows and Linux applications now use a direct Vulkan 1.3 renderer with no native OpenGL fallback. The browser remains WebGL2 and consumes the same backend-neutral scene packets. Native startup rejects devices that do not provide Vulkan 1.3, graphics and present support on one queue, Dynamic Rendering, Synchronization2, timeline semaphores, and a supported UNORM presentation format.

The migration does not add gameplay or save fields. Frame-limit preferences are platform presentation state only. Deterministic gameplay, UI behavior, and web exports remain shared with the pre-migration game.

## Implemented architecture

- `SceneComposer` converts synchronous `RenderSnapshot` views into immutable frame-lifetime `ScenePacket` data with explicit pipeline, blend, coordinate-space, atlas-page, and ordered draw state.
- Rectangles, sprites, polygon circles, radial glows, and uniform-color wide lines use 28-byte packed instances expanded from a six-vertex unit quad. Gradient lines retain an ordered triangle fallback. No wide-line device feature is required.
- Mining terrain has a presentation revision and persistent per-frame-slot GPU streams. Stable terrain is not uploaded again.
- A generated, padded two-page scene atlas replaces per-asset scene textures. Logical texture identity remains backend-neutral; resolved atlas page is explicit render state so adjacent compatible sprites can batch without sorting transparency.
- `VulkanGraphicsBackend` owns instance/device selection, the SDL Vulkan surface, FIFO swapchain, two frames in flight, per-image presentation semaphores, persistently mapped frame rings, timestamp queries, VMA resources, deferred retirement, and a device/driver/shader-ABI pipeline cache.
- `VulkanRmlRenderHost` shares the scene device, swapchain image, command buffer, descriptor allocation, and synchronization. RmlUi renders after the scene with premultiplied-alpha blending and dynamic scissors.
- GLSL 450 is compiled offline into six packaged SPIR-V modules. Native packages contain no runtime shader compiler, shader source, OpenGL import, or Vulkan loader.
- Steamworks support is optional. When configured, Steam initializes before Vulkan and `ISteamUtils::IsSteamRunningOnSteamDeck()` supplies the Deck default. Builds without the SDK have no Steam dependency.

## Lifecycle, pacing, and failure behavior

- FIFO presentation and frame fences bound CPU/GPU work-ahead. `PlatformDefault` resolves to 60 FPS on a detected Steam Deck and display refresh elsewhere; Smooth 60, refresh-compatible Balanced 40/45, Battery 30, and Display modes are presentation preferences only.
- Resize, fullscreen, surface change, out-of-date/suboptimal acquisition, surface loss, and zero extent are handled outside steady-state rendering. Exceptional swapchain or surface recreation may wait for in-flight frames and the presentation queue; the ordinary frame loop never calls queue or device idle.
- Hidden or minimized windows perform no acquire, scene render, UI render, or present work and wait on SDL events at a low cadence. Resume resets the frame clock so simulation cannot catch up a suspended wall-clock interval.
- Device loss produces a clear fatal diagnostic and exits cleanly. Previously completed save writes remain atomic; live device recovery is intentionally deferred.

## Correctness evidence

- Windows Release: 17/17 native tests passed.
- Windows Debug: 17/17 native tests passed.
- Browser: 9/9 WebGL2 tests passed.
- Atlas generator: 5/5 tests passed; committed metadata, layout, generated header, and decoded pixels reproduce exactly.
- Vulkan shaders: all six optimized Vulkan 1.3 SPIR-V modules regenerate byte-for-byte and pass validation.
- Exact-source Windows Debug Mining smoke: Khronos validation and synchronization validation reported zero warnings or errors after 180 presented frames.
- Windows package: contains the executable, two atlas pages and manifest, six SPIR-V modules, and dependency notices. Its executable is byte-identical to the final Release build, imports neither OpenGL nor a Vulkan loader, and starts successfully from an unrelated working directory.
- Linux Release and Debug builds completed in Ubuntu, 17/17 tests passed, and the ELF has no GL, EGL, GLES, or direct Vulkan-loader dependency. A 30-frame Xvfb/lavapipe smoke reported zero validation and synchronization-validation warnings or errors. Lavapipe is correctness-only and supplies no performance result.

Tests cover scene ordering/batching, packed layouts, atlas mapping, line/polygon parity, persistent terrain revisions, frame-lifetime views and enemy indices, Vulkan device/present policies, frame retirement, pacing choices, benchmark isolation, screenshot canonicalization, suspend clock reset, incremental HUD updates, and dirty mining-gate resolution.

## Frozen OpenGL baseline and completion benchmark

The disposable OpenGL harness is derived from exact pre-migration commit `e24619ae11720e0c6ddb61b33bffc5371fd9c5e5`. It is not included in a native package. Accepted timing results use three uncontaminated 60-second runs per renderer and resolution after the common 10-second warm-up. The machine was a Windows 11 Razer Blade 15 with an NVIDIA GeForce RTX 3080 Ti Laptop GPU, driver 581.57, 32 GB RAM, AC power, High performance power plan, and a 59.951 Hz display. Browser GPU work, builds, WSL, and benchmark processes were stopped before capture.

| Mining, seed `0x0BEB17` | OpenGL baseline | Vulkan 1.3 | Change |
|---|---:|---:|---:|
| 1280x800 CPU p95 | 2.757 ms | 1.336 ms | -51.5% |
| 1280x800 GPU p95 | 2.955 ms | 0.393 ms | -86.7% |
| 1280x800 scene submissions p50 | 67 | 5 | -92.5% |
| 1280x800 dynamic upload p50 | 393,536 bytes | 59,808 bytes | -84.8% |
| 2560x1440 CPU p95 | 2.650 ms | 1.361 ms | -48.6% |
| 2560x1440 GPU p95 | 3.151 ms | 0.978 ms | -69.0% |

All six accepted Vulkan runs recorded 3,600/3,600 valid samples, zero timed pipeline events, and the same `0x314c039b93912919` to `0xed80b34271fcbe69` deterministic gameplay hashes as OpenGL. The accepted Vulkan sets recorded 4/10,800 queue/present deadline misses at 1280x800 and 2/10,800 at 2560x1440. The first 2560x1440 attempt was preserved separately and rejected before aggregation because a transient approximately 30 FPS queue/present-return pattern produced 1,582 misses; three subsequent runs met the capture ceiling.

The accepted queue/present-return p99 values were 16.972 ms at 1280x800 and 16.964 ms at 2560x1440, slightly above the 16.680 ms target interval. Queue-present return cadence is a CPU-side pacing diagnostic and is not proof of scanout timing, so the strict 99% displayed-frame criterion is not claimed from these numbers. It still requires PresentMon, supported presentation-timing extensions, or an equivalent external capture.

## Acceptance still requiring hardware

- Review and approve golden images for Title, Hangar, Launch, Flyby, Orbit, Surface Ops, and fixed-seed Mining with a documented perceptual threshold.
- Repeat Windows and Linux performance captures on representative physical GPUs at 1080p, high-DPI/4K, and high refresh; use external scanout timing for the displayed-frame criterion.
- Runtime-test optional Steamworks linking and the packaged `$ORIGIN` lookup when SDK access is available. The no-SDK build and CMake/package path are validated, but an SDK-linked Linux package was not available locally.
- On a physical Steam Deck, capture 1280x800 Smooth 60, Balanced 40/45, and Battery 30 modes, then run a 60-minute soak and measure whole-device power at matched settings.
- Do not claim a battery-life improvement or projected play time until the matched power measurements exist.
