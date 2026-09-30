# Native math calls in whole behavior functions

September 12, 2026, following `c7b2079a`.
[Raw measurements](whole-math-calls-results.json) contain executable identity and
five trials against C emitted from the same lowered program. Tests use Apple M4
Pro, Clang 23.1.1, Release `-O3`, sanitizers disabled and `MallocNanoZone=0`.

## Compiled operations

The shared IR now represents sine, cosine, tangent, inverse sine/cosine/tangent,
exponential, natural/base-10 logarithm, power and two-argument arctangent. SLJIT
calls the corresponding platform math symbol through its scalar C ABI. No graph
lookup, boxed node call, `eval_node` or `flow_run` occurs inside these bodies.
Bytecode executes the same operations using the same math functions.

These transcendental operations use the math implementation also available to
native C. Replacing them with emitted approximations would introduce accuracy and
range-reduction work. The measured call boundary meets the current performance
gate. Inexpensive hardware operations such as square root and rounding remain
separate lowering work; this result does not establish that a helper is optimal
for those operations.

Clamp lowers into native Min and Max instructions, matching the language's
`Max(Min(value, high), low)` definition. That order matters for NaNs, signed zero
and reversed bounds. Clamp introduces no helper call.

## Calling convention and register allocation

Functions containing C calls preserve their context, variable pointer, argument
metadata and step count in saved registers. Values live across calls use saved
floating-point registers; excess values spill to the activation stack. Targets
without saved floating-point registers, including SysV x86, spill those values.
Functions without external calls retain their existing register convention.

This explicit ABI boundary can also support engine calls as they are lowered.
The guarded optimizer currently excludes bodies containing math calls so it does
not reorder or discard them while forwarding variables and delayed stores.

## Performance

The benchmark contains six sine results, six cosine calls and retained results
that exceed the four-register saved FP pool. It also reads a persistent variable
and returns F64, F32 and bool values. Reference transcendental values are computed
before timing. Timing includes argument updates and invocation overhead for both
backends.

| Kernel | SLJIT median | Generated C median | Ratio | Gate |
| --- | ---: | ---: | ---: | ---: |
| Math body with live values and spills | 29.098 ns | 26.040 ns | 1.117× | ≤1.5× |

All existing scalar, argument, pure-body, inlined-call and scheduler gates also
pass. This measures the math call boundary and does not claim a further Arena
improvement; the preceding pure-function migration has its own paired gameplay
measurements.

## Coverage and verification

Pure local-body coverage increases from 21/39 to **26/39**, including pickup
spiral math, ship roll math and health clamping. Coverage remains **1/33 events,
1/31 effectful local bodies and 1/4 library bodies**. The strict catalog gate
continues to fail until all discovered bodies qualify.

Tests compare all eleven math primitives and Clamp with the oracle and bytecode,
including NaNs, infinities, signed zeros, domain boundaries, subnormal values and
reversed Clamp bounds. A nested effectful-call fixture verifies scalar values,
register and stack preservation, mixed returns, independent frame budgets and
partial effects after a call. Generated-C parity checks the same mixed math body.

Reproduce with `test_suite --behavior-parity`,
`tools/bench_behavior.py --trials 5 --iterations 2000000`,
`test_suite --whole-function-audit` and the native/web backend smoke targets.
`make test`, `make build`, `make web`, native no-JIT rejection and the Arena3D
firing/restart smoke passed. Both Arena backends reported 63 instances and zero
oracle calls.

Integer operations, cheap hardware math, vectors, component/entity access,
managed values, engine effects, resumable frames and compiled debugger
instrumentation remain part of the full-coverage goal.
