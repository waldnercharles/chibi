# Whole compiled material writes

Set Material Parameter now executes through a typed host-call interface in whole
functions. SLJIT generates the argument stores, surrounding arithmetic and
control flow, then calls the existing C material writer. Portable bytecode and
generated C consume the same lowered program. No whole body calls the custom
flow callback, node evaluator or interpreter.

The [catalog audit](whole-material-audit.txt) now reports 21/33 whole event
entries, 24/31 effectful local functions, 39/39 pure local functions and 4/4
library functions across 19 assets. New entries are CombatFeedback.Tick and
CombatFeedback.Damaged; Start Hit Flash, Update Hit Flash and Update Dash Glow
also compile as whole functions. Tick qualifies for existing ordered batching
without an authored System. Full coverage remains unfinished; the audit exits
nonzero while exclusions remain.

## Interface and semantics

The earlier read-only data-call ABI is now `node_host_call_fn` and supports both
read-only data producers and synchronous execution nodes. Each declared input
has a compiled type; wildcard inputs use their resolved scalar or plain-struct
type. Generated code prepares borrowed strings and struct fields in reusable
banks, calls the service and retains error text with the original source graph,
node and pin. A host call cannot execute graphs, retain borrowed values or
invalidate component/behavior storage. Execution nodes may perform synchronous
effects. Material writes retain host-sink access metadata and serialized order.

The material writer's search, validation and update logic remain native C. It
checks the target material, parameter name, scalar/vector width and finite
values. The oracle/custom callback and whole compiler use the same capability.
The three authored CombatFeedback parameter-name pins now use the descriptor's
declared string type; no compatibility alias was added.

Ordinary pooled activations and ordered batches retain their argument banks.
The counter verifies bank creation only, not every allocation in the engine.
Initial material overrides still allocate their own names and records. Borrowed
strings in the compiled body refer to immutable program data or activation-owned
arguments. Caller buffers may change after dispatch without changing retained
values, including across a wait.

Stepping tests exposed a stale call-stack entry when the last function call in
an event finished. Its completion safe point now uses the caller's terminal
stack boundary. Oracle, SLJIT, bytecode and generated C agree on this final pause.

## Equivalent generated-C comparison

```sh
python3 tools/bench_material.py --build-dir build-performance \
  --output docs/research/whole-material-results.json
```

Five trials rotate six modes: SLJIT, generated C and bytecode, each through
pooled activations and ordered batches. Each mode warms for 32 events and
measures 20,000 events. An event calls a local function that writes two different
parameters on Self. The float and three-component vector fixtures run with one
or 64 entities, each owning two existing overrides. The timed target is the last
entity, so the larger case exercises the C helper's longer override search.

The benchmark uses Clang 23, Release runtime libraries, sanitizers disabled and
`-O3 -ffp-contract=off` for emitted C. Timing includes dispatch, binding and both
host calls; it excludes compilation, world setup, warmup and the final output
check. Results must match, oracle counters must remain unchanged and warmed
bank creation must remain zero. This compares equivalent generated functions
through the same runtime, not a handwritten ECS outer loop. No builds or other
benchmarks ran concurrently.

Median nanoseconds per event containing two writes from the
[results and raw logs](whole-material-results.json):

| Payload | Entities | Path | SLJIT | Generated C | Bytecode | Native/C |
| --- | ---: | --- | ---: | ---: | ---: | ---: |
| Float | 1 | Pooled | 146.20 | 142.40 | 333.30 | 1.027 |
| Float | 1 | Batch | 71.50 | 69.95 | 269.80 | 1.022 |
| Float | 64 | Pooled | 245.30 | 249.65 | 417.60 | 0.983 |
| Float | 64 | Batch | 182.70 | 175.65 | 361.70 | 1.040 |
| Vector 3 | 1 | Pooled | 165.85 | 161.35 | 429.05 | 1.028 |
| Vector 3 | 1 | Batch | 87.25 | 93.85 | 351.90 | 0.930 |
| Vector 3 | 64 | Pooled | 273.55 | 275.40 | 522.40 | 0.993 |
| Vector 3 | 64 | Batch | 196.50 | 197.35 | 448.10 | 0.996 |

All cases meet the advisory 1.5x target. SLJIT is within about 4% of generated C
or faster in these trials; small differences should not be treated as a stable
advantage over C. Native batching reduces this fixture's cost by about 26% to 51%.
The larger override search costs more in every backend. This supports keeping
the native material service while removing behavior dispatch overhead; it does
not establish that the service's current linear lookup is optimal.

## Arena before and after

Fifteen paired trials for each profiling setting alternate the saved executable
from immediately before this change with the final optimized executable. The
baseline hash matches the nearest-query benchmark's final binary. Both use the
same migrated assets, deterministic input and random seed, with batching enabled.
Each run warms up for 30 frames and records 150 frames. Both compiled backends
assert firing, restart, backend selection and zero oracle calls.
[Raw logs, executable hashes and paired results](whole-material-arena-results.json)
are retained. No builds or other benchmarks ran concurrently.

Median across trials, in microseconds per frame, with profiling disabled:

| Metric | SLJIT before | SLJIT after | Bytecode before | Bytecode after |
| --- | ---: | ---: | ---: | ---: |
| Behavior p50 | 11 | 10 | 20 | 18 |
| Behavior p95 | 27 | 26 | 36 | 34 |
| Simulation p50 | 16 | 14 | 26 | 24 |
| Simulation p95 | 33 | 31 | 43 | 40 |

Native median behavior time falls about 9%; bytecode falls 10%. The median paired
native p50 and p95 differences are both -1 microsecond. The table reports medians
of each run's percentiles, not percentiles of one combined sample. These are
simulation costs for this particular scene and workload, not rendered frame
rates. Absolute baseline timings differ slightly from earlier measurements;
each comparison uses its own paired baseline.

Separate profiled trials attribute the change to CombatFeedback Tick. Native
execution falls from 1.120 to 0.133 microseconds per measured frame; setup falls
from 1.027 to 0.160. Bytecode execution falls from 1.287 to 0.347; setup falls
from 1.013 to 0.173. Each records 990 dispatches over the measured frames. These
short individual timings include profiler resolution effects.

Batched broadcasts rise from 1377 to 2396 across 180 frames. Live Arena whole
coverage rises from 14/22 to 16/22 entries. This comparison measures compilation
and automatic batching together; it does not isolate their contributions.
SpawnTelegraph is now the largest remaining native Tick cost in the profiled
workload, at 2.607 microseconds per measured frame including setup.

## Verification and remaining work

Native `make test` and `make build` pass. Focused tests compare scalar, boolean,
two/three/four-component vector and CF_V2 payloads across oracle, SLJIT, bytecode
and generated C. They cover missing services and targets, missing/null names,
width/type mismatch, nonfinite values, null structs, partial writes at budget
failures, waits, stepping, watches, traversals and logical child frames. Changing
caller string/vector buffers after dispatch does not change the activation's
values. Generated-C parity and the benchmark fixture pass under ASAN/UBSAN.

The Emscripten app builds, and its bytecode smoke executes synchronous typed
effects with borrowed strings/structs, partial failures and reused banks. It
asserts zero oracle calls and no SLJIT availability. The native no-JIT smoke
reports UNAVAILABLE and rejects execution without fallback. Native Arena firing
and restart also pass under ASAN/UBSAN with zero oracle calls.

SpawnTelegraph's GameSpawnRequest getter remains a major Tick exclusion because
its component contains a string. Compiling these reads requires explicit owned
snapshots across waits and later writes. Other exclusions include managed
values, random inputs, remaining visual/physics operations and array membership.
Full debugger coverage and gameplay acceptance remain tracked in the
[completion criteria](behavior-completion.md).
