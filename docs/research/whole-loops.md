# Whole compiled loops

This pass extends the whole-function compiler from `8f0cc573` with `ForLoop`,
`ForLoopWithBreak`, explicit While continuations and `Break`. Both SLJIT and
portable bytecode execute the same lowered control flow. No loop instruction
calls a C language-operation helper or the interpreter.

Counted ranges snapshot their endpoints and direction once, include both ends,
and support descending ranges and equal endpoints. They stop before an integer
boundary could overflow. Stored Index outputs survive completion. Break marks
the nearest pending loop, including when its input pin belongs to an outer node.
Already queued Sequence tails still run. A callee has its own loop scope, so its
Break cannot stop a caller's loop. Return discards the function's pending work.

The compiler records pending continuations as immutable source metadata. Budget
and Break failures reconstruct the existing inspection API's loop, Sequence and
function frames without maintaining an execution stack on every native node.
Nested pure-expression failures retain their source ancestry. Full instrumented
whole-function execution remains unfinished; inspected runs still use the
general compiled backend.

Numeric edge conversions also lower through the same native casts used by
explicit conversion nodes. Tests include real-width coercion, NaN/infinity,
saturation and signed integer widening. Authored `game_transform_to_cf` remains
excluded: it needs an explicit struct-conversion compilation contract.

## Measurements

Apple M4 Pro, Clang 23.1.1, Release `-O3`, sanitizers off, floating contraction
off. Five trials rotate SLJIT, generated C and bytecode order. Each trial makes
100,000 calls, each with 64–71 inclusive iterations, a persistent scalar update,
a returned Index and exact node-budget checks. Compilation and setup of the
caller-owned argument/result storage are outside the timing. The portable VM's
per-invocation slot/frame allocation is included.

| Backend | Median per invocation | Range |
| --- | ---: | ---: |
| SLJIT | 67.650 ns | 66.240–109.850 ns |
| Generated C | 31.480 ns | 26.270–48.070 ns |
| Bytecode | 2,570.660 ns | 2,511.900–2,667.210 ns |

**The 1.5× native/C gate fails: the measured ratio is 2.149×.** It remains an
open acceptance criterion. The first implementation measured 176.800 ns for
SLJIT, so the native loop improved about 62%. Direction hoisting, constant local
folding, saved registers for loop state, local copy coalescing, direct integer
operands and fused comparisons removed work. Native loops count remaining
budget, and their error handlers sit outside the ordinary instruction path.
Explicit I32-to-I64 widening sign-extends; ordinary I32 operations use 32-bit
moves. A trial that deferred step increments across pairs of checks did not
improve the result and was removed.

The remaining gap grows with loop length in the diagnostic sweep. More compiler
work is needed; these results do not establish near-C performance for loops.
The existing scalar, argument, call, pure-function, math, integer, aggregate,
component and scheduler performance gates pass. Component SLJIT/C is 1.460×;
scheduler dispatch is 0.695× its C reference.

Five paired Arena runs alternate the saved baseline and changed executable,
using seed 12345, 30 warmup frames and 150 measured frames. Every run verifies
firing, restart, 63 behavior instances and zero oracle calls.

| Backend | Behavior p50 before / after | Behavior p95 before / after |
| --- | ---: | ---: |
| SLJIT | 45 / 45 µs | 69 / 67 µs |
| Bytecode | 51 / 53 µs | 76 / 72 µs |

Native simulation p50 is also unchanged at 51 µs. The overlapping samples do not
establish an Arena speedup. Wave Tick remains around 16 µs and still uses the
general compiled path. Raw measurements, hashes and verification records are in
[whole-loops-results.json](whole-loops-results.json).

## Reproduce and assess coverage

```sh
build/test_suite --whole-loop
python3 tools/bench_loops.py --trials 5 --iterations 100000 --output /tmp/loops.json
# This performance command currently returns nonzero.
python3 tools/bench_loops.py --length 1024 --trials 5 --iterations 100000
build-performance/test_suite --whole-function-audit
```

Whole coverage remains **5/33 events**, **3/31 effectful local functions**,
**39/39 pure local functions**, and **3/4 library functions**. The strict catalog
command still fails. Current bodies containing counted loops also contain
unsupported operations. Wave's next blockers include the Transform/vector
conversion, persistent aggregate assignment, arrays/ForEach and host effects.
Recursive re-entry of the same loop node is explicitly excluded until dynamic
compiled loop frames are implemented.

Validation passed:

- `make test`, `make build`, and sanitized `--behavior-parity`.
- Ten loop fixtures across oracle, SLJIT and bytecode, comparing state, outputs,
  every tested budget cutoff and source stacks: ascending/descending/equal and
  boundary ranges, endpoint mutation, empty bodies, nested loops, queued tails,
  early Return, Break errors, and repeated successful nested function calls.
- Generated-C parity for those fixtures, including initial step counts below,
  equal to and above the budget and `UINTPTR_MAX`.
- `make web` and the expanded bytecode loop smoke test, with no SLJIT or oracle.
- Native `check-behavior-backend` with SLJIT disabled, rejecting execution.
- Existing scalar/component/scheduler performance gates and ten paired Arena
  runs. The new loop performance gate remains failing as described above.

Full migration requirements remain in [behavior-completion.md](behavior-completion.md).
