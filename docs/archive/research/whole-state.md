# Compiled persistent flow state

DoOnce, DoN, FlipFlop and Gate now lower into whole-function loads, stores and
branches in the shared IR. SLJIT, portable bytecode and generated C use the same
fixed state-slot offsets. Flag/counter operations make no state-helper call and
do not look up a node ID during execution.

Node descriptors declare whether they need an instance state slot. Publication
indexes those nodes across the linked asset snapshot, and each instance owns a
contiguous bank of zero-initialized values. The existing node-ID scratch API
resolves declared nodes to this bank; arbitrary scratch remains separate and can
grow without moving compiled state. Assets without state nodes allocate no bank.
The context binding used by ordinary execution, batching, prepared Tick loops
and outlined calls carries the instance bank. It is not shared between entities.

Hot apply remaps initialized values by stable node ID after checking descriptor,
reference and pin-count compatibility. The current update API waits for idle
instances. Slot reordering and insertion do not lose state; pooling and rebind
clear it. Subsequent [MultiGate compilation](whole-multigate.md) uses the same
bank with an explicit selection helper. Retriggerable Delay remains unfinished.

## Semantics and inspection

DoOnce evaluates Start Closed only while its state is uninitialized. Reset marks
it open and initialized without evaluating that input. Gate initializes before
Enter/Open/Close/Toggle; later operations preserve its initialized state. DoN
skips N on Reset, publishes the pre-increment counter on execution, then advances
only when below the limit. FlipFlop stores its toggled value before choosing an
output. State persists across invocations while output values remain private to
each invocation.

Skipped data inputs still expose their last per-invocation value to watches.
Compiled observation keeps a separate snapshot without executing the skipped
expression. Current data-evaluator semantics also reevaluate DoN's inputs when
its stored Counter output is read. The compiler preserves those effects without
rerunning the counter operation. Removing that reevaluation would be a separate
language change, not an assumed pure-call optimization.

The fixtures exercise 32 combinations of node family, loops, nested calls and
waits. Each runs an eleven-invocation sequence that changes inputs and exercises
control pins. Six modes include tracing, stepping, breakpoints, input failures
and reset-before-first-execution. They compare complete inspection snapshots,
state-bank values and input-helper call counts across oracle, bytecode, SLJIT
and generated C. Separate assertions cover lazy initialization, state isolation,
batched Tick, prepared scheduler loops, missing-instance errors, slot migration,
compiled execution after migration and pool reset.

These tests exposed invalid constant reuse across an expression branch join:
the compiler could reuse a constant defined only on the skipped path. Binding a
local label now ends the region in which constants can be reused. Gate Toggle
after initialization exercises the formerly invalid path.

Dynamic recursive calls that access state, linked stateful library functions,
broader cancellation/budget combinations and retriggerable waits
still need dedicated acceptance. The existing recursive generated-C matrix
checks the changed context ABI, but does not itself prove recursive state-node
semantics.

## Measurement method

`tools/bench_state.py` compares eight synchronous bodies: each node family with
and without a three-iteration loop. Each call cycles Enter/Exec and control-pin
actions against persistent state. Inputs and state are reset before warmup and
measurement, with 1,024 warmup calls, one million measured calls and five trials
in rotating backend order. An independent state-machine model checks the sum of
the cumulative hit count. Compilation, instance creation and activation/host
binding are excluded; argument updates and invocation-state reset are included.

Generated C uses the same IR and context ABI. The reference therefore measures
backend overhead rather than equivalence to an independently optimized C game.
Arena and scaling comparisons are separate regression workloads. The 1.5x body
ratio remains advisory; all misses must be recorded.

[Five million-call trials](whole-state-results.json) give these median costs per
complete body invocation on the current native host:

| Body | SLJIT ns | Generated C ns | Bytecode ns | Native/C ratio |
| --- | ---: | ---: | ---: | ---: |
| DoOnce | 2.891 | 2.312 | 67.040 | 1.250 |
| DoN | 3.250 | 3.016 | 87.754 | 1.078 |
| FlipFlop | 3.687 | 2.942 | 86.307 | 1.253 |
| Gate | 3.543 | 2.880 | 80.344 | 1.230 |
| DoOnce, three iterations | 6.321 | 5.006 | 243.378 | 1.263 |
| DoN, three iterations | 6.310 | 5.708 | 286.627 | 1.105 |
| FlipFlop, three iterations | 8.266 | 5.762 | 321.276 | 1.435 |
| Gate, three iterations | 7.371 | 5.787 | 308.963 | 1.274 |

All eight meet the advisory 1.5x target. These are newly covered body timings,
not a before/after measurement of the prior per-node execution path. They do not
establish a Scene speedup or include instance/activation setup.

[Five paired Arena trials](whole-state-scene-results.json), with detailed
profiling and presentation disabled, leave native behavior p50/p95 unchanged at
0.008/0.022 ms and simulation at 0.013/0.027 ms. Bytecode behavior p50 changes
from 0.017 to 0.018 ms with p95 unchanged at 0.032 ms; simulation changes from
0.023/0.037 to 0.023/0.038 ms. Both executables pass firing/restart assertions
with zero oracle calls. This slice does not establish an Arena speedup.

[Three paired scale trials](whole-state-scale-results.json), 256 sampled frames
each, give these medians of per-run Tick p50/p95 in milliseconds:

| Backend / entities | Before p50/p95 | After p50/p95 |
| --- | ---: | ---: |
| SLJIT / 100 | 0.005 / 0.005 | 0.005 / 0.005 |
| SLJIT / 1,000 | 0.052 / 0.053 | 0.052 / 0.054 |
| SLJIT / 10,000 | 0.590 / 0.866 | 0.595 / 0.881 |
| SLJIT / 20,000 | 2.658 / 3.624 | 3.236 / 4.199 |
| Bytecode / 100 | 0.046 / 0.048 | 0.047 / 0.051 |
| Bytecode / 1,000 | 0.450 / 0.488 | 0.457 / 0.510 |
| Bytecode / 10,000 | 5.056 / 5.816 | 5.609 / 6.384 |
| Bytecode / 20,000 | 13.523 / 14.625 | 12.411 / 14.155 |

The 10,000-entity bytecode p50 worsens in all three pairs, by 1.032, 0.362 and
0.091 ms. Its median paired increase is 0.362 ms, so this is an open regression
signal despite the faster 20,000-entity result. Native 20,000 p50 changes by
-0.158, -0.143 and +1.633 ms; the separate medians conceal strong run-to-run
variation. No stable native scale improvement is claimed. The earlier native
10,000-entity tail regression is also still open. These fixtures omit physics
and rendering and cannot establish playable large-scene frame rates.

[Five longer bytecode pairs](whole-state-scale-repeat-results.json), 512 sampled
frames at 10,000 entities, give p50 4.735/4.767 ms and p95 6.630/6.090 ms. The
paired p50 deltas are +0.037, +0.032, -0.045, +1.201 and +0.093 ms; the median
paired increase is 0.037 ms, about 0.8% of the baseline. Four of five pairs are
slower, but the original 0.362 ms median increase is not reproduced. A large
outlier remains. Peak process memory changes from 106.516 to 110.328 MiB and
setup from 61.994 to 62.849 ms. These results leave a smaller performance and
memory signal to attribute; they do not prove the initial slowdown disappeared.
No source or executable changed between the first and repeated comparison.

## Verification

Native `make test` passes with sanitizers, including the state snapshot matrix,
Scene/editor checks, catalog coverage and application smoke. The generated-C
state matrix and regenerated recursive-call matrix pass. `make web` builds the
application and runs persistent state, batched Tick and prepared-loop smoke as
BYTECODE with SLJIT disabled. The separate native no-JIT build rejects core
behavior execution and runtime capabilities as UNAVAILABLE without interpreter
fallback. Targeted formatting and `git diff --check` pass. Performance signals
above and the language/lifecycle exclusions remain open acceptance work.

```sh
build/test_suite --whole-state
build/test_suite --emit-state-c /tmp/state.c
clang -std=c23 -O3 -ffp-contract=off -shared -fPIC -I src \
  /tmp/state.c -o /tmp/state.so
build/test_suite --whole-state /tmp/state.so
python3 tools/bench_state.py --build-dir build-performance \
  --output docs/research/whole-state-results.json
```
