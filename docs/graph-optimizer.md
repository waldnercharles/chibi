# Graph readability optimizer

Optimize graph folds exact component getters, including a Get Component that
reads Self, variable reads, proven Set Variable outputs, Delta Time, and Self
inputs. Fanout of three or more uses becomes a
graph local when it cannot be folded directly. Locals are named indirections to
existing outputs; they never add persistent variables or execution steps.

The source keeps a `let Name` marker. Click a consumer chip to go to its source
or expand all uses back into wires. Optimization and measured arrangement share
one Undo entry. A no-op does not change the asset, ID counter, layout, or history.
The graph action menu retains the applied counts and skipped-candidate summary.

## Representation and proof

`src/graph/authoring/optimize*.c` own the shared analysis and mutation passes. It checks
an entire source fanout before applying that group. Component identities are
emitted by reflection as descriptor metadata and checked against the registered
descriptor, access contract, aggregate type, and exposed field mapping. Binding
reads keep the runtime's data-demand timing and fresh memo per EXEC activation.
Setting pins, unresolved wildcard types, conflicting dormant bindings, debug
state, and unsupported conversions leave the candidate untouched.

A Set Variable output is the value captured by that assignment. It becomes a
variable input only when every eventual execution consumer is reached after the
assignment, with no intervening write to that variable, suspension, loop boundary,
or unknown effect. The proof follows pure data dependencies and all incoming
execution paths. It can cross exclusive branches and assignments to other
variables. The Set node and execution wires remain. Existing locals on a proven
Set output also become variable inputs, regardless of the fanout threshold.

Self uses the destination's default-Self behavior when equivalent. Explicit
entity inputs, conversions, and dormant values use ordinary inline Self sources.
Each consumer owns one Self node and retains its original data edge semantics;
the editor presents the source inside the input. Existing Self locals can also
be expanded into these inputs. Debug state and custom wire routes are preserved.

Each local has a graph-owned ID, name, source node ID, and source output pin ID.
Its consumers use `PIN_BINDING_LOCAL` and retain their original logical DATA
edges. Those edges retain the conversion descriptor, serialized by registry name.
The editor hides the wires; the existing runtime, flow observer, effect analysis,
cycle checks, system register lowering, shader scope cache, and arrangement all
follow the same dependencies. This avoids a second dependency graph or a new
runtime value store. `graph_node_is_inline` still describes only a single owned
consumer, and a local source never becomes inline. A local input is a value, so
a pure consumer of a local can still fold into its own consumer.

Loading validates local identity and dependency consistency. Clipboard copies
remap IDs and resolve name collisions. A partial copy discards references to
sources outside the selection, as ordinary crossing wires do. Extraction keeps
internal locals and turns crossing dependencies into function ports. Deleting a
source clears its local uses. Expanding a local preserves its exact dependencies
and conversions.

The cleanup passes remove transparent copies/reroutes, replace Break nodes with
exposed aggregate fields, and remove disconnected pure subgraphs in behavior
execution graphs. Constant evaluation is limited to explicitly foldable scalar
math descriptors with literal inputs and finite results. Contextual reads,
randomness, images, and function calls are excluded. System and shader graphs
use aliases without gaining unsupported behavior bindings.

## Automation

The MCP's `optimizeGraph` (native operation `optimize_graph`) uses the same
optimizer and report. It defaults to `dry_run: true`,
accepts `graph_name` or `graph_id`, and allows `fanout_threshold` from 2 through
64. Applying requires the current `expected_hash`. A request is bounded to 512
nodes and 4096 edges. Apply publishes the proposal atomically after the hash check;
no-op apply preserves the original bytes. MCP editing is outside editor Undo.

`math_expressions: true` first replaces each tree of three or more math nodes with
one expression (`behavior_expression_collapse_math`), then optimizes, so a value
used three or more times becomes a local. A tree grows through nodes whose only
use is a plain wire into more math, breadth first, up to about a line of formula;
a deeper operand becomes its own expression. Computed inputs get placeholder
letters (`a`, `b`, ...) for the author to rename; named sources keep their names.
Output is bit-identical. The report adds `created_expressions`. Studio's graph menu (⋯) offers
Optimize and Math to Expressions.

The report includes `folded_inputs`, `removed_nodes`, `created_aliases`,
`changed_edges`, and `skipped` entries with node/pin IDs and stable reasons.

## Validation

Focused model and runtime regressions cover multi-consumer component fields and
unwrapping, Variable/Delta Time/Self, unsafe group rejection, serialization and
clipboard remapping, missing sources, cycles, invalid conversions, extraction,
constant/projection cleanup, and exact Undo/Redo. Differential flow tests change
variables and time across branches, loops, and suspended delays, and check watch
and traversal provenance. System tests exercise generic bytecode, typed bytecode,
and SLJIT; shader tests compare output samples and lowered node counts. The native
UI test clicks Optimize, a local chip, source navigation, expansion, and Undo.
ArenaShard regressions verify both Set Age consumers and the explicit Destroy
Entity Self input through editor history, rendered input chips, and MCP dry-run,
apply, and no-op checks. Set-output differential tests cover intervening writes,
delays with external variable changes, both branch arms, calls, loops, sequence
boundaries, and paths that bypass the assignment.

Useful checks:

```sh
make build
make test
make test-fast TEST_ARGS=--graph-optimize
node tools/mcp/optimizer-smoke.mjs .
```
