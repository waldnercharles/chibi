# HRC sparse storage experiments

September 26–27, 2026. Experimental branch `cwaldner/hrc-sparse-investigation`.
The comparison baseline is commit `1a4e74ea3c15c7d3c13a0252f0124515f5e4516d`,
which snapshots the current renderer and authored project from the original
working tree. The original working tree was left in place.

This implements the first experiments from
[the directional probe investigation](hrc-sparse-directional-probes.md).
The renderer experiment retains three directly traced cascades, the lighting
margin, source filtering, temporal settings and existing receiver reconstruction.
The directional tree remains a standalone CPU study.

## Compact intervals in the renderer

`CHIBI_HRC_COMPACT_INTERVALS=1` enables compact interval storage when an HRC
instance is created. The default is off. The first implementation packs only
the first two interval levels:

- T0 stores both directions at odd spatial probes. This is safe when T1 is
  traced independently; no extension pass needs the discarded T0 entries.
- T1 stores all directions at odd probes and the two even directions at even
  probes. This is safe when T2 is traced independently.
- T2 and higher levels keep their existing layout. In particular, T2 remains
  complete because it supplies the first extension pass when tracing three levels.

The dispatch operates on the packed indices, and merge converts its logical
probe/direction address to the packed address. Omitted entries are never queried;
there is no replacement with zero or with an approximation. This is dependency
compaction rather than an optical occupancy test. Its benefit does not require
an empty scene.

The CPU allocation checks both viewport rotations and invalidates capacity reuse
when a trace setting changes the required layout. RGB and independent scalar
transport use the same mapping. Scalar reuse does not create an extra interval
hierarchy.

For a 1094 × 674 transport domain with three traced levels, RGB interval buffers
lose 7.0345 MiB and logical trace invocations fall by 26.31%. These counts exclude
thread-group padding. The saved intervals are short, so the invocation reduction
is not a prediction of an equal frame-time reduction. Merge, extension, material
rasterization and reconstruction still run.

## Keeping the domain below the next cascade threshold

At the tested 1920 × 1080 viewport, 64-unit lighting margin and world-grid padding,
transport scale 0.5 produces a 1094 × 674 domain and eleven levels. Scale 0.468
produces 1024 × 631 and ten levels. Reconstruction remains at 1.0; independent
luminance is disabled. These are unified RGB transport experiments with full
reconstruction, not the quarter-color/half-luminance mode.

The lower scale reduces calculated hierarchy allocations from 105.26 MiB to
81.48 MiB before interval compaction. It also changes transport samples and the
top of the hierarchy. It must be evaluated as a quality setting, not an exact
optimization. A fixed scale is only a threshold experiment for this viewport;
other viewport sizes, margins and camera scales require a different bound.

## Measurement method

Native Metal on an Apple M4 Pro, Release build, sanitizers disabled. Each ordinary
render run warms up for 240 frames, discards sixteen additional frames, and then
measures 120 frames. Reported frame work includes the existing GPU-completion wait
and excludes pacing. It is host elapsed frame time, not hardware GPU timestamp
measurements for individual passes.

No build or separate benchmark from this investigation overlaps a timed run.
Other applications remained active. The process samples show substantial external
CPU activity, but they cannot establish which processes competed for GPU resources
or how much thermal variation affected a run. Repeated, alternating comparisons
matter more than isolated timings on this host.

The frozen Sandbox fixture holds simulation at frame 1. The moving fixture starts
at frame 30 and advances to 166 with a fixed input timeline, including shots and
destruction. The static dense optical fixture has 1,026 entities. It is a rendering
stress fixture, not a measurement of reactive fog or MPM simulation.

Ordinary timing runs capture only a final image. Separate raw frame captures check
consecutive moving frames; their readback cost excludes them from timing claims.
The benchmark CLI stores the compact flag in both the provenance manifest and
replay environment:

```sh
python3 tools/benchmark_neon_materials.py \
  --output /tmp/hrc-compact-travel --scenario travel --mode live \
  --scale .5 --trace-levels 3 --lighting-margin 64 \
  --reconstruction-scale 1 --render-frames 136 --compact-intervals
```

Omit `--compact-intervals` for the control. Use separate output directories and
alternate the order for matched repetitions. The exact frozen/moving input
requests used for the initial investigation are retained with the raw artifacts
under `/private/tmp/hrc-sparse-results`.

## GPU experiment results

Four counterbalanced dense/compact pairs used the same candidate executable,
shaders, project and request. Each cell below lists the median frame times from
all four repetitions, in milliseconds:

| Fixture | Dense | Compact | Paired saving, range |
| --- | --- | --- | --- |
| Frozen Sandbox | 18.451, 18.229, 23.685, 22.323 | 18.415, 18.211, 22.501, 20.777 | +0.018 to +1.546 ms |
| Moving Sandbox | 21.614, 22.763, 25.193, 23.594 | 21.881, 23.248, 25.232, 23.051 | −0.485 to +0.543 ms |
| Dense optical fixture | 13.594, 16.773, 16.566, 14.218 | 13.390, 16.086, 16.618, 14.322 | −0.104 to +0.687 ms |

A positive saving means compact was faster. The large change between repetitions
limits the precision of this comparison. The moving fixture does not show a
consistent improvement. Compact storage remains opt-in: it saves memory and
work, but this experiment does not establish a useful moving-frame speedup.
All twelve matched pairs produced identical final PNGs and identical initial
and final simulation state. The frozen baseline executable and the candidate
with compaction disabled also produced identical pixels.

The separate scale experiment has a larger but approximate benefit. The last
adjacent baseline pairs measured 18.197 → 16.640 ms for frozen Sandbox and
21.820 → 20.136 ms for moving Sandbox when changing 0.5 to 0.468. These ran
earlier than the compaction pairs, under different external load. An initial
frozen baseline was 28.143 ms; comparing that isolated value with 16.640 would
overstate the benefit. All initial runs remain in the raw results.

At scale 0.468, mean absolute RGB difference was 0.328/255 in the frozen final
frame and 1.330/255 in the moving final frame. Respectively, 1.31% and 12.81% of
pixels differed by more than three code values in at least one channel. Maximum
channel differences were 166 and 96. Material states matched across scales.
These are image differences against the existing HRC, not errors against a
physical reference, and they do not measure temporal stability. Scale 0.468
remains an experiment with no authored default change. Moving-frame timings
still exceed the 16.67 ms frame budget before adding the proposed effects.

Reproduce the allocation counts with:

```sh
python3 tools/analyze_hrc_sparse.py
```

Separate synchronized pass diagnostics measured trace at 1.845 ms dense versus
1.664 ms compact. Merge was 4.020 versus 3.939 ms; extension was 2.454 versus
2.464 ms. Dense material raster, gather and composite measured 2.748, 2.053 and
2.432 ms. Each mark submits and waits, adding overhead and changing overlap.
These are diagnostic host intervals, not GPU timestamps or additive costs for
an ordinary frame. They support the limited conclusion that removing short
traces leaves larger stages untouched.

Quality checks also covered independent quarter-color/half-luminance transport,
scalar reuse, shadow, pinhole and single-pixel-light fixtures. All final images
and simulation states matched. A separate moving run captured 32 consecutive
641 × 359 RGBA frames, all byte-identical with compaction off and on.

That run exposed a preview issue: a Retina DPI event recreated Cute's app canvas
at twice the requested dimensions. The preview now restores the requested canvas
dimensions after window events. Timing pairs used the earlier executable; quality
and pass diagnostics used the capture fix. The HRC implementation was unchanged.
The results file records both executable hashes.

## Directional sparse ray-query prototype

The standalone probe `tools/hrc_sparse_sweep.c` converts a regular 2D optical
grid into parallel, world-anchored line samples for each direction.
Each line uses exact cell-crossing lengths and stores only nonidentity optical
segments in a balanced binary tree. Parent intervals compose their actual ordered
children. Pure black occluders are retained; empty segments transmit light.
Queries integrate partial endpoint cells and retrieve complete interior intervals
from the tree.

This tests the proposed representation conversion and interval query. It does
not implement a new angular cascade schedule, sparse receiver demand, multibounce
updates or a GPU construction pass. It starts from optical coefficients, which
retain visibility information, rather than resampling already-averaged radiance.
The coordinates can be expressed as a dominant-axis shear; the prototype uses
unit direction/normal coordinates and a specified perpendicular line pitch.

The tree and its cell coordinates use doubles. Every node occupies 64 bytes on
this host, and per-line query metadata occupies 32 bytes. Logical persistent
counts below include both. Input grid, allocated capacity, dense validation
buffers and construction scratch are reported separately in the JSON. These
counts cannot be substituted for current GPU RGB8B/scalar4B interval storage or
used to claim the memory usage of sparse vanilla RC.

The default study uses a 96 × 64 optical grid, line pitch 2, a fixed world phase,
4/16/64 directions and eight range queries per intersecting line. It covers
cardinal, diagonal and near-axis directions. The 64-direction cases each issue
25,920 queries. The dense comparison binary-searches the first overlapping
precomputed cell interval, then scans only the requested range. It excludes
construction and is not a comparison against the GPU HRC renderer.

Median CPU timings from three runs with Apple clang 17, `-O2`:

| Scene, 64 directions | Build ms | Tree queries ms | Dense interval queries ms | Logical tree MiB |
| --- | --- | --- | --- | --- |
| Thin wall | 4.530 | 0.071 | 1.956 | 0.262 |
| Tiny emitter | 4.264 | 0.075 | 1.930 | 0.221 |
| Sparse objects | 4.395 | 0.075 | 1.929 | 0.371 |
| Dense fog | 8.756 | 2.192 | 1.966 | 30.423 |

Construction includes traversing the regular grid, producing cell intervals,
compacting active leaves and building parent aggregates. The prototype retains
dense cell intervals for its reference checks, which a production sparse builder
could stream instead. Its time is included, so the build numbers describe this
prototype. They are not a lower bound on a GPU implementation.

This establishes useful sparse queries, not cheap full reconstruction. Even at
this small grid, build dominates sparse cases. Dense fog retains every sampled
leaf, makes the tree about 2.28 times the logical size of the dense interval list,
and gives slower tree queries. A dense fallback is justified before attempting
renderer integration. Rebuilding all directions from all pixels each frame
would discard much of the benefit of fast queries.

Each default run checked 134,400 arbitrary range queries against direct ordered
composition of the same sampled cells. Maximum error was 6.11e-16. Another 336
line checks compared the DDA integrator with an independent cell-box intersection
reference. Tests also changed the phase to 0.37, changed pitch to 1 and 4, and
increased the grid to 128 × 96. Numerical invariants passed in all configurations.

Interpolation between stored lines is the unresolved quality problem. At pitch
2, the thin-wall case produced 206 false-transmission samples among 16,384 tested
receiver/direction pairs. The criterion is reference transmission below 0.001
and interpolated transmission above 0.05. At 16 directions, reducing pitch from
2 to 1 reduced these cases from 46 to 17; increasing pitch to 4 raised them to
104. A changed phase also changes the error. Tiny emitters show large radiance
differences when a sampled line misses their footprint.

Thus, exact queries on stored lines do not make interpolation to arbitrary
receivers exact. The prototype uses simple interpolation after composing each
path, which preserves interval composition but still mixes visibility across
different paths. Conservative source footprints and visibility-aware receiver
reconstruction remain necessary. The broader research report describes why a
nearest-two parent rule alone does not enforce those dependencies.

The probe left the tree on 2026-10-19 with the other retired one-off probes.
Recover it from the commit that added it, then build and run the study:

```sh
git show d837dbe7b:tools/hrc_sparse_sweep.c > /tmp/hrc_sparse_sweep.c
cc -std=c11 -O2 -Wall -Wextra -Werror -pedantic \
  /tmp/hrc_sparse_sweep.c -lm -o /tmp/hrc_sparse_sweep
/tmp/hrc_sparse_sweep > /tmp/hrc-sparse-tree.json
/tmp/hrc_sparse_sweep --directions 16 --pitch 1 --phase .37
```

`--ppm-prefix /tmp/hrc-tree` also writes reference, interpolated and error images
for one direction. The process exits unsuccessfully if tree/serial or DDA/cell-box
equivalence exceeds 1e-8. Interpolation error is deliberately reported rather
than treated as an exactness assertion.

## Validation and retained scope

- `make test` passed in Release, including generation, reflection, reload,
  development tooling, MCP, canonical assets, behavior backends and the full
  test suite/whole-function audit.
- Native Metal `test_suite --radiance` passed with compaction disabled and
  enabled. The new regression exercises production trace/merge at odd dimensions,
  cutoff changes 1 → 2 → 3 → 5 → 3 → 1, nonblack output and a smaller odd resize.
- The standalone prototype passed strict compilation and AddressSanitizer/UBSan
  using Homebrew clang 23. Leak checking was disabled. Apple's clang 17 sanitizer
  stalled in runtime initialization before `main`; a stack sample identified the
  initializer spin, and that process was stopped before using the newer runtime.
- Benchmark preparation persisted the new opt-in flag in both manifest and replay
  environment. Python syntax checks and repository formatting checks passed.

All quality/performance comparisons use `split_frames=1`. Review also found an
existing limitation when changing trace cutoff with split frames enabled:
already-solved quadrants can temporarily retain the old cutoff. That behavior
predates this experiment and is not addressed here.

The [machine-readable results](hrc-sparse-investigation-results.json) contain
every matched GPU repetition, the initial scale runs, pass diagnostics, quality
verdicts, CPU experiments, allocation counts, input requests and executable
hashes. Raw PNG/RGBA captures and logs are under
`/private/tmp/hrc-sparse-results`.

The retained changes are an opt-in compact renderer layout, reproducible
benchmark controls, a preview capture fix, tests and the standalone directional
query study. No authored rendering default changed. The source working tree's
tracked diff still matches its pre-investigation snapshot.
