# Whole-function Delay activations: implementation in progress

Authored Delay now lowers to a time comparison and a continuation in the same
whole program as its enclosing function, loops and nested calls. SLJIT, portable
bytecode and generated C share the saved typed values, logical frame budgets,
source locations and resume PC. Normal native execution emits no source-point
callbacks. A separate native variant adds safe points for pause, stepping and
breakpoints. Timers use host-supplied time; no clock or sleep helper runs inside
the generated body.

The runtime owns the continuation and keeps its program generation alive through
its owning instance. It releases host scratch on completion, error or cancellation.
Idle pooled runs retain typed continuation buffers and bytecode scratch; changing
the function or destroying/resetting the run releases them. Hot apply remains
busy while activations exist and discards old pooled storage before changing the
generation. Per-function timer deadlines and stepping flags preserve nested-call
inspection and Step Over through subsequent waits.

Whole Delay activations now support tracing from their first entry, initial
single-step, and recording enabled or disabled during waits. Generated code
records scalar, array and fixed-layout struct watches, plus source edge
traversals. Logical child inputs, active-leaf inspection and frame-local stacks
now use compiled inspection views. Pure-call watch scopes and pinned values still
need broader parity coverage. These are known gaps, and the runtime migration remains
in progress. Variable array views are explicitly excluded from Delay bodies until
owned snapshots prevent another event's array replacement from invalidating a
suspended view. Spawn and other asynchronous host operations remain excluded.

## Measurements

All measurements use the same lowered nested-loop function with an authored
Delay after each variable increment. Each invocation makes an average of eight
entries (initial entry plus seven timer resumes). Inputs vary over four loop
bounds. The checksum covers the resulting count. Each trial has 10,000 warmup and
200,000 measured invocations; five trials rotate backend order. No builds or
other tests run concurrently with a timing trial. Clang uses O3, no sanitizers
and disabled floating-point contraction. Raw trials record compiler, binary
hashes and the base commit; the tested source includes the current worktree edits.

| Measurement | SLJIT | Generated C | Bytecode | Oracle |
| --- | ---: | ---: | ---: | ---: |
| Final body and continuation entries | 47.035 ns | 65.875 ns | 808.935 ns | — |
| Initial runtime activation | 361.660 ns | — | 1192.390 ns | 5018.585 ns |
| Runtime with pooled continuation buffers | 245.560 ns | — | 998.885 ns | 5017.590 ns |
| Final runtime, including cached breakpoint presence | 213.720 ns | — | 949.015 ns | 5021.150 ns |

The body reuses prepared activation storage. The runtime measurement includes
argument copies, activation setup, all timer resumes, completion and recycling
through the public flow APIs. It reuses the flow object after warmup. The runtime
measurement does not include a Scene scheduler or rendering, and generated C is
compared at the body boundary only.

Pooling reduces native runtime time by 32%. Computing breakpoint presence once
for an immutable program generation removes a repeated source-point scan, giving
41% total improvement over the initial whole Delay runtime. The final native/C
body ratio is 0.714 and passes the existing 1.5 limit. The native body is about
17.2 times faster than bytecode; the public activation path is about 4.4 times
faster. Shared setup and lifecycle work explains the smaller latter gap for this
workload. It does not quantify the cause of every older gameplay measurement.

Final native activation trials range from 208.665 to 323.900 ns (one slower trial);
bytecode ranges from 942.150 to 958.150 ns. Native body trials range from 46.275 to
54.890 ns. Generated-C timing varies across runs and must not be credited as an
SLJIT optimization. These microbenchmarks do not establish an Arena frame-rate
improvement.

[Initial body trials](whole-delay-initial-results.json),
[initial runtime trials](whole-delay-runtime-results.json),
[pooled runtime trials](whole-delay-pool-results.json),
[final verified trials](whole-delay-results.json).

## Verification and remaining acceptance

The focused runtime matrix compares oracle, SLJIT and bytecode at each wait and
pause for ranges, nested calls and endpoint snapshots, with budgets 1 through 45.
It covers single-step, Step Over, persistent stepping across waits, current nodes,
call stacks, logical deadlines and error budgets. Additional tests cover existing
breakpoints after a wait, zero/negative/infinite/NaN durations, cancellation,
activation reuse, hot-apply deferral and destruction while waiting.

A second matrix compares bytecode, native, alternating backends and generated C
at every continuation, both with and without inspection and while switching
instrumented variants. Generated C also passes in an ASan-instrumented library
loaded into the ASan test host. The portable smoke executes an actual authored
Delay with nested calls and asserts zero oracle calls.

`make test`, `make build`, `make web`, the native JIT-unavailable smoke and the
Arena firing/simulation/restart smoke passed. Arena reports SLJIT or BYTECODE with
zero oracle execution. The strict [catalog audit](whole-delay-audit.txt) still
returns nonzero: 13/33 events, 10/31 effectful locals, 39/39 pure locals and 3/4
libraries qualify for whole-function execution. This continuation work has not
removed the catalog's remaining first blockers.

Before calling the runtime migration complete, finish logical child inspection
and the remaining watch cases; support owned values across waits;
and route Spawn's initializer and host completion through the continuation ABI.
Then repeat actual Scene/prototype debugging and full gameplay parity. Existing
counted-loop performance acceptance also remains open.

```sh
build/test_suite --whole-delay
build/test_suite --emit-delay-c /tmp/delay-generated.c
# Compile the emitted source with the same compiler, O3 and -ffp-contract=off.
build/test_suite --check-delay-c /tmp/delay-generated.so
python3 tools/bench_components.py --workload delay --trials 5 --iterations 200000
build-performance/test_suite --whole-function-audit
```

## Inspection implementation and updated measurements

The September 12 inspection pass adds compiled watch and traversal instructions
to Delay bodies. Call frames own separate watch histories. Re-entering a child
clears its prior watches and descendant histories even if recording has since
been disabled. Caller values are staged until a child pauses or returns
successfully, so an immediate child failure cannot publish watches that the
oracle would omit. Struct field watches skip absent parents without raising an
execution error. Watch storage owns its recorded values through subsequent
mutation, pauses and frame reuse.

The ordinary SLJIT variant emits neither observation callbacks nor unused
observation loads. Bytecode uses a compact instruction stream with the same
typed slots and canonical continuation PCs. Both retain values required when
inspection is enabled after a real wait. Generated C now emits separate ordinary
and instrumented bodies as well, so its ordinary comparison no longer pays
per-watch callback guards. Inspection can record values without requesting
safe-point pauses.

An activation that already has retained child watches keeps frame-reset
bookkeeping active after recording is disabled. It stops recording new values
and traversals. Recycling clears that history; a later uninspected invocation
again selects the ordinary body. Activations that have never been inspected do
not allocate watch histories.

The first implementation dispatched disabled observation instructions in the VM
and emitted their unused loads in native code. Measurement caught the regression
before acceptance. Removing that work produced these five-trial medians with the
same workload and settings described above:

| Measurement | SLJIT | Generated C | Bytecode | Oracle |
| --- | ---: | ---: | ---: | ---: |
| Initial inspection implementation, ordinary body | 56.260 ns | 70.665 ns | 1676.305 ns | — |
| Compact ordinary body | 46.310 ns | 40.630 ns | 730.505 ns | — |
| Compact runtime activation | 218.100 ns | — | 894.320 ns | 5051.600 ns |

The native/C body ratio is 1.140, passing the 1.5 limit. The native body is 15.8x
faster than bytecode; the public activation is 4.1x faster. Native body trials
range from 44.985 to 48.590 ns; generated C ranges from 37.610 to 62.960 ns.
The earlier 47.035 ns native body and 213.720 ns activation are from a separate
run. The updated results do not establish a material native runtime improvement
over that checkpoint. The smaller generated-C time follows its instrumentation
split and must not be credited as an SLJIT optimization.

[Initial inspection trials](whole-delay-watch-initial-results.json) and
[compact execution trials](whole-delay-watch-results.json) retain the raw results.
These are still body and flow-runtime microbenchmarks, not Scene scheduler or
rendering measurements, and do not compare a generated-C runtime wrapper.

The expanded oracle/native/bytecode matrix compares every pause and wait for
ranges, Break, nested loops, While, nested calls and endpoint snapshots, with
budgets 1 through 45 and eight stepping/recording modes. Additional array cases
cover empty and nonempty ForEach, nested loops, Break, effectful array calls,
struct elements and projected fields with budgets 1 through 20. Snapshots own
their values and compare metadata, scalar bits, nested fields, call stacks and
exact traversal edges, times and logical step counts. The generated-C matrix
compares recorded callback sequences as well as continuation state, including
alternating instrumented variants and recording without pause callbacks.

The catalog [audit](whole-delay-watch-audit.txt) still fails the full-coverage
gate at 13/33 events, 10/31 effectful locals, 39/39 pure locals and 3/4 libraries.
This debugger work does not remove the remaining authored Spawn exclusions.

Final validation after removing ordinary observation work passed `make test`,
`make build`, `make web`, generated-C callback/continuation parity under ASan and
UBSan, changed-file formatting and `git diff --check`. The native no-JIT smoke
reports UNAVAILABLE with no fallback. Arena firing, simulation and restart pass
on SLJIT and bytecode with 63 instances and zero oracle calls. The Emscripten
smoke executes whole Delay activations with watches and traversals on BYTECODE.
The portable smoke is not the entire native debugger matrix or a browser UI
interaction test.


## Logical frame inspection, September 12

Compiled Delay activations now expose child input values, call depth, current
node, status, step counts, stacks and traversal history through the existing
read-only flow APIs. Views have no execution runtime and are allocated only when
inspection asks for them or tracing records child history. The compiler retains
entry parameters, including otherwise unused arguments, in canonical activation
storage. Native code stores them directly; bytecode and emitted C use the same
layout. It also saves logical frame step counts when entering and leaving calls.
This permits inspection after an ordinary body has already suspended.

The parity matrix compares leaf inputs, state, depth, steps, stacks and traversal
records against the oracle at every wait, pause, error and completion. It covers
array arguments and a function that finishes without Return. That failure keeps
its completed child visible while reporting the parent's call error. This caught
and fixed a mismatch in active-leaf selection, step counts and wake time.
The portable smoke also reads array and unused scalar arguments from an active
compiled child with tracing disabled. No interpreter child frame is created.

[Five-trial frame measurements](whole-delay-frame-results.json) give 48.400 ns
for the native body, 49.530 ns for generated C and 736.995 ns for bytecode. Runtime
activation medians are 228.570 ns native, 870.935 ns bytecode and 5008.325 ns oracle.
The body ratio is 0.977. These are separate runs from the preceding watch figures;
the extra parameter/frame stores are not a performance improvement. The existing
body benchmark's nested callee has no arguments, so these timings do not quantify
argument-heavy frame inspection overhead. The real Tick measurements in
[compiled event lookup](tick-event-index.md) use separate before/after binaries.

Native parity, the full native test suite, generated-C continuation/callback
parity with ASan/UBSan and the unavailable-JIT smoke passed. `make build` and `make web` also passed, including BYTECODE child-array/input
inspection and the absence of SLJIT in the Emscripten smoke. General pure-call/pinned-watch coverage,
non-Delay whole-function inspection and compiled Spawn remain unfinished.
