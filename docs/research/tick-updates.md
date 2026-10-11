# Dedicated Tick updates

September 12, 2026. Scene and prototype execution now use a prepared update
schedule. Authors keep their Tick graphs. Eligible synchronous bodies execute
in attachment order without named-event lookup, activation creation or repeated
host-service binding. Native builds use an SLJIT outer loop; portable bytecode
uses the same rows and reusable scratch through a C loop.

## What changed

The schedule contains only attachments declaring Tick. It borrows immutable
program generations from their instances and owns shared host bindings and VM
scratch. Each row supplies its own variables, owner and delta time. Consecutive
ArenaEnemy updates retain the original EnemyBehavior/CombatFeedback interleaving.
The native loop calls each prepared whole-function entry through its binding;
it does not yet fuse those bodies. The subsequent
[bound-column optimization](bound-columns.md) resolves component IDs once per
prepared span; its measurements are separate from this initial scheduler change.

Rows qualify only when the whole body is synchronous and preserves the current
entity-query dependencies. Host operations obey their existing non-reentrant ABI.
Before effects, native guards check entity liveness/generation, instance/backend,
service revision, tracing and active activations. An ineligible row returns to
the established compiled scheduler. A failure is recorded from the completed
partial execution, then later rows continue without repeating earlier writes.

Attachment changes, hot apply and service rebinding invalidate the schedule.
Rebinding notifies the owning world before the old service can be released.
The first update builds the schedule; steady updates reuse it. Inspection
retains the existing activation/watch/stepping APIs. Profiling uses the prepared
C outer loop with compiled bodies and explicit timing callbacks. Detailed per-behavior profiling is disabled by default. The editor
enables it while the Performance panel is visible and unfrozen; closing or
freezing it restores native iteration. Explicit runtime profiling remains
available. Aggregate frame timings stay active. Unprofiled native spans contain
no profiling calls.

`behavior_last_update` reports total entries, prepared executions, native
outer-loop executions, and entries handled by ordinary dispatch. This distinguishes
whole-body code generation from actual compiled iteration. Explicit named-event
broadcasts remain available for tools and parity comparisons.

## Measurements

[Paired ArenaEnemy results](tick-update-scale-results.json) use all five actual
prototype attachments, two Tick bodies per enemy, fixed positions, eight warmup
updates and 128 measured updates. Three paired trials rotate executable order.
Clang 23.1.1 Release builds disable sanitizers and profiling. Checks require the
same velocity checksum, zero oracle calls, two prepared updates per enemy, and
native outer-loop execution for every unprofiled SLJIT row. Physics, rendering,
contacts, wave spawning and the rest of the Scene are excluded.

The table reports medians of per-run p50 values. Each value includes Tick and
scheduler pumping. Asset/prototype setup precedes measurement, and the cold
schedule build occurs during warmup.

| Backend | Enemies | Before p50 ms | After p50 ms | After p95 ms | Speedup |
| --- | ---: | ---: | ---: | ---: | ---: |
| SLJIT | 100 | 0.018 | 0.005 | 0.006 | 3.60x |
| SLJIT | 1,000 | 0.187 | 0.059 | 0.061 | 3.17x |
| SLJIT | 10,000 | 2.470 | 0.690 | 0.901 | 3.58x |
| SLJIT | 20,000 | 8.408 | 2.925 | 3.784 | 2.87x |
| BYTECODE | 100 | 0.066 | 0.053 | 0.055 | 1.25x |
| BYTECODE | 1,000 | 0.623 | 0.515 | 0.547 | 1.21x |
| BYTECODE | 10,000 | 8.374 | 5.537 | 6.413 | 1.51x |
| BYTECODE | 20,000 | 16.613 | 12.611 | 14.767 | 1.32x |

RSS increases by roughly the size of the compact schedule. It does not allocate
one context, VM bank or activation per row. Cold schedule construction remains
visible: the median first update plus pump at 20,000 enemies was 18.71 ms, versus
2.93 ms steady p50. This cold cost matters during large attachment or hot-apply
changes and is not included in the steady-state speedups.

[Generated-C comparisons](tick-update-c-results.json) cover counters and component
reads/writes at 64, 512 and 4,096 entities. They include world clock setup and
scheduler pumping. All modes produce identical checksums. C bodies are emitted
from the same lowered program and installed only in the benchmark's quiescent
program. GENERATED_C_UPDATE uses the reference C outer loop; SLJIT_UPDATE uses
the emitted native loop. SLJIT_C_UPDATE isolates the outer-loop compiler while
keeping SLJIT bodies. The C outer loop itself is handwritten reference code.

| Workload | Entities | Native loop + SLJIT ns/entity | C loop + SLJIT ns/entity | C loop + generated C ns/entity | SLJIT / generated C |
| --- | ---: | ---: | ---: | ---: | ---: |
| counter | 64 | 4.03 | 5.25 | 3.78 | 1.06x |
| counter | 512 | 8.30 | 8.32 | 8.42 | 0.99x |
| counter | 4,096 | 10.04 | 10.74 | 10.04 | 1.00x |
| component | 64 | 8.67 | 11.35 | 8.54 | 1.01x |
| component | 512 | 11.49 | 12.71 | 12.70 | 0.91x |
| component | 4,096 | 15.18 | 15.85 | 15.70 | 0.97x |

The advisory 1.5x ratio is evaluated on these fixtures. It does not establish
that arbitrary graphs or full Arena gameplay match handwritten C.

## Rendered Scene regression

[Three paired rendered Arena3D trials](tick-update-crowd-results.json) retain the
authored 40-enemy cap, moving without firing, at 1920x1080 over 1,800 fixed ticks.
Both executables explicitly enable detailed profiling for this comparison.
It therefore measures the prepared instrumented path and does not include the
additional benefit of disabling detailed timers by default.

Median behavior means were 0.160 -> 0.161 ms and simulation means were
0.334 -> 0.338 ms. Median paired differences were -0.016 and -0.017 ms, but the
ranges overlap substantially. These runs do not establish a reliable new
whole-scene CPU improvement. Wall p50 was 8.233 -> 8.311 ms and p95 was
9.271 -> 9.270 ms, consistent with presentation remaining the frame limit.
Gameplay and sampling assertions passed. This is a regression check at 40
enemies, not large-population Scene acceptance.

## Remaining cost and next experiments

At 20,000 enemies, the median of three per-run phase p50 values was 2.35 ms
updating and 0.56 ms pumping for SLJIT, and 11.88 and 0.71 ms for bytecode.
Percentiles of separate phases need not sum to the percentile of the total.
The native total p50 varied from 2.89 to 3.78 ms between trials. Use paired
comparisons and the raw ranges when evaluating a later storage change.

The per-enemy cost rises with population after event dispatch is removed.
That is a reason to investigate storage locality, not proof that an ECS library
replacement would solve it. The next useful comparisons are:

- Place frequently accessed instance metadata and numeric state in contiguous
  storage, and compare against the current per-instance heap allocations.
- Specialize repeated ordered body patterns, reducing indirect calls without
  changing cross-entity dependencies or random/effect order.
- Track pending activations directly so the pump need not scan every attachment
  when almost all updates complete synchronously.
- Measure full Scene physics, contacts, spawning, destruction and rendering at
  larger populations. The isolated 20,000-enemy result is not playable-scene
  acceptance.

Standalone System documents still need Scene scheduling and an explicit Convert
to System editor action. Those should share the behavior compiler, preserve
per-entity state and expose why a graph cannot be promoted. Automatic Tick
optimization already works without requiring authors to perform that conversion.
Whole-function operation coverage is unchanged by this scheduler change.

## Validation

Native `make test` and `make build` pass. Parity tests exercise both outer loops,
independent variables, cross-entity dependency order, service rebinding, queued
waits/cancellation, hot apply, pooling, entity growth/deletion/slot reuse and
custom profiling callbacks. Host-call failure snapshots match the oracle,
including partial writes, call stacks and independently owned cleanup storage.

The Arena Scene smoke runs both SLJIT and BYTECODE through firing, simulation and
restart, with 63 live instances and zero oracle calls. It asserts prepared
execution and native outer-loop execution with profiling disabled. The rendered
Performance-panel test checks visibility, freeze/resume, populated timing rows,
and restoration of native iteration when detailed profiling is inactive.

The native no-JIT test reports UNAVAILABLE and rejects execution without an
interpreter fallback. `make web` builds the app and passes the portable backend
smoke, including the prepared outer loop and 20,001-entity component access,
with SLJIT absent. This smoke does not run a large Arena workload in a browser.

The [catalog audit](tick-update-audit.txt) remains at 21/33 whole event entries,
24/31 effectful functions, 39/39 pure functions and 4/4 library functions. Its
nonzero exit is expected because full whole-function coverage remains open.
Raw timing results retain binary hashes, content hashes and trial logs.

Reproduce with `make test`, `make build`, `make web`, and
`cmake --build build-nojit --target check-behavior-backend`. Focused native checks
are `build/test_suite --tick-batch`, `--whole-data`, `--whole-nearest`,
`--compiled-arena`, and `--behavior-profile-ui`. Set
`CBLUEPRINT_BENCH_NO_PROFILE=1` for the native outer-loop Arena assertion.
Benchmark drivers are `tools/bench_arena_scale.py`, `tools/bench_ticks.py`, and
`tools/bench_arena_crowd.py`; use optimized binaries with sanitizers disabled.
