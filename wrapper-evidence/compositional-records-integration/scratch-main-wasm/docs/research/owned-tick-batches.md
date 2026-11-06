# Owned values in prepared Tick execution

The subsequent [managed storage reuse](managed-reuse.md) removes repeated
persistent backing-block allocations and duplicate array copies. The measurements
below describe the earlier batching change.

Eligible synchronous Tick bodies now use the prepared execution path even when
they create arrays, copy managed arguments or store managed persistent values.
The shared lowered program records whether surviving instructions require
temporary storage. Each prepared binding owns a reusable arena, reset before
each row. The SLJIT outer loop conditionally calls the arena reset helper;
the reference C loop and bytecode path follow the same lifetime rule. Bodies
without temporary-value operations skip the reset call.

The synchronous broadcast path also reuses one arena per compiled batch. Both
paths retain their existing restrictions on waits, breakpoints, observation,
active queues and unproven effects. Managed persistent writes continue to take
instance-owned snapshots. Success creates no activation or graph runtime store.

A failed body can retain managed child arguments for later inspection. Failure
materialization transfers the batch arena to the captured activation and leaves
an empty reusable arena in the binding. It preserves prior writes and never
replays the event. This keeps the original values alive while other entities
reuse or destroy the batch. The captured context points at its own arena and
separate host banks.

The regression fixture dispatches nested array Append calls, stores Source,
Saved and Final arrays, and increments an independent run counter. It covers
integer, string, name and nested managed-struct arrays across instances. Tests
exercise native and portable batches, both prepared outer loops, ordinary
activations, oracle state, debug holds and hot apply. Failure checks read the
captured stack and managed inputs only after eight later executions reuse the
same binding and its banks are freed. The counter proves that error capture
does not execute the body twice.

Those checks exposed two earlier ownership gaps. Copying a value into an asset
graph now copies its nominal type name, including names inside array defaults.
Managed variables already own their values, so hot apply now compares the
recorded declared struct layout for scalar and array forms instead of rejecting
them as borrowed pointers. Compatible state survives generation replacement;
changed nominal types or recorded layouts require restart. Borrowed node scratch
still retains its existing compatibility restrictions.

The Tick benchmark now includes arrays of integers, strings, names and managed
structs alongside the existing scalar counter and component cases. It compares
ordinary SLJIT/bytecode activations, synchronous batches, prepared updates,
generated C and the explicit oracle. Generated C uses the same helper operations
and scheduler path, with the reference C outer loop. A complete standalone Scene
AOT export is still unfinished.

This change does not eliminate persistent managed allocations or redundant value
copies. The temporary arena removes repeated activation setup and reuses its
blocks; persistent Source, Saved and Final stores still allocate owned snapshots.
The allocation counters cover VM scratch and graph runtime stores, not every
heap allocation. Large playable scenes, recursive execution and remaining
language/inspection acceptance remain separate unfinished requirements.

## Measurements

[Five rotated trials](owned-tick-results.json) use 100 and 1,000 entities, 64
sampled frames and eight warmup frames per backend. Clang 23.1.1 Release builds
have sanitizers disabled. All twelve workload/count combinations meet the
advisory 1.5x native/generated-C target. At 1,000 entities:

| Tick workload | SLJIT activation ns/entity | SLJIT prepared ns/entity | Generated C prepared ns/entity | Activation/prepared speedup |
| --- | ---: | ---: | ---: | ---: |
| Integer array | 493.484 | 390.906 | 379.656 | 1.262x |
| String array | 749.016 | 626.859 | 629.719 | 1.195x |
| Name array | 781.047 | 616.844 | 614.703 | 1.266x |
| Managed struct array | 2403.484 | 2276.453 | 2302.766 | 1.056x |

The managed-array improvement is smaller because ownership copies and persistent
writes dominate the body. Bytecode activation/prepared speedups for these cases
are 1.176x, 1.142x, 1.165x and 1.014x respectively. Prepared bytecode for managed
struct arrays is 4.8% slower than its synchronous batch wrapper in this sample,
although both now avoid whole activation setup. This result does not establish
that prepared execution is always faster than an existing batch.

The existing scalar counter and component workloads remain in the measurement.
Their prepared native costs are 7.906 and 11.719 ns/entity at 1,000 entities.
These are comparisons within the current executable, not evidence of a new
speedup for scalar Tick bodies that were already eligible before this change.

[Seven paired small Arena runs](owned-tick-scene-results.json) keep native
behavior p50 at 0.007 ms and simulation p50/p95 at 0.013/0.027 ms. Native
behavior p95 changes from 0.022 to 0.021 ms. Bytecode behavior p50 changes from
0.017 to 0.018 ms, with its p95 unchanged at 0.031 ms. This extension does not
show an appreciable small-scene improvement.

[Five paired scaling trials](owned-tick-scale-results.json), with 256 sampled
frames per executable and profiling disabled, measure the actual ArenaEnemy
Tick bodies and all five attachments per enemy:

| Enemies | Native before ms | Native after ms | Bytecode before ms | Bytecode after ms |
| ---: | ---: | ---: | ---: | ---: |
| 100 | 0.005 | 0.005 | 0.047 | 0.046 |
| 1,000 | 0.053 | 0.053 | 0.481 | 0.471 |
| 10,000 | 0.575 | 0.570 | 4.799 | 4.888 |
| 20,000 | 2.522 | 2.416 | 12.371 | 12.408 |

At 20,000, native paired changes range from -0.229 to +0.298 ms, with a median
of -0.100 ms. That variation does not establish a dependable large-crowd gain.
The fixture excludes physics and rendering and holds positions fixed. Peak
native process memory after the change is about 203 MiB at that count; this
does not prove a playable scene at 20,000 enemies.

## Verification

Native `make test` passes with address/undefined-behavior sanitizers, including
the managed Tick regressions, scalar managed-state hot apply, editor/gameplay
checks and the complete authored catalog gate. The native application builds
and passes its headless smoke. Generated-C benchmark bodies match state and run
counts across all six workloads, with zero oracle calls on compiled paths.
`make web` builds the application and runs the shared owned-array Tick fixture
through actual bytecode with SLJIT absent. The native no-JIT smoke rejects core
execution and runtime capabilities with UNAVAILABLE and no interpreter fallback.
Targeted formatting, Python syntax and `git diff --check` pass.

```sh
build/test_suite --owned-ticks
python3 tools/bench_ticks.py --entities 100 1000 --frames 64 --trials 5 \
  --output docs/research/owned-tick-results.json
```
