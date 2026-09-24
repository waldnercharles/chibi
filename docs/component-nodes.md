# Component nodes

Behavior graphs read and write reflected components with three nodes: **Get Component**, **Set Component** and **Has Component**. They replace the generated per-component nodes in the add-node catalog. The generated descriptors remain, and every component node lowers to them before anything evaluates, compiles or debugs the graph.

The implementation is in `graph/nodes/component_nodes.{h,c}`.

## The nodes

| Node | Inputs | Outputs |
| --- | --- | --- |
| Get Component | Component, Entity | Value, plus the fields it exposes |
| Set Component | Exec, Component, Entity, Value or field inputs | Then, Value |
| Has Component | Component, Entity | Value (bool) |

- **Component** is a setting (`PIN_FLAG_SETTING`, `PIN_NAME`) that holds the component's stable key. Setting it with `graph_set_pin_default` retypes the Value pins. It also drops field pins, plus any wires the new type can't carry. Tools may pass the component name; the stored value is always the key.
- **Entity** defaults to Self (`PIN_FLAG_DEFAULT_SELF`). A wire, a binding or a literal handle makes the node address that entity instead.
- **Get** shows only the fields it uses as exposed outputs beside Value (the existing Show/Hide unused fields).
- **Set** writes its whole Value, or only the field inputs it shows. `component_node_show_field` adds or removes one field input. Showing a field replaces Value, and hiding the last field restores it.
  - A field can be shown when it's top-level and has a generated setter. That excludes strings and structs that contain strings.
  - `graph_split_pin` and `graph_recombine_pin` on a component node show every settable field, or restore Value.

## Lowering

`graph_lower_component_nodes` replaces each node in place and keeps node and pin IDs. Breakpoints, watches, flow traces and preview values therefore map back to the authored node.

| Component node | Entity | Lowers to |
| --- | --- | --- |
| Get | Self | `Get_<C>` |
| Get | another entity | `GetEntity_<C>` |
| Has | Self | `Has_<C>` |
| Has | another entity | `HasEntity_<C>` |
| Set, Value | Self | `Set_<C>` |
| Set, Value | another entity | `SetEntity_<C>` |
| Set, fields f1..fn | either | `Set_<C>_f1` → … → `Set_<C>_fn` |

Field setters are chained in field order:
- The first keeps the node ID and Exec. The last keeps Then, Value and the exposed Value fields.
- Each field input keeps its pin ID on the setter that writes it.
- An Entity wire fans out to every Target.

A Set with one field is exactly the old field setter. With several fields, the setters after the first have new node IDs. Breakpoints and incoming wires map to the first setter, and output watches belong to the last, so the debugger doesn't show a multi-field Set's output watches.

Lowering runs where authored assets become runnable:
- At the end of `behavior_asset_link_libraries`. Game loads, scene apply and preview, MCP previews and the editor's compile command all go through it.
- In editor dispatch previews, on the preview copy.
- In `eval_view`, which links an asset that holds component nodes. The graph's event status icons read the program `eval_view_graph` returns.

`behavior_asset_compile` validates a lowered copy, so authoring tools report real diagnostics, and a lowering failure such as "Choose a component for Get Component" becomes a compile error. `behavior_program_publish` refuses an asset that still holds component nodes, and so does instance creation on the interpreter backend.

The optimizer folds a Get Component that reads Self exactly like `Get_<C>`. A read of another entity stays a node. Libraries accept component nodes only with an explicit entity.

## Existing assets: rendered, not migrated

Old graphs keep their generated nodes, and the editor presents them in the new vocabulary. Loading doesn't migrate them. The reasons:
- A generated node is exactly what the corresponding component node lowers to, so it's valid and runs unchanged. Migrating on load would have pushed every loaded asset, test fixture and MCP-built graph through lowering for no runtime difference.
- Migration would have rewritten 53 assets and every test that builds a generated node.
- The presentation is shared. `ed_node_title` names both forms `Get Transform`, `Set Velocity` or `Has Health`. A generated field setter labels its Value and Target pins with the field name and `Entity`, which is how Set Component shows a field.
- Search only creates component nodes, and so does "Convert to getter" on a binding: Get Component showing that field, instead of `Get_<C>` plus `Break_<C>`.

`component_node_raise` converts one generated node into its component node. `test_suite --component-nodes` proves the two forms interchangeable by raising every generated access node in project content, saving and loading the result, lowering it, and comparing it with the original. `SpaceCadet/BumperKick` and `arena2d/MaterialImpactHealth` were converted this way and use the new nodes.

## Documents

The Component setting implies each Value pin's type, so documents write only the key, and the field inputs a Set shows:

```yaml
- id: 4
  type: Get Component
  inputs: {Component: GAME.GameVelocity}
  outputs:
    Value.x: {}
- id: 10
  type: Set Component
  inputs:
    Component: game.GameTransform
    x: {id: 18}
- id: 19
  type: Set Component
  inputs:
    Component: game.GameHealth
    Value:
      default: {current: 5, maximum: 9}
```

Loading prepares a component node from its key and field names before it applies pin entries, and writing compares pins with the same shapes. An unknown key fails to load, like an unknown node type.

## Search

`ed_search_matches` lists the three nodes and leaves out generated access nodes, except in System graphs, whose compact nodes still use them. A non-empty query also offers presets:
- Get, Set and Has for each component.
- Set for each settable field, such as `Set Velocity X`.

A preset spawns the component node already configured. Its `ed_search_match.component` and `.field` carry the preset, and wire compatibility uses the configured pin types.

Browsing the catalog of a behavior graph owned by an entity, measured by `test_suite --editor-graph`:

| | Before | After |
| --- | --- | --- |
| Entries with an empty query | 1,112 | 419 |
| Whole-component Get/Set/Has variants | 350 | 3 component nodes |
| Field setters | 346 | 0 (presets on query) |
| Add/Remove variants | 116 | 116 |
| Make/Break | 129 | 129 |

This is the test registry, which adds a few test-only nodes to the application's. For the 58 game components, queries can find 519 presets: 3 per component plus 345 field presets.

## Editors and tools

- **ImGui editor:**
  - The Details panel shows a Component choice, fed by the host's component catalog through `ed_pin_choices`.
  - The node menu's **Write** submenu toggles Value and each settable field.
  - **Fields** shows or hides Get's outputs.
- **RmlUi graph editor:**
  - It renders component nodes through `ed_search_matches`, `ed_spawn_match_at`, `ed_node_title` and the graph model.
  - Its Properties panel can offer the Component setting through `ed_pin_choices`. The choices are a name catalog, so the selected item compares `editor_choice.name` with the pin's string, and the choice commits with `graph_set_pin_default`.
  - Write toggles call `component_node_show_field` and `component_node_split_value`.
- **MCP:**
  - `add_node` with `type: "Get Component"` and `inputs: {"Component": "GameHealth"}` configures a node.
  - Naming a settable field as a Set Component input, in `inputs` or `connect`, shows that field, and `remove_pin` on a field input hides it.
  - `split_pin` and `recombine_pin` switch between the fields and Value.
  - Other pin edits (`add_pin`, `set_pin`, `remove_pin` on fixed pins) are refused, because the Component setting determines the pins.

## Follow-ups

- Add/Remove Component could take the same Component setting and replace 116 more entries.
- Make and Break could become split input and output pins on any struct, as the graph-language research proposes.
