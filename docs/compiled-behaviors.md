# Compiled behavior execution

Behavior Assets publish a program for their linked generation. Native execution
requires SLJIT. Emscripten publishes portable behavior bytecode and contains no
SLJIT backend. Publication errors use `CMPL-25` and retain source graph, node and
pin identities where applicable. Dispatch and Play reject unavailable code.

`behavior_program.c` resolves data dependencies, conversions, field projections,
continuation targets and control operations into the shared graph IR. Each data
block has a source map and forward memo branches. The behavior instructions are
separate from the older straight-line System instruction set. Both behavior
backends consume the same lowered program.

`behavior_jit.c` emits native data blocks and execution entries. Matching F32/F64
Add, Subtract, Multiply and Divide become native arithmetic; Branch emits a
native conditional jump. Other value operations and host effects call C helpers
with prepared runtime slots. These helpers include managed arrays/structs,
component and entity access, variables, functions, random inputs and structural
operations. They do not recursively call `eval_node` or discover data edges at
runtime. More native arithmetic specialization remains possible; this change
makes no performance claim for callback-heavy graphs.

For uninstrumented native input evaluation, `behavior_expression.c` now lowers
eligible F32/F64 dependency regions without value-helper calls. These regions
support matching Add/Subtract/Multiply/Divide, scalar variable reads and
bindings, defaults, static constant outputs and copy nodes. Variable indices
are resolved at publication; arithmetic chains retain intermediates in registers,
and shared values spill only when necessary. The API materializes the final
input slot once. Inspection uses the existing instrumented compiled blocks.
Mixed types, conversions, host reads and other unsupported operations keep their
regular compiled implementation. WASM continues to use behavior bytecode.

See [measured numeric-region results](research/behavior-numeric-performance.md).
The measured improvement is specific to eligible computation; the Arena trial
has not demonstrated a whole-scene speedup.

Synchronous scalar event and effectful function entries can use a whole-function
program, including branches, Sequence, WhileLoop, ForLoop, ForLoopWithBreak, Break, variable access, scalar event
and function arguments, multiple returns, Min/Max, F32/F64 coercion and inlined
pure local/library calls. Defaults and argument type checks execute in generated
code. Return terminates the function even inside a Sequence or loop. SLJIT retains
scalar temporaries in registers; the same instructions have bytecode and
generated-C implementations. Scalar acyclic effectful calls inline into the same
program using typed locals and logical frames, with independent budgets and
source stacks. Instrumented or unsupported calls still use pooled activations.

Counted loops snapshot their endpoints and direction once. They preserve inclusive
ascending and descending ranges, stored Index outputs, early Return, and Break's
queued Sequence tails. Functions have independent loop scopes and budgets.
Native loops retain scalar state in registers and use explicit branches; portable
bytecode and emitted C execute the same lowered control flow. Static continuation
records preserve pending loop/Sequence frames in budget and Break diagnostics.
Numeric edge conversions use the same compiled saturating casts as explicit nodes.
See [loop parity and performance results](research/whole-loops.md); the loop
microbenchmark still exceeds the native/C performance limit.

Fixed-layout structs and vectors now lower into scalar fields with separate
presence and nominal-type metadata. Make, Break, projected fields, aggregate
arguments/results, immutable variable reads and synchronous function locals use
this representation. Intermediate structs stay in compiled slots; only final
aggregate results allocate storage in the caller's arena. Null pass-through
values remain null, and field access reports the original source node and pin.
Generated single-field component conversions lower into field projections.

Reflected component Get, Has, Set and field Set now lower into checked native
loads/stores, including explicit entity targets, Self/default-Self handles and
component input bindings. The ECS supplies a small storage view; generated code
checks the entity generation, membership, presence and reflected layout before
accessing a row. Registration and migration refresh that view. Straight control
paths reuse validated addresses and forward local snapshots through typed slots;
field values still reflect intervening writes. No node evaluator, component C
accessor or temporary full-component allocation is required by these bodies.
Managed component fields remain excluded. Custom component readers without an ECS
execution view retain the general compiled callback path. See
[component measurements and acceptance](research/whole-components.md).


Mixed F32/F64 arithmetic preserves the language's subnormal rules. Floating-point
Modulo, Atan2 and robust vector normalization call platform `fmodf`, `fmod`,
`atan2f` and `hypot` directly from generated code. They do not call node evaluators.
These calls use the same scalar ABI as generated C. See
[aggregate measurements and acceptance](research/whole-aggregates.md).

Straight paths through multiple frames also have a guarded variant. The guard
checks root/child budgets and call-depth limits before effects occur; the variant
forwards stored values, folds constant arithmetic and removes unobservable
temporary stores. Insufficient budgets use the original checked compiled body,
preserving the exact partial state and failing node. Native, portable and
generated-C execution use this same optimization.

Instrumented execution keeps recording node values through the general compiled
path. Uninstrumented whole functions materialize Return inputs for the caller;
other inputs remain unavailable through `flow_run_input_value`.

See [whole-function and scheduler measurements](research/whole-function-performance.md)
for the initial generated-C performance gates and queue benchmarks. The catalog
now has five eligible events out of 33 and three eligible effectful function
bodies out of 31.
All 39 pure local functions now have independently compiled bodies, as do
three of the four linked library functions.
These counts describe complete entries, not how many individual arithmetic
expressions compile. The strict catalog audit distinguishes the counts.

Activations retain compiled block indices, live slot storage, function children,
owned loop snapshots and wait state. Source nodes are recovered from the program
for inspection. The existing scheduler still orders events and host effects and
owns cancellation, pooling and wakeups. Public dispatch, tick, watch, breakpoint,
step and call-stack APIs keep their existing signatures.

Asset ownership keeps programs alive. An attached instance guards every graph
in its asset. Revision checks include local and linked function graphs. Hot apply
continues to wait for a compatible scheduler boundary, so an old activation
finishes against its original generation before the host releases that asset.
Programs are published during registration, instance creation and preview/apply.

Inspection reports `SLJIT`, `BYTECODE`, `ORACLE` or `UNAVAILABLE`. Runtime snapshot
responses include a backend on each behavior attachment. Scene inspection and
the graph preview show the selected backend. Gameplay instances collect watches
and traversal records only while selected for inspection or held by the debugger.

The interpreter is available through explicit standalone oracle APIs and debug
selection. `behavior_instance_set_backend` selects a backend before dispatch;
`behavior_debug_backend` is a scoped, thread-local override for new instances in
parity tests. `UNAVAILABLE` restores platform selection for that override. It is
never an automatic fallback. System `AUTO` also requires SLJIT on native and
compiled bytecode on Emscripten. Explicit System bytecode/oracle modes remain for
parity and diagnostic tests.

## Validation

- `make test` runs the native backend smoke, existing regression suite, additional
  native/bytecode/oracle behavior checks, scene documents and headless app smoke.
- `build/test_suite --behavior-parity` runs linked-function and debugger contracts
  against bytecode and the oracle, plus gameplay checks in both hosts. Encounter
  snapshots compare reflected component values frame by frame across all three
  backends through spawning, damage and death. Generated prototype identity tags
  are compared by presence because independent loads assign process-local IDs.
- `build/test_suite --compiled-arena` loads Arena3D, fires, simulates and restarts
  through SLJIT and bytecode. It checks every live attachment's backend and asserts
  zero oracle calls. Gameplay simulation fixtures make the same oracle assertion.
- A native configuration with `CHIBI_ENABLE_SJLIT=OFF` runs
  `check-behavior-backend` or `test_suite --backend-unavailable` to verify a hard
  diagnostic and zero interpreter calls.
- `make web` builds the app and runs an Emscripten/Node backend smoke covering an
  event, yield, resume, variables, loops and nested whole-function arguments,
  conversions, Min/Max, returns, nested pure bodies and fixed-layout aggregate arguments/results
  without callee runtime allocation. It asserts bytecode execution,
  SLJIT unavailability and zero oracle calls.

The existing runtime tests cover local/library calls, managed values, loops,
structural host waits, cancellation, hot apply, event ordering, watches and
stepping. The existing gameplay tests cover contacts, damage, death, experience,
restart and preview behavior. These are semantic checks, not a benchmark.

Verified on September 12, 2026: `make test`, `make build`, `make web`,
`test_suite --behavior-parity`, `test_suite --compiled-arena`, and the native
no-JIT smoke target all passed. Arena3D reported 63 live behavior instances for
each compiled backend, with zero oracle calls through firing and restart.

The [remaining whole-function migration](research/behavior-completion.md) lists
the unresolved language/runtime work and the strict catalog coverage command.
Backend parity passes do not imply that this coverage gate passes.

[Whole-function call measurements](research/whole-function-calls.md) cover the
typed argument/return ABI, generated-C comparison and paired scheduler/Arena runs.
The subsequent [call-inlining measurements](research/whole-function-inline.md)
cover logical frames, stored outputs and guarded optimization.

[Tick profiling and frame reuse](research/tick-profiling.md) documents the new
execution/setup measurements, bounded call-frame storage, stress results and
remaining compiler work. The focused rendered check is
`test_suite --behavior-profile-ui`.

[Pure-function body measurements](research/whole-pure-functions.md) cover independent
publication, direct execution inside general compiled events, expanded coverage
gates and paired Arena results.

[Whole-function math calls](research/whole-math-calls.md) documents direct
transcendental calls, register preservation, helper-free Clamp, expanded parity
and the generated-C math benchmark.

[Whole-function integer operations](research/whole-integers.md) covers native
integer instructions, shared division/remainder, saturating casts, pure-call
error source scopes and generated-C acceptance. Integer language operations
introduce no runtime C helpers. The general compiled executor still handles
excluded bodies, including instrumented ones.
