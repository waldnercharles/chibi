# Ordered synchronous Tick execution

Eligible engine broadcasts now execute whole compiled bodies directly, in
attachment order. Successful unobserved events do not allocate or schedule an
activation. The portable backend shares reusable VM scratch across instances of
the same behavior entry. Native bodies enter SLJIT directly. The scheduler skips
idle attachments and avoids submitting a job when no activations remain.

Eligibility is inferred from the lowered instructions. A parameterless event
must be synchronous and contain only explicitly allowed value, component,
variable and control operations. Unknown operations, host commands, queries,
waits and observed events retain ordinary compiled activations. Existing queued
activations also prevent batching. The public single-instance dispatch API still
returns an activation. External profiling callbacks retain ordinary execution;
the engine's recording-only profiler supports batches.
Publication indexes eligible unique entries so ineligible broadcasts can skip
additional event and function lookup. The index changes with its immutable program
generation; renamed and duplicate events retain ordinary dispatch diagnostics.

Component writes can target another entity. The optimization preserves attachment
order, including dependencies between consecutive entities; it does not group by
asset, reorder entities or run them in parallel. Each instance keeps its own
variables. Failure creates an inspectable activation from the completed portion
of execution, preserving source, steps and call stack without repeating writes.
Hot apply invalidates cached scratch; compatible variables survive. Pool reuse
resets state and runs Begin before Tick.

## Controlled measurements

Reproduce with an optimized native build, no concurrent builds or benchmarks:

```sh
python3 tools/bench_ticks.py --build-dir build-performance \
  --output docs/research/tick-batch-results.json
```

The fixtures perform a persistent counter update through a nested function and,
in the component case, an additional Get/Set of an entity's component field. Each
trial includes the engine broadcast, entity iteration, timing update and scheduler
pump. Five trials rotate six execution modes at 64, 512 and 4096 entities. Each
mode warms up for eight frames, then measures 128 frames. All entity values and
checksums must match. Profiling is disabled.

Median nanoseconds per entity, including broadcast and pump:

| Fixture | Entities | SLJIT activation | SLJIT batch | Bytecode activation | Bytecode batch | Generated C batch |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Counter | 64 | 80.322 | 18.433 | 109.985 | 32.471 | 18.188 |
| Counter | 512 | 116.913 | 18.646 | 136.398 | 32.578 | 18.005 |
| Counter | 4096 | 160.582 | 21.660 | 192.139 | 34.580 | 21.080 |
| Component | 64 | 91.064 | 29.785 | 247.192 | 120.972 | 32.593 |
| Component | 512 | 125.015 | 30.472 | 257.874 | 121.490 | 31.189 |
| Component | 4096 | 174.202 | 32.951 | 346.909 | 130.304 | 32.923 |

Generated C comes from the same lowered program and uses the identical engine
wrapper. A test-only entry replacement installs it on a fresh, quiescent program,
verifies that publication retained the entry, then restores the owned JIT pointers
before freeing it. This isolates backend differences without adding a production
C backend. It does not compare with a handwritten C ECS loop that eliminates the
engine wrapper and language checks.

Native batching is 3.1–7.4x faster than ordinary activations in these fixtures.
Bytecode improves 2.0–5.6x. Native batch/generated-C ratios range from 0.914 to
1.036; the 1.5x advisory target is met. Ratios below 1 should be treated
as timing variation, not proof of superior generated code. Raw samples, compiler
and binary hashes are in [the results](tick-batch-results.json).
The [initial measurements](tick-batch-initial-results.json) precede the publication
index. The change reduced the 512-entity counter batch median from 20.889 to
18.646 ns/entity in separate runs; most of the improvement still comes from
eliding activation setup.

After warmup, the bytecode batch reports zero VM scratch initializations, compared
with one per ordinary invocation. Native reports zero in both modes. This metric
counts scratch initialization, not every allocator call. Runtime-store creation
is also zero after warmup in both ordinary and batched execution because ordinary
activations already pool their stores.

The scripts retain strict correctness and complete-measurement checks. Ratio
misses are reported and optionally enforced with `--enforce-native-ratio`.

## Gameplay scope

The Arena comparison toggles `CBLUEPRINT_BENCH_NO_BATCH=1` on the same optimized
binary. `CBLUEPRINT_BENCH_NO_PROFILE=1` disables attribution overhead. The native
smoke asserts batch execution when enabled, the selected backend, firing and
restart, and zero oracle calls. Only 358 broadcasts qualify over 180 frames in
this scene. Nine of 22 live event entries have whole-function bodies; catalog
coverage remains 13 of 33 events.

The initial 15 paired trials for each profiling setting found unchanged behavior
medians and noisy tails, including higher native tails. A further 30 pairs retained
a small native tail increase without profiling, motivating the publication index.
After indexing, another 30 pairs per setting found the following medians across
runs (microseconds of behavior time per frame):

| Profiling | Backend | Ordinary p50 | Batch p50 | Ordinary p95 | Batch p95 |
| --- | --- | ---: | ---: | ---: | ---: |
| Off | SLJIT | 25.5 | 25.0 | 67.5 | 65.0 |
| Off | Bytecode | 34.0 | 34.0 | 70.0 | 68.0 |
| On | SLJIT | 27.0 | 27.0 | 66.0 | 68.0 |
| On | Bytecode | 36.5 | 36.0 | 71.0 | 71.5 |

The no-profile native tail increase did not persist in this sample. Profiled
native tails are still slightly higher; the median paired p95 difference is
4.5 microseconds, while the no-profile paired difference is zero. These small,
variable timings establish neither a meaningful Arena speedup nor a clean tail
improvement. Further gameplay performance acceptance remains open as coverage
expands. Retained evidence: [initial](tick-batch-arena-results.json),
[first repeat](tick-batch-arena-repeat-results.json),
[indexed implementation](tick-batch-index-arena-results.json).

This is an ordered C outer loop over whole compiled bodies. Compiling the outer
loop, eliminating repeated context binding and proving disjoint component effects
for ECS-style iteration remain separate optimization opportunities. Broader Tick
coverage is required before most Arena behavior work can benefit.

## Verification

The focused `--tick-batch` test compares ordinary and batched execution on SLJIT,
bytecode and the explicit oracle. It covers independent variables, cross-entity
write dependencies, profiling, debugger holds, waits, partial-write errors,
step-budget exhaustion, missing returns, hot apply and pooling. It also checks
that host commands, event payloads and breakpoints exclude batching before any
write occurs. Generated-C execution is checked under ASAN/UBSAN. The portable
backend smoke uses the same batch fixture and requires no oracle calls or new
activation runtime stores.

Final validation passed: `make test`, `make build`, `make web`, the native
no-SLJIT rejection target, the generated-C comparison under ASAN/UBSAN, changed-C
format checks and `git diff --check`. The Emscripten smoke reports BYTECODE batch
execution with no activation runtime-store creation or oracle calls. Arena firing,
simulation and restart passed on both compiled backends throughout the paired runs.
Full whole-function coverage and broad gameplay performance acceptance remain open.
