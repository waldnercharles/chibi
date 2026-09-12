# Array views and native iteration

Synchronous whole bodies now compile ForEach and ForEachWithBreak over scalar
and fixed-layout struct elements. Array arguments, variable reads and inlined
parameters use borrowed views. Length, Last Index, Is Empty, Is Not Empty and
Is Valid Index become reads, comparisons and branches. Both SLJIT and portable
bytecode execute the same lowered instructions; generated C uses the same ABI.

The complete eligible body cannot replace or mutate its source arrays, yield,
or call a mutating host operation. That restriction permits iteration without
cloning the array. Index and element outputs retain snapshots in typed locals,
including plain struct fields and null presence. Activation setup still owns
copies of incoming arguments; those costs are outside the whole body.

Owned array defaults, escaping array results, creation/mutation operations,
managed elements and host-returned arrays remain excluded. Adding these requires
snapshot ownership before they can coexist with borrowed views. The current
source diagnostic identifies these exclusions; backend selection still uses the
general compiled executor for such bodies.

## Generated-C performance

```sh
python3 tools/bench_components.py --workload array --trials 5 --iterations 200000
```

The arm64 Release benchmark uses Clang 23.1.1, `-O3 -ffp-contract=off`, five
rotating trials and 10,000 warmup calls per backend. Each call sums 64 double
elements into a persistent variable and returns the sum and last index, with
changing inputs and checked checksums. It includes function arguments, variable
access, iteration, budget checks and returns. Publication, scheduling and
activation input/result setup are excluded. The bytecode VM still allocates its
slot storage per call. The timed native body is checked to contain no C calls.

| Backend | Median ns/call | Five-trial range |
| --- | ---: | ---: |
| SLJIT | 41.920 | 41.195–44.095 |
| Generated C | 34.775 | 34.130–52.955 |
| Bytecode | 3523.590 | 3450.455–3755.055 |

SLJIT/C is **1.205**, passing the unchanged **1.5** gate. Bytecode/SLJIT is
**84.1** for this body. [Final measurements](whole-arrays-results.json) record the
compiler, executable hash and every trial.

The first implementation took **160.340 ns** in SLJIT against **35.755 ns** in C,
failing the gate at **4.484**. Examining emitted instructions showed repeated
floating-point moves and a loop-carried value passing through stack storage.
Native arithmetic and memory operations now use their allocated registers
directly, and hot loop locals use saved floating-point registers where available.
Targets without saved FP registers retain stack locals. The source language and
budget checks are unchanged. [Initial measurements](whole-arrays-initial-results.json)
retain the failed result.

Regression runs also passed the existing generated-C gates for component access
(1.441), struct aliases (1.191), computed stores (1.118) and dynamic-name stores
(1.262). [Regression results](whole-arrays-regressions.json) retain those trials.
The separate counted-loop benchmark remains an open performance gate.

## Gameplay and correctness

Five alternating paired Arena runs compare `bf82c334` with the persistent-store
and array/compiler changes together. Each uses seed 12345, 30 warmup frames and
150 measured frames, with firing/restart and zero oracle calls for 63 instances.

| Backend | Behavior p50 before/after, µs | Behavior p95 before/after, µs | Simulation p50 before/after, µs |
| --- | ---: | ---: | ---: |
| SLJIT | 47 / 48 | 75 / 76 | 54 / 54 |
| Bytecode | 53 / 53 | 74 / 77 | 60 / 60 |

Trial ranges overlap, so these runs demonstrate **no clear Arena speedup**.
[Paired results](whole-arrays-arena-results.json) include raw logs and binary
hashes. The measured Arena binary predates only the final math-call test
extension; its compiler/runtime code matches the final kernel benchmark.

Oracle/SLJIT/bytecode tests cover null and empty arrays, scalar and plain struct
elements, null field errors, nested loops/calls, Break with queued Sequence work,
early Return, variable views, bounds queries, budgets 1–32, partial state and
source stacks. Generated C is compared on the same bodies and budget failures.
A math-call fixture verifies that loop-carried FP values survive a native C call.
A mutation fixture verifies that writes cannot silently invalidate borrowed views.

The tests exposed two correctness defects. Copying a null array reported failure
instead of preserving null, and a native widening cast wrote only 32 bits when
its 64-bit result spilled. Both are fixed. Pointer arithmetic and index widening
also have explicit integer semantics in the bytecode VM, including wasm32.

Native `make test`, `make build`, focused Debug/ASAN and Release tests, generated-C
parity/performance and native JIT-unavailable rejection passed. The final
test-only math-call extension passed focused Debug/ASAN and Release checks.
`make web` also passed, including an Emscripten/Node smoke that executes nested
ForEach with bytecode, no SLJIT and zero oracle calls.

Whole-function coverage remains **5/33 events**, **4/31 effectful local functions**,
**39/39 pure local functions** and **3/4 library functions**. Wave Tick now passes
ForEach lowering and stops at `Find Entities With Component` (graph 2010, node
2065). The strict catalog audit still returns nonzero. Compiled ECS queries and
ownership of their returned arrays are the next blockers; see the
[completion audit](behavior-completion.md) for the remaining scope.
