# Parallel behavior validation and the independent C comparison

This continues the [shared-clock work](shared-clock.md) from `99597376`.
Tracy attributed 43.7% of behavior-update time to validation on the calling
thread. Each row was checked before worker submission, then checked again by
its compiled system loop.

Large eligible spans now validate their existing entity chunks on the shared
task pool. Every validation task joins before query preparation or behavior
execution. If any row is blocked, the complete span returns to the ordinary
scheduler before any behavior has run. Bindings remain private to their chunk,
and entity, service, backend, observation and activation guards remain intact.

Validation stays on the calling thread below 8,192 rows. The initial trial with
unconditional worker submission added about 7 microseconds at 2,000 Arena Tick
rows, while larger populations benefited. The cutoff avoids that extra dispatch
for small spans and the existing 4,096-row generated-C fixtures. It does not
change the separate threshold for parallel behavior execution.
[Initial cutoff experiment](parallel-validation-cutoff-results.json).

This reduces elapsed time through concurrency. It does not reduce the number of
row checks, and it adds a task submission and join for large spans. Eliminating
checks through lifetime and dependency proofs remains separate work.

## Paired measurements

Five alternating-order pairs compare the shared-clock executable with this
change, using the same Release flags, dependencies, assets and input. The host
is an Apple M4 Pro with 14 cores and 48 GiB RAM, Clang 23.1.1 and eight runtime
workers. Each run measures 256 frames after eight warmups. Values below are
medians of trial medians; P95 columns are medians of per-trial P95s.

| Backend | Enemies | Complete behavior P50 before / after | P95 before / after |
| --- | ---: | ---: | ---: |
| SLJIT | 1,000 | 0.039 / 0.039 ms | 0.051 / 0.058 ms |
| SLJIT | 5,000 | 0.125 / 0.114 ms | 0.160 / 0.152 ms |
| SLJIT | 20,000 | 0.670 / 0.422 ms | 0.888 / 0.551 ms |
| Bytecode | 1,000 | 0.452 / 0.453 ms | 0.498 / 0.495 ms |
| Bytecode | 5,000 | 2.305 / 2.323 ms | 2.470 / 2.456 ms |
| Bytecode | 20,000 | 9.619 / 9.668 ms | 10.139 / 10.159 ms |

Native complete behavior work improves 37.0% at 20k and 8.8% at 5k. The 1k
median is unchanged; its measured P95 is 7 microseconds higher. Bytecode does
not use the changed worker path and varies by less than 1% in the medians.
Every checksum matches and interpreter call counts remain zero. These runs
hold enemy positions fixed with all five attachments and inactive feedback;
they exclude physics and rendering.
[Raw paired samples and hashes](parallel-validation-scale-results.json).

Five trials of the independent-C comparison give a 20k native median of
0.431 ms and C median of 0.224 ms. The median of per-trial native/C ratios is
1.916. This includes validation and automatic native worker execution. The
reference remains single-threaded, and every velocity is compared outside the
timed interval. At 1k the ratio remains 3.364, where fixed overhead is more
visible. [Reference samples](parallel-validation-reference-results.json).

The existing 1.5x native/generated-C gate passes for counter, component and
integer/string/name/struct-array Tick workloads at 512 and 4,096 entities.
Three trials of 64 frames give ratios from 0.986 to 1.176. The compiler and
generated bodies are unchanged by this runtime optimization; these results
verify the gate rather than attributing differences in individual kernel ratios
to parallel validation. [Generated-C results](parallel-validation-tick-results.json).

## Rendered combat

Arena3D runs at 1280x720 with 240 measured frames after 30 warmups, held fire,
kills, XP and restart. Each round has three alternating-order before/after
pairs. Checksums, combat traces, native parallel execution and zero interpreter
calls match. Player health is raised to retain the sample window.

| Round, 20k enemies | Behavior P50 before / after | Physics P50 before / after | Wall P50 before / after | Wall P95 before / after |
| --- | ---: | ---: | ---: | ---: |
| Initial | 3.615 / 2.241 ms | 14.065 / 9.639 ms | 22.388 / 15.867 ms | 37.665 / 31.859 ms |
| Repeat | 2.554 / 1.952 ms | 7.159 / 7.106 ms | 13.526 / 12.665 ms | 28.515 / 26.664 ms |

Physics varied substantially during the first round. A subsequent process
snapshot showed a busy virtual machine, window server and filesystem indexers;
this is evidence of competing host work, not proof that it explains every timing
change. The repeat has much closer physics medians and shows a 23.6% behavior
improvement and a 6.4% whole-frame improvement. Both rounds are retained.
Across all six pairs, behavior P50 improves every time, by 0.490 to 1.374 ms;
the median paired reduction is 0.600 ms. The much larger initial wall-time
differences should not be attributed entirely to behavior validation.

The initial 1k runs also have substantial host timing variation: behavior P50
is 0.255 / 0.286 ms and wall P50 is 8.520 / 8.387 ms. Validation remains serial
at that population. These captures do not establish a small-scene improvement.
[Initial samples](parallel-validation-scene-results.json),
[repeat samples](parallel-validation-scene-repeat-results.json).

## Tracy confirmation

Tracy 0.14.1 captures compare the saved shared-clock build with the changed
RelWithDebInfo build in separate 1,800-frame, 20k-enemy combat runs. Connections
begin after first fire, so the captured windows have slightly different frame
counts and include restart. These are attribution samples, not an uninstrumented
speedup estimate.

| Scope | Before mean | After mean |
| --- | ---: | ---: |
| Validate parallel Tick rows | 0.793 ms | 0.245 ms |
| Parallel behavior execution | 0.352 ms | 0.321 ms |
| Behavior update | 1.840 ms | 1.259 ms |

The changed capture records 14,328 validation chunks across eight threads for
1,791 parallel spans. Validation's caller-side elapsed time is lower. Summed
worker-zone time overlaps and includes scheduling effects; it is not a reduction
in aggregate CPU work. The existing generated row guards still run before each
body. [Capture statistics and hashes](parallel-validation-tracy-results.json).

## What the independent C loop does differently

The [reference implementation](../../test/runtime/arena_reference.h) is a
specialized update for the fixed-position, inactive-feedback benchmark. It is
single-threaded. The authored runtime already distributes eligible behavior
execution across workers, so their timing ratio compares wall time, not equal
per-core work.

| Work | Independent C | Current compiled behaviors | Route to similar performance |
| --- | --- | --- | --- |
| Component access | Resolves transform, movement and velocity pointers once after spawning | Shares prepared column bindings, then checks entities and resolves accesses during execution | Bind stable component addresses for a proven interval; refresh after storage or structural changes |
| Nearest target | Builds one filtered target list per frame and searches it once per enemy | Builds category lists, checks candidate handles and performs two searches on the successful pursuit path | Reuse equivalent reads across control-flow blocks when inputs and world dependencies are unchanged |
| Data passing | Uses typed local values and direct arithmetic | Packs and reads generic host-call slots around the nearest-query helper | Lower common queries into typed instructions or specialized helpers in the shared compiler representation |
| Feedback | Asserts the hit timer is zero and performs only movement work | Also executes the authored CombatFeedback Tick, which takes its inactive branch | Prove inactive paths or use explicit event-driven state without skipping active feedback semantics |
| Runtime state | Permits no structural changes and prebinds pointers for the entire run | Supports waits, debugging, service changes, hot apply, errors and entity lifetimes | Establish those conditions per span and retain a checked path when they do not hold |

Arena has one authored `Find Nearest Entity` node. Its `Found` output feeds a
Branch, and its position/distance outputs feed the successful velocity update.
The compiler clears its cached node outputs at each
[execution block](../../src/graph/compiler/behavior_function.c), so this becomes
two calls across the branch boundary. This is not an author duplicating a search.
Reusing the first result requires dominance and effect analysis: the first read
must execute on every path to the reuse, and no intervening write may change its
inputs or query dependencies. Loops, callbacks and resumed waits need particular
care.

Generated C and SLJIT consume the same lowered behavior representation. Changing
that representation to reuse reads or specialize queries can benefit both
backends. Merely exporting the existing representation as C retains its repeated
calls and runtime work. The generated-C benchmark measures how efficiently SLJIT
executes that representation; the independent loop exposes work the compiler
could avoid producing in the first place.

The reference checks resulting velocities outside timing. It does not establish
equivalence for active combat feedback, spawning, destruction, waits, debugging
or rendering. Its assumptions are useful candidates for compiler proofs, not
conditions that authors should have to manage themselves.

## Correctness

The added behavior-world test blocks the first, middle and last row of an
8,193-instance span by selecting bytecode for that instance. Each update must
fall back before effects, execute every instance exactly once, then resume
native parallel execution after the blocker is removed. Existing fixtures cover
managed scratch, failures, debug holds, hot apply, component reads, attachment
churn, service replacement and ordered writes across entities.

The full Debug ASan/UBSan `make test` passes. A separate ThreadSanitizer build
passes `--parallel-runtime` and `--whole-rhythm`, including shared component
reads and independent clocks, with no reported races. It uses global compiler
and linker `-fsanitize=thread` flags with `CHIBI_ENABLE_SANITIZERS=OFF`, because
the bundled EnkiTS sanitizer option hardcodes address/undefined sanitizers.
SLJIT machine code itself is not instrumented by compiler sanitizers.
The final Emscripten `check-behavior-backend` target passes under Node. Changed
C files pass formatting and `git diff --check` passes.

Reproduce the optimized comparisons with a saved `99597376` Release executable:

```sh
python3 tools/bench_arena_scale.py --before build-release/test_suite-before-parallel-validation --after build-release/test_suite --counts 1000 5000 20000 --frames 256 --trials 5 --clocks --output /tmp/parallel-validation-scale.json
python3 tools/bench_arena_reference.py --binary build-release/test_suite --counts 1000 20000 --frames 256 --trials 5 --output /tmp/parallel-validation-reference.json
python3 tools/bench_ticks.py --build-dir build-release --entities 512 4096 --frames 64 --trials 3 --enforce-native-ratio --output /tmp/parallel-validation-ticks.json
python3 tools/bench_arena_scene_scale.py --before build-release/test_suite-before-parallel-validation --after build-release/test_suite --counts 1000 20000 --frames 240 --trials 3 --combat --expect-parallel --output /tmp/parallel-validation-scene.json
```

The repeat scene round uses `--counts 20000`. Build and run tests separately
from timing; use Tracy builds only for attribution. For the traces, run each
Tracy executable with `--bench-arena-scene-scale 20000 1800 0` and
`CBLUEPRINT_BENCH_COMBAT=1`, then connect after first fire.
