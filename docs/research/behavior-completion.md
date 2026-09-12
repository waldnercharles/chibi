# Remaining whole-function migration

The whole-function compiler is not yet a completed gameplay migration. The
September 12 audit of this isolated branch finds 19 behavior assets, 33 event
entries, 70 local functions and four library functions. Five of the 33 event
entries qualify for whole-function execution: `PlayerContactBehavior.Tick`,
`SurfacePulseBehavior.Begin`, `TwinStickInputBehavior.Input`,
`TwinStickInputBehavior.Tick` and `TwinStickWeaponBehavior.Begin`.
Three of 31 effectful function bodies qualify: `ArenaShardBehavior.Initialize Shard`,
`ArenaShardBehavior.Advance Shard Motion` and `ProjectileBehavior.Face Direction`.
All 39 pure local functions now execute whole compiled bodies, as do
three of the four library functions. Pure calls inside general compiled events
use these bodies directly; they no longer require callee runtime stores. The original request's larger
asset/event counts describe another snapshot; acceptance must enumerate content,
not hard-code either set of counts.

All these behaviors already select the general SLJIT or bytecode backend. That
status is not proof of whole-function machine code: the general backend still
uses per-node runtime operations and activation bookkeeping. The scalar timing
results describe eligible benchmark functions, not these authored behaviors.

## Reproduce the exact exclusions

```sh
cmake --build build-performance --target test_suite --parallel 8
build-performance/test_suite --whole-function-audit
```

This explicit acceptance command loads every behavior, library and event
signature from the content catalog, links immutable snapshots, publishes them,
and reports an exclusion's source graph, node type, node ID, pin and direction.
It returns nonzero while any event, local function or library function body is excluded. Its current nonzero result is an
unfinished performance-coverage gate, not a gameplay correctness failure.
`--compiled-arena` also prints these diagnostics for the live assets.

The compiler exposes exclusions separately from publication errors through
`behavior_program_function_diagnostic`. Exclusion records are stored separately
from the node table used during execution. Successful compilation has no
exclusion. Native backend availability policy is unchanged.

Each row identifies the first encountered blocker, not every blocker on every
branch. Removing one can expose another. Adding Max made the contact cooldown
Tick eligible. F32/F64 coercion moved the ship-motion exclusion to a compound
argument in a pure call. Integer arithmetic, comparisons, flags, explicit
numeric casts, fixed-layout aggregates, vector normalization and field unwraps
now lower into whole bodies. Counted ranges, While continuations and Break now
lower into explicit compiled control flow. Numeric edge conversions share the
same saturating casts as explicit conversion nodes. Fixed-layout component
Get/Has/Set, field writes, component bindings and entity
validation now lower directly. Managed values and engine effects still exclude
most entries. The [pure-function measurements](whole-pure-functions.md)
show a 13.6% native Arena behavior-time reduction from executing eligible pure bodies directly. Scalar acyclic effectful calls
and stored outputs now lower into the enclosing program; their previous
exclusions have moved to the first unsupported operation inside the callee.

## Work required to finish

1. **Function execution and resumable state.** Scalar synchronous function bodies
   now have a typed argument/result ABI, early Return and native arithmetic.
   Acyclic effectful calls now inline with typed local storage, independent
   frame budgets, call-depth checks and accurate error stacks. Add recursive
   calls and managed locals. Persist
   live values and continuation PCs across Delay, host waits, cancellation and
   hot apply. Pure-body publication and direct execution now cover every pure local function
   in the current catalog, including fixed-layout aggregate arguments/results.
2. **Full value and control lowering.** Scalar payloads, F32/F64 coercions and
   Min/Max, Clamp, signed I32/I64 arithmetic, I32 comparisons, U32 flag tests,
   saturating numeric casts and eleven scalar transcendental operations are implemented.
   Fixed-layout vectors/structs, projected fields, mixed real arithmetic, floating
   Modulo, Atan2, Normalize Vector 2 and Direction To now lower. Aggregate values
   preserve nulls and nominal type names across calls; final results are owned
   by the caller. Add remaining scalar operations, native 32-bit wide-integer
   lowering, persistent aggregate writes, arrays and managed fields/ownership;
   ForEach and recursive loop activations. ForLoop, ForLoopWithBreak and Break
   are implemented, including reverse/inclusive ranges, endpoint snapshots and
   queued Sequence tails.
   Eliminate runtime dispatch for the remaining language operations. The
   [loop measurements](whole-loops.md) still fail the 1.5x native/C gate; that
   performance work remains open. Scalar Set Variable
   and Call Function outputs now retain their execution values across blocks.
3. **Engine calls from generated code.** Fixed-layout component layouts, field
   offsets, Self and explicit entity checks now compile to direct access. Layout
   changes invalidate the access with a source diagnostic before a read or write.
   Add managed component fields, other entity operations and direct compiled
   support for custom component readers without an ECS storage view. The scalar C-call ABI now preserves live values across native math calls.
   Generate explicit ABI calls for queries,
   spawning/destruction, physics, events, randomness and asynchronous host work.
   Retain deterministic ordering and staged structural mutations.
4. **Whole-function inspection.** Produce instrumented variants with source
   safe points, breakpoints, stepping, recorded values and call frames, including
   paused host waits. Current debugging uses the general compiled path. A
   completed migration must not require the old per-node executor for inspection.
5. **Gameplay-level acceptance.** Compare complete emitted programs across
   SLJIT, portable bytecode, generated C and the explicit oracle. Include state,
   event order, random state, spawning, contacts, damage/death/XP, restart,
   cancellation, ownership and hot apply. Run Scene, prototype preview and editor
   inspection. Measure generated C and SLJIT with the same host operations,
   scheduler and instrumentation settings. Report compile time, code size,
   steady-state simulation cost and allocation behavior as well as microkernels.

Completion means the catalog gate reaches all discovered entries, source-level
exclusions are resolved, helper-based language dispatch is removed, the semantic
and debugger tests pass on native and WASM, and real gameplay meets a documented
performance bound against equivalent generated C. Passing the scalar benchmark
or merely reporting SLJIT must not close this work.

## What need not become JIT-generated

Engine services are already native C. Allocators, ECS structural changes, query
implementations, physics and OS-facing asynchronous operations may remain C
functions called through explicit compiled interfaces. There is no reason to
reimplement them in assembly solely to eliminate a call. Whether a small helper
should be inlined must be decided using measured cost, including argument
marshalling and lost optimization across the call boundary.

EnkiTS/Flecs adoption is not a prerequisite. The repeated scheduler scans were
fixed and measured. Parallel batching would be a separate performance experiment
with deterministic commit and synchronization costs measured explicitly.
