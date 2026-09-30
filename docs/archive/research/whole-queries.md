# Whole-function entity queries

`Find Entities With Component` now lowers through a typed snapshot ABI into
SLJIT, portable bytecode and generated C. The current catalog gains one complete
effectful function: `ArenaWaveBehavior.Count Living Members`. Wave Tick now gets
past the query and stops at `Send Event` (graph 2005, node 2386).

## Execution and ownership

The node descriptor supplies an explicit `node_query_fn` and service name. It
receives a constant component key and caller-owned `pin_array` storage; it does
not enter `eval_node`, `flow_run`, or generic node dispatch. World lookup, ECS
iteration, retirement filtering and snapshot storage remain in native C. SLJIT
emits the consuming array indexing, loop branches, local state and body.

Each lowered producer owns a distinct reusable buffer. Nested consumers cannot
overwrite an outer query's snapshot. Repeated loops reuse capacity instead of
accumulating snapshots in an arena. The invocation releases buffers on normal
completion, early Return and errors. Raw entry callers can prepare/release this
storage explicitly; the normal execution wrapper owns that lifecycle by default.

These are synchronous snapshots. Query arrays cannot be stored in variables,
passed through effectful argument storage, or returned as escaping arrays. Pure
inlined consumers can borrow them without transferring ownership. Recursive loop
activation and mutating host operations remain excluded. General compiled
execution handles those bodies. Dynamic query keys and general managed-array
transfer still require implementation.

The helper now appends handle values directly, avoiding generic array copying.
The ECS query API also removes an unused component-ID allocation for all queries
and stores the single-component query plan inline. Multi-component query plans
retain their owned reference array. Deterministic dense/sparse iteration and
pending-destruction filtering are unchanged.

## Measured performance

Native arm64, Clang 23.1.1, Release `-O3`, sanitizers disabled and
`-ffp-contract=off`. Five trials rotate the three execution backends, with 10,000
warmup invocations and 200,000 measured invocations per trial. The world has 24
slots, 16 matching entities, a reused entity generation and dense query order.
The body updates a count and retains the last visited handle. Generated C comes
from the same lowered program and calls the same snapshot helper. Checksums,
state, return values and error/source diagnostics are checked across backends.
No compilation, other benchmark or regression process runs during measurements.

| Stage | SLJIT, ns | Generated C, ns | Bytecode, ns | C snapshot helper alone, ns |
| --- | ---: | ---: | ---: | ---: |
| Initial typed query | 179.205 | 169.165 | 1356.835 | 156.450 |
| Direct handle append | 125.125 | 123.445 | 1261.060 | 113.225 |
| Inline single-component plan | 87.210 | 83.170 | 1227.700 | 72.040 |

Values are medians. Final SLJIT/generated-C ratio: **1.049**, passing the 1.5
limit. Bytecode takes **14.1 times** as long as SLJIT for this complete body.
The final SLJIT trials ranged from 85.125 to 161.540 ns; generated C ranged from
80.570 to 107.335 ns. Retain the outliers when evaluating the result.

The helper-only timing is a separate warmed diagnostic, not an exact additive
breakdown. It indicates that most remaining query cost lies in the native
snapshot service. This comparison does not prove that the service boundary is
optimal: fusing an eligible query with its consumer could avoid materialization
and a second traversal. Such fusion still needs a proof that membership and
retirement state remain stable for the snapshot's lifetime, and an equivalent
C benchmark. Current timings exclude invocation setup/result preparation and
reuse query buffers between invocations; the Arena results include real runtime
setup and cleanup.

[Initial raw results](whole-queries-initial-results.json),
[append results](whole-queries-append-results.json) and
[final results](whole-queries-results.json) include trial logs, compiler versions
and binary hashes.

```sh
python3 tools/bench_components.py --workload query --trials 5 --iterations 200000 --output /tmp/query-results.json
build/test_suite --whole-query
```

## Arena comparison

Five paired trials alternate before/after order. The baseline is `6b817289`;
its saved executable hash matches the previous array benchmark's recorded
binary. The candidate includes query lowering, direct handle snapshots and the
ECS plan allocation change. Each trial uses seed 12345, 30 warmup frames and 150
measured frames, including firing and restart. Both backends assert zero oracle
execution across 63 live instances.

| Backend | Behavior p50 before/after, µs | Behavior p95 before/after, µs | Simulation p50 before/after, µs | Simulation p95 before/after, µs |
| --- | ---: | ---: | ---: | ---: |
| SLJIT | 46 / 32 | 79 / 54 | 53 / 38 | 93 / 62 |
| Bytecode | 53 / 40 | 76 / 61 | 60 / 47 | 84 / 68 |

Native behavior p50 improves about **30%**, and simulation p50 about **28%**.
Before/after native behavior p50 trial ranges are 46–47 / 32–34 µs; simulation
ranges are 52–54 / 38–40 µs. These measure the changes together, not the isolated
effect of JIT compilation. Presentation time and larger-population scaling are
not measured by this scenario. [Paired results](whole-queries-arena-results.json)
contain raw logs and executable hashes.

## Validation and remaining work

Oracle/SLJIT/bytecode parity covers empty, dense and sparse worlds, pending
engine/host destruction, reused handles, order, nested query loops, repeated
buffer reuse, effectful and pure calls, Break, Return, partial state, budget
failures, missing world/component, active-query errors and source stacks.
Generated C additionally checks successful results and budget/host failures for
all seven query fixtures. An ownership regression checks the precise Return-pin
exclusion for an escaping query array.

Native `make test`, `make build` and native JIT-unavailable rejection passed.
The final test-only extensions passed focused Debug/ASAN and Release checks,
generated-C parity and `make web`. The Emscripten smoke prints BYTECODE for nested
whole query snapshots and asserts no SLJIT or oracle execution.

Whole-function coverage is **5/33 events**, **5/31 effectful local functions**,
**39/39 pure local functions** and **3/4 library functions**. The strict catalog
audit still returns nonzero. `Send Event`, other host operations, managed value
transfer, resumable frames and instrumented whole-function debugging remain
unfinished. The separate counted-loop performance gate is still open. See the
[completion audit](behavior-completion.md); this checkpoint does not complete
the migration.
