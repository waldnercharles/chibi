# Compiled Retriggerable Delay

Retriggerable Delay now lowers into whole SLJIT, portable bytecode and generated-C
bodies. Its duration and deadline arithmetic, availability check, conditional
continuation and suspension use the shared typed IR. An explicit runtime helper
finds an older suspended activation at the same source node in the same instance
and changes its deadline. It never calls the interpreter or evaluates graph pins.

Retriggering preserves the older invocation's event arguments. The new invocation
skips the delayed output but still executes pending Sequence work. Host waits are
excluded from deadline retiming. Nested and recursive calls identify the source
through the immutable compiled definition group. Cached debugger views receive
the updated wake time as well as the underlying continuation frame.

The runtime context remains 456 bytes on this arm64 host. The callback uses the
space previously occupied by ownership flags and padding; those flags move into
existing padding after the argument count. Generated code uses the shared context
layout and offsetof. The generated-C recursive matrix was rebuilt against the
new header to check existing outlined calls too.

## Validation

The parity matrix exercises 450 combinations of direct, nested, recursive and
looping calls, duration boundaries, execution budgets, stepping, breakpoints,
paused waits and cancellation. Oracle, bytecode, SLJIT and generated C compare
variables, errors, call stacks, watched values, traversal records and wake times.
Generated-C comparisons replace the root and every outlined callee. Independent
assertions check retained original inputs and pending Sequence tails.

Lifecycle cases check separate instances, cancellation, activation pooling,
compatible hot apply after source-node reordering and a paused invocation that
later retriggers another activation. Active instances reject hot apply while
their activations remain outstanding. Missing runtime context produces a source
node diagnostic rather than silently accepting an unsupported wait.

Native `make test` passes with sanitizers. `make web` builds the application and
executes the Retriggerable Delay smoke as BYTECODE with SLJIT absent. The native
no-JIT core and runtime smoke tests report UNAVAILABLE without interpreter
fallback. The recursive generated-C regression also passes.

## Performance scope

Arena currently does not use this node. The paired ArenaEnemy and Scene checks
measure integration regressions, not a new Arena speedup. This change completes
one language operation; it does not complete every debugger/language combination
or provide a standalone Scene AOT export.

The [initial integration comparison](whole-retrigger-initial-scale-results.json)
found a repeatable bytecode regression, with 1,000-enemy p50 rising from 0.427 to
0.463 ms and 20,000-enemy p50 from 12.924 to 13.652 ms. ARM64 disassembly showed
the VM zeroing its operand snapshot before overwriting it with the selected
input. Operand snapshots now directly select the input or a zero value. This
preserves aliasing semantics and moves zero initialization into the absent-input
branch. Opcode dispatch and register allocation also changed; the experiment
does not isolate the entire regression to those stores.

The [final five paired trials](whole-retrigger-scale-results.json) use the saved
pre-Delay executable, Release builds, 512 measured frames after eight warmups,
disabled profiling, actual ArenaEnemy components and five attachments per enemy.
Physics and rendering are excluded. Checksums match with zero oracle calls.

| Backend | Enemies | Before p50 ms | After p50 ms | Paired p50 delta ms |
| --- | ---: | ---: | ---: | ---: |
| SLJIT | 100 | 0.005 | 0.005 | 0.000 |
| SLJIT | 1,000 | 0.054 | 0.053 | -0.001 |
| SLJIT | 10,000 | 0.735 | 0.656 | -0.044 |
| SLJIT | 20,000 | 4.070 | 4.262 | +0.029 |
| BYTECODE | 100 | 0.042 | 0.040 | -0.002 |
| BYTECODE | 1,000 | 0.426 | 0.405 | -0.021 |
| BYTECODE | 10,000 | 6.008 | 5.908 | -0.134 |
| BYTECODE | 20,000 | 12.886 | 12.684 | -0.158 |

The repeatable bytecode regression is absent in this final comparison. Large
populations still vary across trials: the native 20,000-enemy paired p50 changes
range from -0.017 to +0.704 ms. These results establish no new native speedup and
do not close the earlier native tail-latency investigation. Use paired differences
rather than subtracting separate medians or comparing different sessions.

[Five paired Scene firing/restart checks](whole-retrigger-scene-results.json)
retain native behavior p50 at 0.008 ms and simulation p50/p95 at 0.013/0.027 ms.
Bytecode behavior p50 stays at 0.017 ms and simulation p50 at 0.022 ms. Both
backends report zero oracle calls. These hidden-host checks omit presentation
and do not establish large playable-scene performance.

The final measurements use executable SHA-256
`034fa58c09e778945a68791f7343ac1484906e55c46f730052a4220f4ec00072`.

Reproduce the focused checks with `build/test_suite --whole-retrigger` and
`--emit-retrigger-c /tmp/whole-retrigger.c`. Compile the emitted source with
Clang `-std=c23 -O3 -shared -undefined dynamic_lookup -Isrc`, then pass the library
path to `--whole-retrigger`. `--whole-recursion` accepts its independently emitted
library in the same way. Run `make test`, `make web` and the no-JIT
`check-behavior-backend` target for the broader checks.
