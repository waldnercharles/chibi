# Reuse behavior reads across branches

This continues the [parallel validation work](parallel-validation.md) from
`2ba99cf9`. Arena's authored Enemy behavior has one nearest-query node: its
`Found` output chooses a branch, then position and distance determine velocity.
Clearing the compiler's per-block cache caused two searches on that path.

The shared compiler now reuses the first result in unobserved execution. A
contextual host data node can declare `reusable`: the same node and inputs have
stable outputs and errors while behavior and host state remain unchanged, with
no observable effects. Nearest query opts in. Ordinary host callbacks remain
barriers. This declaration belongs to native node implementations; authors do
not need to add a cache or manage invalidation.

The pass follows single forward predecessors. Branch successors inherit proven
results; joins and back edges stop the search. Variable/component writes,
random inputs, local frame changes, commands, unknown calls and waits stop
query-result reuse. The pass removes the duplicate call and its argument-bank
writes, redirects result storage and removes unused loads.

Checked world, owner, entity and component addresses also survive eligible
branches. Component field writes preserve those addresses, but fields are read
again. The pass never moves a lookup before its first checked access or carries
an address across a yield. It does not prebind rows across frames.

Inspection retains the original program. The optimized body keeps canonical
instruction positions for native labels, source errors and continuation
metadata; bytecode uses its compact form. SLJIT register allocation and C
emission consume the changed operands. Slots saved at real continuations retain
their definitions, allowing inspection to be enabled after a wait.

## Paired measurements

Five alternating-order pairs compare the saved `2ba99cf9` executable with
`1faf9cd6`, using identical assets, input and Release builds without sanitizers.
The host is an Apple M4 Pro with 14 cores and 48 GiB RAM, Clang 23.1.1 and eight
runtime workers. Runs measure 256 frames after eight warmups. Values are medians
of trial medians; P95 columns are medians of per-trial P95s.

| Backend | Enemies | Complete behavior P50 before / after | P95 before / after | P50 reduction |
| --- | ---: | ---: | ---: | ---: |
| SLJIT | 1,000 | 0.040 / 0.038 ms | 0.074 / 0.064 ms | 5.0% |
| SLJIT | 5,000 | 0.130 / 0.116 ms | 0.169 / 0.151 ms | 10.8% |
| SLJIT | 20,000 | 0.464 / 0.444 ms | 0.666 / 0.652 ms | 4.3% |
| Bytecode | 1,000 | 0.464 / 0.348 ms | 0.528 / 0.402 ms | 25.0% |
| Bytecode | 5,000 | 2.268 / 1.727 ms | 2.471 / 1.852 ms | 23.9% |
| Bytecode | 20,000 | 9.537 / 7.332 ms | 10.086 / 7.894 ms | 23.1% |

These runs include clocks, update scheduling and pumping, hold enemy positions
fixed, execute all five authored attachments with inactive feedback, and exclude
physics/rendering. Checksums match and interpreter calls remain zero. The 20k
median paired native reduction is 0.035 ms; the ratio of independent medians in
the table is a different statistic. [Repeat samples and hashes](branch-reads-scale-repeat-results.json).

The initial round had large native scheduling variation: 20k trial medians
ranged from 0.762 to 3.820 ms before and 0.614 to 2.534 ms after. A process
snapshot showed another running game, a separate WebAssembly optimizer, a busy
virtual machine and desktop/indexing activity. Both rounds are retained; the
initial native measurements do not establish a reliable speedup. Bytecode
improved in both rounds. [Initial samples](branch-reads-scale-results.json).

Five independent-C trials give 20k native update P50 of 0.434 ms versus 0.238 ms
for the specialized C loop, with median per-trial ratio 1.823. At 1k the ratio
is 3.455. Native execution is parallel and the reference is single-threaded;
this compares elapsed time, not work per core. The reference's fixed-position,
inactive-feedback assumptions still limit the comparison.
[Reference samples](branch-reads-reference-results.json).

Three trials of the six existing Tick workloads at 512/4,096 entities pass the
1.5x native/generated-C gate, with ratios 0.805–1.133 and matching state and
allocation checks. This compares the same lowered bodies; it is distinct from
the independent C algorithm. [Generated-C samples](branch-reads-tick-results.json).

## Rendered combat

Three alternating pairs per population run Arena3D at 1280x720 for 240 measured
frames after 30 warmups, with held fire, kills, XP and restart. State, combat
traces, native parallel execution and zero interpreter calls match.

| Enemies | Behavior P50 before / after | Physics P50 before / after | Wall P50 before / after | Wall P95 before / after |
| ---: | ---: | ---: | ---: | ---: |
| 1,000 | 0.231 / 0.219 ms | 0.726 / 0.713 ms | 8.479 / 8.428 ms | 8.847 / 8.859 ms |
| 20,000 | 2.169 / 2.112 ms | 8.459 / 8.193 ms | 14.433 / 14.231 ms | 29.236 / 28.268 ms |

At 20k, behavior improves in two pairs and rises by 0.009 ms in one; the median
paired reduction is 0.078 ms. Physics also changes, particularly in one pair,
so the whole-frame difference cannot be attributed entirely to this compiler
change. At 1k, presentation waits remain about 6.8 ms. This short scenario does
not establish a new frame-rate guarantee or sustained projectile-load result.
[Combat samples and hashes](branch-reads-scene-results.json).

## Validation

The new shared fixture covers either branch, an actual control-flow join,
variable/component writes, effectful input evaluation, three loop iterations,
wait/resume with changed service state, repeated dispatch and observed execution.
It also pauses after the first read, changes service state, and verifies that
stepping reads the new value. Native, bytecode and generated-C executions use
the same fixture; WebAssembly runs it in the portable backend smoke suite.

Arena's query-count check now requires one search per enemy per measured frame
in both native and bytecode profiling runs. Existing tests retain missing/stale
component, layout migration, error/budget, hot-apply and parallel-order coverage.
At 1,000 enemies over 16 measured frames, searches fall from 32,000 to 16,000
for each backend while category-list builds remain 16.
[Query-count samples](branch-reads-query-results.json).

The full Debug ASan/UBSan `make test`, Release `make build`, generated-C host-data
parity suite, native parallel-runtime suite, and Emscripten
`check-behavior-backend` pass. Changed C files pass formatting, and
`git diff --check` passes. Compiler sanitizers do not instrument SLJIT machine
code; bytecode and generated-C parity provide separate coverage.

## Remaining opportunities

The query still packs generic host arguments and resolves candidate components
inside its helper. Typed query lowering and a dependency-checked target snapshot
could reduce that work. The independent C reference also prebinds owner component
pointers across the whole run; this change reuses them only within a proven
execution path. Cross-frame binding needs storage and entity-lifetime guards.

SLJIT's register allocator conservatively counts calls that the runtime body
now skips when deciding whether values must survive a helper call. Refining that
analysis may reduce spills; it needs its own measurements. Native scheduling and
row validation also remain costs. Halving query calls does not halve total
behavior time, and these results do not establish universal near-C performance.

Reproduce with a saved Release executable from `2ba99cf9`:

```sh
python3 tools/bench_arena_scale.py --before build-release/test_suite-before-branch-reads --after build-release/test_suite --counts 1000 5000 20000 --frames 256 --trials 5 --clocks --output /tmp/branch-reads-scale.json
python3 tools/bench_arena_reference.py --binary build-release/test_suite --counts 1000 20000 --frames 256 --trials 5 --output /tmp/branch-reads-reference.json
python3 tools/bench_ticks.py --build-dir build-release --entities 512 4096 --frames 64 --trials 3 --enforce-native-ratio --output /tmp/branch-reads-ticks.json
python3 tools/bench_arena_scene_scale.py --before build-release/test_suite-before-branch-reads --after build-release/test_suite --counts 1000 20000 --frames 240 --trials 3 --combat --expect-parallel --output /tmp/branch-reads-scene.json
```
