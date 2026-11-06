# Explicit struct alias conversions

The September 12 conversion change lowers `transform_to_cf` and
`transform_from_cf` inside whole functions. `NODE_JIT("identity")` is an
explicit promise that a by-value struct conversion copies the underlying value
and changes its nominal type. Reflection requires one struct parameter with
the same canonical C type as the result. The behavior compiler also checks
scalar field types, offsets and aggregate size. Managed fields remain excluded.

Both explicit conversion nodes and conversion edges use existing typed slots;
they introduce no native helper call or intermediate allocation. Only the
caller's final aggregate result needs owned output storage. Missing input values
produce a source node/pin diagnostic. The System JIT now requires this annotation
before replacing a conversion with a copy. A regression conversion that changes
coordinates verifies that compatible layouts alone do not authorize that rewrite.

## Measurements

`tools/bench_components.py --workload alias --trials 5 --iterations 2000000`
compiles generated C from the same lowered pure conversion body using the Release
build's Clang, `-O3 -ffp-contract=off`. Each trial rotates SLJIT, generated C and
bytecode, warms up 10,000 calls, changes inputs and checks a checksum. Caller
output storage is reused. The benchmark includes argument checks, field loads,
conversion validation and result stores; it excludes scheduler, publication and
result allocation costs. It is not a Tick timing.

| Backend | Median ns/call | Range across five trials |
| --- | ---: | ---: |
| SLJIT | 2.141 | 2.004–2.872 |
| Generated C | 1.709 | 1.560–2.395 |
| Bytecode | 40.546 | 38.069–48.817 |

SLJIT/C is **1.253**, passing the unchanged **1.5** limit. Bytecode/SLJIT is
**18.94**. These nanosecond measurements have noticeable run-to-run variation;
use the repeated-trial gate, not a single sample. The [raw result](whole-aliases-results.json)
records the compiler, baseline commit and measured executable's SHA-256.

## Correctness and inspection

Tests compare both directions, explicit nodes and conversion edges, synchronous
and pure nested calls, result ownership, signed zero/NaN payloads, nulls, budgets,
source nodes/pins and call stacks across the oracle, whole SLJIT/bytecode and
the general compiled inspection paths. Generated-C comparison includes null
errors and source mapping. The portable smoke fixture uses separate types without
linking game capabilities. Reflection rejects invalid arity, scalar/pointer
arguments and distinct C record types without replacing the previous registry.

These tests exposed and fixed two oracle/inspection defects: pure Return pins
skipped their conversion edges, and pure-call failures lost their call frames.
Error frames now survive inspection in the oracle and general compiled paths;
they allocate only on failure and clear when an activation stops or is reused.

## Remaining coverage

The catalog audit is unchanged at **5/33 events**, **3/31 effectful local
functions**, **39/39 pure local functions**, and **3/4 library functions**.
`ArenaWaveBehavior.Tick` advances past its conversion to `ForEach` in graph 2010,
node 2068. `Formation Position` still stops at persistent aggregate Set Variable
in graph 2421, node 2471. No Arena speedup is claimed for this change.

Persistent fixed-layout storage, arrays/ForEach, managed ownership, direct host
operations and resumable/instrumented whole activations remain required. The
separate counted-loop benchmark still misses its 1.5x generated-C limit. See
[the completion audit](behavior-completion.md) for the full remaining scope.

Validation completed: `make test`, `make build`, `make web`, native
`--system-arithmetic`, generated-C parity/performance, and `check-behavior-backend`
in the Emscripten and native JIT-disabled builds. Native Arena firing, simulation
and restart passed under SLJIT and bytecode with 63 instances and zero oracle
calls. The whole-function catalog audit deliberately remains nonzero at the
coverage counts above. Native app checks ran with macOS window-service access;
the initial sandboxed Arena launch could not initialize its application.
