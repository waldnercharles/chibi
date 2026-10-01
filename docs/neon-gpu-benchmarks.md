# NeonSwarm GPU benchmarks and captures

The first measured renderer optimization is documented in
[compact HRC radiance buffers](neon-hrc-optimization.md).

The benchmark tool copies NeonSwarm, applies stress fixtures through Chibi MCP,
and preserves each resulting project beside its request, image and measurements.
It never changes the playable project's tuning. HRC adaptation is disabled and
both scale and directly traced levels are recorded.

## Run the suite

```sh
make build-release
python3 tools/benchmark_neon_materials.py --scenario all --output /tmp/neon-1080
python3 tools/benchmark_neon_materials.py --scenario all --mode live --resolution .5 \
  --width 1280 --height 800 --output /tmp/neon-800
```

The first command measures all five scenarios in live and frozen modes at HRC
scales 0.5 and 1.0. Use a fresh output directory for each experiment. Runs are
sequential; do not run another benchmark, build, or game alongside them.
`--repeat 3` records independent repetitions. The default directly traces the
first three cascades, matching Sandbox. Use `--trace-levels 1` to compare the
one-level tracing configuration.

| Scenario | Workload |
| --- | --- |
| `travel` | Existing rightward flight and sustained fire. |
| `dense-fog` | A broad additional cloud, with over 10,000 active parcels required at the endpoint. |
| `ignition` | Three finite fuel clouds and fixed ignition sources; at least 20 burning parcels required at both endpoints. |
| `destruction` | Thin destructible pillars cut by normal shots; at least eight detached fragments required at the endpoint. |
| `camera-turns` | Flight and firing with direction changes every half second, including changes during measured rendering. |

`--scenario physics-all --render-frames 240` renders the four
[physics/material scenarios](research/physics-material-benchmarks.md): projectile
barrages into terrain and enemies, a destructible crowd, and interacting fog/liquid.
These use fixture-specific preparation frames: 60 normally and 120 for the
default 20,000-enemy crowd. They share the CPU runner's 240-frame input window.
`all` continues to select the five scenarios above.

These are controlled stress fixtures. Fuel amount and obstacle layout differ
from normal play. In particular, the ignition sources deliberately maintain a
busy burning region; the scenario is not an average combat measurement.

## What the timings mean

Live mode prepares the scenario at a fixed step, warms the renderer for 240
frames with simulation held still, then advances and renders 180 frames.
The first 16 advancing frames are discarded, leaving 164 measurements. Simulation
is fixed at 60 Hz and pacing sleep is outside the measured interval. The p95 and
p99 use nearest-rank percentiles. Very short runs are for validation, not budgets.

Frozen mode prepares the same endpoint as live mode, then warms and renders
that snapshot without advancing simulation or camera time. Lighting history
still converges. Before and after state must match, and simulation time must
measure zero. This isolates a stable rendering workload; it does not test moving
shadows or camera reprojection. Live mode covers those changes.

Each rendered endpoint is checked against an independent headless run. Material
state must also match across scales, modes and repeats. World hashes include
authored HRC configuration, so changing scale intentionally changes that hash.
The tool reports state from the process that actually rendered the image.

Render/present is host elapsed time including submission and GPU waiting. It is
not a GPU shader duration. GPU-complete scene rendering is synchronized before
presentation, matching the existing benchmark boundary. Startup, shader compilation,
fixture authoring, state inspection, readback and pacing are outside measurement.

The output includes:

- `summary.md` and `summary.json` with all measured settings and results.
- `comparisons.json` with frozen RGB image differences against scale 1.0 when
  that reference was requested. These report 8-bit channel differences, not
  perceptual quality or physical accuracy. Inspect the referenced images too.
- A directory per case containing `project/`, `request.json`, `launch.json`,
  `manifest.json`, `report.json`, `snapshot.json`, `frame.png` and logs.
- Binary, engine shader and copied-project fingerprints, Git revision/dirty
  status, OS and locally available GPU information.

The snapshot is reconstructed by deterministic replay. It is not a serialized
solver checkpoint that can be transferred across arbitrary engine revisions.

## Time individual passes

Add `"pass_timing": true` to a `preview_scene` request (`chibi_ctl --render-request`)
for average GPU milliseconds per marked pass; `"pass_timing_detail": true` also
splits every HRC cascade level. On Metal the times come from command-buffer
timestamps without stalling the frame; elsewhere each mark waits for the GPU.
See [the timestamp method](neon-hrc-optimization.md#per-pass-gpu-time).

## Record a Metal frame

```sh
python3 tools/benchmark_neon_materials.py --scenario ignition --mode frozen \
  --resolution .5 --metal-capture --output /tmp/neon-metal-ignition
python3 tools/benchmark_neon_materials.py --scenario travel --mode frozen \
  --resolution 1 --metal-capture --output /tmp/neon-metal-full
```

The tool enables `MTL_CAPTURE_ENABLED=1` only in the capture process. It records
one frame after warmup, at rendered frame index 16, into `frame.gputrace`.
Capture timing includes instrumentation and file-writing overhead; it is marked
as instrumented and must not be compared with ordinary benchmark timings.
Metal and Tracy captures run separately.

The installed Metal runtime successfully writes trace files on this single-GPU
Apple Silicon Mac even without the Xcode application. Full Xcode is needed for
the Metal debugger and counter analysis. Open the `.gputrace` in Xcode and inspect
material tile binning/rasterization, fluid density raster, HRC trace/extension/
merge, gather, filtering, bounce, composite and planar reflections.

The capture bridge is linked only into `chibi_ctl`. It uses the same default
Metal device factory as the pinned SDL backend and currently requires one Metal
GPU. Multi-GPU hosts must use Xcode's device selection until the renderer exposes
explicit native-device access. Other platforms return an unsupported-capture
error; their benchmarks and frozen previews still work.

This follows Apple's [programmatic capture workflow](https://developer.apple.com/documentation/xcode/capturing-a-metal-workload-programmatically).
The capture begins before the frame acquires its command buffer and ends after
submission. It does not record initialization or all warmup frames.

## Replay or open under a debugger

```sh
/tmp/neon-1080/travel-frozen-1-1/replay.sh
python3 tools/benchmark_neon_materials.py \
  --replay /tmp/neon-1080/travel-frozen-1-1 --allow-renderer-changes
```

Replay checks the saved project's fingerprint and, by default, the binary and
engine shaders. The explicit override permits intentional renderer comparisons;
the resulting simulation state must still match the saved report. Each replay
gets a new directory, preserving the original report, image and trace. A changed
simulation requires a new baseline.

`--prepare-only` creates the bundle and launch files without rendering. In Xcode,
use Debug Executable with the binary and arguments from `launch.json`.
`chibi_ctl --render-request FILE` reads the request directly, so the debugger does
not need redirected stdin. Use the recorded environment for programmatic capture.
Replayed captures require a fresh trace path, which `replay.sh` supplies.

On Steam Deck, use the same fixed benchmark request with Vulkan and obtain a
native RADV/Radeon GPU Profiler capture. RenderDoc can inspect dispatches and
resources. No Deck performance claim follows from matching its display resolution
on this Mac. See [profiling tools](tracy.md#gpu-pass-profiling).

## Regression checks

`make test` includes an independent noncombat MCP scene. An event scheduled after
preparation must execute during live rendering and match headless replay; frozen
rendering of that endpoint must retain the complete state. Invalid capture paths,
orphan capture-frame settings and future inputs in frozen previews are rejected.
The full NeonSwarm suite additionally checks actual gas, burning and debris loads.

Per-pass command-buffer timestamps now attribute GPU time without Xcode; GPU
counters would still be needed to separate bandwidth, arithmetic and occupancy.
The trace files establish reproducible input for that analysis; their existence
is not evidence of a particular GPU bottleneck.

## Local baseline: September 19, 2026

Apple M4 Pro, 20 GPU cores, macOS 26.7, Metal, Release build. One sequential run
per case, with 164 measured frames and one directly traced HRC level. These are
short local workload samples, not repeated-trial confidence bounds or Deck results.

| Scenario | 1080p scale 0.5 median / p95 | 1080p scale 1 median / p95 | 1280×800 scale 0.5 p95 |
| --- | --- | --- | --- |
| Travel | 12.33 / 12.72 ms | 32.35 / 33.43 ms | 9.32 ms |
| Dense fog | 13.76 / 14.33 ms | 33.69 / 34.16 ms | 9.90 ms |
| Ignition | 14.26 / 14.91 ms | 34.17 / 34.94 ms | 11.09 ms |
| Destruction | 11.39 / 12.00 ms | 31.37 / 32.06 ms | 8.11 ms |
| Camera turns | 11.76 / 18.82 ms | 31.54 / 38.52 ms | 14.82 ms |

All table values use live simulation. The camera-turn workload exceeds the
16.67 ms frame budget at p95 even with half-scale HRC. Its frozen endpoint has
p95 9.87 ms at scale 0.5, so a stable snapshot alone misses this tail. The workload
includes shots, burning and destruction; camera movement is not an isolated cause.

Frozen scale-0.5 images were compared with their scale-1 counterparts. Across
the five scenarios, mean absolute channel differences ranged from 0.018 to
0.573 on an 8-bit scale. At most 0.69% of pixels differed by more than three in
any RGB channel; the largest channel difference was 23. Dark backgrounds occupy
much of these images, so these global statistics cannot establish equivalent
lighting quality at bright edges or during motion.

Artifacts from this run are in `/tmp/neon-benchmark-suite/1080p` and
`/tmp/neon-benchmark-suite/800p`. Actual one-frame Metal traces are in
`metal-ignition/ignition-frozen-0.5-1/frame.gputrace` and
`metal-full/travel-frozen-1-1/frame.gputrace` under that same root. Both contain
the material and HRC pass labels. Capture instrumentation and timings are excluded
from the table. Xcode counter analysis is still pending.

### Camera-turn tail diagnosis

Two separate Tracy captures reproduced the tail. The second adds a
`Material blast effects` scope around blast impulses, occlusion and terrain damage.
Among the last 164 advancing frames, ten exceeded 16.67 ms. Scopes below are
inclusive host durations; nested rows must not be added together.

| Scope | Mean in the ten slow frames | Mean in the other 154 frames |
| --- | --- | --- |
| Preview simulation | 9.980 ms | 1.564 ms |
| Material blast effects | 8.428 ms | 0.095 ms |
| Gas flow | 0.325 ms | 0.337 ms |
| Material collection | 0.144 ms | 0.129 ms |
| Present / GPU wait | 9.353 ms | 9.321 ms |

Blast handling accounts for almost all the additional simulation time. Its
largest measured call was 10.918 ms. The current code scans obstacles for blast
occlusion and repeatedly scans material records along terrain-damage spokes.
The next CPU experiment should reduce those candidate scans using spatial bounds
and cached transforms, while retaining blast ordering and matching the saved
material state. The current capture identifies the overall blast path; it does
not yet separate impulse/occlusion cost from terrain-spoke cost.

The capture and per-frame analysis are in
`/tmp/neon-benchmark-suite/tracy-camera-blasts`. The analysis matches the last 164
simulation intervals to their enclosing render frames and stops the work interval
at the end of `Preview present / GPU wait`, excluding pacing. Worker scopes are
not summed into elapsed time. These instrumented values explain the release
tail; they are not substituted into the release table.

Release, Debug and Tracy builds succeeded. `make test` passed, including the
rendered-input and frozen-state regressions. All 25 ordinary benchmark cases
matched their headless endpoints; both Metal files report one captured frame.
Guarded replay preserved the frozen world/material state, and tampered binary or
project fingerprints were rejected before rendering. Rebuilding the executable
changes its fingerprint; use the explicit renderer-change override when comparing
that new build with these saved bundles.
