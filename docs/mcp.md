# Chibi MCP

The project exposes a local MCP server backed by the native `chibi_ctl`
controller. The controller uses the generated reflection registry and the
same asset loaders and compiler used by the editor.

## Start the server in a terminal

Run this from the project root:

```sh
make mcp
```

The target builds `chibi_ctl`, which handles headless automation and GPU previews
through its `--render` mode, then starts the Node.js adapter in the foreground.
The adapter uses stdin and stdout for
newline-delimited JSON-RPC. It does not open a port. The readiness message is
written to stderr so it does not corrupt the protocol stream.

Paste these lines into the running terminal to perform a live MCP handshake,
list the available tools, and ask for project status:

```json
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"manual-terminal","version":"0.1"}}}
{"jsonrpc":"2.0","method":"notifications/initialized","params":{}}
{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"getProjectStatus","arguments":{}}}
```

The server prints one response for each request that has an `id`. The
notification has no response. Press Ctrl-D to send EOF and stop the server, or
Ctrl-C to interrupt it.

For a repeatable one-shot check, send the same protocol messages through a
pipe. Build diagnostics stay on stderr while stdout contains JSON-RPC:

```sh
printf '%s\n' \
  '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"manual-terminal","version":"0.1"}}}' \
  '{"jsonrpc":"2.0","method":"notifications/initialized","params":{}}' \
  '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"getProjectStatus","arguments":{}}}' |
  make mcp
```

The checked-in `.mcp.json` starts the self-locating adapter for MCP hosts that
support project configuration. The public catalog and canonical workflow below
are shared by MCP hosts and Codex.

Codex can start the adapter automatically for this repository. Its project
scoped registration lives in `.codex/config.toml`; it is loaded when Codex is
started in this repository or one of its subdirectories. The launcher starts
the stdio adapter immediately, so hosts with short connect timeouts can attach
from a fresh worktree. When `build/chibi_ctl` is missing and no cached build
of the same sources exists, the adapter builds it in the background with output
on stderr, and the first tool call waits for it. A rebuilt controller replaces the running one on the
next request. Run
`make mcp-setup` to generate that machine-local registration. Restart Codex
or open a new task after adding or changing project MCP configuration.

## Asset files

Authored assets under `content/` are YAML. Documents omit what loading
reproduces: default values, pin fields that match the node type's template
pin, and `x`/`y` for nodes the editor arranges when it opens a graph. Pins are
keyed by name, and their IDs follow the node ID in order (inputs, then
outputs); an `id` appears only where the sequence breaks. Edges, locals and
event bindings name pins as `<node id>.<pin name>`, or
`<node id>.<pin>.<field>` for an exposed struct field. Wildcard pins (math
operators, reroutes, loops) omit the types their connections determine.
Expression functions store their `math_expression` source and `entry_id`;
loading rebuilds their nodes, mode and return node, and names them after their
source unless `name` says otherwise. An expression call's pins omit the groups
and flags its function implies. A call to a function in the same asset takes each pin's
`declaration_id` from the signature pin with its name. Enums are spelled by
label. A node whose pin names repeat lists its pins with explicit IDs instead. `build/chibi_ctl --project-root <root> --format`
rewrites every asset in canonical form, and `--check` lists assets that
differ. Prefer the build tools over hand edits; they keep IDs, hashes and
layout consistent.

## Default authoring workflow

For reproducible rendering measurements, `previewScene` accepts
`freeze_simulation` and an `input_timeline` spanning preparation and advancing
render frames. Results include initial/final runtime state and the active GPU
backend. Optional `metal_capture` and `capture_frame` record one warmed frame on
supported Macs. Without vendor tools, `pass_timing` waits for the GPU after each
render pass and reports average milliseconds per pass, and `capture_frames`
writes every measured frame to a raw RGBA8 file for judging motion and temporal
effects. Both serialize or read back each frame, so their frame totals are not
benchmarks. See [the GPU benchmark workflow](neon-gpu-benchmarks.md).

Use the MCP for authored content whenever it is available. Route requests by
domain:

- Graph work (behaviors, events, libraries, materials, shapes, shader
  libraries, fields, and Systems): search with `searchAssets` and
  `searchNodes`, inspect with `inspectGraph`, then use `buildGraph`. Check
  shaders with `previewShader`, fields with `previewField` (CPU evaluation, no
  GPU required), and Systems with `inspectSystem` and `getExecutionStatus`.
- Prototype or prefab work: use `describeComponents` when field details are
  unclear, then use `buildPrototype`.
- Scene or level composition: use `buildScene`.
- Studio's component tabs, prototype icons, and number ranges and units:
  edit `content/editor/Components.yaml` (kind `editor`), run `--format`, then
  call `validateAsset`.
- Game UI documents (`content/ui/<Name>.yaml`, kind `ui`, with `rml` body
  markup and optional `rcss` styles): read one with `inspectUi`, change it
  with `buildUi`, then call `validateAsset`; see
  [Author a UI document](#author-a-ui-document).

Before changing or removing a function, variable, event, component field or
shared asset, check what the edit affects with `findReferences`, and trace
who reaches a function or event with `callHierarchy`.

Call `validateAsset` after every published change. Use `previewShader`,
`previewScene`, `runScenario`, `captureRuntimeSnapshot`, or
`profileScenario` for evidence. Existing assets require the current
`expected_hash`; graph and optimizer dry runs report proposed changes without
saving them. Tool results include complete `structuredContent` and
`next_steps`.

- `buildGraph`: create or edit any graph asset; `query` replaces a System's ECS query.
- `previewField`: bake a bounded scalar field and return its PNG, range and hash.
- `buildPrototype`: create or edit reflected components, fields, and behavior attachments.
- `buildScene`: create or edit named entities, prototype instances, overrides, and attachments.
- `buildUi`: create or edit a ui asset's markup and styles as text edits that keep its formatting, validated with RmlUi before saving.
- `optimizeGraph`: analyze or publish proven graph folds and graph-local aliases.
- `renameSymbol`: rename an asset, event, function, parameter, variable or local and every reference to it across assets, dry run first.
- `deleteSymbol`: delete an asset, event definition, function, macro or variable nothing references, or force it and list what dangles.
- `previewShader`: compile and render a material or shape graph.
- `previewScene`: render a prototype or scene with its authored presentation.
- `searchAssets`: find catalog entries by kind, name, path, or description.
- `searchNodes`: list a domain's node types, or find nodes, configured component nodes, and their pins by words.
- `inspectGraph`: inspect graph content, diagnostics, and the current content hash; with `include_uses: true`, the asset, its functions and macros, and a behavior's variables carry `uses`, their project-wide use counts by role. Counting reads every asset, so it is off by default.
- `inspectUi`: read a ui asset's outline, props, events, slots, widgets, tokens, save keys, syntax errors and hash.
- `readAsset`: read one authored asset as parsed JSON.
- `validateAsset`: run native asset loading and compiler diagnostics; for a ui asset, its syntax errors and every warning RmlUi logs, with lines.
- `listAssets`: list deterministic asset paths, sizes, and hashes.
- `getAssetCatalog`: read semantic asset metadata, dependencies, diagnostics, and registry counts. Graph assets compile linked with the project's libraries and events, as Studio's Problems do.
- `inspectAssetDependencies`: inspect incoming or outgoing direct and transitive dependencies. An asset depends on the behaviors it attaches, the prototypes it places or spawns, the libraries it calls, the event definitions whose events it sends or handles, and the materials, shapes, fields, systems and ui documents its component fields, pin defaults and `<use ui>` widgets name.
- `findReferences`: find a symbol's definition and every use across the project by role, with counts; see [Navigate references](#navigate-references).
- `callHierarchy`: trace a function's or event's callers and callees as a tree.
- `getProjectStatus`: return project, asset, and reflection counts.
- `describeComponents`: inspect reflected component fields, stable keys, and native defaults.
- `inspectSystem`: inspect a typed system query, compiled graph, inferred effects, and diagnostics.
- `getExecutionStatus`: report the backend selected for a behavior or system.
- `runScenario`: run a bounded deterministic prototype or scene scenario; `behavior_failures` lists runs that stopped on an error, by node and message.
- `captureRuntimeSnapshot`: capture bounded ECS entities (32 unless `entity_limit` is set), reflected fields, behaviors, and timing.
- `profileScenario`: profile a bounded prototype or scene with warmup and timing samples.
- `recordScenario`: record bounded normalized inputs and deterministic frame hashes.
- `replayScenario`: replay a returned recording and report divergence.
- `describeRegistry`: inspect reflected types, services, and components; request `sections: ["nodes"]` for every node and pin.
- `describeContract`: inspect the private native contract and its public tool mappings.

## Navigate references

`findReferences` and `callHierarchy` read the project's reference index: every
place an asset names another asset or a symbol inside one, with its asset,
graph, node, pin and role. Both select a symbol by name:

- An asset: `{"asset": {"kind": "prototype", "name": "Bullet"}}`. Its uses
  are the references that depend on it: attachments, spawns, calls into a
  library, sends and handles of an event definition's event, and the assets
  component fields, pin defaults and `<use ui>` widgets name. A scene binds
  its page, the ui asset it shows its UI on: its own `ui:` (component `ui`),
  or for a scene that names none, `project.yaml`'s (component `project ui`).
- A function or macro: add `"graph": "Apply Damage"` or `"graph_id": 1001`
  to the asset.
- A member of the asset: `"variable": "Health"`; `"parameter": "Amount"`
  with the function's `graph`; or `"local": "speed"`, with `graph` or
  `graph_id` when several graphs have one by that name.
- An event: `{"event": "Damaged"}`. Host events (`Tick`, `Contact Begin`), ui
  `data-event` names and action names count too.
- What ui assets and stylesheets name: `{"ui_class": "card"}`,
  `{"token": "--primary"}` or `{"save_key": "theme"}`. The result lists the
  `definitions` (the selectors that style the class, the declarations of the
  custom property) and the `uses`, each by `path`, `line` and `column`, across
  `content/ui` and its stylesheets, and a `description`: what Studio's Text
  Editor shows when the pointer rests on the name.
- A component or one of its fields: `{"component": "GameHealth"}` or
  `{"component": "GameHealth", "field": "current"}`.

`findReferences` returns the `definition` (an asset is its own; a component's
is its reflected key and field type; host events have none), `counts` of
every use by role, and a page of `uses` (`limit`, default 50, and `offset`),
which `roles` can narrow. Each use names its role and asset, and either its
`graph_id`, `graph`, `node` and `pin` or its scene `object` and `component`
field. Pass `graph_id` and the asset to `inspectGraph` to read around a node:

```json
{"event": "Damage", "roles": ["send"]}
```

```json
{"symbol": {"kind": "event", "name": "Damage"},
 "definition": {"asset": {"kind": "event", "name": "Damage", "path": "content/events/Damage.yaml"}},
 "counts": {"send": 2, "handle": 1},
 "uses": [{"role": "send", "asset": {"kind": "behavior", "name": "PlayerContactBehavior"},
           "graph_id": 3, "graph": "Apply Contact Damage", "node": 107}, ...],
 "total": 2, "offset": 0, "count": 2, "has_more": false}
```

`callHierarchy` takes a function or macro, or an event, with `direction`
(`incoming`, `outgoing` or `both`, the default) and `depth` (1 to 6, default
2). Its `root` lists `callers` and `callees`. Items are:

- `function`: a function or macro, by asset, `graph_id` and `name`.
- `handler`: the run an Event node starts, by asset, `graph_id`, `node` and
  `event`. A call or send in an event graph belongs to each handler whose
  exec wires reach it.
- `event`: callers are its senders and callees its handlers; `host` marks
  engine events.
- `place`: a sender outside graphs, such as a ui document or an action
  binding.
- `graph`: the part of an event graph no handler runs.

`sites` lists the node IDs of the calls or sends joining an item to its
parent, and `cycle` marks an item that repeats an ancestor (it is not
expanded). `more` marks an item, the root included, with children left out:
past the depth, or past the tree's limit of 1,024 items. Trace again from
that item, or with a greater depth, to list them.

## Rename and delete across assets

`renameSymbol` and `deleteSymbol` change every asset a symbol reaches as one
change, as a language server's rename and safe delete do. They select the
symbol as `findReferences` does: an `asset` `{kind, name}`, with `graph` or
`graph_id` for a function or macro, or `variable`, `parameter` or `local`; or
an `event`. An event with a definition renames or deletes its
`content/events` asset too. A `component` or `field` is refused, since C
reflection names them, and so are scenes, since `project.yaml` names the start
scene by path. An asset's or event's new name must name a file, and a rename
that only changes an asset's case is refused, since file systems that ignore
case see the same file; rename it through another name.

Both default to `dry_run: true`. The dry run lists each asset it changes with
its `path`, current `hash`, `after_hash`, `new_name` and `new_path` when the
file is renamed, `deleted` when it goes, and `changes`: the references
rewritten there, placed as `findReferences` places a use (`role`, `graph_id`,
`graph`, `node`, `pin`, or `object` and `component`). `expected_hashes` maps
each path to its hash, and `mentions` lists values that still equal the old
name where the index records no reference, such as a prototype name a library
call passes on to `Spawn Prototype`; review and edit those with `buildGraph`.
To publish, call again with `dry_run: false` and the dry run's
`expected_hashes` unchanged. An asset that changed since, or a dry run that no
longer matches the assets the change reaches, is an `asset_conflict` and
publishes nothing:

```json
{"event": "Damage", "new_name": "Harm"}
```

```json
{"dry_run": true, "published": false,
 "assets": [{"kind": "behavior", "name": "HullDamage", "path": "content/behaviors/HullDamage.yaml",
             "hash": "1b670a0c93c49454", "after_hash": "2f787d311593ea17",
             "changes": [{"role": "handle", "graph_id": 0, "node": 1}]},
            {"kind": "event", "name": "Damage", "path": "content/events/Damage.yaml",
             "hash": "2944b52c078d8949", "new_name": "Harm", "new_path": "content/events/Harm.yaml",
             "after_hash": "7ff62620cd89e472", "changes": [{"role": "define"}]}, ...],
 "expected_hashes": {"content/behaviors/HullDamage.yaml": "1b670a0c93c49454", ...},
 "mentions": [], "dangling": []}
```

A rename rewrites event nodes, Send and Call Event, `data-event` and
`data-keys`, `GameActionBinding.action`, attachments, placed and spawned
prototypes, component fields and struct defaults such as a `GameShader`
value's material, mesh parts, `<use ui>` widgets, library calls'
`reference_asset` and `reference_name`, the pins of calls in other assets that
follow a library function's parameter, a scene's `ui:`, and the payload pins
an event definition's variables name. Renaming the page `project.yaml` names
is refused; point `ui:` there at another asset first. The files are written together; each changed
asset is then validated, and if one that validated before no longer does,
every file is restored and `invalid` lists its diagnostics.

`deleteSymbol` refuses while anything references the symbol and lists those
`references`. With `force` it deletes anyway and lists them as `dangling`. A
variable goes with its Get and Set nodes and bindings, as in Unreal, so its own
uses never dangle. Deleting an asset or an event definition removes its file.

## Author a UI document

A ui asset is YAML with an `rml` block of body markup and an optional `rcss`
block of styles (see [Player-facing capabilities](#player-facing-capabilities)
for bindings, widgets and pages). tree-sitter parses the markup with the HTML
grammar and the styles with the CSS grammar, so every result points at the
file's own lines.

`inspectUi` with `{"kind": "ui", "name": "PauseMenu"}` returns:

- `elements`: each element in document order with its `index`, `tag`,
  `attributes`, `line` and `parent` index; `rules`: each rule with its
  `selector`, `line`, `parent` rule and `declarations`.
- `props`: the names the bindings read that the document does not define,
  which hosts pass with `data-alias-<name>`; `widgets`: each `<use ui>` with
  the `props` it passes; `slots`; and `events`, as `searchNodes` lists them.
- `tokens` (the custom properties it reads), `declared_tokens`, `classes`,
  `styled_classes`, `save_keys`, `diagnostics` and the content `hash`.

`buildUi` applies `operations` in order, each to the text the one before it
left, and names an element by its `inspectUi` index or by a selector exactly
one element matches: a tag, `#id` and `.class` parts, as `#resume` or
`use.card`. Indexes after an insert, move or delete refer to the new text.

| Operation | Fields |
| --- | --- |
| `set_attribute`, `remove_attribute` | `element`, `name`, and `value` to set |
| `add_class`, `remove_class` | `element`, `class` |
| `set_style`, `remove_style` | `element`, `property`, and `value` to set: one declaration of the `style` attribute |
| `insert_element` | `markup`, and `target` with `position` `inside` (after its children, the default), `before` or `after`; without `target`, at the end of the markup |
| `move_element` | `element`, `target`, `position` |
| `delete_element` | `element` |
| `set_rml`, `set_rcss` | `text`: the block's whole value |

Edits keep the author's formatting: new lines take the indentation of the
lines around them, and a self-closing element opened to hold a child gains a
close tag. The result lists each operation's `changes` as file edits by line
and column. Like the other build tools it needs `expected_hash` unless it
`create`s the asset (whose operations must `set_rml`), and `dry_run: true`
returns the new `text` without saving. The edited asset is written in
canonical form and validated first, as `validateAsset` does; an invalid one
is refused with its `diagnostics`:

```json
{"kind": "ui", "name": "PauseMenu", "expected_hash": "eade8b8fb50dc048",
 "operations": [
   {"op": "insert_element", "target": "#panel", "markup": "<div class=\"hint\">Esc to resume</div>"},
   {"op": "set_style", "element": "#defeated", "property": "color", "value": "#ff8c96"}]}
```

`validateAsset` on a ui asset reports, as `rule_name`:

- `SYNTAX`: what the grammars reject, as an element left open (RML closes
  every element, as XML does) or a declaration missing its value, with its
  file `line`, `column`, `block` and `block_line`.
- `LOAD`: what the asset loader rejects, as an unknown key, a missing widget
  or an unreadable `@import`.
- `RMLUI`: every warning RmlUi logs loading the asset in a scratch document
  with the ui data model, as a binding expression it cannot parse or an RCSS
  property it rejects, at the line that wrote it. `block` is `rml` for the
  markup and `rcss` for the styles, with the file `line`; a warning about an
  imported stylesheet names it as `source`, with its `block_line` in that
  file. `widgets` is what only the markup with its widgets expanded shows,
  with the widget as `source` and its line in the widget's `rml` block.

## Check adapter distribution

The adapter package keeps the runtime modules and launcher in its published
file boundary. From `tools/mcp`, run `npm ci`, `npm run audit:names`, and
`npm run pack:check`. The audit uses `o200k_base` to report candidate name
token counts, token pieces, description limits, and the serialized
`tools/list` budget.

The client can also read these MCP resources:

- `chibi://project/status` and `chibi://catalog` for stable project summaries.
- `chibi://asset/{kind}/{name}` for parsed authored JSON.
- `chibi://asset/{kind}/{name}/dependencies` for dependency relationships.
- `chibi://asset/{kind}/{name}/graph/{graph_id}` for one normalized graph.
- `chibi://recording/{id}` for a generated deterministic recording.

All public tools report safety metadata through `describeContract`. They do not
provide arbitrary shell commands, network access, editor UI control, or access
to files outside the project data needed by the native controller. `buildGraph`
writes authored graph assets after validation and an expected-hash check. They are marked as modifying tools. `recordScenario`
writes only its bounded generated recording artifact under `/tmp`, and
`replayScenario` accepts only a returned
recording or a safe generated recording path.

For graph work, the agent should reason from node identity, pins, edges, calls,
effects, and diagnostics. Node comments (`comment`) are part of that record.
`inspectGraph` omits node coordinates, inline UI hints, hidden comment
bubbles, comment-box positions, and wire routing by default. Pass
`include_layout: true` when those editor details are needed. Placement is
presentation data; the graph arranger assigns coordinates after graph content
changes, so an agent does not need to choose node positions by hand.

Each inspected node also lists its pins as `in` and `out`, written as
`searchNodes` writes them (`"A: double"`, `"Then: exec"`), with the types
wildcard pins resolved to. The serialized node fields omit pins that match
their template, so these lists are where to find pin names to connect. Pass
`include_pins: false` to leave them out.

`buildPrototype` applies the same boundary to prototype assets. Components are
selected by reflected name or stable key, `add_component` uses native defaults,
`set_field` accepts JSON values through the prototype loader's type conversion,
and behavior attachments are resolved and loaded before publication. The
complete edit batch is serialized, loaded again for round-trip validation, and
published with the same expected-hash check as graph edits.

`buildScene` applies the same boundary to multi-entity fixtures. Entities are
addressed by name and reference a prototype; component overrides, retained
editor values, and behavior attachments are semantic JSON objects. The scene
loader validates every prototype, override, and behavior before publication.
Scene scenarios use the authored entity list, so a test can include a target,
enemy, and support entity without constructing a second JSON fixture by hand.

## Author a graph or shader

Use `searchAssets` to find an asset, `inspectGraph` to read its graph and
current hash, and `searchNodes` to fetch the few node signatures needed for
an edit. Submit graph changes through `buildGraph`. The server
assigns IDs, initializes typed pins, validates the resulting asset, and
automatically arranges the affected graphs. The edit API has no node coordinates
or layout controls.

`searchNodes` without a query or filter lists the domain's node types by
category. For behaviors it adds the host events, the project's event
definitions, the events its ui documents send, and the component and struct
names that Component and Type settings take. It costs about 1,300 tokens
(5.5 KB) in the bundled projects, so read it once instead of guessing names.

A query ranks entries whose name, keywords, pins or expression function start
with every word:
- Nodes, with the expression that writes them: `clamp` returns
  `{"type": "Clamp", "expression": "clamp(Value, Min, Max)", ...}`.
- Configured component nodes: `set health current` returns
  `{"type": "Set Component", "inputs": {"Component": "GameHealth"}, "field":
  "current", "in": ["Exec: exec", "Entity: ecs.entity = Self", "current: int"],
  ...}`. Pass `type` and `inputs` to `add_node`, and name the `field` in
  `inputs` or `connect`.
- Events: `on hit` returns `{"type": "Event", "event_name": "Contact Begin",
  "out": ["Then: exec", "Other: ecs.entity", "Point: CF_V2"]}`, and a project
  event also returns its `Send Event` with `reference_name`. Naming an event
  exactly ranks it first.
- UI events: each `data-event` and `data-keys` name in `content/ui` reaches the
  `GameUi` entity showing the document, so `extract` returns `{"type":
  "Event", "event_name": "Extract", "description": "Sent when the player
  clicks #extract in Bank."}`. The pause menu's events go to the player shell
  and are not listed. A UI query such as `button` that finds no event suggests authoring
  one in the ui asset. Pass the behavior you are editing as `behavior` to
  list only the events that reach it: those of the `GameUi` documents on the
  prototypes and scene objects that attach it, as Studio's Add menu does.

To extend a graph from a pin, pass its type as `wire_from` (an output you
will wire from) or `wire_to` (an input you will wire into), written as pins
read: `exec`, `float`, `GameHealth`, `array<int>`. Results keep nodes that can
take the wire, ranked like a wire dropped in Studio (exact type, then a
conversion, then a wildcard), and name the pin to connect as `wire_input` or
`wire_output`: `{"query": "destroy", "wire_from": "ecs.entity"}` returns
Destroy Entity with `"wire_input": "Entity"`.

Every result carries a one-sentence `description` of what the node or event
does, from the node's `description`, the host event table, the event asset or
the ui document.

Pins read `name: type`. Behavior search offers component nodes instead of the
generated `Get_<Component>` and `Make_<Type>` nodes they lower to. Pass
`domain: "system"` for System graphs, which use the generated nodes directly.
They offer what the System compiler turns into bytecode, the same list Studio
shows: the Tick `Event`, `Get_<Component>`, `Set_<Component>` for components
without text, `Make_<Type>`, `Break_<Type>`, Behavior Expression, Delta Time,
Random Real, and data nodes with one result. Other nodes run through the
slower flow interpreter, so they are not offered.

Graph metadata can be authored in the same request with `description`,
`owner_type`, and `graph_description`. The response echoes the selected graph
metadata. `owner_type` is useful for behavior assets that expose `Self`; graph
descriptions give functions and event graphs an authoring purpose that catalog
search can surface.

For example, these `buildGraph` arguments create a material:

```json
{
  "kind": "material",
  "name": "WarmSurface",
  "create": true,
  "operations": [
    {"op": "add_node", "ref": "color", "type": "Shader Vector3",
     "inputs": {"X": 1, "Y": 0.25, "Z": 0}},
    {"op": "connect", "from": "color", "from_pin": "Value",
     "to": "output", "to_pin": "Base Color", "kind": "data"}
  ]
}
```

New material and shape assets have an `output` node alias. New shapes start
with 32-unit half extents; use `{"op":"set_bounds","value":[32,24]}`
to set the finite positive half extents that enclose a shape. Other aliases last
only for the current request; use the stable IDs returned in `result.nodes`
for later requests. Creating a function with `add_graph` and `ref: "math"`
also exposes `math.entry` and `math.return`, so the same batch can add its
pins and connections without guessing IDs. Pin names avoid manually maintaining
serialized pin IDs.
Graph batches also support `select_graph`, `rename_graph`, `remove_graph`,
`remove_variable`, `set_pin`, `remove_pin`, `split_pin`, `recombine_pin`,
`expose_fields`, `hide_fields`, `bind_input`, `clear_binding`, `create_local`, and
`expand_local`. `create_local` on an output that already has a local renames
that local; without a `name`, a new local takes the name Studio's Name Value
gives it. `expand_local` removes the local and wires its readers to the output
again.

Studio's code actions are operations too, and `ref` names what they create:
- `collapse_to_function` and `collapse_to_macro` take `nodes` (IDs or refs) and
  a `name`, and replace the selection with a call to a new graph. The call and
  the graph both take `ref`.
- `expand_node` replaces a call to one of the asset's own functions or macros
  with a copy of its body.
- `convert_to_expression` turns `nodes` that form a connected, pure calculation
  into one Behavior Expression.
- `promote_to_variable` gives an input a Get of a new variable, or an output a
  Set; `name` defaults to the pin's name.

`add_pin`, `remove_pin` and `set_pin` on a function's or macro's Entry outputs
or Return inputs edit its signature, so every call to it follows: a renamed
input keeps its wires on each call, and a new input shows its `default`.
`set_pin.order` moves a signature pin to the index of another pin that side
declares; each call's pins follow and keep their wires, and so do the pins of
the graph's other Return nodes.
`set_comment` gives a node a comment, drawn as a bubble above it like
Unreal's node comments: `{"op":"set_comment","node":12,"comment":"Clamp before
the dash"}`. An empty `comment` removes it, and `"hidden": true` shows the
bubble only while the node is hovered; either field alone keeps the other.
Select the owning graph before referring to one of its nodes. Graph selectors
accept IDs, names, or aliases created in the same request. Function calls can
resolve `reference_name` without a numeric graph ID.

Add a Behavior Expression with `add_node` and its `source` text, for example
`{"op":"add_node","type":"Behavior Expression","source":"vec2(x, y)","ref":"motion"}`.
Use `set_expression` with the node selector and new `source` to regenerate it;
valid edits reconcile pins and disconnect incompatible wires, while invalid edits
leave the previous generated graph in place. Inputs match by name, as in
Unreal's Math Expression: a name that stays keeps its wire or binding, a name
that disappears loses it (the result lists it under `unfed_inputs`), and a new
name starts unfed unless it matches a graph local, which it then reads. To
rename an input and keep its wire, use `set_pin` with the expression input and
the new `name`; the source is rewritten to match. Bind an assignment's leftmost
identifier pin with the normal `bind_input` operation; it does not need a special
name.

For `bind_input`, choose `kind: "variable"` with a variable ID/name/alias,
`kind: "component"` with a reflected component name or stable key and optional
`field`, `kind: "delta_time"`, or `kind: "event_output"` with the source
event's `event_node_id` and `event_pin_id`. Omitting `field` binds the whole
component. Event output bindings are scoped to the event activation; Tick's
`DeltaSeconds` payload also supplies the Delta Time context automatically.
These operations use the same graph model as editor bindings and projections.

Read and write components with `Get Component`, `Set Component` and
`Has Component`, attach and detach them with `Add Component` and
`Remove Component`, and build or split structs with `Make Struct` and
`Break Struct` (`inputs: {"Type": "GameTransform"}`). Choose the component with
the `Component` input, by name or stable key, for example
`{"op":"add_node","type":"Set Component","inputs":{"Component":"GameHealth","current":5}}`.
`add_node` applies a `Component` or `Type` input before the pins it creates, so
their order in `inputs` doesn't matter. `Entity` defaults to Self; connect it to address another entity. Set writes its
`Value`, or only the field inputs it shows: naming a settable field in `inputs` or
`connect` shows it, and `split_pin`/`recombine_pin` switch between every field and
`Value`. Get exposes fields with `expose_fields`. These nodes lower to the
generated `Get_<Component>`, `Set_<Component>_<field>`, `Add_<Component>` and
`Make_<Type>` descriptors when the asset runs; see
[component nodes](component-nodes.md).

`add_node` honors `event_name`. Tick, Input, and Contact Begin events acquire
their host payload pins automatically. Declare custom payloads as variables in
an `event` asset first; adding Event, Call Event, or Send Event for that name
then applies its declaration IDs and types. Payload declarations participate in
link validation. Do not recreate their pins manually.

Use `set_output` on a constant node when a shader value has no input pins;
provide `type` for a vector or struct value so the wildcard output is resolved
before validation.
Existing assets require the exact `expected_hash` returned by inspection or
the previous edit. A stale hash returns `asset_conflict`; inspect again before
constructing a replacement batch. `create: true` refuses to overwrite an asset.

For example, this prototype edit adds a movement component, sets a reflected
field, and attaches a behavior without reproducing component defaults or JSON
field ordering:

```json
{
  "kind": "prototype",
  "name": "PhaseWarden",
  "expected_hash": "0123456789abcdef",
  "operations": [
    {"op": "add_component", "component": "GameMovement", "fields": {"speed": 55}},
    {"op": "set_field", "component": "GameHealth", "field": "maximum", "value": 30},
    {"op": "add_behavior", "behavior": "PhaseWardenBehavior"}
  ]
}
```

Pass `dry_run: true` to validate and obtain predicted hashes without saving.
A failed operation or validation leaves the authored file unchanged. Successful
batches publish one complete file, and concurrent MCP writes to the same asset
are serialized. The hash is checked again just before publication; this is
not a locking protocol shared with unrelated filesystem writers.

These are asset edits on disk, without an editor undo entry. An open editor
reloads what they change: graph assets and systems at once, prototypes and the
open scene when its runtime stops. A document with unsaved edits in the editor
keeps them and offers Reload instead. Referenced libraries are linked for validation, while
the saved file retains its authored library references.

Call `previewShader` with `{"kind":"material","name":"WarmSurface"}` to
inspect the result. It returns a PNG image and compact diagnostics, with image
bytes excluded from the accompanying structured JSON. Material and shape
previews use the production renderer in a separate native process. They
require a working graphics backend; on Linux, use a display or Xvfb. Shader
libraries are validated as reusable functions; preview a material or shape
that consumes the library to see its effect.

Requests are bounded: node search returns at most 50 entries, edit batches
contain at most 128 operations, scene edits contain at most 128 operations and
64 behavior attachments per entity. Shader previews are at most 512 by
512; scene previews accept 64–1024 pixels per side. `searchNodes` can filter by pin type, container, pin kind, and purity.
Preview time is explicit and deterministic. Pass `parameters` as an object
keyed by `Shader Parameter` names to try scalar values or exact-width vector
arrays without editing the asset. Unknown names, nonfinite values, and wrong
vector widths are rejected before drawing. Generated GLSL is available with
`include_source: true` and is limited to 64 KiB. MCP cancellation and the
preview timeout terminate the GPU child process.
Use `camera` with `center: [x, y]` and `span: [width, height]` to override
preview framing. Use `sample_grid` to request up to a 16 by 16 CPU evaluation
grid over `uv_min` and `uv_max`; each sample returns the UV coordinate and all
shader output vectors, with local positions mapped from that UV rectangle.
This separates graph behavior from production geometry or camera framing
assumptions. Materials read `beat_phase`, `beat_pulse`, and `influence` (linear
RGB at the preview center, fading out at radius 50) in both the image and the
samples, so scene-driven looks can be checked without a running scene.

`optimizeGraph` uses the same optimizer as the editor's Graph Actions menu.
Start with `dry_run: true`: it reports before and after node and edge counts,
folded inputs, removed nodes, local aliases, changed edges, and each skipped
opportunity. Safe folds include component getters, variable reads, `DeltaTime`,
`Self`, and constants when their source, consumer, conversion, fan-out, and
debug state are unambiguous. The native controller validates the optimized
clone before an apply request publishes it with `expected_hash`. With
`math_expressions: true` it first turns math trees into expressions; the result
lists each new expression's source under `expressions` and the graph's local
names under `locals`, so a dry run shows the formulas to review before applying.

The stdio adapter negotiates MCP protocol versions `2025-06-18` and
`2025-11-25`, keeps notifications response-free, and advertises the dynamic
`chibi://asset/{kind}/{name}` and
`chibi://asset/{kind}/{name}/dependencies` and
`chibi://asset/{kind}/{name}/graph/{graph_id}` and
`chibi://recording/{id}` resource templates through
`resources/templates/list`. Recording IDs are bounded opaque identifiers; the
recording resource is resolved by the controller so clients never need direct
filesystem access. The static `chibi://catalog` resource provides the
same semantic catalog as the `getAssetCatalog` tool. Tool responses include a
compact text summary for conversational clients and the complete native
response in `structuredContent`, including a `next_steps` follow-up.
The `describeContract` result labels each private native operation with the
public MCP tool or tools that expose it.

The native controller also accepts newline-delimited JSON directly:

```sh
printf '%s\n' '{"contract":1,"operation":"status"}' |
  build/chibi_ctl --project-root .
```

The repository contains two project roots: Arena3D at the repository root and
Arena2D at `projects/arena2d`, with its own manifest and `content` tree. The
MCP launcher defaults to Arena2D when that project is present, matching the
default Make and editor workflows. Set `CHIBI_ROOT` to select a different
project explicitly. Point the controller or renderer at the project root when
searching, authoring, running, or previewing assets; names are resolved only
within that project.

```sh
printf '%s\n' '{"contract":1,"operation":"status"}' |
  build/chibi_ctl --project-root projects/arena2d
```

For example, search the catalog and inspect a graph's asset dependencies:

```sh
printf '%s\n' '{"contract":1,"operation":"search_assets","query":"Health"}' |
  build/chibi_ctl --project-root .
printf '%s\n' '{"contract":1,"operation":"asset_dependencies","kind":"behavior","name":"HealthBehavior","direction":"both","transitive":true}' |
  build/chibi_ctl --project-root .
printf '%s\n' '{"contract":1,"operation":"inspect_graph","kind":"library","name":"DirectionMath","graph_name":"Direction Orientation"}' |
  build/chibi_ctl --project-root .
printf '%s\n' '{"contract":1,"operation":"inspect_system","kind":"system","name":"MovementSystem"}' |
  build/chibi_ctl --project-root .
printf '%s\n' '{"contract":1,"operation":"run_scenario","kind":"prototype","name":"ArenaPlayer","frames":60,"fixed_dt":0.0166666666666667}' |
  build/chibi_ctl --project-root .
printf '%s\n' '{"contract":1,"operation":"runtime_snapshot","kind":"prototype","name":"ArenaPlayer","frames":2,"entity_limit":32,"component_limit":16,"field_limit":32}' |
  build/chibi_ctl --project-root .
printf '%s\n' '{"contract":1,"operation":"record_scenario","kind":"prototype","name":"ArenaPlayer","frames":60,"fixed_dt":0.0166666666666667,"inputs":[{"movement":{"x":1,"y":0},"aim":{"x":0,"y":1},"actions":[{"name":"Fire","pressed":true}],"events":[{"name":"Trace"}]}]}' |
  build/chibi_ctl --project-root .
```

`runScenario` accepts prototype or scene targets, runs at most 108,000 fixed steps,
and returns the resolved asset identity, frame count, timestep, world hash, live
entity count, and engine timing fields. The fixed timestep must match the
engine's `1/60` second step. Each request creates an isolated preview runtime;
the returned world hash can be compared across repeated calls to check
deterministic behavior.

`getExecutionStatus` distinguishes availability from selection. Read the reported backend and fallback details for the selected asset. A typed
system may report generic bytecode, typed bytecode, or `sljit`; `fully_jit` is true only when the actual
Tick path selected SLJIT for the live temporary system. `jit_available` alone
means the backend exists in the build and does not mean the asset is using it.

`profileScenario` runs optional warmup frames before collecting samples. It
aggregates engine frame timing and behavior invocation and CPU counters across
all sampled frames, and returns the target's final world hash. Timing is
diagnostic data from the isolated runtime; it does not alter authored assets.
Each behavior row exposes `inclusive_cpu_ms` (including nested root events) and
`cpu_ms` (exclusive time), plus an average for each.
The result also includes `allocations`, which reports calls and requested bytes
observed through the engine's `xalloc` wrappers during sampled frames. It does
not claim to count third-party or system allocator traffic.

Recorded scenario inputs use `movement` and `aim` samples plus an `actions`
array of `{ "name": string, "pressed": boolean }` records. Action names are
resolved against the attached behavior graphs, so unknown actions are rejected
instead of being silently ignored.

`captureRuntimeSnapshot` uses the same isolated preview runtime and returns ordered
entity and component records with reflected field values, attached behavior
state, and timing data. `entity_limit`, `component_limit`, `field_limit`, and
`behavior_limit` bound the response; `fields` can restrict reflected field
selection. The operation is read-only and does not write authored content.

`runScenario` and `captureRuntimeSnapshot` accept up to 600 input changes across a
run lasting up to 30 simulated minutes. Use `inputs` for consecutive frames or
`input_timeline: [{"frame": 0, "input": {...}}, ...]` for sparse changes. Timeline
frames must be unique, increasing, and less than `frames`. Movement, aim, and
physical controls remain held between samples. Named events and UI actions
occur only at their specified frame.

Each input can contain:

- `movement: {"x": ..., "y": ...}` with both axes in [-1, 1].
- `aim: {"x": ..., "y": ..., "point": false, "firing": false}`. Direction axes
  are in [-1, 1]; `point: true` uses world coordinates within ±1,000,000.
- `controls: [1, 3]`, the held physical control codes interpreted by authored
  `GameActionBinding` components. This also determines continuous firing.
- `actions: [{"name": "Scan", "pressed": true}]`, up to 32 normalized action
  transitions. The action must exist in attached behaviors. Presses activate
  its event on GameInput owners; release permits the next press.
- `events: [{"name": "Award", "target": "ship", "values": [{"type": "int", "value": 42}]}]`,
  up to 32 events with up to 16 payload values. Types are bool, int, uint,
  float, double, name, string, or entity. Entity values resolve scene names.
- `ui: [{"target": "hud", "element": "#volume", "event": "Volume", "value": 0.5}]`,
  a player activating an element of a `GameUi` document: the optional `value`
  is stored under the `element` selector for `Get UI Value`, then the event
  the element emits is sent to the target entity, as the rendered document
  does.

Aim goes through the engine's normal input path. An `Aim` event and the old
object-shaped `actions` input are not part of this contract.

Scenario preparation registers the entire runtime prototype catalog and its
behavior dependencies before launch, including prototypes spawned by name but
not placed in the scene. `buildScene` copies prototype behaviors when
`add_entity` omits `behaviors`; an explicit empty array creates an entity without
attached behaviors. Scene/world positions and player UI layout are authored
component fields. Only graph node placement is automatic.

`recordScenario` captures at most 600 fixed steps. Recording format **v2** has
one canonical set of fields: `version`, `contract_version`, `id`, `format`,
`kind`, `name`, `content_hash`, `runtime_content_hash`, `assets`, `frames`,
`fixed_dt`, `artifact_path`, `inputs`, `saved_state`, `frame_hashes`,
`live_entities`, and `world_hash`. `assets` is the runtime dependency manifest.
Per-frame hashes include reflected world state, committed saves, visibility
memory and sound cue history. Replaying rejects changed dependencies before
execution. Old v1 recordings and duplicate field aliases were removed; record
new evidence after a contract change. Artifact IDs and paths are still bounded
and stored outside authored content.

`replayScenario` accepts exactly one returned recording or its generated
`recording_path`. It reports the first divergent frame when evidence differs.
The initial `saved_state` is restored into an isolated store for recording and
replay. No scenario reads or writes the player's real save file.

## Player-facing capabilities

These optional capabilities are available as reflected components or graph
nodes in both the editor and MCP. They contain no job, loadout, reward, or
extraction policy.

| Capability | Authoring contract |
| --- | --- |
| Camera | One enabled `GameCamera` plus `GameTransform`. Positive width/height set the logical span; offset follows the entity. Disable it to release the camera. |
| Controls | `GameActionBinding` maps one physical source to an event name. Multiple entities can bind the same action; their held states combine. `firing` also sets GameAim.firing. Remove GameInput while fitting to suspend ship controls. |
| Player UI | `GameUi` shows `content/ui/<document>.yaml` (RML markup and RCSS) over the game view. `Set UI Text`, `Set UI Class`, `Set UI Visible`, `Set UI Value` and `Focus UI Element` change every element their RCSS selector matches, such as `#score` or `#hull .fill`; `Click UI Element` clicks the first match, and `Move UI Focus` steps focus through the matches, wrapping. `Get UI Value` reads what was last set under the same selector, and an emitting input's value under `#id`. `data-event-click="emit('Resume')"` sends that event to the entity when clicked; on an input, `data-event-change` sends it with the input's value. |
| Save progress | `Read Saved Text`, `Set Saved Text`, `Remove Saved Text`, `Commit Save`, `Discard Save Changes`. Pending changes become durable only on commit. `Commit Save.Failed` supports retry/error UI. |
| Sound | `Play Sound` synthesizes a swept tone/noise cue. Frequency and end frequency: 20–10,000 Hz; duration: 0.01–4 s; volume/noise: 0–1; pan: 0 left, 0.5 center, 1 right. Editor Play and standalone Play mix up to 64 voices. A zero volume mutes a cue. |
| Lighting | `GameHRC2D` authors fractional 2D transport resolution, trace/feedback settings, ambient, intensity, radiance fog, shadow floor, falloff, temporal blending, upscaling and debug view. `GameEnvironment` authors 3D exposure and bloom. |
| Line of sight | `Raycast` returns Found, Entity, Point, Normal and Distance against circles and rotated boxes. Category filtering is independent of collision masks. New colliders and teleports are visible to the query. |
| Fog | One enabled `GameFog` defines origin, 1–256 rows/columns, cell size, color and ambient drift. `drift: 0` removes fog animation. `GameVision` sources reveal nearby cells, blocked by `occluders`. |
| Terrain memory | `GameVision.remember` adds explored terrain. Positive duration creates a shrinking temporary clearing; age starts at zero. `Visibility At` reads Visible/Explored/Obstacle. `GameRenderState.visibility_culled` opts an actor into current-sight culling; `hidden` always hides it. |
| Sensor contacts | `GameSensorContact` stores an observed position, lifetime and color, independently of explored terrain. |

RCSS `dp` units scale with the view height, 720 pixels being one pixel per
`dp`; `GameUi.order` stacks documents. UI events carry no payload, like action
events: each button emits its event with `data-event-click="emit('Name')"`,
and a behavior reads an input's value with `Get UI Value`. Behavior graphs own rules, text and history. While the pointer
is over an interactive element, it does not reach gameplay.

A ui asset reuses another as a widget with `<use ui="Meter" id="hull"/>`: a
div with the use element's attributes (`ui` becomes `data-ui`) around the
widget's markup, whose `<slot/>` holds the use element's children. A widget
element marked `attrs`, like Vue's `v-bind="$attrs"`, takes the use element's
other attributes instead, so `<use ui="MenuCheck" data-checked="save.sound">`
binds the widget's own checkbox. A widget takes data as RmlUi aliases, as Vue
components take props: `<use ui="TextField" data-alias-text="row.value"/>`
binds the widget's `text` to the row's value. Widget RCSS comes before the host's, so the
host can restyle it; the events it emits reach the host's entity, and
`#hull .fill` selects inside one instance. Editing a widget reloads every
document that uses it.

A scene's UI shows on a page, as a web page's content shows in its root
layout: the scene's root document, named by the scene's `ui:`, or by the
project's `ui:` in `project.yaml` for scenes that name none. A root document
is a ui asset whose RCSS styles the whole page unscoped, so it holds the
tokens and shared control classes widgets use, usually by
`@import "theme/tokens.rcss";` of stylesheets beside it (in order, once
each; an edit to any of them restyles). Its markup is the page body, which
it may wrap in `<body ...>` to set the body's attributes, and the scene's
GameUi documents show in its `<slot/>`, or after its markup. Tokens follow
shadcn/ui: `--background`, `--foreground`, `--primary`, `--muted-foreground`,
`--border` and the rest, light on `:root` and dark on `.dark`, so a shadcn
theme's two variable blocks paste in as they are (`:root` names `body`);
Studio's `StudioShell` binds the class, as next-themes sets it:
`<body data-class-dark="save.theme == 'dark' || (save.theme == 'system' && system.dark)">`.
All RCSS, in root documents and ui assets alike, takes Sass-style nested
rules with `&` (a nested `@media` wraps its rules) and `//` comments, and
colors take CSS `color-mix(in srgb | oklab, a [p%], b [p%])`, so shades
derive from tokens: `color-mix(in oklab, var(--accent) 20%, transparent)`.
The Studio scene's root document styles the editor itself, so Studio's
panels look the same in the editor, in the Scene and in a prototype
preview, which shows on the open scene's page. Prototype previews show a
`GameUi` even while it is hidden.

Each asset's RCSS applies only inside it: rules are scoped to the element
showing the asset (`data-ui="<name>"`), and `body` in a selector names that
element, so `body.paused #title` styles a state class set on it. Game UI lets
the pointer through to the game except over `data-event-click` elements and inputs;
set `pointer-events: auto` where a document should block it, such as a menu's
backdrop.

`data-keys="Enter:Choose Down:Next"` sends the named event when that key is
pressed while focus is on the element or inside it, and stops the key there.
Keys are A-Z, 0-9, Space, Enter, Escape, Tab, Backspace, Delete, Up, Down,
Left, Right, Home, End, PageUp and PageDown. A document focuses its first
`autofocus` element when it is shown. RmlUi gives `:focus` to every element on
the way to the focused one, so pick the focused row with a class, such as
`.row:focus`.

Documents bind data with RmlUi's data bindings, which follow Vue: `{{ save.name }}`
in text, `data-for="item : list"`, `data-if`, `data-class-active="expr"`,
`data-attr-src="expr"`, `data-value="save.volume"` on an input (`data-value-lazy`
sets it on Enter or blur, as Vue's `v-model.lazy`),
`data-checked="save.theme"` on a radio with a `value` or on a checkbox, and
`data-event-click="expr"`, where `emit('Name')` sends an event to the entity.
`save` holds the world's saved keys (the store of `Read Saved Text`) as text,
`true` and `false` as booleans; a key nothing saved reads empty until a
binding sets it, and changes stay pending until a behavior commits the save.
As in HTML, radios group only by `name`.

The player shows `content/ui/PauseMenu.yaml` while paused. Its `body` has the
class `paused`, `cleared` or `defeated`, and its `Resume`, `Restart` and `Quit`
events control the session.

Control codes: mouse left=1, mouse right=2, Space=3, Shift=4, Tab=5, Enter=6,
A–Z=7–32, pad A/B/X/Y=33–36, shoulders=37/38, triggers=39/40, aim stick=41.
The editor shows named choices. Existing Arena controls are migrated to
components in its player prototype and scene.

Save stores allow 1,024 namespaced keys of 1–256 bytes, values up to 65,536
bytes, and 1 MiB total text. Standalone native Play uses the platform's
`chibi/game/profiles.json` user-data path and atomic file replacement; Web Play
uses browser localStorage. Namespace keys for each game. Editor previews keep
committed data only for that live world; scenarios accept and return
`saved_state` for explicit cross-run tests. Export contains committed values
only. Restart discards pending values. Store unlock choices at extraction and
leave run inventory in ordinary variables/components. Saved settings can drive
sound volume, effect intensity, camera displacement and animation amounts.

The web 2D renderer uses the authored ambient and emission gains directly; indirect lighting, light haze and shadow-floor tuning require the native radiance renderer.

Use `previewScene` with a scene/prototype name, optional frames, inputs and
saved state to inspect a running result. It uses the same 2D/3D renderer,
authored camera and UI documents, with pixel dimensions independent of display DPI.
This tool needs GPU/display access. Returned images include the UI; structured
JSON omits the base64 bytes. For visual fixtures, explicitly author material
emission rather than relying on the emissive appearance fallback.

Catalog, search, and asset-list responses accept a numeric `limit` and `cursor`,
and return `total`, `count`, `has_more`, and `next_cursor`. Catalog and search
also accept `summary: true` to omit relationship arrays from each asset record.

The launcher selects the Arena2D project by default when it is present and
honors an explicit `CHIBI_ROOT`. It sets the controller path from its own
location. When launching `server.mjs` directly outside the repository, set
`CHIBI_ROOT` or `CHIBI_CTL` to an explicitly built controller executable.

`make test` includes the MCP protocol and native-controller smoke checks,
including `design-smoke.mjs`, which builds independent fixtures entirely through
MCP. `CHIBI_KEEP_DESIGN_FIXTURE=1` retains them; `CHIBI_MCP_GPU=1` also renders
2D/3D fog, fitting and dock previews. `--game-capabilities` runs native save,
visibility and sound checks. Set
`CHIBI_VISIBILITY_BENCH=1` for the moving-observer visibility benchmark.
Custom build checks use that build's controller and render-controller binaries.

The adapter is intentionally local and bounded. Semantic asset edits, runtime
inspection, deterministic recordings, and shader previews share the native
controller contract without adding arbitrary shell or file access.

### Graph optimization

`optimizeGraph` reports proven binding folds, graph locals, cleanup, and skipped
candidates. It defaults to a dry run. Apply uses `dry_run: false` and the current
`expected_hash`; graph selection uses `graph_id` or `graph_name`. The fanout
threshold defaults to three. Locals preserve source evaluation and never add
persistent behavior variables. See [graph optimizer](graph-optimizer.md) for
representation, safety rules, and validation.
