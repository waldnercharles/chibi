# Graph language: lessons from Unreal and Blender

Research for the behavior graph, shader graph and fields, 2026-09-23. It compares Unreal Engine 5 (Blueprints, the Material Editor, Niagara, PCG, RigVM) and Blender 5.x (Geometry, Shader and Compositor nodes) with an inventory of our own graph system. Editor interaction contracts are in [Node editor references](../editor-graph-references.md).

## Direction

A behavior graph describes what the author wants. The compiler and optimizer do the rest:
- they convert types implicitly,
- they fold, arrange and simplify,
- they refuse constructs that would fail at runtime.

Automation is a feature to lean into, not an escape hatch. Both references support this:
- Blueprint's compiler has almost no optimizer: it prunes unreachable nodes and does no constant folding or CSE. Its best-known traps are semantic, not visual.
- Blender's evaluator is lazy, folds constants, deduplicates and infers usage, but it leaves layout to add-ons.

We already sit between the two: pure values are memoized once per activation, the authoring optimizer folds sources into bindings and folds constants, and extraction and arrangement are automatic.

## Where we stand

These are strengths to keep:

- **Pure values evaluate once per activation.** Unreal re-evaluates a pure node for every consumer, which is its most-cited trap: a ForEach pulls its array about 2n+1 times.
- **The authoring optimizer** (`graph/authoring/optimize*.c`):
  - It folds getters, variables, delta time and Self into bindings, aliases fan-out into locals, and removes and constant-folds pure nodes.
  - It is exposed through MCP as a dry run.
- **The whole-function compiler:**
  - Its passes are query fusion, access reuse, value pruning, guarded fast paths and parallel proofs.
  - It emits PERF diagnostics.
- **Behavior Expressions** compile to ordinary graphs, the approach Blender's pending Expression node takes. Selections convert back into expressions.
- **Conversions ride on edges**, so they don't clutter the canvas.
- Latent work is allowed in functions, unlike Unreal.

The inventory found these problems:

1. **Three languages share one model but not one vocabulary.** Behavior, shader and field graphs each define Add, Clamp, Remap, Sin/Cos, Noise and the SDF primitives. Each dispatches differently: descriptor callbacks, "Shader X" name matching, and a field pointer table. There are three Selects with three meanings.
2. **Seven ways to decompose a struct:**
   - Break nodes
   - split pins
   - exposed fields
   - Set Members
   - expression member access
   - dotted bindings
   - System compact pins

   The optimizer converts between them, which is powerful but hard to predict.
3. **Catalog bloat.**
   - 914 of 1,156 descriptors are generated: 8 variants per component, 317 field setters, and Make/Break for every type.
   - Raw C names leak into titles (`game_move_transform`), and a libc enum (`idtype_t`) is reflected.
4. **Stringly-typed dispatch.** `Get_`/`Set_` prefixes, `Case:` pin names, category-string domain gating and name lookups in the expression builder.
5. **Game concepts in core.** Fire, Dash and Contact Begin are host events in `graph/document/events.h`. Beat Phase and Surface Influence are shader built-ins.
6. **Semantic gaps:**
   - no mutable function locals
   - global `rand()`
   - no string, UI or input nodes
   - no Map nodes
   - loops bounded only by the 10k step budget
7. **Five-plus IRs and two independent parallel-safety analyses.** This is compiler complexity, not user-facing, but it multiplies the cost of every new node.

## Proposals

### Simplify the language

- **One node vocabulary with per-backend lowering.**
  - Math, compare, select, noise and SDF nodes are defined once, each with behavior, GLSL and field lowerings. This is Blender's "one definition, many backends": `build_multi_function` plus `gpu_fn`.
  - Shader and field graphs then offer the same nodes as behavior graphs, and Behavior Expressions work in all three.
  - Done at the expression layer (2026-09-25): one function table names each graph kind's node, so expressions share spelling, arity and argument order everywhere, and shader and field math converts to expressions. The three node sets remain.
- **Structure types for data.**
  - Sockets are Single, Field or Dynamic, later also List and Grid. They have static shapes, and inference runs over a small lattice where Single wins over Field.
  - A field wired into a single-value input is refused at connect time with the reason.
  - Math nodes are Dynamic: one Add serves constants, per-entity fields and per-cell grids.
  - This is Blender's 5.0 design, including the lessons it learned the hard way.
- **Two ways to reach into a struct.**
  - Split pins on the node, and member access in expressions.
  - Make, Break, exposed fields and field setters become presentations of split pins, not separate catalog entries.
  - The optimizer's forms stay, but the editor shows one.
- **A curated catalog.**
  - Reflected component access becomes three parameterized nodes, **Get**, **Set** and **Has** component. Each has a component picker and split pins, and the entity defaults to Self.
  - This replaces 456 whole-component variants and 317 setters in search. Search still finds "Set Velocity" by keyword.
  - Generated descriptors stay as the lowering targets.
- **One Real type.**
  - Authors see "Real"; the compiler picks single or double precision and inserts widening silently, as Unreal 5 does for floats.
  - The Real/Integer comparison pairs merge into generic comparisons.
- **Input actions as data.**
  - Fire and Dash leave core. Input action assets fire events with Started, Triggered and Completed phases and a typed value (Unreal Enhanced Input).
  - Hold, tap and chord logic live on the action, not in graphs.

### Legibility

A graph should read like the sentence the author had in mind. Presentation never changes evaluation.

Model and arrange work, each its own PR:
- **Inline by default.** Every pure source that qualifies folds into its consumer's pin row. Qualifying sources are getters, constants, Self, field reads, single-input conversions and small pure trees shown as one expression chip. A per-node choice of auto, always or expanded replaces the `inline_source` bool, and arrange skips inline nodes.
- **Routing styles.** Arrange routes wires as curved, orthogonal (track-assigned channels with rounded corners, like Electronic Nodes' Manhattan) or straight. It bundles fan-outs into a trunk and scores each style as it is drawn.
- **Event bands.** Event chains stack in a fixed order (Begin, Tick, input, custom), so a graph reads top to bottom by event.
- **Long wires become names.** Arrange reports wires that span three or more columns or cross many others, and the editor offers to turn each into a local (a named reroute).
- **Component access in three nodes.** Reflected components get Get, Set and Has nodes with split pins (see "Simplify the language").

Editor work, in the RmlUi graph editor:
- **Follow the selection.** Selecting or hovering a node highlights its wires and dims the rest.
- **Wire weight.** Exec wires are thick and white; data wires are thin and take their type's color.
- **Conversion chips** on wires that convert.
- **Usage graying.** Nodes that no event reaches, or whose outputs nothing reads, draw at half opacity.
- **Level of detail.** Zoomed out, pins and values hide and titles grow to stay readable. Comment frame titles always stay readable.
- **Routing style as a setting,** drawing the geometry arrange returns.

### Conversions

Edges already carry the conversion. Decide which ones are automatic and show them as chips on the wire:

| Kind | Examples | Behavior |
| --- | --- | --- |
| Lossless | int → int64 → real, f32 → f64, enum → int, scalar → vector splat, single-field struct unwrap | Automatic, no chip |
| Lossy or interpretive | real → int (truncate), vector → scalar, bool ↔ number | Automatic with a chip; the chip offers "make explicit" as Truncate, Round, Floor or Ceil, or a component pick. This is Blender 5.2's Implicit Conversion node. |
| Anything → text | numbers, names, entities | Automatic only into string inputs, so Format Text and Print |
| Forbidden | vector ↔ rotation as Euler, matrix → rotation (drops translation), int → bool as `> 0` | Refused with a reason and a suggested node (Blender's foot-guns) |

Wildcards must resolve when connected; an undetermined type is refused at connect time instead of waiting for compile (Unreal's CMPL-12 equivalent).

### Safety rails in the compiler

- **Only the chosen branch runs.**
  - Select and Switch evaluate only the chosen input, as RigVM's lazy pins do and Blender's Switch does for single values.
  - Unreal evaluates every Select option; Blender evaluates both branches of a field switch.
- **Dominance for impure outputs.** Reading the output of an exec node that may not have run on every path is an error, not Unreal's "pruned, read as default" warning. The Set Variable proof in `optimize_set.c` has the machinery.
- **Total math.**
  - Division, log, sqrt, pow and modulo are safe (Cycles SVM).
  - An out-of-range index returns the default or clamps.
  - Each case is counted and shown in the inspector instead of stopping the game.
- **Optional entities.**
  - Entity inputs that may be invalid need a validated form or a Not Valid path. This removes Unreal's "Accessed None" class of bugs.
  - Capability checks (Has Component with Found/Not Found) replace casts.
- **Visible loop budgets.** Each loop reports its budget at its own node, not through a shared counter (Unreal's 1,000,000 global). A lint flags While loops whose condition reads nothing the body writes.
- **Usage inference.**
  - Inputs that cannot affect a result are grayed out, e.g. behind a constant condition or an inactive menu branch, and inputs controlled by a menu can hide (Blender 4.4/4.5).
  - The same analysis prunes work at runtime.
- **Lifetimes.** Timers, subscriptions and async tasks belong to their entity and are cancelled when it is destroyed.
- **Warnings with IDs.** Each warning has an ID and a source link to its node and pin. Projects can suppress a warning or promote it to an error.
- **Fan-out lint.** A pure node with randomness or an expensive query that feeds several consumers suggests "promote to local", since memoization makes the value shared.

### Nodes to add

Ranked by value for gameplay, with sources:

1. **Timers with handles** (Unreal): Set, Clear, Pause, Remaining; looping with an initial delay. Timers take a graph reference, never a function-name string.
2. **Tween / Timeline** (Unreal): curve tracks with Play/Reverse/Stop/Set Time and Update/Finished outputs, on a shared scheduler rather than a ticking component each.
3. **Event subscription** (Unreal dispatchers): Bind, Unbind and Call, with automatic unbinding when the subscriber is destroyed.
4. **Input action events with phases** (Unreal Enhanced Input), replacing host Fire and Dash.
5. **Interp To and Spring** (Unreal): frame-rate-independent Interp To and Interp To Constant for real, vector and angle values. We already have a damped spring.
6. **Designer math** (Unreal and Blender): Map Range (clamped and stepped), Lerp, Ease, Snap, Wrap, Ping-Pong, Smooth Min/Max, Float Curve.
7. **Deterministic randomness** (Unreal streams, Blender Random Value): seeded streams, plus Random by ID so each entity's value is stable across spawns and reorders.
8. **Tags** (Unreal Gameplay Tags): hierarchical tags with Has, Has Any/All, Query and Switch on Tag. They also serve field and query filters.
9. **Container helpers:**
   - Map nodes.
   - Filter, Sort By and Map with an inline predicate, which Unreal lacks.
   - Min/Max By.
   - Lists as first-class values (Blender 5.2).
10. **Validity:** Is Valid in pure and exec forms, and Validated Get as a view of a Get (Unreal).
11. **Data rows** (Unreal Data Tables): Get Row with Found/Not Found and a typed row.
12. **Zones** (Blender):
    - a **Repeat** zone with iteration state carried between passes;
    - a **Tick** (simulation) zone with explicit state items, delta time and Skip.
    
    Both are compiled as real loops.
13. **Capture** (Blender Capture Attribute): snapshot a field before later writes change it.
14. **Callbacks** (Blender closures): strategy hooks such as on-hit, targeting and steering that reusable abilities expose. They use typed signatures with stable IDs, not Blender's name matching.
15. **Neighborhood queries and group reductions** (Blender): Sample Nearest, Index of Nearest, Accumulate, and Average/Min/Max by group. Find Nearest Entity and Raycast exist already.
16. **Warning and Viewer** (Blender): author-defined validation messages with severity, and inspection of any value while the game runs.
17. **Rotation and transform types** (Blender): a rotation socket with explicit conversions, Align to Vector, and Rotate Rotation.
18. **Strings:** Join, Split, Contains, Replace, Slice and Format.
19. **Development-only nodes** (Unreal): Print, Draw Debug shapes and asserts, stripped from release builds.
20. **Persistence completion:** async save/load slots on top of the existing save nodes.

### Editor tools

These live in the RmlUi graph editor ([contract](../editor-graph-references.md)). They are listed here for what they need from the model:

- **Format as you go** (Blueprint Assist, Blender Node Arrange). Arrange the selection, a tree or all events, lock nodes, and stack parameters beside their consumer. This consumes the arrange work in progress in the `vigilant-mclean` worktree; the editor calls arrange and never forks it.
- **Link-drag-search with conversions** (Blender, Unreal): results are filtered by type, include operations such as "Math › Add", and auto-wire.
- **Insert on wire, delete and reconnect, swap node** (Blender, Unreal, Node-RED). Swapping keeps wires by name and type.
- **Live optimizer suggestions.** Optimize becomes a set of quiet suggestions on the canvas (fold this getter, promote this fan-out) rather than only a command. Each can be applied as one undo step.
- **Inspection:**
  - pin tooltips with last values;
  - a viewer that takes any socket;
  - watches and breakpoints;
  - a timings overlay with field cost attributed to its consumer (Blender).
- **Link errors that explain and fix.** Every refused link says why, and offers the conversion, capture or validation node that would make it legal.
- **Project-wide Find References** and named reroutes (portals). Blender never shipped portals; Unreal has them only in materials.

## What not to copy

**From Unreal:**
- pure re-evaluation per consumer
- reading defaults from exec nodes that never ran
- Cast To chains that hard-load assets
- timers looked up by function-name string
- latent nodes banned in functions
- macros pasted at each use
- a global runaway-loop counter
- Accessed None found only at runtime
- Get (a copy) vs Get (a ref) on arrays of structs
- promotable operators defaulting to vector × vector
- a search index that has to be built by hand

**From Blender:**
- field/single mistakes discovered only after connecting
- the same field giving different values per consumer
- switches that evaluate both branches for fields
- surprising implicit conversions (the table above)
- closures and bundles matched by name with manual Sync buttons
- only executed nodes being inspectable
- node semantics changing between versions without versioning
- no built-in auto-layout
- no named reroutes

## Coordination

- **`asset-storage-format` (merged, PR #93):** authored assets are YAML, and MCP tool schemas are trimmed.
  - New asset fields can land now: prototype icons and colors, node versioning, tags, input actions.
  - The editor must not assume `.json`; scene titles already strip any extension.
- **`vigilant-mclean` (merged, PR #94):** made `graph/authoring/arrange.c` faster and routes long wires through lanes. Routing styles build on it in their own PR. The graph editor consumes arrange; it doesn't modify it.
- **This worktree:** the RmlUi editor. It uses the editor's mutation helpers and commands, and adds model-level commands (move, delete, disconnect, reroute) through `editor_command_apply`.

## Suggested order

1. **The RmlUi graph editor at parity**, built to the interaction contract, together with conversion chips, link-error reasons and the editor legibility work above. In parallel, three separate PRs: inline by default, routing styles with event bands, and component access in three nodes.
2. **Safety rails that need only compiler analysis:** lazy Select/Switch, dominance errors, usage graying, total math, warning IDs.
3. **Catalog curation:** parameterized component nodes, one Real type, a single struct presentation. The descriptors stay, so this is mostly search and presentation.
4. **One vocabulary across behavior, shader and field graphs,** after the YAML format lands, since node identities change.
5. **New nodes in the ranked order above,** each validated in a second scene per `AGENTS.md`.
