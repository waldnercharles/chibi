# Tick profiling and function-frame reuse

September 11, 2026. [Raw trial results](tick-profiling-results.json).

## What changed

The behavior profiler now separates successful dispatch setup from execution by
behavior asset and event. Setup covers event lookup, validation, input copying,
and activation creation or reuse. Execution includes called functions and host
operations. Nested root events and their setup are subtracted from their caller.
Idle waits accrue no execution time; resumed work stays attributed to its original
event even when another event's dispatch wakes it.

The editor exposes execution and setup columns under **View → Performance →
Simulation**. Its frozen history owns its labels and preserves both measurements.
The remaining world scans, scheduling, reaping, and callback overhead appear as
unassigned time. These are monotonic elapsed intervals, not hardware CPU counters;
preemption can increase them, and parallel intervals can overlap.

`game_behavior_set_profiling(world, false)` disables the callbacks and clock reads.
Profiling retains its existing enabled-by-default behavior. The Arena benchmark
accepts `CBLUEPRINT_BENCH_NO_PROFILE=1` for an uninstrumented comparison. Counters
include successful dispatches, first execution intervals, and resumes.

The first implementation removes repeated allocation of effectful function and
macro call frames. Pooled activations retain at most one idle frame per call site,
including nested calls. Argument buffers and child-frame lists retain their capacity. Reuse resets
arguments, local outputs, waits, services, watches, traversal history, and errors.
Frames referenced by retained host requests are not recycled. Disabling the
activation pool, releasing an instance for reuse, replacing its asset, or destroying
it frees retained frames. Releasing an instance clears cached function references
before its mutation guard is released, so deleted functions cannot leave dangling
frames. Standalone oracle runs without instance pooling keep their original
allocation behavior.

This applies to existing compiled Scene execution on native SLJIT and portable
bytecode. It neither changes authored graphs nor groups/reorders entity updates.
It does not expand whole-function compiler eligibility or introduce ECS batching.

## Measurements

Apple M4 Pro, Homebrew Clang 23.1.1, Release `-O3`, sanitizers disabled,
`MallocNanoZone=0`. Builds and benchmark execution were serialized. Other desktop
activity was present. A stale object from a same-second source swap was detected;
those measurements were discarded, the object and executables were rebuilt, and
distinct executable hashes were verified.

Arena uses seed 12345, 180 fixed steps with firing, and 150 measured steps after
warmup. Five alternating before/after runs report the median of each run's frame
percentiles. Both variants include the new profiler.

| Backend | Behavior p50 before → after | Behavior p95 before → after | Simulation p50 before → after |
| --- | --- | --- | --- |
| SLJIT | 0.114 → 0.088 ms | 0.186 → 0.154 ms | 0.120 → 0.096 ms |
| Bytecode | 0.122 → 0.092 ms | 0.191 → 0.151 ms | 0.129 → 0.099 ms |

Median behavior time fell 23% on SLJIT and 25% on bytecode. The separate three-run
SLJIT check with profiling disabled measured 0.083 ms median behavior time. That
small difference is not a precise estimate of instrumentation overhead.

The profiler identifies body execution as the larger opportunity than root setup:

| Native Tick behavior | Execution before → after, ms/frame | Setup after, ms/frame |
| --- | --- | --- |
| ArenaWaveBehavior | 0.022047 → 0.017340 | 0.000493 |
| ArenaShipMotionBehavior | 0.020933 → 0.017847 | 0.000393 |
| ProjectileBehavior | 0.018793 → 0.015040 | 0.001660 |
| EnemyBehavior | 0.013447 → 0.013020 | 0.001707 |

A second authored fixture increments independent per-instance counters through an
effectful function. It checks every result across eight warmup and 128 measured
updates, with tracing and profiling disabled. Five trials give these median
nanoseconds per entity:

| Instances | SLJIT before → after | Bytecode before → after |
| --- | --- | --- |
| 64 | 3424 → 251 | 4831 → 334 |
| 512 | 2984 → 332 | 6913 → 386 |
| 4096 | 2646 → 639 | 3369 → 702 |

The allocating baseline was noisy. At 4,096 instances its SLJIT trials ranged from
1,193–3,804 ns/entity; reuse ranged from 621–680 ns/entity. These wall-time results
include allocator and scheduling effects, so they do not establish a fixed speedup
for other scenes. The complete stress process peaked at 287,096,832 bytes RSS
before reuse and 342,900,736 afterward. Frame reuse trades allocation churn for
retained memory; these are process peaks, not per-entity live allocation counts.

The generated-C scalar gate still passes: whole-function SLJIT/generated-C ratio
1.303 against the 1.5 limit; SLJIT dispatch/C-host dispatch ratio 0.657 against 1.1.
That gate covers the scalar fixture, not whole Arena functions.

The final rendered 40-enemy crowd run passed 1,800 fixed steps. Mean behavior
time was 0.445 ms; the 40-enemy bucket averaged 0.589 ms over 834 samples. Two
baseline runs recorded overall means of 0.657/0.641 ms. Wall-frame p50 remained
around 8.2 ms: 8.163/8.158 ms before and 8.221 ms afterward. Other desktop activity
and physics/render timing varied, so these crowd observations do not establish a
reliable frame-rate improvement.

## Reproduce

```sh
cmake --build build-performance --target test_suite bench_scheduler bench_behavior -j 8
MallocNanoZone=0 build-performance/test_suite --compiled-arena
CBLUEPRINT_BENCH_NO_PROFILE=1 MallocNanoZone=0 build-performance/test_suite --compiled-arena
MallocNanoZone=0 build-performance/bench_scheduler
python3 tools/bench_behavior.py
```

For a before/after experiment, preserve each executable before changing source.
When swapping source quickly, remove the affected object and relink executables;
verify different hashes before recording a comparison.

## Remaining work

The measured improvement comes from reusing runtime storage. Arena still has zero
whole-function-eligible event entries in this checkout. Effectful calls and
component operations remain on the general compiled path with native C helpers.
The next compiler work should target those operations in the measured hot graphs,
then batch eligible synchronous Tick updates with per-entity state and explicit
component access. A native-like arithmetic kernel does not imply native-like
Scene scheduling or ECS access.

## Validation

- `make test` and `make build` pass, including the native app smoke.
- `test_suite --behavior-parity` passes Scene/prototype gameplay snapshots and
  pooled function fixtures on bytecode and the explicit oracle. Native fixtures
  run in `make test`.
- Function regressions cover 32 changing-argument invocations with bounded frame
  storage, nested waits, retained host requests, cancellation, managed values,
  stepping, watches, fresh traversal history, and deleting a function while its
  instance is inactive before reusing that instance.
- `test_suite --behavior-profile-ui` passes rendered behavior/event expansion,
  owned history, setup values, freeze, and resume. The screenshot was inspected.
- `make web` passes the app build and bytecode event/yield/resume/variable and
  whole-function smoke tests, with SLJIT absent and zero oracle calls.
- Native `check-behavior-backend` with `CHIBI_ENABLE_SJLIT=OFF` reports
  `UNAVAILABLE`, with no interpreter fallback.
- The broader `--arena-ui` run fails at `test/arena_input.c:202`, the Fire-release
  membership assertion, on both the saved baseline and this change. The focused
  profiler test does not claim to resolve that existing input failure.
