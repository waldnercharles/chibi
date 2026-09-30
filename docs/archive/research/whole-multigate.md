# Compiled MultiGate selection

MultiGate now lowers into the shared behavior IR. Input evaluation, source safe
points and output branches execute as SLJIT, portable bytecode or generated C.
An explicit C helper owns and updates the persistent selection mask and returns
an output index. It never calls a graph evaluator. This is whole-function
compiled coverage with a C operation, not an all-SLJIT implementation of the
selection algorithm.

The helper receives a publication-resolved state index and output count plus
typed Start Index and flag arguments. It does no node-ID lookup or generic pin
packing. First execution allocates one instance-owned mask; subsequent calls
reuse it. The context supplies the helper to outlined calls and
prepared Tick rows. Ordered batching remains eligible because this operation
does not invalidate component storage or reenter behavior execution.

## Preserved behavior and ownership

Reset does not evaluate data inputs or allocate an uninitialized mask. Execution
evaluates Start Index, Random and Loop in that order. Exhausted masks stop until
reset unless Loop is enabled. A valid Start Index forces the first output while
still consuming the requested random draw. Invalid indices start searching at
zero. Zero outputs allocate an empty mask on execution and consume no draw.

Each invocation retains its input watches and continuation, while calls and
waits share the instance's node state. The helper changes state before the
selected output executes, matching cancellation and budget-failure behavior.

The ownership tests exposed a preexisting problem: state strings and interned
names used one allocation list, and content interning could merge two mutable
masks or make a name borrow a mask. Interning now excludes mutable state buffers.
Hot apply retains an already owned buffer by pointer without content deduplication.
Reordered state slots remain independent, and changed output counts require a
restart. Pooling clears state and releases mask storage.

## Acceptance and measurement

The fixture matrix covers 0, 1, 2, 5 and 17 outputs; direct and nested calls;
three-iteration loops; and waits. Nine controls cover unobserved execution,
tracing, stepping, breakpoints, cancellation, failure at each data input and
budget exhaustion. Each fixture runs 32 invocations, comparing complete debugger
snapshots, masks, input-effect order and subsequent random draws across oracle,
bytecode, SLJIT and generated C. Separate checks cover missing-instance source
errors, forced first-output random draws, hot apply, pooling, batched Tick and
two independent instances in a prepared update loop.

`tools/bench_multigate.py` measures twelve warmed bodies with 1, 5 or 17 outputs,
random or ordered selection, and one or three selections per invocation. Inputs
change each iteration, masks loop, and every 31st invocation resets them. Each
backend starts from the same seed and initial state. A checksum includes selected
outputs and hit counts throughout the measured run. Warmed execution asserts no
new host buffers, VM scratch or mask allocations.

The four implementations are SLJIT, bytecode, generated C from the same IR, and
a handwritten C driver using the same selection helper. The driver preserves
state/results but omits graph budgets and inspection. Neither C reference proves
parity with an independently optimized selection algorithm. Compilation, cold
mask allocation, instance setup and scheduling are outside these body timings.
Scene and scale comparisons measure regressions separately; current Arena assets
do not contain MultiGate and cannot demonstrate a speedup from this new coverage.

## Measured results

[Five rotated trials](whole-multigate-results.json), 500,000 measured invocations per body:

| Outputs | Selections | Random | SLJIT ns | Generated C ns | C driver ns | Bytecode ns |
| ---: | ---: | :---: | ---: | ---: | ---: | ---: |
| 1 | 1 | No | 8.292 | 7.972 | 5.302 | 109.714 |
| 1 | 1 | Yes | 9.164 | 8.712 | 6.094 | 108.706 |
| 1 | 3 | No | 20.698 | 20.492 | 12.616 | 386.122 |
| 1 | 3 | Yes | 23.340 | 22.406 | 16.192 | 390.024 |
| 5 | 1 | No | 10.984 | 10.090 | 7.066 | 129.724 |
| 5 | 1 | Yes | 17.186 | 16.518 | 13.576 | 133.120 |
| 5 | 3 | No | 27.782 | 26.524 | 18.158 | 453.262 |
| 5 | 3 | Yes | 47.308 | 45.664 | 36.090 | 482.000 |
| 17 | 1 | No | 11.158 | 10.028 | 6.792 | 158.782 |
| 17 | 1 | Yes | 36.850 | 36.008 | 27.640 | 188.444 |
| 17 | 3 | No | 33.566 | 28.948 | 20.742 | 588.694 |
| 17 | 3 | Yes | 101.152 | 100.778 | 75.466 | 610.142 |

All twelve native/generated-C ratios meet the advisory 1.5x target, ranging
from 1.004 to 1.160. Native/C-driver ratios range from 1.266 to 1.643. The
handwritten driver still has less surrounding work, and neither reference
measures an independently optimized selection algorithm.

The initial integration enlarged the context from 448 to 456 bytes on this
64-bit host and shifted existing fields. The final layout puts the new callback
at the end, restoring all earlier field offsets while retaining the size increase.
Arguments and argument count are at offsets 112 and 120 again. Actual cache-line
placement depends on allocation alignment; offsets alone do not establish a cause.

[Five paired layout comparisons](whole-multigate-layout-results.json) isolate
this field move. The median paired bytecode p50 change is +0.007 ms at 1,000
entities, +0.037 ms at 10,000, and -0.538 ms at 20,000. This does not explain
the broad slowdown in the initial integration. The
[initial body](whole-multigate-initial-results.json),
[Scene](whole-multigate-initial-scene-results.json), and
[scale](whole-multigate-initial-scale-results.json) reports remain available.

[Five final baseline pairs](whole-multigate-scale-results.json), 512 sampled
frames each, compare the pre-MultiGate binary with the final layout:

| Backend / entities | Before p50/p95 ms | After p50/p95 ms | Median paired p50 delta ms |
| --- | ---: | ---: | ---: |
| SLJIT/100 | 0.005/0.005 | 0.005/0.006 | +0.000 |
| BYTECODE/100 | 0.046/0.051 | 0.050/0.054 | +0.004 |
| SLJIT/1000 | 0.054/0.056 | 0.054/0.059 | +0.000 |
| BYTECODE/1000 | 0.467/0.485 | 0.513/0.555 | +0.037 |
| SLJIT/10000 | 0.586/0.992 | 0.686/0.973 | +0.010 |
| BYTECODE/10000 | 4.890/6.423 | 5.248/6.922 | +0.388 |
| SLJIT/20000 | 2.519/4.118 | 2.801/4.068 | +0.185 |
| BYTECODE/20000 | 12.363/14.270 | 12.866/14.935 | +0.633 |

Bytecode is slower in all five pairs at 100 and 1,000 entities and four of
five at 10,000 and 20,000. Native results are mixed; three of five 20,000-entity
pairs are slower. The portable integration regression remains open. Context size,
allocation classes and changed VM dispatch code need separate attribution before
acceptance. This coverage addition is not an accepted Scene performance improvement.
These scaling fixtures omit physics and rendering.

A subsequent [immutable VM loop-bound optimization](vm-bounds.md) reduces
bytecode Tick time while retaining this selection helper and context layout.
Its separate report records the newer executable and does not replace these
integration measurements.

[Five final Arena pairs](whole-multigate-scene-results.json) leave native
behavior p50 at 0.008 ms and simulation p50/p95 at 0.013/0.027 ms. Behavior p95
changes from 0.021 to 0.022 ms. Portable behavior stays at 0.017/0.031 ms;
simulation p50 stays at 0.023 ms and p95 changes from 0.038 to 0.037 ms.
Firing/restart and zero-oracle assertions pass. No Scene speedup is claimed.

## Verification

The final context layout passes native `make test`, including the new matrix,
catalog audit, editor checks and application smoke. Generated-C MultiGate parity
passes in the benchmark harness. The regenerated recursive generated-C matrix
also passes after the context ABI change. `make web` builds the application and
executes MultiGate, nested calls, waits and prepared updates as BYTECODE with
SLJIT absent. Native no-JIT checks reject execution as UNAVAILABLE without an
interpreter fallback. These semantic checks do not close the scale regression.

```sh
build/test_suite --whole-multigate
build/test_suite --emit-multigate-c /tmp/multigate.c
clang -std=c23 -O3 -ffp-contract=off -shared -fPIC -I src \
    /tmp/multigate.c -o /tmp/multigate.so
build/test_suite --whole-multigate /tmp/multigate.so
python3 tools/bench_multigate.py --output docs/research/whole-multigate-results.json
```

Retriggerable waits, pure recursion, same-invocation loop re-entry and dedicated
stateful dynamic-recursion/library acceptance remain in the broader completion
work. Existing native tail and portable timing/memory signals remain open.
