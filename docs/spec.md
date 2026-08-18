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

In scope: named and custom events with parameters, variables including array,
set, and map containers, user-defined functions, macros, the full flow-control
catalog, a compile and validation pass, and an interactive debugger.

Out of scope for now: actors, Blueprint class assets, inheritance, interfaces,
class defaults, Construction Script, and event dispatchers bound to objects.
An ECS binding is a separate optional module layered on the same node
descriptor API, never folded into core.

`SCOPE-1` Core (graph, types, evaluation, execution, serialization, compile)
MUST NOT include any host, windowing, rendering, or ECS header. The only
runtime dependencies are libc and the vendored yyjson.

`SCOPE-2` Core MUST be usable headlessly. Every behavior in this document
except the `UI` section MUST be reachable and testable without creating a
window.

`SCOPE-3` Core MUST NOT hold mutable process-global state. Error reporting,
evaluation status, and run state are per-instance, so two runs, or a run plus a
headless test, cannot interfere.

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

`TYPE-1` The primitive pin types are `PIN_BOOL`, the sized integers `PIN_I8`,
`PIN_U8`, `PIN_I16`, `PIN_U16`, `PIN_I32`, `PIN_U32`, `PIN_I64`, `PIN_U64`, the
floats `PIN_F32` and `PIN_F64`, `PIN_STR`, `PIN_STRUCT`, `PIN_ENUM`, and the
execution marker `PIN_FLOW`.

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

`TYPE-9` A container pin MUST NOT connect to a single-value pin of the same
element type, and vice versa. Container and element are converted by explicit
nodes only.

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

## NODE: node model

`NODE-1` A node MUST reference exactly one node descriptor, which supplies its
behavior, its default title, its pin template, and its category.

`NODE-2` A node MUST carry a canvas position. Core evaluation MUST ignore it.

`NODE-3` A node MUST carry an optional title override. When set, the editor
displays it instead of the descriptor name; it MUST NOT affect behavior or
lookup.

`NODE-4` A node MUST carry an optional user comment string, rendered as a
bubble attached to the node.

`NODE-5` A node MUST carry two general instance-reference fields: a 64-bit
reference ID and a reference name. Together with instance pins these express
every per-node configuration in this document: the target of a variable Get or
Set, the target of a function or macro call, a custom event's name, and a
switch's enum type. No feature may add a bespoke per-node field where these
suffice.

`NODE-6` A node descriptor MUST distinguish pure from impure. A pure node has
no exec pins and MUST be re-derivable at any time from its inputs. An impure
node has at least one exec input.

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

## VAR: variables

`VAR-1` A variable MUST carry an ID, a name, a pin type, a type name, a
container, an authored default value, and a category string for grouping in the
editor.

`VAR-2` A variable's scope MUST be either blueprint-wide or local to one
function or macro graph. A local variable MUST NOT be visible outside its
graph.

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

`VAR-8` Variable storage MUST live on the run, not on the blueprint. Starting a
run MUST initialize each variable to its authored default. Two runs MUST NOT
share variable storage.

`VAR-9` A container variable MUST support the operations of its container:
array indexing, length, add, insert, remove, clear, and contains; set add,
remove, contains, and length; map insert, remove, find, keys, values, and
length.

`VAR-10` The editor MUST offer promoting a pin to a variable: it creates a
variable typed from the pin, wires an appropriate Get or Set node, and names it
from the pin.

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

## EVT: events

`EVT-1` An event node is an entry point: it has no exec input, at least one
exec output, and zero or more data outputs that carry the event's parameters.

`EVT-2` An event's name MUST be authored through the editor. It is stored in
the node's reference name field.

`EVT-3` Event names MUST be unique within a blueprint. A duplicate MUST be a
compile error, and dispatching a duplicated name MUST fail rather than pick
one.

`EVT-4` A custom event's parameter list MUST be authored through the editor:
add, remove, rename, retype, reorder, and set a default per parameter. Each
parameter is a data output pin on the event node, so this is an instance-pin
operation under `PIN-14`.

`EVT-5` Reordering or retyping a parameter MUST update the matching call nodes
and MUST invalidate only the edges that are no longer type-compatible, each
reported by `CMPL`.

`EVT-6` A Call Event node MUST invoke a custom event by ID, taking that event's
parameters as data inputs, and MUST have an exec input and an exec output.

`EVT-7` Dispatching an event by name MUST fail with a distinct, inspectable
error for unknown name and for duplicate name.

`EVT-8` A lifecycle event set MUST exist and MUST be host-triggered rather than
self-firing: Begin, fired once when the host starts a blueprint, and Tick,
fired per host frame with a delta-time parameter. Core MUST NOT contain a frame
loop.

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

`FN-8` A macro MAY contain latent nodes and MAY have several exec inputs and
outputs. A macro MUST NOT have local variables.

`FN-9` Macro expansion MUST be recursion-checked at compile time, and a
recursive macro MUST be a compile error.

`FN-10` A macro's wildcard pins MUST resolve per call site.

`FN-11` The editor MUST offer collapsing a selection of nodes into a new
function or macro, and expanding a call back into its nodes. Collapsing MUST
derive the signature from the edges crossing the selection boundary.

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

`EXEC-8` While a run is in any non-idle state, structural mutation of its
blueprint MUST be rejected: adding or removing a node, adding or removing an
edge, changing a pin's name, type, or count, renaming or retyping a variable,
renaming an event, and creating or deleting a graph. Editing an authored
default on a pin that no edge drives MAY be permitted, and MUST NOT affect the
live run.

`EXEC-9` A run MUST record the blueprint's topology revision at start, and MUST
fail with a distinct error if it detects the topology changed under it.

`EXEC-10` Suspension MUST use a host-supplied monotonic clock. Core MUST NOT
call any wall-clock or sleep function.

`EXEC-11` A latent node that has suspended MUST resume at the point it
suspended, with its frame and its loop state intact.

`EXEC-12` A host callback channel MUST be reachable from a node's execution
callback, so side-effecting nodes such as Print String deliver output to the
host rather than to stdout.

`EXEC-13` Run-scoped per-node scratch storage MUST exist, keyed by node ID, so
stateful nodes such as DoOnce, Gate, MultiGate, and FlipFlop keep state across
activations within one run and start clean in a new run.

`EXEC-14` A node MUST tolerate being activated again while an earlier
activation of it is still on the continuation stack, which is what a loop body
feeding back into its own subtree produces. Its run-scoped scratch is shared
across those activations; its data memo is not, per `EXEC-3`.

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

`RUN-6` A run MUST be stoppable from any non-idle state, ending in idle without
running further nodes. Stop MUST release run storage, discard variables, clear
per-node scratch, memo, cycle, error, and watch state, release the mutation
guard, and return pin displays to preview values.

`RUN-7` WITHDRAWN. Reset is not a separate lifecycle operation. Stop carries
its cleanup semantics under RUN-6, matching Unreal's Play/Stop session model.

`RUN-8` Freeing or stopping a run MUST leave the blueprint mutable. A run that
ends while its mutation guard is held MUST release it.

`RUN-9` A run MUST expose its step count, its current node, its call stack as
an ordered list of graph and node IDs, and its structured error.

`RUN-10` Advancing time MUST be host-driven: the host supplies a monotonic time
and asks the run to continue.

`RUN-11` Two runs of the same blueprint MUST be independently valid: separate
variable storage, separate scratch, separate error state, separate step counts.

`RUN-12` A run MUST refuse to start against a blueprint with compile errors,
reporting the first blocking diagnostic.

`RUN-13` Run state MUST NOT be serialized. Saving while a run is non-idle MUST
write authored data only, and a suspended latent node MUST NOT survive a save
and load.

`RUN-14` Loading, undoing, or authoring an edit while a run is non-idle MUST
stop and release that run before replacing or mutating node identities. The
editor MUST perform this teardown at one authoring-edit choke point, because a
flow_run holds raw node pointers.

## CAT: node catalog

`CAT-1` The catalog MUST expose every node in this section with the listed pin
shape, and the editor MUST make each one's configuration authorable.

Flow control:

`CAT-2` Branch. Exec in, bool condition, True and False exec outs.

`CAT-3` Sequence. Exec in, N exec outs run in order, N user-extensible from 2
with no fixed maximum.

`CAT-4` ForLoop. Exec in, first and last index, Body exec out, Index data out,
Completed exec out, inclusive forward range.

`CAT-5` ForLoopWithBreak. ForLoop plus a Break exec in that stops iteration and
continues to Completed.

`CAT-6` WhileLoop. Exec in, bool condition re-evaluated per iteration, Body and
Completed exec outs.

`CAT-7` ForEachLoop. Exec in, container in as a wildcard, Body exec out,
element and index data outs, Completed exec out. A break variant MUST exist.

`CAT-8` DoOnce. Exec in, Reset exec in, Completed exec out, bool "start
closed". State is run-scoped per node.

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

`CAT-17` IsValid. Takes a value, gives Is Valid and Is Not Valid exec outs.

Latent:

`CAT-18` Delay. Exec in, duration, Completed exec out. A second activation
while waiting MUST be ignored.

`CAT-19` RetriggerableDelay. As `CAT-18`, except a second activation restarts
the timer.

Utility:

`CAT-20` Print String. Exec in, string in, exec out. Output goes to the host
callback channel under `EXEC-12`.

`CAT-21` Typed literal nodes for every primitive type, plus struct and enum
literals. Pure, value authored in the editor.

`CAT-22` Make and Break for every reflected struct type. Pure.

`CAT-23` Call nodes for every reflected function. Pure unless the descriptor
declares otherwise.

`CAT-24` Container operation nodes covering `VAR-9`.

`CAT-25` Arithmetic, comparison, and boolean nodes for every numeric primitive,
plus string concatenation and comparison. Pure.

`CAT-26` Explicit conversion nodes for every conversion permitted by `TYPE-7`.

`CAT-27` Every catalog entry MUST declare a category path used for grouping in
the spawn menu, and a search keyword list.

## CMPL: compile and validation

`CMPL-1` Compiling MUST be an explicit operation over one blueprint that
produces an ordered diagnostic list and never modifies the authored graph.

`CMPL-2` A diagnostic MUST carry a severity of error or warning, the graph ID,
the node ID, the pin ID where applicable, a stable rule code, and a message.

`CMPL-3` Errors MUST block a run under `RUN-12`. Warnings MUST NOT.

`CMPL-4` Error: a required exec input with no incoming edge on a node that can
only run from an entry path.

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

`FMT-14` Blueprint metadata is exactly: the schema version, the blueprint name,
the ID counter, and a free-form description. Editor-only transient state MUST
NOT be written, per `RUN-13`.

`FMT-15` Copy MUST place the subset document from `FMT-10` on the host text
clipboard, and paste MUST accept a subset document from it, so a selection
transfers between editor instances as text.

## UI: editor surfaces

`UI-1` Every value the file format can express MUST be authorable in the
editor. No feature ships whose configuration requires hand-editing JSON. This
is the binding constraint on every other `UI` requirement.

`UI-2` The editor MUST present a canvas, a toolbar, a blueprint tree, a details
panel, a compiler results panel, and an output log.

`UI-3` The blueprint tree MUST list graphs by kind, events, functions, macros,
and variables, and MUST support creating, renaming, deleting, and opening each.

`UI-4` The details panel MUST be context-sensitive on the current selection and
MUST expose every authorable property of it: for a node, its title override,
comment, reference target, and per-pin name, type, container, and default; for
an event or function, its parameter list with add, remove, rename, retype,
reorder, and default; for a variable, its name, type, container, default, and
category.

`UI-5` The details panel MUST be the single place a per-node property is
edited. A property reachable only through a context menu or only through the
canvas is a defect.

`UI-6` The toolbar MUST offer Compile, Run, Pause, Resume, Step, and Stop,
plus an entry-point selector listing the blueprint's events, and MUST show the
current run state and step count.

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
be togglable from the pin's context menu and from the details panel.

`UI-12` A breakpoint MUST be togglable from the node's context menu and by a
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

`UI-17` Undo and redo MUST cover every authoring operation in this document,
including pin edits, parameter list edits, variable edits, and graph creation
and deletion.

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

`UI-29` Find in blueprint MUST exist: one search across every graph over node
titles, descriptor names, pin names, comments, literal values, and variable
names, with navigable results.

`UI-30` Find references MUST exist for a variable, an event, a function, and a
macro, listing every referencing node with navigation, so deleting one is an
informed decision.

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

`UI-36` An authoring action during a live run MUST report that it stops the
run first, per `UI-7` and `RUN-14`. It MUST NOT be presented as a rejected edit.

`UI-37` Align, distribute, straighten connection, and zoom to fit SHOULD exist.

`UI-38` Holding a bound key while clicking the canvas SHOULD spawn that node
type directly, bypassing the spawn menu for the most-used nodes.

`UI-39` Pin values MUST have one canonical textual rendering shared by pin
displays, the details panel, watches, and the output log, so the same value
never renders two ways.

`UI-40` The output log MUST offer a Clear action that removes all retained
entries without changing the authored graph or the undo history.

`UI-41` Every authoring mutation MUST pass through one editor choke point that
stops and releases a non-idle run before the mutation. This includes graph
edits, value edits, undo, redo, and load.

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

`UI-51` Dragging any node in a multi-selection MUST move every selected node
together and preserve their relative positions.

## REFL: reflection

`REFL-1` Reflection MUST be a build-time tool over annotated C sources, using
libclang, emitting a generated C registry. See `docs/adr/0005`.

`REFL-2` Annotations MUST use `__attribute__((annotate(...)))`, so annotated
headers compile under both GCC and Clang.

`REFL-3` An annotated struct MUST emit a `type_desc`, a Make descriptor, and a
Break descriptor.

`REFL-4` An annotated function MUST emit a call descriptor whose inputs are its
parameters and whose output is its return value.

`REFL-5` An annotated enum MUST emit a labelled enum descriptor under `TYPE-4`.

`REFL-6` Generated struct field offsets and sizes MUST come from the real
compiler through `offsetof` and `sizeof` in the emitted C, not from libclang's
layout numbers.

`REFL-7` The expansion policy from annotation to descriptors lives in the tool,
not in the runtime, so an alternative policy can emit different node sets from
the same type descriptors.

`REFL-8` The tool MUST fail the build with a located message on an
unsupported annotated declaration, rather than silently omitting it.

## Implementation layout

Where each part of this document lives. `SCOPE-1` is enforced by the dependency
edges: only `editor` and `app` link the host.

`core/` holds the node descriptor, pin model, type descriptors, graph storage,
data evaluation, imperative execution, built-in node descriptors, the compile
pass, and serialization. Depends on libc and vendored yyjson only.

`reflect/` holds the build-time libclang tool. Depends on libclang, and nothing
at runtime depends on it.

`generated/` holds the tool's output, compiled into `core`.

`editor/` holds the dcimgui blueprint widget, its panels, and the preview
evaluation view. Depends on `core` and the host.

`app/` holds the host frame loop. See `docs/adr/0009`.

`demo/` holds annotated example types and functions that exercise reflection.

`test/` holds `self_check.c`, which links `core` and `demo` and never creates a
window, per `SCOPE-2`.

`third_party/` holds vendored dependencies: yyjson for JSON, ckit for dynamic
arrays, hash maps, and string interning.

## Deferred

Recorded so they are not rediscovered as gaps:

- Blueprint classes, inheritance, interfaces, and class defaults.
- ECS binding: entity and component Get, Set, Add, Remove, and Query nodes.
- Event dispatchers and delegate binding.
- Timeline nodes and curve authoring.
- Replication and network authority.
- Bytecode compilation of graphs. `EXEC` describes an interpreter; a compiler
  is an optimisation, not a correctness requirement.
- Multi-return and out-parameter reflected functions beyond `PIN-15`.
- Mutable string values. `PIN_STR` is an immutable arena- or blueprint-owned
  string.
- Dirty tracking for data evaluation, pending a profile that shows full
  recompute is the bottleneck.
