# Compiled continuation foundation

September 12, 2026; baseline `97b5a4ad`, isolated compiled-behavior branch.

Whole-function IR now has an explicit suspension instruction. SLJIT spills live
values and returns to the caller. A later invocation restores those values and
jumps directly to the generated continuation. Bytecode and generated C share the
same continuation positions, typed scalar storage and logical frame budgets.
Tests can alternate backends on every resume without retaining a native stack.

This is a compiler-boundary implementation. Authored Delay and Spawn Prototype
still use the general compiled runtime. No additional catalog body qualifies
for whole-function execution in this checkpoint. The next integration must own
these activations in `flow_run`, connect timer and host-request completion,
preserve initialization-before-Begin ordering, retain program/value lifetimes,
and support inspection activated while waiting. Full source-level debugging and
managed values across arbitrary waits remain open.

## State and lifetime

A backward liveness analysis follows both branch edges and loop back edges.
Suspensions save only live SSA values and locals, together with active parent
budgets. Native countdown budgets are converted to ordinary step counts at the
boundary so another backend can resume. A native jump table selects the restore
block in constant time. Fresh entries initialize their locals; resumes restore
saved locals. Synchronous functions have no continuation dispatch or allocation.

The caller initializes and releases continuation storage once per activation.
It must retain the program generation and borrowed argument/value/service
storage until completion or cancellation. The host wrapper retains command
argument buffers and query bindings while suspended, releases its own bindings
on completion/error, and supports explicit release on cancellation. Bytecode allocates its scratch cache
on first use and reuses it across resumes and fresh runs of the same activation.
Native-only activations do not allocate that cache. These rules
do not yet provide ownership for arbitrary borrowed arrays across Scene waits.
The optimizer treats suspension as a component-address and local-forwarding
barrier.

## Correctness checks

The fixtures insert suspension instructions into real lowered loop/call graphs,
either after every source-node check or between individual expression
instructions. They compare final variables, returns, error node/pin and step
budgets with the interpreter oracle. At every suspension they compare position,
active frame, frame/root steps and persistent state across bytecode, SLJIT,
alternating backends and generated C. The matrix includes ten loop shapes and
budgets 1 through 45, nested calls, forward/backward branches, Break, early
Return, and errors after earlier effects. A focused F32/F64 regression preserves
`A` across a suspended `A + B/C` expression: a floating divide's unused target
field must not kill a live slot as an integer remainder definition would. That
regression failed before the liveness fix and passes across all three backends.

Additional oracle comparisons cover nine command fixtures: partially assembled
host arguments, signed widths, handles, floating-point bits/subnormals, scalar
names, owned persistent stores, nested calls, host errors and cleanup after
completion or cancellation. Native and WASM smoke tests run nested continuations and floating live values
without calling the oracle. The WASM run uses only bytecode.

The existing synchronous command benchmark also passes after the continuation
changes: SLJIT 82.290 ns, generated C 73.195 ns, bytecode 520.675 ns; native/C
ratio 1.124. [Regression measurements](whole-suspend-command-results.json).

Final `make test`, `make build` and `make web` pass. The web smoke reports BYTECODE
continuation execution with zero oracle calls, and the native no-JIT smoke reports
UNAVAILABLE with no interpreter fallback. Arena firing/simulation/restart also
passes with SLJIT and bytecode, 63 instances and zero oracle calls. The strict catalog audit still reports
13/33 event entries, 10/31 effectful local bodies, 39/39 pure local bodies and 3/4
library bodies; its nonzero exit is expected until the migration is complete.

## Measured resume cost

Release Clang 23.1.1, Apple arm64, `-O3`, sanitizers off. Five rotating trials of
200,000 complete activations after 10,000 warmups; no concurrent builds or tests.
The workload runs an outer range with nested function calls and an inner loop,
suspending at every executed source-node check. Each measured activation makes
35.5 backend entries/resumes on average. Native and generated C call the same
context ABI; initialization/allocation is outside the measured loop. Bytecode
reuses its scratch storage after the first warmup entry. This is not Scene/scheduler
or presentation timing.

| Implementation | Median per complete activation | Range |
|---|---:|---:|
| Initial SLJIT linear resume dispatch | 203.975 ns | 201.675–211.400 ns |
| SLJIT jump table and fresh-only local initialization | 122.575 ns | 119.385–123.160 ns |
| Bytecode before scratch reuse | 2925.835 ns | 2879.460–3023.100 ns |
| Bytecode after initial scratch reuse | 933.520 ns | 904.965–950.845 ns |
| SLJIT, final verified run | 124.565 ns | 121.970–130.795 ns |
| Generated C, final verified run | 108.185 ns | 105.175–110.160 ns |
| Bytecode with scratch reuse, final verified run | 1036.650 ns | 1019.440–1046.730 ns |

The initial native/C ratio was 2.226 and failed the 1.5 limit. The revised ratio
is 1.303 after native dispatch optimization. Reusing bytecode scratch and directly
indexing its continuation map then reduces bytecode time by about two thirds.
The final verification includes the floating-division regression. Its paired
native/C ratio is 1.151 and passes. Native time stays about 125 ns; generated
C varies between the builds/runs, so its change must not be credited as a native
optimization. SLJIT is about 8.3 times faster than optimized bytecode for this
workload. This must not be extrapolated to complete gameplay. Recorded runs include checksums, compiler,
binary hash, trial order and raw measurements:
[initial results](whole-suspend-initial-results.json),
[native dispatch results](whole-suspend-jump-results.json),
[initial scratch reuse results](whole-suspend-cache-results.json),
[final verified results](whole-suspend-results.json).

```sh
build/test_suite --whole-suspend
python3 tools/bench_components.py --workload suspend --trials 5 --iterations 200000
# The benchmark also compiles generated C and verifies continuation parity.
make test
make build
make web
```
