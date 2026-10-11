# Rhythm and reflected native calls

The authored catalog now passes whole-function compilation: 33/33 events,
31/31 effectful local functions, 39/39 pure functions and four linked library
functions across 19 assets. The [audit](whole-reflected-audit.txt) enumerates
linked snapshots and now runs in `make test`. It fails when a future authored
entry loses coverage and reports its graph, node and pin.

This is catalog coverage, not completion of every language operation, debugger
case, crowd workload or standalone export in the [completion criteria](behavior-completion.md).

## Scene Rhythm

Scene Rhythm emits checks and clock arithmetic into the shared IR. The compiler
adds generic context reads and checked component-column metadata access; there
is no rhythm-specific native backend. SLJIT emits the arithmetic directly,
including hardware floor on ARM64. Other targets may use the existing scalar
math ABI. Bytecode and generated C consume the same instructions.

The ECS execution view now exposes component population and the XOR of occupied
slots. When population is one, that value identifies its row. Add/remove/destroy
update both in constant time; growth and migration retain them. This supports
an optional singleton without scanning the world. The C rhythm sampler uses the
same metadata. The sampler retains defaults when no clock exists, rejects
multiple clocks and invalid settings, and preserves negative-time phase behavior.

Instance simulation time is distinct from activation wait time. Compiled
contexts retain the instance clock address, and prepared Tick rows bind their
own address even when they share the compiled program and service binding.
Wait resumption reacquires current component storage. Reflected layout guards
retain source diagnostics after incompatible component changes. The node declares
its specific Rhythm read for effect analysis.

[Five rotated trials](whole-rhythm-results.json), 500,000 warmed calls each:

| Access | SLJIT ns | Generated C ns | Bytecode ns | SLJIT/C |
| --- | ---: | ---: | ---: | ---: |
| Column lookup | 12.128 | 5.744 | 273.634 | 2.111 |
| Bound column | 12.074 | 4.854 | 271.854 | 2.487 |

Both miss the advisory 1.5x target. The absolute difference is about 6–7 ns per
call, and the native body is about 22.5x faster than bytecode. Timing excludes
compilation, setup and scheduling; it includes a nested function and four
persistent output stores. These are not whole-Scene speedups.

## Reflection as a coverage shortcut

Reflection now generates a `node_host_call` adapter for eligible `NODE_READ`
functions automatically. They must declare a pure value operation with no
services or other effects. Raw string outputs are excluded because ownership is
not established by their C type. The whole compiler also checks supported input
layouts and plain output ownership. Unsupported signatures retain diagnostics;
reflection does not infer arbitrary C semantics or memory ownership.

The adapter calls the declared native C function with typed arguments and copies
results into caller-owned storage. It never invokes `eval_node` or `flow_run`,
allocates, or retains borrowed arguments. Headers and struct buffers are prepared
outside execution and reused. A NULL service in this ABI means a value operation
that needs no host service. Exposed fields project from a native call's retained
aggregate output instead of changing the native signature.

This compiles Primitive Part and Transform Visual Offset without a handwritten
lowering for either. It adds ArenaDrop.Begin, Emit Source Frame and Spawn Source
Beam. Scene Rhythm separately adds ArenaPickup.Tick and Wait and Pulse. The
reflection tests include a new function with NODE_OUT parameters interleaved
between inputs. This exposed an input-index bug in the old generated read
adapter; both adapters now use compact input indices. Null struct arguments now
produce an error instead of being dereferenced.

[Five rotated trials](whole-reflected-results.json), 500,000 warmed calls each:

| Nested fixture | SLJIT ns | Generated C ns | Bytecode ns | SLJIT/C |
| --- | ---: | ---: | ---: | ---: |
| Primitive Part | 28.800 | 19.276 | 438.944 | 1.494 |
| Transform Visual Offset | 41.188 | 21.886 | 438.930 | 1.882 |
| Interleaved scalar outputs | 7.822 | 6.546 | 149.274 | 1.195 |

The transform fixture misses the advisory ratio. Timings include argument
packing, the native call, nested function machinery and persistent result stores.
Warmed execution allocates no host data or VM scratch. C and SLJIT call the same
opaque helper; this comparison cannot prove parity with a handwritten, inlined
implementation. The current generated C uses runtime call bindings. Static
bindings and helper visibility/LTO remain work for the complete AOT export.

Use direct IR for inexpensive arithmetic, control flow and common field access.
Use typed C calls for substantial algorithms when their measured call/packing
cost is acceptable. Opaque calls prevent SLJIT inlining and optimization through
their bodies. Accurate effect/ownership contracts preserve more optimization
around them; coarse batch helpers can remove more overhead than per-entity
expansion. Neither a native label nor a good same-helper C ratio proves good
query complexity or data locality.

## Scene results and verification

[Seven paired Arena runs](whole-rhythm-reflection-scene-results.json) compare the
saved search-stage executable with rhythm/reflection, profiling disabled, 180
fixed steps and 30 warmup frames. Native behavior p50/p95 remain 0.008/0.022 ms;
simulation p50/p95 remain 0.013/0.027 ms. Bytecode behavior p50 changes from
0.017 to 0.018 ms, while p95 and simulation metrics stay unchanged. This small
firing/restart workload shows no native Scene gain. Its final live snapshot is
22/22 whole entries and omits unused/retired assets; the separate catalog audit
is what proves all authored bodies compile.

Oracle/bytecode/SLJIT/generated-C tests compare state, source errors, call stacks,
watches and traversals through nested calls, waits, cancellation, budgets,
stepping and breakpoints. Rhythm cases include defaults, duplicate/invalid clocks,
component removal and changed settings at waits, and distinct clocks in a shared
native Tick batch. Reflected calls test multiple outputs, projected struct fields,
null arguments, domain errors and retained caller inputs. ECS growth tests check
population/singleton metadata after large additions and destruction.

Native `make test` and `make build` pass, including reflection, runtime-capability
smoke and the gameplay suite. Emitted-C parity and actual native Arena firing and
restart pass with zero oracle calls. `make web` builds the app and executes actual
rhythm and reflected geometry through bytecode with SLJIT absent. Native no-JIT
checks reject execution without oracle fallback. Generated-C component read/write
checks also pass after the shared column-lookup change.

```sh
build/test_suite --whole-function-audit
build/test_suite --whole-rhythm
build/test_suite --whole-reflected
python3 tools/bench_rhythm.py --output /tmp/rhythm-results.json
python3 tools/bench_reflected.py --output /tmp/reflected-results.json
```
