# Whole pure-function execution

September 12, 2026. Baseline: `0f43d0b2`. [Raw results](whole-pure-functions-results.json)
include executable hashes, five alternating Arena pairs and five kernel trials.
The tests use Apple M4 Pro, Clang 23.1.1, Release `-O3`, sanitizers disabled and
`MallocNanoZone=0`.

## Execution change

Publication now lowers pure function bodies independently of their callers.
Eligible calls execute their complete body through SLJIT or the shared bytecode
IR. Native calls use the caller's typed argument and result storage; they allocate
no callee runtime store and perform no per-node evaluation inside the body.
Bytecode also avoids callee runtime stores, but currently allocates VM scratch.
This reaches pure calls inside events that still require the general compiled
runtime for component access or host operations.

The lowerer evaluates and caches every return from an inlined pure call, including
unused outputs. This preserves evaluation effects: an unused returned random
sample must still advance the random state. Such operations remain excluded from
whole-function lowering until their explicit compiled operations are available.

Pure evaluation has a separate call-depth counter from effectful activations.
Publication derives the maximum admissible incoming depth for inlined calls.
Calls near that boundary use the checked compiled runtime to preserve the exact
failing node and pin. Inspection also uses the general compiled path to record
edges and watches. Neither path invokes the interpreter; the oracle is explicit.

## Measurements

Arena uses the same scene, seed, firing input and profiling settings, with 30
warmup and 150 measured frames per run. Values below are medians of five runs.

| Backend | Behavior median before | After | Reduction | Behavior p95 before | After |
| --- | ---: | ---: | ---: | ---: | ---: |
| SLJIT | 88 µs | 76 µs | 13.6% | 163 µs | 135 µs |
| Bytecode | 91 µs | 79 µs | 13.2% | 151 µs | 131 µs |

Native simulation median, including physics and systems, fell from 95 to 83 µs.
These are bounded simulation measurements, not presentation or sustained frame
rate measurements.

The nested two-result arithmetic fixture measures **1.963 ns SLJIT / 1.434 ns
generated C**, a **1.369× ratio**, passing the unchanged 1.5× gate. The other
scalar, argument, inlined-call and scheduler gates also pass. Each trial uses
two million invocations, rotates backend order and checks result checksums.

Calling that fixture through the general pull interface measures 48.719 ns
SLJIT, 99.259 ns bytecode and 1,530.741 ns oracle. These are backend comparisons,
not before/after measurements. Compiled calls create zero callee runtime stores;
the oracle creates two per call. `runtime_store_creations()` exposes a monotonic
count on the calling thread for allocation checks without instrumentation in
successful compiled calls.

## Coverage and acceptance

The catalog gate now includes all local and library function bodies, including
unused definitions. Current coverage is:

| Entry kind | Whole compiled | Total |
| --- | ---: | ---: |
| Events | 1 | 33 |
| Effectful local functions | 1 | 31 |
| Pure local functions | 21 | 39 |
| Library functions | 1 | 4 |

The gate intentionally fails while any discovered body is excluded. Remaining
pure blockers include transcendental math, integer operations, vectors and
compound results. Full event coverage additionally needs component/entity
access, engine effects, managed values, resumable/recursive frames and compiled
inspection. A full generated-C gameplay baseline remains required.

Runtime tests compare local and linked-library calls, all results, repeated calls,
NaNs, infinities, signed zero,
inspection and depth-boundary failures across SLJIT, bytecode and the oracle.
They assert no callee runtime allocation for uninstrumented compiled calls and
verify the random-state effect of an unused return. The Emscripten smoke exercises
nested pure bodies with zero callee stores, zero oracle calls and no SLJIT.

Reproduce with `tools/bench_behavior.py --trials 5 --iterations 2000000`,
`test_suite --behavior-parity`, `test_suite --compiled-arena` and
`test_suite --whole-function-audit`. `make test`, `make build`, `make web`, the native no-JIT smoke,
sanitizer-enabled behavior parity and the generated-C performance gates passed.
The expanded catalog audit remains nonzero at the coverage reported above.
