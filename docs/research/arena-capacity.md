# Entity growth, activation memory and attachment scaling

September 12, 2026. The engine now runs the actual ArenaEnemy prototype with
20,000 enemies, 100,000 behavior attachments and zero interpreter-oracle calls
in the isolated Tick fixture. This removes the former 10,000-total-entity
ceiling. The fixture still excludes physics, rendering, collisions, wave spawning
and the rest of the Scene. It does not demonstrate 20,000 playable enemies.

## Changes

- ECS identity arrays grow geometrically during structural operations. Component
  occupancy covers the world; payload columns grow only when a component is added
  at a higher slot. Existing compiled programs use refreshed storage pointers
  without recompilation or an extra per-row indirection. Queries reject growth
  and schema mutation while their pointers are in use.
- Creation keeps a lowest-free-slot cursor instead of scanning the occupied prefix
  from slot 1 each time. Destroy and canceled reservations update that cursor and
  invalidate their handles. Flat-column pointers must be reacquired after
  structural commits and schema changes.
- Activation input and scratch arenas allocate on first use. They retain needed
  blocks for pooling, instead of reserving two 64 KiB blocks per activation.
- Attachment lookup and inspection use ordered indices by entity slot. Fresh
  attachment sets append directly; unchanged contiguous sets retain storage.
  Reordering, removal, rollback and replacement rebuild the index. Deferred Begin
  resolves its stable sequence snapshot through this index. The global broadcast
  order remains intact.

The attachment change removes repeated whole-world scans and copies during
ordinary prototype creation. Bulk removal and hot replacement still contain
linear scans and shifts, so repeated large removal bursts remain a scaling task.

## Measurements

All runs use optimized Clang 23.1.1 builds with sanitizers disabled, fixed content,
all five authored attachments, eight warmup ticks and three paired trials with
rotating order. Positions are fixed, and velocity snapshots, backend selection,
attachment counts and zero oracle calls are asserted. Profiling is disabled in
timed comparisons. No builds or other benchmarks ran concurrently. RSS is peak
process resident memory, not live retained allocations, and includes engine and
asset storage. The historical `_mb` field reports MiB.

Three comparisons isolate the changes, with raw logs and executable/content
hashes:

1. [Dynamic capacity](arena-capacity-results.json), versus the saved query-cache
   baseline, at 100/1,000/9,999 enemies. Tick stayed near its previous range and
   small worlds saved about 7 MiB. Cold setup at 9,999 rose from about 2.03 to
   2.29 seconds; capacity growth alone did not solve allocation or attachment
   costs.
2. [On-demand arenas](arena-lazy-results.json), with dynamic capacity in both
   executables, at 100/1,000/10,000/20,000. Native peak RSS at 20,000 dropped from
   3,943 to 399 MiB. Setup remained quadratic, about 11 seconds at 20,000.
3. [Attachment indexing](arena-attachment-results.json), with lazy arenas in both
   executables, at the same counts. Native setup at 20,000 dropped from 11,597 to
   126 ms, about 92x faster. Peak RSS dropped from 396 to 226 MiB. Native setup at
   10,000 dropped from 2,366 to 62 ms, about 38x faster.

Final results, median of per-run percentiles over 32 measured ticks:

| Backend | Enemies | Tick p50 ms | Tick p95 ms | Cold setup ms | Peak RSS MiB |
| --- | ---: | ---: | ---: | ---: | ---: |
| SLJIT | 100 | 0.018 | 0.022 | 0.700 | 15.8 |
| SLJIT | 1,000 | 0.184 | 0.192 | 6.389 | 25.2 |
| SLJIT | 10,000 | 3.993 | 4.404 | 61.911 | 121.7 |
| SLJIT | 20,000 | 9.364 | 9.940 | 126.226 | 225.8 |
| BYTECODE | 100 | 0.067 | 0.070 | 0.751 | 16.0 |
| BYTECODE | 1,000 | 0.617 | 0.688 | 6.182 | 24.8 |
| BYTECODE | 10,000 | 8.720 | 9.193 | 62.272 | 120.5 |
| BYTECODE | 20,000 | 18.108 | 19.038 | 130.571 | 223.0 |

These changes improve creation and memory; they do not establish a steady Tick
speedup. The initial attachment comparison measured native 10,000-enemy Tick
3.391 -> 3.993 ms and 20,000-enemy Tick 8.948 -> 9.364 ms. A
[longer repeat](arena-attachment-repeat-results.json) uses 256 measured ticks,
three paired native trials, and the same executable hashes. It measures:

| Enemies | Before p50 | After p50 | Before p95 | After p95 | Median paired p50 delta |
| --- | ---: | ---: | ---: | ---: | ---: |
| 10,000 | 3.890 ms | 3.963 ms | 4.654 ms | 4.829 ms | +0.014 ms |
| 20,000 | 9.011 ms | 9.303 ms | 9.628 ms | 9.900 ms | +0.292 ms |

The repeat narrows the larger initial difference but retains a roughly 3%
20,000-enemy slowdown. The index is not consulted by the timed Tick broadcast
or pump. Allocation placement and cache behavior are possible causes, not a
confirmed attribution. This tradeoff is recorded alongside the large startup and
memory gains; the outer scheduler and data locality remain optimization targets.

A [separate instrumented run](arena-capacity-profile.txt) at 20,000 enemies
records 1,280,000 nearest searches across 32 measured ticks, 640,032 world
candidates and 32 cache builds. It validates the shared-query bound and zero
oracle calls. Its timings include profiling overhead and are not mixed above.

## Rendered Arena regression

[Three paired rendered trials](arena-capacity-crowd-results.json) compare the
saved pre-capacity executable to the final executable on actual Arena3D at
1920x1080, moving without firing. Each run has 1,800 fixed ticks, 180 warmup ticks
and 834 samples at the authored 40-enemy cap. Gameplay and sampling windows
match. Per-run median behavior means are 0.124 -> 0.120 ms; simulation means are
0.263 -> 0.251 ms. However, paired differences and run ranges do not support a
reliable new CPU speedup. Wall p50 is 8.288 -> 8.345 ms and wall p95 is
9.195 -> 9.216 ms. This is a regression check, not large-count acceptance.

## Remaining work at this checkpoint

The subsequent [dedicated Tick update loop](tick-updates.md) addresses the
broadcast/context setup below and contains newer Tick measurements.

At this checkpoint, Tick was still delivered as an event to individual attachments. Eligible whole
bodies avoid activation creation, but per-instance dispatch and context binding
remain. The next compiler optimization is automatic execution as ECS-style
systems: compile an outer entity loop and share bindings across compatible
bodies while preserving state, effects and ordering. Authors should keep their
Tick graphs. Waits, inspection and unproven effects retain existing scheduling.

Full physics/rendering scenes at 10,000 and 20,000, large spawn/removal bursts,
source-linked performance diagnostics and complete whole-function coverage
remain unfinished. The [current audit](arena-capacity-audit.txt) is unchanged:
21/33 events, 24/31 effectful local bodies, 39/39 pure local bodies and 4/4 library
bodies qualify for whole-function execution. General compiled callbacks cover
the exclusions; a SLJIT label does not mean the outer entity loop is compiled.

## Reproduction

```sh
python3 tools/bench_arena_scale.py \
  --before build-performance/test_suite-before-capacity \
  --after build-performance/test_suite-before-lazy-arenas \
  --counts 100 1000 9999 --frames 32 --trials 3 --output /tmp/capacity.json
python3 tools/bench_arena_scale.py \
  --before build-performance/test_suite-before-attachment-index \
  --counts 100 1000 10000 20000 --frames 32 --trials 3 --output /tmp/attachments.json
python3 tools/bench_arena_scale.py \
  --before build-performance/test_suite-before-attachment-index \
  --counts 10000 20000 --frames 256 --trials 3 --backends SLJIT \
  --output /tmp/attachments-repeat.json
```

The lazy-arena report used `test_suite-before-lazy-arenas` as before and
`test_suite-before-attachment-index` as after. Saved binaries and their hashes
are necessary to reproduce each isolated comparison.

## Validation

- `make test` and `make build` passed after the final attachment/Begin change.
- The native sanitizer suite covers 20,001-entity creation, mixed dense/bitset
  queries, high-slot migration, iteration guards, canceled reservations and stale
  handles. SLJIT and native-host bytecode use already-published whole component
  functions before and after growth, including missing components and high/low
  reads and writes. One test was migrated to reacquire a component pointer after
  adding another row; the sanitizer found its old fixed-column assumption.
- Attachment regressions cover interleaved broadcast order, immediate/deferred
  Begin, failed publication rollback, 600-entity index growth, hot replacement,
  removal and generation reuse on SLJIT, bytecode and oracle.
- `--compiled-arena` passed native and bytecode firing, simulation and restart
  with 63 instances, 16/22 whole entries and zero oracle calls in that live subset.
- A native build without SLJIT reports `UNAVAILABLE` and refuses execution.
- `make web` passed. Emscripten's backend smoke asserts absence of SLJIT, executes
  bytecode component access across 20,001 entities, and passes the existing waits,
  continuation and debugger smoke checks. That smoke does not run the actual
  Arena query-cache or attachment-index workload in a browser.
- The changed C/H files pass clang-format verification; `git diff --check` passes.
- Full whole-function audit still returns nonzero for the documented exclusions.
