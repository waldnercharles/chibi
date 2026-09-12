# Remaining whole-function migration

The whole-function compiler is not yet a completed gameplay migration. The
September 11 audit of this isolated branch finds 19 behavior assets, 33 event
entries, 70 local functions and four library functions. **Zero of the 33 event
entries qualifies for whole-function execution.** The separate Arena live-assets
sample finds zero of 22 entries across 13 assets. The original request's larger
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
It returns nonzero while any entry is excluded. Its current nonzero result is an
unfinished performance-coverage gate, not a gameplay correctness failure.
`--compiled-arena` also prints these diagnostics for the live assets.

The compiler exposes exclusions separately from publication errors through
`behavior_program_function_diagnostic`. Exclusion records are stored separately
from the node table used during execution. Successful compilation has no
exclusion. Native backend availability policy is unchanged.

Each row identifies the first encountered blocker, not every blocker on every
branch. Removing one can expose another. From Arena's 22 live entries:

| First blocker | Entries | Examples |
| --- | ---: | --- |
| Effectful function calls | 7 | ArenaWaveBehavior.Tick, CombatFeedback.Damaged |
| Component or entity operations, including projected fields | 10 | TwinStickInputBehavior.Input, TwinStickWeaponBehavior.Tick, EnemyBehavior.Tick |
| Integer comparisons | 3 | HealthBehavior.Damage, ArenaImpactFeedback.Damaged |
| Max | 1 | PlayerContactBehavior.Tick |
| Input conversion | 1 | ArenaShipMotionBehavior.Tick |

## Work required to finish

1. **Function execution and resumable state.** Compile effectful local/library
   calls and returns with a typed argument/result ABI, function-local storage,
   per-frame budgets, recursion limits and accurate error attribution. Persist
   live values and continuation PCs across Delay, host waits, cancellation and
   hot apply. Existing pure scalar inlining is only one special case.
2. **Full value and control lowering.** Add event payloads, integers, numeric
   conversions and remaining scalar operations; vectors and projected struct
   fields; arrays, structs and managed ownership; ForLoop, ForEach and Break.
   Eliminate runtime dispatch for these language operations. Standalone function
   entries currently do not receive the whole-function ABI.
3. **Engine calls from generated code.** Resolve component layouts, field
   offsets, entity-reference checks and access contracts at publication. Compile
   direct typed access where valid. Generate explicit ABI calls for queries,
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
