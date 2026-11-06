# Whole managed structs and strings

September 12, 2026. The whole compiler now supports nested reflected structs
containing scalar fields, strings and names. The [catalog audit](whole-managed-audit.txt)
reaches 28/33 events and 26/31 effectful local functions. All 39 pure local
functions and four library functions still qualify. Newly covered bodies are
ArenaImpactFeedback.Prepare Enemy Experience, ArenaImpactFeedback.Died and
SpawnTelegraphBehavior.Begin. Live Arena coverage increases from 19/22 to 20/22.

## Execution and ownership

Native field loads, stores, branches, calls and continuations remain in the
whole compiled body. String pointers can borrow immutable program values or
activation-owned arguments. A string read from mutable storage is copied when
saved into a local. Returned strings and managed result type names are copied
into the caller's result arena. Nested fields follow the same rules.

Three explicit operations carry the ownership work:

- `BF_COPY_STRING` copies a string into the supplied activation or result arena.
- `BF_INTERN_STRING` retains a component string using the world's existing
  string pool before generated code stores the field.
- `BF_STORE_VALUE` packs a typed value into a reusable argument bank, then calls
  the existing persistent-variable ownership implementation. It copies the new
  payload before releasing the previous variable arena.

A managed Set Variable captures its input before overwriting storage, including
self-assignment. Constants and already-retained local strings can be reused.
Native, bytecode and generated C execute the same lowered operations. The
helpers perform allocation and retention; they do not evaluate nodes or graphs.

Inspection uses owned snapshots for managed structs. Tests also exposed a native
watch bug: a spilled one-byte boolean was tested as four bytes. Generated watch
code now loads its declared width. Null exposed-field errors retain the oracle's
message and source pin.

## Measurements

[Five Release trials](whole-managed-results.json), 50,000 calls per backend and
64 warmup calls per trial, rotate SLJIT, bytecode and generated C. Generated C
uses LLVM 23 with `-O3 -ffp-contract=off`. Each body performs nested calls,
persistent struct/string writes and self-assignment. The component variant also
stores a managed component. Setup and compilation are outside the timed loop.

| Workload | SLJIT ns/call | Generated C ns/call | Bytecode ns/call | SLJIT/C |
| --- | ---: | ---: | ---: | ---: |
| Managed records | 437.76 | 430.26 | 987.86 | 1.017 |
| Managed component | 482.58 | 457.98 | 1143.66 | 1.054 |

Both workloads meet the advisory 1.5x target. SLJIT is 2.26–2.37x faster than
bytecode. The comparison uses identical runtime helpers. It does not establish
parity with a handwritten implementation that avoids those ownership costs.
Warm calls allocate no host argument banks or VM scratch. Persistent variable
stores still allocate a new arena for each assignment, and string copies still
consume arena space. This is not an allocation-free path.

[Seven paired Arena runs](whole-managed-scene-results.json), alternating saved
before/after executables with profiling disabled, show no material Scene speedup.
The fixture runs 180 fixed steps with firing and restart, discards 30 warmup
steps and asserts zero interpreter calls.

| Backend | Behavior p50 before/after ms | Behavior p95 before/after ms | Simulation p50 before/after ms | Simulation p95 before/after ms |
| --- | --- | --- | --- | --- |
| SLJIT | 0.011 / 0.011 | 0.024 / 0.024 | 0.015 / 0.015 | 0.030 / 0.029 |
| Bytecode | 0.019 / 0.019 | 0.034 / 0.034 | 0.024 / 0.024 | 0.040 / 0.040 |

The newly eligible events are Begin and Died. This migration does not remove a
hot per-frame scheduler cost, and the Scene numbers should not be presented as
evidence of such a gain.

## Validation and remaining work

Oracle, bytecode, SLJIT and emitted-C parity covers nested calls, struct and
string self-assignment, null/empty/UTF-8 strings, caller input mutation, variable
replacement during a wait, result and component lifetimes, missing services and
components, budgets, cancellation, stepping, breakpoints, watches, traversals and
call-stack snapshots. Native tests use ASan/UBSan. `make test`, `make build`,
native Arena, `make web` with BYTECODE smoke, and native no-JIT rejection pass.

Five catalog events and five effectful functions remain excluded. Managed
arrays, sets/maps, managed host outputs, recursive activations, the remaining
host operations and full inspection coverage still need work. The explicit
To Name conversion still excludes SpawnTelegraph.Tick. Primitive Part,
Transform Visual Offset, Trigger Visual Reaction, Scene Rhythm and Contains
are other current source-level exclusions.

Bodies with allocating ownership operations currently use ordinary whole
activations rather than prepared Tick batches. Copies can accumulate during a
long-running activation until it is recycled. Persistent-value storage reuse,
string-pool lookup cost and managed batching remain performance work. The
scheduler/data-locality and complete standalone Scene AOT export priorities
remain open; the emitted C here is a body comparison, not a runnable export.
