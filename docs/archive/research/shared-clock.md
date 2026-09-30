# Shared behavior clocks and remaining execution costs

The behavior world now owns one simulation clock. Attached instances borrow it,
so changing time or delta time no longer writes every attachment. This applies
to native system loops, prepared bytecode, ordinary activations and parallel
workers. Independent instances retain local clocks for editor previews and
standalone execution. Pool release snapshots and detaches a borrowed clock.

An activation still captures its start time and delta time. Wait readiness and
Scene Rhythm continue to observe the current instance clock. Workers only read
the world clock; the owner advances it between executions after joining workers.
No authored asset changes or opt-in settings are required.

The implementation starts from `a055888c`, separately from the material-world
work in progress. The comparison executable was built from that same commit,
with identical dependencies and Release flags, before changing runtime code.
Measurements were taken on an Apple M4 Pro with 14 CPU cores and 48 GiB RAM,
using Homebrew Clang 23.1.1. Native task execution used eight workers.

## Paired clock measurements

Five alternating-order pairs, Release without sanitizers or profiling, 256
measured frames after eight warmups. Actual ArenaEnemy assets run with fixed
positions, all five attachments and inactive feedback. Physics and rendering
are excluded. Each table entry is the median of trial medians.

| Backend | Enemies | Before complete behavior work | After | Reduction |
| --- | ---: | ---: | ---: | ---: |
| SLJIT | 1,000 | 0.053 ms | 0.039 ms | 26.4% |
| SLJIT | 5,000 | 0.175 ms | 0.130 ms | 25.7% |
| SLJIT | 20,000 | 1.118 ms | 0.591 ms | 47.1% |
| Bytecode | 1,000 | 0.461 ms | 0.446 ms | 3.3% |
| Bytecode | 5,000 | 2.371 ms | 2.318 ms | 2.2% |
| Bytecode | 20,000 | 10.120 ms | 9.616 ms | 5.0% |

At 20k, native complete-work P95 fell from 1.350 to 0.739 ms. The separate clock
phase fell from 0.337 ms to below the fixture's timer resolution. Avoiding writes
also changes cache traffic, so total improvement is not limited to the separate
clock timer. This is a local behavior benchmark, not a whole-frame speedup.
All paired native/bytecode checksums match and execute without interpreter calls.
[Raw measurements and executable/content hashes](shared-clock-scale-results.json).

## C comparisons

The existing Tick benchmark passes its enforced 1.5x native/generated-C gate
across counter, component, integer-array, string-array, name-array and
struct-array workloads at 512 and 4,096 entities. Three trials use 64 measured
frames. Eleven of twelve native/generated-C ratios are between 0.966 and 1.114;
the 4,096-entity name-array case is 1.453 and remains an optimization lead.
Compiled modes retain zero warmed VM scratch allocations, runtime stores and
arena backing-block growth. This does not count every allocation.
[Generated-C comparison](shared-clock-tick-results.json).

The independent Arena C loop still has a material advantage. At 20k entities,
median native update time is 0.637 ms and C is 0.213 ms. The median of per-trial
ratios is 2.882. That reference prebinds component pointers and does one nearest
search per entity; authored execution currently does two. It covers fixed
positions and inactive feedback, excluding waits, structural changes, physics
and rendering. Every velocity is compared outside timing. This identifies more
work to remove before claiming near-C performance for ordinary authored scenes.
[Independent-C comparison](shared-clock-reference-results.json).

## Rendered combat scene

Three alternating-order pairs run Arena3D at 1280x720 for 240 measured frames
after 30 warmups, including all attachments, physics, firing, kills and XP.
Population is prefilled and player health is raised to retain the sample window.
The runner checks combat traces, restart, native parallel execution and absence
of interpreter calls. Paired checksums and combat traces match.

| Enemies | Behavior P50 before / after | Wall P50 before / after | Wall P95 before / after |
| --- | ---: | ---: | ---: |
| 1,000 | 0.218 / 0.209 ms | 8.396 / 8.361 ms | 8.885 / 8.856 ms |
| 20,000 | 3.048 / 2.541 ms | 13.858 / 13.723 ms | 27.948 / 27.649 ms |

At 20k, median behavior time improves 16.6%, while median whole-frame time
changes only 1.0%. Physics P50 varies from 7.016 to 7.492 ms in these pairs;
physics dominates the remaining frame cost and was not changed here. At 1k,
presentation waits dominate. The results support a behavior improvement, not a
large whole-frame gain. The phase P50s do not add up to a wall P50.
[Rendered-scene samples](shared-clock-scene-results.json).

## Tracy attribution

A separate RelWithDebInfo build with Tracy 0.14.1 captured a 10.05-second window
during a 1,800-frame rendered 20k-enemy combat run. The window includes 551
complete engine frames and 552 behavior updates. This is an instrumented sample
of the changed runtime, not another before/after measurement. The fixture calls
rendering outside the instrumented engine-frame scope, so that scope does not
represent the complete application frame.

| Scope | Calls | Mean per call | Share of behavior-update inclusive time |
| --- | ---: | ---: | ---: |
| Behavior update | 552 | 1.766 ms | 100% |
| Validate parallel Tick rows | 552 | 0.772 ms | 43.7% |
| Prepare Tick schedule | 177 | 0.850 ms | 15.4% |
| Prepare parallel behavior queries | 552 | 0.106 ms | 6.0% |
| Parallel behavior execution | 552 | 0.316 ms | 17.9% |

Schedule repair is intermittent: its contribution averaged across all behavior
updates is 0.273 ms. The parallel-execution scope measures the caller's elapsed
time through submission and joining; worker durations overlap. Physics averages
11.940 ms inside a 15.731 ms engine-frame scope in this later combat window.
Task waits can execute queued work and overlap other workers; summing worker
durations would overstate time available to recover from a frame.
[Selected zone statistics and source locations](shared-clock-tracy-results.json).

## Next implementation work

The [parallel-validation follow-up](parallel-validation.md) moves checks for
large spans onto workers, preserving the barrier before behavior execution.
The original attribution below predates that follow-up; removing checks through
proofs remains open.

1. **Remove repeated serial validation where revisions prove it safe.**
   `game_behavior_parallel_run` validates every row and refreshes bindings before
   workers run kernels with their own row guards. This is the largest measured
   behavior scope. Move stable facts into schedule/span preparation; keep checks
   required for entity destruction, service changes, hot apply, breakpoints and
   errors. Validate immediate reads after writes and error ordering across chunk
   boundaries before changing when a guard runs.
2. **Reduce schedule repair during combat churn.** The existing incremental
   repair still accounts for 15.4% of behavior-update time in this trace. Measure
   rows and spans rebuilt per spawn/destroy event before changing the repair
   representation; retain unchanged spans and bindings whenever possible.
3. **Prepare only queries the scheduled functions need.**
   `game_entities_parallel_begin` currently clears category lists and scans
   transform/collider entities for every eligible parallel run. Compiler read
   summaries can identify when no query snapshot is needed. Reusing a snapshot
   across runs needs explicit invalidation for transform, collider, membership
   and structural writes. Its measured preparation cost is 6.0% here.
4. **Eliminate redundant searches and expose their scaling to authors.** The
   independent C benchmark does one nearest search per entity while authored
   Arena behavior does two. First measure candidate visits and prove that the
   queries have identical inputs with no intervening writes. Reuse equivalent
   reads within that boundary; investigate spatial indexing only where candidate
   counts show a need. Report repeated world scans and lost batch eligibility at
   the responsible graph nodes. This addresses algorithm cost beyond compiler
   throughput.
5. **Attribute the name-array gap.** Its 1.453x generated-C ratio is the largest
   microbenchmark gap. Separate copying/ownership work from emitted loop and
   dispatch overhead before selecting a change.

For application frame time, profile physics pair generation, sweeps, solving and
writeback next. The current trace establishes physics as a large enclosing
scope, but does not isolate which physics operation is responsible.

## Acceptance

Clock tests cover two instances borrowing one clock, independent instances and
worlds, changing delta time, waits, pooled reuse and native parallel execution
with 4,097 instances. Existing tests cover pause, hot apply, event ordering,
inspection, failures and native/bytecode/oracle parity. Generated C system loops
use the same clock contract as native emission.

Release and Tracy builds pass. The full Debug ASan/UBSan `make test` passes,
including generation, reflection, reload, development file watching, MCP,
backend smoke, runtime tests, scene documents, the whole-function audit and a
headless application smoke run. macOS file watching required running that suite
outside the command sandbox.
The Emscripten `check-behavior-backend` target also passes under Node, including
portable behavior execution, pending queues, clocks and physics runtime checks.
All 15 changed C/header files pass Clang 23's formatting check. Repository-wide
`make check-format` reports macro alignment in unchanged `shader_nodes.c` and
`audio.c`; both failures reproduce against the base commit with that formatter.
CI uses Clang 22 for formatting.

Reproduce the performance checks from a worktree with a Release comparison
executable saved before applying this change:

```sh
python3 tools/bench_arena_scale.py --before build-release/test_suite-before-shared-clock --after build-release/test_suite --counts 1000 5000 20000 --frames 256 --trials 5 --clocks --output /tmp/shared-clock-scale.json
python3 tools/bench_ticks.py --build-dir build-release --entities 512 4096 --frames 64 --trials 3 --enforce-native-ratio --output /tmp/shared-clock-ticks.json
python3 tools/bench_arena_reference.py --binary build-release/test_suite --counts 1000 20000 --frames 256 --trials 3 --output /tmp/shared-clock-reference.json
python3 tools/bench_arena_scene_scale.py --before build-release/test_suite-before-shared-clock --after build-release/test_suite --counts 1000 20000 --frames 240 --trials 3 --combat --expect-parallel --output /tmp/shared-clock-scene.json
```

For attribution, run `TRACY_PORT=8097 CBLUEPRINT_BENCH_COMBAT=1
build-tracy/test_suite --bench-arena-scene-scale 20000 1800 0`, connect a matching
Tracy client to localhost port 8097 and record a window after warmup. Compare
optimized timings with Tracy disabled, separately from builds and test runs.

## Behavior performance by default

Authors should write behaviors without manually selecting an execution backend
or reorganizing their graphs into systems. The compiler and runtime should form
system loops, share stable query work and partition independent execution when
the available dependency information proves that doing so preserves behavior.

Two C comparisons answer different questions. Generated C from the same lowered
body measures compiler and dispatch overhead. An independently written C loop
also exposes unnecessary work in the lowered algorithm. Both matter; a good
generated-C ratio alone does not establish that authored behavior is close to
the best ordinary C implementation.

Protecting authors also requires identifying expensive algorithms. Diagnostics
should connect world scans, repeated searches, container copies and lost batch
eligibility to the responsible graph operations. Runtime summaries should report
actual execution and candidate counts. Compiler transformations must retain
ordering, ownership, errors and immediate reads after writes. Fidelity changes
such as lower update rates require an authored policy.
