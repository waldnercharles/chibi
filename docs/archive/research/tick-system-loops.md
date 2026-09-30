# Tick bodies inside native system loops

This report records the initial homogeneous-loop implementation and its
measurements. [Repeated interleaved Tick systems](tick-sequences.md) now extend
it to sequences of different graph bodies while preserving attachment order.

Parameterless synchronous Tick entries now receive a native system kernel when
the immutable behavior program is published. The kernel embeds the same lowered
body inside an ordered row loop: one native entry/prologue covers a consecutive
span, with no separate behavior-body ABI call per row. Local functions already
inlined by lowering remain in that body. Explicit native host/value helpers
remain calls. No graph interpretation is introduced.

The scheduler retains attachment order and shared prepared bindings. It caches
compatible consecutive spans when the schedule changes, then invokes a system
kernel once per span. The existing native prepared loop covers the gaps without
any per-row system-selection work. Singleton rows use their prepared body.
The kernel stops at a different function or a failed row guard; it can continue
across compatible bindings of the same function without reordering them.

This boundary selection matters for mixed scenes. The first dispatcher checked
for compatible spans on every row; Arena exercised only two system rows per
frame and paid that check on thousands of other rows. Selecting cached span
boundaries outside the native row loops removes that overhead.

Before each row, the shared native guard verifies entity generation/liveness,
instance liveness, backend, program generation, service revision, active
activations, tracing and profiling. The body gets that instance's variables,
owner, clock and managed-value ownership. Temporary arenas are reused. A failed
body returns the failing row and its existing source/frame diagnostic, preserving
partial effects exactly once. It never retries a body that produced effects.
Bounded fast bodies retain their budget/depth guard; a rejected fast guard can
use the checked compiled body. Observer/wait cases retain compiled activations.

Simple bodies keep loop state in saved registers. Bodies needing those registers
across host calls use four stack slots shared by the span. The initial all-stack
version regressed simple bodies in paired measurements; it was revised before
acceptance. Native generation is owned/freed with the immutable function.
WASM retains the prepared portable bytecode loop; these native kernels are not
an interpreter fallback or a new portable format.

Runtime inspection exposes `tick_system_compiled`. `game_behavior_last_update`
reports `system_loop`, counting entries actually attempted inside kernels,
separately from all prepared/native-loop entries. Compilation availability does
not promise that the current attachment order forms a profitable span.

## Comparison and limits

`behavior_function_write_system_c` emits the same lowered body plus an ordered C
kernel with a direct body call in the same translation unit. The benchmark
compiles it with O3 and compares it through the same scheduler. A test-only
replacement installs/restores generated C pointers on quiescent programs. This
is a compiler comparison; it is not a complete standalone Scene export pipeline.

`tools/bench_ticks.py` rotates backend order, pairs before/after executables,
checks snapshots and warmed allocations, and compares native kernels against
`GENERATED_C_SYSTEM_UPDATE`. Ordinary activations, prepared C dispatch, bytecode
and oracle remain explicit comparison modes. Scalar counter/component fixtures
exercise nested calls and instance/component writes. Array/string/name/struct
fixtures exercise managed ownership. The ratio gate is advisory by default.

These loops preserve sparse ECS storage and existing attachment order. They do
not reorder interleaved behavior assets into archetype groups, vectorize the
body, fuse different graphs, or parallelize dependent writes. Arena attaches
multiple Tick behaviors per enemy, so large consecutive homogeneous spans are
not guaranteed. Whole-scene measurements must be assessed separately from the
homogeneous-kernel benchmark.

## Measured results

Three paired trials, 256 frames per sample, 512 and 4,096 entities, Release/O3,
without sanitizers or profiling. The baseline already has prepared native Tick
dispatch and the container changes. Times include the update/pump loop used by
the fixture, not compilation or initial attachment.

At 4,096 entities, median nanoseconds per entity:

| Workload | Previous prepared loop | Native system loop | Generated-C system loop |
| --- | ---: | ---: | ---: |
| Counter | 6.225 | 6.034 | 5.843 |
| Component read/write | 11.059 | 10.831 | 10.695 |
| Integer arrays | 237.359 | 234.144 | 230.625 |
| String arrays | 369.565 | 355.365 | 353.623 |
| Name arrays | 349.604 | 348.934 | 350.063 |
| Struct arrays | 2,089.807 | 2,131.288 | 2,194.316 |

The native/generated-C ratios across both populations are 0.971–1.085. These
are modest changes relative to the already prepared Tick loop; several managed
workloads have overlapping trial ranges and no reliable speedup. Copying still
dominates those bodies. All backend/executable checksums match; warmed compiled
batches/updates allocate no VM scratch, runtime stores or arena backing blocks,
and never enter the oracle.

Raw samples, ranges, paired deltas and executable/generated-source hashes:
[tick-system-results.json](tick-system-results.json).

```sh
python3 tools/bench_ticks.py --before build-performance/test_suite-before-system-loops \
  --entities 512 4096 --frames 256 --trials 3 \
  --output docs/research/tick-system-results.json
```

## Arena outcome

Three paired runs at each population, 240 measured frames after 30 warmup
frames, at 1280×720 with profiling off. These render the Arena Scene, aim/fire,
kill enemies, collect experience and restart. Median phase times in milliseconds:

| Enemies | Behavior before | Behavior after | Frame before | Frame after |
| --- | ---: | ---: | ---: | ---: |
| 1,000 | 0.249 | 0.254 | 8.318 | 8.382 |
| 5,000 | 1.314 | 1.310 | 9.373 | 9.586 |

There is no appreciable Arena speedup from this pass. Per-run behavior p50
ranges overlap: 0.240–0.269 before versus 0.228–0.257 after at 1,000; 1.286–1.347
before versus 1.291–1.323 after at 5,000. Frame/physics changes also vary between
pairs. Only 480 entries enter system kernels over the 240 measured frames,
versus 478,686 and 2,398,716 prepared Tick entries respectively. Most Arena
enemy attachments remain interleaved. The cached span boundaries prevent that
small group from adding a selection check to every enemy row.

Every run matches the before executable's position checksum and per-frame
combat trace, reports zero oracle calls and no retained behavior failures,
and passes the restart reset checks. Raw samples and hashes:
[tick-system-scene-results.json](tick-system-scene-results.json).

```sh
python3 tools/bench_arena_scene_scale.py \
  --before build-performance/test_suite-before-system-loops \
  --counts 1000 5000 --frames 240 --trials 3 --combat \
  --output docs/research/tick-system-scene-results.json
```

## Verification

`make build`, the complete `make test` suite, `make web` (including executed
bytecode smoke tests with SLJIT absent), and the native no-JIT
`check-behavior-backend` target pass. The no-JIT target reports `UNAVAILABLE`
and asserts zero interpreter fallback. Native checks use ASan/UBSan; performance
measurements use a separate Release build without sanitizers. `git diff --check`
passes.

Regressions cover ordered execution and cross-entity component dependencies,
observation/profile guards, service changes, stale entities, hot apply, managed
values, waits, source failures and budgets. Direct kernel tests stop at a failed
middle row without rerunning partial effects or executing later rows. Runtime
counters verify that eligible world updates enter system kernels; the portable
backend retains its prepared bytecode path.
