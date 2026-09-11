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
  event, yield, resume and variable assignment. It asserts bytecode execution,
  SLJIT unavailability and zero oracle calls.

The existing runtime tests cover local/library calls, managed values, loops,
structural host waits, cancellation, hot apply, event ordering, watches and
stepping. The existing gameplay tests cover contacts, damage, death, experience,
restart and preview behavior. These are semantic checks, not a benchmark.

Verified on September 11, 2026: `make test`, `make build`, `make web`,
`test_suite --behavior-parity`, `test_suite --compiled-arena`, and the native
no-JIT smoke target all passed. Arena3D reported 63 live behavior instances for
each compiled backend, with zero oracle calls through firing and restart.
