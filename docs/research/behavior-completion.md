# Remaining whole-function migration

The current authored catalog has complete whole-function coverage. The September
12 audit of this isolated branch finds 19 behavior assets: 33/33 events, 31/31
effectful local functions, 39/39 pure local functions and 4/4 linked library
functions compile as whole bodies. [The complete audit](whole-reflected-audit.txt)
now exits successfully and `make test` runs it to catch catalog regressions.

Typed Contains/Find added ArenaPickup.Collect Along Spiral. Direct Scene Rhythm
lowering adds ArenaPickup.Tick and Wait and Pulse. Automatically generated native
call adapters for reflected NODE_READ functions add ArenaDrop.Begin, Emit Source
Frame and Spawn Source Beam. Those complex geometry operations execute compiled C
through the shared typed call ABI; their bodies are not expanded into SLJIT.
[Search measurements](whole-search.md) and [rhythm/reflection results](whole-rhythm-reflection.md)
record the measurements and remaining performance gaps.

[Array-operation lowering](whole-array-ops.md) extends language coverage beyond
the catalog with creation, mutation, equality and managed-struct searches through
explicit owned-value C calls. Array Get lowers directly. Oracle, SLJIT, bytecode
and generated C compare ownership, errors, waits and debugger snapshots.
[Owned Tick batching](owned-tick-batches.md) now supplies reusable temporary
arenas to eligible synchronous broadcasts and prepared updates. Failure
activations retain those arenas for inspection. Compatible managed scalar and
array variables can also survive hot apply with recorded layout checks.

[Managed storage reuse](managed-reuse.md) now copies persistent arrays and strings
once into alternating instance arenas. Warmed managed Tick writes reuse backing
blocks, including after compatible variable reordering. Pool release frees both
arenas. This improves the measured managed Tick workloads; it does not establish
complete language coverage or a playable large Scene.

[Set lowering](whole-sets.md) adds all four registered Set nodes, including
managed ownership, direct return values and prepared Tick batches. Set values
retain their container kind across all backends and inspection. Map is currently
an enum placeholder without registered operations or a key/value model; no Map
execution support is claimed. Set storage remains linear, with quadratic
construction, so larger container algorithms still need performance work.

[Indexed pending queues](pending-queues.md) remove idle attachment scans from
pumping and paused-state inspection. The 20,000-enemy isolated native Tick
workload improves from 4.642 to 4.274 ms, while the small native Arena Scene
remains unchanged. The subsequent [instance pages](instance-pages.md) group hot
metadata and variable headers in stable storage. Native isolated Tick p50 falls
from 4.220 to 1.527 ms at 20,000 enemies in five paired trials. Clock propagation,
body fusion and full playable Scene scaling remain unfinished.

[Subsequent native scaling work](native-scaling.md) removes redundant clock
propagation and global attachment scans from targeted event delivery. At 5,000
rendered enemies, indexed delivery reduces behavior p50 from 2.465 to 0.845 ms.
Contact collection/merge changes reduce physics cost separately. Optional physics
phase timers and an independent Arena C reference now guide the next experiments.

Catalog coverage is not complete language, gameplay or export acceptance. New
unproven operations still use the general compiled backend with per-node work;
its SLJIT status alone does not establish whole-function execution. Managed
containers, comprehensive inspection parity, large-crowd scenes, scheduler/state
locality and a runnable Scene AOT export remain unfinished. The original request's
larger asset counts describe a different snapshot; acceptance enumerates content
rather than hard-coding a count.

[Recursive call frames](recursive-calls.md) now execute explicit call IR through
an iterative SLJIT/bytecode/generated-C runner, including waits and retained
managed values. Authored effectful self/mutual recursion now lowers into immutable
definition groups, including linked libraries and managed returns. Dynamic
logical frames now support compiled behavior dispatch, debugger source stacks,
stepping, watches, traversal records and retained host requests. Publication
accepts these groups and the recursive dispatch audit passes. Loops remain active
across recursive calls and host waits with private state per invocation. Pure
recursion, re-entry of the same active loop node within one invocation and
active-generation replacement acceptance remain unfinished.
The runtime integration's paired Arena timings are unchanged, while the
[10,000-entity native tail comparison](dynamic-recursion-scale-repeat-results.json)
remains an unresolved regression signal. Passing semantic checks does not close
that performance acceptance.

[Whole Switch compilation](whole-switches.md) adds Int/Enum/String/Name selection,
including loops, nested calls, waits and instrumented synchronous bodies. Short
constant text comparisons become native byte comparisons; longer and dynamic
comparisons use the explicit C equality helper. [Persistent state lowering](whole-state.md)
now covers DoOnce, DoN, FlipFlop and Gate through fixed instance slots, including
prepared Tick execution. [MultiGate compilation](whole-multigate.md) adds typed
selection helpers, compiled output branches and independent mutable masks across
hot apply. [Retriggerable Delay](whole-retrigger.md) now updates retained compiled
deadlines through an explicit runtime helper, preserving original event inputs
and pending Sequence work. Pure recursion and same-invocation loop re-entry remain
language coverage work. Dedicated state-node
acceptance through dynamic recursion and linked libraries also remains open. The
[state-bank scale comparison](whole-state.md) also records a new 10,000-entity
bytecode regression signal; semantic coverage does not close its performance acceptance.
The subsequent [MultiGate measurements](whole-multigate.md) meet the generated-C
body target but show another repeatable bytecode regression in existing Tick
workloads. Moving the optional callback to preserve earlier context offsets did
not explain it. Attribute the context-size increase, allocation classes and VM
dispatch changes before accepting this integration as a performance result.
The subsequent [immutable VM loop-bound change](vm-bounds.md) reduces bytecode
Tick p50 by 13–18% against that integration in five paired trials at each of
100–20,000 entities. It preserves the helper and context ABI. Native timing
variation and the original regression's cause remain separate investigations.

The continuation compiler now saves live typed values and logical frame budgets
and resumes through native code labels, portable instructions, or generated C.
[Continuation measurements and boundary tests](whole-suspend.md) meet the 1.5x
native/C target. The current [Delay migration](whole-delay.md) uses this state in runtime-owned
whole activations and adds source safe points, stepping and pooled storage. It is
still in progress: Delay bodies now record scalar, array and struct watches and
traversals and logical child inspection. Remaining pure-call and pinned-watch cases are unfinished.
[Whole Spawn and asynchronous commands](whole-async.md) now use the same
continuation state and preserve nested host waits, cancellation and retained
call inspection. The subsequent [motion lowering](whole-motion.md) adds ship Tick
and four effectful function bodies. [Typed host data calls](whole-nearest.md) add
nearest-entity queries, enemy Tick and pickup collection. The
[synchronous material-write migration](whole-material.md) adds CombatFeedback's
Tick and Damaged entries plus its three local functions, raising catalog coverage
to 21 events. [Random lowering and VM operand metadata](whole-random.md) raise
the event count to 25 and reduce native setup and activation memory at scale.
[Whole particle emission](whole-particles.md) raises it to 26 and preserves
service checks before input effects. [Managed structs and strings](whole-managed.md)
raise it to 28, with caller/activation retention, persistent stores and component
string ownership. [Explicit string-to-name conversion](whole-names.md) adds
SpawnTelegraph.Tick and raises event coverage to 29. [Native visual reaction
expansion](whole-reaction.md) raises it to 31 and adds a publication-only hook for
runtime capabilities to emit the shared IR. Ordinary bodies
omit observation callbacks; the latest Delay native/C ratio is 0.977. Child
argument and frame snapshots remain available when inspection starts at a wait.

## Current implementation order

The September 13 direction prioritizes native gameplay scaling after completing
the Retriggerable Delay integration and its regression measurements:

1. Contiguous instance/variable storage and specialized compiled Tick loops.
   Keep instance addresses stable, preserve attachment order, and compare the
   actual ArenaEnemy workload before attempting body fusion or vectorization.
2. Parallel batches of proven-independent updates, using the existing task pool
   if available. The current ECS does not require replacement to support this.
   Measure executor, synchronization and deterministic effect-commit costs.
3. Reduce full-population clock maintenance, sleeping-queue polling and bulk
   attachment removal costs, guided by phase measurements.
4. Increase complete playable Scene populations and attribute behavior, physics,
   contacts, structural changes and rendering separately. Isolated Tick timings
   do not establish playable-scene capacity.

Standalone Scene AOT exports are explicitly deferred by the user. Language and
inspection gaps below remain recorded; they do not prevent optimizing the
already compiled Arena Tick bodies. Source-linked performance diagnostics remain
part of the performance-by-default direction.

## Performance policy

Correct behavior, native SLJIT availability, portable bytecode execution and zero
interpreter fallback are required. Full compiler coverage remains a completion
requirement. Generated-C timing ratios are optimization targets, not unconditional
migration blockers. The default body target remains 1.5x, and every miss must be
reported. The benchmark scripts accept `--enforce-native-ratio` for experiments
that need a strict timing gate; correctness and incomplete-measurement failures
always remain errors.

Prioritize measured end-to-end Tick cost per entity and frame, including setup,
scheduling, allocations and component access. Compare representative entity
counts with profiling disabled, then use a separate instrumented run to attribute
cost. Report medians, tail latency, memory and backend coverage. A microkernel
improvement alone is not evidence of a Scene improvement. Investigate repeatable
real-workload regressions before accepting an optimization.

The compiler now infers eligibility for ordered synchronous broadcasts and
automatically optimizes those Tick behaviors. Successful unobserved executions
avoid activation setup, bytecode shares scratch and the scheduler skips idle work;
combine updates or schedule them in parallel only when analysis proves that
component accesses, persistent state and host effects permit it. Preserve event
FIFO, Begin before Tick, waits and deferred structural commits. Authors should
not need to rewrite a behavior as a System to receive safe optimizations.

## Scale and performance by default

The target includes hundreds, thousands and tens of thousands of active entities.
The authored 40-enemy Arena cap is a regression workload, not sufficient scale
acceptance. Whole-function coverage remains required, but a native backend label
or a good generated-C ratio cannot justify an algorithm that scales poorly.
Generated C using the same slow helper can conceal the same problem.

Measure 100, 1,000, 10,000 and 20,000 entities, including cold attachment/spawn
cost, resident memory, Tick p50/p95, query candidate counts and setup/dispatch
cost. Keep all actual ArenaEnemy components and five attachments in the Tick
scaling fixture. Also run full scenes with physics, structural effects and
rendering; the isolated fixed-position fixture does not prove playable frame
rates. Record unsupported counts as unfinished acceptance, not omitted samples.

The September 13 [shared native task pool](shared-task-pool.md) passes paired
firing/death/XP/restart checks at 1,000 enemies and reduces physics p50 from
1.319 to 0.825 ms. The strengthened 5,000-enemy check exposes a separate limit:
ArenaWaveBehavior's Count Living Members exceeds the default 10,000-step budget,
and ordinary dispatch reaps the failed activation. Its encounter count can stay
stale while other gameplay continues. This remains unfinished acceptance, with
compiled query reductions and persistent source diagnostics the next priorities.

The September 12 scaling probe found repeated nearest-entity world scans, a
fixed 10,000-entity ceiling and roughly 1.9 GB peak process memory near that
ceiling. Query reuse removes the repeated scans. Subsequent
[capacity and storage work](arena-capacity.md) adds automatic ECS growth,
on-demand activation arenas and indexed attachment lookup. The real prototype
Tick fixture now runs 20,000 enemies and 100,000 attachments with zero oracle
calls. Native cold setup is about 126 ms and peak memory about 226 MiB. These
are isolated Tick results, not full Scene acceptance. Large removal bursts,
managed state retention and scheduler/data locality still need work.

The compiler now records component writes and checks declared native host effects.
The ordered broadcaster uses that proof to reuse category-filtered query
membership across consecutive safe bodies. It invalidates the scope before
unproven operations, component mutations, observation, waits and at broadcast
end. Entity positions, retirement, exclusions and distances remain live reads.
Selective player targeting can therefore share one membership scan; searches
against a large matching population still need spatial indexing or another
measured algorithm. Bounded candidate caches are not general query optimization.

Preventing authoring performance landmines remains required: expose expensive
world scans, work amplification, allocation/retention and structural churn with
source-linked diagnostics; automatically apply proven batching, specialization
and shared queries. Warnings alone are not the intended optimization strategy.
These diagnostics and the larger-count full-scene acceptance are not finished.

## Automatic Tick systems

Scene and prototype execution now use a dedicated Tick schedule. It caches
entry/service bindings, skips attachments without Tick, and runs eligible ordered
stretches through an SLJIT outer loop. Portable bytecode uses the same prepared
rows with a C loop. Per-instance state stays independent. The ordinary named
event API remains available for explicit dispatch and oracle comparisons.

Synchronous whole bodies must preserve query dependencies and obey the host
ABI's non-reentrancy/storage rules. Native guards check entity generation,
instance/backend/service identity, tracing and activation queues before effects.
Inspection, waits, changed services and unproven effects use the existing compiled
scheduler. Hot apply and attachment edits invalidate the schedule. Profiling uses
the prepared C loop and compiled bodies with timing callbacks.

Detailed behavior profiling is now opt-in. Normal Play uses the uninstrumented
update loop; the editor enables detailed timers while Performance is visible
and unfrozen. Runtime/tooling callers can explicitly enable profiling through
the engine API. Aggregate frame timing remains active.

[Measured update-loop results](tick-updates.md) cover actual ArenaEnemy attachments
at 100, 1,000, 10,000 and 20,000 entities, plus generated-C body comparisons through
a reference C outer loop. These changes share host binding and VM scratch.
The subsequent [bound-column optimization](bound-columns.md) resolves component
IDs outside prepared spans while retaining live presence/layout checks. Bodies
are not yet fused, vectorized or reordered. [Instance pages](instance-pages.md)
now group hot metadata and variable headers. Managed payloads and other state
still use separate storage, and full attachment scans for clocks remain targets.
Indexed pending queues avoid idle scans for pumping and paused-state inspection.

The editor already recognizes distinct System documents containing a graph and
an optional query. Its System asset runtime is not scheduled by the Scene host;
Scene schedules the new prepared Tick path. There is no shipped
`content/systems` folder in this snapshot. Existing authoring support alone is
not proof of gameplay integration.

Offer an explicit Convert to System action for users who want a query-based
System asset and scheduling control. Share the compiler and eligibility proof
with automatic Tick optimization. Diagnose unsupported state/effects at source
nodes; do not silently change per-entity state into shared System state or widen
which entities execute. Automatic optimization should not require this action.
The explicit conversion action and scheduling of standalone System assets are
not yet implemented. Automatic updates do not require either.

## Reproduce the exact exclusions

```sh
cmake --build build-performance --target test_suite --parallel 8
build-performance/test_suite --whole-function-audit
```

This explicit acceptance command loads every behavior, library and event
signature from the content catalog, links immutable snapshots, publishes them,
and reports an exclusion's source graph, node type, node ID, pin and direction.
It returns nonzero if any event, local function or library function body is excluded. The current catalog passes; a future exclusion fails the `make test` coverage gate.
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
same saturating casts as explicit conversion nodes. Explicitly annotated struct
alias conversions now copy typed fields without a native helper; the
[alias measurements](whole-aliases.md) meet the 1.5x generated-C target. Wave Tick
now lowers its input conversion, ForEach, component query and
`Spawn Prototype` into a whole body. Synchronous Send Event and Destroy Entity
now call their explicit deferred-command ABI from whole bodies; see
[command measurements and parity](whole-commands.md). Read-only array views,
plain elements and ForEach now lower without array helpers; their
[benchmark](whole-arrays.md) meets the 1.5x generated-C target. Persistent fixed-layout
struct writes reuse instance storage and compile into field stores; the
[store measurements](whole-stores.md) meet the 1.5x generated-C target. Fixed-layout component
Get/Has/Set, field writes, component bindings and entity
validation now lower directly. Managed values and remaining engine effects still
exclude some entries. The [pure-function measurements](whole-pure-functions.md)
show a 13.6% native Arena behavior-time reduction from executing eligible pure bodies directly. Scalar acyclic effectful calls
and stored outputs now lower into the enclosing program; their previous
exclusions have moved to the first unsupported operation inside the callee.

## Work required to finish

1. **Function execution and resumable state.** Scalar synchronous function bodies
   now have a typed argument/result ABI, early Return and native arithmetic.
   Acyclic effectful calls now inline with typed local storage, independent
   frame budgets, call-depth checks and accurate error stacks. Effectful recursive
   calls now use separately compiled definitions with dynamic logical frames.
   Finish pure recursion, same-invocation loop re-entry and remaining managed value
   forms. Live values and continuation PCs now persist across
   Delay and host waits, with cancellation and retained generation ownership.
   Extend that lifecycle coverage to the remaining value forms. Pure-body
   publication and direct execution now cover every pure local function
   in the current catalog, including fixed-layout aggregate arguments/results.
2. **Full value and control lowering.** Scalar payloads, F32/F64 coercions and
   Min/Max, Clamp, signed I32/I64 arithmetic, I32 comparisons, U32 flag tests,
   saturating numeric casts and eleven scalar transcendental operations are implemented.
   Fixed-layout vectors/structs, projected fields, mixed real arithmetic, floating
   Modulo, Atan2, Normalize Vector 2 and Direction To now lower. Aggregate values
   preserve nulls and nominal type names across calls; final results are owned
   by the caller. Persistent plain structs now reuse stable instance storage,
   preserving local snapshots, nulls, hot apply and pooling. Add remaining scalar
   operations, native 32-bit wide-integer lowering, remaining managed containers
   and same-invocation loop re-entry. Switch Int/Enum/String/Name now compile
   into whole bodies, including source inspection through loops and waits.
   Nested structs with string/name fields now have whole compiled loads, stores,
   function snapshots and caller-owned results. Mutable strings copy at retention
   boundaries; persistent managed stores still allocate.
   [Owned arrays](whole-owned-arrays.md) now cover defaults, local/parameter
   retention, caller-owned results and persistent writes, including strings and
   registered managed structs. An explicit C snapshot helper owns nested values
   and type names. Iteration survives source-variable replacement; returned query
   arrays survive query storage release. Broader hot-apply and pooling coverage
   for these array forms remains to be completed.
   Array creation/mutation/equality now use explicit owned-value calls, with
   direct Array Get and scalar search lowering. Eligible allocating Tick bodies
   now use reusable batch arenas. Copy elimination and reusable persistent
   managed storage remain performance work.
   Read-only array arguments and variables now support ForEach, ForEachWithBreak,
   length, last-index and bounds queries. ForLoop, ForLoopWithBreak and Break
   are implemented, including reverse/inclusive ranges, endpoint snapshots and
   queued Sequence tails.
   Advance Damped Spring, Quantize Angle, Smooth Step, Square Root, Floor,
   Ceiling and Round now lower into whole bodies. Arm64 uses hardware square
   root and rounding; other native targets use explicit scalar libm calls.
   Native comparisons feeding a branch or validation check avoid materializing
   a temporary boolean when it has a single use. The motion benchmark still
   exceeds the advisory native/C target for spring arithmetic.
   Eliminate runtime dispatch for the remaining language operations. The
   [loop measurements](whole-loops.md) still exceed the 1.5x native/C target; that
   performance work remains open. Scalar Set Variable
   and Call Function outputs now retain their execution values across blocks.
3. **Engine calls from generated code.** Fixed-layout component layouts, field
   offsets, Self and explicit entity checks now compile to direct access. Layout
   changes invalidate the access with a source diagnostic before a read or write.
   Managed component strings now retain through the existing world string pool
   before native field stores. Add other entity operations and direct compiled
   support for custom component readers without an ECS storage view. The scalar C-call ABI now preserves live values across native math calls.
   Component-name queries now call a typed snapshot producer; ForEach and its body
   remain native. Each lowered query owns reusable storage for this synchronous
   invocation. Dynamic keys and snapshots crossing stored array parameters/results or
   persistent stores remain excluded. See [query measurements](whole-queries.md).
   Send Event with scalar payloads and Destroy Entity now use a typed command ABI
   that preserves deferred FIFO delivery and structural mutation. Scalar names
   survive calls, stores and host payloads, including dynamic-name ownership.
   Spawn and asynchronous host work now use typed begin/completion calls,
   including borrowed string arguments. Read-only host data calls now support
   fixed scalar/plain-struct signatures, multiple outputs and nested result
   snapshots. The typed interface now also supports synchronous host effects
   with borrowed strings and plain structs, including Set Material Parameter.
   Its errors retain the source flow node and original diagnostic text.
   Find Nearest Entity uses this ABI and reuses buffers in warmed
   pooled activations and ordered batches. Service bindings refresh across
   instances and generations. Standalone pure calls still allocate temporary
   host/activation storage. Add other queries, physics,
   managed event payloads and remaining host operations. Random Real, Random
   Range Real and Random Range Vector now lower into whole bodies, retaining
   the existing RNG calls and draw order. ARM64 uses native fused multiply-add;
   platforms with wider long double retain an explicit scalar range kernel.
   Retain deterministic ordering and staged structural mutations.
4. **Whole-function inspection.** Delay bodies now have instrumented variants
   with source safe points, breakpoints, stepping, recorded values and traversal
   histories and logical child input/active-leaf views. The same inspection now
   covers synchronous and waiting host-data bodies, including nested failures
   captured from a batch without rerunning effects. Finish pure-call and
   pinned-watch parity and extend inspection to every whole body. Paused host
   waits now have source stacks, watches and traversal parity, including
   earlier calls retained by a host while a later invocation executes. Other debugging still uses the general compiled path. A completed
   migration must not require the old per-node executor for inspection.
5. **Gameplay-level acceptance.** Compare complete emitted programs across
   SLJIT, portable bytecode, generated C and the explicit oracle. Include state,
   event order, random state, spawning, contacts, damage/death/XP, restart,
   cancellation, ownership and hot apply. Run Scene, prototype preview and editor
   inspection. Measure generated C and SLJIT with the same host operations,
   scheduler and instrumentation settings. Report compile time, code size,
   steady-state simulation cost and allocation behavior as well as microkernels.
6. **Standalone AOT exports.** The C emitter currently exports individual compiled
   bodies for comparison. It does not export a runnable Scene. Add the complete
   asset closure, program metadata and host bindings, static entry registration,
   initialization, resources and a player target without the editor. The current
   app target still links the editor, including in Play mode. Validate a complete
   exported Scene against the dynamic runtime before claiming export support or
   an end-to-end AOT speedup. Static bindings and exposed entity loops are useful
   optimization targets; emitting the same generic callbacks alone does not
   guarantee a faster executable.

Completion means the catalog gate reaches all discovered entries, source-level
exclusions are resolved, helper-based language dispatch is removed, the semantic
and debugger tests pass on native and WASM, and representative gameplay demonstrates measured Tick improvements.
Document remaining gaps against equivalent generated C and their contribution
to total execution cost. Passing the scalar benchmark
or merely reporting SLJIT must not close this work.

## What need not become JIT-generated

Engine services are already native C. Allocators, ECS structural changes, query
implementations, physics and OS-facing asynchronous operations may remain C
functions called through explicit compiled interfaces. There is no reason to
reimplement them in assembly solely to eliminate a call. Whether a small helper
should be inlined must be decided using measured cost, including argument
marshalling and lost optimization across the call boundary.

A practical shortcut is to compile the surrounding graph and call substantial
optimized C routines through declared interfaces. Keep cheap field access,
arithmetic and ordinary control flow in generated code. For repeated Tick work,
prefer a typed batch operation when effect ordering permits it. A batch call can
amortize setup and give Clang a contiguous loop to optimize. The current host
interface prepares known `pin_value` layouts; it still has argument preparation
cost and is not equivalent to an arbitrary direct C signature.

Opaque calls prevent cross-body inlining, specialization and some loop
optimizations. Accurate read/write, ownership, reentrancy and suspension
contracts let the graph compiler optimize around those calls conservatively.
Reflection can generate signatures and adapters, but cannot infer those semantic
contracts from a C prototype. Static generated-C exports with helper bodies
available to [LLVM LTO](https://llvm.org/docs/LinkTimeOptimization.html) can recover
some cross-function optimization. The runtime-bound comparison libraries do not
currently provide that export path. Benchmark end-to-end work against an
independent C implementation as well as the same-helper generated-C reference.

EnkiTS/Flecs adoption is not a prerequisite. Repeated scheduler and idle pending
scans were removed and measured; changed simulation time still scans attachments,
while unchanged clock propagation is elided. Parallel batching
would be a separate performance experiment with deterministic commit and
synchronization costs measured explicitly.


The [compiled event index](tick-event-index.md) removes full graph-node scans
from compiled dispatch while preserving public activation APIs, event ordering
and hot apply. This improves the larger counter Tick fixture but does not
establish an Arena speedup or implement ECS batching. Whole-function coverage
and effect analysis remain the larger opportunities in the profiled Tick bodies.

The subsequent [ordered Tick batch path](tick-batches.md) executes eligible whole
bodies in attachment order without successful activation records. Controlled
counter/component broadcasts improve 3.1–7.4x on SLJIT and 2.0–5.6x on bytecode.
Native cost is at most 3.6% above C emitted from the same IR through the same engine
wrapper. This does not establish equivalent performance to a handwritten ECS
loop. Current Arena coverage permits only 358 such broadcasts in 180 frames;
whole-function catalog coverage was 13 of 33 events at that measurement.
The subsequent Spawn migration raises this to 16 of 33, with live Arena coverage
increasing from 9 to 12 of its 22 entries. More lowering support,
shared context binding and proven effect isolation remain unfinished.

The [motion migration](whole-motion.md) raises whole coverage to 17/33 catalog
events and 13/22 live Arena entries. Ship Tick qualifies for ordered batching
automatically. Fifteen paired trials with profiling disabled reduced native
median Arena behavior time from 20 to 15 microseconds per frame. Its standalone
spring body still misses the advisory generated-C ratio, at 2.486x.
