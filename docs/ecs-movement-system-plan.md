# ECS movement system

This branch is the smallest ECS editor slice: one `MovementSystem`, one Player
Entity, WASD input, and the existing Event Graph editor.

## Runtime contract

- `GameTransform` stores position.
- `GameMovement` stores speed.
- `GameInput` stores the current input axis.
- The System query is derived from component read/write declarations on its
  graph nodes.
- `Tick` runs once for each matching Entity with that Entity bound as `Self`.
- The System has no persistent Behavior attachment per Entity.
- The first System backend supports synchronous graphs only.

The graph in `content/systems/MovementSystem.json` remains the source of truth
for movement. The host only maps keyboard state into `GameInput`.

## Compilation path

The runtime lowers the compact System graph into a bytecode program and then
into `graph_ir`. `Get_*` nodes expose component fields directly and are pulled
through data edges. `Set_*` nodes consume a component value or its fields and
retain execution edges because writes are ordered effects. Break, Make, and
identity-conversion nodes are compiler operations rather than required authored
nodes.

The compiler selects a typed fast path when every value has fixed storage and
every reflected pure call has a generated typed adapter. The generic bytecode
path keeps boxed `pin_value` values as a profiling and fallback path. SLJIT is
the primary runtime for the current synchronous System graph; graphs outside
its supported shape still use the older paths temporarily.

The typed path preserves component writes and uses declared component access to
build the query. A component field result aliases column storage only while no
later declared write can change a value that is still live; otherwise the
compiler keeps a snapshot copy. Reflection supplies C types, field layout, type
aliases, and typed adapters. Adapters with up to four arguments use direct
arity-specific calls; larger calls retain the pointer-array ABI. Reflection
does not decide whether a native function has hidden side effects.

When enabled, the SLJIT backend consumes the same typed `graph_ir` and emits a
synchronous native entry point. Its register path recognizes the current `CF_V2`
movement operations and keeps their values in floating-point registers. On
AArch64 it uses two-lane `float` instructions for vector loads, multiplies, and
adds. The operation kind comes from `NODE_JIT(...)` annotations emitted by
`reflect.c`, not from operation-name tests in the backend. `NODE_JIT_FAST_MATH`
also permits the AArch64 lowerer to contract the movement multiply/add chain
into an FMLA instruction, so that path intentionally follows fast-math
floating-point rounding. The memory path
handles fixed-size POD values, component copies, struct copies, constants, and
direct adapters with at most three input arguments. Unsupported IR remains on
typed bytecode. The runtime selects SLJIT for `AUTO` only when compilation
succeeds.

Generated C remains an optional later backend for a measured hot graph.
Generated code is a cache, not authored source. A hand-written movement
algorithm is not part of the runtime.

## JIT ABI and graph IR

The System compiler now exposes a lowered `graph_ir` for future native
backends. It uses stable value IDs and ordered instructions. Values carry their
pin type, container kind, storage size, alignment, and layout key. Instructions
carry operation IDs, resource IDs, effect declarations, native call adapters,
and source node pointers for diagnostics. Copy and alias optimizations are
already reflected in the lowered program. `graph_ir_hash()` provides the cache
key for compiled code; changing a node name or reflected layout changes the
key.

The first ABI is defined in `core/jit_abi.h`:

- `graph_jit_entry_fn` receives one Entity, its delta and time, resolved
  component rows, a host callback table, and an optional activation frame.
- `GRAPH_JIT_OK`, `GRAPH_JIT_ERROR`, and `GRAPH_JIT_SUSPENDED` are the entry
  results.
- The host table handles component lookup, operations that remain runtime calls,
  and error reporting.
- `graph_jit_frame` reserves a resume block, live slots, and wake time for a
  future coroutine backend. Synchronous entries must return `GRAPH_JIT_OK` or
  `GRAPH_JIT_ERROR` and must not retain context pointers.

SLJIT is the machine-code backend. It is isolated in
`core/graph_jit_sljit.*`; it does not expose SLJIT types through the System API.
Register-lowered programs also have a batch entry that consumes the query's
matched slots and removes one C-to-JIT call per Entity. The batch entry is used
only for register-lowered graphs; memory-lowered programs keep the one-Entity
ABI. Unsupported operations continue through the existing bytecode or flow
paths rather than entering the SLJIT entry point. The authoring host sends one
render-only execution pulse after each successful matched System tick. This
restores graph bubbles without instrumenting the JIT, recording per-Entity
traversals, or adding work to the hot loop.

## System graph support

"Arbitrary graph" means any graph the editor and core evaluator can represent;
it does not mean that every graph is currently a query-driven System. The
current support boundary is:

| Runtime path | Supported | Not supported |
| --- | --- | --- |
| SLJIT graph JIT | The compact synchronous System graph with data-driven reads, ordered component writes, fixed-size POD values, implicit field projection/packing, identity type aliases, component access, constants, and reflected direct adapters; the current AArch64 register/SIMD lowerer handles the reflected `CF_V2` movement operations | Branches and loops, latent/stateful nodes, structural operations, managed values, and unsupported call shapes |
| Typed bytecode | The compact System graph and the older ordered `Get_*`/`Set_*` shape with fixed-size scalar, enum, handle, and registered POD struct values | Latent or stateful nodes; structural `Add_*`/`Remove_*`; strings, names, arrays, sets, maps, service-backed calls, explicit output parameters, and unknown struct layouts |
| Generic System bytecode | The older synchronous System graph shape with boxed values and compatible pure calls; retained for comparison while JIT coverage grows | The full behavior graph language, latent continuation state, and structural System operations |
| System flow fallback | Other graph shapes that the flow evaluator accepts and whose `Tick` completes in one call | A suspended `Tick`; persistent per-Entity Behavior state; structural changes while the System query is running |
| Behavior runtime | General event graphs, including branches, variables, stateful nodes, and latent continuations | Query-wide batch execution as a System |

The user does not choose these paths. The System panel reports `SLJIT SIMD
batch`, `SLJIT batch`, `SLJIT SIMD`, `SLJIT registers`, `SLJIT memory`, `typed
bytecode`, `generic bytecode`, or `flow interpreter`. Editing a graph triggers
recompilation; an unsupported fast-path value falls back without changing the
authored graph.

## UI scope

The active app submits only:

- `System`: query, play/stop, reset, match count, and status.
- `Entity`: Player component values.
- `Scene`: the Player viewport.
- `Node Canvas`: the editable Event Graph.

The app no longer compiles the old scene workbench, demo graph, or input
behavior source. Legacy behavior, live asset, and scene code remains available
for existing regression targets until the replacement backend is complete.

## Acceptance checks

- The Player moves with WASD or arrow keys while `Scene` is focused.
- The graph can be edited, recompiled, and applied without a Behavior attach.
- Entities missing one queried component are not processed.
- A headless test covers graph loading, query matching, movement, and exclusion.
- A benchmark reports generic bytecode, typed bytecode, SLJIT, and native C
references with and without the same ECS query.
