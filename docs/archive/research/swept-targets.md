# Prioritize slow frames and batch swept-target updates

September 19, 2026. This follows the [target snapshot work](query-snapshots.md).
Further isolated kernel tuning has a small frame-time budget in the current
Arena workload. The previous independent-C comparison measured 0.344 ms for
native behavior updates and 0.218 ms for the specialized C implementation.
That gap is useful compiler evidence, but it cannot explain multi-millisecond
full-scene stalls.

## Current attribution

Tracy 0.14.1 captured the worktree at `446d925a`, with six added physics phase
zones. The rendered Arena3D fixture used 20,000 enemies, 1,800 combat frames,
eight workers, and coarse physics timers. Connection started after first fire.
Detailed behavior inspection stayed off, preserving parallel Tick execution.

The slowest 89 of 1,794 complete engine frames averaged 30.338 ms. These are
the slowest simulation frames, selected by their own durations. Rendering and
presentation happen outside this scope in the fixture. The table reports
durations from those same frames, rather than adding unrelated percentiles.

| Work | Mean in slowest 5% of engine frames |
| --- | ---: |
| Swept-target index maintenance | 7.964 ms |
| Box2D step | 6.678 ms |
| Contact event pump, including lifecycle flushes | 6.671 ms |
| Contact pair collection and worker join | 3.418 ms |
| Pair sorting and contact event queueing | 1.919 ms |
| Behavior update, including preparation and execution | 1.071 ms |

Parallel behavior execution is nested inside behavior update and averages
0.252 ms on these frames. Swept-target maintenance averages 1.152 ms across
the full capture, with a 0.219 ms median and 8.967 ms P95. Its concentration
in slow frames makes it a better first target than another kernel tweak.
[Capture metadata, zone statistics, selected frames and scenario log](swept-target-profile.json).

## Implementation

When continuous sensors exist, the physics service maintains a spatial tree
covering both endpoints of each target's motion. Moving targets previously
removed and reinserted their tree proxies individually. They now update leaf
bounds with `b2DynamicTree_EnlargeProxy`, then call
`b2DynamicTree_Rebuild(false)` once before the worker queries. Unchanged
subtrees remain intact. Existing conservative margins still avoid unnecessary
updates, and the tree stays dormant when there are no continuous sensors.

Exact relative-motion casts, collider masks, owner exclusion, nearest-hit
selection, contact ordering and the Box2D solve remain the same. This shared
physics change requires no authored asset changes or quality settings.

## Results and limits

The follow-up Tracy capture measures swept-target maintenance at 0.437 ms mean
and 1.860 ms P95, down from 1.152 ms and 8.967 ms. The phase P95 fell about 79%.
A separate Release comparison with only coarse physics timers measured
1.190 to 0.447 ms mean for that phase, about 62% lower. All runs matched the
combat trace, snapshot checksum, eight-worker execution and restart acceptance.

This establishes a phase-level improvement. It does **not** establish a
repeatable whole-scene speedup. The three alternating, uninstrumented Release
pairs below ran 1,800 frames each with fixed content/input at 1280x720.

| Pair | Frame median before / after | Frame P95 before / after |
| --- | ---: | ---: |
| 1 | 19.019 / 16.469 ms | 37.750 / 25.605 ms |
| 2, after first | 19.543 / 18.429 ms | 35.211 / 46.196 ms |
| 3 | 22.673 / 32.354 ms | 39.951 / 65.895 ms |

A later coarse-timed pair, after first, measured 17.270 / 18.171 ms median and
30.994 / 27.000 ms P95. The Tracy runs measured 16.941 / 17.685 ms median and
30.478 / 28.066 ms P95 for the whole rendered frame. The post-change capture's
slowest engine frames now spend 14.928 ms in the Box2D step, 5.338 ms collecting
pairs, 3.800 ms pumping contact events and 1.034 ms maintaining swept targets.
Those are a newly selected slow-frame population, not matched frame indices.

A CPU snapshot after the first comparison showed VirtualMachine at 119.9%,
the Codex service at 50.1% and WindowServer at 41.9%. This establishes competing
work, not the cause of every regression. All measurements, including the
regressions, are retained. Keep this change in the draft performance branch;
repeatable full-scene acceptance is still required before recommending merge.
[Paired samples, phase logs and executable hashes](swept-target-results.json).

The shared physics tests cover walls, filters, nearest hits, moving and rotating
targets, zero-delta synchronization, retirement, reused slots, reset and ordered
contact re-entry. A new standalone fixture repeatedly moves a population of
targets across old index bounds and checks the nearest crossing hit. It runs
with serial and parallel collection, uses no Arena mechanics, and exercises
both two- and four-substep configurations.

Validation passed: the full Debug ASan/UBSan `make test` target, Release native
runtime checks, WebAssembly `check-behavior-backend`, the Tracy build, formatting
of changed C files, and `git diff --check`. The native physics cases used eight
workers; the WebAssembly cases exercised the portable serial path.

Reproduce the full-scene comparison with saved optimized executables:

```sh
python3 tools/bench_arena_scene_scale.py --before build-release/test_suite-before-bulk-sweeps --after build-release/test_suite --counts 20000 --frames 1800 --trials 3 --combat --expect-parallel --expect-after-workers 8 --output /tmp/swept-targets.json
```

For phase attribution, set `CBLUEPRINT_BENCH_COMBAT=1`,
`CBLUEPRINT_BENCH_PHYSICS_PROFILE=1` and
`CBLUEPRINT_BENCH_REQUIRE_PARALLEL=1`, then invoke each executable with
`--bench-arena-scene-scale 20000 1800 0`. For Tracy, connect after first fire and
group named phase occurrences by their containing `Engine frame`. Occurrence
tuples contain a start time and duration, both in nanoseconds, not an end time.

## Next priorities

1. Reduce contact/event work. Every Arena enemy has an authored contact reaction,
   so demand checks alone cannot discard the dense enemy contact population.
   Separate event execution from lifecycle work, then remove repeated pair
   enumeration and per-event allocation/lookup where the measured cost lies.
   Ordered compiled event batches are a candidate; preserve RNG, handler order,
   retirement, waits and the existing work budget.
2. Investigate Box2D's remaining broadphase/solve cost after those changes.
   Fewer substeps, different collision responses or simulation LOD change the
   authored contract and are separate choices, not equivalent optimizations.
3. Return to compiler and query work when representative workloads expose a
   larger budget. The independent C reference prebinds component pointers and
   specializes active paths. Generated C and SLJIT can use the same shared
   planning and lifetime proofs, but benchmarks must include the cost of those
   proofs. Query-heavy scenes with many targets need separate scaling evidence;
   Arena's mostly single-target nearest queries cannot establish that result.

Prioritize repeatable full-scene median and tail reductions at equal gameplay.
Keep the C comparison as a compiler check, and avoid using its percentage gain
as the estimate of frame-time impact.
