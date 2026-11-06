# Prepare demanded target snapshots

This follows [branch read reuse](branch-reads.md), comparing the saved Release
executable from `75836956` with `a579befe` (including allocator commit `d376a59b`).
The changes require no author settings or asset migrations.

Parallel Tick plans now record which nearest-query categories their bodies can
read. A body without a nearest query contributes no categories. Literal U32
masks, including constants passed through an authored function, contribute only
their set bits. Multiple queries and bodies contribute their union. Dynamic
values, ambiguous control flow and opaque invocations use all categories.
This analysis runs when the existing plan is prepared, rather than per entity.

Before workers execute a query-using span, the caller builds contiguous target
positions and handles for the demanded categories. It filters retiring entities
and nonfinite positions once. Each search still checks its own exclusion, radius
and origin, and retains the lowest-handle tie rule. The existing component and
structural-write proofs keep the snapshot valid until all workers and any
synchronous failure recovery finish. The next span rebuilds from current state.
Serial query caching continues to read current target components.

The SLJIT allocator now excludes removed runtime instructions from its floating
call, integer-clobber, ternary temporary and function-layout analyses. Inspected
execution continues to account for every original operation and observer call.

## Validation

The no-query regression first failed because enabling nearest-query profiling
serialized an unrelated eligible batch. It now runs on workers with no searches.
Compiler planning tests cover zero, combined and high-bit masks, unions of
multiple calls, constant function parameters and dynamic parameters.

Query tests compare oracle outputs with native and bytecode execution, both
with and without a prepared snapshot. They cover empty worlds, exclusion, radius
boundaries, ties, multi-category targets, invalid inputs and positions, queued
retirement and slot reuse. A 2,049-reader world exercises actual worker dispatch
with constant and per-instance masks, then moves targets, changes categories,
retires a target and replaces it between updates. Outputs match the serial
backend, with zero interpreter calls.

Full Debug ASan/UBSan `make test`, Release `make build`, Emscripten
`check-behavior-backend`, generated-C host-data parity and focused ThreadSanitizer
query/parallel-runtime suites pass. Existing parity fixtures exercise branches,
joins, variable/component writes, loops, real calls, waits/resumes, inspection
and paused service changes. Sanitizers do not instrument SLJIT machine code;
bytecode and generated C provide separate semantic checks. Changed files pass
formatting and `git diff --check`.

## Measurements

Measurements use optimized, unsanitized builds, identical assets and fixed
input. The paired comparison includes clocks, scheduling and pumping,
all five ArenaEnemy attachments and inactive feedback, with fixed positions
and no physics/rendering. Both rounds use five alternating-order pairs and eight
warmups: the initial round measures 512 frames and the repeat measures 256.
Independent C uses the corresponding sampling duration.
Rendered combat uses three pairs, 240 measured frames and 30 warmups.

The repeat gives the following medians of trial medians. P95 columns are medians
of each trial's P95, not percentiles of the combined sample set.

| Backend | Enemies | Complete behavior P50 before / after | P95 before / after |
| --- | ---: | ---: | ---: |
| SLJIT | 1,000 | 0.037 / 0.036 ms | 0.045 / 0.046 ms |
| SLJIT | 5,000 | 0.093 / 0.089 ms | 0.124 / 0.125 ms |
| SLJIT | 20,000 | 0.331 / 0.293 ms | 0.499 / 0.483 ms |
| Bytecode | 1,000 | 0.335 / 0.341 ms | 0.370 / 0.392 ms |
| Bytecode | 5,000 | 1.700 / 1.721 ms | 1.840 / 1.864 ms |
| Bytecode | 20,000 | 7.285 / 7.260 ms | 7.793 / 7.808 ms |

Native reductions are 2.7%, 4.3% and 11.5%. At 20k the median paired difference
is -0.040 ms. Bytecode changes by less than 2%; it uses neither parallel snapshots
nor the native register allocator. All checksums and zero-interpreter-call checks
match. [Repeat samples and hashes](query-scale-repeat-results.json).

The initial round was inconsistent: native 20k medians were 0.433 / 0.476 ms,
but the median paired difference was -0.016 ms. Native 1k P95 grew from 0.147 to
0.410 ms. A process snapshot during the subsequent rendered run showed a VM at
139% CPU, `duetexpertd` at 85%, and WindowServer at 42%; this confirms competing
work, without establishing the cause of each timing difference. Both rounds are
retained. The repeat supports a modest native gain, not a portable speedup
guarantee. [Initial samples](query-scale-results.json). An earlier partial run
overlapped the end of sanitizer validation; it was stopped and excluded.

Intermediate three-pair, 256-frame measurements separate the changes:

| Step | Native 1k P50 before / after | Native 20k P50 before / after | Median paired 20k difference |
| --- | ---: | ---: | ---: |
| Query demand | 0.043 / 0.041 ms | 0.600 / 0.537 ms | -0.085 ms |
| Position snapshots | 0.037 / 0.035 ms | 0.303 / 0.322 ms | +0.006 ms |
| Allocator analysis | 0.033 / 0.032 ms | 0.447 / 0.369 ms | -0.080 ms |

These are separate runs under varying host conditions and must not be compounded.
The snapshot-only measurements do not establish an independent speedup in this
one-target fixture. The change removes per-search component resolution, but its
benefit at larger target populations remains to be measured.
[Demand samples](query-demand-results.json),
[snapshot samples](query-snapshot-results.json),
[allocator samples](query-allocator-results.json).

The repeated independent-C comparison measures native update P50 of 0.344 ms
and C P50 of 0.218 ms at 20k, with median per-trial ratio 1.557. At 1k, the ratio
is 3.182 (0.035 / 0.011 ms). Native execution uses workers; C is single-threaded,
prebinds pointers across the run and assumes inactive feedback. This compares
elapsed time, not work per core. The noisier initial run had a 20k ratio of
4.264; that variability limits conclusions about the remaining gap.
[Repeat reference samples](query-reference-repeat-results.json),
[initial reference samples](query-reference-results.json).

All six existing Tick workloads at 512/4,096 entities pass the 1.5x
native/generated-C gate, with ratios 0.909–1.103 and matching state/allocation
checks. This comparison uses the same lowered bodies and is separate from the
independent C algorithm. [Generated-C samples](query-tick-results.json).

## Rendered combat

Arena3D runs at 1280x720 with held fire, kills, XP and restart. State, combat
traces, parallel execution and zero interpreter calls match in all pairs.

| Enemies | Behavior P50 before / after | Physics P50 before / after | Wall P50 before / after | Wall P95 before / after |
| ---: | ---: | ---: | ---: | ---: |
| 1,000 | 0.218 / 0.215 ms | 0.660 / 0.640 ms | 8.383 / 8.353 ms | 8.867 / 8.849 ms |
| 20,000 | 2.003 / 1.980 ms | 7.581 / 7.600 ms | 13.509 / 13.173 ms | 27.813 / 27.421 ms |

At 20k, behavior improves in every pair by 0.013–0.047 ms; the median paired
reduction is 0.023 ms, about 1.1%. Whole-frame differences include variation in
other phases and cannot be attributed entirely to this change. This short
scenario does not establish sustained projectile-load or frame-rate guarantees.
[Combat samples and hashes](query-scene-results.json).

## Remaining work

Nearest calls still pack the generic host ABI. Typed lowering could remove
argument-bank writes and result unpacking. The independent C reference also
prebinds owner component pointers across the complete run; equivalent runtime
binding needs entity and storage-lifetime guards. Large Tick spans still check
each row every update. These remain separate opportunities with their own
correctness and performance requirements.

## Reproduction

Save the Release executable from `75836956` before building this change:

```sh
python3 tools/bench_arena_scale.py --before build-release/test_suite-before-query-snapshots --after build-release/test_suite --counts 1000 5000 20000 --frames 512 --trials 5 --clocks --output /tmp/query-scale.json
python3 tools/bench_arena_reference.py --binary build-release/test_suite --counts 1000 20000 --frames 512 --trials 5 --output /tmp/query-reference.json
python3 tools/bench_ticks.py --build-dir build-release --entities 512 4096 --frames 64 --trials 3 --enforce-native-ratio --output /tmp/query-ticks.json
python3 tools/bench_arena_scene_scale.py --before build-release/test_suite-before-query-snapshots --after build-release/test_suite --counts 1000 20000 --frames 240 --trials 3 --combat --expect-parallel --output /tmp/query-scene.json
```

Use `--frames 256` for the isolated and independent-C repeat. Intermediate
measurements use the corresponding saved executables and `--backends SLJIT`
with three pairs.
