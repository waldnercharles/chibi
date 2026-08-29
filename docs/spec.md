# c-blueprints specification

Normative description of c-blueprints: an Unreal-Blueprints-style visual
scripting system written in C23, engine-agnostic and ECS-agnostic.

This document states what correct behavior is. It does not track progress.
Progress lives in `docs/roadmap.md`; the reasoning behind each locked decision
lives in `docs/adr/`.

Every normative statement carries a stable ID such as `PIN-4`. Roadmap
acceptance criteria cite those IDs, and `test/self_check.c` check names cite
them, so a requirement with no citing test is mechanically visible as
unimplemented. IDs are append-only. A withdrawn requirement keeps its number
and is marked withdrawn; numbers are never reused. See `docs/adr/0007`.

`docs/ue5-event-graph-parity-audit.md` records the UE 5.8 capability audit,
the three-lane parity model, the reordered implementation sections, and the
decisions intentionally deferred until their section begins. ADR 0011 defines
the persistent-instance runtime used below.

Requirements use RFC 2119 sense: MUST is binding, SHOULD admits a documented
exception, MAY is optional.

## Contents

- [Scope](#scope)
- [ID](#id-identity) identity
- [TYPE](#type-type-system) type system
- [PIN](#pin-pin-model) pin model
- [NODE](#node-node-model) node model
- [VAR](#var-variables) variables
- [BP](#bp-blueprints-and-graphs) blueprints and graphs
- [EVT](#evt-events) events
- [FN](#fn-functions-and-macros) functions and macros
- [DATA](#data-data-evaluation) data evaluation
- [EXEC](#exec-imperative-execution) imperative execution
- [RUN](#run-run-lifecycle) run lifecycle
- [CAT](#cat-node-catalog) node catalog
- [CMPL](#cmpl-compile-and-validation) compile and validation
- [DBG](#dbg-debugging) debugging
- [FMT](#fmt-file-format) file format
- [UI](#ui-editor-surfaces) editor surfaces
- [REFL](#refl-reflection) reflection
- [Implementation layout](#implementation-layout)

## Scope

The parity target is the Blueprint scripting language, not the Unreal engine
object model. See `docs/adr/0001`.

In scope: generic persistent Blueprint definitions and instances, host-catalog
events with fixed signatures, variables including array, set, and map
containers, user-defined functions, macros, delegates and event dispatchers,
the full flow-control catalog, a compile and validation pass, and an
interactive debugger.

`docs/game-spec.md` specifies the game runtime built on this language: an
entity store, a prototype format, behavior attachment, parallel execution, live
authoring, host surfaces, and module hot reload. It is a consumer of this
document and nothing in it may be required by `core/`.

Out of scope for core: actors, Blueprint class assets, inheritance, interfaces,
class defaults, Construction Script, and any assumed host object model. An ECS
binding is a separate optional module layered on the same node descriptor and
reflection APIs, never folded into core.

`SCOPE-1` Core (graph, types, evaluation, execution, serialization, compile)
MUST NOT include any host, windowing, rendering, or ECS header. The only
runtime dependencies are libc and vendored third-party sources (yyjson, fmt).

`SCOPE-2` Core MUST be usable headlessly. Every behavior in this document
except the `UI` section MUST be reachable and testable without creating a
window.

`SCOPE-3` Core MUST NOT hold mutable process-global state. Error reporting,
evaluation status, and run state are per-instance, so two runs, or a run plus a
headless test, cannot interfere.

`SCOPE-4` Parity claims MUST name Unreal Engine 5.8 as their fixed reference
release and MUST classify every audited language family as Core, Generated,
Consumer, Deferred, or Excluded. A fixed catalog count MUST NOT be presented as
proof of parity because Unreal actions are generated from reflection, loaded
modules, and plugins.

`SCOPE-5` Core language behavior MUST be engine-independent. Engine actions
such as actor, component, asset, input, networking, animation, UI, and ECS
operations satisfy the Consumer lane only through a documented host node pack
or through the Generated lane's reflection contracts.

## ID: identity

`ID-1` Every blueprint, graph, node, pin, and variable MUST carry a 64-bit
identity that is unique within its containing blueprint and stable across save,
load, undo, and redo.

`ID-2` IDs MUST be allocated from a monotonic counter stored on the blueprint.
The counter MUST be serialized, so a load followed by new allocations never
reissues a retired ID.

`ID-3` ID zero MUST mean "no reference" and MUST NOT be allocated.

`ID-4` Deleting an entity MUST retire its ID permanently. A reference to a
retired ID MUST resolve to nothing and MUST NOT resolve to an unrelated entity.

`ID-5` Copy and paste MUST allocate fresh IDs for the pasted entities and MUST
rewrite intra-selection references to the new IDs. A reference that points
outside the copied selection MUST be preserved as-is when the paste target is
the same blueprint, and MUST be cleared and reported as a compile error when it
is not.

`ID-6` Every cross-entity reference in the data model and in the file format
MUST be by ID, never by array index and never by name. Names are display and
search data only.

## TYPE: type system

`TYPE-1` WITHDRAWN. The primitive list omitted Name and nominal host handles.
It is replaced by `TYPE-11`.

`TYPE-2` A struct type MUST be described by a `type_desc` giving its name,
size, and per-field name, pin type, byte offset, byte size, and C type
spelling.

`TYPE-3` Exactly two functions MUST touch raw struct bytes: `pin_write_field`
and `pin_read_field`. No other code may compute a field address.

`TYPE-4` An enum type MUST be described by a name and an ordered list of
(label, value) entries, so the editor can offer a labelled dropdown rather than
a raw integer.

`TYPE-5` Two data pins are connection-compatible when their pin types are equal
and, for `PIN_STRUCT` and `PIN_ENUM`, their type names are equal, and their
containers are equal.

`TYPE-6` Integer widths are distinct types. A connection between different
widths MUST be rejected unless an explicit conversion applies under `TYPE-7`.

`TYPE-7` A connection between incompatible-but-convertible types MUST be
offered as an explicit conversion: the editor inserts a visible conversion node
rather than coercing silently. The permitted conversions are integer widening
within the same signedness, integer to float, float to double, and any scalar
to string.

`TYPE-8` The containers are single value, array, set, and map. A map carries
two type slots, key and value.

`TYPE-10` `PIN_F32` and `PIN_F64` are one Real category. A Real output MUST
connect to a Real input regardless of width, converting at the boundary, and
both MUST render in one Real colour. This follows Unreal, where float and double
are a single Real pin type. Integer widths remain distinct under `TYPE-6`,
which also follows Unreal, where Byte, Integer and Integer64 are separate.

`TYPE-9` A container pin MUST NOT connect to a single-value pin of the same
element type, and vice versa. Container and element are converted by explicit
nodes only.

`TYPE-11` The pin types are `PIN_BOOL`, the sized integers `PIN_I8`, `PIN_U8`,
`PIN_I16`, `PIN_U16`, `PIN_I32`, `PIN_U32`, `PIN_I64`, `PIN_U64`, the floats
`PIN_F32` and `PIN_F64`, `PIN_NAME`, `PIN_STR`, `PIN_HANDLE`, `PIN_STRUCT`,
`PIN_ENUM`, and the execution marker `PIN_FLOW`.

`TYPE-12` `PIN_NAME` MUST be a first-class immutable identifier type distinct
from `PIN_STR`. It MUST round-trip its authored spelling. Its equality,
canonicalization, and ordering policy MUST be fixed before Switch on Name or
Name ordering ships; localized Text remains a consumer-provided value type.

`TYPE-13` `PIN_HANDLE` MUST carry a host-owned 64-bit opaque value and a nominal
type name. Core MUST NOT dereference, retain, release, or infer a C pointer from
the value. Two handle pins are compatible only when their nominal type names
match.

`TYPE-14` Promotable numeric operations MUST use the UE-facing widening ladder
Byte to Integer to Integer64 to Real. F32 and F64 remain one Real category,
with F64 selected only when the winning registered signature requires it.
Other C integer widths require an exact signature or an explicit registered
promotion rule; C's usual arithmetic conversions MUST NOT decide graph types.

## PIN: pin model

Node instances own their pins. See `docs/adr/0002`.

`PIN-1` A node descriptor's pin lists are a template. Creating a node MUST copy
that template into instance-owned storage.

`PIN-2` An instance pin MUST carry: its ID, its display name, its pin type, its
type name, its kind (data or exec), its container, its authored default value,
its split parent (zero when it is not a split child), and flags for advanced
visibility and wildcard state.

`PIN-3` An instance pin's name, type, type name, container, and default value
MUST be mutable after creation, subject to the compile rules in `CMPL`.

`PIN-4` A node's authored pin defaults and its runtime pin values MUST be
separate storage. Evaluation writes runtime values and MUST NOT overwrite an
authored default.

`PIN-5` An unconnected data input MUST evaluate to its authored default. An
authored default that was never set MUST be the typed zero value, which for
`PIN_STR` is the empty string and for a container is the empty container.

`PIN-6` A data input MUST have at most one driver. Connecting a second driver
MUST replace the first.

`PIN-7` A data output MAY drive any number of inputs.

`PIN-8` An exec input MUST accept any number of incoming edges, because
execution paths merge.

`PIN-9` An exec output MUST drive at most one target. Ordered fan-out is
expressed by a node with several exec outputs, such as Sequence, never by
multiple edges from one output.

`PIN-10` An exec edge MUST carry a non-negative order value; a data edge MUST
NOT carry one.

`PIN-11` A wildcard pin has no resolved type until it is connected. On
connection it MUST adopt the peer's type, type name, and container, and every
other wildcard pin in the same wildcard group on that node MUST resolve with
it. Disconnecting the last edge in the group MUST return the group to
unresolved.

`PIN-12` A `PIN_STRUCT` pin MUST be splittable: splitting replaces the pin's
visible presence with one child pin per struct field, each child recording the
parent's ID as its split parent. Recombining MUST restore the parent pin.
Splitting MUST be rejected while the pin has an edge.

`PIN-13` A pin marked advanced MUST be hidden behind a per-node expander and
MUST behave identically when shown.

`PIN-14` A node type MAY declare a pin group as user-extensible with a declared
minimum and an optional maximum. Adding and removing pins in such a group MUST
be an editor operation, and removal MUST delete the edges attached to the
removed pin.

`PIN-15` A pin MAY be marked pass-by-reference. A reference input and a
reference output on the same node name the same storage, which is how a node
mutates a caller's variable.

`PIN-16` A pin MUST have a color derived from its type and container, applied
consistently to the pin, its edges, and its details panel row, so type is
readable without hovering.

`PIN-17` A service-backed input MUST be a stable instance pin with an ID and a
nominal service type. Binding or unbinding a service MUST NOT add, remove, or
retype the pin. A connected data edge overrides service injection. An
unconnected pin MAY be hidden while its service is bound and MUST be shown as
unresolved when no matching service is bound.

## NODE: node model

`NODE-1` A node MUST reference exactly one node descriptor, which supplies its
behavior, its default title, its pin template, and its category.

`NODE-2` A node MUST carry a canvas position. Core evaluation MUST ignore it.

`NODE-3` WITHDRAWN by ADR 0021. Node title overrides are not authored state.
Nodes display their descriptor name or an internal reference label.

`NODE-4` WITHDRAWN by ADR 0021. Node-level comments are not authored state.
Graph comment boxes remain the annotation entity under `NODE-9`.

`NODE-5` A node MUST carry two general instance-reference fields: a 64-bit
reference ID and a reference name. Together with instance pins these express
variable, function, macro, and other node-specific internal targets. Event
names are selected from the host catalog and are not free-form node metadata.
No feature may add a bespoke per-node field where these suffice.

`NODE-6` WITHDRAWN. It incorrectly required every impure node to have an exec
input, excluding executable entry nodes. It is replaced by `NODE-13`.

`NODE-7` A node descriptor MUST declare whether it is latent, meaning it can
suspend execution. Latent nodes are legal in event graphs and macros and are
rejected in function graphs by `CMPL-9`.

`NODE-8` A hand-authored node descriptor MUST be a first-class alternative to a
reflected one. Reflection is sugar that emits descriptors.

`NODE-9` A comment box is a node-like graph entity with a position, a size, a
title, and a color. Moving it MUST move the nodes fully inside it. It MUST have
no pins and MUST NOT participate in evaluation.

`NODE-10` A reroute node is a single-input, single-output pass-through used to
shape wires. It MUST exist for both data and exec, MUST adopt its type from its
connection, and MUST be transparent to evaluation.

`NODE-11` A node MUST be disableable without being deleted. A disabled node
MUST be skipped by execution, passing its exec input through to its first exec
output, and MUST produce typed zero values on its data outputs. Disabled state
MUST be serialized and MUST be reversible without losing edges.

`NODE-12` A node descriptor MUST declare a display class: event, function call,
pure, flow control, variable access, or literal. The display class drives the
node's color and title styling, which is how a Blueprint graph is read at a
glance.

`NODE-13` A descriptor MUST classify behavior as pure or executable. A pure
node has no exec pins, has no observable side effects, and is re-derivable from
its inputs. An executable entry has no exec input and at least one exec output.
Every other executable node has at least one exec input. Executable behavior
MUST run only from an event activation, never from preview evaluation.

## VAR: variables

`VAR-1` A variable MUST carry an ID, a name, a pin type, a type name, a
container, an authored default value, and a category string for grouping in the
editor.

`VAR-2` WITHDRAWN. It allowed macro-local variables while `FN-8` prohibited
them. Function locals are replaced by `VAR-13`; macro-local state remains at
its decision gate.

`VAR-3` Variable names MUST be unique within their scope. A rename MUST be
accepted only when it keeps that uniqueness.

`VAR-4` A Get node MUST produce the variable's current value on one data output
typed from the variable. A Set node MUST have an exec input, an exec output, a
data input typed from the variable, and a data output carrying the value that
was written.

`VAR-5` Get and Set nodes MUST reference their variable by ID. Renaming or
retyping a variable MUST update every referencing node's display and pin type
without rewriting references.

`VAR-6` Retyping a variable MUST invalidate edges on referencing nodes that are
no longer type-compatible, and each invalidated edge MUST be reported by
`CMPL`.

`VAR-7` Deleting a variable that is referenced MUST be permitted, and every
orphaned reference MUST become a compile error naming the node and the lost
variable.

`VAR-8` WITHDRAWN. Run-scoped variables reset between events and cannot model
persistent Event Graph behavior. It is replaced by `VAR-11`.

`VAR-9` WITHDRAWN. It bundled three containers, omitted common UE 5.8
operations, and prevented Arrays from shipping before Set and Map contracts.
It is replaced for Arrays by `VAR-12`; Set and Map operations remain behind
their decision gate.

`VAR-10` The editor MUST offer promoting a pin to a variable: it creates a
variable typed from the pin, wires an appropriate Get or Set node, and names it
from the pin.

`VAR-11` Blueprint-wide variable storage MUST live on a persistent
`blueprint_instance`, initialize from authored defaults exactly once when the
instance is created, survive separate event activations, and remain independent
between two instances of one definition. Function locals remain per call frame.

`VAR-12` Arrays MUST support Make Array, Get, Set Array Elem, Add, Add Unique,
Append, Insert, Clear, Contains, Find, Identical, Length, Last Index, Resize,
Remove Index, Remove Item, Is Empty, Is Not Empty, Is Valid Index, Reverse, and
Swap. Shuffle, random-item, and typed Sort nodes remain gated on accepted
random-service and ordering contracts. Actor-class Filter Array is a Consumer
operation, not a core array primitive.

`VAR-13` A function-local variable MUST be allocated in its function call frame
and MUST NOT be visible outside that function graph. Recursive and concurrent
calls MUST receive independent local storage initialized from the authored
local default.

## BP: blueprints and graphs

`BP-1` A blueprint is the unit of authoring and of the file format. It owns its
ID counter, its variables, its graphs, and its metadata.

`BP-2` A graph MUST declare its kind: event graph, function graph, or macro
graph.

`BP-3` A blueprint MAY contain any number of graphs of each kind. Graph names
MUST be unique within a blueprint.

`BP-4` A graph MUST own its nodes and edges. An edge MUST NOT cross graphs.
Cross-graph relationships are expressed by call nodes referencing a graph ID.

`BP-5` An event graph MAY contain many entry points, and execution paths from
different entries MAY merge.

`BP-6` A function graph MUST have exactly one entry node and at least one
return node. Its entry node's outputs are the function's parameters and its
return node's inputs are the function's results.

`BP-7` A macro graph MAY have several exec inputs on its entry node and several
exec outputs on its return node.

`BP-8` A blueprint MUST have exactly one default event graph, created with the
blueprint and not deletable.

`BP-9` Removing a node MUST remove every edge incident to it and MUST retire
its ID under `ID-4`, so a stale reference resolves to nothing rather than to
an unrelated entity.

`BP-10` Removing a node or edge that does not exist in the graph MUST report an
error rather than silently doing nothing.

`BP-11` Removing an existing edge MUST remove exactly that edge and leave
unrelated edges intact.

`BP-12` A Blueprint definition MAY declare one nominal owner `PIN_HANDLE` type.
Each instance of that definition MUST supply either a matching opaque handle or
no owner. The owner type is authored and serialized; the runtime handle value
is not.

`BP-13` A Blueprint definition is authored, shared data. A
`blueprint_instance` is runtime behavior state referring to one definition.
Creating instances MUST NOT copy or mutate the definition's graphs, pins,
defaults, or metadata.

## EVT: events

`EVT-1` An event node is an entry point: it has no exec input, at least one
exec output, and zero or more data outputs that carry the event's parameters.

`EVT-2` An event's name MUST be selected from the host event catalog through
the editor. It is stored in the node's reference name field.

`EVT-3` Event names MUST be unique within a blueprint. A duplicate MUST be a
compile error, and dispatching a duplicated name MUST fail rather than pick
one.

`EVT-4` WITHDRAWN by ADR 0021. Event parameter lists are not free-form editor
state. Catalog signatures create fixed data output pins.

`EVT-5` WITHDRAWN by ADR 0021. Custom event call-site signature propagation is
deferred with custom events.

`EVT-6` WITHDRAWN by ADR 0021. Call Event and its custom-event invocation path
are deferred.

`EVT-7` Dispatching an event by name MUST fail with a distinct, inspectable
error for unknown name and for duplicate name.

`EVT-8` WITHDRAWN. Begin and Tick are host lifecycle conventions, not
engine-independent language entries. It is replaced by `EVT-9` and `EVT-10`.

`EVT-9` Core MUST expose only generic named events with authored parameter
signatures. Dispatching an event on a Blueprint instance MUST create a distinct
activation carrying copied parameter values. Core MUST assign no lifecycle
meaning to an event name.

`EVT-10` The host event catalog MUST provide `Begin()`, `Tick(Real
DeltaSeconds)`, and `Input(CF_V2 Delta)` signatures. These signatures are host
conventions and MUST NOT be registered as mandatory core event types.

`EVT-11` WITHDRAWN by ADR 0021. Call Event is deferred with custom events.

`EVT-19` The editor MUST create event nodes only from the supplied host event
catalog. A created catalog event MUST receive the catalog name and every
catalog data pin in declared order. The editor MUST NOT expose free-form event
creation or parameter surgery.

`EVT-20` Fixed catalog event pins MUST be read-only in Inspector. Literal
values connected to or authored on other nodes remain editable. Dynamic pin
operations are permitted only for node types that own that behavior.

`EVT-12` Typed delegates and event dispatchers are a planned core language
family for communication between persistent Blueprint instances. Their callback
identity, target lifetime, invocation ordering, reentrancy, and serialization
MUST be fixed by an ADR before this requirement is expanded into executable
semantics.

`EVT-13` A delegate binding MUST have a stable instance-owned binding ID. Adding
the same target instance and event ID twice MUST return the existing binding.

`EVT-14` A delegate target MUST be weak. Destroying the target instance MUST
remove its bindings before its runtime storage is released, and dispatch MUST
never call a destroyed target.

`EVT-15` Delegate invocation MUST snapshot binding IDs in registration order.
Changes made by a callback take effect on the next dispatch, not the current
snapshot.

`EVT-16` Removing a delegate MUST address the binding ID and remove exactly one
binding, leaving other registrations intact.

`EVT-17` A binding whose event signature no longer matches MUST remain visible as
broken authored runtime state and produce one structured diagnostic rather than
silently coercing its parameters.

`EVT-18` Delegate bindings, callback queues, and invocation snapshots MUST NOT
be serialized or entered into the undo history.

## FN: functions and macros

`FN-1` A function's signature MUST be authored through the editor by editing
the entry node's output pins and the return node's input pins, using the same
instance-pin operations as `EVT-4`.

`FN-2` Calling a function MUST be a node whose reference ID is the function
graph, whose data inputs mirror the entry node's outputs, and whose data
outputs mirror the return node's inputs.

`FN-3` A function call MUST execute the callee's graph and return before the
caller's next node runs. A function MUST NOT suspend.

`FN-4` A function graph MUST support local variables under `VAR-2`.

`FN-5` A function with no exec pins on its entry and return nodes MUST be
usable as a pure node, and its calls MUST then participate in data evaluation
rather than execution.

`FN-6` Recursion MUST be permitted and MUST be bounded by a call-depth limit
that produces a structured error rather than a stack overflow.

`FN-7` A macro call MUST be expanded at compile time into a copy of the macro's
body in the calling graph, with the macro's entry and return exec pins rewired
to the call site's edges.

`FN-8` WITHDRAWN. Macro-local state was specified inconsistently with `VAR-2`
and is intentionally deferred. Macro exec shape and latent legality remain
covered by `BP-7` and `NODE-7`.

`FN-9` Macro expansion MUST be recursion-checked at compile time, and a
recursive macro MUST be a compile error.

`FN-10` A macro's wildcard pins MUST resolve per call site.

`FN-11` The editor MUST offer collapsing a selection of nodes into a new
function or macro, and expanding a call back into its nodes. Collapsing MUST
derive the signature from the edges crossing the selection boundary.

`FN-12` Before macro implementation begins, an ADR MUST fix expansion products,
macro-local state, wildcard resolution boundaries, and debugger source mapping.
Executable macro requirements MUST be appended after that decision rather than
inferred from the withdrawn `FN-8`.

## DATA: data evaluation

`DATA-1` Data evaluation is pull-based: evaluating an output recurses into the
inputs it needs, so only requested outputs are computed.

`DATA-2` Within one activation a node MUST compute at most once. The memo MUST
be cleared between activations.

`DATA-3` A cycle among data edges MUST be detected and MUST produce a
structured error naming a node on the cycle.

`DATA-4` Data evaluation MUST be deterministic: the same graph, defaults, and
variable values MUST produce the same outputs.

`DATA-5` Exec edges MUST NOT participate in data dependency resolution.

`DATA-6` Transient computed values MUST be owned by a per-activation arena. No
node may allocate or free individually.

`DATA-7` Evaluation MUST return a status and MUST expose a structured result
carrying a code, the responsible node, the responsible pin where applicable,
and a message. The status MUST live on the evaluation context, not in a global.

`DATA-8` These MUST be errors rather than silently-computed nodes: a cycle, an
invalid descriptor, a missing struct input, an unresolved wildcard, a retired
reference, and an arena allocation failure.

`DATA-9` A struct value produced during an activation MUST remain valid for the
lifetime of that activation and MUST NOT be assumed valid afterwards. A value
that must outlive its activation MUST be copied out.

## EXEC: imperative execution

`EXEC-1` Imperative execution walks exec edges from one entry node, and is
separate from data evaluation. See `docs/adr/0006`.

`EXEC-2` Execution state MUST be an explicit continuation stack of frames, not
the C call stack, so a run can suspend and resume.

`EXEC-3` Activating a node MUST reset data memoization first, so each
activation sees fresh data.

`EXEC-4` A node MUST be able to continue to a named exec output, to fail with a
code and message, to suspend until a host time, and to iterate a bounded range.

`EXEC-5` Ordered fan-out MUST run its outputs in ascending order, and a failure
in one MUST stop the remaining ones.

`EXEC-6` A run MUST enforce a step budget. Exhausting it MUST produce a
structured error, not a hang.

`EXEC-7` A run MUST enforce a call-depth limit for function calls and macro
recursion checks.

`EXEC-8` WITHDRAWN. A persistent instance retains variables and node scratch
even when no activation is running, so guarding only non-idle runs is
insufficient. It is replaced by `EXEC-19`.

`EXEC-9` A run MUST record the blueprint's topology revision at start, and MUST
fail with a distinct error if it detects the topology changed under it.

`EXEC-10` Suspension MUST use a host-supplied monotonic clock. Core MUST NOT
call any wall-clock or sleep function.

`EXEC-11` A latent node that has suspended MUST resume at the point it
suspended, with its frame and its loop state intact.

`EXEC-12` A host callback channel MUST be reachable from a node's execution
callback, so side-effecting nodes such as Print String deliver output to the
host rather than to stdout.

`EXEC-13` WITHDRAWN. Stateful-node scratch must survive separate events on one
persistent instance. It is replaced by `EXEC-15`.

`EXEC-14` WITHDRAWN. Its re-entry rule remains necessary, but its run-scoped
scratch premise is replaced by instance scratch and activation-local memo in
`EXEC-15` through `EXEC-18`.

`EXEC-15` Per-node scratch MUST live on the Blueprint instance, keyed by node
ID, so DoOnce, Gate, MultiGate, FlipFlop, and related nodes share state across
event activations on that instance and start clean on another instance.

`EXEC-16` Each event activation MUST own its continuation and call stacks,
temporary arena, data memo, cycle guard, parameter values, structured error,
step count, watch values, and latent state. None of that activation-local state
may be read or freed by another activation.

`EXEC-17` One Blueprint instance MAY have several ready or suspended
activations. Its scheduler MUST activate at most one at a time and MUST choose
the next ready activation in deterministic order. A suspended activation MUST
NOT prevent a later event from starting.

`EXEC-18` A node MUST tolerate re-entry while an earlier activation of it
remains on a continuation stack. Re-entrant executions on one instance share
instance scratch under `EXEC-15` and never share data memo under `EXEC-16`.

`EXEC-19` WITHDRAWN. Its immutability rule is right, but its teardown rule
required destroying instances before every authored edit, which does not scale
past one preview instance and is unnecessary when authoring targets a
definition that has none. It is replaced by `EXEC-20`.

`EXEC-20` A Blueprint definition MUST be immutable while any runtime instance
is attached to it. Authoring MUST target a definition with no attached
instances. Replacing the definition a world is running MUST destroy that
definition's instances, replace the definition, and recreate them as one
operation. See `docs/adr/0020` and the `APPLY` section of
`docs/game-spec.md`.

## RUN: run lifecycle

`RUN-1` A run MUST be startable from a named event, from a specific entry node,
and with a supplied parameter list.

`RUN-2` A run MUST report exactly one state: idle, running, suspended, paused,
completed, or failed. Suspended means a latent node is waiting; paused means
the host or user stopped it between steps.

`RUN-3` A run MUST be pausable between node activations by an external caller,
not only by a latent node.

`RUN-4` A run MUST support stepping exactly one node activation and returning,
leaving the run paused.

`RUN-5` A run MUST support stepping over a call node, meaning run until control
returns to the calling graph.

`RUN-6` WITHDRAWN. Stopping one event activation must not discard persistent
instance variables or node scratch. It is replaced by `RUN-15` and `RUN-17`.

`RUN-7` WITHDRAWN. Reset is not a separate lifecycle operation. Stop carries
its cleanup semantics under RUN-6, matching Unreal's Play/Stop session model.

`RUN-8` WITHDRAWN. Stopping one activation cannot release a definition guard
held by its persistent instance or another activation. It is replaced by
`RUN-20`.

`RUN-9` A run MUST expose its step count, its current node, its call stack as
an ordered list of graph and node IDs, and its structured error.

`RUN-10` Advancing time MUST be host-driven: the host supplies a monotonic time
and asks the run to continue.

`RUN-11` WITHDRAWN. Separate activations on one instance intentionally share
variables and node scratch. Instance and activation isolation are specified by
`RUN-18`.

`RUN-12` A run MUST refuse to start against a blueprint with compile errors,
reporting the first blocking diagnostic.

`RUN-13` Run state MUST NOT be serialized. Saving while a run is non-idle MUST
write authored data only, and a suspended latent node MUST NOT survive a save
and load.

`RUN-14` WITHDRAWN. A definition can have several activations and persistent
instance state, so releasing only one `flow_run` is insufficient. It is
replaced by `RUN-19`.

`RUN-15` A Blueprint instance MUST have explicit create and destroy operations.
Create initializes owner-independent state and authored variable defaults.
Destroy cancels every ready, running, paused, or suspended activation and then
releases instance variables and per-node scratch without executing another
node.

`RUN-16` Starting, stepping, pausing, resuming, stopping, and inspecting apply
to one event activation. Advancing an instance scheduler MAY make another ready
activation current, but MUST NOT merge their lifecycle states, errors, step
counts, or watch values.

`RUN-17` Stopping an event activation MUST release only that activation's
continuations, temporaries, memo, error, watch, and latent state. It MUST leave
the containing instance and its variables and per-node scratch intact.

`RUN-18` Two instances of one definition MUST have independent owner handles,
variables, node scratch, service bindings, activation queues, errors, and step
counts. Activations on one instance share only the state explicitly owned by
that instance.

`RUN-19` WITHDRAWN. It required teardown at every authoring edit rather than at
the point a running definition is replaced, and it scoped teardown to preview
instances rather than to the changed definition. It is replaced by `RUN-21`.

`RUN-21` Loading, undo, redo, and authored structural change MUST target a
definition with no attached instances. Replacing a running definition MUST be
scoped to that definition: instances of every other definition, and all host
state those instances refer to, MUST be unaffected. Runtime state MUST NOT
enter the undo stack.

`RUN-20` Creating an instance MUST acquire its definition's mutation guard, and
destroying or explicitly detaching that instance MUST release it. A definition
becomes mutable only after its last attached instance is gone; stopping or
completing one activation MUST NOT release the guard.

## CAT: node catalog

`CAT-1` WITHDRAWN. It treated withdrawn, Deferred, Generated, and Consumer
families as one fixed catalog. It is replaced by `CAT-29` and `CAT-36`.

Flow control:

`CAT-2` Branch. Exec in, bool condition, True and False exec outs.

`CAT-3` Sequence. Exec in, N exec outs run in order, N user-extensible from 2
with no fixed maximum.

`CAT-4` ForLoop. Exec in, first and last index, Body exec out, Index data out,
Completed exec out, and a half-open range that increments or decrements toward
but excludes last. Equal bounds skip Body.

`CAT-5` ForLoopWithBreak. ForLoop plus a Break exec in that stops iteration and
continues to Completed.

`CAT-6` WhileLoop. Exec in, bool condition re-evaluated per iteration, Body and
Completed exec outs.

`CAT-7` ForEachLoop. Exec in, container in as a wildcard, Body exec out,
element and index data outs, Completed exec out. A break variant MUST exist.

`CAT-8` WITHDRAWN. Its pin shape remains valid, but run-scoped state is not.
The instance-scoped replacement is `CAT-35`.

`CAT-9` DoN. Exec in, Reset exec in, N in, Exit exec out, Counter data out.

`CAT-10` FlipFlop. Exec in, A and B exec outs alternating, bool "is A" data
out.

`CAT-11` Gate. Enter, Open, Close, and Toggle exec ins, Exit exec out, bool
"start closed".

`CAT-12` MultiGate. Exec in, Reset exec in, bool loop, bool random, start-index
in, N user-extensible exec outs.

`CAT-13` Switch on Int. Exec in, int selection, one exec out per configured
case plus Default. Cases are authored per node.

`CAT-14` Switch on String. As `CAT-13` with string cases.

`CAT-15` Switch on Enum. Exec in, enum selection, one exec out per enum entry,
derived from the enum type named in the node's reference name.

`CAT-16` Select. Wildcard option inputs, an index or enum selector, one
wildcard output. Pure.

`CAT-17` WITHDRAWN. Core cannot define validity for an unspecified value.
Object and handle validity remain Consumer or Generated operations until a
handle-type validity contract is accepted.

Latent:

`CAT-18` Delay. Exec in, duration, Completed exec out. A second activation
while waiting MUST be ignored.

`CAT-19` RetriggerableDelay. As `CAT-18`, except a second activation restarts
the timer.

Utility:

`CAT-20` Print String. Exec in, string in, exec out. Output goes to the host
callback channel under `EXEC-12`.

`CAT-21` WITHDRAWN. "Every primitive" incorrectly creates host-handle and
non-UE-width literals. It is replaced by `CAT-30`; reflected struct and enum
construction remains under `CAT-22`.

`CAT-22` Make and Break for every reflected struct type. Pure.

`CAT-23` Call nodes for every reflected function. Pure unless the descriptor
declares otherwise.

`CAT-24` WITHDRAWN. It inherits the incomplete bundled container list in the
withdrawn `VAR-9`. Arrays are replaced by `CAT-34`; Set and Map remain behind
their decision gate.

`CAT-25` WITHDRAWN. Concrete operators for every C numeric primitive do not
match UE promotable operators. It is replaced by `CAT-33`.

`CAT-26` Explicit conversion nodes for every conversion permitted by `TYPE-7`.

`CAT-28` WITHDRAWN. It omitted Name and gave Make Literal nodes the wrong pin
shape. It is replaced by `CAT-30`.

`CAT-27` Every catalog entry MUST declare a category path used for grouping in
the spawn menu, and a search keyword list.

`CAT-29` The maintained parity matrix MUST list each audited UE 5.8 language
family and its Core, Generated, Consumer, Deferred, or Excluded disposition.
Catalog completeness means every Core entry is authorable and tested and every
Generated family has a tested reflection contract; it does not mean the fixed
catalog enumerates engine or plugin actions.

`CAT-30` Core literal nodes are Bool, Byte, Integer, Integer64, Real, Name, and
String. Each is pure and has one typed `Value` input carrying the authored
default and one typed return output. Extra C integer widths, nominal handles,
and localized Text do not receive core literal nodes.

`CAT-31` Switch on Name has an exec input, a Name selection input, one exec
output per authored Name case, and Default. Its cases use the accepted Name
equality policy from `TYPE-12`.

`CAT-32` Self is a pure node whose output has the Blueprint definition's
optional nominal owner-handle type. Creating Self in a definition with no owner
type MUST be rejected, and evaluating it returns that instance's supplied
opaque owner handle.

`CAT-33` Add, Subtract, Multiply, Divide, equality, ordering, And, Or, and Not
MUST use promotable wildcard nodes backed by registered operation signatures.
Resolution is exact-signature first, then the ladder in `TYPE-14`; no candidate
or an ambiguous best candidate is a compile error. Consumers MAY add operation
signatures without modifying core.

`CAT-34` Core array operation nodes MUST cover every operation in `VAR-12`,
plus ForEach and ForEach with Break under `CAT-7`. Their wildcard groups resolve
to one element type per node instance.

`CAT-35` DoOnce has Exec and Reset exec inputs, a Completed exec output, and an
authored Start Closed bool. Its fired state lives in instance scratch under
`EXEC-15`, so separate events on one instance share it and another instance
does not.

`CAT-36` Every non-withdrawn Core catalog entry MUST expose its specified pin
shape, authorable configuration, category, search keywords, runtime behavior,
and headless acceptance check. Generated and Consumer entries satisfy parity
through their registered contracts rather than by appearing in this fixed
list.

`CAT-37` Set Members in Struct is executable with exec input and output, one
pass-by-reference struct input under `PIN-15`, and one authorable data input per
exposed member. It MUST update only the exposed members and preserve every
unexposed member.

`CAT-38` Format Text is pure with one String format input, a user-extensible
list of wildcard argument inputs, and one String output. Each argument pin
adopts the type of the value connected to it under `PIN-11` and reverts to
unresolved when its driver edge is dropped. Evaluating it substitutes `{0}`,
`{1}`, ... and bare `{}` (the next argument in order) in the format string with
the corresponding argument's rendering; `{{` and `}}` render literal braces. A
placeholder MAY carry printf-style width, alignment, and precision modifiers
(`{:.2f}`, `{:08d}`, `{:x}`), which render numeric values with the matching
conversion family, promoting integers for floating-point modifiers; a
placeholder with no modifier renders through `pin_value_to_string`. A
placeholder whose index has no matching argument renders as the empty string.

## CMPL: compile and validation

`CMPL-1` Compiling MUST be an explicit operation over one blueprint that
produces an ordered diagnostic list and never modifies the authored graph.

`CMPL-2` A diagnostic MUST carry a severity of error or warning, the graph ID,
the node ID, the pin ID where applicable, a stable rule code, and a message.

`CMPL-3` Errors MUST block a run under `RUN-12`. Warnings MUST NOT.

`CMPL-4` Warning: a required exec input with no incoming edge on a node that can
only run from an entry path. The node is unreachable, but the graph remains
valid to apply.

`CMPL-5` Error: a data edge between incompatible pins, which can arise from a
retype after the edge was made.

`CMPL-6` Error: a cycle among data edges.

`CMPL-7` Error: a reference to a retired or missing variable, event, function,
or macro.

`CMPL-8` Error: a duplicate event name, or a duplicate name within one
parameter list, or a duplicate variable name in one scope.

`CMPL-9` Error: a latent node in a function graph.

`CMPL-10` Error: a function graph without exactly one entry node, or without at
least one return node.

`CMPL-11` Error: a recursive macro expansion.

`CMPL-12` Error: an unresolved wildcard pin that carries an edge.

`CMPL-13` Warning: a node unreachable from any entry node.

`CMPL-14` Warning: an unconnected data input on an impure node, which silently
uses its default.

`CMPL-15` Warning: a variable that is never read, and a variable that is never
written.

`CMPL-16` Compiling MUST be deterministic and MUST order diagnostics by graph,
then node position, then rule code, so the list is stable between runs.

`CMPL-17` Every diagnostic MUST be navigable: selecting it selects and reveals
the node or pin it names.

`CMPL-18` A node disabled under `NODE-11` MUST be excluded from every rule in
this section except one warning reporting that it is disabled.

`CMPL-19` A promotable operator with no matching registered signature or with
an ambiguous best signature MUST be a blocking diagnostic naming the operation
and the connected pin types.

`CMPL-20` An authored owner-handle, struct, enum, or other nominal type name
that is unavailable in the active registry MUST be a blocking diagnostic that
preserves and names the unresolved type.

## DBG: debugging

`DBG-1` A breakpoint MUST be settable on any node with an exec pin, keyed by
node ID, and MUST persist with the blueprint.

`DBG-2` Hitting a breakpoint MUST pause the run before that node's activation,
with the node identified.

`DBG-3` A breakpoint MUST be individually enabled or disabled, and all
breakpoints MUST be disableable at once without losing them.

`DBG-4` A watch MUST be settable on any data pin, keyed by node ID and pin ID,
and MUST persist with the blueprint.

`DBG-5` A watched pin's value MUST be captured at execution time and MUST
remain readable after the activation that produced it. A struct value MUST be
copied into watch storage, never retained as a pointer into a released arena.

`DBG-6` While a run is non-idle, pin value displays MUST show values recorded by
that run, not values from continuous preview evaluation. See `docs/adr/0008`.

`DBG-7` The most recently traversed exec edges MUST be identifiable so the
editor can show the execution path, with the traversal order preserved.

`DBG-8` The call stack under `RUN-9` MUST be presented as a navigable list, and
selecting a frame MUST reveal its node.

`DBG-9` A run's structured error MUST identify the responsible node and pin, and
MUST be navigable under `CMPL-17`.

`DBG-10` Continuous preview evaluation of pure nodes is an editor convenience.
It MUST be suspendable, MUST NOT write into run-owned storage, and MUST NOT
call any impure or latent node.

`DBG-11` A watch on a pure node's pin MUST be captured during data evaluation,
not during execution, because a pure node is never activated by the imperative
evaluator. A watch on such a pin MUST still report a value after a run.

`DBG-12` Deleting a node, or removing a pin, MUST discard the breakpoints and
watches attached to it. A discarded breakpoint MUST NOT reappear on a later
node that reuses its position.

`DBG-13` A node whose last activation or evaluation failed MUST render as
failed, and hovering a pin that carries a diagnostic MUST show that
diagnostic's message.

`DBG-14` Stopping a run MUST discard its recorded watch values. After Stop,
pin displays MUST return to continuous preview values.

`DBG-15` Each traversal record under `DBG-7` MUST carry the run time at which
the edge was traversed and the activation ordinal, so a consumer can age a
record without observing execution as it happens.

`DBG-16` Traversal records MUST be bounded, discarding the oldest once the
budget is reached, so a long run cannot grow them without limit.

`DBG-17` When an executable node evaluates a pure data chain, traversal records
MUST include every data edge evaluated in that chain, in evaluation order, so a
flow visualisation can show the requested data path rather than only the final
value entering the executable node.

`DBG-18` An entity-owned runtime instance MUST support an inspection hold for
completed activations and an instance-local breakpoint enable override, so a
host debugger can inspect one entity without changing another entity's run.

`DBG-19` Runtime sink messages emitted by an entity-owned behavior MUST be
retained with the entity and behavior asset identity so the host can inspect
messages without confusing them with the editor preview log.

## TRACE: runtime trace retention

`TRACE-1` A completed activation MUST leave a terminal trace record, including
for an event with no traversed edges, until its inspection record is released.

`TRACE-2` A failed activation's trace record MUST retain its event name and
structured error code after the activation is released.

`TRACE-3` Runtime trace storage MUST be bounded oldest-first, survive activation
pooling, and clear when an instance is reused.

## FMT: file format

`FMT-1` A blueprint MUST serialize to a single JSON document with
`"version": 3`.

`FMT-2` The document MUST contain the ID counter, the blueprint metadata, the
variable list, and the graph list.

`FMT-3` Every entity MUST be written with its ID, and every reference MUST be
written as an ID. Array position MUST carry no meaning.

`FMT-4` An edge MUST record its kind as `"data"` or `"exec"`, its source node
and pin IDs, its target node and pin IDs, and, for exec only, its order.

`FMT-5` A node MUST record its descriptor name, its ID, its position, its title
override, its comment, its reference ID, its reference name, and its full
instance pin list including authored defaults, split parents, and wildcard
resolution.

`FMT-6` A pin's authored default MUST be written; a pin's runtime value MUST
NOT be.

`FMT-7` Breakpoints, watches, and comment boxes MUST be serialized. Canvas pan
and zoom SHOULD be serialized per graph.

`FMT-8` Loading MUST reject a malformed or unsupported document with a message
naming the offending path, and MUST leave the caller's existing data unchanged.

`FMT-9` Versions before 3 are not supported. See `docs/adr/0003`.

`FMT-10` A subset save MUST produce a document of the same schema containing
the selected nodes, the edges with both endpoints inside the selection, and the
variables those nodes reference, for use by clipboard and undo.

`FMT-11` Append MUST allocate fresh IDs under `ID-5` and MUST offset positions
by a supplied delta.

`FMT-12` Save and load MUST round-trip: loading a saved document and saving it
again MUST produce a semantically identical document.

`FMT-13` Output MUST be stable: key order and number formatting MUST be
deterministic so files diff cleanly.

`FMT-14` WITHDRAWN. The exact metadata list omitted the authored owner-handle
type added by `BP-12`. It is replaced by `FMT-18`.

`FMT-15` Copy MUST place the subset document from `FMT-10` on the host text
clipboard, and paste MUST accept a subset document from it, so a selection
transfers between editor instances as text.

`FMT-16` A Blueprint definition's optional owner-handle type and every authored
Name value MUST serialize in the Blueprint document. A runtime owner handle,
service binding, Blueprint instance, activation, continuation, variable value,
node scratch value, or watch value MUST NOT serialize.

`FMT-17` Loading a Name MUST preserve its authored spelling exactly. Loading an
owner-handle type that is unavailable in the active registry MUST preserve the
name and produce a compile diagnostic rather than substituting another type.

`FMT-18` Blueprint metadata is exactly the schema version, Blueprint name, ID
counter, free-form description, and optional nominal owner-handle type.
Editor-only and runtime state MUST NOT be written, per `FMT-16`.

## UI: editor surfaces

`UI-1` Every value the file format can express MUST be authorable in the
editor. No feature ships whose configuration requires hand-editing JSON. This
is the binding constraint on every other `UI` requirement.

`UI-2` The editor MUST present a canvas, a toolbar, Components, My Blueprint,
a universal Inspector, a compiler results panel, and an output log.

`UI-3` My Blueprint MUST list graphs by kind, functions, macros, variables, and
Prototype-owned Behavior modules. It MUST NOT duplicate an Events list or an
active graph's Nodes list.

`UI-4` The universal Inspector MUST be context-sensitive on the current
selection and MUST expose every remaining authorable property of it: fixed
catalog signatures are read-only, node execution state remains editable, and
variable/component/instance fields use their owning document's model. Node
title overrides, node comments, reference IDs, and generic pin schema editing
are not Inspector properties.

`UI-5` The universal Inspector MUST be the single place a contextual property
is edited. A property reachable only through a context menu or only through the
canvas is a defect.

`UI-6` A Behavior toolbar MUST offer Compile, Run, Pause, Resume, Step, and
Stop, plus an entry-point selector listing catalog events, and MUST show the
current run state and step count. Scene Runtime MUST separately offer Play,
Pause, Step, Restart, and Stop.

`UI-7` A toolbar action that is invalid in the current run state MUST be
disabled with a reason on hover, never silently ignored.

`UI-8` The output log MUST receive host callback output under `EXEC-12` with the
originating graph and node, and clicking an entry MUST select and reveal that
node.

`UI-9` The compiler results panel MUST list diagnostics under `CMPL-2` and MUST
navigate under `CMPL-17`.

`UI-10` Pin value display MUST follow `DBG-6`: run-recorded values while a run
is live, preview values otherwise.

`UI-11` A watched pin MUST show its value inline on the canvas, and watches MUST
be toggleable from the pin's context menu and from the details panel.

`UI-12` A breakpoint MUST be toggleable from the node's context menu and by a
keyboard shortcut, and MUST render on the node.

`UI-13` The spawn menu MUST group by the category path from `CAT-27`, MUST
filter on name and keywords, and MUST be context-sensitive when opened by
dragging from a pin, offering only nodes with a compatible pin.

`UI-14` Dropping a wire on empty canvas MUST open the spawn menu filtered to
compatible nodes and MUST wire the created node on accept.

`UI-15` The canvas MUST support node placement and dragging, pan, zoom,
single and additive selection, marquee selection, group drag, link creation
with live type feedback, link and node deletion, and hover highlighting of
nodes, pins, and links.

`UI-16` Cut, copy, paste, duplicate, delete, undo, redo, save, and load MUST be
bound to keyboard shortcuts, and the binding table MUST be data, not a chain of
conditionals, so it can be listed in the UI.

`UI-17` Undo and redo MUST cover every authoring operation in the active
Prototype, Behavior, or Scene document, including pin edits, variable edits,
component edits, scene overrides, and graph creation and deletion. A propagated
Prototype edit MUST undo as one transaction.

`UI-18` Undo MUST NOT capture run state. Starting, stepping, pausing, or
stopping a run MUST NOT create an undo entry.

`UI-19` Selecting a node programmatically MUST be possible, and revealing a
node MUST center the canvas on it, for use by `CMPL-17`, `UI-8`, and `DBG-8`.

`UI-20` A minimap SHOULD exist for large graphs.

`UI-21` The editor MUST support several open graphs with per-graph canvas
state, since functions and macros are edited alongside the event graph.

`UI-22` A rejected connection MUST say why at the cursor, naming both pin types,
rather than failing silently. Where `TYPE-7` permits a conversion, the offer to
insert it MUST appear in the same place.

`UI-23` A node with a required input that has no driver and no authored default
MUST show that on the node, and hovering MUST name the pin.

`UI-24` Adding and removing pins in a user-extensible group under `PIN-14` MUST
be available on the node itself, not only in the details panel, because that is
where the pin count is being read.

`UI-25` Splitting and recombining a struct pin under `PIN-12` MUST be available
from that pin's context menu.

`UI-26` Advanced pins under `PIN-13` MUST be shown and hidden by an expander on
the node.

`UI-27` An enum-typed value MUST be edited as a dropdown of the labels from
`TYPE-4`, never as a raw integer.

`UI-28` A switch node's case list MUST be authorable in the details panel: add,
remove, reorder, and set each case value. For Switch on Enum the list is
derived and MUST be read-only.

`UI-29` WITHDRAWN by ADR 0021. Find in Blueprint is deferred until user testing
shows a need.

`UI-30` WITHDRAWN by ADR 0021. Find references is deferred with Find in
Blueprint.

`UI-31` Double-clicking a call node MUST open its target graph. Double-clicking
a wire MUST insert a reroute node at that point.

`UI-32` Disabling a node under `NODE-11` MUST be available from its context
menu, and a disabled node MUST render distinctly from an enabled one.

`UI-33` A comment box's title, color, and size MUST be editable: the title from
the canvas, the rest from the details panel.

`UI-34` Breakpoints MUST be listable in one place with per-breakpoint and
global enable toggles, per `DBG-3`.

`UI-35` Node and pin coloring MUST follow `NODE-12` and `PIN-16`, and the
mapping MUST be one table rather than scattered constants.

`UI-36` WITHDRAWN. Releasing one live run is insufficient when a persistent
preview instance owns state and several activations. It is replaced by
`UI-55`.

`UI-37` Auto arrange, straighten connection, and zoom to fit SHOULD exist.
Auto arrange MUST reduce wire crossings and wire-node collisions rather than
only spacing nodes out, MUST leave no two node boxes overlapping, and MUST be
deterministic and bounded for a graph of any size. See `docs/adr/0015`.

`UI-38` Holding a bound key while clicking the canvas SHOULD spawn that node
type directly, bypassing the spawn menu for the most-used nodes.

`UI-39` Pin values MUST have one canonical textual rendering shared by pin
displays, the details panel, watches, and the output log, so the same value
never renders two ways.

`UI-40` The output log MUST offer a Clear action that removes all retained
entries without changing the authored graph or the undo history.

`UI-41` WITHDRAWN. The edit choke point must tear down the affected preview
instance, not only one non-idle run. It is replaced by `UI-56`.

`UI-42` Editor panels MUST be independently movable, resizable, and dockable,
and their layout MUST persist between sessions.

`UI-43` The output log MUST be its own dockable panel rather than a fixed
region inside the canvas panel.

`UI-44` While a run is non-idle, an exec edge that has been traversed MUST
animate marker dots travelling along it from source to target, so direction of
flow is readable without reading pin names.

`UI-45` A recently traversed exec edge MUST render thicker and brighter than a
resting edge, and MUST decay back to its resting weight and colour over time
rather than snapping.

`UI-46` The node being activated MUST highlight, and the highlight MUST decay
once activation moves on.

`UI-47` A data pin whose value changed during the current run MUST flash and
settle back to its resting appearance.

`UI-48` Every transition in `UI-44` through `UI-47` MUST be interpolated over
elapsed time, not frame count, so the animation is identical at any frame rate
and on any display refresh.

`UI-49` Flow visualisation MUST be derived from the traversal records in
`DBG-7` and `DBG-15`. The renderer MUST NOT observe execution directly or hold
a callback into the evaluator, so visualisation cannot alter execution timing
or outlive the run's data.

`UI-50` When no run is active, flow visualisation MUST settle to the resting
state rather than freezing part-way through a transition.

`UI-52` Canvas shortcuts MUST remain available unless a text-entry field holds
focus, in which case that field owns the editing keys. No other active widget
may suppress them. A focused field MUST be visually distinct, and clicking the
canvas or pressing Escape MUST release it.

`UI-51` Dragging any node in a multi-selection MUST move every selected node
together and preserve their relative positions.

`UI-53` The universal Inspector MUST expose the Blueprint definition's
optional owner handle type and fixed host event signatures. Catalog event pins
MUST remain read-only. Service-backed pins MUST remain visible while connected,
MAY hide while unconnected and resolved, and MUST reappear with an unresolved
indication when their service is absent.

`UI-54` The preview runtime MUST identify its Blueprint instance and current
event activation separately. Stopping one activation MUST leave persistent
instance variables available for later event dispatch; destroying or replacing
the preview instance MUST clear them.

`UI-55` Behavior graph edits MUST mutate a working copy while an existing
preview keeps its applied snapshot. Editing MUST NOT require preview teardown
or be rejected because a preview is attached.

`UI-56` WITHDRAWN. It routed every authored mutation through instance teardown,
which makes editing cost scale with the number of live instances.

`UI-57` The editor MUST route every replacement of a running definition through
one choke point, and MUST state that applying destroys and recreates that
definition's instances. An authored edit that does not replace a running
definition MUST NOT require teardown and MUST NOT be rejected. The editor MUST
show when a definition has unapplied edits.

## REFL: reflection

`REFL-1` Reflection MUST be a build-time tool over annotated C sources, using
libclang, emitting a generated C registry. See `docs/adr/0005`.

`REFL-2` Annotations MUST use `__attribute__((annotate(...)))`, so annotated
headers compile under both GCC and Clang.

`REFL-3` An annotated struct MUST emit a `type_desc`, a Make descriptor, and a
Break descriptor.

`REFL-4` WITHDRAWN. It allows only ordinary inputs and one non-void return and
cannot express executable calls, explicit output parameters, or injected
services. It is replaced by `REFL-9` through `REFL-14`.

`REFL-5` An annotated enum MUST emit a labelled enum descriptor under `TYPE-4`.

`REFL-6` Generated struct field offsets and sizes MUST come from the real
compiler through `offsetof` and `sizeof` in the emitted C, not from libclang's
layout numbers.

`REFL-7` The expansion policy from annotation to descriptors lives in the tool,
not in the runtime, so an alternative policy can emit different node sets from
the same type descriptors.

`REFL-8` The tool MUST fail the build with a located message on an
unsupported annotated declaration, rather than silently omitting it.

`REFL-9` A reflected function MUST opt into exactly one explicit graph contract:
`NODE_FN` for pure evaluation or `NODE_EXEC` for ordered execution. Purity MUST
NOT be inferred from C constness, pointer spelling, or function-body analysis.
An unannotated function emits no call node.

`REFL-10` A reflected function MAY return void or one ordinary C value and MAY
mark supported pointer parameters explicitly as graph outputs. Pointer
direction MUST NOT be inferred from constness. Generated wrappers MUST expose
all ordinary inputs, the optional return, and every explicit output as stable
instance pins.

`REFL-11` An injectable service pointer type MUST be declared once as a service
type. `reflect.c` MUST generate its service descriptor, stable type identity,
lookup metadata, and typed host binding helper from that declaration. A
function using the pointer type MUST NOT repeat a per-parameter context
annotation.

`REFL-12` A generated call descriptor MUST retain every service-backed input
pin under `PIN-17`. At execution, a connected value wins; otherwise the wrapper
looks up the matching bound service. An unresolved required service MUST return
a structured error naming the pin and nominal service type.

`REFL-13` `reflect.c` MUST generate an operation table mapping each annotated
operation name to its pure function signatures. Operator resolution MUST be
deterministic, use `TYPE-14`, and permit a consumer to register additional
signatures without editing the evaluator.

`REFL-14` An event activation MUST snapshot its borrowed service bindings when
it is dispatched. Core MUST NOT own, retain, or destroy a bound service. The
host MUST keep each borrowed service alive until every activation holding that
snapshot has completed or been stopped.

## Implementation layout

Where each part of this document lives. `SCOPE-1` is enforced by the dependency
edges: only `editor` and `app` link the host.

`core/` holds the node descriptor, pin model, type descriptors, graph storage,
data evaluation, imperative execution, built-in node descriptors, the compile
pass, and serialization. Depends on libc and vendored yyjson and fmt only.

`reflect/` holds the build-time libclang tool. Depends on libclang, and nothing
at runtime depends on it.

`generated/` holds the tool's output, compiled into `core`.

`editor/` holds the dcimgui blueprint widget, its panels, and the preview
evaluation view. Depends on `core` and the host.

`app/` holds the host frame loop. See `docs/adr/0009`.

`demo/` holds annotated example types and functions that exercise reflection.

`test/` holds `self_check.c`, which links `core` and `demo` and never creates a
window, per `SCOPE-2`.

`third_party/` holds vendored dependencies: yyjson for JSON, debugbreak for
debugger breakpoints, and fmt for Format Text string formatting.

## Deferred

Recorded so they are not rediscovered or decided before their implementation
section.

### Host-specific families

- Blueprint classes, inheritance, interfaces, class defaults, and
  Construction Script.
- Consumer-defined casts, validity, object/class/interface handles, localized
  Text, replication, and network authority.

The ECS binding is no longer deferred. It is specified in `docs/game-spec.md`
as a consumer module, which is what the scope statement above always required
of it.

### Language decision gates

- Delegate callback identity, target lifetime, invocation ordering,
  reentrancy, and serialization under `EVT-12`.
- Set and Map key equality, hashing, mutation, and deterministic save order.
- Macro local state, macro expansion products, and debugger source mapping for
  macros and collapsed graphs.
- Async actions, timelines, cancellation, curve authoring, and persistence.

### Optimization gates

- Bytecode compilation of graphs. `EXEC` describes an interpreter; a compiler
  is an optimisation, not a correctness requirement.
- Mutable string values. `PIN_STR` is an immutable arena- or blueprint-owned
  string.
- Dirty tracking for data evaluation, pending a profile that shows full
  recompute is the bottleneck.
