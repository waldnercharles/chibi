# Behavior code language

This slice compiles into the existing typed behavior graph and runtime. The graph remains the executable artifact; there is no separate interpreter. `validate` resolves names against the registered engine components and event pins without writing files. `compile` lowers the source into regular graph nodes and runs the native graph validator before publication.

## Example

```c
behavior ProjectilePulse {
  state int[] marks = [1, 2, 1];
  state set<int> seen = [];

  on Tick(GameProjectile projectile) {
    for (mark in marks) {
      if (seen.contains(mark)) {
        projectile.age += event.dt;
      } else {
        seen.add(mark);
      }
    }
  }
}
```

`GameProjectile projectile` declares a reflected component context. Component values use qualified paths such as `projectile.age`. The compiler lowers each used member to a typed scalar input bound to that reflected component field, so the source keeps qualified paths while the graph uses the field-binding shape supported by Behavior Expression nodes. `event.dt` resolves to the Tick event's `DeltaSeconds` output. `self` is only accepted by the registered `destroy(self)` call.

The graph keeps deterministic internal input names such as `_bc_component_0_age` and `_bc_event_DeltaSeconds` for expressions and bindings. The editor shows their source-facing labels (`projectile.age` and `event.dt`) separately; `inspectGraph` reports each label with the original pin name and stable pin ID. Older or unlabeled pins fall back to their internal name.

## Names and lifetime

- Component types, fields, events, and payload pins come from the engine registry. Unknown names, duplicate component contexts, missing fields, and ambiguous bare fields fail before publication.
- Component aliases are local to one handler. A bare component field is allowed only when exactly one declared component has that field. Qualifying it is preferred. Bare `dt` is not a special name.
- `event` is read-only and only exposes fields registered for that event. Host aliases are explicit: Tick `dt`, Input `direction`, ContactBegin `other` and `point`. The display event `Contact Begin` has the source name `ContactBegin`.
- `state T[] name = [...]` and `state set<T> name = [...]` create behavior variables whose values persist between event invocations. Primitive collection defaults use scalar literals. String defaults decode `\n`, `\r`, `\t`, `\b`, `\f`, `\v`, `\0`, escaped quotes and backslashes, and `\uXXXX` or `\u{...}` Unicode escapes; unsupported escapes report `E_STRING_ESCAPE`. Primitive element types are `int`, `float`, `bool`, and `string`; entity sets use `entity` and must start empty so handles come from runtime event data.
- A state declaration is created on the first compile. Editing or removing its type or default through source is currently rejected before graph changes; existing graph variables remain available to the visual editor.
- `for (item in values) { ... }` iterates a typed state array in element order. Its binding is read-only and visible only in that loop body, including nested `if` blocks. It cannot shadow a component alias, state name, reserved word, or enclosing loop binding. Sets are deliberately not iterable in this slice.
- Set statements are `set.add(item);`, `set.remove(item);`, and `if (set.contains(item)) { ... }`. Their item may be the visible element of a typed array loop or a direct registered event payload field, and must match the set element type. Entity set items can come from handle payloads such as `event.other`. General Set Add and Set Remove graph nodes preserve snapshot semantics; removal preserves the insertion order of remaining entries. Set iteration order is not exposed.
- There are no implicit mutable locals. The first loop slice supports `for` over state arrays, not C-style ranges, `while`, `break`, or asynchronous control flow. Set methods accept only a loop element or direct event payload field, not arbitrary expressions. Array mutation is not part of this source slice.

## Graph behavior and errors

Arrays lower to the existing `ForEach` node and behavior variables. That node visits elements in array order and has snapshot semantics; graph edits remain ordinary editable nodes. Component bindings, event-output wires, set membership, and set updates remain visible in the generated graph. The source compiler gives each `set.add` a dedicated read of its state variable. When that read feeds only the matching Set Add, whose result feeds a write to the same variable, the native behavior compiler appends into the instance-owned set with geometric capacity. It copies the inserted value and values moved during capacity growth; duplicate values leave the set unchanged. Existing snapshots remain valid because the variable arena retains replaced storage until reset. The compiler falls back to the pure graph operation when another pin reads the set or a watched input would lose its value. Set Remove and other graph-authored Set Add patterns keep their snapshot behavior.

Code-to-graph and graph-to-code cover the structured forms above. The map records semantic variable targets as well as node IDs and types, so retargeting a source set operation recreates its Get/Set Variable nodes instead of keeping stale references. Graph input bindings must map back to a declared source component alias and a registered field; the exported source is resolved again before it is returned. An edit that changes source-owned node types or wires, changes a mapped variable reference in the graph, or adds an unsupported cross-link stops export/compile with an error before the source or map is rewritten. Extra graph content outside the mapped source-owned region remains outside the export. `// @node name` markers are optional and only needed when statement identities cannot be matched uniquely.

The current bridge keeps `.code` source, a `.map.json` identity sidecar, and the existing `.kdl` behavior graph. A future code-canonical behavior format can treat the typed graph as the compiler/editor representation and retain layout and node identities as editor metadata. It would need versioned migration that preserves state, identity, and unsupported graph content; other asset types are outside that direction. No destructive KDL migration is part of this slice.

`validate FILE` prints one JSON object. On failure its diagnostic includes a stable `code`, `message`, and source `span` (`start`, `end`, `line`, `column`). It checks parsing and schema/name resolution; expression typing and executable graph rules are confirmed by `compile` through the native graph compiler. Native build diagnostics retain their graph compiler rule code and are mapped back to the owning source statement where the operation can be identified.

Known diagnostic codes include `E_COMPONENT_ALIAS_REQUIRES_MEMBER`, `E_NAME_COLLISION_ALIAS_FIELD`, `E_NAME_AMBIGUOUS_COMPONENT_FIELD`, `E_COMPONENT_FIELD_NOT_FOUND`, `E_EVENT_FIELD_NOT_FOUND`, `E_ASSIGNMENT_READ_ONLY`, `E_NAME_UNRESOLVED`, `E_EVENT_UNRESOLVED`, `E_ARRAY_UNRESOLVED`, `E_LOOP_BINDING_READ_ONLY`, `E_COLLECTION_ELEMENT_TYPE`, `E_SET_UNRESOLVED`, `E_SET_ARGUMENT_UNSUPPORTED`, `E_SET_ARGUMENT_TYPE_UNSUPPORTED`, `E_SET_ELEMENT_TYPE`, and `E_STRING_ESCAPE`.

## Performance and authoring checks

The native collection probe sampled source-generated graphs at 16, 64, 128, and 256 items for 120 fixed ticks at `1/60` second. Each first tick inserted distinct values; later ticks exercised duplicate adds. The probe includes the `ForEach` snapshot on every tick, so each loop still copies one full input array per tick. Source compiles into the same graph and native backend used by visual graphs; there is no separate source interpreter, so a source-versus-graph runtime timing comparison is not meaningful. These measurements characterize the generated graph path in this environment and make no speed promise. Checksums, allocation counts, and frame-time tails are recorded in the standalone Library example report.

The validator resolves names through the registry APIs `describeComponents` and `searchNodes`; authors do not need to hard-code reflection IDs. The resolver/compiler tests, native graph compilation, a collection-loop simulation, and a deferred-destroy probe were executed. A future LSP can be a thin client of the same validator and diagnostic model.
