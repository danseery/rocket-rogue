# Native performance benchmarks

OREBIT's native benchmark mode runs deterministic, save-suppressed gameplay scenarios with an isolated preference/save directory and writes a versioned JSON report. Native release packages use Vulkan 1.3; the browser remains WebGL2. Linux lavapipe is correctness-only and must never be used for performance acceptance.

## Build

```powershell
npm.cmd run build:native:release
```

Use `build/native-release/bin/RocketRogue.exe` for timing. The `native-profile` preset keeps optimized code, symbols, and frame pointers for profiler investigations, but final comparisons use `native-release`.

## One capture

```powershell
build\native-release\bin\RocketRogue.exe `
  --benchmark-scenario mining `
  --benchmark-seed 0x0BEB17 `
  --benchmark-warmup-seconds 10 `
  --benchmark-frame-count 3600 `
  --benchmark-renderer vulkan `
  --benchmark-frame-limit smooth60 `
  --benchmark-resolution 1280x800 `
  --benchmark-json build\benchmarks\vulkan-mining-1280x800-run1.json `
  --benchmark-screenshot build\benchmarks\vulkan-mining-1280x800-run1.png `
  --benchmark-profile-dir build\benchmark-profiles\vulkan-mining-1280x800-run1
```

The benchmark refuses to run if its profile directory equals, contains, or is contained by the real player-profile directory. It also disables the performance overlay during capture so publishing the overlay cannot contaminate samples. Benchmark mode consumes neutral keyboard, mouse, and controller input and does not depend on the window manager granting foreground focus; this exception is confined to the save-isolated CLI. Hidden or minimized benchmark windows still suspend acquisition, rendering, presentation, and sample counters until restored.

Available scenarios are `title`, `hangar`, `launch`, `flyby`, `orbit`, `surface-ops`, and `mining`. Use either `--benchmark-duration-seconds` for exploratory performance captures or `--benchmark-frame-count` for repeatable A/B comparisons, never both. Frame-count mode freezes one exact simulation interval when the benchmark starts and converts the warm-up duration to a fixed frame count. Reports record that deterministic cadence as `simulationFramesPerSecond` separately from the refresh-compatible `targetFramesPerSecond` used for physical presentation. A skipped acquire or present invalidates the run and publishes no partial report because retrying after simulation advances would change the workload. This keeps equivalent renderer runs on identical gameplay hashes without changing normal gameplay timing. Run `RocketRogue.exe --help` for the complete contract.

`--benchmark-frame-limit` accepts `platform-default`, `smooth60`, `balanced`, `battery30`, or `display`; benchmark mode defaults to `smooth60`. Use the explicit balanced and battery values for the corresponding Steam Deck captures instead of relying on whatever an isolated profile previously contained.

`--benchmark-screenshot <path.png>` is optional. It performs one Vulkan swapchain readback only after timed sampling is complete, renders that image without another simulation tick, canonicalizes BGRA/RGBA surfaces to tightly packed RGBA8, and writes the PNG atomically. The benchmark's final gameplay hash is sampled before the extra render and verified again afterwards. Ordinary shipping frames do not allocate a readback buffer or issue an image copy. A platform whose present surface does not expose transfer-source usage reports the capture as unsupported without weakening the shipping Vulkan requirements.

The screenshot mechanism provides reproducible resolution, scenario setup, pixel layout, and capture timing relative to the benchmark. It does not by itself approve a golden image: presentation and CSS animations may still be at different phases in wall-clock benchmark runs. Capture Title, Hangar, Launch, Flyby, Orbit, Surface Ops, and fixed-seed Mining with their matching scenario names on an approved machine, review those PNGs visually, then adopt them as references and compare fresh captures with a documented perceptual threshold. Keep the approved references and diff policy out of this performance report until that human acceptance pass is complete.

Each report contains CPU/GPU/limiter distributions, queue-present return cadence and deadline misses, scene submissions, uploaded bytes, pipeline events, device memory, hardware identity, active refresh, and canonical initial/final gameplay-state hashes. CPU work excludes time deliberately blocked on frame retirement, FIFO swapchain acquisition, or an explicit software deadline; that time is reported as limiter/idle so Vulkan FIFO and OpenGL VSync runs remain comparable. Pipeline creation and texture initialization finish before the timed capture. Queue-present return cadence is a CPU-side pacing diagnostic, not proof of scanout timing; the 99% displayed-frame acceptance criterion requires runtime-supported presentation timing or an external frame-capture tool.

## Summarize a run set

After collecting at least three equivalent schema-v2 captures, aggregate their run-level percentiles instead of pooling individual frames:

```powershell
npm.cmd run summarize:benchmarks -- `
  --json-output build\benchmarks\vulkan-mining-1280x800-aggregate.json `
  build\benchmarks\vulkan-mining-1280x800-run1.json `
  build\benchmarks\vulkan-mining-1280x800-run2.json `
  build\benchmarks\vulkan-mining-1280x800-run3.json
```

The tool refuses mixed schema versions, scenarios, renderers, platforms, GPUs, resolutions, seeds, simulation or presentation frame rates, active refresh rates, present-interval sources, or gameplay-state hashes. Early schema-v2 OpenGL baselines without the explicit simulation field are normalized to their original target rate. The tool prints a compact table and optionally writes a machine-readable aggregate containing the median of the run-level p50, p95, and p99 for CPU, GPU, queue/present return, limiter idle, scene draws, uploaded bytes, and pipeline events. For fixed-frame reports, the nominal capture length is submitted frames divided by the target frame rate. Warnings call out short warm-ups or captures, rejected samples, deadline misses above 1%, timed pipeline events, and p99 values above the selected frame budget. Draw/upload reductions and displayed scanout timing still require their explicitly recorded baseline or external timing evidence.

## Acceptance matrix

For each GPU, capture three runs after warm-up at:

- 1280x800 at the Smooth 60 mode;
- 1920x1080;
- the representative high-DPI or 4K mode;
- a high-refresh desktop mode when available.

Use the median of the three run-level p50, p95, and p99 values. Close overlays, recording software, browsers with GPU-heavy tabs, and update/install processes. Keep power mode, display refresh, fullscreen state, driver, and background workload constant. Record hardware, OS, driver, active refresh, power mode, and whether the machine was on AC power.

Desktop completion requires:

- identical deterministic gameplay hashes for equivalent fixed-step captures;
- accepted image parity and zero Vulkan validation warnings/errors;
- at least 99% of capped 60 FPS frames within 16.67 ms;
- no uncontrolled work above the selected cap and no timed pipeline creation;
- at least 50% fewer Mining scene submissions and dynamic uploaded bytes than the frozen OpenGL baseline;
- Vulkan native p95 no worse than OpenGL by more than 5%.

Physical Steam Deck acceptance is separate: repeat 1280x800 at Smooth 60, Balanced 40/45, and Battery 30, then run a 60-minute soak. Measure whole-device power at matched settings. Do not claim battery-life improvement from renderer timing alone.

## Frozen OpenGL baseline

The native OpenGL backend is not part of shipping packages. If a preserved pre-migration baseline executable is available, run it only from its frozen build directory and label results `opengl`. The Vulkan-only executable deliberately rejects `--benchmark-renderer=opengl`.
