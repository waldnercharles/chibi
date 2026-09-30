# Whole Spawn and asynchronous commands

Spawn Prototype now runs inside whole compiled event and function bodies. The
compiler emits its arguments, the host call, suspension, result loads and the
remaining control flow. SLJIT, behavior bytecode and generated C consume the same
lowered program. The engine still owns prototype cloning, allocation, deferred
structural changes and Begin delivery through its existing C services.

The catalog gate advances from 13 to 16 of 33 events, 10 to 16 of 31 effectful
local functions, and three to all four library functions. All 39 pure local
functions still qualify. New whole entries include ArenaWaveBehavior.Tick,
TwinStickWeaponBehavior.Tick and ArenaImpactFeedback.Damaged. The linked
WeaponActions.Fire Projectile function now qualifies too.
[Exact exclusions](whole-async-audit.txt) still return a nonzero completion gate.
Full coverage remains unfinished.

## Ownership and execution

The command descriptor now distinguishes a synchronous invocation from an
asynchronous begin. Begin receives typed arguments and a host request. Success
transfers a reference to the service. Failure must not retain the request or
queue work, and its diagnostic preserves the source node and logical call stack.

The runtime retains typed command results alongside the compiled activation.
Host completion checks the declared output type, container and nominal name.
Completion without an output preserves the node's previous/default value.
Cancelled or detached requests reject later completion, including after pooling
and destruction. Structural mutation invalidates cached component access across
the compiled suspension.

Names and strings now have pointer-valued slots for immutable constants, copied
event arguments and nested call locals. This permits Spawn identities and host
arguments without string-node dispatch. It does not implement arbitrary managed
string variables, root results or string-containing aggregates. Those operations
remain explicit exclusions until their ownership is lowered.

The Spawn host service shares prototype preparation with the oracle. It keeps
the existing initializer-before-Begin barrier, FIFO queue, cancellation and
deferred destruction. Tests run both engine and prototype-preview hosts with a
nested Spawn followed by Delay, with normal completion, breakpoints and cancellation.

## Inspection

Host waits queue the next node before suspension. Compiled source stacks and
traversal timestamps now represent that ordering, while timed Delay retains its
existing scheduling semantics. Completion updates the recorded output in the
correct logical child frame before execution resumes.

If a host retains a completed call while another invocation begins, compiled
inspection retains its watch snapshot. A later call prunes that snapshot once
the host releases the request. These records contain inspection data, not an
executable child runtime store. Ordinary execution omits observation callbacks.

The shared runtime snapshot test compares root and child states, steps, wake
times, inputs, variables, source stacks, errors, watches and ordered traversal
records. The asynchronous matrix covers deferred and immediate completion, missing services, failures
on the first or second request, invalid completion metadata, omitted outputs,
pause, single stepping, breakpoints, early host release, backend switching and
inspection enabled after host completion. The latter keeps recorded values from
the completed call when the host still retains its request.
Generated C runs through the same runtime activation and host request APIs.

## Measured cost

Native arm64, Clang 23.1.1, Release `-O3`, sanitizers disabled and
`-ffp-contract=off`. Five trials rotate four backend modes. Each mode warms 32
activations and measures 20,000. Each activation makes three nested calls with
two timed waits and one asynchronous host wait per call, for nine resumptions.
Both dynamic input names and an escaped constant string reach the C host.

Timings include dispatch, pooled activation setup/release, scheduler work, host
requests, argument copying, completion and captured-payload cleanup. They exclude
graph publication and real waiting wall time. No build, test or other benchmark
ran concurrently. Every mode asserts identical final state and request count.

| Backend | Median ns/activation | Trial range, ns |
| --- | ---: | ---: |
| SLJIT | 870.80 | 824.25–915.95 |
| Generated C | 868.75 | 858.25–876.10 |
| Bytecode | 1735.60 | 1698.10–1772.90 |
| Oracle | 4776.10 | 4681.45–4819.35 |

SLJIT/generated-C is 1.002. The close result means the emitted native body adds
little overhead relative to C through this same runtime. It does not establish
equivalence to a handwritten ECS loop, nor isolate each host helper's cost.
SLJIT is 1.99x faster than bytecode and 5.48x faster than the oracle in this fixture.
Compiled modes create zero additional runtime stores after warmup. The oracle
creates 40,000 child runtime stores across 20,000 activations.
[Raw trials and source hash](whole-async-results.json).

The native/C target remains advisory at 1.5x. Correctness, complete measurements,
required backend availability and zero oracle fallback remain hard failures.

## Acceptance and reproduction

`make test`, `make build`, the focused Spawn test and generated-C comparison passed under
ASAN/UBSAN. The real Arena smoke passed firing, simulation and restart on SLJIT
and bytecode with 63 instances and zero oracle calls. Live Arena whole-function
coverage rose from 9 to 12 of 22 entries. Its ordinary Tick batch count remains
358 in 180 frames because asynchronous events retain full activations.
The Emscripten build and bytecode host-wait smoke pass with no SLJIT execution.
The native build with SLJIT disabled reports UNAVAILABLE and rejects execution
without interpreter fallback.
[Arena smoke output and binary hash](whole-async-arena-smoke.json). These smoke
timings are not a paired performance comparison and do not establish an Arena
speedup from Spawn lowering.

```sh
build/test_suite --whole-async
build/test_suite --whole-spawn
build/test_suite --whole-delay
build/test_suite --emit-async-c /tmp/generated-async.c
clang -std=c23 -O3 -ffp-contract=off -shared -fPIC -Isrc \
  /tmp/generated-async.c -o /tmp/generated-async.so
build/test_suite --whole-async /tmp/generated-async.so
python3 tools/bench_async.py --trials 5 --count 20000
build/test_suite --whole-function-audit # intentionally fails until full coverage
```

Next exclusions include managed fields and ownership, random operations,
Find Nearest Entity, visual host commands, Smooth Step and spring math. Pure-call
and pinned-watch parity and inspection activation changes still need full
coverage. See [the completion requirements](behavior-completion.md).
