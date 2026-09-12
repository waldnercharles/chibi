# Numeric behavior regions: first performance acceptance

September 11, 2026. Baseline: `6dd0b0fd`. Measurements use Apple M4 Pro,
Homebrew Clang 23.1.1, CMake Release, sanitizers off, SLJIT enabled. This work
begins the [helper audit's acceptance criteria](behavior-jit-helper-audit.md).
It does not complete the engine-wide native-performance objective.

## Implemented

Native input evaluation can now execute an entire eligible numeric dependency
region without calling the generic evaluator, slot helpers or variable lookup
for each operation. It consumes the already-lowered behavior IR, resolves
variable indices at publication, eliminates data-edge copies, retains arithmetic
intermediates in registers, and spills shared values only when required. No
arithmetic reassociation or FMA contraction is introduced.

Coverage is matching F32/F64 Add/Subtract/Multiply/Divide, scalar variable reads
and variable bindings, unconnected numeric defaults, static constant outputs,
and pure copy nodes. It is not integer/vector lowering or general graph-function
compilation. Authored literal nodes whose descriptor uses a generic function
callback are not newly specialized by this change. Output inspection and
unsupported/mixed regions retain their regular compiled implementation.

At the API boundary the result is written to the final input slot, preserving
existing runtime storage semantics. Instrumented execution keeps the compiled
source-mapped blocks so intermediate watches and traversal remain observable.
The data-edge observer is now enabled by active tracing, rather than by the
permission to honor authored breakpoints. Breakpoint checks remain independent.
All native routes still require SLJIT; bytecode and oracle semantics are retained.
The generated numeric function has no C calls. The surrounding API still uses
one final runtime-slot lookup and the event scheduler still runs in C.

## Reproducible comparison

`bench_behavior` builds a graph computing four repetitions of `x * 1.01 + .125`.
Every iteration supplies a varying input, consumes the output in a checksum,
and checks the final checksum against equivalent handwritten C. The dispatch
case uses pooled activations and disables inspection. Kernel timings include
API entry, epoch reset and final result materialization; they exclude dispatch.
Handwritten C arithmetic excludes that API work and is a lower-bound reference.
The C dispatch reference runs the handwritten calculation in one native host
node through the same event scheduler, with the same input and variable update.
It does not reproduce unused graph input/output slot bookkeeping.

Build and run after configuring dependencies as for the native project:

```sh
cmake -S . -B build-performance -DCMAKE_BUILD_TYPE=Release \
  -DCHIBI_ENABLE_SANITIZERS=OFF
cmake --build build-performance --target bench_behavior scene_benchmark --parallel 8
build-performance/bench_behavior 1000000 0
build-performance/bench_behavior 1000000 1
build-performance/bench_behavior 1000000 2
build-performance/scene_benchmark
```

The second benchmark argument rotates backend order. Five before/after trials
alternated version order; each trial ran one million measured iterations after
warmup. Baseline and optimized executables used the identical updated benchmark
source. The baseline executable was built with the original `behavior_program.c`
from `6dd0b0fd`; the optimized source was then restored and rebuilt. Measurements
were sequential. Publication is timed separately from steady-state execution.
These are throughput measurements on this machine, not per-instruction latency
or portable speed guarantees. Assertions and checksum checks are active in the
benchmark; the C arithmetic reference disables floating-point contraction.

| Path | Before median ns | After median ns | Before range | After range |
| --- | ---: | ---: | ---: | ---: |
| SLJIT numeric input | 157.586 | 6.766 | 152.934–159.035 | 6.679–13.634 |
| SLJIT pooled dispatch | 322.386 | 118.327 | 318.492–337.247 | 116.185–127.719 |
| Bytecode numeric input | 266.042 | 262.315 | 257.936–291.740 | 261.389–265.558 |
| Bytecode pooled dispatch | 436.157 | 431.667 | 428.691–438.083 | 430.217–447.343 |
| Oracle numeric output | 310.979 | 308.557 | 308.300–313.878 | 306.016–331.487 |
| Oracle pooled dispatch | 485.357 | 476.530 | 474.735–534.904 | 470.076–479.548 |
| Handwritten C arithmetic | 0.770 | 0.772 | 0.712–0.798 | 0.687–1.716 |
| Handwritten C through scheduler | 91.043 | 89.857 | 90.586–158.176 | 89.168–91.596 |

The eligible numeric input is 23.3x faster; pooled dispatch is 2.72x faster.
Optimized dispatch remains 1.32x the C-through-scheduler reference. The raw C
arithmetic remains much cheaper than either API path. Publication median changed
from 0.156 ms to 0.206 ms; ranges were 0.146–0.185 and 0.174–0.512 ms. Additional
native entries increase publication work and code size. Code bytes have not yet
been quantified, so this is not evidence of a code-size improvement.

## Scene results and limits

The existing synthetic scene benchmark uses a constant Boolean branch, with no
eligible numeric region. Its five-trial median average behavior time was
0.206070 ms before and 0.199160 ms after; ranges overlap. Treat this as unchanged,
not a gain caused by numeric lowering. Deterministic final scene hashes matched.

Arena3D used the native release `test_suite --compiled-arena`, seed 12345, firing
for 180 fixed simulation frames, discarding the first 30 timing samples. Five
interleaved before/after runs measured existing engine behavior/physics/system
CPU timings, excluding rendering/presentation. No timing threshold is asserted.

| SLJIT Arena run | Behavior p50 before/after ms | Behavior p95 before/after ms | Simulation p50 before/after ms | Simulation p95 before/after ms |
| --- | --- | --- | --- | --- |
| 1 | 0.109 / 0.106 | 0.145 / 0.134 | 0.115 / 0.112 | 0.157 / 0.141 |
| 2 | 0.100 / 0.118 | 0.157 / 0.190 | 0.106 / 0.124 | 0.168 / 0.198 |
| 3 | 0.102 / 0.102 | 0.132 / 0.136 | 0.109 / 0.109 | 0.140 / 0.143 |
| 4 | 0.102 / 0.107 | 0.127 / 0.139 | 0.108 / 0.113 | 0.133 / 0.147 |
| 5 | 0.103 / 0.104 | 0.171 / 0.137 | 0.108 / 0.110 | 0.181 / 0.145 |

Arena has no demonstrated speedup. Before/after ranges overlap, and
bytecode timings also varied. These short measurements do not establish a
causal whole-scene gain or regression. Longer repeated workload/profile runs are required before claiming
whole-scene performance acceptance. Each run verified firing, restart, 63 live
instances, correct backend and zero oracle calls.

## Acceptance status

- Met for this region: repeatable release comparison with C, original SLJIT,
  bytecode and oracle; observable checksum; steady-state/publication separation;
  no per-operation helper calls; numerical and debugging parity checks.
- New regression cases exercise F32/F64 shared DAGs, variable bindings, changing
  inputs, NaNs, infinities and signed zero against bytecode/oracle, and require
  final input-slot materialization. Existing contracts check waits, cancellation,
  managed ownership, functions, source maps, breakpoints, watches and hot apply.
- Still open: engine-wide near-native performance, scalar/Boolean/integer/vector
  coverage beyond this region, compiled control-flow regions and function frames,
  container/ECS fast paths, helper/allocation profiling by category, code-size
  accounting, and a statistically convincing Arena frame-time improvement.

The dominant remaining architectural work is eliminating per-node C scheduling
and generic storage across ordinary control flow and calls. The measured kernel
win does not justify retaining those helpers on performance grounds.

Final verification: `make test`, `make build`, `make web`,
`test_suite --behavior-parity`, the release Arena comparisons, and the native
JIT-unavailable smoke passed. Web reports BYTECODE with zero oracle calls;
the native no-JIT configuration reports UNAVAILABLE without fallback.
`git diff --check` passed. No full-scene performance threshold has been met.
