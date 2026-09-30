# Whole aggregate execution

September 12, 2026. Baseline compiler: `d3b03171`. Measurements use Apple M4
Pro, Clang 23.1.1, Release `-O3`, sanitizers disabled and `MallocNanoZone=0`.
[Raw trials and executable hashes](whole-aggregates-results.json) retain the
initial failed gate, the optimization trials and the final measurements.

## Compiled values

The whole-function compiler now flattens fixed-layout struct fields into typed
scalar slots. Make, Break, projected outputs, aggregate arguments and returns,
immutable variable reads, and synchronous function locals use those slots.
Nested structs keep their reflected offsets; narrow integers, booleans, I64 and
mixed floating-point fields keep their own widths. Intermediate aggregates
need no runtime objects or allocations. Final aggregate results are materialized
in the caller's arena and survive callee teardown.

Presence and nominal type names travel separately from the fields. A null
aggregate can pass through a function unchanged. Dereferencing it fails at the
original graph/node/pin. A regression test caught loss of `GameTransform`'s
nominal name when returned through a `CF_V2` parameter; compiled calls now retain
that metadata. Function-frame tests compare successful results, null failures,
partial execution and budget diagnostics with the oracle.

Generated single-field component conversions have an explicit `NODE_JIT_UNWRAP`
contract. The compiler lowers those conversions to field access; it does not
infer arbitrary C function behavior from a name or compatible type layout.

Mixed F32/F64 arithmetic retains the language's special subnormal behavior and
applies the consuming pin's coercion. Floating Modulo, Atan2, Normalize Vector 2
and Direction To now lower as complete expressions. Generated machine code calls
platform `fmodf`, `fmod`, `atan2f` and `hypot` directly through scalar C ABIs.
Normalizing in double precision preserves very large and subnormal float vectors.
These calls do not invoke a node evaluator or marshal graph values. The generated
C implementation uses the same operations and numerical rules.

## Measurements

The struct kernel passes a record through two nested function calls, changes its
nested position, and returns the record and a scalar. It preserves a byte, a
boolean, an I64 stamp and a double field. Caller output storage is preallocated
for this kernel measurement; allocation at the runtime boundary is included in
the Arena measurements below.

The initial emitter took 7.38 ns against generated C's 3.42 ns, failing the
unchanged 1.5x bound at **2.16x**. It repeated presence checks and reloaded the
result pointer for each field. Eliminating redundant checks and grouping
contiguous result stores removed that cost. After the nominal-type fix, five
final trials measured:

| Backend | Median per call | Range |
| --- | ---: | ---: |
| SLJIT | 4.253 ns | 4.200–7.310 ns |
| Generated C | 3.307 ns | 3.171–3.343 ns |
| Bytecode | 109.583 ns | 107.532–121.556 ns |

SLJIT is **1.286x generated C** and about **25.8x faster than bytecode** for this
body. One native timing outlier is retained in the raw results. All existing
scalar, integer, math, argument/return, inlined-call and scheduler gates also
pass. Each trial executes two million invocations, rotates backend order and
checks checksums; generated C compiles with `-ffp-contract=off`.

Arena uses identical content, seed 12345, firing input and profiling settings,
with 30 warmup and 150 measured frames per run. The baseline was rebuilt from an
archive of `d3b03171` using the same toolchain and dependency sources. Five pairs
alternate which compiler runs first. These are medians of the five per-run
measurements:

| Backend | Behavior p50 before | After | Reduction | Behavior p95 before | After |
| --- | ---: | ---: | ---: | ---: | ---: |
| SLJIT | 71 µs | 49 µs | 31.0% | 98 µs | 72 µs |
| Bytecode | 76 µs | 54 µs | 28.9% | 99 µs | 74 µs |

Native simulation p50, including physics and systems, falls from 77 to 55 µs.
Each run verifies firing, restart, 63 behavior instances, the selected compiled
backend and zero oracle calls. This measures bounded simulation work, not frame
presentation or sustained gameplay frame rate.

The per-event profile explains the remaining small SLJIT/bytecode gap. Native
Enemy Tick execution falls from 12.51 to 4.33 µs per frame and Ship Motion Tick
from 10.16 to 4.35 µs. Wave Tick remains around 16 µs. Component access, event
activation, loops and engine effects still use the shared general compiled path.
Completing those entries is the next performance step.

## Acceptance and remaining coverage

The content audit now accepts all **39/39 pure local functions** and **3/4 library
functions**, up from 26/39 and 1/4. Only 1/33 events and 1/31 effectful local
functions qualify as complete whole-function bodies. The audit deliberately
returns nonzero until all discovered bodies qualify.

New parity coverage includes nested fixed-layout values, projected multi-result
calls, owned returns, null pass-through and dereference errors, nominal aliases,
synchronous frames and budget failures. Mixed real arithmetic and vector tests
cover NaNs, infinities, signed zero, subnormal values and large magnitudes against
the oracle. Generated C compares aggregates, identity/projection results and
nested effectful frames with both compiled backends. The Emscripten smoke uses
its own reflected fixture pack to check portable aggregate execution without a
runtime/editor dependency.

Persistent aggregate writes, managed fields and containers, component/entity
access, structural/host effects, resumable frames and whole-function inspection
remain unfinished. Full generated-C gameplay parity, compile-time/code-size
reporting and gameplay performance bounds also remain required. The complete
migration is tracked in [behavior-completion.md](behavior-completion.md).

Reproduce with `tools/bench_behavior.py --trials 5 --iterations 2000000`,
`test_suite --behavior-parity`, `test_suite --compiled-arena` and
`test_suite --whole-function-audit`. Full native and web verification results
are recorded in the accompanying results file.
