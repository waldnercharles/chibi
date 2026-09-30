# Whole compiled nearest-entity queries

Find Nearest Entity now participates in whole-function compilation. The compiler
emits the argument stores, result loads, surrounding branches and arithmetic;
the existing native C world search executes through an explicit fixed signature.
It does not call a node evaluator or flow executor. The same lowered program
produces SLJIT machine code, portable bytecode and generated C.

The [catalog audit](whole-nearest-audit.txt) reports 19/33 whole event entries,
21/31 effectful local functions, 39/39 pure local functions and 4/4 library
functions across 19 assets. New whole entries are EnemyBehavior.Tick and
ArenaPickupBehavior.CollectExperience, plus the pickup's Start Collection
function. Enemy Tick also qualifies for automatic ordered batching. Full
coverage remains unfinished and the audit still exits nonzero.

## Host call contract and storage

The typed `node_host_call_fn` ABI supports a fixed declared signature of scalars
and plain structs. Arguments borrow values for the duration of the call. The
caller supplies result headers and non-null struct backing; the helper fills
every payload while preserving those headers and pointers. It cannot execute
graphs, mutate structural state or retain borrowed values. Managed values are
excluded with source diagnostics until their ownership lowering is implemented.

Each producer owns a distinct argument/result bank. The compiler reserves this
bank before lowering dependencies, then snapshots all outputs into typed slots.
Nested producers cannot overwrite an earlier result, even when their outputs
share the same reflected type. Null aggregate inputs retain their null meaning.

Pooled activations and ordered batches retain their banks across invocations.
Bindings refresh for each instance, and changing the immutable program generation
replaces the cached storage. The new allocation counter counts bank creation,
not every allocator call in the engine. Standalone pure whole calls currently
use temporary host/activation storage; they are functionally covered but do not
have the same allocation-free warm path.

The subsequent [material-write migration](whole-material.md) extends this
interface to synchronous effects and names it `node_host_call_fn`. Read-only
data producers retain the contract described here.

## Equivalent generated-C comparison

```sh
python3 tools/bench_nearest.py --build-dir build-performance \
  --output docs/research/whole-nearest-results.json
```

Five trials rotate six modes: SLJIT, generated C and bytecode, each through
ordinary pooled activations and ordered batches. Each mode warms for 32 calls
and measures 10,000 calls against worlds of 32 and 512 candidate entities. All
candidates have Transform and Collider; the fixture verifies the winning handle,
position, distance and success flag on every call. Checksums must agree, oracle
call counters must stay unchanged and warmed bank creation must remain zero.

Clang 23 builds Release runtime code without sanitizers and compiles emitted C
with `-O3 -ffp-contract=off`. Timings include the same query, runtime setup and
result checks for every backend. They exclude compilation and world construction.
This compares equivalent generated bodies through the existing runtime; it is
not a comparison against an optimized handwritten ECS outer loop. No builds or
other benchmarks ran concurrently.

Median nanoseconds per call from the
[results and raw logs](whole-nearest-results.json):

| Candidates | Path | SLJIT | Generated C | Bytecode | Native/C |
| ---: | --- | ---: | ---: | ---: | ---: |
| 32 | Pooled activation | 354.9 | 344.2 | 696.8 | 1.031 |
| 32 | Ordered batch | 275.7 | 267.6 | 608.6 | 1.030 |
| 512 | Pooled activation | 3584.0 | 3518.6 | 3872.6 | 1.019 |
| 512 | Ordered batch | 3462.2 | 3454.9 | 3811.5 | 1.002 |

All four comparisons meet the advisory 1.5x target. At 32 candidates, native
batching saves about 22% over the pooled activation in this fixture. The relative
bytecode gap narrows as candidate count grows and all backends spend more time
in the shared query. The measurements support retaining this query as a C
service while compiling its surrounding behavior. They do not prove that every
helper is cheap, or that the linear world search is the best query algorithm.

## Arena before and after

Fifteen paired trials for each profiling setting alternate the saved executable
from immediately before this change with the final optimized executable. The
baseline hash matches the preceding motion benchmark's final binary. Both use
the same assets, deterministic input and random seed, with batching enabled.
Each run warms up for 30 frames and records 150 frames. Both compiled backends
assert firing, restart, backend selection and zero oracle calls.
[Raw logs, executable hashes and paired results](whole-nearest-arena-results.json)
are retained. No builds or other benchmarks ran concurrently.

Median across trials, in microseconds per frame, with profiling disabled:

| Metric | SLJIT before | SLJIT after | Bytecode before | Bytecode after |
| --- | ---: | ---: | ---: | ---: |
| Behavior p50 | 15 | 11 | 23 | 21 |
| Behavior p95 | 31 | 28 | 40 | 38 |
| Simulation p50 | 21 | 17 | 29 | 27 |
| Simulation p95 | 38 | 34 | 47 | 44 |

Native median behavior time falls 26.7%; bytecode falls 8.7%. The median paired
native p50 and p95 differences are both -4 microseconds. The table reports
medians of each run's percentiles, not percentiles of one combined sample. These
are simulation costs for this scene and workload, not rendered frame rates or
a guarantee for larger scenes. Absolute baseline timings differ slightly from
the earlier motion measurements; each comparison uses its own paired baseline.

Separate profiled trials attribute the change to Enemy Tick. Native execution
cost falls from 3.960 to 1.887 microseconds per measured frame; setup falls from
1.333 to 0.200. Bytecode execution falls from 5.060 to 4.247; setup falls from
1.393 to 0.207. Each records 840 enemy Tick dispatches over the measured frames.
These short individual timings include profiler resolution effects.

Batched broadcasts rise from 537 to 1377 across 180 frames. Live Arena whole
coverage rises from 13/22 to 14/22 entries. This change combines whole-function
lowering with the existing automatic batching path; the paired result does not
isolate the contribution of each. The runtime still preserves attachment order
and uses ordinary compiled activations where batching is ineligible.

The largest remaining native Tick costs in the profiled run are
SpawnTelegraphBehavior at 2.560 microseconds per measured frame and CombatFeedback
at 2.093, including setup. Their first whole-function blockers are
Get_GameSpawnRequest and Set Material Parameter respectively. These are the next
measured candidates for expanding whole compilation. The pickup's Contains
operation remains another catalog blocker but is not a leading cost in this
particular Arena run.

## Verification

Native `make test` and `make build` pass. The real nearest-query tests compare
oracle, SLJIT and bytecode on categories, exclusions, retiring/deleted entities,
stale generations, tie ordering, zero radius, invalid coordinates/radius and
invalid transforms. Generated C executes the real query fixture under ASAN/UBSAN.

A reusable mock service tests nested multi-output calls with scalar and nested
struct values, wide integers and null structs. It compares oracle, SLJIT,
bytecode and generated-C snapshots through different budgets, service errors,
wait/resume, stepping, watches, traversal history and logical call stacks.
Failure in a batch preserves the same diagnostics and partial state without
rerunning the helper. Additional cases cover pure calls, service rebinding and
program generation changes. Native Arena firing and restart pass under
ASAN/UBSAN with zero oracle calls.

`make web` passes; the Emscripten smoke executes nested data calls using BYTECODE
with zero oracle calls and warmed buffer reuse. The native build configured
with `CHIBI_ENABLE_SJLIT=OFF` reports UNAVAILABLE and rejects execution without
interpreter fallback.

The remaining whole-function and debugger work is tracked in the
[completion criteria](behavior-completion.md). Managed ownership, randomness,
remaining visual/physics operations and full inspection coverage still need
implementation. Native/C ratios remain advisory; semantic parity, backend
availability, complete measurements and honest coverage remain hard requirements.
