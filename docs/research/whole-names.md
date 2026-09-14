# Whole string-to-name conversion

The explicit `To Name` node and `To Name` edge conversion now lower into whole
functions. This removes the last exclusion in SpawnTelegraphBehavior.Tick.
The [catalog audit](whole-names-audit.txt) now reports 29/33 events, 26/31
effectful local functions, 39/39 pure local functions and 4/4 library functions.
These counts describe this branch's 19 assets. Full language coverage remains
unfinished.

The conversion preserves the language contract: a null string becomes an empty
name, the result carries `name` metadata, and converted text remains valid after
its input changes. Explicit node and edge forms share one lowering. This does
not introduce implicit string/name compatibility.

The compiler emits a typed string-copy operation with an empty-string fallback.
SLJIT emits the null selection and calls the existing arena-copy capability;
bytecode and generated C use the same operation. The helper performs allocation,
`strlen` and `memcpy`. It does not evaluate graphs or dispatch nodes. Calls,
returns, variable accesses and surrounding control flow remain compiled.
Retained locals and caller-owned results preserve strings across waits and
source replacement. Allocating bodies still use whole activations rather than
the prepared Tick batch path.

## Measurements

[Five Release trials](whole-names-results.json) rotate SLJIT, bytecode and
generated C over 50,000 invocations after 64 warmup calls. Each body stores a
string variable, converts through a pure or effectful function, passes the name
through another function and stores its result. Timing excludes compilation and
activation setup. Generated C uses Clang 23, `-O3 -ffp-contract=off` and the same
ownership helpers.

| Call variant | SLJIT ns | Generated C ns | Bytecode ns | SLJIT/C |
| --- | ---: | ---: | ---: | ---: |
| Effectful | 94.20 | 92.34 | 235.06 | 1.020 |
| Pure | 87.38 | 86.78 | 188.48 | 1.007 |

Both meet the advisory 1.5x native/C target. SLJIT is 2.16–2.50x faster than
bytecode here. The shared helpers retain allocation costs in both native
variants; this comparison does not establish parity with handwritten C that
eliminates those copies. Warm invocations allocate no host banks or VM scratch,
but persistent variable assignments still allocate and string copies consume
arena storage.

[Seven paired Arena runs](whole-names-scene-results.json) alternate saved
before/after executables with profiling disabled. Each runs 180 fixed frames
with firing and restart, discards 30 warmup frames, and asserts zero oracle calls.
Times are medians of each run's percentile measurements, in milliseconds.

| Backend | Behavior p50 before/after | Behavior p95 before/after | Simulation p50 before/after | Simulation p95 before/after |
| --- | --- | --- | --- | --- |
| SLJIT | 0.011 / 0.008 | 0.025 / 0.022 | 0.015 / 0.013 | 0.029 / 0.027 |
| Bytecode | 0.019 / 0.017 | 0.032 / 0.031 | 0.024 / 0.022 | 0.039 / 0.037 |

A [separate instrumented run](whole-names-profile.json) recorded 360
SpawnTelegraph.Tick dispatches and eight resumes in both executables. Native
execution fell from 2.227 to 0.940 microseconds per measured frame; setup fell
from 0.353 to 0.173. This single attribution run is subject to timer resolution
and profiling overhead. Use the paired uninstrumented runs for Scene timing.

The smoke's end-of-run live coverage remains 20/22 because telegraphs retire
before that snapshot. This snapshot misses transient assets. The catalog audit
and execution profile answer different coverage questions. The remaining live
exclusions are ArenaEnemyVisual.Contact Begin and Damaged. These small Arena
results do not establish performance with thousands of active enemies or include
frame presentation cost.

## Validation and remaining work

Oracle, bytecode, SLJIT and generated-C snapshots agree for explicit/edge
conversions, pure/effectful nested calls, null/empty/UTF-8 input, owned returns,
caller input mutation, variable replacement during waits, cancellation, step
budgets, breakpoints, stepping, watches, traversals and call stacks. Direct
returned strings and type names survive source mutation and program destruction.
Compiled cases assert whole-function execution and zero oracle calls.

Native `make test`, `make build`, ASan/UBSan Arena execution and emitted-C parity
pass. `make web` passes with the new BYTECODE smoke. The native build with SLJIT
disabled reports UNAVAILABLE and rejects execution without interpreter fallback.
Changed-file formatting, Python compilation and `git diff --check` pass.

Reproduce the new checks and measurements:

```sh
build/test_suite --whole-names
build/test_suite --emit-name-c /tmp/names.c
clang -std=c23 -O3 -ffp-contract=off -shared -fPIC -Isrc /tmp/names.c -o /tmp/names.so
build/test_suite --whole-names /tmp/names.so
python3 tools/bench_names.py --build-dir build-performance --output /tmp/names.json
```

The remaining catalog exclusions start at Primitive Part, Transform Visual
Offset, Trigger Visual Reaction, Scene Rhythm and Contains. Managed arrays,
remaining language operations, complete debugger coverage, scheduler/state
locality and complete standalone Scene AOT exports remain tracked in
[the completion criteria](behavior-completion.md). The generated C measured
here exports function bodies; it is not yet a runnable Scene export.
