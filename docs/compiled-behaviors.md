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

Random Real, Random Range Real and Random Range Vector lower into whole bodies.
The existing RNG stays an explicit C call; conversions, bounds checks, vector
fields and scalar blends use typed operations. ARM64 emits hardware fused
multiply-add. Targets with wider `long double` retain an explicit range kernel
for scalar precision. Unused vector coordinates and pure-function returns keep
their random draws, while repeated node reads share a computed value. Watches
and stepping retain the same sequence. VM operand-presence flags are computed
once after lowering and fit existing instruction padding.

Synchronous whole functions can borrow array arguments and variables when the
complete body cannot mutate them. ForEach and ForEachWithBreak emit direct
iteration over scalar or plain struct elements, retaining Index and element
snapshots in typed locals. Length, Last Index, Is Empty, Is Not Empty and Is Valid
Index use direct reads and comparisons. Null arrays retain their diagnostic or
empty-query semantics. Owned defaults, escaping array results, persistent writes,
string/name elements and registered managed structs now compile as whole bodies.
An explicit C helper deep-copies arrays at retention boundaries. Immutable inputs
and saved snapshots share storage within an invocation; iteration snapshots a
mutable variable before the body can replace it. Array creation, mutation and
comparison now use explicit owned-value C calls within whole bodies. Borrowed-array operations
add no array helper or iteration allocation. Activation argument copies remain
outside the whole-function body.

Make Array, Add, Add Unique, Append, Set Array Elem, Insert, Remove Index,
Remove Item, Clear, Reverse, Swap, Resize and Identical use named C adapters.
Struct Contains/Find use the same value ABI. The helper receives prepared typed
arguments and invocation storage, returns a complete owned value, and cannot
evaluate graphs. SLJIT calls the concrete adapter directly. Bytecode and generated
C use the corresponding immutable program binding. Array Get uses direct bounds
checks and element loads. Helper errors retain their source node and pin.
Eligible synchronous Tick batches now provide reusable invocation arenas for
owned values. On failure, the captured activation takes ownership of the arena
before the next row reuses the binding. Persistent managed stores retain
instance-owned snapshots.

Contains and Find now lower to typed search loops over numeric, boolean, enum,
handle, string and name elements. Numeric equality compares the value rather
than padding or unused union bytes; signed zero compares equal and NaN does not.
Enums and handles also compare nominal type names. String comparisons use an
explicit text helper; numeric searches use direct loads and comparisons with no
runtime helper. Indexed addresses and field branches have native lowering, and
loop invariants use saved registers while expression temporaries retain scratch
registers. Null-array metadata, nested pure-call traversals and repeated
breakpoints preserve the oracle's inspection behavior.

`Find Entities With Component` supports whole-function execution with a constant
component key. A typed `node_query_fn` produces an owned handle snapshot through
an explicit C call. SLJIT emits the consuming array indexing, loop and body;
bytecode and generated C consume the same operation. Each static query producer
has separate reusable storage, released on success, Return and failure. Queries
still exclude unsafe membership/layout mutation and recursive loop activation.
Escaping arrays now copy into invocation or variable storage, so returned query
results survive release of query storage. General compiled execution still
handles excluded shapes. Raw native/generated-C
entry callers use `behavior_function_prepare_host` and
`behavior_function_release_host`; `behavior_function_run` owns this lifecycle
by default.

`Find Nearest Entity` uses a fixed `node_host_call_fn` signature with borrowed
scalar/plain-struct arguments and caller-owned result backing. Generated code
stores typed arguments, calls the world query and snapshots its outputs into
compiled slots. Each producer has its own bank, including nested calls. Warmed
pooled activations and ordered batches reuse these banks; bindings refresh for
each instance. Synchronous and waiting bodies have instrumented variants for
source stepping, watches, traversals and logical child inspection. Failed batches
materialize their saved activation without rerunning the query. Standalone pure
calls still use temporary host and activation storage.

`Set Material Parameter` uses the same typed host-call ABI for synchronous
effects. Scalar and plain-vector payloads, borrowed string arguments and dynamic
error messages retain their source diagnostics. Generated code prepares the
arguments directly and calls the C material service. It never calls the custom
flow callback. The service cannot invalidate component or behavior storage;
its host-sink access metadata keeps parallel scheduling conservative. Successful
ordered batches reuse argument banks and avoid per-entity activations.

`Send Event` with scalar payloads and `Destroy Entity` use a synchronous
`node_command_fn` ABI in whole bodies. SLJIT emits argument computation, typed
payload writes, branches and loops, then calls the known C command directly.
The command owns retained payloads and queues effects; it cannot dispatch graphs,
flush structural changes or invalidate behavior/component storage. Bytecode and
generated C use the same lowered command and error/source information.
Invocation scratch is separate from immutable program metadata.

Scalar values retain their actual type names through arguments, locals,
arithmetic, coercions, array elements, returns and persistent variables. Borrowed
names are interned before persistent stores. Canonical query element names and
proven constant locals avoid redundant ownership calls. Arbitrary caller names
still get owned storage.

Integer operations lower to native instructions with shared division/remainder,
saturating casts and pure-call error source scopes. They introduce no runtime C
helpers. The general compiled executor still handles excluded bodies, including
instrumented ones.

Native floating-point arithmetic and loads/stores use allocated registers
directly. Loop locals use saved floating-point registers where available, including
across C ABI math calls; other targets retain stack storage. Spilled widening
casts write the full destination width.

Fixed-layout structs and vectors now lower into scalar fields with separate
presence and nominal-type metadata. Make, Break, projected fields, aggregate
arguments/results, immutable variable reads and synchronous function locals use
this representation. Intermediate structs stay in compiled slots; only final
aggregate results allocate storage in the caller's arena. Null pass-through
values remain null, and field access reports the original source node and pin.
Generated single-field component conversions lower into field projections.

Persistent plain struct variables own fixed storage for their reflected layout.
Whole functions write fields, presence and nominal-type metadata directly;
assignment results retain their earlier value in typed locals. Known declared
type names use instance-owned metadata without a helper. Dynamic argument type
names pass through an explicit ownership callback before publication in the
variable. Backing storage survives null assignment, compatible hot apply and
pool reuse. Managed structs keep their existing arena ownership and general
compiled execution. Copying a variable retains a snapshot independently of later
assignment or instance destruction.

Reflected component Get, Has, Set and field Set now lower into checked native
loads/stores, including explicit entity targets, Self/default-Self handles and
component input bindings. The ECS supplies a small storage view; generated code
checks the entity generation, membership, presence and reflected layout before
accessing a row. Registration refreshes that view. Straight control
paths reuse validated addresses and forward local snapshots through typed slots;
field values still reflect intervening writes. No node evaluator, component C
accessor or temporary full-component allocation is required by these bodies.
Managed component fields remain excluded. Custom component readers without an ECS
execution view retain the general compiled callback path.

Mixed F32/F64 arithmetic preserves the language's subnormal rules. Floating-point
Modulo, Atan2 and robust vector normalization call platform `fmodf`, `fmod`,
`atan2f` and `hypot` directly from generated code. They do not call node evaluators.
These calls use the same scalar ABI as generated C.

Straight paths through multiple frames also have a guarded variant. The guard
checks root/child budgets and call-depth limits before effects occur; the variant
forwards stored values, folds constant arithmetic and removes unobservable
temporary stores. Insufficient budgets use the original checked compiled body,
preserving the exact partial state and failing node. Native, portable and
generated-C execution use this same optimization.

Whole bodies containing waits or typed host-data calls have instrumented variants
that record source-node values and traversal history. Their logical child frames
also preserve argument and error snapshots. Other instrumented bodies still use
the general compiled path. Extending inspection to every whole body remains part
of the migration.

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

Use the selectors in [test iteration](testing.md) for current commands and
build requirements. Focused runtime checks include `whole-alias`,
`whole-integer`, `whole-conversion`, `whole-select`, `whole-math` and `owned-tick`.
Choose checks for the affected lowering, ownership and scheduling contracts;
backend parity alone does not establish complete whole-function coverage.

## Struct identity conversions

`NODE_JIT("identity")` promises a by-value struct copy with a nominal type change.
Reflection requires one struct argument with the same canonical C type as the
return value. Whole-function lowering checks the scalar field layout and reuses
typed slots for both explicit nodes and conversion edges. Native execution emits
no conversion helper or intermediate allocation. System native lowering also
requires this annotation; compatible layouts alone do not imply identity.

## Query reuse during ordered broadcasts

Whole programs record component writes, including writes inside linked functions,
and retain native host-effect declarations. Before each eligible synchronous
broadcast body, the scheduler checks whether that body preserves the components
used by nearest-entity membership. Consecutive proven bodies reuse bounded,
category-filtered candidate lists. Component mutations, unproven effects,
inspection, queued work and broadcast completion end the scope. Queries still
read current positions, retirement and exclusions and compute exact distances.

This optimization is automatic for authored Enemy Tick. It is independent of
whether its body runs as SLJIT or bytecode. It does not cache final nearest
results or permit changes to observable event order. Query counters are opt-in
through the runtime inspection API.

Within one unobserved whole-function execution, the shared compiler also reuses
the result of a contextual data node declared `node_host_call.reusable` when a
single forward predecessor proves the earlier call runs first. `Find Nearest
Entity` opts into this contract: reading `Found` before a Branch and reading its
position/distance afterward performs one search. Authors need no cache nodes.
The same optimization feeds SLJIT, generated C and portable bytecode.

Variable/component writes, random inputs, undeclared host effects, joins, loop
back edges and waits stop result reuse. Checked world/entity/component addresses
can survive field writes along a proven path; field values are still loaded
again. Nothing moves ahead of its first checked access. Inspection retains the
original program so stepping can observe changes made while paused, and actual
continuations retain their saved slots.

## ECS growth and activation storage

Entity identity arrays grow automatically at structural boundaries. Component
occupancy covers the world capacity; payload columns grow only when that
component is added at a higher slot. Compiled code keeps the same checked flat
column access. The stable world execution view refreshes its generation,
occupancy and column pointers after growth. Existing published functions need
no recompilation. Column and row pointers must be reacquired after structural
commits or schema changes, and active queries reject storage/schema growth.

Allocation preserves the deterministic lowest free slot, using a cursor to skip
the occupied prefix. Canceled reservations advance their generation, so their
handles cannot become valid after reuse. Optional native bulk reservation does
not require authors to estimate an entity cap.

Activation input and scratch arenas allocate on first use and retain blocks for
pool reuse. Large managed values still grow these arenas while preserving
addresses until reset/free. Empty events no longer preallocate two 64 KiB blocks.
Attachment lookup and inspection use an ordered index by entity slot. Appending
an entity's first attachment set keeps the broadcast array in place; edits that
change its ordering rebuild the index. Deferred Begin resolves its stable
sequence snapshot through that index, preserving order across nested attachment
changes. The prepared Tick schedule described below now supplies the native
outer loop.

The behavior world also indexes attachments with nonempty activation queues.
Pumping and paused-state inspection skip idle attachments. Empty/nonempty queue
notifications maintain membership, including waits, debug holds, cancellation
and pooled reuse. Serial callbacks preserve the existing attachment delivery
order. `behavior_last_pump_count` exposes visited queues for inspection and
performance assertions. Attached instances borrow their world's simulation clock,
so advancing time and delta time performs constant work regardless of attachment
count. Standalone instances retain independent clocks. Activations snapshot their
start time and delta time; waits and Scene Rhythm still read the current clock
where their contracts require it. Pool release detaches the borrowed clock.

## Prepared Tick updates

Scene and prototype steps call `behavior_update`. The world caches Tick
entries in attachment order and shared service/VM bindings for synchronous whole
functions that preserve entity-query dependencies. `update_jit.c` emits a native
outer loop over these rows. Each body retains its own instance variables and
executes the same whole-function code used by explicit dispatch. Portable builds
use `behavior_update_run_c` over the same schedule and bytecode bodies.

The loop checks live entity generations, backend, tracing, active activations,
asset identity and service revision before effects. An observed or ineligible
row returns to the existing compiled scheduler. Failures retain source diagnostics
without rerunning prior effects. Attachment edits and hot apply rebuild the
schedule; service rebindings invalidate cached work before its old service can
be released. A service may be
explicitly rebound to the same pointer to refresh a changed execution view.

Bindings own copied service-name tables and reusable host/VM scratch. Instances
and immutable program generations belong to the world and outlive synchronous
execution. Host calls may not execute graphs, invalidate storage or retain
borrowed values. Before each prepared span, bindings resolve component IDs into
columns once per function and service view in each execution chunk. SLJIT,
bytecode and generated C use those columns while retaining entity, presence
and layout checks at the original operation. An untaken branch therefore
cannot introduce a missing-component or layout diagnostic. Columns are
refreshed after possible structural mutations;
captured failures never retain them.

Proven-independent entity chunks execute on the shared native task pool while
preserving attachment order within an entity. Large spans also validate rows
and refresh their private bindings on workers. Every validation task joins
before any behavior effects; a blocked row returns the whole span to the normal
scheduler. Small spans validate on the calling thread to avoid extra task
overhead.

`behavior_last_update` distinguishes prepared executions, native outer-loop
executions and rows handled by normal dispatch. Profiling deliberately uses the
prepared C loop with compiled bodies; unprofiled native spans contain no timing
callbacks. `behavior_set_native_update` selects the C outer loop for compiler
comparisons. The named-event and debugger APIs remain unchanged.

Detailed behavior profiling is now opt-in. Normal Play uses the uninstrumented
update loop; the editor enables detailed timers while Performance is visible
and unfrozen. Runtime/tooling callers can explicitly enable profiling through
the engine API. Aggregate frame timing remains active.

Whole Emit Particles and Emit Directed Particles bodies call explicit native
particle entry points with preallocated typed argument banks. Optional
`node_host_call.missing_service` guards preserve service checks before evaluating
inputs. Native code emits the pointer check directly; bytecode and generated C
retain the same ordering.

Whole managed structs now flatten nested scalar and string/name fields. Mutable
string snapshots retain at local/result boundaries, persistent assignments use
an owned typed-value helper, and component writes retain strings in the ECS
world before native field stores. Managed debugger watches own their snapshots.
Allocating ownership operations currently exclude prepared Tick batching.

Persistent managed writes now copy into a spare per-variable arena, then publish
the result and reset the previous arena. Each array element is copied once.
Warmed writes can reuse backing blocks while preserving self-assignment, borrowed
slices, errors and hot-apply reordering. Pool release frees both arenas.

Explicit `To Name` nodes and edge conversions now lower into whole bodies,
including null-to-empty normalization and owned results. The native path uses
the existing arena-copy helper for string retention. SpawnTelegraph.Tick now
qualifies for whole-function execution.

Runtime capabilities may provide `node_desc.lower`, a publication-only expansion
into the shared behavior IR. Trigger Visual Reaction uses it for direct checked
component access, finite validation and stores, preserving argument/state errors.
Its ordinary generated body makes no runtime helper calls. The descriptor
declares component reads/writes and conservatively serializes arbitrary Target
writes.

Scene Rhythm now expands into shared context reads, component-column metadata
and clock arithmetic. ECS population/singleton metadata avoids a query scan on
every sample. Compiled activations and prepared Tick rows read their own instance
simulation clock, preserving its distinction from wait time.

Reflection automatically emits compiled native-call adapters for eligible pure
`NODE_READ` functions with supported value signatures. The adapter calls C with
borrowed arguments and caller-owned results; it cannot execute graphs or retain
inputs. Unsupported effects or ownership still require an explicit contract.
Raw string outputs are not inferred to be safe. Reflected component field/layout
metadata already drives generic component lowering. Complex native functions can
therefore stay C helpers while arithmetic and control flow remain directly lowered.

The four registered Set operations also compile as whole bodies. Construction
and insertion call explicit owned-value C helpers; length is a direct load and
membership uses typed search loops or managed-struct equality helpers. Lowered
container metadata preserves Set identity through frames, stores, returns and
inspection.

Switch Int/Enum/String/Name now lower into whole-function comparisons and
branches, with source inspection through loops, calls and waits. Short constant
text labels compare directly in SLJIT; longer and dynamic equality calls use the
native helper.
Effectful recursive calls also preserve active loop state across calls and host
waits. Re-entering the same active loop node within one invocation remains
unsupported by whole-function lowering.

DoOnce, DoN, FlipFlop and Gate now use fixed per-instance slots and direct compiled
flag/counter operations. Publication supplies slot indices; compatible hot apply
remaps values by node ID and pooling resets them. Ordinary, batched and prepared
Tick execution bind the same bank.

MultiGate now uses the same fixed state bank through an explicit C selection
helper. Inputs and output branches remain compiled. Masks stay independent across
hot apply and pooling, including prepared Tick execution.
