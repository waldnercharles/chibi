# Neon Swarm GPU benchmarks and captures

`tools/benchmark_neon_materials.py` copies Neon Swarm (`projects/arena2d`),
applies stress fixtures through Chibi MCP, and preserves each resulting project
beside its request, image and measurements. It never changes the playable
project's tuning. HRC adaptation is disabled; resolution, trace levels, gather
resolution and lighting margin are pinned and recorded. The HRC lab has its own
runner; see [HRC lighting](hrc-lab.md#run-the-lab).

## Run the suite

```sh
make build-release
python3 tools/benchmark_neon_materials.py --scenario all --output /tmp/neon-1080
python3 tools/benchmark_neon_materials.py --scenario all --mode live --resolution .5 \
  --width 1280 --height 800 --output /tmp/neon-800
```

The first command measures all five scenarios in live and frozen modes at HRC
resolutions 0.5 and 1. Use a fresh output directory for each experiment. Runs
are sequential; do not run another benchmark, build, or game alongside them.
`--repeat 3` records independent repetitions. Defaults are three directly
traced levels, `--gather-resolution Full` and `--lighting-margin 64`; pass
`--lighting-margin 0` to match Sandbox.

| Scenario | Workload |
| --- | --- |
| `travel` | Rightward flight and sustained fire. |
| `dense-fog` | A broad additional cloud, with over 10,000 active parcels required at the endpoint. |
| `ignition` | Three finite fuel clouds and fixed ignition sources; at least 20 burning parcels required at both endpoints. |
| `destruction` | Thin destructible pillars cut by normal shots; at least eight detached fragments required at the endpoint. |
| `camera-turns` | Flight and firing with direction changes every half second, including changes during measured rendering. |

These are controlled stress fixtures. Fuel amount and obstacle layout differ
from normal play; the ignition sources deliberately keep a busy burning region.

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
shadows or camera reprojection.

Each rendered endpoint is checked against an independent headless run. Material
state must also match across resolutions, modes and repeats. World hashes include
authored HRC configuration, so changing resolution intentionally changes that
hash.

Render/present is host elapsed time including submission and GPU waiting, not a
shader duration. Startup, shader compilation, fixture authoring, state
inspection, readback and pacing are outside measurement.

The output includes:

- `summary.md` and `summary.json` with all measured settings and results.
- `comparisons.json` with frozen RGB image differences against resolution 1 when
  that reference was requested. These are 8-bit channel differences, not
  perceptual quality; inspect the images too.
- A directory per case containing `project/`, `request.json`, `launch.json`,
  `manifest.json`, `report.json`, `snapshot.json`, `frame.png`, `replay.sh` and
  logs.
- Binary, engine shader and copied-project fingerprints, Git revision/dirty
  status, OS and locally available GPU information.

The snapshot is reconstructed by deterministic replay, not a serialized solver
checkpoint that transfers across engine revisions.

## Time individual passes

Add `"pass_timing": true` to a `preview_scene` request (`chibi_ctl --render-request`)
for average GPU milliseconds per marked pass; `"pass_timing_detail": true` also
splits every HRC cascade level. On Metal, `chibi_ctl` installs a command-buffer
clock (`src/render/gpu_clock.m`): each mark submits without waiting, and a pass
is charged only the part of its interval after all earlier GPU work, so overlap
is not counted twice. Unmarked work such as UI accumulates as `Unmarked`. Other
backends wait for the GPU at each mark. The response's `pass_timing_source`
names the method. Each mark adds a command-buffer boundary, so compare timed
runs only with other timed runs.

## Record a Metal frame

```sh
python3 tools/benchmark_neon_materials.py --scenario ignition --mode frozen \
  --resolution .5 --metal-capture --output /tmp/neon-metal-ignition
```

The tool enables `MTL_CAPTURE_ENABLED=1` only in the capture process and
records rendered frame 16 into `frame.gputrace`, following Apple's
[programmatic capture workflow](https://developer.apple.com/documentation/xcode/capturing-a-metal-workload-programmatically).
Capture timing includes instrumentation and must not be compared with ordinary
benchmark timings. Metal and Tracy captures run separately. Full Xcode is needed
for the Metal debugger and counter analysis.

The capture bridge is linked only into `chibi_ctl` and requires one Metal GPU.
Other platforms return an unsupported-capture error; their benchmarks and frozen
previews still work.

## Profile the CPU with Tracy

```sh
make build-tracy
python3 tools/benchmark_neon_materials.py --build build-tracy --mode live \
  --resolution .5 --tracy-capture build-tracy/capture/tracy-capture \
  --output /tmp/neon-tracy
```

For zone comparisons, select the last 164 `Preview simulation` intervals and
their render work; the capture also contains startup and frozen warmup. Worker
CPU totals overlap, so do not add them to main-thread time. See
[Tracy](tracy.md) for building the capture and export tools.

## Replay or open under a debugger

```sh
/tmp/neon-1080/travel-frozen-1-1/replay.sh
python3 tools/benchmark_neon_materials.py \
  --replay /tmp/neon-1080/travel-frozen-1-1 --allow-renderer-changes
```

Replay checks the saved project's fingerprint and, by default, the binary and
engine shaders. The override permits intentional renderer comparisons; the
simulation state must still match the saved report. Each replay gets a new
directory. A changed simulation requires a new baseline.

`--prepare-only` creates the bundle and launch files without rendering. In Xcode,
use Debug Executable with the binary and arguments from `launch.json`.
`chibi_ctl --render-request FILE` reads the request directly, so the debugger
does not need redirected stdin.

On Steam Deck, use the same request with Vulkan and a RADV/Radeon GPU Profiler
capture. Matching the Deck's resolution on a Mac says nothing about Deck
performance.

## Open findings

The `camera-turns` scenario misses 16.67 ms at p95 in live mode even at
resolution 0.5, while its frozen endpoint does not. Tracy attributes the slow
frames to the `Material blast effects` scope: blast occlusion scans obstacles
and terrain-damage spokes rescan material records. Bounding those candidate
scans spatially, while keeping blast order and material state, is the next CPU
experiment. Past measurements are in the
[HRC optimization log](archive/neon-hrc-optimization.md) and the
[material performance record](archive/neon-material-performance.md).

## Regression checks

`make test` includes an independent noncombat MCP scene. An event scheduled after
preparation must execute during live rendering and match headless replay; frozen
rendering of that endpoint must retain the complete state. Invalid capture paths,
orphan capture-frame settings and future inputs in frozen previews are rejected.
