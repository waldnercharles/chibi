# Compiled event lookup and Tick acceptance

September 12, 2026. This follows the revised [performance policy](behavior-completion.md#performance-policy).

A published program now owns an index of named root event entries. Compiled
native and bytecode dispatch search that index instead of every source node.
The index exists even when compilation fails, preserving distinct unknown-event
and duplicate-event diagnostics before the compile error. The explicit oracle
keeps its independent source lookup.

Instance creation, backend selection, hot apply and pool reuse publish the current
generation before dispatch. Its mutation guard then keeps the event index valid.
The change adds no per-instance event cache or per-dispatch allocation and does
not reorder entities, events, component accesses or structural commits.
`behavior_instance_dispatch` still returns the normal inspectable activation.
This is dispatch preparation, not automatic conversion to an ECS System.

## Measurements

Apple M4 Pro, Clang 23.1.1, Release with sanitizers disabled, `MallocNanoZone=0`.
Builds and measurements were serialized. [Five alternating before/after trials](tick-event-index-results.json)
include executable hashes, full output and individual samples. Both binaries
contain the logical-frame work; they differ in the event-index implementation.

The counter fixture updates an independent persistent value through an authored
Tick function. It checks every entity after eight warmup and 128 measured updates,
with tracing and profiling disabled. Its larger form adds 192 disconnected
Delta Time nodes to the root graph. Those nodes are never executed; they expose
lookup cost that previously scaled with unrelated authored nodes. This is a
synthetic dispatch stress case, not a prediction for every behavior graph.

| Entities | SLJIT, 194-node graph, ns/entity before → after | Bytecode, 194-node graph, ns/entity before → after |
| ---: | ---: | ---: |
| 64 | 156.006 → 73.120 | 184.814 → 101.807 |
| 512 | 179.459 → 107.529 | 205.032 → 126.450 |
| 4096 | 238.058 → 150.097 | 266.106 → 177.422 |

The 512-entity native reduction is 40%. The original two-node graph is roughly
unchanged: 104.248 → 106.598 ns/entity at 512 entities and 147.263 → 145.784 at
4096. Trial ranges overlap. These measurements include activation setup,
scheduling, execution and recycling. They do not measure allocator counts or
memory peaks; the index retains one pointer per named event per program.

The [initial version](tick-event-index-initial-results.json) called publication
again during dispatch, adding a generation-revision scan and slowing the tiny
graph. Publication now happens at lifecycle transitions, avoiding that extra scan.
Existing execution validation still runs.

The five-run Arena sample had noisy tails, including native behavior p95
0.054 → 0.066 ms. A separate [15 alternating-pair comparison](tick-event-index-arena-results.json)
with profiling disabled measured:

| Backend | Behavior p50 before → after | Behavior p95 before → after | Simulation p50 before → after |
| --- | --- | --- | --- |
| SLJIT | 0.025 → 0.024 ms | 0.048 → 0.047 ms | 0.031 → 0.030 ms |
| Bytecode | 0.033 → 0.033 ms | 0.057 → 0.057 ms | 0.039 → 0.039 ms |

Native behavior p95 ranged 0.043–0.081 ms before and 0.043–0.065 afterward.
The earlier tail slowdown did not repeat. The small median differences and
measurement resolution do not establish an Arena speedup. Each run exercised
firing, simulation and restart, reported the expected backend and asserted zero
oracle calls. Live whole-function coverage remains 9/22 entries across 13 unique
assets, distinct from backend selection. The current [catalog audit](whole-delay-frame-audit.txt) remains 13/33 event
entries, 10/31 effectful local bodies, 39/39 pure local bodies and 3/4 library
bodies. Its nonzero exit remains an unfinished coverage requirement.

## Next compiler work

Eligible synchronous Tick updates can become system-style execution before the
entire behavior language is lowered. The compiler must prove which updates can
share setup or reorder component accesses. Unknown effects, waits, reentrant host
operations and structural changes keep their current scheduling semantics until
they have explicit compiled contracts. Begin, FIFO event delivery, deterministic
random inputs, cancellation and hot apply remain required.

Measure the next implementation against the same scheduler and host services,
using both real Arena/prototype workloads and a component-update fixture at
multiple entity counts. Preserve activation/debugger observability, measure
setup and body time separately, and add allocation and memory measurements.
Arithmetic ratios alone cannot establish native-like ECS update performance.

## Reproduce

```sh
cmake --build build-performance --target test_suite bench_scheduler --parallel 6
MallocNanoZone=0 build-performance/bench_scheduler --tick 0
MallocNanoZone=0 build-performance/bench_scheduler --tick 192
CBLUEPRINT_BENCH_NO_PROFILE=1 MallocNanoZone=0 build-performance/test_suite --compiled-arena
python3 tools/bench_components.py --workload delay --iterations 200000
# Opt into a hard generated-C timing target when an experiment requires it:
python3 tools/bench_components.py --workload delay --iterations 200000 --enforce-native-ratio
```

Preserve each executable before modifying the compiler. Alternate run order and
keep builds/tests out of the measurement interval. Correctness failures and
incomplete measurements always fail; target misses are reported explicitly.
The native test suite also checks event renaming across backend selection, hot
apply and instance pooling, plus existing unknown/duplicate-event diagnostics.

Validation passed `make test`, `make build`, `make web`, generated-C
Delay parity under ASan/UBSan and the native no-JIT smoke. The web smoke
executes BYTECODE and asserts SLJIT is absent. Timing-policy spot checks
use a deliberately unattainable target to verify advisory success and
explicit enforcement failure without weakening semantic checks.
