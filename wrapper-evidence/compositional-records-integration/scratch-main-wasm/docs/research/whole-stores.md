# Persistent struct stores

Whole-function Set Variable now supports fixed-layout structs, including nested
fields. SLJIT emits direct field and metadata stores into instance-owned backing
storage. Portable bytecode and generated C consume the same instructions. The
stored value may be null while its backing remains allocated; reads still observe
null and projected fields retain their source error. Set outputs use local value
snapshots, so a later assignment cannot change an earlier output.

Known declared type names use instance-owned metadata directly. Dynamic names
from arguments use one explicit ownership callback; the field stores remain
native instructions. The generated-C comparison uses the same ownership callback.
The known-type benchmark replaces that callback with an assertion failure to
verify that it makes no such calls.

Plain backing storage is allocated at instance setup and reused through assignment,
compatible hot apply and pooling. Hot apply remaps it with stable variable IDs and
rejects changed layouts. Managed fields retain the arena-backed path and current
restart policy. Public variable copies now retain type names independently of the
instance as well as copying data; an ASAN lifecycle test exposed the previous
borrowed-name lifetime bug.

## Measurements

```sh
python3 tools/bench_components.py --workload store --trials 5 --iterations 1000000
```

The Release benchmark uses Clang 23.1.1, `-O3 -ffp-contract=off`, 10,000 warmup
calls and rotating backend order. Inputs change and checksums are verified. Each
timed body includes argument loads, field computation/copy, persistent stores,
local result snapshots, returns and budget checks. Caller result storage is reused;
scheduler, publication and allocation setup are outside the timing.

| Whole body | SLJIT ns/call | Generated C ns/call | Bytecode ns/call | SLJIT/C |
| --- | ---: | ---: | ---: | ---: |
| Computed value, known type | 6.283 | 5.497 | 154.538 | 1.143 |
| Argument copy, dynamic name | 9.249 | 7.616 | 132.236 | 1.214 |

Both pass the unchanged **1.5** limit. These are small kernels, not Tick timings.
The VM still allocates its slot array per invocation. [Raw measurements](whole-stores-results.json)
include all trials, the compiler and executable SHA-256.

Five paired Arena runs compare baseline `bf82c334` with this runtime change,
alternating run order with seed 12345, 30 warmup frames and 150 measured frames.

| Backend | Behavior p50 before/after, µs | Behavior p95 before/after, µs | Simulation p50 before/after, µs |
| --- | ---: | ---: | ---: |
| SLJIT | 47 / 47 | 87 / 82 | 54 / 53 |
| Bytecode | 52 / 53 | 76 / 80 | 59 / 59 |

Trial ranges overlap; **no median Arena speedup is demonstrated**. Each run
passed firing and restart with 63 behavior instances and zero oracle calls.
[Paired results](whole-stores-arena-results.json) retain per-run records and both
executable hashes. The Arena executable predates the final test-only hot-apply
extension; its runtime/compiler code is the same as the final kernel benchmark.

## Correctness and remaining coverage

Oracle, whole SLJIT and bytecode tests compare nested calls, computed/copy/null
stores, loop writes, retained Set outputs, budgets 1–17, partial state on error,
source nodes/pins and call stacks. Generated C is compared against bytecode on
the same bodies and error cases. Native ASAN tests also cover input-name ownership,
independent snapshots, compiled execution after variable reordering during hot
apply, pool reuse, and managed structs retaining their existing ownership.

Validation passed: `make test`, `make build`, `make web`, generated-C parity and
performance, native JIT-unavailable rejection and paired Arena execution. The
final test-only hot-apply extension passed focused Debug/ASAN and Release tests.
The web build runs the persistent-store fixture under bytecode with no SLJIT.

Whole-function coverage increases to **4/31 effectful local functions**, adding
`ArenaWaveBehavior.Formation Position`. Coverage remains **5/33 events**,
**39/39 pure local functions** and **3/4 library functions**. The strict catalog
gate still returns nonzero. Wave Tick stops at `ForEach` (graph 2010, node 2068).
Arrays, managed ownership, host effects, resumable and instrumented whole
activations remain open, as does the separate counted-loop native/C performance
gate. See [the completion audit](behavior-completion.md).
