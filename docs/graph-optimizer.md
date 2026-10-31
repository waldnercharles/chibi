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

`graph/authoring/optimize` owns the shared analysis and mutation passes. It checks
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

## Authoring

Studio's graph menu (⋯) offers Optimize and Math to Expressions. These actions
use editor document history. The former `optimize_graph` agent mutation request
is retired; agents edit saved `.chibi`/KDL source and use native inspection and
validation as described in the [asset workflow](runtime-authoring.md).

The optimizer report includes `folded_inputs`, `removed_nodes`,
`created_aliases`, `changed_edges`, and `skipped` entries with node/pin IDs and
stable reasons. Math to Expressions first collapses eligible math trees before
optimizing; computed inputs receive placeholder letters for the author to rename.

## Historical validation

The results and commands below describe the earlier implementation revision.
The MCP transport and its mutation checks are retired; choose current checks
using the [testing instructions](testing.md).


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

The separate output-promotion regression also exposed reversed Get/Set wiring.
Explicit output promotion now connects the output to a Set Variable input; the
automatic optimizer does not use this API.

The optimizer (`archive/src/graph/authoring/optimize*`) and its
`--graph-optimize` and `--ui-optimizer` checks are archived; nothing builds or
runs them.

The Set Variable and explicit Self follow-up passed a native Release build, 230
focused optimizer and binding checks, rendered editor interaction, and MCP checks.
The changed authoring modules also compile for WebAssembly. The complete suite
currently has one unrelated catalog failure: `content/prototypes/ActionBinding.json`
is present but absent from the shipped-content allowlist.

## Authored-example dry runs

The following graph copies were analyzed without modifying content assets.
The selection covers Get_Encounter, Get_Pickup, Delta Time,
Call Pure Function, and Spawn Prototype. Counts reflect the snapshot used during
implementation; existing source debug flags and routes can prevent rewrites.

| Asset | Graph | Folded inputs | Removed nodes | Locals |
| --- | --- | ---: | ---: | ---: |
| ArenaDropBehavior | Spawn Surface Pulse | 0 | 0 | 0 |
| ArenaDropBehavior | Spawn Source Beam | 0 | 0 | 1 |
| ArenaImpactFeedback | Spawn Surface Pulse | 0 | 0 | 0 |
| ArenaImpactFeedback | Prepare Enemy Experience | 0 | 0 | 1 |
| ArenaPickupBehavior | Events | 2 | 1 | 0 |
| ArenaPickupBehavior | Wait and Pulse | 4 | 2 | 0 |
| ArenaPickupBehavior | Collect Along Spiral | 7 | 1 | 2 |
| ArenaShardBehavior | Events | 5 | 3 | 0 |
| ArenaShardBehavior | Advance Shard Motion | 12 | 8 | 1 |
| ArenaShipMotionBehavior | Events | 11 | 5 | 0 |
| ArenaShipMotionBehavior | Advance Face Roll | 0 | 0 | 0 |
| ArenaShipMotionBehavior | Compute Aim Basis | 0 | 0 | 0 |
| ArenaShipMotionBehavior | Apply Ship Pose | 0 | 0 | 0 |
| ArenaWaveBehavior | Events | 1 | 1 | 0 |
| ArenaWaveBehavior | Refresh Encounter | 8 | 4 | 0 |
| ArenaWaveBehavior | Advance Wave | 12 | 5 | 0 |
| ArenaWaveBehavior | Spawn Telegraph | 0 | 0 | 1 |
| ArenaWaveBehavior | Formation Position | 1 | 1 | 3 |
| ArenaWaveBehavior | Row Formation | 0 | 0 | 0 |
| ArenaWaveBehavior | Side Formation | 0 | 0 | 0 |
| ArenaWaveBehavior | Ellipse Formation | 0 | 0 | 0 |
| CombatFeedback | Update Hit Flash | 5 | 4 | 0 |
| EnemyBehavior | Events | 4 | 3 | 0 |
| HealthBehavior | Apply Damage | 1 | 1 | 2 |
| PlayerContactBehavior | Events | 4 | 4 | 0 |
| ProjectileBehavior | Events | 4 | 3 | 0 |
| ProjectileBehavior | Face Direction | 0 | 0 | 0 |
| SpawnTelegraphBehavior | Events | 4 | 4 | 0 |
| SpawnTelegraphBehavior | Arrive At Marker | 0 | 0 | 0 |
| SurfacePulseBehavior | Events | 6 | 6 | 0 |
| TwinStickInputBehavior | Events | 3 | 3 | 0 |
| TwinStickWeaponBehavior | Events | 8 | 5 | 0 |
| TwinStickWeaponBehavior | Fire Ready Projectiles | 2 | 2 | 0 |
| WavePulseBehavior | Events | 2 | 2 | 1 |
